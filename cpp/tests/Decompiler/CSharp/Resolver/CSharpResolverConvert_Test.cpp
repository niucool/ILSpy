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

// Tests for the CSharpResolver Convert / ResolveCast region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 1319-1470 plus
// the private GetEnumUnderlyingType member at line 985): TryConvert / TryConvertEnum /
// the two Convert overloads / the public ResolveCast / the CSharpPrimitiveCast wrapper /
// the GetEnumUnderlyingType member -- the conversion-application machinery the future
// ResolveUnaryOperator / ResolveBinaryOperator / ResolveCastExpression slices consume.
//
// The load-bearing cruxes:
//  (a) the IDENTITY early-out -- the C# `c == Conversion.IdentityConversion` reference
//      comparison ports to singleton POINTER identity, so an identity conversion returns
//      the caller's result handle UNCHANGED (no re-wrap);
//  (b) the CONSTANT-FOLDING route -- a compile-time constant under a non-None
//      non-user-defined conversion re-resolves through ResolveCast (which re-derives the
//      constant through the target type via Util::Cast), while a USER-DEFINED conversion
//      of the same constant only wraps (the !c.IsUserDefined() gate);
//  (c) the extension-vs-member distinction -- ResolveCast's `targetType.GetEnumUnderlying
//      Type()` (line 1403) is the TypeUtils EXTENSION (qualified on the IType receiver),
//      NOT the resolver's own same-name member: for a non-enum target the extension passes
//      the target through so the folding reads the target's OWN TypeCode, while the
//      member would report null (a definition's EnumUnderlyingType is null for non-enums)
//      -- pinned by the string-constant-to-int ErrorResolveResult fold, which needs the
//      int target's own TypeCode to reach the `is string` rejection;
//  (d) the enum-underlying folding -- an enum target folds through its UNDERLYING TypeCode
//      (const 1 -> enum E becomes ConstantResolveResult(E, 1)), and the native-integer
//      arm folds through the hardcoded 32-bit code with `checkForOverflow: true` (NOT the
//      resolver's flag), where an OVERFLOW falls back to the non-constant
//      ConversionResolveResult ("the conversion is not a compile-time constant"), never
//      to an error;
//  (e) the checked-context threading -- the folding path consults the resolver's
//      CheckForOverflow (the unchecked wrap folds int 2147483647 to short -1, the checked
//      yields ErrorResolveResult), and the wrapper's wrap carries the flag into the
//      ConversionResolveResult;
//  (f) TryConvertEnum's nullable arm -- the target rebind is LOCAL (the caller's
//      targetType is untouched), the isNullable flag flips only on the nullable success,
//      and the enum-typed operand wraps in the ImplicitNullableConversion singleton only
//      when it is not already nullable; the allowConversionFromConstantZero=false gate
//      rejects the constant-0-to-enum enumeration conversion in BOTH arms;
//  (g) the member-vs-extension semantics -- the resolver's GetEnumUnderlyingType member
//      returns the definition's EnumUnderlyingType (null for a definition-bearing
//      non-enum, faithfully mirroring the C# ITypeDefinition.EnumUnderlyingType), the
//      SpecialType.UnknownType null object for a definitionless type, and NO custom-
//      modifier unwrap (unlike the TypeUtils extension).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;
namespace UT = ILSpy::Decompiler::Util;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::Create;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The shared compilation, with the `System.Nullable`1` definition registered (the
// TryConvertEnum nullable arm resolves it through NullableType.Create -> FindType). The
// registration is kept alive by the static handle for the program's lifetime (the
// LookupCompilation stores only a raw pointer), and the per-compilation
// CSharpConversions::Get factory caches its instance on the compilation's CacheManager,
// so the compilation must outlive every resolver constructed over it (it does -- a
// function-local static). The Convert region resolves only uncached conversions (the
// ResolveResult-based Implicit/ExplicitConversion entries -- the cached IType-based entry
// is never reached), so no test-local type can dangle in the shared instance's cache.
LookupCompilation& Compilation() {
    static LookupCompilation compilation;
    static const std::shared_ptr<LookupTypeDefinition> nullableOfT =
        std::make_shared<LookupTypeDefinition>(
            "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
            TypeKind::Struct, Accessibility::Public, compilation, nullptr,
            KnownTypeCode::NullableOfT);
    compilation.RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
    return compilation;
}

// A fresh resolver over the shared compilation (the make_shared discipline the two
// identity-preserving With* early-outs require).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// A resolver with the checked-overflow context (the WithCheckForOverflow clone).
std::shared_ptr<CSharpResolver> MakeCheckedResolver() {
    return MakeResolver()->WithCheckForOverflow(true);
}

// A `LookupTypeDefinition` with the given known type code (the GetTypeCode-resolving stub
// -- a KnownType placeholder is NOT an ITypeDefinition and yields TypeCode::Empty, the
// D514 precedent). Function-local statics keep ONE shared-managed instance per primitive
// (the type-cache model: the shared_from_this discipline the ResolveCast/Convert
// targetType handles require, and the identity-conversion same-instance shape).
std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name, KnownTypeCode code,
                                              TypeKind kind = TypeKind::Struct) {
    return std::make_shared<LookupTypeDefinition>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
        Accessibility::Public, Compilation(), nullptr, code);
}

std::shared_ptr<LookupTypeDefinition> IntDef() {
    static const auto t = MakeDef("Int32", KnownTypeCode::Int32);
    return t;
}

std::shared_ptr<LookupTypeDefinition> LongDef() {
    static const auto t = MakeDef("Int64", KnownTypeCode::Int64);
    return t;
}

std::shared_ptr<LookupTypeDefinition> ShortDef() {
    static const auto t = MakeDef("Int16", KnownTypeCode::Int16);
    return t;
}

std::shared_ptr<LookupTypeDefinition> StringDef() {
    static const auto t = MakeDef("String", KnownTypeCode::String, TypeKind::Class);
    return t;
}

// An enum definition with the int underlying type (the enum-underlying folding crux).
std::shared_ptr<LookupTypeDefinition> EnumDef() {
    static const auto t = MakeDef("E", KnownTypeCode::None, TypeKind::Enum);
    static const bool underlyingSet = [] {
        t->SetEnumUnderlyingType(IntDef());
        return true;
    }();
    (void)underlyingSet;
    return t;
}

// The native-integer stub (the D514 convention: a LookupTypeDefinition with Kind=NInt
// and KnownTypeCode=None, so GetTypeCode yields Empty while the Kind-based arms still
// recognize it).
std::shared_ptr<LookupTypeDefinition> NIntDef() {
    static const auto t = MakeDef("nint", KnownTypeCode::None, TypeKind::NInt);
    return t;
}

// A compile-time constant over a type (the boxed value is one of the Util::Cast held
// types -- int32_t / int64_t / std::string / ...).
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

const ErrorResolveResult* AsError(const std::shared_ptr<ResolveResult>& r) {
    return dynamic_cast<const ErrorResolveResult*>(r.get());
}

} // namespace

// ---- TryConvert -----------------------------------------------------------------------------

TEST(CSharpResolverConvertTest, TryConvertAppliesImplicitConversionToConstant)
{
    // A constant int 5 -> long: the implicit numeric conversion is valid, and Convert
    // CONSTANT-FOLDS the constant through ResolveCast (a fresh ConstantResolveResult
    // typed long carrying the re-derived int64 value).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(5));
    const ResolveResult* original = rr.get();
    ASSERT_TRUE(resolver->TryConvert(rr, *LongDef()));
    EXPECT_NE(rr.get(), original);
    ASSERT_NE(AsConstant(rr), nullptr);
    EXPECT_EQ(&rr->Type(), LongDef().get());
    ASSERT_TRUE(rr->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int64_t>(rr->ConstantValue()), 5);
}

TEST(CSharpResolverConvertTest, TryConvertWrapsNonConstantResult)
{
    // A plain int result -> long: the wrap carries the original as the input operand and
    // the resolver's (default unchecked) flag.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(IntDef());
    const ResolveResult* original = rr.get();
    ASSERT_TRUE(resolver->TryConvert(rr, *LongDef()));
    EXPECT_NE(rr.get(), original);
    const ConversionResolveResult* converted = AsConversion(rr);
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(converted->Input(), original);
    EXPECT_FALSE(converted->CheckForOverflow());
}

TEST(CSharpResolverConvertTest, TryConvertIdentityReturnsSameResult)
{
    // int -> int (the same shared instance): the identity conversion early-out returns
    // the caller's handle UNCHANGED.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(IntDef());
    const ResolveResult* original = rr.get();
    ASSERT_TRUE(resolver->TryConvert(rr, *IntDef()));
    EXPECT_EQ(rr.get(), original);
}

TEST(CSharpResolverConvertTest, TryConvertFalseLeavesResultUnchanged)
{
    // string -> int: no implicit conversion exists -> false, the handle unchanged.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(StringDef());
    const ResolveResult* original = rr.get();
    EXPECT_FALSE(resolver->TryConvert(rr, *IntDef()));
    EXPECT_EQ(rr.get(), original);
}

// ---- TryConvertEnum -------------------------------------------------------------------------

TEST(CSharpResolverConvertTest, TryConvertEnumNonNullableConstantZeroConverts)
{
    // The implicit constant-0-to-enum conversion applies non-nullable: the constant
    // folds through the enum's UNDERLYING TypeCode, isNullable stays false, and the
    // enum-typed operand is untouched (the non-nullable arm returns before reaching it).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(0));
    std::shared_ptr<ResolveResult> enumRR = MakePlain(EnumDef());
    const ResolveResult* originalRR = rr.get();
    const ResolveResult* originalEnumRR = enumRR.get();
    bool isNullable = false;
    ASSERT_TRUE(resolver->TryConvertEnum(rr, *EnumDef(), isNullable, enumRR));
    EXPECT_NE(rr.get(), originalRR);
    ASSERT_NE(AsConstant(rr), nullptr);
    EXPECT_EQ(&rr->Type(), EnumDef().get());
    ASSERT_TRUE(rr->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int32_t>(rr->ConstantValue()), 0);
    EXPECT_FALSE(isNullable);
    EXPECT_EQ(enumRR.get(), originalEnumRR);
}

TEST(CSharpResolverConvertTest, TryConvertEnumDisallowedConstantZeroReturnsFalse)
{
    // allowConversionFromConstantZero=false rejects the ENUMERATION conversion in BOTH
    // arms (the constant-0-to-enum conversion IS an enumeration conversion whether the
    // target is the plain enum or its nullable form) -> false, everything unchanged.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(0));
    std::shared_ptr<ResolveResult> enumRR = MakePlain(EnumDef());
    const ResolveResult* originalRR = rr.get();
    const ResolveResult* originalEnumRR = enumRR.get();
    bool isNullable = false;
    EXPECT_FALSE(resolver->TryConvertEnum(rr, *EnumDef(), isNullable, enumRR,
                                          /*allowConversionFromConstantZero=*/ false));
    EXPECT_EQ(rr.get(), originalRR);
    EXPECT_EQ(enumRR.get(), originalEnumRR);
    EXPECT_FALSE(isNullable);
}

TEST(CSharpResolverConvertTest, TryConvertEnumNullableArmWrapsEnumOperand)
{
    // Already nullable (the non-nullable arm is skipped): the target is rebound to its
    // Nullable<T> form locally, the constant converts against the nullable target, and
    // the enum-typed operand wraps in the ImplicitNullableConversion singleton.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(0));
    std::shared_ptr<ResolveResult> enumRR = MakePlain(EnumDef());
    const ResolveResult* originalEnumRR = enumRR.get();
    bool isNullable = true;
    ASSERT_TRUE(resolver->TryConvertEnum(rr, *EnumDef(), isNullable, enumRR));
    EXPECT_TRUE(isNullable);
    // The rebound target is observable through the converted result's type: the
    // constant-0 -> Nullable<E> conversion is not foldable (the nullable enum target has
    // no underlying TypeCode), so the result wraps with the nullable type.
    const ConversionResolveResult* converted = AsConversion(rr);
    ASSERT_NE(converted, nullptr);
    EXPECT_TRUE(IsNullable(rr->Type()));
    EXPECT_FALSE(rr->IsCompileTimeConstant());
    // The enum operand wraps (it is not already nullable) in the ImplicitNullableConversion.
    EXPECT_NE(enumRR.get(), originalEnumRR);
    const ConversionResolveResult* wrappedEnum = AsConversion(enumRR);
    ASSERT_NE(wrappedEnum, nullptr);
    EXPECT_TRUE(IsNullable(enumRR->Type()));
    EXPECT_EQ(wrappedEnum->Input(), originalEnumRR);
    EXPECT_EQ(wrappedEnum->ConversionShared().get(),
              Conversions::ImplicitNullableConversion().get());
}

TEST(CSharpResolverConvertTest, TryConvertEnumNullableArmKeepsNullableEnumOperand)
{
    // The enum operand is already Nullable<E> -> NOT re-wrapped (pointer identity).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(0));
    ITypePtr nullableEnum = Create(Compilation(), *EnumDef());
    std::shared_ptr<ResolveResult> enumRR = MakePlain(nullableEnum);
    const ResolveResult* originalEnumRR = enumRR.get();
    bool isNullable = true;
    ASSERT_TRUE(resolver->TryConvertEnum(rr, *EnumDef(), isNullable, enumRR));
    EXPECT_EQ(enumRR.get(), originalEnumRR);
}

TEST(CSharpResolverConvertTest, TryConvertEnumNoConversionReturnsFalse)
{
    // A plain string result has no implicit conversion to the enum in either arm ->
    // false, everything unchanged.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(StringDef());
    std::shared_ptr<ResolveResult> enumRR = MakePlain(EnumDef());
    const ResolveResult* originalRR = rr.get();
    const ResolveResult* originalEnumRR = enumRR.get();
    bool isNullable = false;
    EXPECT_FALSE(resolver->TryConvertEnum(rr, *EnumDef(), isNullable, enumRR));
    EXPECT_EQ(rr.get(), originalRR);
    EXPECT_EQ(enumRR.get(), originalEnumRR);
    EXPECT_FALSE(isNullable);
}

// ---- Convert --------------------------------------------------------------------------------

TEST(CSharpResolverConvertTest, ConvertIdentityReturnsInputUnchanged)
{
    // The singleton pointer-identity early-out (the C# reference comparison).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> result =
        resolver->Convert(rr, *IntDef(), Conversions::IdentityConversion());
    EXPECT_EQ(result.get(), rr.get());
}

TEST(CSharpResolverConvertTest, ConvertConstantNonIdentityFoldsThroughResolveCast)
{
    // A compile-time constant under a valid non-user-defined conversion re-resolves
    // through ResolveCast: the folded constant is re-derived through the target type
    // (int32 5 -> int64 5).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(5));
    std::shared_ptr<ResolveResult> result =
        resolver->Convert(rr, *LongDef(), Conversions::ImplicitNumericConversion());
    ASSERT_NE(AsConstant(result), nullptr);
    EXPECT_EQ(&result->Type(), LongDef().get());
    ASSERT_TRUE(result->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int64_t>(result->ConstantValue()), 5);
}

TEST(CSharpResolverConvertTest, ConvertWrapsNonConstantWithResolverFlag)
{
    // A non-constant wraps in the ConversionResolveResult carrying the resolver's
    // checkForOverflow flag (false by default, true for the checked clone).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(IntDef());
    std::shared_ptr<Conversion> c = Conversions::ImplicitNumericConversion();
    std::shared_ptr<ResolveResult> result = resolver->Convert(rr, *LongDef(), c);
    const ConversionResolveResult* converted = AsConversion(result);
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(converted->Input(), rr.get());
    EXPECT_EQ(converted->ConversionShared().get(), c.get());
    EXPECT_FALSE(converted->CheckForOverflow());

    auto checked = MakeCheckedResolver();
    std::shared_ptr<ResolveResult> checkedResult = checked->Convert(rr, *LongDef(), c);
    const ConversionResolveResult* checkedConverted = AsConversion(checkedResult);
    ASSERT_NE(checkedConverted, nullptr);
    EXPECT_TRUE(checkedConverted->CheckForOverflow());
}

TEST(CSharpResolverConvertTest, ConvertConstantUnderUserDefinedConversionDoesNotFold)
{
    // The !c.IsUserDefined() gate: a user-defined conversion of a compile-time constant
    // only WRAPS (no ResolveCast folding) -- the wrapper is not a compile-time constant
    // and carries the original as the input operand.
    auto resolver = MakeResolver();
    auto method = std::make_shared<LookupMethod>("op_Implicit", Compilation());
    std::shared_ptr<Conversion> userDefined =
        Conversions::UserDefinedConversion(method.get(), /*isImplicit=*/ true, nullptr,
                                           nullptr);
    std::shared_ptr<ResolveResult> rr = MakeConstant(IntDef(), int32_t(5));
    std::shared_ptr<ResolveResult> result = resolver->Convert(rr, *LongDef(), userDefined);
    const ConversionResolveResult* converted = AsConversion(result);
    ASSERT_NE(converted, nullptr);
    EXPECT_FALSE(result->IsCompileTimeConstant());
    EXPECT_EQ(converted->Input(), rr.get());
    EXPECT_EQ(converted->ConversionShared().get(), userDefined.get());
}

TEST(CSharpResolverConvertTest, ConvertTwoArgResolvesImplicitConversionFirst)
{
    // The 2-arg overload: an int constant -> long folds (the implicit numeric
    // conversion resolves internally), an inconvertible pair wraps with the None
    // singleton (the wrapper IS an error result -- IsError derives from !IsValid).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> folded =
        resolver->Convert(MakeConstant(IntDef(), int32_t(5)), *LongDef());
    ASSERT_NE(AsConstant(folded), nullptr);
    EXPECT_EQ(&folded->Type(), LongDef().get());

    std::shared_ptr<ResolveResult> rr = MakePlain(StringDef());
    std::shared_ptr<ResolveResult> wrapped = resolver->Convert(rr, *IntDef());
    const ConversionResolveResult* converted = AsConversion(wrapped);
    ASSERT_NE(converted, nullptr);
    EXPECT_TRUE(wrapped->IsError());
    EXPECT_EQ(converted->ConversionShared().get(), Conversions::None().get());
}

// ---- ResolveCast ----------------------------------------------------------------------------

TEST(CSharpResolverConvertTest, ResolveCastFoldsConstantToNumericTarget)
{
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*LongDef(), MakeConstant(IntDef(), int32_t(5)));
    ASSERT_NE(AsConstant(result), nullptr);
    EXPECT_EQ(&result->Type(), LongDef().get());
    ASSERT_TRUE(result->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int64_t>(result->ConstantValue()), 5);
}

TEST(CSharpResolverConvertTest, ResolveCastFoldsConstantToEnumThroughUnderlying)
{
    // The enum target folds through its UNDERLYING TypeCode: const 1 -> E becomes
    // ConstantResolveResult(E, 1) -- the value keeps the int32 boxing.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*EnumDef(), MakeConstant(IntDef(), int32_t(1)));
    ASSERT_NE(AsConstant(result), nullptr);
    EXPECT_EQ(&result->Type(), EnumDef().get());
    ASSERT_TRUE(result->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int32_t>(result->ConstantValue()), 1);
}

TEST(CSharpResolverConvertTest, ResolveCastUncheckedConstantWraps)
{
    // The default (unchecked) resolver: int 2147483647 -> short folds through the
    // two's-complement wrap to short -1.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*ShortDef(), MakeConstant(IntDef(), int32_t(2147483647)));
    ASSERT_NE(AsConstant(result), nullptr);
    ASSERT_TRUE(result->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int16_t>(result->ConstantValue()), -1);
}

TEST(CSharpResolverConvertTest, ResolveCastCheckedOverflowYieldsError)
{
    // The checked clone: the same cast overflows -> ErrorResolveResult typed short.
    auto resolver = MakeCheckedResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*ShortDef(), MakeConstant(IntDef(), int32_t(2147483647)));
    ASSERT_NE(AsError(result), nullptr);
    EXPECT_EQ(&result->Type(), ShortDef().get());
}

TEST(CSharpResolverConvertTest, ResolveCastStringConstantToNonStringTargetIsError)
{
    // The `expression.ConstantValue is string` rejection -> ErrorResolveResult. This
    // also pins the EXTENSION-vs-MEMBER crux: the fold needs the int target's OWN
    // TypeCode (the TypeUtils extension passes the non-enum target through); the
    // resolver's GetEnumUnderlyingType member would report null for the non-enum
    // definition and the fold would never run.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*IntDef(), MakeConstant(StringDef(), std::string("abc")));
    ASSERT_NE(AsError(result), nullptr);
    EXPECT_EQ(&result->Type(), IntDef().get());
}

TEST(CSharpResolverConvertTest, ResolveCastStringTargetPassthrough)
{
    // The String arm: a string constant to the string target stays a constant (the
    // value preserved verbatim); a NON-string constant to the string target is an
    // error; a NULL constant (the empty std::any, the null literal) stays a constant
    // with the null value preserved.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> stringConstant =
        resolver->ResolveCast(*StringDef(), MakeConstant(StringDef(), std::string("abc")));
    ASSERT_NE(AsConstant(stringConstant), nullptr);
    ASSERT_TRUE(stringConstant->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::string>(stringConstant->ConstantValue()), "abc");

    std::shared_ptr<ResolveResult> intConstant =
        resolver->ResolveCast(*StringDef(), MakeConstant(IntDef(), int32_t(5)));
    ASSERT_NE(AsError(intConstant), nullptr);
    EXPECT_EQ(&intConstant->Type(), StringDef().get());

    std::shared_ptr<ResolveResult> nullConstant =
        resolver->ResolveCast(*StringDef(), MakeConstant(StringDef(), std::any()));
    ASSERT_NE(AsConstant(nullConstant), nullptr);
    EXPECT_FALSE(nullConstant->ConstantValue().has_value());
}

TEST(CSharpResolverConvertTest, ResolveCastNIntFoldsThrough32BitCode)
{
    // The native-integer arm: the nint target (no TypeCode of its own) folds through
    // the hardcoded Int32 code with checkForOverflow: TRUE.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*NIntDef(), MakeConstant(IntDef(), int32_t(5)));
    ASSERT_NE(AsConstant(result), nullptr);
    EXPECT_EQ(&result->Type(), NIntDef().get());
    ASSERT_TRUE(result->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int32_t>(result->ConstantValue()), 5);
}

TEST(CSharpResolverConvertTest, ResolveCastNIntOverflowFallsBackToConversionResult)
{
    // The 32-bit probe of a long constant that does not fit overflows -> the fallback
    // is the non-constant ConversionResolveResult ("the conversion is not a compile-time
    // constant"), NOT an error (the conversion itself is valid).
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> result =
        resolver->ResolveCast(*NIntDef(), MakeConstant(LongDef(), int64_t(5000000000LL)));
    const ConversionResolveResult* converted = AsConversion(result);
    ASSERT_NE(converted, nullptr);
    EXPECT_FALSE(result->IsCompileTimeConstant());
    EXPECT_FALSE(result->IsError());
    EXPECT_EQ(&result->Type(), NIntDef().get());
}

TEST(CSharpResolverConvertTest, ResolveCastNonConstantWrapsWithExplicitConversion)
{
    // A non-constant expression wraps; the explicit entry checks the implicit conversion
    // FIRST, so int -> long carries the ImplicitNumericConversion singleton.
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> rr = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> result = resolver->ResolveCast(*LongDef(), rr);
    const ConversionResolveResult* converted = AsConversion(result);
    ASSERT_NE(converted, nullptr);
    EXPECT_FALSE(result->IsCompileTimeConstant());
    EXPECT_EQ(converted->Input(), rr.get());
    EXPECT_EQ(converted->ConversionShared().get(),
              Conversions::ImplicitNumericConversion().get());
    EXPECT_FALSE(converted->CheckForOverflow());
}

// ---- CSharpPrimitiveCast wrapper -------------------------------------------------------------

TEST(CSharpResolverConvertTest, WrapperThreadsCheckForOverflowFlag)
{
    // The internal wrapper threads the resolver's CheckForOverflow flag into the Util
    // converter: unchecked wraps int.MaxValue-as-long to int -1, checked throws
    // OverflowException.
    auto resolver = MakeResolver();
    std::any uncheckedResult =
        resolver->CSharpPrimitiveCast(TypeCode::Int32, std::any(int64_t(4294967295LL)));
    ASSERT_TRUE(uncheckedResult.has_value());
    EXPECT_EQ(std::any_cast<int32_t>(uncheckedResult), -1);

    auto checked = MakeCheckedResolver();
    std::any checkedResult;
    EXPECT_THROW(checkedResult = checked->CSharpPrimitiveCast(TypeCode::Int32,
                                                              std::any(int64_t(4294967295LL))),
                UT::OverflowException);
}

// ---- GetEnumUnderlyingType member ------------------------------------------------------------

TEST(CSharpResolverConvertTest, GetEnumUnderlyingTypeMemberResolvesUnderlying)
{
    auto resolver = MakeResolver();
    EXPECT_EQ(resolver->GetEnumUnderlyingType(*EnumDef()), IntDef().get());
}

TEST(CSharpResolverConvertTest, GetEnumUnderlyingTypeMemberDefinitionlessYieldsUnknownType)
{
    // A definitionless type (the KnownType placeholder, GetDefinition() == null)
    // reports the SpecialType.UnknownType null object.
    auto resolver = MakeResolver();
    auto knownType = std::make_shared<KnownType>(KnownTypeCode::Object);
    const IType* underlying = resolver->GetEnumUnderlyingType(*knownType);
    ASSERT_NE(underlying, nullptr);
    EXPECT_EQ(underlying->Kind(), TypeKind::Unknown);
}

TEST(CSharpResolverConvertTest, GetEnumUnderlyingTypeMemberNonEnumDefinitionYieldsNull)
{
    // A definition-bearing NON-enum reports the definition's null EnumUnderlyingType --
    // the faithful C# ITypeDefinition.EnumUnderlyingType for non-enums, UNLIKE the
    // TypeUtils extension which passes the type through.
    auto resolver = MakeResolver();
    EXPECT_EQ(resolver->GetEnumUnderlyingType(*IntDef()), nullptr);
}
