// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver numeric-promotion region (cpp/Decompiler/CSharp/Resolver/
// CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 536-561 +
// 1055-1230): MakeNullable / UnaryNumericPromotion (C# spec draft-v11 section 12.4.7.2) /
// IsSigned / the two CastTo overloads / BinaryNumericPromotion (section 12.4.7.3) -- the
// operand-shaping machinery the future ResolveUnaryOperator (line 420) / ResolveBinaryOperator
// (line 667) slices consume.
//
// The load-bearing cruxes:
//  (a) the unary promotion table -- `-` on uint promotes the operand type to LONG (the
//      type variable REBINDS through the const IType*& out-parameter); `+`/`~` on the
//      small unsigned types [char..ushort] promotes to INT; the C# `goto case Plus` for a
//      non-uint minus falls through to the shared check;
//  (b) the nullable null-literal remap -- a null literal under `+`/`~` is treated as
//      sbyte so the promotion to int32 fires for it too (the Kind==Null gate is the only
//      way the empty TypeCode reaches the [Char..UInt16] range);
//  (c) IsSigned's implicit-constant-expression-conversion exceptions -- a NON-NEGATIVE
//      int/long compile-time constant counts as unsigned (the uint+signed-constant
//      promotion picks uint, not long; the ulong arm takes no binding error);
//  (d) CastTo's three routes -- the already-in-shape early-out (returns the operand
//      UNCHANGED under IType structural equality), the allowNullableConstants fold (a
//      null constant folds to a null constant over the target shape; a foldable constant
//      re-folds through ResolveCast), and the Convert wrap carrying the
//      ImplicitNullableConversion / ImplicitNumericConversion singleton;
//  (e) the binary promotion decision tree -- decimal (with the float/double binding
//      error), double, float, ulong (with the signed-operand binding error), the
//      native-integer nuint/nint kind arms (the Kind-based checks AFTER the code remaps),
//      the uint-with-signed-operand long promotion, long, and the default int; the null
//      literal promotes to the OTHER operand's type code first (only under isNullable).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The single fixture: the shared compilation plus one REGISTERED instance per primitive
// the region resolves through FindType. Every FindType target IS the accessor instance
// (the type-cache model -- the unary rebind and the CastTo already-in-shape early-out
// compare against the SAME instance FindType returns, so the identity assertions are
// meaningful). The per-compilation CSharpConversions::Get factory caches its instance on
// the compilation's CacheManager, and the numeric-promotion region resolves only
// UNCACHED conversions (the ResolveResult-based Implicit/ExplicitConversion entries
// inside Convert/ResolveCast -- the cached IType-based entry is never reached), so no
// test-local type can dangle in the shared instance's implicit-conversion cache.
struct Fixture {
    LookupCompilation compilation;
    std::vector<std::shared_ptr<LookupTypeDefinition>> defs;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;

    Fixture() {
        auto make = [this](KnownTypeCode code, const char* name,
                           TypeKind kind = TypeKind::Struct) {
            auto t = std::make_shared<LookupTypeDefinition>(
                name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
                Accessibility::Public, compilation, nullptr, code);
            compilation.RegisterKnownType(code, t.get());
            defs.push_back(t);
            return t;
        };
        // The Nullable`1 definition mirrors the CSharpResolverConvert_Test compilation
        // (name/namespace/arity; the Create composition and the IsNullable reads resolve
        // it through FindType).
        nullableOfT = std::make_shared<LookupTypeDefinition>(
            "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
            TypeKind::Struct, Accessibility::Public, compilation, nullptr,
            KnownTypeCode::NullableOfT);
        compilation.RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
        defs.push_back(nullableOfT);
        make(KnownTypeCode::Int32, "Int32");
        make(KnownTypeCode::Int64, "Int64");
        make(KnownTypeCode::UInt32, "UInt32");
        make(KnownTypeCode::UInt16, "UInt16");
        make(KnownTypeCode::Char, "Char");
        make(KnownTypeCode::Byte, "Byte");
        make(KnownTypeCode::Double, "Double");
        make(KnownTypeCode::Single, "Single");
        make(KnownTypeCode::Decimal, "Decimal");
        make(KnownTypeCode::UInt64, "UInt64");
        make(KnownTypeCode::String, "String", TypeKind::Class);
    }
};

Fixture& Fix() {
    static Fixture fixture;
    return fixture;
}

LookupCompilation& Compilation() { return Fix().compilation; }

// The REGISTERED definition for a known type code (the FindType identity target).
std::shared_ptr<LookupTypeDefinition> Def(KnownTypeCode code) {
    // A linear scan over the small fixture table (the codes are unique per instance).
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

std::shared_ptr<LookupTypeDefinition> IntDef() { return Def(KnownTypeCode::Int32); }
std::shared_ptr<LookupTypeDefinition> LongDef() { return Def(KnownTypeCode::Int64); }
std::shared_ptr<LookupTypeDefinition> UIntDef() { return Def(KnownTypeCode::UInt32); }

// A fresh resolver over the shared compilation.
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// The C# `SpecialType.NullType` singleton shape (Kind=Null, isReferenceType=true -- the
// shared-managed construction the MakeNullable passthrough and the Kind reads need).
std::shared_ptr<SpecialType> NullType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
    return t;
}

// A compile-time constant over a type (the boxed value is one of the Util::Cast held
// types -- int32_t / int64_t / ...; an EMPTY any is the C# null literal).
std::shared_ptr<ResolveResult> MakeConstant(ITypePtr type, std::any value) {
    return std::make_shared<ConstantResolveResult>(std::move(type), std::move(value));
}

// A plain (non-constant) resolve result over a type.
std::shared_ptr<ResolveResult> MakePlain(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// The dynamic_cast helpers asserting the result shape (the ASSERT-before-read discipline).
const ConstantResolveResult* AsConstant(const std::shared_ptr<ResolveResult>& r) {
    return dynamic_cast<const ConstantResolveResult*>(r.get());
}

const ConversionResolveResult* AsConversion(const std::shared_ptr<ResolveResult>& r) {
    return dynamic_cast<const ConversionResolveResult*>(r.get());
}

} // namespace

TEST(CSharpResolverNumericPromotionTest, MakeNullableWrapsWhenFlagTrue) {
    auto resolver = MakeResolver();
    ITypePtr result = resolver->MakeNullable(*IntDef(), /*isNullable=*/true);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(IsNullable(*result));
    // The wrapper's type argument is the input instance (the Create composition).
    const auto* pt = dynamic_cast<const ILSpy::Decompiler::TypeSystem::ParameterizedType*>(result.get());
    ASSERT_NE(pt, nullptr);
    EXPECT_TRUE(pt->GetTypeArgument(0).get() == IntDef().get());
}

TEST(CSharpResolverNumericPromotionTest, MakeNullablePassthroughReturnsSameInstanceWhenFlagFalse) {
    auto resolver = MakeResolver();
    ITypePtr result = resolver->MakeNullable(*IntDef(), /*isNullable=*/false);
    ASSERT_NE(result, nullptr);
    // The passthrough recovers the input's own owning handle -- pointer identity.
    EXPECT_TRUE(result.get() == IntDef().get());
    EXPECT_FALSE(IsNullable(*result));
}

TEST(CSharpResolverNumericPromotionTest, MakeNullableWrapsFreshInstancePerCall) {
    auto resolver = MakeResolver();
    ITypePtr first = resolver->MakeNullable(*IntDef(), true);
    ITypePtr second = resolver->MakeNullable(*IntDef(), true);
    EXPECT_NE(first.get(), second.get());
    // Both wrap the same element instance.
    EXPECT_TRUE(IsNullable(*first));
    EXPECT_TRUE(IsNullable(*second));
}

TEST(CSharpResolverNumericPromotionTest, MakeNullablePassthroughWorksForSpecialType) {
    auto resolver = MakeResolver();
    ITypePtr result = resolver->MakeNullable(*NullType(), /*isNullable=*/false);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result.get() == NullType().get());
    EXPECT_EQ(result->Kind(), TypeKind::Null);
}

TEST(CSharpResolverNumericPromotionTest, MinusPromotesUInt32ToLongAndRebindsType) {
    auto resolver = MakeResolver();
    const IType* type = UIntDef().get();
    auto expression = MakePlain(UIntDef());
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Minus, type,
                                                 /*isNullable=*/false, expression);
    // The type variable REBINDS to the FindType(Int64) result (the registered instance).
    EXPECT_TRUE(type == LongDef().get());
    // The operand wraps in the long conversion (a non-constant never folds).
    const ConversionResolveResult* conversion = AsConversion(result);
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
    // The wrap carries the ImplicitNumericConversion singleton (the isNullable=false arm).
    EXPECT_EQ(conversion->ConversionShared().get(),
              Conversions::ImplicitNumericConversion().get());
}

TEST(CSharpResolverNumericPromotionTest, MinusUInt32ConstantFoldsToLongConstant) {
    auto resolver = MakeResolver();
    const IType* type = UIntDef().get();
    auto expression = MakeConstant(UIntDef(), std::any(std::uint32_t{5}));
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Minus, type,
                                                 /*isNullable=*/false, expression);
    EXPECT_TRUE(type == LongDef().get());
    // A compile-time constant under a non-identity non-user-defined conversion folds
    // through ResolveCast -- the uint box re-derives through the long TypeCode.
    const ConstantResolveResult* constant = AsConstant(result);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
    const std::int64_t* value = std::any_cast<std::int64_t>(&constant->ConstantValue());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, std::int64_t{5});
}

TEST(CSharpResolverNumericPromotionTest, MinusFallsThroughToPlusForNonUInt32) {
    auto resolver = MakeResolver();
    const IType* type = IntDef().get();
    auto expression = MakePlain(IntDef());
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Minus, type,
                                                 /*isNullable=*/false, expression);
    // Int32 is outside [Char..UInt16] -- the fall-through finds nothing to promote: the
    // operand is returned UNCHANGED (pointer identity) and the type is not rebound.
    EXPECT_EQ(result.get(), expression.get());
    EXPECT_TRUE(type == IntDef().get());
}

TEST(CSharpResolverNumericPromotionTest, PlusPromotesCharToInt32) {
    auto resolver = MakeResolver();
    auto charDef = Def(KnownTypeCode::Char);
    ASSERT_NE(charDef, nullptr);
    const IType* type = charDef.get();
    auto expression = MakePlain(charDef);
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Plus, type,
                                                 /*isNullable=*/false, expression);
    EXPECT_TRUE(type == IntDef().get());
    EXPECT_NE(result.get(), expression.get());
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int32);
}

TEST(CSharpResolverNumericPromotionTest, BitNotPromotesUInt16ToInt32) {
    auto resolver = MakeResolver();
    auto u16 = Def(KnownTypeCode::UInt16);
    ASSERT_NE(u16, nullptr);
    const IType* type = u16.get();
    auto expression = MakePlain(u16);
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::BitNot, type,
                                                 /*isNullable=*/false, expression);
    EXPECT_TRUE(type == IntDef().get());
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int32);
}

TEST(CSharpResolverNumericPromotionTest, PlusOnInt32LeavesOperandUnchanged) {
    auto resolver = MakeResolver();
    const IType* type = IntDef().get();
    auto expression = MakePlain(IntDef());
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Plus, type,
                                                 /*isNullable=*/false, expression);
    EXPECT_EQ(result.get(), expression.get());
    EXPECT_TRUE(type == IntDef().get());
}

TEST(CSharpResolverNumericPromotionTest, NullableNullLiteralIsTreatedAsSByteAndPromotesToInt32) {
    auto resolver = MakeResolver();
    const IType* type = NullType().get();
    // The null literal: a compile-time constant whose ConstantValue is null (the empty any).
    auto expression = MakeConstant(NullType(), std::any());
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Plus, type,
                                                 /*isNullable=*/true, expression);
    // The Kind==Null remap to SByte puts the empty code inside [Char..UInt16], so the
    // promotion to int32 fires and the type rebinds.
    EXPECT_TRUE(type == IntDef().get());
    EXPECT_NE(result.get(), expression.get());
    // The conversion target is the Nullable<int> wrapper (MakeNullable under isNullable).
    EXPECT_TRUE(IsNullable(result->Type()));
}

TEST(CSharpResolverNumericPromotionTest, NonPromotableOperatorLeavesOperandUnchanged) {
    auto resolver = MakeResolver();
    const IType* type = UIntDef().get();
    auto expression = MakePlain(UIntDef());
    // Logical not has no numeric promotion arm at all (the default case).
    auto result = resolver->UnaryNumericPromotion(UnaryOperatorType::Not, type,
                                                 /*isNullable=*/false, expression);
    EXPECT_EQ(result.get(), expression.get());
    EXPECT_TRUE(type == UIntDef().get());
}

TEST(CSharpResolverNumericPromotionTest, IsSignedSByteAndInt16ReturnTrue) {
    auto plain = MakePlain(IntDef());
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::SByte, plain));
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int16, plain));
}

TEST(CSharpResolverNumericPromotionTest, IsSignedInt32NonNegativeConstantReturnsFalse) {
    auto constant = MakeConstant(IntDef(), std::any(std::int32_t{5}));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Int32, constant));
    // The zero boundary counts as non-negative.
    auto zero = MakeConstant(IntDef(), std::any(std::int32_t{0}));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Int32, zero));
}

TEST(CSharpResolverNumericPromotionTest, IsSignedInt32NegativeConstantAndPlainReturnTrue) {
    auto constant = MakeConstant(IntDef(), std::any(std::int32_t{-5}));
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int32, constant));
    auto plain = MakePlain(IntDef());
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int32, plain));
}

TEST(CSharpResolverNumericPromotionTest, IsSignedInt64HonorsTheConstantException) {
    auto nonNegative = MakeConstant(LongDef(), std::any(std::int64_t{1}));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Int64, nonNegative));
    auto negative = MakeConstant(LongDef(), std::any(std::int64_t{-1}));
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int64, negative));
    auto plain = MakePlain(LongDef());
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int64, plain));
}

TEST(CSharpResolverNumericPromotionTest, IsSignedMismatchedBoxCountsAsSigned) {
    // An int32 code over a LONG box: the pointer-form unbox fails (the C# would throw
    // InvalidCastException), and the mismatched box counts as signed (the safe faithful
    // fallback).
    auto mismatched = MakeConstant(IntDef(), std::any(std::int64_t{5}));
    EXPECT_TRUE(CSharpResolver::IsSigned(TypeCode::Int32, mismatched));
}

TEST(CSharpResolverNumericPromotionTest, IsSignedUnsignedAndOtherCodesReturnFalse) {
    auto plain = MakePlain(IntDef());
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Byte, plain));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::UInt16, plain));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::UInt32, plain));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::UInt64, plain));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Boolean, plain));
    EXPECT_FALSE(CSharpResolver::IsSigned(TypeCode::Decimal, plain));
}

TEST(CSharpResolverNumericPromotionTest, CastToSameShapeReturnsOperandUnchanged) {
    auto resolver = MakeResolver();
    auto expression = MakePlain(IntDef());
    auto result = resolver->CastTo(*IntDef(), /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/true);
    // MakeNullable passthrough == the IntDef instance, and the operand's type is the
    // SAME registered instance -- the IType structural (identity) equality early-out.
    EXPECT_EQ(result.get(), expression.get());
}

TEST(CSharpResolverNumericPromotionTest, CastToFoldableConstantFoldsOverTarget) {
    auto resolver = MakeResolver();
    auto expression = MakeConstant(IntDef(), std::any(std::int32_t{5}));
    auto result = resolver->CastTo(*LongDef(), /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/true);
    const ConstantResolveResult* constant = AsConstant(result);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
    const std::int64_t* value = std::any_cast<std::int64_t>(&constant->ConstantValue());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, std::int64_t{5});
}

TEST(CSharpResolverNumericPromotionTest, CastToNullConstantFoldsToNullConstantOverTarget) {
    auto resolver = MakeResolver();
    auto expression = MakeConstant(IntDef(), std::any());
    auto result = resolver->CastTo(*LongDef(), /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/true);
    const ConstantResolveResult* constant = AsConstant(result);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
    // The null constant keeps its null ConstantValue over the target shape.
    EXPECT_FALSE(constant->ConstantValue().has_value());
}

TEST(CSharpResolverNumericPromotionTest, CastToNullConstantWithFlagFalseDoesNotFold) {
    auto resolver = MakeResolver();
    auto expression = MakeConstant(IntDef(), std::any());
    auto result = resolver->CastTo(*LongDef(), /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/false);
    // The fold gate is off -- the null constant falls to Convert, whose ResolveCast route
    // cannot fold an empty box, so the operand wraps (NOT a compile-time constant).
    EXPECT_EQ(AsConstant(result), nullptr);
    const ConversionResolveResult* conversion = AsConversion(result);
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
}

TEST(CSharpResolverNumericPromotionTest, CastToNonConstantWrapsWithNumericConversion) {
    auto resolver = MakeResolver();
    auto expression = MakePlain(IntDef());
    auto result = resolver->CastTo(*LongDef(), /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/true);
    const ConversionResolveResult* conversion = AsConversion(result);
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(GetTypeCode(result->Type()), TypeCode::Int64);
    EXPECT_EQ(conversion->ConversionShared().get(),
              Conversions::ImplicitNumericConversion().get());
}

TEST(CSharpResolverNumericPromotionTest, CastToNullableTargetWrapsInNullableWrapper) {
    auto resolver = MakeResolver();
    auto expression = MakePlain(IntDef());
    auto result = resolver->CastTo(*IntDef(), /*isNullable=*/true, expression,
                                   /*allowNullableConstants=*/false);
    EXPECT_TRUE(IsNullable(result->Type()));
    const ConversionResolveResult* conversion = AsConversion(result);
    ASSERT_NE(conversion, nullptr);
    // The isNullable=true arm carries the ImplicitNullableConversion singleton.
    EXPECT_EQ(conversion->ConversionShared().get(),
              Conversions::ImplicitNullableConversion().get());
}

TEST(CSharpResolverNumericPromotionTest, CastToTypeCodeOverloadResolvesThroughFindType) {
    auto resolver = MakeResolver();
    auto expression = MakePlain(IntDef());
    auto result = resolver->CastTo(TypeCode::Int64, /*isNullable=*/false, expression,
                                   /*allowNullableConstants=*/true);
    // The TypeCode overload delegates through the ReflectionHelper FindType extension to
    // the registered Int64 instance -- the wrap's type IS that instance.
    const ConversionResolveResult* conversion = AsConversion(result);
    ASSERT_NE(conversion, nullptr);
    EXPECT_TRUE(&result->Type() == LongDef().get());
}

TEST(CSharpResolverNumericPromotionTest, BinaryPromotesIntAndLongToLong) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(IntDef());
    auto rhs = MakePlain(LongDef());
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::Int64);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::Int64);
}

TEST(CSharpResolverNumericPromotionTest, BinaryPromotesIntAndDoubleToDouble) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(IntDef());
    auto rhs = MakePlain(Def(KnownTypeCode::Double));
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::Double);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::Double);
}

TEST(CSharpResolverNumericPromotionTest, BinaryDecimalWithFloatIsBindingError) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(Def(KnownTypeCode::Single));
    auto rhs = MakePlain(Def(KnownTypeCode::Decimal));
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    // decimal + float/double is a binding error (no promotion exists), but the operands
    // are still promoted to decimal.
    EXPECT_FALSE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::Decimal);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::Decimal);
}

TEST(CSharpResolverNumericPromotionTest, BinaryUInt64WithSignedOperandIsBindingError) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(IntDef()); // a plain int is signed
    auto rhs = MakePlain(Def(KnownTypeCode::UInt64));
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_FALSE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::UInt64);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::UInt64);
}

TEST(CSharpResolverNumericPromotionTest, BinaryUInt64WithNonNegativeInt64ConstantIsNoError) {
    auto resolver = MakeResolver();
    // The implicit-constant-expression-conversion exception: a non-negative long
    // constant counts as unsigned, so ulong + 1L promotes without a binding error.
    auto lhs = MakeConstant(LongDef(), std::any(std::int64_t{1}));
    auto rhs = MakePlain(Def(KnownTypeCode::UInt64));
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::UInt64);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::UInt64);
}

TEST(CSharpResolverNumericPromotionTest, BinaryUInt32WithSignedConstantPromotesToUInt32) {
    auto resolver = MakeResolver();
    // The constant exception again: int constant 1 + plain uint -> uint (NOT long).
    auto lhs = MakeConstant(IntDef(), std::any(std::int32_t{1}));
    auto rhs = MakePlain(UIntDef());
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::UInt32);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::UInt32);
}

TEST(CSharpResolverNumericPromotionTest, BinaryUInt32WithPlainSignedIntPromotesToLong) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(IntDef());
    auto rhs = MakePlain(UIntDef());
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::Int64);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::Int64);
}

TEST(CSharpResolverNumericPromotionTest, BinaryDefaultPromotesSmallOperandsToInt32) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(Def(KnownTypeCode::Byte));
    auto rhs = MakePlain(Def(KnownTypeCode::UInt16));
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(GetTypeCode(lhs->Type()), TypeCode::Int32);
    EXPECT_EQ(GetTypeCode(rhs->Type()), TypeCode::Int32);
}

TEST(CSharpResolverNumericPromotionTest, BinaryNoopWhenBothAlreadyInt32) {
    auto resolver = MakeResolver();
    auto lhs = MakePlain(IntDef());
    auto rhs = MakePlain(IntDef());
    auto lhsBefore = lhs.get();
    auto rhsBefore = rhs.get();
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    // int + int: both CastTo calls hit the already-in-shape early-out -- the operand
    // handles are UNCHANGED (pointer identity) and no binding error is reported.
    EXPECT_TRUE(ok);
    EXPECT_EQ(lhs.get(), lhsBefore);
    EXPECT_EQ(rhs.get(), rhsBefore);
}

TEST(CSharpResolverNumericPromotionTest, BinaryNullableNullLiteralPromotesToOtherOperandType) {
    auto resolver = MakeResolver();
    auto lhs = MakeConstant(NullType(), std::any());
    auto rhs = MakePlain(IntDef());
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/true, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    // The null literal first promotes to the OTHER operand's type code (the null-literal
    // arm), folding to a null constant over the Nullable<int32> target shape.
    const ConstantResolveResult* constant = AsConstant(lhs);
    ASSERT_NE(constant, nullptr);
    EXPECT_TRUE(IsNullable(lhs->Type()));
    EXPECT_FALSE(constant->ConstantValue().has_value());
    // The int operand wraps into the same Nullable<int32> shape.
    EXPECT_TRUE(IsNullable(rhs->Type()));
}

TEST(CSharpResolverNumericPromotionTest, BinaryBothNullLiteralsMakeNoPromotion) {
    auto resolver = MakeResolver();
    auto lhs = MakeConstant(NullType(), std::any());
    auto rhs = MakeConstant(NullType(), std::any());
    auto lhsBefore = lhs.get();
    auto rhsBefore = rhs.get();
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/true, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    // Neither null-literal arm fires (the other side's code is Empty, outside
    // [Boolean..Decimal]) and the decision tree is never entered -- both operands are
    // returned UNCHANGED and no binding error is reported.
    EXPECT_TRUE(ok);
    EXPECT_EQ(lhs.get(), lhsBefore);
    EXPECT_EQ(rhs.get(), rhsBefore);
}

TEST(CSharpResolverNumericPromotionTest, BinaryNUIntKindPromotesBothToNUInt) {
    auto resolver = MakeResolver();
    // The native-integer operand is a SpecialType (Kind=NUInt; GetTypeCode yields Empty,
    // remapped to UInt32 by the Kind hack -- a LookupTypeDefinition stub of the same Kind
    // would reach the unchecked same-Kind structural cast in the SpecialType equality,
    // so the shared-managed SpecialType is the faithful stub here).
    auto nuintType = std::make_shared<SpecialType>(TypeKind::NUInt, std::optional<bool>(false));
    auto lhs = MakePlain(nuintType);
    auto rhs = MakePlain(UIntDef());
    auto lhsBefore = lhs.get();
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    // The nuint operand is already in shape (the CastTo early-out under the kind-based
    // SpecialType equality); the uint operand promotes to the native-integer shape.
    EXPECT_EQ(lhs.get(), lhsBefore);
    EXPECT_EQ(rhs->Type().Kind(), TypeKind::NUInt);
}

TEST(CSharpResolverNumericPromotionTest, BinaryNIntKindPromotesBothToNInt) {
    auto resolver = MakeResolver();
    auto nintType = std::make_shared<SpecialType>(TypeKind::NInt, std::optional<bool>(false));
    auto lhs = MakePlain(nintType);
    auto rhs = MakePlain(IntDef());
    auto lhsBefore = lhs.get();
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(lhs.get(), lhsBefore);
    EXPECT_EQ(rhs->Type().Kind(), TypeKind::NInt);
}

TEST(CSharpResolverNumericPromotionTest, BinarySignedWithNUIntIsBindingError) {
    auto resolver = MakeResolver();
    auto nuintType = std::make_shared<SpecialType>(TypeKind::NUInt, std::optional<bool>(false));
    auto lhs = MakePlain(IntDef()); // signed -> the nuint arm reports a binding error
    auto rhs = MakePlain(nuintType);
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_FALSE(ok);
    EXPECT_EQ(lhs->Type().Kind(), TypeKind::NUInt);
    EXPECT_EQ(rhs->Type().Kind(), TypeKind::NUInt);
}

TEST(CSharpResolverNumericPromotionTest, BinaryNonNumericOperandsMakeNoPromotion) {
    auto resolver = MakeResolver();
    // A pair outside [Char..Decimal] on the codes: the decision tree is never entered.
    auto stringDef = Def(KnownTypeCode::String);
    ASSERT_NE(stringDef, nullptr);
    auto lhs = MakePlain(stringDef);
    auto rhs = MakePlain(stringDef);
    auto lhsBefore = lhs.get();
    bool ok = resolver->BinaryNumericPromotion(/*isNullable=*/false, lhs, rhs,
                                               /*allowNullableConstants=*/true);
    EXPECT_TRUE(ok);
    EXPECT_EQ(lhs.get(), lhsBefore);
}
