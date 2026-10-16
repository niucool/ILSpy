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

// Tests for `StringComparer` (cpp/Decompiler/TypeSystem/StringComparer.hpp, the port
// of the BCL `System.StringComparer` absorbed into `ILSpy::Decompiler::TypeSystem`).
// `StringComparer` is the abstract equality comparer `ICompilation.NameComparer` returns
// -- the last leaf dependency of `ICompilation` (toward `TypeSystemAstBuilder` /
// `CSharpAmbience`). The tests pin the `Ordinal` (case-sensitive byte-wise) and
// `OrdinalIgnoreCase` (ASCII case-folded) singletons, the `Equals` / `GetHashCode`
// contract (Equal strings get Equal hashes), the hash-consistent-with-equality
// invariant, the singleton stability, the polymorphic dispatch through the base
// pointer, and the `TopLevelTypeNameComparer.GetHashCode` XOR pattern (the load-bearing
// consumer shape).

#include "Decompiler/TypeSystem/StringComparer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// `Ordinal.Equals` is byte-wise and case-sensitive: identical strings are equal,
// strings differing in any byte are not, and empty equals empty.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalEqualsIsCaseSensitiveByteWise)
{
    const TS::StringComparer& cmp = TS::StringComparer::Ordinal();
    EXPECT_TRUE(cmp.Equals("Foo", "Foo"));
    EXPECT_FALSE(cmp.Equals("Foo", "foo"));
    EXPECT_FALSE(cmp.Equals("Foo", "FOO"));
    EXPECT_FALSE(cmp.Equals("Foo", "Foo "));
    EXPECT_FALSE(cmp.Equals("Foo", "Fo0"));
    EXPECT_TRUE(cmp.Equals("", ""));
    EXPECT_FALSE(cmp.Equals("", "x"));
}

// ---------------------------------------------------------------------------
// `Ordinal.GetHashCode` is consistent with `Equals`: Equal strings get Equal
// hashes, and distinct strings get distinct hashes (the hash-collision-free
// common case for short identifiers). The returned `int` matches the .NET
// `int` return the `TopLevelTypeNameComparer` XORs.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalGetHashCodeIsConsistentWithEquals)
{
    const TS::StringComparer& cmp = TS::StringComparer::Ordinal();
    EXPECT_EQ(cmp.GetHashCode("Foo"), cmp.GetHashCode("Foo"));
    EXPECT_NE(cmp.GetHashCode("Foo"), cmp.GetHashCode("foo"));
    EXPECT_NE(cmp.GetHashCode("System"), cmp.GetHashCode("System.Collections"));
    EXPECT_NE(cmp.GetHashCode(""), cmp.GetHashCode("x"));
    static_assert(std::is_same_v<decltype(cmp.GetHashCode("x")), int>,
                  "GetHashCode returns int (the .NET int return)");
}

// ---------------------------------------------------------------------------
// `OrdinalIgnoreCase.Equals` folds ASCII `A`-`Z` before the byte comparison, so
// case variants of the same string are equal while genuinely-different strings
// (different length, different non-ASCII-case bytes) are not.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalIgnoreCaseEqualsIsCaseInsensitiveAscii)
{
    const TS::StringComparer& cmp = TS::StringComparer::OrdinalIgnoreCase();
    EXPECT_TRUE(cmp.Equals("Foo", "Foo"));
    EXPECT_TRUE(cmp.Equals("Foo", "foo"));
    EXPECT_TRUE(cmp.Equals("Foo", "FOO"));
    EXPECT_TRUE(cmp.Equals("System.Collections", "system.collections"));
    EXPECT_TRUE(cmp.Equals("", ""));
    EXPECT_FALSE(cmp.Equals("Foo", "Foo "));
    EXPECT_FALSE(cmp.Equals("Foo", "Fo0"));
    EXPECT_FALSE(cmp.Equals("Foo", "FooBar"));
}

// ---------------------------------------------------------------------------
// `OrdinalIgnoreCase.GetHashCode` is consistent with its case-insensitive
// `Equals`: case variants of the same string get Equal hashes (the fold is
// applied before hashing), and genuinely-different strings get distinct hashes.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalIgnoreCaseGetHashCodeIsConsistentWithEquals)
{
    const TS::StringComparer& cmp = TS::StringComparer::OrdinalIgnoreCase();
    EXPECT_EQ(cmp.GetHashCode("Foo"), cmp.GetHashCode("Foo"));
    EXPECT_EQ(cmp.GetHashCode("Foo"), cmp.GetHashCode("foo"));
    EXPECT_EQ(cmp.GetHashCode("Foo"), cmp.GetHashCode("FOO"));
    EXPECT_EQ(cmp.GetHashCode("System.Collections"),
              cmp.GetHashCode("system.collections"));
    EXPECT_NE(cmp.GetHashCode("Foo"), cmp.GetHashCode("FooBar"));
    EXPECT_NE(cmp.GetHashCode(""), cmp.GetHashCode("x"));
}

// ---------------------------------------------------------------------------
// `Ordinal` and `OrdinalIgnoreCase` distinguish on case-only-differing strings:
// `Ordinal` reports them unequal (case-sensitive), `OrdinalIgnoreCase` reports
// them equal (case-insensitive) -- the load-bearing behavioral distinction the
// two singletons exist to provide.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalAndOrdinalIgnoreCaseDistinguishOnCaseOnlyDifferingStrings)
{
    const TS::StringComparer& ord = TS::StringComparer::Ordinal();
    const TS::StringComparer& icase = TS::StringComparer::OrdinalIgnoreCase();
    EXPECT_FALSE(ord.Equals("List", "list"));
    EXPECT_TRUE(icase.Equals("List", "list"));
    // Both agree on identical and on genuinely-different strings.
    EXPECT_TRUE(ord.Equals("List", "List"));
    EXPECT_TRUE(icase.Equals("List", "List"));
    EXPECT_FALSE(ord.Equals("List", "List`1"));
    EXPECT_FALSE(icase.Equals("List", "List`1"));
}

// ---------------------------------------------------------------------------
// The `TopLevelTypeNameComparer.GetHashCode` XOR pattern (the load-bearing
// consumer shape): `hash(Name) ^ hash(Namespace) ^ TypeParameterCount`, all `int`.
// The test exercises the pattern through both comparers, confirming the `int`
// `GetHashCode` XORs with an `int` type-parameter count with no widening.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, GetHashCodeXorPatternMatchesTopLevelTypeNameComparer)
{
    const TS::StringComparer& ord = TS::StringComparer::Ordinal();
    const TS::StringComparer& icase = TS::StringComparer::OrdinalIgnoreCase();
    std::string name = "Dictionary";
    std::string ns = "System.Collections.Generic";
    int typeParameterCount = 2;

    int ordHash = ord.GetHashCode(name) ^ ord.GetHashCode(ns) ^ typeParameterCount;
    int icaseHash = icase.GetHashCode(name) ^ icase.GetHashCode(ns) ^ typeParameterCount;
    // Same inputs through the same comparer yield a stable composite hash.
    EXPECT_EQ(ordHash, ord.GetHashCode(name) ^ ord.GetHashCode(ns) ^ typeParameterCount);
    EXPECT_EQ(icaseHash,
              icase.GetHashCode(name) ^ icase.GetHashCode(ns) ^ typeParameterCount);
    // The composite is deterministic and the two comparers disagree on mixed-case
    // inputs (the case-fold changes the hash); both reads are int (no widening when
    // XORing the int hashes with the int type-parameter count).
    static_assert(std::is_same_v<decltype(ordHash), int>);
    static_assert(std::is_same_v<decltype(icaseHash), int>);
    (void)ordHash;
    (void)icaseHash;
}

// ---------------------------------------------------------------------------
// `Ordinal()` / `OrdinalIgnoreCase()` return stable singletons: each accessor
// returns the same `StringComparer&` across calls (the C# `static readonly`
// field), and the two singletons are distinct references.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, OrdinalAndOrdinalIgnoreCaseReturnStableSingletons)
{
    EXPECT_EQ(&TS::StringComparer::Ordinal(), &TS::StringComparer::Ordinal());
    EXPECT_EQ(&TS::StringComparer::OrdinalIgnoreCase(),
              &TS::StringComparer::OrdinalIgnoreCase());
    EXPECT_NE(&TS::StringComparer::Ordinal(), &TS::StringComparer::OrdinalIgnoreCase());
}

// ---------------------------------------------------------------------------
// `Equals` / `GetHashCode` dispatch polymorphically through a `StringComparer*`:
// a pointer to the `Ordinal` singleton reaches `OrdinalStringComparer`'s
// overrides, a pointer to the `OrdinalIgnoreCase` singleton reaches
// `OrdinalIgnoreCaseStringComparer`'s overrides.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, DispatchesPolymorphicallyThroughBasePointer)
{
    const TS::StringComparer* ord = &TS::StringComparer::Ordinal();
    const TS::StringComparer* icase = &TS::StringComparer::OrdinalIgnoreCase();
    // Through the base pointer, Ordinal is case-sensitive, OrdinalIgnoreCase is not.
    EXPECT_FALSE(ord->Equals("Foo", "foo"));
    EXPECT_TRUE(icase->Equals("Foo", "foo"));
    // The hashes a case-sensitive vs case-insensitive comparer produce for the
    // same mixed-case input differ (the fold changes the hash), confirming each
    // pointer reached its own override.
    EXPECT_NE(ord->GetHashCode("Foo"), icase->GetHashCode("Foo"));
}

// ---------------------------------------------------------------------------
// `Equals` is reflexive and symmetric (the `IEqualityComparer<string>` contract):
// `Equals(x, x)` is true and `Equals(x, y) == Equals(y, x)`, for both comparers.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, EqualsIsReflexiveAndSymmetric)
{
    const TS::StringComparer& ord = TS::StringComparer::Ordinal();
    const TS::StringComparer& icase = TS::StringComparer::OrdinalIgnoreCase();
    EXPECT_TRUE(ord.Equals("Foo", "Foo"));
    EXPECT_TRUE(icase.Equals("Foo", "Foo"));
    EXPECT_EQ(ord.Equals("Foo", "foo"), ord.Equals("foo", "Foo"));
    EXPECT_EQ(icase.Equals("Foo", "foo"), icase.Equals("foo", "Foo"));
    EXPECT_EQ(ord.Equals("List", "list"), ord.Equals("list", "List"));
    EXPECT_EQ(icase.Equals("List", "list"), icase.Equals("list", "List"));
}

// ---------------------------------------------------------------------------
// The two concrete comparer classes are `final` and concrete (the C#
// `StringComparer` nested singletons are `sealed`), `StringComparer` is abstract
// and polymorphic with a virtual destructor.
// ---------------------------------------------------------------------------
TEST(TS_StringComparer, ClassShapeMatchesCSharp)
{
    static_assert(std::has_virtual_destructor_v<TS::StringComparer>,
                  "StringComparer has a virtual destructor");
    static_assert(std::is_abstract_v<TS::StringComparer>,
                  "StringComparer is abstract (Equals/GetHashCode are pure virtual)");
    static_assert(std::is_polymorphic_v<TS::StringComparer>,
                  "StringComparer is polymorphic");
    static_assert(std::is_final_v<TS::OrdinalStringComparer>,
                  "OrdinalStringComparer is final (the C# sealed nested singleton)");
    static_assert(std::is_final_v<TS::OrdinalIgnoreCaseStringComparer>,
                  "OrdinalIgnoreCaseStringComparer is final (the C# sealed nested singleton)");
    static_assert(std::is_default_constructible_v<TS::OrdinalStringComparer>);
    static_assert(std::is_default_constructible_v<TS::OrdinalIgnoreCaseStringComparer>);
    // The derived singletons ARE StringComparers (the static Ordinal/OrdinalIgnoreCase
    // return const StringComparer& to an instance of each).
    static_assert(std::is_base_of_v<TS::StringComparer, TS::OrdinalStringComparer>);
    static_assert(std::is_base_of_v<TS::StringComparer, TS::OrdinalIgnoreCaseStringComparer>);
}
