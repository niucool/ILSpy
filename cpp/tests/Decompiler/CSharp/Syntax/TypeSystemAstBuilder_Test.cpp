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
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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

// ===========================================================================
// MergeReadOnlyModifiers (TypeSystemAstBuilder.cs line 2277), a local static
// helper inside ConvertProperty / ConvertIndexer / ConvertCustomEvent that lifts
// the `readonly` modifier from the accessor(s) to the declaration when all
// carrying accessors agree -- the C# 7.2 `readonly` on a property/event is stored
// on the declaration, but the resolver may have placed it on the individual
// accessors. The C# `EntityDeclaration decl` (non-null) ports to
// `EntityDeclaration&`; the C# `Accessor?` (nullable) ports to `Accessor*`.
//
// The tests use a `MethodDeclaration` for the `decl` parameter (a concrete
// `EntityDeclaration` with an empty ctor) and `Accessor` instances for the two
// accessor parameters; `Modifiers()` / `HasModifier()` are inherited from
// `EntityDeclaration` and work identically on both.
// ===========================================================================

// accessor1 null: the helper returns immediately; decl and accessor2 are
// untouched (the early-return before any modifier read).
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersNoOpWhenAccessor1IsNull)
{
    Syntax::MethodDeclaration decl;
    decl.Modifiers(Modifiers::Public);
    Syntax::Accessor accessor2;
    accessor2.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, nullptr, &accessor2);

    EXPECT_EQ(decl.Modifiers(), Modifiers::Public);
    EXPECT_EQ(accessor2.Modifiers(), Modifiers::Readonly);
}

// accessor1 has readonly and accessor2 is null: the readonly bit is lifted from
// accessor1 to the declaration (the first if-branch).
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersLiftsReadOnlyFromSingleAccessorToDecl)
{
    Syntax::MethodDeclaration decl;
    Syntax::Accessor accessor1;
    accessor1.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, &accessor1, nullptr);

    EXPECT_FALSE(accessor1.HasModifier(Modifiers::Readonly));
    EXPECT_TRUE(decl.HasModifier(Modifiers::Readonly));
}

// both accessors have readonly: the readonly bit is lifted from both to the
// declaration (the else-if branch).
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersLiftsReadOnlyFromBothAccessorsToDecl)
{
    Syntax::MethodDeclaration decl;
    Syntax::Accessor accessor1;
    Syntax::Accessor accessor2;
    accessor1.Modifiers(Modifiers::Readonly);
    accessor2.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, &accessor1, &accessor2);

    EXPECT_FALSE(accessor1.HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(accessor2.HasModifier(Modifiers::Readonly));
    EXPECT_TRUE(decl.HasModifier(Modifiers::Readonly));
}

// accessor1 has readonly, accessor2 is non-null but does NOT have readonly: the
// else-if fails (not both carry it), so no modifier is moved.
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersNoOpWhenOnlyAccessor1HasReadOnly)
{
    Syntax::MethodDeclaration decl;
    Syntax::Accessor accessor1;
    Syntax::Accessor accessor2;
    accessor1.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, &accessor1, &accessor2);

    EXPECT_TRUE(accessor1.HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(accessor2.HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(decl.HasModifier(Modifiers::Readonly));
}

// accessor1 lacks readonly (accessor2 non-null): no modifier is moved regardless
// of accessor2's state -- the first if and the else-if both require accessor1 to
// carry readonly.
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersNoOpWhenAccessor1LacksReadOnly)
{
    Syntax::MethodDeclaration decl;
    Syntax::Accessor accessor1;
    Syntax::Accessor accessor2;
    accessor2.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, &accessor1, &accessor2);

    EXPECT_FALSE(accessor1.HasModifier(Modifiers::Readonly));
    EXPECT_TRUE(accessor2.HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(decl.HasModifier(Modifiers::Readonly));
}

// The decl's existing modifiers are preserved: the readonly bit is OR'd in, not
// replacing the prior value (the read-modify-write through the Modifiers()
// getter/setter pair).
TEST(TypeSystemAstBuilderTest, MergeReadOnlyModifiersPreservesExistingDeclModifiers)
{
    Syntax::MethodDeclaration decl;
    decl.Modifiers(Modifiers::Public);
    Syntax::Accessor accessor1;
    accessor1.Modifiers(Modifiers::Readonly);

    Syntax::MergeReadOnlyModifiers(decl, &accessor1, nullptr);

    EXPECT_TRUE(decl.HasModifier(Modifiers::Public));
    EXPECT_TRUE(decl.HasModifier(Modifiers::Readonly));
    EXPECT_EQ(decl.Modifiers(), Modifiers::Public | Modifiers::Readonly);
}

// ===========================================================================
// CompareType / CompareAny / CompareAttribute (TypeSystemAstBuilder.cs lines
// 886 and 835), the local static attribute-sorting pair inside `ConvertAttributes`.
// `CompareType` orders two types by their (reflection) name; `CompareAttribute`
// orders two attributes by type, decode-errors, then the fixed and named argument
// lists; `CompareAny` is the `IComparable` dispatch on the `std::any`-boxed
// argument values (the C# `is IComparable ? CompareTo : 0` port). The C#
// `IType.FullName` is the minimal-port-deferred accessor, so `CompareType` compares
// `ReflectionName` (the D433/D455 FullName-to-ReflectionName convention).
//
// The `TestAttribute` stub is a concrete `IAttribute` holding a configurable
// attribute type, decode-errors flag, and the fixed/named argument snapshots; it
// is anonymous-namespace-scoped so it does not ODR-conflict with the existing
// `TestAttribute` stubs in the D386 `IAttribute_Test` / D393 `ITypeDefinition_Test`
// / etc. reconciliation files (each anonymous namespace is a distinct scope).
// ===========================================================================
namespace {

class TestAttribute : public TS::IAttribute {
public:
    TestAttribute(TS::ITypePtr attributeType, bool hasDecodeErrors,
                 std::vector<TS::CustomAttributeTypedArgument> fixedArgs,
                 std::vector<TS::CustomAttributeNamedArgument> namedArgs)
        : attributeType_(std::move(attributeType)), hasDecodeErrors_(hasDecodeErrors),
          fixedArgs_(std::move(fixedArgs)), namedArgs_(std::move(namedArgs)) {}

    const TS::IType& AttributeType() const override { return *attributeType_; }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return hasDecodeErrors_; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override { return fixedArgs_; }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override { return namedArgs_; }

private:
    TS::ITypePtr attributeType_;
    bool hasDecodeErrors_;
    std::vector<TS::CustomAttributeTypedArgument> fixedArgs_;
    std::vector<TS::CustomAttributeNamedArgument> namedArgs_;
};

// A helper that boxes a `KnownType` as an `ITypePtr` in a `std::any` (the `System.Type`
// box the decoder would produce for a `typeof(...)` argument).
std::any BoxType(TS::KnownTypeCode code) {
    return std::any(TS::ITypePtr(std::make_shared<TS::KnownType>(code)));
}

} // namespace

// ---------------------------------------------------------------------------
// CompareType orders by the reflection (full) name: `System.Object` precedes
// `System.String`; equal types yield 0; the reverse pair is the negation.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, CompareTypeOrdersByReflectionName)
{
    TS::KnownType object(TS::KnownTypeCode::Object);  // "System.Object"
    TS::KnownType string(TS::KnownTypeCode::String);  // "System.String"
    EXPECT_LT(Syntax::CompareType(object, string), 0);
}

TEST(TypeSystemAstBuilderTest, CompareTypeReturnsZeroForEqualTypes)
{
    TS::KnownType a(TS::KnownTypeCode::Object);
    TS::KnownType b(TS::KnownTypeCode::Object);
    EXPECT_EQ(Syntax::CompareType(a, b), 0);
}

TEST(TypeSystemAstBuilderTest, CompareTypeReverseIsNegation)
{
    TS::KnownType object(TS::KnownTypeCode::Object);
    TS::KnownType string(TS::KnownTypeCode::String);
    EXPECT_GT(Syntax::CompareType(string, object), 0);
}

// ---------------------------------------------------------------------------
// CompareAny: an empty `std::any` (the C# `null`, which is not `IComparable`)
// yields 0 regardless of the other operand -- the C# `&&` short-circuits on the
// null side.
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, CompareAnyReturnsZeroForBothEmpty)
{
    EXPECT_EQ(Syntax::CompareAny(std::any(), std::any()), 0);
}

TEST(TypeSystemAstBuilderTest, CompareAnyReturnsZeroForOneEmpty)
{
    EXPECT_EQ(Syntax::CompareAny(std::any(static_cast<std::int32_t>(3)), std::any()), 0);
    EXPECT_EQ(Syntax::CompareAny(std::any(), std::any(static_cast<std::int32_t>(3))), 0);
}

// ---------------------------------------------------------------------------
// CompareAny compares two same-type primitives by the C# `CompareTo` ordering
// (negative / zero / positive).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, CompareAnyComparesInt32)
{
    using I = std::int32_t;
    EXPECT_LT(Syntax::CompareAny(std::any(I(3)), std::any(I(5))), 0);
    EXPECT_EQ(Syntax::CompareAny(std::any(I(3)), std::any(I(3))), 0);
    EXPECT_GT(Syntax::CompareAny(std::any(I(5)), std::any(I(3))), 0);
}

TEST(TypeSystemAstBuilderTest, CompareAnyComparesInt64)
{
    using I = std::int64_t;
    EXPECT_LT(Syntax::CompareAny(std::any(I(3)), std::any(I(5))), 0);
    EXPECT_GT(Syntax::CompareAny(std::any(I(5)), std::any(I(3))), 0);
}

TEST(TypeSystemAstBuilderTest, CompareAnyComparesDouble)
{
    EXPECT_LT(Syntax::CompareAny(std::any(1.5), std::any(2.5)), 0);
    EXPECT_GT(Syntax::CompareAny(std::any(2.5), std::any(1.5)), 0);
}

TEST(TypeSystemAstBuilderTest, CompareAnyComparesString)
{
    EXPECT_LT(Syntax::CompareAny(std::any(std::string("aaa")), std::any(std::string("bbb"))), 0);
    EXPECT_EQ(Syntax::CompareAny(std::any(std::string("x")), std::any(std::string("x"))), 0);
    EXPECT_GT(Syntax::CompareAny(std::any(std::string("bbb")), std::any(std::string("aaa"))), 0);
}

// `Boolean.CompareTo`: `true` is greater than `false`.
TEST(TypeSystemAstBuilderTest, CompareAnyComparesBool)
{
    EXPECT_LT(Syntax::CompareAny(std::any(false), std::any(true)), 0);
    EXPECT_GT(Syntax::CompareAny(std::any(true), std::any(false)), 0);
    EXPECT_EQ(Syntax::CompareAny(std::any(true), std::any(true)), 0);
}

TEST(TypeSystemAstBuilderTest, CompareAnyComparesChar)
{
    EXPECT_LT(Syntax::CompareAny(std::any(char16_t('A')), std::any(char16_t('B'))), 0);
    EXPECT_EQ(Syntax::CompareAny(std::any(char16_t('A')), std::any(char16_t('A'))), 0);
}

// A boxed `System.Type` is `IComparable` by full name; the C++ `ITypePtr` box
// compares by `ReflectionName` (the `System.Object` name precedes `System.String`).
TEST(TypeSystemAstBuilderTest, CompareAnyComparesITypePtrByReflectionName)
{
    EXPECT_LT(Syntax::CompareAny(BoxType(TS::KnownTypeCode::Object),
                                 BoxType(TS::KnownTypeCode::String)), 0);
    EXPECT_EQ(Syntax::CompareAny(BoxType(TS::KnownTypeCode::Object),
                                BoxType(TS::KnownTypeCode::Object)), 0);
    EXPECT_GT(Syntax::CompareAny(BoxType(TS::KnownTypeCode::String),
                               BoxType(TS::KnownTypeCode::Object)), 0);
}

// Different comparable types (int vs string) yield 0 -- the documented deviation
// (the C# `CompareTo` would throw; the well-formed same-AttributeType case never
// reaches it, so the port returns 0 rather than crash the sort).
TEST(TypeSystemAstBuilderTest, CompareAnyReturnsZeroForDifferentTypes)
{
    EXPECT_EQ(Syntax::CompareAny(std::any(static_cast<std::int32_t>(3)),
                               std::any(std::string("3"))), 0);
}

// An unhandled type (a boxed `std::vector<int>` is not `IComparable`) yields 0 --
// the C# `else -> 0` branch for a non-`IComparable`.
TEST(TypeSystemAstBuilderTest, CompareAnyReturnsZeroForUnhandledType)
{
    std::vector<int> v{1, 2, 3};
    EXPECT_EQ(Syntax::CompareAny(std::any(v), std::any(v)), 0);
}

// ---------------------------------------------------------------------------
// CompareAttribute: two attributes of different attribute types order by the
// type name (the first discriminiator).
// ---------------------------------------------------------------------------
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByAttributeType)
{
    TestAttribute objectAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    TestAttribute stringAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String), false, {}, {});
    EXPECT_LT(Syntax::CompareAttribute(objectAttr, stringAttr), 0);
    EXPECT_GT(Syntax::CompareAttribute(stringAttr, objectAttr), 0);
}

// Two attributes of the same type with no arguments compare equal (0).
TEST(TypeSystemAstBuilderTest, CompareAttributeReturnsZeroForEqualTypes)
{
    TestAttribute a(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    TestAttribute b(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    EXPECT_EQ(Syntax::CompareAttribute(a, b), 0);
}

// A decode-errored attribute sorts before a clean one (the `HasDecodeErrors`
// discriminator after the type match).
TEST(TypeSystemAstBuilderTest, CompareAttributeErroredSortsFirst)
{
    TestAttribute clean(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    TestAttribute errored(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), true, {}, {});
    EXPECT_LT(Syntax::CompareAttribute(errored, clean), 0);  // errored sorts first (< 0)
    EXPECT_GT(Syntax::CompareAttribute(clean, errored), 0);
}

// Both decode-errored attributes compare equal (the `&&` early return).
TEST(TypeSystemAstBuilderTest, CompareAttributeBothErroredReturnsZero)
{
    TestAttribute a(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), true, {}, {});
    TestAttribute b(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), true, {}, {});
    EXPECT_EQ(Syntax::CompareAttribute(a, b), 0);
}

// Attributes of the same type with different fixed-argument counts order by the
// count (the length discriminator before the element-wise loop).
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByFixedArgCount)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestAttribute one(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(1)))}, {});
    TestAttribute two(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(1))),
         TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(2)))},
        {});
    EXPECT_LT(Syntax::CompareAttribute(one, two), 0);  // 1 < 2 args
    EXPECT_GT(Syntax::CompareAttribute(two, one), 0);
}

// Same count, different argument type: order by the type name (via `CompareType`).
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByFixedArgType)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);   // "System.Int32"
    TS::ITypePtr stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String); // "System.String"
    TestAttribute intAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(1)))}, {});
    TestAttribute stringAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(stringType, std::any(std::string("1")))}, {});
    EXPECT_LT(Syntax::CompareAttribute(intAttr, stringAttr), 0);  // Int32 < String
}

// Same count and type, different value: order by the boxed value (via `CompareAny`).
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByFixedArgValue)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestAttribute small(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(3)))}, {});
    TestAttribute large(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false,
        {TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(5)))}, {});
    EXPECT_LT(Syntax::CompareAttribute(small, large), 0);
    EXPECT_GT(Syntax::CompareAttribute(large, small), 0);
}

// Named-argument count discriminates after the fixed arguments.
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByNamedArgCount)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TS::CustomAttributeNamedArgument oneArg(
        std::string("A"), TS::CustomAttributeNamedArgumentKind::Field, intType,
        std::any(static_cast<std::int32_t>(1)));
    TestAttribute zero(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    TestAttribute one(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {oneArg});
    EXPECT_LT(Syntax::CompareAttribute(zero, one), 0);
    EXPECT_GT(Syntax::CompareAttribute(one, zero), 0);
}

// Same named-arg count, different member name: order by the name.
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByNamedArgName)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestAttribute aName(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {},
        {TS::CustomAttributeNamedArgument(std::string("A"), TS::CustomAttributeNamedArgumentKind::Field,
                                         intType, std::any(static_cast<std::int32_t>(1)))});
    TestAttribute bName(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {},
        {TS::CustomAttributeNamedArgument(std::string("B"), TS::CustomAttributeNamedArgumentKind::Field,
                                         intType, std::any(static_cast<std::int32_t>(1)))});
    EXPECT_LT(Syntax::CompareAttribute(aName, bName), 0);  // "A" < "B"
}

// Same name and type, different value: order by the boxed value (via `CompareAny`).
TEST(TypeSystemAstBuilderTest, CompareAttributeOrdersByNamedArgValue)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestAttribute small(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {},
        {TS::CustomAttributeNamedArgument(std::string("A"), TS::CustomAttributeNamedArgumentKind::Field,
                                         intType, std::any(static_cast<std::int32_t>(3)))});
    TestAttribute large(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {},
        {TS::CustomAttributeNamedArgument(std::string("A"), TS::CustomAttributeNamedArgumentKind::Field,
                                         intType, std::any(static_cast<std::int32_t>(5)))});
    EXPECT_LT(Syntax::CompareAttribute(small, large), 0);
    EXPECT_GT(Syntax::CompareAttribute(large, small), 0);
}

// Two structurally-identical attributes (same type, no errors, same arguments)
// compare equal -- the full crux pinning every discriminiator returns 0.
TEST(TypeSystemAstBuilderTest, CompareAttributeEqualReturnsZero)
{
    TS::ITypePtr intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    std::vector<TS::CustomAttributeTypedArgument> fixed = {
        TS::CustomAttributeTypedArgument(intType, std::any(static_cast<std::int32_t>(42)))};
    std::vector<TS::CustomAttributeNamedArgument> named = {
        TS::CustomAttributeNamedArgument(std::string("N"), TS::CustomAttributeNamedArgumentKind::Field,
                                       intType, std::any(static_cast<std::int32_t>(7)))};
    TestAttribute a(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, fixed, named);
    TestAttribute b(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, fixed, named);
    EXPECT_EQ(Syntax::CompareAttribute(a, b), 0);
}

// The function dispatches through the `IAttribute` interface (the base-class
// pointer reaches the concrete stub's overrides).
TEST(TypeSystemAstBuilderTest, CompareAttributeDispatchesThroughInterface)
{
    TestAttribute objectAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), false, {}, {});
    TestAttribute stringAttr(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String), false, {}, {});
    const TS::IAttribute& a = objectAttr;
    const TS::IAttribute& b = stringAttr;
    EXPECT_LT(Syntax::CompareAttribute(a, b), 0);
}

// ---------------------------------------------------------------------------
// MakeSimpleType / MakeMemberType (TypeSystemAstBuilder.cs lines 747 and 762),
// the local static name-to-AstType factories. The C# 7 discard identifier `_`
// is a reserved token, so a type named `_` is emitted as the verbatim `@_` to
// keep it a valid identifier. The factories return a raw `new`-ed node (the
// non-owning leak model); the tests read the name back through the node
// accessors and delete the node to avoid leaking across the test process.
// ---------------------------------------------------------------------------

// MakeSimpleType with a normal name returns a SimpleType carrying that name.
TEST(TypeSystemAstBuilderTest, MakeSimpleTypeReturnsSimpleTypeWithName)
{
    std::unique_ptr<Syntax::SimpleType> st(Syntax::MakeSimpleType("List"));
    ASSERT_NE(st, nullptr);
    ASSERT_TRUE(st->Identifier().has_value());
    EXPECT_EQ(*st->Identifier(), "List");
}

// The reserved `_` discard identifier is substituted with the verbatim `@_`:
// `Identifier::Create` strips the `@` prefix and stores it as the `IsVerbatim`
// flag (the lexical `@`-escaping detail), so the token's `Name` is `_` and its
// `IsVerbatim` flag is true -- the load-bearing crux distinguishing a verbatim
// `@_` (emitted by `MakeSimpleType`) from a plain `_` (which would be the
// reserved discard, not a valid type name).
TEST(TypeSystemAstBuilderTest, MakeSimpleTypeSubstitutesVerbatimAtForDiscard)
{
    std::unique_ptr<Syntax::SimpleType> st(Syntax::MakeSimpleType("_"));
    ASSERT_NE(st, nullptr);
    ASSERT_TRUE(st->Identifier().has_value());
    EXPECT_EQ(*st->Identifier(), "_");
    EXPECT_TRUE(st->IdentifierToken()->IsVerbatim());
}

// MakeSimpleType returns a `SimpleType` (IS-A `AstType`), so the returned
// pointer binds to an `AstType*` -- the shape the ConvertType call sites rely on.
TEST(TypeSystemAstBuilderTest, MakeSimpleTypeReturnsAstTypeSubclass)
{
    Syntax::AstType* ast = Syntax::MakeSimpleType("Foo");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::SimpleType*>(ast), ast);
    delete ast;
}

// MakeMemberType with a normal name returns a MemberType carrying the target
// and the member name.
TEST(TypeSystemAstBuilderTest, MakeMemberTypeReturnsMemberTypeWithTargetAndName)
{
    auto* target = Syntax::MakeSimpleType("System");
    std::unique_ptr<Syntax::MemberType> mt(Syntax::MakeMemberType(target, "Collections"));
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->Target(), target);
    EXPECT_EQ(mt->MemberName(), "Collections");
}

// The reserved `_` discard identifier is substituted with the verbatim `@_` in
// the member name (the `@` stripped into the token's `IsVerbatim` flag, so the
// `MemberName` is `_` with `IsVerbatim` true), while the target is carried
// through unchanged.
TEST(TypeSystemAstBuilderTest, MakeMemberTypeSubstitutesVerbatimAtForDiscard)
{
    auto* target = Syntax::MakeSimpleType("System");
    std::unique_ptr<Syntax::MemberType> mt(Syntax::MakeMemberType(target, "_"));
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->Target(), target);
    EXPECT_EQ(mt->MemberName(), "_");
    EXPECT_TRUE(mt->MemberNameToken()->IsVerbatim());
}

// MakeMemberType returns a `MemberType` (IS-A `AstType`), and its target IS-A
// `AstType` (a `SimpleType`), so the dotted-name chain
// `MakeMemberType(MakeSimpleType(ns), name)` composes through the `AstType*`
// parameter -- the shape the ConvertType dotted-name call site relies on.
TEST(TypeSystemAstBuilderTest, MakeMemberTypeComposesWithMakeSimpleTypeTarget)
{
    Syntax::AstType* outer = Syntax::MakeMemberType(Syntax::MakeSimpleType("System"), "Collections");
    ASSERT_NE(outer, nullptr);
    auto* mt = dynamic_cast<Syntax::MemberType*>(outer);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->MemberName(), "Collections");
    auto* target = mt->Target();
    ASSERT_NE(target, nullptr);
    auto* st = dynamic_cast<Syntax::SimpleType*>(target);
    ASSERT_NE(st, nullptr);
    ASSERT_TRUE(st->Identifier().has_value());
    EXPECT_EQ(*st->Identifier(), "System");
    delete outer;
}

// ===========================================================================
// CalculateHammingWeight (TypeSystemAstBuilder.cs line 1427), the local
// population-count function inside `PrepareConstant` (inside `ConvertEnumValue`).
// The `[Flags]` enum decomposition prefers single-bit members (weight == 1), so
// each enum member's constant value is reduced to its bit-weight via this helper.
// It is pure (no type-system state), so it ports ahead of the instance method
// that drives it, and the tests need no stubs -- just the raw 64-bit values.
// ===========================================================================

// Zero has no set bits.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightZeroReturnsZero)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(0), 0);
}

// A single set bit (the least-significant) has weight 1.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightOneReturnsOne)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(1), 1);
}

// All 64 bits set yields the full 64 -- the load-bearing high-byte accumulation
// path (the `(x * h01) >> 56` fold) must reach the top byte.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightAllOnesReturnsSixtyFour)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(~0ULL), 64);
}

// A single bit at each extreme (bit 0 and bit 63) yields 1, exercising both the
// low and the high end of the SWAR accumulation.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightSingleBitAtEachExtreme)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(1ULL << 0), 1);
    EXPECT_EQ(Syntax::CalculateHammingWeight(1ULL << 63), 1);
}

// A run of n set bits (2^n - 1) yields exactly n -- the canonical popcount shape.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightPowerOfTwoMinusOneCountsAllBits)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(0xFFULL), 8);
    EXPECT_EQ(Syntax::CalculateHammingWeight(0xFFFFULL), 16);
    EXPECT_EQ(Syntax::CalculateHammingWeight(0xFFFFFFFFULL), 32);
}

// An arbitrary value (0b101101 = 0x2D = 45) has 4 set bits -- the non-trivial
// case the enum decomposition relies on for a multi-flag combined value.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightArbitraryValue)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(0x2DULL), 4);
}

// The alternating-bit patterns (0x55... and 0xAA...) each have 32 set bits,
// exercising the even/odd bit positions across the full 64-bit width.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightAlternatingBitsPattern)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(0x5555555555555555ULL), 32);
    EXPECT_EQ(Syntax::CalculateHammingWeight(0xAAAAAAAAAAAAAAAAULL), 32);
}

// Bits confined to the high byte (0xFF00...00) yield 8 -- the high-byte fold
// path that a naive shift-only popcount would mishandle.
TEST(TypeSystemAstBuilderTest, CalculateHammingWeightHighByteOnly)
{
    EXPECT_EQ(Syntax::CalculateHammingWeight(0xFF00000000000000ULL), 8);
}

// ===========================================================================
// TryGetSpecialConstant (TypeSystemAstBuilder.cs line 1252, the
// `specialConstants` static readonly Dictionary). The lookup maps a boxed BCL
// primitive value to the (KnownTypeCode, member-name) pair of the BCL static
// field that renders it as a named reference. The tests exercise each BCL
// primitive arm -- the integer MinValue/MaxValue pairs, the unsigned MaxValue
// singletons, and the float/double NaN / infinities / MinValue / MaxValue /
// Epsilon -- plus the not-found (nullopt) cases for a non-special value and an
// unmatched type. The NaN cases use std::isnan (NaN != NaN), so they are the
// load-bearing crux distinguishing the isnan arm from a naive == comparison.
// ===========================================================================

// A boxed byte.MaxValue (255) maps to (Byte, "MaxValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantByteMaxValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::uint8_t>(255)));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::Byte);
    EXPECT_EQ(r->second, "MaxValue");
}

// A boxed sbyte.MinValue (-128) maps to (SByte, "MinValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSByteMinValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int8_t>(-128)));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::SByte);
    EXPECT_EQ(r->second, "MinValue");
}

// A boxed sbyte.MaxValue (127) maps to (SByte, "MaxValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSByteMaxValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int8_t>(127)));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::SByte);
    EXPECT_EQ(r->second, "MaxValue");
}

// A boxed short.MinValue / MaxValue maps to the Int16 pair.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantInt16MinMax)
{
    auto mn = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int16_t>(-32768)));
    ASSERT_TRUE(mn.has_value());
    EXPECT_EQ(mn->first, TS::KnownTypeCode::Int16);
    EXPECT_EQ(mn->second, "MinValue");
    auto mx = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int16_t>(32767)));
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->first, TS::KnownTypeCode::Int16);
    EXPECT_EQ(mx->second, "MaxValue");
}

// A boxed ushort.MaxValue maps to (UInt16, "MaxValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantUInt16MaxValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::uint16_t>(65535)));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::UInt16);
    EXPECT_EQ(r->second, "MaxValue");
}

// A boxed int.MinValue / MaxValue maps to the Int32 pair.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantInt32MinMax)
{
    auto mn = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::int32_t>::min()));
    ASSERT_TRUE(mn.has_value());
    EXPECT_EQ(mn->first, TS::KnownTypeCode::Int32);
    EXPECT_EQ(mn->second, "MinValue");
    auto mx = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::int32_t>::max()));
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->first, TS::KnownTypeCode::Int32);
    EXPECT_EQ(mx->second, "MaxValue");
}

// A boxed uint.MaxValue maps to (UInt32, "MaxValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantUInt32MaxValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::uint32_t>::max()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::UInt32);
    EXPECT_EQ(r->second, "MaxValue");
}

// A boxed long.MinValue / MaxValue maps to the Int64 pair.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantInt64MinMax)
{
    auto mn = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::int64_t>::min()));
    ASSERT_TRUE(mn.has_value());
    EXPECT_EQ(mn->first, TS::KnownTypeCode::Int64);
    EXPECT_EQ(mn->second, "MinValue");
    auto mx = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::int64_t>::max()));
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->first, TS::KnownTypeCode::Int64);
    EXPECT_EQ(mx->second, "MaxValue");
}

// A boxed ulong.MaxValue maps to (UInt64, "MaxValue").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantUInt64MaxValue)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<std::uint64_t>::max()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::UInt64);
    EXPECT_EQ(r->second, "MaxValue");
}

// A boxed float NaN maps to (Single, "NaN") -- the load-bearing isnan crux,
// since a naive == comparison would fail (NaN != NaN).
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSingleNaN)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<float>::quiet_NaN()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(r->second, "NaN");
}

// A boxed float positive/negative infinity maps to the matching member.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSingleInfinities)
{
    auto neg = Syntax::TryGetSpecialConstant(std::any(-std::numeric_limits<float>::infinity()));
    ASSERT_TRUE(neg.has_value());
    EXPECT_EQ(neg->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(neg->second, "NegativeInfinity");
    auto pos = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<float>::infinity()));
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(pos->second, "PositiveInfinity");
}

// A boxed float MinValue / MaxValue maps to the Single pair. C# float.MinValue
// is the most-negative finite = -numeric_limits<float>::max(), NOT FLT_MIN.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSingleMinMax)
{
    auto mn = Syntax::TryGetSpecialConstant(std::any(-std::numeric_limits<float>::max()));
    ASSERT_TRUE(mn.has_value());
    EXPECT_EQ(mn->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(mn->second, "MinValue");
    auto mx = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<float>::max()));
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(mx->second, "MaxValue");
}

// A boxed float Epsilon (the smallest positive subnormal) maps to (Single,
// "Epsilon") -- denorm_min(), NOT FLT_EPSILON (the 1-to-next gap).
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantSingleEpsilon)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<float>::denorm_min()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::Single);
    EXPECT_EQ(r->second, "Epsilon");
}

// A boxed double NaN maps to (Double, "NaN").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantDoubleNaN)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<double>::quiet_NaN()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(r->second, "NaN");
}

// A boxed double positive/negative infinity maps to the matching member.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantDoubleInfinities)
{
    auto neg = Syntax::TryGetSpecialConstant(std::any(-std::numeric_limits<double>::infinity()));
    ASSERT_TRUE(neg.has_value());
    EXPECT_EQ(neg->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(neg->second, "NegativeInfinity");
    auto pos = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<double>::infinity()));
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(pos->second, "PositiveInfinity");
}

// A boxed double MinValue / MaxValue maps to the Double pair.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantDoubleMinMax)
{
    auto mn = Syntax::TryGetSpecialConstant(std::any(-std::numeric_limits<double>::max()));
    ASSERT_TRUE(mn.has_value());
    EXPECT_EQ(mn->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(mn->second, "MinValue");
    auto mx = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<double>::max()));
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(mx->second, "MaxValue");
}

// A boxed double Epsilon (the smallest positive subnormal) maps to (Double,
// "Epsilon").
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantDoubleEpsilon)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::numeric_limits<double>::denorm_min()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, TS::KnownTypeCode::Double);
    EXPECT_EQ(r->second, "Epsilon");
}

// A non-special integer value (42) yields nullopt.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantNonSpecialValueReturnsNullopt)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int32_t>(42)));
    EXPECT_FALSE(r.has_value());
}

// The runtime type distinguishes keys: a boxed int 255 (NOT byte.MaxValue, the
// int arm has no entry for 255) yields nullopt, even though byte.MaxValue is 255.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantTypeDistinguishesByteFromInt)
{
    // A boxed int32_t 255 is NOT a special constant (the int arm only matches
    // int.MinValue / int.MaxValue).
    auto r = Syntax::TryGetSpecialConstant(std::any(static_cast<std::int32_t>(255)));
    EXPECT_FALSE(r.has_value());
}

// An empty std::any (the C# null constant) yields nullopt.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantEmptyAnyReturnsNullopt)
{
    auto r = Syntax::TryGetSpecialConstant(std::any());
    EXPECT_FALSE(r.has_value());
}

// An unmatched type (a boxed std::string) yields nullopt.
TEST(TypeSystemAstBuilderTest, TryGetSpecialConstantUnmatchedTypeReturnsNullopt)
{
    auto r = Syntax::TryGetSpecialConstant(std::any(std::string("hello")));
    EXPECT_FALSE(r.has_value());
}
