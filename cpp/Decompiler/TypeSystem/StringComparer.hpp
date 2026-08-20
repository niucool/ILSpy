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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the BCL `System.StringComparer` (absorbed into `ILSpy::Decompiler::TypeSystem`,
// the D384 `MethodSemanticsAttributes` / D381 `EntityHandle` BCL-absorption precedent) --
// the abstract class `ICompilation.NameComparer` returns (a leaf dependency of
// `ICompilation`, toward `TypeSystemAstBuilder` / `CSharpAmbience`).
//
// The C++ port has no `System` namespace mirror, so a BCL abstract class the TypeSystem
// surface needs lands in `ILSpy::Decompiler::TypeSystem` (NOT `System::`). The C#
// `ICompilation.NameComparer` property returns the reference type by reference, so the
// port returns `const StringComparer&`; `SimpleCompilation.NameComparer` returns
// `StringComparer.Ordinal`.
//
// KEY PORT CONVENTIONS:
//  (a) `System.StringComparer` is an abstract class implementing
//      `IEqualityComparer<string>` (and `IComparer<string>`); the surface the
//      TypeSystem consumers use is `Equals(string, string)` + `GetHashCode(string)`
//      (`TopLevelTypeNameComparer` / `FullTypeNameComparer` XOR the two hashes with
//      the type-parameter count; `MergedNamespace` constructs a
//      `Dictionary<string, INamespace>(compilation.NameComparer)`), so the C++ port
//      mirrors those two virtuals plus the `Ordinal` / `OrdinalIgnoreCase` static
//      instances. The `IComparer<string>.Compare` surface is NOT ported (no
//      TypeSystem consumer calls it through `StringComparer`).
//  (b) `GetHashCode` returns `int` (the .NET `int` return), matching the
//      `object.GetHashCode()` signature the `TopLevelTypeNameComparer.GetHashCode`
//      XORs with `obj.TypeParameterCount` (also `int`).
//  (c) `Ordinal` compares byte-wise (`std::string::operator==`) and hashes via
//      `std::hash<std::string>` (the `size_t` hash truncated to `int`), the faithful
//      C++ counterpart of `StringComparer.Ordinal` (ordinal byte comparison + the
//      string's ordinal hash).
//  (d) `OrdinalIgnoreCase` lowercases the ASCII range `A`-`Z` for both comparison
//      and a case-folded hash (lowercase then `std::hash`), matching
//      `StringComparer.OrdinalIgnoreCase` for the ASCII identifier range the
//      TypeSystem names live in. The full Unicode case folding the .NET
//      `OrdinalIgnoreCase` special-cases (e.g. `I`-dot, `s`-sharp) is NOT ported --
//      a documented deviation; type and namespace names in .NET metadata are ASCII
//      identifiers, so the ASCII fold is the load-bearing case.
//  (e) The static `Ordinal()` / `OrdinalIgnoreCase()` accessors return
//      `const StringComparer&` to a function-local static singleton (the
//      Meyers-singleton pattern, the `KnownAttributeTypeNames` D378 precedent) --
//      the C++ counterpart of the C# `static readonly StringComparer Ordinal` field.

#pragma once

#include <functional>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// The BCL `System.StringComparer` abstract class -- a polymorphic string
// equality comparer `ICompilation.NameComparer` returns. Concrete compilations
// (the unported `SimpleCompilation`) expose one via `ICompilation::NameComparer()`;
// the resolver / round-trip paths use it for type / namespace name lookups
// (`TopLevelTypeNameComparer` / `FullTypeNameComparer` / `MergedNamespace`).
class StringComparer {
public:
    virtual ~StringComparer() = default;

    // `IEqualityComparer<string>.Equals(string, string)` -- true iff the two
    // strings are equal under this comparer's comparison rule.
    virtual bool Equals(const std::string& x, const std::string& y) const = 0;

    // `IEqualityComparer<string>.GetHashCode(string)` -- a hash of the string
    // consistent with `Equals` (Equal strings get Equal hashes). Returns `int`
    // (the .NET `int` return).
    virtual int GetHashCode(const std::string& obj) const = 0;

    // `static readonly StringComparer Ordinal` -- the ordinal (byte-wise,
    // case-sensitive) comparer `SimpleCompilation.NameComparer` returns.
    static const StringComparer& Ordinal();

    // `static readonly StringComparer OrdinalIgnoreCase` -- the ordinal
    // case-insensitive comparer (ASCII `A`-`Z` folded to lowercase).
    static const StringComparer& OrdinalIgnoreCase();
};

// The ordinal (case-sensitive) `StringComparer` -- byte-wise `Equals`
// (`std::string::operator==`) and `std::hash<std::string>` for `GetHashCode`.
class OrdinalStringComparer final : public StringComparer {
public:
    bool Equals(const std::string& x, const std::string& y) const override {
        return x == y;
    }
    int GetHashCode(const std::string& obj) const override {
        return static_cast<int>(std::hash<std::string>{}(obj));
    }
};

// The ordinal case-insensitive `StringComparer` -- ASCII `A`-`Z` lowercased for
// both `Equals` (case-folded byte comparison) and `GetHashCode` (hash of the
// lowercased string).
class OrdinalIgnoreCaseStringComparer final : public StringComparer {
public:
    bool Equals(const std::string& x, const std::string& y) const override {
        return ToLowerAscii(x) == ToLowerAscii(y);
    }
    int GetHashCode(const std::string& obj) const override {
        return static_cast<int>(std::hash<std::string>{}(ToLowerAscii(obj)));
    }

private:
    // Lowercase the ASCII range `A`-`Z`, leaving every other byte unchanged -- the
    // case fold `StringComparer.OrdinalIgnoreCase` applies to the ASCII identifier
    // range the TypeSystem names live in (the `std::tolower`-on-`unsigned char`
    // pattern the `AssignVariableNames` transform already uses).
    static std::string ToLowerAscii(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            out.push_back(c >= 'A' && c <= 'Z'
                              ? static_cast<char>(c - 'A' + 'a')
                              : static_cast<char>(c));
        }
        return out;
    }
};

inline const StringComparer& StringComparer::Ordinal() {
    static const OrdinalStringComparer instance;
    return instance;
}

inline const StringComparer& StringComparer::OrdinalIgnoreCase() {
    static const OrdinalIgnoreCaseStringComparer instance;
    return instance;
}

} // namespace ILSpy::Decompiler::TypeSystem
