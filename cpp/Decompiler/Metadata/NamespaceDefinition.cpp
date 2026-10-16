// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The namespace-definition cache implementation (see the header for the
// contract and the decompiled-C# algorithm notes). The build reads only the
// public MetadataFile raw surface, so this translation unit carries no winmd
// state.

#include "Decompiler/Metadata/NamespaceDefinition.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The build-time builder (the C# NamespaceDataBuilder): the mutable node the
// populate/merge/link phases reshape before freezing. Children are stored as
// raw handle values (the frozen node's public shape), everything else by
// value.
struct Builder {
    std::uint32_t handleValue = 0;
    std::string simpleName;
    std::string fullName;
    std::uint32_t parentHandle = 0;
    bool hasParent = false;
    std::vector<std::uint32_t> children;
    std::vector<std::uint32_t> types;
    std::vector<std::uint32_t> exported;
};

// The C# `TypeAttributesExtensions.IsNested(flags)` over the raw Flags
// column: `(flags & NestedFamANDAssem) != 0`. NestedFamANDAssem is 0x6, so
// the test is true for the nested visibilities 2..7 (any of bits 1/2 set)
// and false for NotPublic(0)/Public(1) -- the decompiled .NET 10 bit trick.
bool FlagsAreNested(std::uint32_t flags) {
    return (flags & 0x00000006u) != 0;
}

// The C# `GetSimpleName(handle, segmentIndex)` string-level equivalent: hop
// one '.' per index step (a step with no further '.' stops the hops, the
// decompiled IndexOfRaw NUL stop), then read to the next '.' or the end. With
// segmentIndex larger than the segment count this yields the LAST segment
// (the hops stop early and the read runs to the end), which is exactly the
// `int.MaxValue` call the real-namespace population makes.
std::string SegmentAt(const std::string& fullName, std::size_t segmentIndex) {
    std::size_t pos = 0;
    for (std::size_t i = 0; i < segmentIndex; i++) {
        std::size_t dot = fullName.find('.', pos);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    std::size_t end = fullName.find('.', pos);
    return fullName.substr(pos, end == std::string::npos
        ? std::string::npos : end - pos);
}

// The last '.'-delimited segment of a full name (the whole name when it
// carries no dot, "" for the empty name) -- GetSimpleName with the
// unbounded index.
std::string LastSegment(const std::string& fullName) {
    return SegmentAt(fullName, static_cast<std::size_t>(-1));
}

// The number of '.' characters of a name (the C# `AsSpan().Count('.')`).
std::size_t DotCount(const std::string& name) {
    return static_cast<std::size_t>(
        std::count(name.begin(), name.end(), '.'));
}

// The build state shared by the populate/merge/link phases (the C# locals of
// PopulateNamespaceTable, hoisted so the phase helpers stay one screen each).
struct BuildState {
    const MetadataFile* file = nullptr;
    std::vector<Builder> bs;
    // handle Value -> builder index (repointed by the duplicate merge); the
    // C# Dictionary<NamespaceDefinitionHandle, NamespaceDataBuilder> with
    // its add-only insertion-order enumeration.
    std::unordered_map<std::uint32_t, std::size_t> byHandle;
    std::vector<std::uint32_t> handleOrder;
    // full name -> FIRST builder index (the merge keeps the first); the C#
    // stringTable with its insertion-order enumeration.
    std::unordered_map<std::string, std::size_t> byName;
    std::vector<std::size_t> byNameOrder;
    std::vector<std::size_t> virtuals;
    std::uint32_t virtualCounter = 0;
};

// The C# `LinkChildDataToParentData`.
void LinkChildDataToParentData(BuildState& s, std::size_t child,
                               std::size_t parent) {
    s.bs[child].hasParent = true;
    s.bs[child].parentHandle = s.bs[parent].handleValue;
    s.bs[parent].children.push_back(s.bs[child].handleValue);
}

// The C# `LinkChildToParentNamespace`: walk one namespace up at a time from
// a real namespace, linking each level to an existing real parent, an
// existing virtual parent, or a freshly synthesized one.
void LinkChildToParentNamespace(BuildState& s, std::size_t realChild) {
    std::string fullName = s.bs[realChild].fullName;
    std::size_t child = realChild;
    while (true) {
        std::size_t dot = fullName.rfind('.');
        std::string parentName;
        if (dot == std::string::npos) {
            if (fullName.empty()) return;  // the root itself: no parent
            parentName = "";
        } else {
            parentName = fullName.substr(0, dot);
        }
        auto it = s.byName.find(parentName);
        if (it != s.byName.end()) {
            LinkChildDataToParentData(s, child, it->second);
            return;
        }
        for (std::size_t vi : s.virtuals) {
            if (s.bs[vi].fullName == parentName) {
                LinkChildDataToParentData(s, child, vi);
                return;
            }
        }
        // The C# `SynthesizeNamespaceData(parentName, realChild.Handle)`:
        // the virtual's simple name is the segment at the parent name's dot
        // count of the real child's full name (the parent name is a prefix of
        // it, so the segment is the parent's own last segment).
        Builder v;
        v.handleValue = 0x80000000u | ++s.virtualCounter;
        v.fullName = parentName;
        v.simpleName = SegmentAt(s.bs[realChild].fullName,
                                  DotCount(parentName));
        s.bs.push_back(std::move(v));
        std::size_t vi = s.bs.size() - 1;
        s.virtuals.push_back(vi);
        LinkChildDataToParentData(s, child, vi);
        fullName = parentName;
        child = vi;
    }
}

} // namespace

NamespaceCache::NamespaceCache(const MetadataFile* file) : file_(file) {}

const NamespaceDefinition& NamespaceCache::GetRootNamespace() const {
    EnsureBuilt();
    // The root handle (the full-name offset 0) is always the first inserted
    // table key, and its builder is never merged away (later duplicate-full-
    // name builders merge INTO it), so it resolves to node 0.
    return nodes_[handles_.front().second];
}

const NamespaceDefinition& NamespaceCache::GetNamespaceData(
    NamespaceDefinitionHandle handle) const {
    EnsureBuilt();
    for (const auto& entry : handles_) {
        if (entry.first == handle.Value)
            return nodes_[entry.second];
    }
    // The C# `Throw.InvalidHandle()` -- BadImageFormatException with the
    // exact SR message; the port's std::out_of_range convention for it.
    throw std::out_of_range("Invalid handle.");
}

std::string NamespaceCache::GetFullName(
    NamespaceDefinitionHandle handle) const {
    return GetNamespaceData(handle).FullName;
}

std::vector<NamespaceDefinitionHandle>
NamespaceCache::GetHandlesInTableOrder() const {
    EnsureBuilt();
    std::vector<NamespaceDefinitionHandle> result;
    result.reserve(handles_.size());
    for (const auto& entry : handles_)
        result.push_back(NamespaceDefinitionHandle{entry.first});
    return result;
}

void NamespaceCache::EnsureBuilt() const {
    if (built_) return;

    // The C# PopulateNamespaceTable body, phase by phase.
    BuildState s;
    s.file = file_;
    try {
        // The root (the C# `NamespaceDefinitionHandle.FromFullNameOffset(0)`
        // builder added before any table walk; its Name and FullName are the
        // empty string at heap offset 0).
        {
            Builder root;
            root.handleValue = 0;
            root.simpleName = "";
            root.fullName = "";
            s.bs.push_back(std::move(root));
            s.byHandle.emplace(0u, 0u);
            s.handleOrder.push_back(0u);
        }

        // PopulateTableWithTypeDefinitions + PopulateTableWithExportedTypes:
        // every non-nested row lands in the namespace named by its namespace
        // column's #Strings offset. A namespace shared by a typedef and an
        // exported type accumulates both (one builder per offset).
        auto addEntity = [&s](std::uint32_t nsOffset, std::uint32_t token,
                              bool exported) {
            auto it = s.byHandle.find(nsOffset);
            if (it != s.byHandle.end()) {
                if (exported)
                    s.bs[it->second].exported.push_back(token);
                else
                    s.bs[it->second].types.push_back(token);
                return;
            }
            std::string fullName = s.file->CorString(nsOffset);
            Builder b;
            b.handleValue = nsOffset;
            b.simpleName = LastSegment(fullName);
            b.fullName = fullName;
            if (exported)
                b.exported.push_back(token);
            else
                b.types.push_back(token);
            s.byHandle.emplace(nsOffset, s.bs.size());
            s.handleOrder.push_back(nsOffset);
            s.bs.push_back(std::move(b));
        };

        std::uint32_t typeDefRows =
            s.file->CorTableRowCount(CorTableIndex::TypeDef);
        for (std::uint32_t row = 0; row < typeDefRows; row++) {
            std::uint32_t flags = s.file->CorTableColumnValue(
                CorTableIndex::TypeDef, row, 0);
            if (FlagsAreNested(flags)) continue;
            std::uint32_t nsOffset = s.file->CorTableColumnValue(
                CorTableIndex::TypeDef, row, 2);
            addEntity(nsOffset, (0x02u << 24) | (row + 1), false);
        }
        std::uint32_t exportedRows =
            s.file->CorTableRowCount(CorTableIndex::ExportedType);
        for (std::uint32_t row = 0; row < exportedRows; row++) {
            // The nested-forwarder skip: Implementation.Kind ==
            // HandleKind.ExportedType is tag 2 of the coded index.
            std::uint32_t implementation = s.file->CorTableColumnValue(
                CorTableIndex::ExportedType, row, 4);
            if ((implementation & 0x3u) == 2) continue;
            std::uint32_t nsOffset = s.file->CorTableColumnValue(
                CorTableIndex::ExportedType, row, 3);
            addEntity(nsOffset, (0x27u << 24) | (row + 1), true);
        }

        // MergeDuplicateNamespaces: iterate the handle table in insertion
        // order (the C# add-only Dictionary's entries order); a builder whose
        // full name was already taken merges into the FIRST builder -- its
        // lists are appended after the first builder's own -- and its handle
        // key is repointed there.
        for (std::uint32_t hv : s.handleOrder) {
            std::size_t bi = s.byHandle[hv];
            auto it = s.byName.find(s.bs[bi].fullName);
            if (it != s.byName.end()) {
                Builder& dst = s.bs[it->second];
                Builder& src = s.bs[bi];
                dst.types.insert(dst.types.end(), src.types.begin(),
                                src.types.end());
                dst.exported.insert(dst.exported.end(), src.exported.begin(),
                                    src.exported.end());
                s.byHandle[hv] = it->second;
            } else {
                s.byName.emplace(s.bs[bi].fullName, bi);
                s.byNameOrder.push_back(bi);
            }
        }

        // ResolveParentChildRelationships: link every real namespace (in the
        // string-table insertion order, root first) into its parent chain,
        // synthesizing virtual namespaces for the parent levels that carry
        // no rows of their own.
        for (std::size_t bi : s.byNameOrder)
            LinkChildToParentNamespace(s, bi);

        // Freeze: the nodes keep the builder order (root, the real
        // namespaces in encounter order, then the virtuals in synthesis
        // order -- the C# dictionary2 enumeration order), and the handle map
        // keys follow the handle table's insertion order with the merged
        // keys repointed at the surviving builder.
        nodes_.resize(s.bs.size());
        for (std::size_t i = 0; i < s.bs.size(); i++) {
            NamespaceDefinition& n = nodes_[i];
            n.Name = std::move(s.bs[i].simpleName);
            n.FullName = std::move(s.bs[i].fullName);
            n.Parent = NamespaceDefinitionHandle{
                s.bs[i].hasParent ? s.bs[i].parentHandle : 0u};
            n.NamespaceDefinitions.reserve(s.bs[i].children.size());
            for (std::uint32_t hv : s.bs[i].children)
                n.NamespaceDefinitions.push_back(
                    NamespaceDefinitionHandle{hv});
            n.TypeDefinitions = std::move(s.bs[i].types);
            n.ExportedTypes = std::move(s.bs[i].exported);
        }
        handles_.reserve(s.handleOrder.size() + s.virtuals.size());
        for (std::uint32_t hv : s.handleOrder)
            handles_.emplace_back(hv, s.byHandle[hv]);
        // The C# appends the virtual handles to the frozen table after the
        // real-handle entries (dictionary2.Add in the virtualNamespaces
        // loop), in synthesis order.
        for (std::size_t vi : s.virtuals)
            handles_.emplace_back(s.bs[vi].handleValue, vi);
    } catch (const std::exception&) {
        // A corrupt column or string-heap read mid-build degrades to the
        // root-only tree (the never-throw surface convention; the C# builds
        // inside the reader, where a BadImageFormatException never reaches
        // these accessors).
        nodes_.clear();
        handles_.clear();
        NamespaceDefinition root;
        nodes_.push_back(std::move(root));
        handles_.emplace_back(0u, 0u);
    }
    built_ = true;
}

} // namespace ILSpy::Decompiler::Metadata
