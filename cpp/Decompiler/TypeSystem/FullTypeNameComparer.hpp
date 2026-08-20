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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/FullTypeName.cs (the
// `FullTypeNameComparer` nested class). A leaf dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience`, consuming the D398 `StringComparer`.
//
// The C# `sealed class FullTypeNameComparer : IEqualityComparer<FullTypeName>` holds a
// `StringComparer` and compares two `FullTypeName`s: the top-level
// (`TypeParameterCount`, `Name`, `Namespace` through `NameComparer`) must match, then
// each nested level's additional type-parameter count and name (through `NameComparer`)
// must match pairwise. The two static singletons `Ordinal` / `OrdinalIgnoreCase` are the
// instances the type system constructs. The C# `FullTypeName.Equals(FullTypeName)` and
// `FullTypeName.GetHashCode()` delegate to `FullTypeNameComparer.Ordinal`, so this
// comparer IS the equality definition for `FullTypeName` (the C++ `FullTypeName::operator==`
// is an equivalent field-wise definition that predates this port and is left in place).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public readonly StringComparer NameComparer` instance field ports to a
//      PUBLIC reference member `const StringComparer& NameComparer` (the D400
//      `TopLevelTypeNameComparer` precedent) -- the faithful mirror of a public readonly
//      field holding a reference type, preserving the `cmp.NameComparer.Equals(...)`
//      access syntax. A reference member makes the class non-copy-assignable (the
//      singletons are returned by `const&` and never copy-assigned).
//  (b) `Equals` mirrors the C# structure: a `NestingLevel` mismatch short-circuits;
//      the top-level triple is checked (and short-circuits on a mismatch); then each
//      nesting level's `GetNestedTypeAdditionalTypeParameterCount` and
//      `GetNestedTypeName` are compared pairwise through `NameComparer`.
//  (c) `GetHashCode` mirrors the C# `unchecked` wraparound arithmetic by accumulating
//      in `unsigned int` (well-defined modular wraparound, no signed-overflow UB) and
//      casting the result back to `int` (the two's-complement bit pattern is identical to
//      the C# `unchecked int` wrap). The C# loop body uses `obj.Name` (the innermost
//      name) and `obj.TypeParameterCount` (the total count) on EVERY iteration -- NOT
//      the per-level `GetNestedTypeName(i)` / `GetNestedTypeAdditionalTypeParameterCount(i)`
//      accessors that `Equals` uses. This asymmetry is a C# source quirk (the loop adds the
//      same value each iteration); it is consistent with `Equals` (equal names share the
//      innermost name and total count, so they hash equal) but is not maximally
//      discriminating. It is ported VERBATIM (not "fixed").
//  (d) The static `Ordinal()` / `OrdinalIgnoreCase()` accessors return
//      `const FullTypeNameComparer&` to a function-local static singleton (the
//      Meyers-singleton pattern, the D398 / D400 precedent).
//  (e) The class is `final` (the C# `sealed`), constructible only from a
//      `const StringComparer&`.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The C# `sealed class FullTypeNameComparer : IEqualityComparer<FullTypeName>`.
// Compares two full type names: the top-level triple, then each nested level pairwise.
class FullTypeNameComparer final {
public:
    // The C# `public readonly StringComparer NameComparer` -- the configured string
    // comparer. A public reference member (the faithful mirror of the C# public readonly
    // field).
    const StringComparer& NameComparer;

    // The C# `FullTypeNameComparer(StringComparer nameComparer)` ctor.
    explicit FullTypeNameComparer(const StringComparer& nameComparer)
        : NameComparer(nameComparer) {}

    // `IEqualityComparer<FullTypeName>.Equals` -- true iff the two names share a
    // nesting level, a matching top-level triple (`TypeParameterCount`, `Name`,
    // `Namespace` through `NameComparer`), and pairwise-matching nested levels
    // (each level's additional type-parameter count and name through `NameComparer`).
    bool Equals(const FullTypeName& x, const FullTypeName& y) const {
        if (x.NestingLevel() != y.NestingLevel())
            return false;
        const TopLevelTypeName& topX = x.GetTopLevelTypeName();
        const TopLevelTypeName& topY = y.GetTopLevelTypeName();
        if (topX.TypeParameterCount() == topY.TypeParameterCount()
            && NameComparer.Equals(topX.Name(), topY.Name())
            && NameComparer.Equals(topX.Namespace(), topY.Namespace()))
        {
            for (int i = 0; i < x.NestingLevel(); i++)
            {
                if (x.GetNestedTypeAdditionalTypeParameterCount(i)
                    != y.GetNestedTypeAdditionalTypeParameterCount(i))
                    return false;
                if (!NameComparer.Equals(x.GetNestedTypeName(i), y.GetNestedTypeName(i)))
                    return false;
            }
            return true;
        }
        return false;
    }

    // `IEqualityComparer<FullTypeName>.GetHashCode` -- the top-level triple XORed
    // (as in `TopLevelTypeNameComparer`), then for each nesting level the running
    // hash is scaled by 31 and the innermost-name/total-count XOR is added. The
    // accumulation is in `unsigned int` to mirror the C# `unchecked` wraparound
    // without signed-overflow UB; the result is cast back to `int`.
    int GetHashCode(const FullTypeName& obj) const {
        const TopLevelTypeName& top = obj.GetTopLevelTypeName();
        unsigned int hash = static_cast<unsigned int>(
            NameComparer.GetHashCode(top.Name())
            ^ NameComparer.GetHashCode(top.Namespace())
            ^ top.TypeParameterCount());
        for (int i = 0; i < obj.NestingLevel(); i++)
        {
            hash *= 31u;
            hash += static_cast<unsigned int>(
                NameComparer.GetHashCode(obj.Name()) ^ obj.TypeParameterCount());
        }
        return static_cast<int>(hash);
    }

    // `static readonly FullTypeNameComparer Ordinal` -- the case-sensitive comparer
    // wired to `StringComparer::Ordinal()`.
    static const FullTypeNameComparer& Ordinal();

    // `static readonly FullTypeNameComparer OrdinalIgnoreCase` -- the case-insensitive
    // comparer wired to `StringComparer::OrdinalIgnoreCase()`.
    static const FullTypeNameComparer& OrdinalIgnoreCase();
};

inline const FullTypeNameComparer& FullTypeNameComparer::Ordinal() {
    static const FullTypeNameComparer instance(StringComparer::Ordinal());
    return instance;
}

inline const FullTypeNameComparer& FullTypeNameComparer::OrdinalIgnoreCase() {
    static const FullTypeNameComparer instance(StringComparer::OrdinalIgnoreCase());
    return instance;
}

} // namespace ILSpy::Decompiler::TypeSystem
