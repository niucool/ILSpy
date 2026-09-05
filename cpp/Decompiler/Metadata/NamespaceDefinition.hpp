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

// The namespace-definition tree: the port's stand-in for System.Reflection
// .Metadata's NamespaceCache / NamespaceDefinition / NamespaceDefinitionHandle
// (the MetadataReader.GetNamespaceDefinitionRoot / GetNamespaceDefinition /
// GetString(NamespaceDefinitionHandle) surface the C# MetadataNamespace /
// MetadataModule walk consumes). SRM computes the tree lazily over the TypeDef
// and ExportedType tables; this port reproduces the decompiled .NET 10
// NamespaceCache algorithm over the winmd-backed raw column reads:
//   - every non-nested TypeDef row and every non-nested-forwarder ExportedType
//     row lands in the namespace named by its namespace column (a #Strings
//     heap offset -- the "full-name handle"; two rows in the same namespace
//     normally share one offset, so one builder accumulates both);
//   - duplicate full names at DIFFERENT heap offsets merge into the first
//     builder (its lists first, the later builders' appended);
//   - parent namespaces that carry no rows of their own are synthesized as
//     "virtual" namespaces (the 0x80000000|index handles), linked level by
//     level up to the nearest real ancestor or the root;
//   - the root is the full-name offset 0 (the empty string), which doubles as
//     the NIL handle value.
//
// SRM guards the lazy build with a lock; the port is single-threaded like the
// rest of the MetadataFile lazy caches (the typeLookup built-flag precedent),
// the documented divergence. A corrupt string-heap read during the build
// degrades to the root-only tree instead of propagating the C# BadImageFormat
// Exception (the never-throw surface convention the MetadataFile lookups
// carry); GetNamespaceData on an unknown handle throws std::out_of_range with
// SRM's exact "Invalid handle." message.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;
class NamespaceCache;

// The C# `NamespaceDefinitionHandle`: an opaque namespace key -- either a
// #Strings full-name heap offset (raw offset bits), or a synthesized virtual
// index with the high bit set. The root's full-name offset 0 is also the nil
// value (IsNil is true for the root handle), a C# quirk this port carries.
struct NamespaceDefinitionHandle {
    // The raw C# `_value`: the heap offset, or 0x80000000|index for virtual
    // handles, or 0 for the nil/root value.
    std::uint32_t Value = 0;

    // The C# `IsNil` -- only the root's 0 offset (the empty full name).
    bool IsNil() const { return Value == 0; }

    // The C# `IsVirtual` -- a synthesized intermediate namespace.
    bool IsVirtual() const { return (Value & 0x80000000u) != 0; }

    // The C# `GetHeapOffset` -- the #Strings full-name offset (raw offset
    // bits, so 0 for nil and virtual handles alike).
    std::uint32_t HeapOffset() const { return Value & 0x1FFFFFFFu; }

    // The C# `FromFullNameOffset` factory.
    static NamespaceDefinitionHandle FromFullNameOffset(
        std::uint32_t stringHeapOffset) {
        return NamespaceDefinitionHandle{stringHeapOffset};
    }

    // The C# `FromVirtualIndex` factory (the pre-incremented counter: the
    // first virtual namespace carries index 1).
    static NamespaceDefinitionHandle FromVirtualIndex(
        std::uint32_t virtualIndex) {
        return NamespaceDefinitionHandle{0x80000000u | virtualIndex};
    }
};

inline bool operator==(const NamespaceDefinitionHandle& a,
                       const NamespaceDefinitionHandle& b) {
    return a.Value == b.Value;
}

inline bool operator!=(const NamespaceDefinitionHandle& a,
                       const NamespaceDefinitionHandle& b) {
    return !(a == b);
}

// The C# `NamespaceDefinition` (over the internal NamespaceData): one frozen
// namespace node. The C# `Name` is a dot-terminated StringHandle the caller
// resolves through the reader; this port stores the resolved string (the
// SRM GetSimpleName last-segment result). The type/exported-type lists hold
// the raw 0x02....../0x27...... tokens (the port's raw-token convention), in
// the order the cache walk appended them.
struct NamespaceDefinition {
    std::string Name;
    std::string FullName;
    NamespaceDefinitionHandle Parent;
    std::vector<NamespaceDefinitionHandle> NamespaceDefinitions;
    std::vector<std::uint32_t> TypeDefinitions;
    std::vector<std::uint32_t> ExportedTypes;
};

// The C# `NamespaceCache`: the per-reader lazy tree. One instance hangs off
// the MetadataFile implementation and is built on first use.
class NamespaceCache {
public:
    // Reads the tables through the public MetadataFile raw surface (the
    // CorTableRowCount / CorTableColumnValue / CorString reads), so the cache
    // carries no winmd state of its own.
    explicit NamespaceCache(const MetadataFile* file);

    // The C# `GetRootNamespace`.
    const NamespaceDefinition& GetRootNamespace() const;

    // The C# `GetNamespaceData` -- the node a handle maps to; throws
    // std::out_of_range ("Invalid handle.") for a handle that is not in the
    // table (the C# BadImageFormatException arm). The nil handle and the
    // root's full-name offset are the same key: both return the root.
    const NamespaceDefinition& GetNamespaceData(
        NamespaceDefinitionHandle handle) const;

    // The C# `GetFullName` (the public MetadataReader.GetString(Namespace
    // DefinitionHandle)): the full name of the node a handle maps to.
    std::string GetFullName(NamespaceDefinitionHandle handle) const;

    // Every handle key of the handle->node table, in table insertion order
    // (root first, then the TypeDef-table encounter order, then the
    // ExportedType-table encounter order, then the virtual handles in
    // synthesis order). The C# surface exposes no equivalent; this accessor
    // stands in for reflecting the private _namespaceTable the gold probe
    // dumps, so the port tests can pin the map exactly.
    std::vector<NamespaceDefinitionHandle> GetHandlesInTableOrder() const;

private:
    void EnsureBuilt() const;

    const MetadataFile* file_;

    // The frozen nodes in table order: root, the real namespaces in
    // encounter order, then the virtual namespaces in synthesis order.
    // Mutable behind the const accessors: the cache builds lazily on first
    // use (the C# lock-guarded lazy build's single-threaded equivalent, the
    // MetadataFile typeLookup built-flag precedent).
    mutable std::vector<NamespaceDefinition> nodes_;

    // The handle->node map entries in table insertion order (the pair's
    // first element is the handle's raw Value, the second the nodes_ index).
    mutable std::vector<std::pair<std::uint32_t, std::size_t>> handles_;

    mutable bool built_ = false;
};

} // namespace ILSpy::Decompiler::Metadata
