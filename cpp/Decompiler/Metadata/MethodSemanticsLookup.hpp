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

// Port of ICSharpCode.Decompiler/Metadata/MethodSemanticsLookup.cs -- the
// accessor->association lookup structure. For an accessor method, it answers
// which property/event the method is an accessor of (`GetSemantics`), via one
// entry list sorted by method row number. The C# builds the list by walking
// every property row (`PropertyDefinition.GetAccessors()` -- the Getter and
// Setter slots) and every event row (`EventDefinition.GetAccessors()` -- the
// Adder and Remover slots, plus the Raiser the default filter drops), then
// `AddEntry` skips a semantics kind the filter excludes and nil methods.
//
// Consumers: the `MetadataMethod` ctor (`MetadataFile.MethodSemanticsLookup
// GetSemantics(handle)` -- the SymbolKind.Accessor arm + `AccessorOwner` /
// `AccessorKind`), `MetadataTypeDefinition.Methods` (the accessor grouping),
// and `CSharpDecompiler.DecompileWholeModule`'s method-kind decisions -- the
// named next-in-order Phase-2 piece (MetadataMethod) is its first consumer,
// which is why the class lands now.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class MethodSemanticsLookup` (internal) ports to a plain
//      class in `ILSpy::Decompiler::Metadata` (the C# namespace), ctor public
//      (the MetadataFile lazy property and the tests construct it directly --
//      the C# internal-ctor `MetadataFile` access maps to the port's
//      public-ctor convention for internal classes the tests drive, the
//      SwmProbe/MinimalCorlib precedent).
//  (b) The C# `MetadataReader metadata` ctor parameter ports to `const
//      MetadataFile&`: the class reads ONLY the raw table surface
//      (`MetadataFile::MethodSemanticsRows()` and the Property/Event row
//      counts), the established raw-surface convention (the
//      NamespaceCache/GetTypeDefinition lookups). The `filter` parameter
//      keeps its default value (`CSharpAccessors`, the C# `csharpAccessors`
//      const). The `(filter & Other) != 0` NotSupportedException ports to
//      `std::logic_error` carrying the exact C# message (the CSharpOperators
//      NotSupportedException mapping convention).
//  (c) The C# builds the entry list PER ASSOCIATION through SRM's
//      `GetAccessors()`: a binary search over the ECMA-mandated sorted
//      Association column finds the association's contiguous row run, and the
//      EXACT-VALUE switch keeps the LAST matching row per slot (`Getter`
//      0x2 / `Setter` 0x1 for properties, `Adder` 0x8 / `Remover` 0x10 /
//      `Raiser` 0x20 for events -- a combined or unknown flag value matches
//      NO arm; `Other` 0x4 rows match the accessors' Others but never reach
//      `AddEntry`). The port builds the SAME entries with one linear walk
//      over the whole table keeping a per-association last-row-wins map: the
//      two agree on every file SRM can open, because SRM itself rejects an
//      unsorted table at reader construction (MethodSemanticsTableReader:
//      `if (!declaredSorted && !CheckSorted()) Throw.TableNotSorted(...)`)
//      and a declared-sorted table makes each association's rows contiguous
//      in row order. The one divergence: a file that DECLARES the sorted bit
//      but lies (unsorted data) makes SRM's binary search pick an arbitrary
//      subrange while the port's walk sees every matching row -- unreachable
//      through MetadataBuilder (which emits sorted) and every real producer;
//      documented here.
//  (d) The C# slot assignment keeps the LAST row even when its method column
//      is NIL (`GetMethod(rowId).RowId` is 0): `AddEntry` then drops the nil
//      method, so an association whose last Getter row has a nil method ends
//      with NO entry at all. The port reproduces this (the nil method skips
//      at the AddEntry level, not the slot level) -- pinned by the MslSynth
//      P5 shape (a Getter row then a nil-method Getter row: NEITHER method
//      is an accessor).
//  (e) The C# `List<Entry>.Sort()` is an UNSTABLE sort, but every real
//      metadata file has distinct method row numbers (a method is an accessor
//      of at most one association), so the relative order of equal keys never
//      matters in practice. The port uses `std::stable_sort` -- deterministic
//      among duplicates (the C# iteration order: properties' Getter before
//      Setter, property rows before event rows) where the C# order is
//      unspecified.
//  (f) The C# `List<Entry>.BinarySearch` is the classic .NET bisection loop
//      (the first mid whose element COMPARES EQUAL is returned, not the
//      leftmost equal element) -- reproduced verbatim (a `std::lower_bound`
//      substitute would pick a different entry among duplicate row numbers).
//  (g) The C# `(EntityHandle, MethodSemanticsAttributes)` tuple ports to the
//      `SemanticsInfo` struct: the association as the RAW TOKEN (0x17......
//      property / 0x14...... event, the D381 EntityHandle-as-raw-token
//      convention -- the C# `accessorOwner.Kind is PropertyDefinition or
//      EventDefinition` check becomes a top-byte test) and the semantics flag
//      (0 / `None` for a non-accessor). The GetSemantics parameter is the raw
//      0x06...... method token; the search key is its low 24 bits (the C#
//      handle IS the row number, so the C# cannot observe a wrong-table byte).
//  (h) `Entries()` is the port's reflection-dump test seam (the C# surface
//      exposes no entry enumeration): the sorted entry list the gold probe
//      reflects out of the real engine's private `entries` field, returned by
//      const reference into the lookup.

#pragma once

#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"

#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

// `class MethodSemanticsLookup` (MethodSemanticsLookup.cs lines 33-116): the
// lookup structure that, for an accessor, finds the associated property or
// event. Built once per `MetadataFile` (the lazy property), single-threaded.
class MethodSemanticsLookup {
public:
    // The C# `const MethodSemanticsAttributes csharpAccessors = Getter |
    // Setter | Adder | Remover` (the default filter; the Raiser rows are
    // dropped and the Other rows are structurally absent -- the C# ctor
    // rejects a filter that includes Other). The enum's `|` operators are
    // non-constexpr (the MethodSemanticsAttributes.hpp free functions), so the
    // composite is a const member defined in the .cpp (the TypeAttributes
    // masks precedent).
    static const ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes
        CSharpAccessors;

    // The C# `(EntityHandle, MethodSemanticsAttributes)` return tuple: the
    // association token (0x17...... = the property / 0x14...... = the event;
    // 0 = the nil handle) and the accessor semantics flag (None = a
    // non-accessor, the miss shape).
    struct SemanticsInfo {
        std::uint32_t AssociationToken = 0;
        ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes Semantics
            = ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;
    };

    // The C# private `readonly struct Entry` (the test-seam shape, convention
    // (h)): one accessor row -- the semantics kind, the 1-based method row
    // number, and the association token.
    struct Entry {
        ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes Semantics
            = ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;
        std::uint32_t MethodRowNumber = 0;  // 1-based; the C# field is int
        std::uint32_t AssociationToken = 0;
    };

    // The C# `public MethodSemanticsLookup(MetadataReader metadata,
    // MethodSemanticsAttributes filter = csharpAccessors)`: builds the sorted
    // entry list. Throws `std::logic_error` (the NotSupportedException,
    // convention (b)) when the filter includes `Other`.
    explicit MethodSemanticsLookup(const MetadataFile& file,
        ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes filter
            = CSharpAccessors);

    // The C# `public (EntityHandle, MethodSemanticsAttributes)
    // GetSemantics(MethodDefinitionHandle method)`: the binary search over the
    // sorted entries by method row number (convention (f)). The miss shape is
    // (0, None) -- the C# (nil, 0).
    SemanticsInfo GetSemantics(std::uint32_t methodToken) const;

    // The sorted entry list (the reflection-dump test seam, convention (h)):
    // ascending by method row number.
    const std::vector<Entry>& Entries() const { return entries_; }

private:
    std::vector<Entry> entries_;  // sorted by MethodRowNumber
};

} // namespace ILSpy::Decompiler::Metadata
