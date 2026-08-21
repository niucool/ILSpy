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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder static helpers (cpp/Decompiler/CSharp/Syntax/
// TypeSystemAstBuilder.hpp, the port of the self-contained static methods on
// ICSharpCode.Decompiler/CSharp/Syntax/TypeSystemAstBuilder.cs). The full
// TypeSystemAstBuilder class is the long-pole CSharpAmbience blocker and is
// deferred; the static helpers that depend only on already-ported TypeSystem /
// Syntax leaves land here incrementally. The first helper is ModifierFromAccessibility
// (TypeSystemAstBuilder.cs line 2497), a pure switch on Accessibility (D373) mapping
// a symbol's visibility to the Syntax Modifiers bits (D270), with the
// usePrivateProtected gate for the C# 7.2 `private protected` accessibility.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include <gtest/gtest.h>

namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ILSpy::Decompiler::TypeSystem;
using Syntax::Modifiers;
using TS::Accessibility;

// ---------------------------------------------------------------------------
// The six real accessibility values each map to the expected single modifier
// or modifier pair, independent of usePrivateProtected (only ProtectedAndInternal
// reads the gate).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityMapsPrivateToPrivate)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Private, false),
              Modifiers::Private);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Private, true),
              Modifiers::Private);
}

TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityMapsPublicToPublic)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Public, false),
              Modifiers::Public);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Public, true),
              Modifiers::Public);
}

TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityMapsProtectedToProtected)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Protected, false),
              Modifiers::Protected);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Protected, true),
              Modifiers::Protected);
}

TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityMapsInternalToInternal)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Internal, false),
              Modifiers::Internal);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::Internal, true),
              Modifiers::Internal);
}

// ---------------------------------------------------------------------------
// ProtectedOrInternal ("protected internal") maps to the bitwise OR of the two
// visibility bits, independent of usePrivateProtected -- the C# `protected internal`
// is Protected | Internal, the most-accessible combination.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityMapsProtectedOrInternalToBoth)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::ProtectedOrInternal, false),
              Modifiers::Protected | Modifiers::Internal);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::ProtectedOrInternal, true),
              Modifiers::Protected | Modifiers::Internal);
}

// ---------------------------------------------------------------------------
// ProtectedAndInternal ("private protected") is the load-bearing usePrivateProtected
// gate: when true it emits `private protected` (Private | Protected, the C# 7.2
// accessibility); when false it falls back to the pre-C#-7.2 `protected` (Protected).
// This is the sole case the bool arg changes the result.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, ProtectedAndInternalEmitsPrivateProtectedWhenUsePrivateProtectedTrue)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::ProtectedAndInternal, true),
              Modifiers::Private | Modifiers::Protected);
}

TEST(TypeSystemAstBuilderTest, ProtectedAndInternalFallsBackToProtectedWhenUsePrivateProtectedFalse)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::ProtectedAndInternal, false),
              Modifiers::Protected);
}

// ---------------------------------------------------------------------------
// The default case (Accessibility::None or any other value) returns Modifiers::None,
// the no-modifier sentinel.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, ModifierFromAccessibilityReturnsNoneForDefault)
{
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::None, false),
              Modifiers::None);
    EXPECT_EQ(Syntax::ModifierFromAccessibility(Accessibility::None, true),
              Modifiers::None);
}

// ---------------------------------------------------------------------------
// The bit values are faithful to the C# Modifiers enum: Protected | Internal and
// Private | Protected are distinct composite values (0x0006 and 0x0005), NOT the
// same as any single modifier -- pinning the composite-mapping convention.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, CompositeModifiersAreDistinctFromSingleModifiers)
{
    const auto protectedOrInternal = Syntax::ModifierFromAccessibility(
        Accessibility::ProtectedOrInternal, false);
    EXPECT_NE(protectedOrInternal, Modifiers::Protected);
    EXPECT_NE(protectedOrInternal, Modifiers::Internal);

    const auto privateProtected = Syntax::ModifierFromAccessibility(
        Accessibility::ProtectedAndInternal, true);
    EXPECT_NE(privateProtected, Modifiers::Private);
    EXPECT_NE(privateProtected, Modifiers::Protected);
}
