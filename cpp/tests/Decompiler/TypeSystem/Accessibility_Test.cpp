// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Accessibility` (cpp/Decompiler/TypeSystem/Accessibility.hpp, the port
// of ICSharpCode.Decompiler/TypeSystem/Accessibility.cs). `Accessibility` is the
// visibility of a type-system entity and a leaf dependency of `TypeSystemAstBuilder`
// (whose `ModifierFromAccessibility` switches on it). The tests pin the enum's
// integer values (the partial-order helpers depend on them being 0..6 in
// declaration order), the relational operators (the C# built-in enum comparison),
// and the three partial-order helpers -- especially the protected-vs-internal
// correction, the sole place the integer order does not match the partial order.

#include "Decompiler/TypeSystem/Accessibility.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// The enum values are 0..6 in C# declaration order. The partial-order helpers
// depend on this exact integer ordering (the C# comment: the enum is sorted
// similar to the partial order), so the values are pinned explicitly.
// ---------------------------------------------------------------------------
TEST(AccessibilityTest, EnumValuesMatchCSharpDeclarationOrder)
{
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::None), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::Private), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::ProtectedAndInternal), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::Protected), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::Internal), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::ProtectedOrInternal), 5);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Accessibility::Public), 6);
}

// ---------------------------------------------------------------------------
// The relational operators compare the underlying uint8_t values, mirroring
// the C# built-in enum comparison the partial-order helpers use.
// ---------------------------------------------------------------------------
TEST(AccessibilityTest, RelationalOperatorsCompareUnderlyingValues)
{
    EXPECT_TRUE(TS::Accessibility::None < TS::Accessibility::Public);
    EXPECT_FALSE(TS::Accessibility::Public < TS::Accessibility::None);
    EXPECT_TRUE(TS::Accessibility::Private <= TS::Accessibility::Private);
    EXPECT_TRUE(TS::Accessibility::Private >= TS::Accessibility::Private);
    EXPECT_TRUE(TS::Accessibility::Internal > TS::Accessibility::Protected);
    EXPECT_FALSE(TS::Accessibility::Protected > TS::Accessibility::Internal);
    EXPECT_TRUE(TS::Accessibility::Public >= TS::Accessibility::Internal);
    EXPECT_FALSE(TS::Accessibility::Private >= TS::Accessibility::Internal);
}

// ---------------------------------------------------------------------------
// LessThanOrEqual is the partial order: true iff b is accessible everywhere a
// is. The two documented chains hold, and the protected-vs-internal pair is the
// sole correction -- they are incomparable, so both directions are false even
// though the integers have Protected(3) < Internal(4).
// ---------------------------------------------------------------------------
TEST(AccessibilityTest, LessThanOrEqualPartialOrder)
{
    // reflexive
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::Public, TS::Accessibility::Public));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::None, TS::Accessibility::None));
    // none is accessible nowhere, so none <= everything
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::None, TS::Accessibility::Public));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::None, TS::Accessibility::Private));
    // chain: private <= protected_and_internal <= protected <= protected_or_internal <= public
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::Private, TS::Accessibility::Protected));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::ProtectedAndInternal, TS::Accessibility::Protected));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::Protected, TS::Accessibility::ProtectedOrInternal));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::ProtectedOrInternal, TS::Accessibility::Public));
    // chain: private <= protected_and_internal <= internal <= protected_or_internal <= public
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::ProtectedAndInternal, TS::Accessibility::Internal));
    EXPECT_TRUE(TS::LessThanOrEqual(TS::Accessibility::Internal, TS::Accessibility::ProtectedOrInternal));
    // THE protected-vs-internal correction: the two are incomparable
    EXPECT_FALSE(TS::LessThanOrEqual(TS::Accessibility::Protected, TS::Accessibility::Internal));
    EXPECT_FALSE(TS::LessThanOrEqual(TS::Accessibility::Internal, TS::Accessibility::Protected));
    // public is accessible everywhere, so public <= only public
    EXPECT_FALSE(TS::LessThanOrEqual(TS::Accessibility::Public, TS::Accessibility::Internal));
    EXPECT_FALSE(TS::LessThanOrEqual(TS::Accessibility::Public, TS::Accessibility::Protected));
}

// ---------------------------------------------------------------------------
// Intersect is the more-restrictive of the two, except the protected-vs-internal
// pair intersects to ProtectedAndInternal ("private protected"). It is
// symmetric: swapping the operands gives the same result (the swap inside the
// helper normalises the order).
// ---------------------------------------------------------------------------
TEST(AccessibilityTest, IntersectReturnsMoreRestrictive)
{
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Public, TS::Accessibility::Private), TS::Accessibility::Private);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Private, TS::Accessibility::Public), TS::Accessibility::Private);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Protected, TS::Accessibility::Internal), TS::Accessibility::ProtectedAndInternal);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Internal, TS::Accessibility::Protected), TS::Accessibility::ProtectedAndInternal);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Protected, TS::Accessibility::Public), TS::Accessibility::Protected);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::None, TS::Accessibility::Public), TS::Accessibility::None);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Public, TS::Accessibility::Public), TS::Accessibility::Public);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::Private, TS::Accessibility::Protected), TS::Accessibility::Private);
    EXPECT_EQ(TS::Intersect(TS::Accessibility::ProtectedOrInternal, TS::Accessibility::Protected), TS::Accessibility::Protected);
}

// ---------------------------------------------------------------------------
// Union is the less-restrictive of the two, except the protected-vs-internal
// pair unions to ProtectedOrInternal ("protected internal"). It is symmetric.
// ---------------------------------------------------------------------------
TEST(AccessibilityTest, UnionReturnsLessRestrictive)
{
    EXPECT_EQ(TS::Union(TS::Accessibility::Private, TS::Accessibility::Public), TS::Accessibility::Public);
    EXPECT_EQ(TS::Union(TS::Accessibility::Public, TS::Accessibility::Private), TS::Accessibility::Public);
    EXPECT_EQ(TS::Union(TS::Accessibility::Protected, TS::Accessibility::Internal), TS::Accessibility::ProtectedOrInternal);
    EXPECT_EQ(TS::Union(TS::Accessibility::Internal, TS::Accessibility::Protected), TS::Accessibility::ProtectedOrInternal);
    EXPECT_EQ(TS::Union(TS::Accessibility::None, TS::Accessibility::Private), TS::Accessibility::Private);
    EXPECT_EQ(TS::Union(TS::Accessibility::Protected, TS::Accessibility::Public), TS::Accessibility::Public);
    EXPECT_EQ(TS::Union(TS::Accessibility::Public, TS::Accessibility::Public), TS::Accessibility::Public);
    EXPECT_EQ(TS::Union(TS::Accessibility::None, TS::Accessibility::None), TS::Accessibility::None);
    EXPECT_EQ(TS::Union(TS::Accessibility::ProtectedOrInternal, TS::Accessibility::Internal), TS::Accessibility::ProtectedOrInternal);
}
