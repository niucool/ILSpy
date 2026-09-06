// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of MethodSemanticsLookup -- see the header for the port
// conventions. The single-pass build (convention (c)) reproduces the C#
// per-association GetAccessors walk: SRM's exact-value switch keyed on the
// association kind, the LAST matching row winning each slot (nil methods
// included, convention (d)), then the per-association AddEntry loop in the C#
// iteration order (properties first, then events).

#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The raw ECMA II.22.28 MethodSemantics flag values (the exact switch arms of
// SRM's PropertyDefinition/EventDefinition.GetAccessors).
constexpr std::uint32_t kSetter = 0x1;
constexpr std::uint32_t kGetter = 0x2;
constexpr std::uint32_t kAdder = 0x8;
constexpr std::uint32_t kRemover = 0x10;
constexpr std::uint32_t kRaiser = 0x20;

}  // namespace

const ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes
    MethodSemanticsLookup::CSharpAccessors =
        ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::Getter
        | ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::Setter
        | ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::Adder
        | ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::Remover;

MethodSemanticsLookup::MethodSemanticsLookup(const MetadataFile& file,
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes filter)
{
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    if ((filter & TS::MethodSemanticsAttributes::Other) != TS::MethodSemanticsAttributes::None) {
        // The C# `throw new NotSupportedException("SRM doesn't provide access
        // to 'other' accessors")` (convention (b)).
        throw std::logic_error(
            "NotSupportedException: SRM doesn't provide access to 'other' accessors");
    }

    // The SRM GetAccessors slot semantics as one linear pass: for each
    // association (a Property 0x17...... or Event 0x14...... token), the LAST
    // row whose flags column carries the association kind's EXACT arm value
    // wins the slot. A row whose flags match the OTHER kind's arm (a Getter
    // row on an event, an Adder row on a property) or carry a combined or
    // unknown value matches NO arm (the SRM switches have no default case).
    // The slot keeps the row even when its method column is nil (row 0) --
    // convention (d): the nil check happens at the AddEntry level below.
    struct AssociationSlots {
        std::uint32_t primary = 0;    // property Getter / event Adder
        std::uint32_t secondary = 0;  // property Setter / event Remover
        std::uint32_t tertiary = 0;   // event Raiser
    };
    std::unordered_map<std::uint32_t, AssociationSlots> slots;
    for (const auto& row : file.MethodSemanticsRows()) {
        if (row.AssociationToken == 0)
            continue;  // a nil association matches no iterated handle
        std::uint32_t methodRow = row.MethodToken & 0x00FFFFFFu;
        AssociationSlots& s = slots[row.AssociationToken];
        switch (row.AssociationToken >> 24) {
            case 0x17:  // Property: the Getter/Setter arms only
                if (row.RawSemantics == kGetter)
                    s.primary = methodRow;
                else if (row.RawSemantics == kSetter)
                    s.secondary = methodRow;
                break;
            case 0x14:  // Event: the Adder/Remover/Raiser arms only
                if (row.RawSemantics == kAdder)
                    s.primary = methodRow;
                else if (row.RawSemantics == kRemover)
                    s.secondary = methodRow;
                else if (row.RawSemantics == kRaiser)
                    s.tertiary = methodRow;
                break;
            default:
                break;
        }
    }

    // The C# AddEntry loop, in the C# iteration order: every property row
    // (Getter then Setter), then every event row (Adder, Remover, Raiser).
    // AddEntry drops the kinds the filter excludes and nil methods (the
    // slot's 0 value -- which also carries the last-row-wins nil case,
    // convention (d)).
    const std::uint32_t filterMask = static_cast<std::uint32_t>(filter);
    auto addEntry = [&](TS::MethodSemanticsAttributes semantics,
                        std::uint32_t methodRow, std::uint32_t associationToken) {
        if ((static_cast<std::uint32_t>(semantics) & filterMask) == 0 || methodRow == 0)
            return;
        Entry entry;
        entry.Semantics = semantics;
        entry.MethodRowNumber = methodRow;
        entry.AssociationToken = associationToken;
        entries_.push_back(entry);
    };
    const std::uint32_t propertyCount =
        file.CorTableRowCount(CorTableIndex::Property);
    for (std::uint32_t row = 1; row <= propertyCount; row++) {
        const std::uint32_t token = (0x17u << 24) | row;
        const auto it = slots.find(token);
        if (it == slots.end())
            continue;
        addEntry(TS::MethodSemanticsAttributes::Getter, it->second.primary, token);
        addEntry(TS::MethodSemanticsAttributes::Setter, it->second.secondary, token);
    }
    const std::uint32_t eventCount = file.CorTableRowCount(CorTableIndex::Event);
    for (std::uint32_t row = 1; row <= eventCount; row++) {
        const std::uint32_t token = (0x14u << 24) | row;
        const auto it = slots.find(token);
        if (it == slots.end())
            continue;
        addEntry(TS::MethodSemanticsAttributes::Adder, it->second.primary, token);
        addEntry(TS::MethodSemanticsAttributes::Remover, it->second.secondary, token);
        addEntry(TS::MethodSemanticsAttributes::Raiser, it->second.tertiary, token);
    }

    // The C# `entries.Sort()` -- unstable there, stable here (convention (e));
    // real metadata has distinct method row numbers, so the two agree.
    std::stable_sort(entries_.begin(), entries_.end(),
        [](const Entry& a, const Entry& b) {
            return a.MethodRowNumber < b.MethodRowNumber;
        });
}

MethodSemanticsLookup::SemanticsInfo MethodSemanticsLookup::GetSemantics(
    std::uint32_t methodToken) const
{
    // The .NET List<T>.BinarySearch loop (convention (f)): the first mid whose
    // element compares equal is returned, not the leftmost equal element.
    SemanticsInfo result;
    std::uint32_t rowNumber = methodToken & 0x00FFFFFFu;
    int lo = 0;
    int hi = static_cast<int>(entries_.size()) - 1;
    while (lo <= hi) {
        int i = lo + ((hi - lo) >> 1);
        std::uint32_t entryRow = entries_[i].MethodRowNumber;
        if (entryRow == rowNumber) {
            result.AssociationToken = entries_[i].AssociationToken;
            result.Semantics = entries_[i].Semantics;
            return result;
        }
        if (entryRow < rowNumber) {
            lo = i + 1;
        } else {
            hi = i - 1;
        }
    }
    return result;  // the miss shape: (0, None)
}

}  // namespace ILSpy::Decompiler::Metadata
