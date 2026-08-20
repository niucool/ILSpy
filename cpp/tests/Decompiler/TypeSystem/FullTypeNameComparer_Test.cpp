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

// Tests for `FullTypeNameComparer` (cpp/Decompiler/TypeSystem/FullTypeNameComparer.hpp, the
// port of the `FullTypeNameComparer` nested class in
// ICSharpCode.Decompiler/TypeSystem/FullTypeName.cs). The comparer is a leaf dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience`, consuming the D398 `StringComparer`; the C#
// `FullTypeName.Equals` / `GetHashCode` delegate to `FullTypeNameComparer.Ordinal`, so this
// comparer IS the equality definition for `FullTypeName`. The tests pin the top-level triple +
// per-nesting-level pairwise `Equals`, the `NestingLevel`-mismatch short-circuit, the
// case-insensitive variant, the hash consistency (exercising the nesting loop), the
// top-level-hash cross-check against `TopLevelTypeNameComparer`, the stable singletons, the
// public `NameComparer` field, the reflexive/symmetric contract, and the `final` shape.

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/FullTypeNameComparer.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeNameComparer.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// `Ordinal.Equals` compares two top-level `FullTypeName`s by their top-level
// triple (`TypeParameterCount`, `Name`, `Namespace`): equal only when all three
// match, and the name/namespace comparison is case-sensitive under `Ordinal`.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, OrdinalEqualsTopLevelTypesCompareByTopLevel)
{
    const TS::FullTypeNameComparer& cmp = TS::FullTypeNameComparer::Ordinal();
    TS::FullTypeName a("System.Collections.Generic.Dictionary`2");
    TS::FullTypeName b("System.Collections.Generic.Dictionary`2");
    TS::FullTypeName diffName("System.Collections.Generic.List`2");
    TS::FullTypeName diffNs("System.Collections.Dictionary`2");
    TS::FullTypeName diffTpc("System.Collections.Generic.Dictionary`1");
    EXPECT_TRUE(cmp.Equals(a, b));
    EXPECT_FALSE(cmp.Equals(a, diffName));
    EXPECT_FALSE(cmp.Equals(a, diffNs));
    EXPECT_FALSE(cmp.Equals(a, diffTpc));
    // Case-sensitive under Ordinal.
    TS::FullTypeName lower("system.collections.generic.dictionary`2");
    EXPECT_FALSE(cmp.Equals(a, lower));
}

// ---------------------------------------------------------------------------
// `Equals` short-circuits on a `NestingLevel` mismatch: a top-level name is
// never equal to a nested name, and names nested at different depths are not
// equal, regardless of the shared top-level segment.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, OrdinalEqualsNestingLevelMustMatch)
{
    const TS::FullTypeNameComparer& cmp = TS::FullTypeNameComparer::Ordinal();
    TS::FullTypeName top("NS.A`1");
    TS::FullTypeName oneLevel("NS.A`1+B`2");
    TS::FullTypeName twoLevel("NS.A`1+B`2+C`3");
    EXPECT_FALSE(cmp.Equals(top, oneLevel));
    EXPECT_FALSE(cmp.Equals(top, twoLevel));
    EXPECT_FALSE(cmp.Equals(oneLevel, twoLevel));
}

// ---------------------------------------------------------------------------
// `Ordinal.Equals` for nested types compares each level pairwise: the
// top-level triple must match, then each level's `GetNestedTypeAdditionalTypeParameterCount`
// and `GetNestedTypeName` (through `NameComparer`) must match in order. A mismatch
// at any level (additional type-parameter count OR name) returns false.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, OrdinalEqualsNestedTypesComparePerLevel)
{
    const TS::FullTypeNameComparer& cmp = TS::FullTypeNameComparer::Ordinal();
    TS::FullTypeName a("NS.A`1+B`2+C`3");
    TS::FullTypeName b("NS.A`1+B`2+C`3");
    EXPECT_TRUE(cmp.Equals(a, b));
    // Diff additional type-parameter count at level 1 (B`2 vs B`4).
    TS::FullTypeName diffAddTpc("NS.A`1+B`4+C`3");
    EXPECT_FALSE(cmp.Equals(a, diffAddTpc));
    // Diff name at level 1 (C vs D).
    TS::FullTypeName diffNestedName("NS.A`1+B`2+D`3");
    EXPECT_FALSE(cmp.Equals(a, diffNestedName));
    // Diff top-level Name (everything nested equal) -> false.
    TS::FullTypeName diffTopName("NS.X`1+B`2+C`3");
    EXPECT_FALSE(cmp.Equals(a, diffTopName));
}

// ---------------------------------------------------------------------------
// `OrdinalIgnoreCase.Equals` folds ASCII case for both the top-level `Name` /
// `Namespace` and each nested `GetNestedTypeName`, so case variants of the same
// full type name are equal. The integer `TypeParameterCount` (per-level and
// total) remains an exact comparison (not case-folded).
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, OrdinalIgnoreCaseEqualsIsCaseInsensitive)
{
    const TS::FullTypeNameComparer& cmp = TS::FullTypeNameComparer::OrdinalIgnoreCase();
    TS::FullTypeName a("NS.A`1+B`2+C`3");
    TS::FullTypeName b("ns.a`1+b`2+c`3");
    TS::FullTypeName c("NS.A`1+B`2+D`3");
    EXPECT_TRUE(cmp.Equals(a, b));
    // A genuinely-different nested name (D vs C) is still not equal even case-insensitively.
    EXPECT_FALSE(cmp.Equals(a, c));
    // The top-level case-insensitive match alone is insufficient when a nested name differs.
    TS::FullTypeName d("ns.a`1+b`2+d`3");
    EXPECT_FALSE(cmp.Equals(a, d));
}

// ---------------------------------------------------------------------------
// `GetHashCode` is consistent with `Equals`: equal full type names get equal
// hashes. This is the load-bearing invariant, exercised both for top-level names
// (no nesting loop) and for nested names (the `NestingLevel`-iteration loop).
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, GetHashCodeIsConsistentWithEquals)
{
    const TS::FullTypeNameComparer& cmp = TS::FullTypeNameComparer::Ordinal();
    TS::FullTypeName a("System.Collections.Generic.Dictionary`2");
    TS::FullTypeName b("System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(cmp.GetHashCode(a), cmp.GetHashCode(b));
    TS::FullTypeName na("NS.A`1+B`2+C`3");
    TS::FullTypeName nb("NS.A`1+B`2+C`3");
    EXPECT_EQ(cmp.GetHashCode(na), cmp.GetHashCode(nb));
    static_assert(std::is_same_v<decltype(cmp.GetHashCode(a)), int>,
                  "GetHashCode returns int (the .NET int return)");
    // The case-insensitive comparer hashes case variants of the same name equally.
    const TS::FullTypeNameComparer& icase = TS::FullTypeNameComparer::OrdinalIgnoreCase();
    TS::FullTypeName lo("ns.a`1");
    TS::FullTypeName hi("NS.A`1");
    EXPECT_EQ(icase.GetHashCode(lo), icase.GetHashCode(hi));
}

// ---------------------------------------------------------------------------
// For a top-level `FullTypeName` (`NestingLevel == 0`), the `FullTypeNameComparer`
// hash is exactly the `TopLevelTypeNameComparer` hash of the top-level name (the
// same all-`int` XOR, the nesting loop runs zero times). This cross-checks the
// two comparers agree on the top-level shape they share.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, GetHashCodeTopLevelMatchesTopLevelTypeNameComparer)
{
    const TS::FullTypeNameComparer& full = TS::FullTypeNameComparer::Ordinal();
    const TS::TopLevelTypeNameComparer& top = TS::TopLevelTypeNameComparer::Ordinal();
    TS::FullTypeName a("System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(full.GetHashCode(a), top.GetHashCode(a.GetTopLevelTypeName()));
    // The OrdinalIgnoreCase pair likewise agrees on the top-level shape.
    const TS::FullTypeNameComparer& fullIcase = TS::FullTypeNameComparer::OrdinalIgnoreCase();
    const TS::TopLevelTypeNameComparer& topIcase = TS::TopLevelTypeNameComparer::OrdinalIgnoreCase();
    EXPECT_EQ(fullIcase.GetHashCode(a), topIcase.GetHashCode(a.GetTopLevelTypeName()));
}

// ---------------------------------------------------------------------------
// `Ordinal()` / `OrdinalIgnoreCase()` return stable singletons: each accessor
// returns the same `FullTypeNameComparer&` across calls, and the two singletons
// are distinct references.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, OrdinalAndOrdinalIgnoreCaseAreStableSingletons)
{
    EXPECT_EQ(&TS::FullTypeNameComparer::Ordinal(), &TS::FullTypeNameComparer::Ordinal());
    EXPECT_EQ(&TS::FullTypeNameComparer::OrdinalIgnoreCase(),
              &TS::FullTypeNameComparer::OrdinalIgnoreCase());
    EXPECT_NE(&TS::FullTypeNameComparer::Ordinal(), &TS::FullTypeNameComparer::OrdinalIgnoreCase());
}

// ---------------------------------------------------------------------------
// The public `NameComparer` reference field exposes the configured `StringComparer`:
// `Ordinal()`'s `NameComparer` IS `StringComparer::Ordinal()` (pointer identity), and
// `OrdinalIgnoreCase()`'s IS `StringComparer::OrdinalIgnoreCase()`.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, NameComparerFieldExposesConfiguredStringComparer)
{
    EXPECT_EQ(&TS::FullTypeNameComparer::Ordinal().NameComparer,
              &TS::StringComparer::Ordinal());
    EXPECT_EQ(&TS::FullTypeNameComparer::OrdinalIgnoreCase().NameComparer,
              &TS::StringComparer::OrdinalIgnoreCase());
}

// ---------------------------------------------------------------------------
// `Equals` is reflexive and symmetric (the `IEqualityComparer<FullTypeName>`
// contract): `Equals(x, x)` is true and `Equals(x, y) == Equals(y, x)`, for both
// comparers and for both top-level and nested names.
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, EqualsIsReflexiveAndSymmetric)
{
    const TS::FullTypeNameComparer& ord = TS::FullTypeNameComparer::Ordinal();
    const TS::FullTypeNameComparer& icase = TS::FullTypeNameComparer::OrdinalIgnoreCase();
    TS::FullTypeName a("NS.A`1+B`2");
    TS::FullTypeName b("ns.a`1+b`2");
    EXPECT_TRUE(ord.Equals(a, a));
    EXPECT_TRUE(icase.Equals(a, a));
    EXPECT_EQ(ord.Equals(a, b), ord.Equals(b, a));
    EXPECT_EQ(icase.Equals(a, b), icase.Equals(b, a));
}

// ---------------------------------------------------------------------------
// The class shape matches the C# `sealed class`: `final`, constructible from a
// `const StringComparer&`, not default-constructible, and non-copy-assignable
// (the reference member).
// ---------------------------------------------------------------------------
TEST(TS_FullTypeNameComparer, ClassShapeMatchesCSharp)
{
    static_assert(std::is_final_v<TS::FullTypeNameComparer>,
                  "FullTypeNameComparer is final (the C# sealed class)");
    static_assert(std::is_constructible_v<TS::FullTypeNameComparer, const TS::StringComparer&>,
                  "Constructible from a const StringComparer& (the C# ctor)");
    static_assert(!std::is_default_constructible_v<TS::FullTypeNameComparer>,
                  "Not default-constructible (the ctor requires a StringComparer)");
    static_assert(!std::is_copy_assignable_v<TS::FullTypeNameComparer>,
                  "Not copy-assignable (the reference member, matching the C# readonly field)");
    static_assert(!std::is_abstract_v<TS::FullTypeNameComparer>, "Concrete (not abstract)");
}
