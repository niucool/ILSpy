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

// Port of ICSharpCode.Decompiler/TypeSystem/TopLevelTypeName.cs (the
// `TopLevelTypeNameComparer` nested class). A leaf dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience`, consuming the D398 `StringComparer`.
//
// The C# `sealed class TopLevelTypeNameComparer : IEqualityComparer<TopLevelTypeName>`
// holds a `StringComparer` and compares two `TopLevelTypeName`s by their
// `TypeParameterCount`, `Name`, and `Namespace` (the name and namespace through the
// `StringComparer`, so `Ordinal` is case-sensitive and `OrdinalIgnoreCase` folds
// ASCII case). The two static singletons `Ordinal` / `OrdinalIgnoreCase` are the
// instances the type system constructs (the C# `static readonly` fields wired to
// `StringComparer.Ordinal` / `StringComparer.OrdinalIgnoreCase`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public readonly StringComparer NameComparer` instance field ports to a
//      PUBLIC reference member `const StringComparer& NameComparer` -- the faithful
//      mirror of a public readonly field holding a reference type, preserving the
//      `cmp.NameComparer.Equals(...)` access syntax (no accessor parens). A reference
//      member makes the class non-copy-assignable (the singletons are returned by
//      `const&` and never copy-assigned, so this matches the C# `sealed` reference
//      type's copy-by-reference semantics).
//  (b) `Equals` returns the conjunction of the three component comparisons
//      (`TypeParameterCount ==`, then `NameComparer.Equals(Name)`, then
//      `NameComparer.Equals(Namespace)`); `GetHashCode` XORs the three `int` hashes
//      (`NameComparer.GetHashCode(Name) ^ NameComparer.GetHashCode(Namespace) ^
//      TypeParameterCount`) -- the all-`int` XOR the `TopLevelTypeNameComparer.GetHashCode`
//      consumer shape the D398 `StringComparer.GetHashCodeXorPatternMatchesTopLevelTypeNameComparer`
//      test already pinned.
//  (c) The static `Ordinal()` / `OrdinalIgnoreCase()` accessors return
//      `const TopLevelTypeNameComparer&` to a function-local static singleton (the
//      Meyers-singleton pattern, the D398 `StringComparer::Ordinal` precedent) -- the
//      C++ counterpart of the C# `static readonly` fields.
//  (d) The class is `final` (the C# `sealed`), constructible only from a
//      `const StringComparer&` (no default ctor -- the C# ctor requires the comparer).

#pragma once

#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The C# `sealed class TopLevelTypeNameComparer : IEqualityComparer<TopLevelTypeName>`.
// Compares two top-level type names by `TypeParameterCount`, `Name`, and `Namespace`
// (the latter two through the configured `StringComparer`).
class TopLevelTypeNameComparer final {
public:
    // The C# `public readonly StringComparer NameComparer` -- the configured string
    // comparer. A public reference member (the faithful mirror of the C# public
    // readonly field), so `cmp.NameComparer.Equals(...)` reads it without accessor parens.
    const StringComparer& NameComparer;

    // The C# `TopLevelTypeNameComparer(StringComparer nameComparer)` ctor.
    explicit TopLevelTypeNameComparer(const StringComparer& nameComparer)
        : NameComparer(nameComparer) {}

    // `IEqualityComparer<TopLevelTypeName>.Equals` -- true iff the two names share
    // `TypeParameterCount`, `Name`, and `Namespace` (the name and namespace compared
    // through `NameComparer`, so case sensitivity follows the configured comparer).
    bool Equals(const TopLevelTypeName& x, const TopLevelTypeName& y) const {
        return x.TypeParameterCount() == y.TypeParameterCount()
            && NameComparer.Equals(x.Name(), y.Name())
            && NameComparer.Equals(x.Namespace(), y.Namespace());
    }

    // `IEqualityComparer<TopLevelTypeName>.GetHashCode` -- `hash(Name) ^
    // hash(Namespace) ^ TypeParameterCount`, all `int` (the XOR pattern consistent
    // with `Equals`: equal names get equal hashes).
    int GetHashCode(const TopLevelTypeName& obj) const {
        return NameComparer.GetHashCode(obj.Name())
             ^ NameComparer.GetHashCode(obj.Namespace())
             ^ obj.TypeParameterCount();
    }

    // `static readonly TopLevelTypeNameComparer Ordinal` -- the case-sensitive
    // comparer wired to `StringComparer::Ordinal()`.
    static const TopLevelTypeNameComparer& Ordinal();

    // `static readonly TopLevelTypeNameComparer OrdinalIgnoreCase` -- the
    // case-insensitive comparer wired to `StringComparer::OrdinalIgnoreCase()`.
    static const TopLevelTypeNameComparer& OrdinalIgnoreCase();
};

inline const TopLevelTypeNameComparer& TopLevelTypeNameComparer::Ordinal() {
    static const TopLevelTypeNameComparer instance(StringComparer::Ordinal());
    return instance;
}

inline const TopLevelTypeNameComparer& TopLevelTypeNameComparer::OrdinalIgnoreCase() {
    static const TopLevelTypeNameComparer instance(StringComparer::OrdinalIgnoreCase());
    return instance;
}

} // namespace ILSpy::Decompiler::TypeSystem
