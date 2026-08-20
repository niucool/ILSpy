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

// Tests for `TopLevelTypeNameComparer` (cpp/Decompiler/TypeSystem/TopLevelTypeNameComparer.hpp,
// the port of the `TopLevelTypeNameComparer` nested class in
// ICSharpCode.Decompiler/TypeSystem/TopLevelTypeName.cs). The comparer is a leaf dependency
// toward `TypeSystemAstBuilder` / `CSharpAmbience`, consuming the D398 `StringComparer`. The
// tests pin the three-component `Equals` (TypeParameterCount + Name + Namespace, the latter
// two through the configured `StringComparer`), the all-`int` XOR `GetHashCode`, the
// `Ordinal` / `OrdinalIgnoreCase` stable singletons, the public `NameComparer` reference
// field, the reflexive/symmetric contract, and the `final` sealed-class shape.

#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeNameComparer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// `Ordinal.Equals` compares all three components: two names are equal only when
// they share `TypeParameterCount`, `Name`, and `Namespace`, and the name/namespace
// comparison is case-sensitive (the `Ordinal` `StringComparer` is byte-wise).
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, OrdinalEqualsComparesAllThreeComponents)
{
    const TS::TopLevelTypeNameComparer& cmp = TS::TopLevelTypeNameComparer::Ordinal();
    TS::TopLevelTypeName a("System.Collections.Generic", "Dictionary", 2);
    TS::TopLevelTypeName b("System.Collections.Generic", "Dictionary", 2);
    TS::TopLevelTypeName diffName("System.Collections.Generic", "List", 2);
    TS::TopLevelTypeName diffNs("System.Collections", "Dictionary", 2);
    TS::TopLevelTypeName diffTpc("System.Collections.Generic", "Dictionary", 1);
    EXPECT_TRUE(cmp.Equals(a, b));
    EXPECT_FALSE(cmp.Equals(a, diffName));
    EXPECT_FALSE(cmp.Equals(a, diffNs));
    EXPECT_FALSE(cmp.Equals(a, diffTpc));
    // The name comparison is case-sensitive under Ordinal.
    TS::TopLevelTypeName lowerName("System.Collections.Generic", "dictionary", 2);
    EXPECT_FALSE(cmp.Equals(a, lowerName));
}

// ---------------------------------------------------------------------------
// `OrdinalIgnoreCase.Equals` folds ASCII case before comparing `Name` and
// `Namespace`, so case variants of the same top-level type name are equal.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, OrdinalIgnoreCaseEqualsIsCaseInsensitive)
{
    const TS::TopLevelTypeNameComparer& cmp = TS::TopLevelTypeNameComparer::OrdinalIgnoreCase();
    TS::TopLevelTypeName a("System.Collections.Generic", "Dictionary", 2);
    TS::TopLevelTypeName b("system.collections.generic", "dictionary", 2);
    TS::TopLevelTypeName c("SYSTEM.COLLECTIONS.GENERIC", "DICTIONARY", 2);
    EXPECT_TRUE(cmp.Equals(a, b));
    EXPECT_TRUE(cmp.Equals(a, c));
    // The TypeParameterCount is still an exact int comparison (not case-folded).
    TS::TopLevelTypeName diffTpc("system.collections.generic", "dictionary", 1);
    EXPECT_FALSE(cmp.Equals(a, diffTpc));
}

// ---------------------------------------------------------------------------
// `GetHashCode` is consistent with `Equals`: equal names get equal hashes, and
// the hash is the all-`int` XOR `hash(Name) ^ hash(Namespace) ^ TypeParameterCount`.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, GetHashCodeIsConsistentWithEquals)
{
    const TS::TopLevelTypeNameComparer& cmp = TS::TopLevelTypeNameComparer::Ordinal();
    TS::TopLevelTypeName a("System.Collections.Generic", "Dictionary", 2);
    TS::TopLevelTypeName b("System.Collections.Generic", "Dictionary", 2);
    EXPECT_EQ(cmp.GetHashCode(a), cmp.GetHashCode(b));
    // The hash is exactly the documented XOR composition.
    int expected = cmp.NameComparer.GetHashCode(a.Name())
                 ^ cmp.NameComparer.GetHashCode(a.Namespace())
                 ^ a.TypeParameterCount();
    EXPECT_EQ(cmp.GetHashCode(a), expected);
    static_assert(std::is_same_v<decltype(cmp.GetHashCode(a)), int>,
                  "GetHashCode returns int (the .NET int return)");
}

// ---------------------------------------------------------------------------
// `GetHashCode` distinguishes names that differ only in `TypeParameterCount`
// (same name + namespace, different generic arity) -- the arity is part of the
// XOR, so the hashes differ.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, GetHashCodeDistinguishesByTypeParameterCount)
{
    const TS::TopLevelTypeNameComparer& cmp = TS::TopLevelTypeNameComparer::Ordinal();
    TS::TopLevelTypeName one("System", "Tuple", 1);
    TS::TopLevelTypeName two("System", "Tuple", 2);
    EXPECT_NE(cmp.GetHashCode(one), cmp.GetHashCode(two));
    EXPECT_FALSE(cmp.Equals(one, two));
}

// ---------------------------------------------------------------------------
// `Ordinal()` / `OrdinalIgnoreCase()` return stable singletons: each accessor
// returns the same `TopLevelTypeNameComparer&` across calls (the C# `static
// readonly` fields), and the two singletons are distinct references.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, OrdinalAndOrdinalIgnoreCaseAreStableSingletons)
{
    EXPECT_EQ(&TS::TopLevelTypeNameComparer::Ordinal(),
              &TS::TopLevelTypeNameComparer::Ordinal());
    EXPECT_EQ(&TS::TopLevelTypeNameComparer::OrdinalIgnoreCase(),
              &TS::TopLevelTypeNameComparer::OrdinalIgnoreCase());
    EXPECT_NE(&TS::TopLevelTypeNameComparer::Ordinal(),
              &TS::TopLevelTypeNameComparer::OrdinalIgnoreCase());
}

// ---------------------------------------------------------------------------
// The public `NameComparer` reference field exposes the configured `StringComparer`:
// `Ordinal()`'s `NameComparer` IS `StringComparer::Ordinal()`, and `OrdinalIgnoreCase()`'s
// IS `StringComparer::OrdinalIgnoreCase()` (pointer identity). The field is read without
// accessor parens (`cmp.NameComparer`), mirroring the C# public readonly field access.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, NameComparerFieldExposesConfiguredStringComparer)
{
    EXPECT_EQ(&TS::TopLevelTypeNameComparer::Ordinal().NameComparer,
              &TS::StringComparer::Ordinal());
    EXPECT_EQ(&TS::TopLevelTypeNameComparer::OrdinalIgnoreCase().NameComparer,
              &TS::StringComparer::OrdinalIgnoreCase());
    // The exposed comparer dispatches to its StringComparer (case-sensitive under Ordinal).
    EXPECT_FALSE(TS::TopLevelTypeNameComparer::Ordinal().NameComparer.Equals("Foo", "foo"));
    EXPECT_TRUE(TS::TopLevelTypeNameComparer::OrdinalIgnoreCase().NameComparer.Equals("Foo", "foo"));
}

// ---------------------------------------------------------------------------
// `Equals` is reflexive and symmetric (the `IEqualityComparer<TopLevelTypeName>`
// contract): `Equals(x, x)` is true and `Equals(x, y) == Equals(y, x)`, for both
// comparers.
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, EqualsIsReflexiveAndSymmetric)
{
    const TS::TopLevelTypeNameComparer& ord = TS::TopLevelTypeNameComparer::Ordinal();
    const TS::TopLevelTypeNameComparer& icase = TS::TopLevelTypeNameComparer::OrdinalIgnoreCase();
    TS::TopLevelTypeName a("System", "List", 1);
    TS::TopLevelTypeName b("system", "list", 1);
    EXPECT_TRUE(ord.Equals(a, a));
    EXPECT_TRUE(icase.Equals(a, a));
    EXPECT_EQ(ord.Equals(a, b), ord.Equals(b, a));
    EXPECT_EQ(icase.Equals(a, b), icase.Equals(b, a));
}

// ---------------------------------------------------------------------------
// The class shape matches the C# `sealed class`: `final`, constructible from a
// `const StringComparer&`, not default-constructible, and non-copy-assignable (the
// reference member).
// ---------------------------------------------------------------------------
TEST(TS_TopLevelTypeNameComparer, ClassShapeMatchesCSharp)
{
    static_assert(std::is_final_v<TS::TopLevelTypeNameComparer>,
                  "TopLevelTypeNameComparer is final (the C# sealed class)");
    static_assert(std::is_constructible_v<TS::TopLevelTypeNameComparer,
                                          const TS::StringComparer&>,
                  "Constructible from a const StringComparer& (the C# ctor)");
    static_assert(!std::is_default_constructible_v<TS::TopLevelTypeNameComparer>,
                  "Not default-constructible (the ctor requires a StringComparer)");
    static_assert(!std::is_copy_assignable_v<TS::TopLevelTypeNameComparer>,
                  "Not copy-assignable (the reference member, matching the C# readonly field)");
    static_assert(!std::is_abstract_v<TS::TopLevelTypeNameComparer>,
                  "Concrete (not abstract)");
}
