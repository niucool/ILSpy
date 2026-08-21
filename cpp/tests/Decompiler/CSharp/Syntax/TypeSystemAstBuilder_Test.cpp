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
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// ===========================================================================
// Pure-math fraction helpers (TypeSystemAstBuilder.cs lines 1458-1490 and
// 1725-1773), the family backing ConvertFloatingPointLiteral. They are pure
// (no type-system state) and port ahead of the instance method that drives
// them. The C# `long` is 64-bit, so the port uses std::int64_t; the C#
// `(long Num, long Den)` tuple ports to std::pair<std::int64_t, std::int64_t>.
// ===========================================================================

// ---------------------------------------------------------------------------
// IsValidFraction: a zero numerator or non-positive denominator is never valid.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, IsValidFractionRejectsZeroNumerator)
{
    EXPECT_FALSE(Syntax::IsValidFraction(0, 5));
}

TEST(TypeSystemAstBuilderTest, IsValidFractionRejectsNonPositiveDenominator)
{
    EXPECT_FALSE(Syntax::IsValidFraction(1, 0));
    EXPECT_FALSE(Syntax::IsValidFraction(1, -2));
    EXPECT_FALSE(Syntax::IsValidFraction(3, -6));
}

// ---------------------------------------------------------------------------
// A whole fraction (den == 1) or a unit fraction (|num| == 1) is always valid,
// regardless of the 5-smooth denominator gate -- these are the short-form cases.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, IsValidFractionAcceptsDenominatorOne)
{
    EXPECT_TRUE(Syntax::IsValidFraction(3, 1));
    EXPECT_TRUE(Syntax::IsValidFraction(-3, 1));
}

TEST(TypeSystemAstBuilderTest, IsValidFractionAcceptsUnitNumerator)
{
    EXPECT_TRUE(Syntax::IsValidFraction(1, 7));
    EXPECT_TRUE(Syntax::IsValidFraction(-1, 7));
    EXPECT_TRUE(Syntax::IsValidFraction(1, 11));
}

// ---------------------------------------------------------------------------
// A proper fraction (|num| < den) with a 5-smooth denominator (divisible by
// 2, 3, or 5) is valid; a non-5-smooth denominator is rejected even when
// proper -- the gate that rejects coincidental fractions such as 113/355.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, IsValidFractionAcceptsProperFractionWithSmoothDenominator)
{
    EXPECT_TRUE(Syntax::IsValidFraction(3, 6));   // 6 % 2 == 0
    EXPECT_TRUE(Syntax::IsValidFraction(3, 9));   // 9 % 3 == 0
    EXPECT_TRUE(Syntax::IsValidFraction(2, 10));  // 10 % 2 == 0
    EXPECT_TRUE(Syntax::IsValidFraction(1, 15));  // |1| == 1 (also smooth)
}

TEST(TypeSystemAstBuilderTest, IsValidFractionRejectsProperFractionWithNonSmoothDenominator)
{
    EXPECT_FALSE(Syntax::IsValidFraction(3, 7));  // 7 not divisible by 2, 3, or 5
    EXPECT_FALSE(Syntax::IsValidFraction(2, 7));
    EXPECT_FALSE(Syntax::IsValidFraction(5, 13));
}

// ---------------------------------------------------------------------------
// An improper fraction (|num| >= den) is rejected (it is not a proper
// fraction; the caller would reduce it first).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, IsValidFractionRejectsImproperFraction)
{
    EXPECT_FALSE(Syntax::IsValidFraction(7, 3));
    EXPECT_FALSE(Syntax::IsValidFraction(6, 6));
    EXPECT_FALSE(Syntax::IsValidFraction(-7, 3));
}

// ---------------------------------------------------------------------------
// EqualDoubles / EqualFloats are plain IEEE equality. Equal values (including
// the +0.0 == -0.0 identity) return true; different values and NaN return false
// (NaN != NaN is the IEEE rule the C# == operator follows).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, EqualDoublesReturnsTrueForEqualValues)
{
    EXPECT_TRUE(Syntax::EqualDoubles(1.5, 1.5));
}

TEST(TypeSystemAstBuilderTest, EqualDoublesReturnsFalseForDifferentValues)
{
    EXPECT_FALSE(Syntax::EqualDoubles(1.5, 2.5));
}

TEST(TypeSystemAstBuilderTest, EqualDoublesTreatsSignedZeroAsEqual)
{
    EXPECT_TRUE(Syntax::EqualDoubles(0.0, -0.0));
}

TEST(TypeSystemAstBuilderTest, EqualDoublesReturnsFalseForNaN)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(Syntax::EqualDoubles(nan, nan));
}

TEST(TypeSystemAstBuilderTest, EqualFloatsReturnsTrueForEqualValues)
{
    EXPECT_TRUE(Syntax::EqualFloats(1.5f, 1.5f));
}

TEST(TypeSystemAstBuilderTest, EqualFloatsReturnsFalseForDifferentValues)
{
    EXPECT_FALSE(Syntax::EqualFloats(1.5f, 2.5f));
}

// ---------------------------------------------------------------------------
// IsEqual dispatches to EqualDoubles/EqualFloats by the isDouble flag,
// dividing the candidate (num, den) in the matching floating-point precision.
// The C# `(double)`/`(float)` cast on a mismatched box throws InvalidCastException;
// the std::any port throws std::bad_any_cast.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, IsEqualReturnsTrueWhenFractionMatchesDoubleValue)
{
    EXPECT_TRUE(Syntax::IsEqual(1, 2, std::any(0.5), true));
}

TEST(TypeSystemAstBuilderTest, IsEqualReturnsFalseWhenFractionDoesNotMatchDoubleValue)
{
    EXPECT_FALSE(Syntax::IsEqual(1, 3, std::any(0.5), true));
}

TEST(TypeSystemAstBuilderTest, IsEqualReturnsTrueWhenFractionMatchesFloatValue)
{
    EXPECT_TRUE(Syntax::IsEqual(1, 2, std::any(0.5f), false));
}

TEST(TypeSystemAstBuilderTest, IsEqualReturnsFalseWhenFractionDoesNotMatchFloatValue)
{
    EXPECT_FALSE(Syntax::IsEqual(1, 3, std::any(0.5f), false));
}

TEST(TypeSystemAstBuilderTest, IsEqualThrowsOnWrongBoxedType)
{
    // A float box with isDouble=true: the double cast on a float any throws.
    EXPECT_THROW(Syntax::IsEqual(1, 2, std::any(0.5f), true), std::bad_any_cast);
    // A double box with isDouble=false: the float cast on a double any throws.
    EXPECT_THROW(Syntax::IsEqual(1, 2, std::any(0.5), false), std::bad_any_cast);
}

// ---------------------------------------------------------------------------
// FractionApprox returns the best rational approximation within the
// max-denominator bound. Exact rationals (1/2, 1/3) round-trip; a negative
// value re-applies the sign to the numerator.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, FractionApproxHalfReturnsOneHalf)
{
    auto r = Syntax::FractionApprox(0.5, 100);
    EXPECT_EQ(r.first, 1);
    EXPECT_EQ(r.second, 2);
}

TEST(TypeSystemAstBuilderTest, FractionApproxThirdReturnsOneThird)
{
    auto r = Syntax::FractionApprox(1.0 / 3.0, 100);
    EXPECT_EQ(r.first, 1);
    EXPECT_EQ(r.second, 3);
}

TEST(TypeSystemAstBuilderTest, FractionApproxNegativeHalfReturnsNegativeOneHalf)
{
    auto r = Syntax::FractionApprox(-0.5, 100);
    EXPECT_EQ(r.first, -1);
    EXPECT_EQ(r.second, 2);
}

TEST(TypeSystemAstBuilderTest, FractionApproxNegativeThirdReturnsNegativeOneThird)
{
    auto r = Syntax::FractionApprox(-1.0 / 3.0, 100);
    EXPECT_EQ(r.first, -1);
    EXPECT_EQ(r.second, 3);
}

// ---------------------------------------------------------------------------
// The celebrated PI approximation 355/113 is the best convergent within
// maxDenominator 1000 (the next convergent 103993/33102 exceeds the bound).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, FractionApproxPiReturnsThreeFiftyFiveOverOneHundredThirteen)
{
    const double pi = 3.141592653589793; // Math.PI
    auto r = Syntax::FractionApprox(pi, 1000);
    EXPECT_EQ(r.first, 355);
    EXPECT_EQ(r.second, 113);
}

// ---------------------------------------------------------------------------
// When maxDenominator cuts off between two convergents, the best result may
// be a SEMI-CONVERGENT (the secondN/secondD path). For value 0.6 with bound 4,
// the convergent 1/2 (delta 0.1) loses to the semi-convergent 2/3 (delta
// 0.0667), so the second path is taken.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, FractionApproxPicksSemiConvergentWhenCloser)
{
    auto r = Syntax::FractionApprox(0.6, 4);
    EXPECT_EQ(r.first, 2);
    EXPECT_EQ(r.second, 3);
}

TEST(TypeSystemAstBuilderTest, FractionApproxPicksNegativeSemiConvergentWhenCloser)
{
    auto r = Syntax::FractionApprox(-0.6, 4);
    EXPECT_EQ(r.first, -2);
    EXPECT_EQ(r.second, 3);
}

// ---------------------------------------------------------------------------
// The magnitude guard: |value| > 0x7FFFFFFF returns (0, 0) so the sign-stripped
// continued-fraction loop is never entered with an overflow-prone magnitude.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, FractionApproxLargeMagnitudeReturnsZeroZero)
{
    auto r = Syntax::FractionApprox(1e18, 1000);
    EXPECT_EQ(r.first, 0);
    EXPECT_EQ(r.second, 0);
}

TEST(TypeSystemAstBuilderTest, FractionApproxLargeNegativeMagnitudeReturnsZeroZero)
{
    auto r = Syntax::FractionApprox(-1e18, 1000);
    EXPECT_EQ(r.first, 0);
    EXPECT_EQ(r.second, 0);
}

// ---------------------------------------------------------------------------
// FractionApprox(0, ...) yields (0, 1) (the algorithm's first convergent for a
// zero value); IsValidFraction(0, 1) then rejects it, so zero is rendered as a
// plain literal rather than a fraction by the caller.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, FractionApproxZeroReturnsZeroOverOne)
{
    auto r = Syntax::FractionApprox(0.0, 100);
    EXPECT_EQ(r.first, 0);
    EXPECT_EQ(r.second, 1);
}

// ---------------------------------------------------------------------------
// GetNullabilityDisambiguator (TypeSystemAstBuilder.cs line 2707) returns the
// constraint keyword that keeps `T?` as a nullable annotation on an override or
// explicit interface implementation, or nullopt where the type parameter
// neither needs nor permits one. The switch is on tp.IsReferenceType (the D429
// std::optional<bool> accessor ITypeParameter inherits from IType):
//   true    => "class"    (a reference type)
//   nullopt => "default"  (neither ref nor value)
//   false   => nullopt     (a value type uses Nullable<T>)
//
// The test stub is a minimal concrete ITypeParameter that exposes a configurable
// IsReferenceType() (the sole accessor the helper reads); every other
// ITypeParameter / IType / ISymbol pure-virtual is overridden with a trivial
// default.
// ---------------------------------------------------------------------------
namespace {

class TestDisambiguatorTypeParameter : public TS::ITypeParameter {
public:
    explicit TestDisambiguatorTypeParameter(std::optional<bool> isReferenceType)
        : isReferenceType_(isReferenceType) {}

    // --- IType ---
    TS::TypeKind Kind() const override { return TS::TypeKind::TypeParameter; }
    // The single Name() override is the final overrider for IType::Name,
    // ISymbol::Name, and ITypeParameter::Name (the D381 diamond disambiguation).
    std::string Name() const override { return {}; }
    std::string ReflectionName() const override { return {}; }
    int TypeParameterCount() const override { return 0; }
    std::optional<bool> IsReferenceType() const override { return isReferenceType_; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::TypeParameter; }

    // --- ITypeParameter ---
    TS::SymbolKind OwnerType() const override { return TS::SymbolKind::TypeDefinition; }
    const TS::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return 0; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::VarianceModifier Variance() const override { return TS::VarianceModifier::Invariant; }
    TS::ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<TS::ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    TS::Nullability NullabilityConstraint() const override { return TS::Nullability::Oblivious; }
    std::vector<TS::TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const TS::IType& /*other*/) const override { return false; }

private:
    std::optional<bool> isReferenceType_;
};

} // namespace

TEST(TypeSystemAstBuilderTest, GetNullabilityDisambiguatorReturnsClassForReferenceType)
{
    TestDisambiguatorTypeParameter tp(std::optional<bool>(true));
    auto r = Syntax::GetNullabilityDisambiguator(tp);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "class");
}

TEST(TypeSystemAstBuilderTest, GetNullabilityDisambiguatorReturnsDefaultForUnconstrained)
{
    TestDisambiguatorTypeParameter tp(std::nullopt);
    auto r = Syntax::GetNullabilityDisambiguator(tp);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "default");
}

TEST(TypeSystemAstBuilderTest, GetNullabilityDisambiguatorReturnsNulloptForValueType)
{
    TestDisambiguatorTypeParameter tp(std::optional<bool>(false));
    auto r = Syntax::GetNullabilityDisambiguator(tp);
    EXPECT_FALSE(r.has_value());
}

TEST(TypeSystemAstBuilderTest, GetNullabilityDisambiguatorDistinguishesAllThreeStates)
{
    TestDisambiguatorTypeParameter refType(std::optional<bool>(true));
    TestDisambiguatorTypeParameter unknown(std::nullopt);
    TestDisambiguatorTypeParameter valueType(std::optional<bool>(false));

    auto rRef = Syntax::GetNullabilityDisambiguator(refType);
    auto rUnknown = Syntax::GetNullabilityDisambiguator(unknown);
    auto rValue = Syntax::GetNullabilityDisambiguator(valueType);

    ASSERT_TRUE(rRef.has_value());
    ASSERT_TRUE(rUnknown.has_value());
    EXPECT_FALSE(rValue.has_value());
    EXPECT_NE(*rRef, *rUnknown);
}
