// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` ResolveResult-based implicit-conversion dispatch --
// `Detail::ImplicitConversion(const ICompilation&, const ResolveResult&, IType&, bool, bool)`
// (a faithful port of the C# `CSharpConversions.ImplicitConversion(ResolveResult, IType, bool,
// bool)`, CSharpConversions.cs line 101, C# spec draft-v11 section 10.2). The dispatch is the
// core the public `ImplicitConversion(ResolveResult, IType)` (line 143) and
// `ExplicitConversion(ResolveResult, IType)` (line 281) entry points build on; it wires the
// already-ported helpers together in spec order:
//   * the compile-time-constant arms (`ImplicitEnumerationConversion` D527, then
//     `ImplicitConstantExpressionConversion` D521),
//   * the interpolated-string arm (an RTTI check on `InterpolatedStringResolveResult` plus
//     `IsKnownType(IFormattable/FormattableString)`),
//   * the dynamic arm (`resolveResult.Type.Kind == TypeKind.Dynamic`),
//   * the (DEFERRED) anonymous-function / method-group arms (yield `None` until their machinery
//     lands),
//   * the compile-time-constant fallback (`StandardImplicitConversion` D523 +
//     `UserDefinedImplicitConversion` D530),
//   * the non-constant fallback (the DEFERRED tuple arm, the `ThrowResolveResult` arm, then the
//     IType-based `ImplicitConversion` D531).
//
// The test stubs:
//  * `ConstantResolveResult` (D432) is the faithful stub for a compile-time constant -- it
//    overrides `IsCompileTimeConstant` to `true` and `ConstantValue` to the stored boxed value.
//    The constant's `Type` is a `LookupTypeDefinition` (`Def(Int32)`) so `GetTypeCode` resolves
//    the `KnownTypeCode`.
//  * `Def(ktc, kind)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`,
//    `GetDefinition() == this`) with a configurable `KnownTypeCode` and `TypeKind` --
//    `GetTypeCode` resolves via the numeric cast, and `IsKnownType` reads the type's own
//    `GetDefinition()->KnownTypeCode`, so a `Def(IFormattable, Class)` makes
//    `IsKnownType(it, IFormattable)` resolve on the type itself.
//  * `EnumDef()` is `Kind == Enum` and `KnownTypeCode == None` -- the to-side the enumeration arm
//    checks (`GetUnderlyingType(enumDef).Kind() == Enum`).
//  * `NullableOf(Def(Enum))` is a `ParameterizedType` over the `System.Nullable`1` definition,
//    so `NullableType.GetUnderlyingType` strips it to the `Enum` type argument -- the lifted
//    crux (`0 -> E?`).
//  * `InterpolatedStringResolveResult` (D-something) is constructed with the string type as its
//    base `Type`, an empty format string, and an empty argument list -- the RTTI check the
//    interpolated-string arm matches.
//  * `ThrowResolveResult` (D-something) is the parameterless throw-expression stub -- its
//    `Type()` is `NoType()` (Kind == None, NOT Dynamic), so it falls through the dynamic arm to
//    the throw arm.
//  * A plain `ResolveResult(SpecialType(Dynamic, true))` is a non-constant result whose `Type`
//    is `dynamic` -- the dynamic arm fires on `Kind() == Dynamic`.
//  * A plain `ResolveResult(Def(Int32))` is a non-constant result whose `Type` is `int` -- it
//    falls through to the IType-based fallback (`ImplicitConversion` D531).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"  // ConstantResolveResult (the compile-time-constant stub)
#include "Decompiler/Semantics/Conversion.hpp"              // Conversion (the flag accessors)
#include "Decompiler/Semantics/ConversionFactories.hpp"     // Conversions (the singletons / EnumerationConversion factory)
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"  // InterpolatedStringResolveResult (the interpolated-string arm)
#include "Decompiler/Semantics/ResolveResult.hpp"           // ResolveResult (the non-constant base stub)
#include "Decompiler/Semantics/ThrowResolveResult.hpp"      // ThrowResolveResult (the throw arm)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"                  // SpecialType, ParameterizedType
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitConversion;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::ThrowResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via the numeric cast
// (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17); `IsKnownType` reads the type's own
// `GetDefinition()->KnownTypeCode`, so a `Def(IFormattable, Class)` makes `IsKnownType(it,
// IFormattable)` resolve on the type itself. The D514/D527 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive, enum, or interface definition as an `ITypePtr` (for dereferencing to the
// `const IType& toType` and for passing to `ConstantResolveResult` / `ResolveResult` ctors).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	return MakeDef(ktc, kind);
}

// An enum definition: `Kind == Enum` and `KnownTypeCode == None` (a real enum carries no
// `KnownTypeCode`). `GetUnderlyingType(enumDef).Kind() == Enum` -- the to-side guard the
// enumeration arm checks.
ITypePtr EnumDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Enum);
}

// A `LookupTypeDefinition` for the `System.Nullable`1` generic definition (a struct,
// `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
	return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
		FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element;
// `NullableType.IsNullable` returns true -- the lifted-form crux (`0 -> E?`).
ITypePtr NullableOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A compile-time constant of type `int` (`Int32`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstInt32(std::int32_t val) {
	return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int32), std::any(val));
}

// An interpolated-string result whose `Type` is `string` (a `Class`), with an empty format string
// and an empty argument list. The RTTI check `dynamic_cast<InterpolatedStringResolveResult>`
// matches; the `Type().Kind()` is `Class` (NOT `Dynamic`), so it does not also fire the dynamic
// arm.
std::shared_ptr<InterpolatedStringResolveResult> InterpolatedString() {
	return std::make_shared<InterpolatedStringResolveResult>(
		Def(KnownTypeCode::String, TypeKind::Class), std::string(""), std::vector<std::shared_ptr<ResolveResult>>{});
}

// A non-constant result whose `Type` is `dynamic` (a `SpecialType(Dynamic, true)`) -- the
// dynamic arm fires on `Type().Kind() == TypeKind::Dynamic`.
std::shared_ptr<ResolveResult> DynamicResult() {
	return std::make_shared<ResolveResult>(std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType=*/true));
}

// A non-constant result whose `Type` is the supplied type (a plain `ResolveResult` base stub --
// `IsCompileTimeConstant` is the base default `false`). Falls through to the IType-based
// fallback.
std::shared_ptr<ResolveResult> Result(ITypePtr type) {
	return std::make_shared<ResolveResult>(std::move(type));
}

} // namespace

// ===========================================================================
// ImplicitConversion(ResolveResult, IType, bool, bool) (CSharpConversions.cs line 101).
// The ResolveResult-based dispatch. The public entries call this with (true, true) (the
// implicit entry) and (false, false) (the explicit entry's implicit check).
// ===========================================================================

// ---------------------------------------------------------------------------
// The compile-time-constant enumeration arm -- `int 0 -> E` routes to the D527 enumeration
// conversion (a non-lifted EnumerationConversion) via the dispatch.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, CompileTimeConstantZeroToEnumRoutesToEnumerationConversion) {
	auto c = ConstInt32(0);
	auto result = ImplicitConversion(Compilation(), *c, *EnumDef(), /*allowUserDefined*/ true, /*allowTuple*/ true);
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_FALSE(result->IsLifted());
	EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// The lifted enumeration crux -- `int 0 -> E?` routes to the D527 LIFTED EnumerationConversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, CompileTimeConstantZeroToNullableEnumRoutesToLiftedEnumerationConversion) {
	auto c = ConstInt32(0);
	auto result = ImplicitConversion(Compilation(), *c, *NullableOf(EnumDef()), true, true);
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsLifted());
	EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// The compile-time-constant constant-expression arm -- `int 5 -> sbyte` (5 fits in [-128, 127])
// routes to the D521 constant-expression conversion. The dispatch returns the
// `ImplicitConstantExpressionConversion` singleton.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, CompileTimeConstantIntToFitTypeRoutesToImplicitConstantExpressionConversion) {
	auto c = ConstInt32(5);
	auto result = ImplicitConversion(Compilation(), *c, *Def(KnownTypeCode::SByte), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitConstantExpressionConversion().get());
}

// ---------------------------------------------------------------------------
// The enumeration arm fires regardless of `allowUserDefined` -- the public explicit entry calls
// the dispatch with `allowUserDefined: false`, but the enumeration arm is NOT gated by it. A
// `int 0 -> E` still routes to the EnumerationConversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, EnumerationArmFiresRegardlessOfAllowUserDefined) {
	auto c = ConstInt32(0);
	auto result = ImplicitConversion(Compilation(), *c, *EnumDef(), /*allowUserDefined*/ false, /*allowTuple*/ false);
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// The interpolated-string arm -- an `InterpolatedStringResolveResult` -> `IFormattable` routes
// to the `ImplicitInterpolatedStringConversion` singleton.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, InterpolatedStringToIFormattableIsInterpolatedStringConversion) {
	auto rr = InterpolatedString();
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::IFormattable, TypeKind::Class), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitInterpolatedStringConversion().get());
}

// ---------------------------------------------------------------------------
// The interpolated-string arm -- an `InterpolatedStringResolveResult` -> `FormattableString`
// routes to the `ImplicitInterpolatedStringConversion` singleton.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, InterpolatedStringToFormattableStringIsInterpolatedStringConversion) {
	auto rr = InterpolatedString();
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::FormattableString, TypeKind::Class), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitInterpolatedStringConversion().get());
}

// ---------------------------------------------------------------------------
// The interpolated-string arm does NOT fire for an unrelated to-type -- an
// `InterpolatedStringResolveResult` -> `int` (not IFormattable/FormattableString) falls through
// the arm, then through the deferred/dynamic/throw arms, to the IType-based fallback, which
// yields `None` (a `string` does not implicitly convert to `int`).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, InterpolatedStringToUnrelatedTypeFallsThroughToNone) {
	auto rr = InterpolatedString();
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::Int32), true, true);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// The dynamic arm -- a result whose `Type` is `dynamic` routes to the
// `ImplicitDynamicConversion` singleton (a `dynamic` converts implicitly to any type).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, DynamicTypedResultIsImplicitDynamicConversion) {
	auto rr = DynamicResult();
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::Int32), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitDynamicConversion().get());
}

// ---------------------------------------------------------------------------
// The throw-expression arm -- a `ThrowResolveResult` routes to the
// `ThrowExpressionConversion` singleton (a throw expression converts implicitly to any type).
// `ThrowResolveResult::Type()` is `NoType()` (Kind == None, NOT Dynamic), so it falls through
// the dynamic arm to the throw arm.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, ThrowResolveResultIsThrowExpressionConversion) {
	auto rr = std::make_shared<ThrowResolveResult>();
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::Int32), true, true);
	EXPECT_EQ(result.get(), Conversions::ThrowExpressionConversion().get());
}

// ---------------------------------------------------------------------------
// The compile-time-constant standard-implicit fallback -- a `int 5 -> long` (long is not an
// enum, and `int -> long` is not a constant-expression conversion) routes to the D523
// `StandardImplicitConversion`, whose numeric-widening arm yields `ImplicitNumericConversion`.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, CompileTimeConstantIntToLongRoutesToStandardImplicitNumericConversion) {
	auto c = ConstInt32(5);
	auto result = ImplicitConversion(Compilation(), *c, *Def(KnownTypeCode::Int64), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
}

// ---------------------------------------------------------------------------
// The non-constant IType-based fallback -- a non-constant `int -> long` result routes to the
// D531 IType-based `ImplicitConversion`, whose `StandardImplicitConversion` numeric-widening
// arm yields `ImplicitNumericConversion`.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, NonConstantResultRoutesToITypeBasedImplicitConversion) {
	auto rr = Result(Def(KnownTypeCode::Int32));
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::Int64), true, true);
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
}

// ---------------------------------------------------------------------------
// A non-constant inconvertible pair -- a non-constant `bool -> string` yields `None` (no
// standard implicit conversion, and the user-defined fallback finds no operators on the
// stub definitions).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitResolveResultTest, NonConstantInconvertibleReturnsNone) {
	auto rr = Result(Def(KnownTypeCode::Boolean));
	auto result = ImplicitConversion(Compilation(), *rr, *Def(KnownTypeCode::String, TypeKind::Class), true, true);
	EXPECT_EQ(result.get(), Conversions::None().get());
}
