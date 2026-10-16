// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including limitation the rights to use, copy,
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

// Tests for the public `CSharpConversions::ImplicitConversion(ResolveResult, IType)` /
// `ExplicitConversion(ResolveResult, IType)` entry points (CSharpConversions.cs lines 143 /
// 281). These are the ResolveResult-based public methods that delegate to the already-ported
// `Detail::ImplicitConversion(const ICompilation&, const ResolveResult&, IType&, bool, bool)`
// dispatch (D528). `ImplicitConversion(ResolveResult, IType)` is a thin wrapper delegating with
// `(allowUserDefined: true, allowTuple: true)`; `ExplicitConversion(ResolveResult, IType)` checks
// the dynamic arm first (`resolveResult.Type.Kind == TypeKind.Dynamic` -> ExplicitDynamicConversion),
// then the implicit-check-first (the dispatch with `(false, false)`), then the (deferred) tuple
// arm, then `Detail::ExplicitConversionImpl`, then `Detail::UserDefinedExplicitConversion`.
//
// CRUX: the dynamic arm in `ExplicitConversion(ResolveResult, IType)` fires BEFORE the implicit
// check (which would yield `ImplicitDynamicConversion`), so a `dynamic`-typed result yields
// `ExplicitDynamicConversion` -- this distinguishes the explicit ResolveResult method from the
// implicit one. The implicit-check-first crux (an implicit conversion subsumes the explicit one)
// mirrors the IType-based `ExplicitConversion`: `int 5 -> long` yields `ImplicitNumericConversion`
// (the implicit check wins), not `ExplicitNumericConversion`.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public methods)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::ImplicitConversion (for the RED neuter cross-check)
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

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
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
// `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via the numeric cast;
// `IsKnownType` reads the type's own `GetDefinition()->KnownTypeCode`, so a `Def(IFormattable,
// Class)` makes `IsKnownType(it, IFormattable)` resolve on the type itself. The D514/D527/D528
// `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	return MakeDef(ktc, kind);
}

// An enum definition: `Kind == Enum` and `KnownTypeCode == None` (a real enum carries no
// `KnownTypeCode`). `GetUnderlyingType(enumDef).Kind() == Enum` -- the to-side the enumeration
// arm checks.
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
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element.
ITypePtr NullableOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A compile-time constant of type `int` (`Int32`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstInt32(std::int32_t val) {
	return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int32), std::any(val));
}

// An interpolated-string result whose `Type` is `string` (a `Class`), with an empty format string
// and an empty argument list. The RTTI check matches; `Type().Kind()` is `Class` (NOT `Dynamic`).
std::shared_ptr<InterpolatedStringResolveResult> InterpolatedString() {
	return std::make_shared<InterpolatedStringResolveResult>(
		Def(KnownTypeCode::String, TypeKind::Class), std::string(""), std::vector<std::shared_ptr<ResolveResult>>{});
}

// A non-constant result whose `Type` is `dynamic` (a `SpecialType(Dynamic, true)`) -- the dynamic
// arm fires on `Type().Kind() == TypeKind::Dynamic`.
std::shared_ptr<ResolveResult> DynamicResult() {
	return std::make_shared<ResolveResult>(std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType=*/true));
}

// A non-constant result whose `Type` is the supplied type (a plain `ResolveResult` base stub --
// `IsCompileTimeConstant` is the base default `false`). Falls through to the IType-based fallback.
std::shared_ptr<ResolveResult> Result(ITypePtr type) {
	return std::make_shared<ResolveResult>(std::move(type));
}

} // namespace

// ===========================================================================
// CSharpConversions::ImplicitConversion(ResolveResult, IType) (CSharpConversions.cs line 143).
//
//   return ImplicitConversion(resolveResult, toType, allowUserDefined: true, allowTuple: true);
//
// A thin wrapper delegating to the Detail::ImplicitConversion ResolveResult-based dispatch
// (D528) with the public-entry flag pair (true, true). NOT cached.
// ===========================================================================

// The compile-time-constant enumeration arm -- `int 0 -> E` routes to the EnumerationConversion
// (non-lifted) via the dispatch.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionEnumerationArm) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(0);
	auto result = conversions.ImplicitConversion(*c, *EnumDef());
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_FALSE(result->IsLifted());
	EXPECT_TRUE(result->IsValid());
}

// The lifted enumeration crux -- `int 0 -> E?` routes to the LIFTED EnumerationConversion.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionLiftedEnumerationArm) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(0);
	auto result = conversions.ImplicitConversion(*c, *NullableOf(EnumDef()));
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsLifted());
	EXPECT_TRUE(result->IsValid());
}

// The constant-expression arm -- `int 5 -> sbyte` (5 fits in [-128, 127]) routes to the
// ImplicitConstantExpressionConversion singleton.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionConstantExpressionArm) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(5);
	auto result = conversions.ImplicitConversion(*c, *Def(KnownTypeCode::SByte));
	EXPECT_EQ(result.get(), Conversions::ImplicitConstantExpressionConversion().get());
}

// The interpolated-string arm -- an InterpolatedStringResolveResult -> IFormattable routes to the
// ImplicitInterpolatedStringConversion singleton.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionInterpolatedStringArm) {
	CSharpConversions conversions(Compilation());
	auto rr = InterpolatedString();
	auto result = conversions.ImplicitConversion(*rr, *Def(KnownTypeCode::IFormattable, TypeKind::Class));
	EXPECT_EQ(result.get(), Conversions::ImplicitInterpolatedStringConversion().get());
}

// The dynamic arm -- a result whose Type is `dynamic` routes to the ImplicitDynamicConversion
// singleton (a `dynamic` converts implicitly to any type).
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionDynamicArm) {
	CSharpConversions conversions(Compilation());
	auto rr = DynamicResult();
	auto result = conversions.ImplicitConversion(*rr, *Def(KnownTypeCode::Int32));
	EXPECT_EQ(result.get(), Conversions::ImplicitDynamicConversion().get());
}

// The throw-expression arm -- a ThrowResolveResult routes to the ThrowExpressionConversion
// singleton (a throw expression converts implicitly to any type).
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionThrowArm) {
	CSharpConversions conversions(Compilation());
	auto rr = std::make_shared<ThrowResolveResult>();
	auto result = conversions.ImplicitConversion(*rr, *Def(KnownTypeCode::Int32));
	EXPECT_EQ(result.get(), Conversions::ThrowExpressionConversion().get());
}

// The compile-time-constant standard-implicit fallback -- `int 5 -> long` routes to the
// StandardImplicitConversion numeric-widening arm -> ImplicitNumericConversion.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionStandardImplicitNumeric) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(5);
	auto result = conversions.ImplicitConversion(*c, *Def(KnownTypeCode::Int64));
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
}

// The non-constant IType-based fallback -- a non-constant `int -> long` routes to the IType-based
// ImplicitConversion, whose numeric-widening arm yields ImplicitNumericConversion.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionNonConstantNumeric) {
	CSharpConversions conversions(Compilation());
	auto rr = Result(Def(KnownTypeCode::Int32));
	auto result = conversions.ImplicitConversion(*rr, *Def(KnownTypeCode::Int64));
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
}

// A non-constant inconvertible pair -- a non-constant `bool -> string` yields None.
TEST(CSharpConversionsResolveResultEntryTest, PublicImplicitConversionNone) {
	CSharpConversions conversions(Compilation());
	auto rr = Result(Def(KnownTypeCode::Boolean));
	auto result = conversions.ImplicitConversion(*rr, *Def(KnownTypeCode::String, TypeKind::Class));
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// ===========================================================================
// CSharpConversions::ExplicitConversion(ResolveResult, IType) (CSharpConversions.cs line 281).
//
//   if (resolveResult.Type.Kind == TypeKind.Dynamic) return Conversion.ExplicitDynamicConversion;
//   Conversion c = ImplicitConversion(resolveResult, toType, false, false);
//   if (c != Conversion.None) return c;
//   if (resolveResult is TupleResolveResult tupleRR) { ... }   // DEFERRED
//   c = ExplicitConversionImpl(resolveResult.Type, toType);
//   if (c != Conversion.None) return c;
//   return UserDefinedExplicitConversion(resolveResult, resolveResult.Type, toType);
//
// The dynamic arm fires BEFORE the implicit check; the implicit check is first among the rest
// (an implicit conversion subsumes the explicit one).
// ===========================================================================

// ---------------------------------------------------------------------------
// The dynamic arm crux -- a `dynamic`-typed result yields ExplicitDynamicConversion, NOT
// ImplicitDynamicConversion. The dynamic arm fires before the implicit check (which would yield
// ImplicitDynamicConversion). This is the CRUX distinguishing the explicit ResolveResult method
// from the implicit one.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionDynamicArmIsExplicitDynamic) {
	CSharpConversions conversions(Compilation());
	auto rr = DynamicResult();
	auto result = conversions.ExplicitConversion(*rr, *Def(KnownTypeCode::Int32));
	EXPECT_EQ(result.get(), Conversions::ExplicitDynamicConversion().get());
	// Sanity: the dynamic conversion singleton is the explicit flavour (IsExplicit, NOT IsImplicit).
	EXPECT_TRUE(result->IsDynamicConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_FALSE(result->IsImplicit());
}

// ---------------------------------------------------------------------------
// Implicit-check-first: an implicit conversion is returned despite the method being named
// "ExplicitConversion". The CRUX mirroring the IType-based ExplicitConversion.
// ---------------------------------------------------------------------------

// `int 5 -> long` (compile-time constant): the implicit check fires the standard-implicit numeric
// widening arm -> ImplicitNumericConversion (the implicit check wins; ExplicitConversionImpl
// would yield ExplicitNumericConversion).
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionImplicitCheckFirstNumeric) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(5);
	auto result = conversions.ExplicitConversion(*c, *Def(KnownTypeCode::Int64));
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
}

// `int 0 -> E` (compile-time constant): the implicit check fires the enumeration arm -> the
// EnumerationConversion (implicit). The implicit-check-first fires the enumeration arm before the
// explicit dispatch.
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionImplicitCheckFirstEnumeration) {
	CSharpConversions conversions(Compilation());
	auto c = ConstInt32(0);
	auto result = conversions.ExplicitConversion(*c, *EnumDef());
	EXPECT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// `throw -> long`: the implicit check fires the throw arm -> ThrowExpressionConversion (a throw
// expression converts implicitly to any type, so the implicit check wins).
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionImplicitCheckFirstThrow) {
	CSharpConversions conversions(Compilation());
	auto rr = std::make_shared<ThrowResolveResult>();
	auto result = conversions.ExplicitConversion(*rr, *Def(KnownTypeCode::Int64));
	EXPECT_EQ(result.get(), Conversions::ThrowExpressionConversion().get());
}

// ---------------------------------------------------------------------------
// Explicit fallback: the implicit check returns None, so the ExplicitConversionImpl dispatch
// fires and returns the EXPLICIT conversion singleton.
// ---------------------------------------------------------------------------

// `long -> int` (non-constant): the implicit check returns None (no implicit narrowing), then
// ExplicitConversionImpl fires the numeric arm -> ExplicitNumericConversion. The classic
// explicit narrowing.
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionExplicitFallbackNumeric) {
	CSharpConversions conversions(Compilation());
	auto rr = Result(Def(KnownTypeCode::Int64));
	auto result = conversions.ExplicitConversion(*rr, *Def(KnownTypeCode::Int32));
	EXPECT_EQ(result.get(), Conversions::ExplicitNumericConversion().get());
}

// `int 5 -> long` would be implicit, but `long -> int` (narrowing) is the explicit fallback. A
// non-constant `int* ` is not reachable via the ResolveResult dispatch's deferred arms; instead
// pin the explicit pointer fallback via a non-constant ResolveResult whose Type is a pointer.
// `void* -> int*`: the implicit check returns None (the implicit pointer arm converts any
// pointer TO void*, never FROM void*), then ExplicitConversionImpl fires the pointer arm ->
// ExplicitPointerConversion.
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionExplicitFallbackPointer) {
	CSharpConversions conversions(Compilation());
	auto voidPtr = std::make_shared<ILSpy::Decompiler::TypeSystem::PointerType>(
		std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(KnownTypeCode::Void));
	auto intPtr = std::make_shared<ILSpy::Decompiler::TypeSystem::PointerType>(
		std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(KnownTypeCode::Int32));
	auto rr = Result(voidPtr);
	auto result = conversions.ExplicitConversion(*rr, *intPtr);
	EXPECT_EQ(result.get(), Conversions::ExplicitPointerConversion().get());
}

// ---------------------------------------------------------------------------
// None case -- no dynamic, no implicit, no explicit, no user-defined -> None.
// ---------------------------------------------------------------------------

// `bool -> string`: the implicit check returns None, ExplicitConversionImpl returns None, and
// UserDefinedExplicitConversion returns None (no op_Explicit operator on the stub definitions).
TEST(CSharpConversionsResolveResultEntryTest, PublicExplicitConversionNone) {
	CSharpConversions conversions(Compilation());
	auto rr = Result(Def(KnownTypeCode::Boolean));
	auto result = conversions.ExplicitConversion(*rr, *Def(KnownTypeCode::String, TypeKind::Class));
	EXPECT_EQ(result.get(), Conversions::None().get());
}
