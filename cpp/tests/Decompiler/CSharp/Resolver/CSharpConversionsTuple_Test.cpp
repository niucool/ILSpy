// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Detail::TupleConversion` (CSharpConversions.cs lines 1480 + 1506, C# 9.0 spec
// sections 10.2.13 + 10.3.6) -- the tuple-conversion helpers, plus the four dispatch arms that
// wire them:
//   * `Detail::StandardImplicitConversion` (D523) -- the tuple arm after the pointer arm.
//   * `Detail::ExplicitConversionImpl` (D524) -- the LAST arm returns the TupleConversion as-is.
//   * `Detail::ImplicitConversion(ResolveResult, ...)` (D528) -- the `else`-branch tuple arm
//     gated on `allowTuple && resolveResult is TupleResolveResult`.
//   * `CSharpConversions::ExplicitConversion(ResolveResult, IType)` (line 281) -- the
//     `resolveResult is TupleResolveResult` arm after the implicit-check-first.
//
// CRUX STUB CONVENTIONS (carried from the D529/D539 GetTupleElementTypes / D531 delegate tests):
//  * `Def(ktc)` is the shared-managed `LookupTypeDefinition` stub (the D514 precedent). The
//    per-element conversion dispatch (`CSharpConversions::Get(compilation).ImplicitConversion` /
//    `.ExplicitConversion`) calls `GetTypeCode` (resolves `KnownTypeCode` via the numeric cast) and
//    `IdentityConversion` (structural equality via `TypeErasure`); a `LookupTypeDefinition`
//    satisfies both (IS-A `ITypeDefinition`, `GetDefinition() == this`).
//  * `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    identity-conversion crux cases (the same-type element pairs) MUST reuse the SAME `ITypePtr`
//    instance for both sides; the numeric-widening crux cases use DISTINCT instances (identity
//    fails, the numeric arm fires via `GetTypeCode`).
//  * `ValueTupleDef(arity)` is the `System.ValueTuple`N` generic definition stub (Namespace=
//    "System", Name="ValueTuple", Kind=Struct); `ValueTupleOf(def, args)` is an instantiated
//    `ParameterizedType` over it -- the underlying representation a real C# tuple type resolves
//    to (the D539 precedent).
//  * `TupleLiteral(elements, underlying)` builds a `TupleResolveResult` from per-element
//    `ResolveResult`s + the pre-built underlying `ValueTuple<...>` (the D405 `TupleType`
//    minimal-port ctor accepts a pre-built underlying, so the `TupleResolveResult` ctor threads
//    it to `GetTupleType`).
//  * `Compilation()` is a `LookupCompilation` with a `CacheManager` so
//    `CSharpConversions::Get(compilation)` resolves (the per-compilation cached singleton the
//    `Detail::TupleConversion` per-element calls thread).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"            // CSharpConversions (the public methods)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"     // Detail::TupleConversion
#include "Decompiler/Semantics/Conversion.hpp"                         // Conversion (the flag accessors)
#include "Decompiler/Semantics/ConversionFactories.hpp"               // Conversions (the singletons)
#include "Decompiler/Semantics/ResolveResult.hpp"                      // ResolveResult (the plain base stub)
#include "Decompiler/Semantics/TupleResolveResult.hpp"                // TupleResolveResult (the tuple-literal stub)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"                            // ParameterizedType, KnownType, TupleType
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TupleConversion;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TupleResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A shared-managed `LookupTypeDefinition` stub with a configurable `KnownTypeCode` / `TypeKind`
// (the D514 `MakeDef` precedent). The per-element conversion dispatch calls `GetTypeCode` (resolves
// the `KnownTypeCode`) and `IdentityConversion` (structural equality), so the stub MUST be a
// `LookupTypeDefinition` (IS-A `ITypeDefinition`); a `KnownType` placeholder (NOT an
// `ITypeDefinition`, `GetDefinition()`=null) yields `TypeCode::Empty` for `GetTypeCode`.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// The `System.ValueTuple`N` generic definition stub: a `LookupTypeDefinition` with
// Namespace="System", Name="ValueTuple", Kind=Struct, so `GetTupleElementTypes` recognises an
// instantiated `ParameterizedType` over it (the D539 precedent).
std::shared_ptr<LookupTypeDefinition> ValueTupleDef(int arity) {
	return std::make_shared<LookupTypeDefinition>(
		"ValueTuple", "System",
		FullTypeName(TopLevelTypeName("System", "ValueTuple", arity)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// An instantiated `System.ValueTuple<...>` over the definition stub with the given element type
// arguments. `Kind()` delegates to the definition's `Kind()` (Struct), `Name()` to "ValueTuple",
// `GetDefinition()` to the definition (Namespace="System").
ITypePtr ValueTupleOf(std::shared_ptr<LookupTypeDefinition> def, std::vector<ITypePtr> args) {
	return std::make_shared<ParameterizedType>(std::move(def), std::move(args));
}

// A `ValueTuple<T1, T2>` over the supplied element types (a 2-arg `ParameterizedType`).
ITypePtr ValueTuple2(ITypePtr e0, ITypePtr e1) {
	return ValueTupleOf(ValueTupleDef(2), {std::move(e0), std::move(e1)});
}

// A plain `ResolveResult` base stub carrying the supplied type (the per-element result of a
// tuple literal `(T)`; `IsCompileTimeConstant` is the base default `false`).
std::shared_ptr<ResolveResult> ElementResult(ITypePtr type) {
	return std::make_shared<ResolveResult>(std::move(type));
}

// A `TupleResolveResult` carrying the per-element `ResolveResult`s, built through the
// C#-faithful ctor over the shared compilation and a valueTupleAssembly module that
// registers the `System.ValueTuple`8` definition (the C# `FindValueTupleType` resolves
// the underlying chain through the module arm). The `elementNames` default to
// `std::nullopt` (the C# `default(ImmutableArray<string>)` "not provided" sentinel).
// The value-tuple module the `TupleLiteral` helper hands to the C#-faithful ctor (the
// registered `System.ValueTuple`8` definition covers every arity the tests build).
struct VtModuleFixture {
	LookupCompilation comp;
	LookupModule module;
	std::shared_ptr<LookupTypeDefinition> def;
	VtModuleFixture() : module(comp, "VtLib") {
		def = std::make_shared<LookupTypeDefinition>(
			"ValueTuple", "System",
			FullTypeName(TopLevelTypeName("System", "ValueTuple", 8)),
			TypeKind::Struct, Accessibility::Public, comp, &module);
		module.SetTypeDefinition(TopLevelTypeName("System", "ValueTuple", 8), def.get());
	}
};
VtModuleFixture& VtFixture() {
	static VtModuleFixture f;
	return f;
}

std::shared_ptr<TupleResolveResult> TupleLiteral(
		std::vector<std::shared_ptr<ResolveResult>> elements, ITypePtr /*underlying*/) {
	return std::make_shared<TupleResolveResult>(VtFixture().comp, std::move(elements),
	                                            std::nullopt, &VtFixture().module);
}

} // namespace

// ===========================================================================
// Detail::TupleConversion(const TupleResolveResult&, IType&, bool) -- the tuple-literal overload.
// ===========================================================================

// A 2-element tuple literal `(int, string)` -> `ValueTuple<int, string>` with identity element
// conversions (the same `int`/`string` instances on both sides) yields a TupleConversion.
TEST(CSharpConversionsTupleTest, TupleResolveResultTwoElementIdentityYieldsTupleConversion) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr underlying = ValueTuple2(intType, stringType);
	auto fromRR = TupleLiteral({ElementResult(intType), ElementResult(stringType)}, underlying);
	ITypePtr toType = ValueTuple2(intType, stringType);
	auto result = TupleConversion(Compilation(), *fromRR, *toType, /*isExplicit*/ false);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_FALSE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::IdentityConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::IdentityConversion().get());
}

// A 2-element tuple literal `(int, int)` -> `ValueTuple<long, long>` with implicit numeric
// widening element conversions yields a TupleConversion whose elements are ImplicitNumericConversion.
TEST(CSharpConversionsTupleTest, TupleResolveResultTwoElementImplicitNumericWidening) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType0 = Def(KnownTypeCode::Int64);
	ITypePtr longType1 = Def(KnownTypeCode::Int64);
	ITypePtr underlying = ValueTuple2(intType, intType);
	auto fromRR = TupleLiteral({ElementResult(intType), ElementResult(intType)}, underlying);
	ITypePtr toType = ValueTuple2(longType0, longType1);
	auto result = TupleConversion(Compilation(), *fromRR, *toType, /*isExplicit*/ false);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::ImplicitNumericConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::ImplicitNumericConversion().get());
}

// A 2-element tuple literal `(long, long)` -> `ValueTuple<int, int>` with EXPLICIT numeric
// narrowing element conversions yields a TupleConversion whose elements are ExplicitNumericConversion
// (the isExplicit=true per-element dispatch).
TEST(CSharpConversionsTupleTest, TupleResolveResultTwoElementExplicitNumericNarrowing) {
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr intType0 = Def(KnownTypeCode::Int32);
	ITypePtr intType1 = Def(KnownTypeCode::Int32);
	ITypePtr underlying = ValueTuple2(longType, longType);
	auto fromRR = TupleLiteral({ElementResult(longType), ElementResult(longType)}, underlying);
	ITypePtr toType = ValueTuple2(intType0, intType1);
	auto result = TupleConversion(Compilation(), *fromRR, *toType, /*isExplicit*/ true);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_FALSE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::ExplicitNumericConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::ExplicitNumericConversion().get());
}

// A length mismatch (2-element literal -> 3-element tuple) yields None.
TEST(CSharpConversionsTupleTest, TupleResolveResultLengthMismatchYieldsNone) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto def3 = ValueTupleDef(3);
	ITypePtr toType = ValueTupleOf(def3, {intType, intType, intType});
	ITypePtr underlying = ValueTuple2(intType, intType);
	auto fromRR = TupleLiteral({ElementResult(intType), ElementResult(intType)}, underlying);
	auto result = TupleConversion(Compilation(), *fromRR, *toType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A non-tuple toType (a plain `int`) yields None (GetTupleElementTypes returns nullopt).
TEST(CSharpConversionsTupleTest, TupleResolveResultNonTupleToTypeYieldsNone) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr underlying = ValueTuple2(intType, intType);
	auto fromRR = TupleLiteral({ElementResult(intType), ElementResult(intType)}, underlying);
	auto result = TupleConversion(Compilation(), *fromRR, *intType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// An element that does not convert implicitly (`bool` -> `string` is not implicit) short-circuits
// the whole tuple conversion to None, even when the other element converts.
TEST(CSharpConversionsTupleTest, TupleResolveResultInconvertibleElementYieldsNone) {
	ITypePtr boolType = Def(KnownTypeCode::Boolean);
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr underlying = ValueTuple2(boolType, intType);
	auto fromRR = TupleLiteral({ElementResult(boolType), ElementResult(intType)}, underlying);
	ITypePtr toType = ValueTuple2(stringType, longType);
	auto result = TupleConversion(Compilation(), *fromRR, *toType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// ===========================================================================
// Detail::TupleConversion(IType&, IType&, bool) -- the IType overload.
// ===========================================================================

// A `ValueTuple<int, string>` -> `ValueTuple<int, string>` (identity elements) yields a
// TupleConversion with identity element conversions.
TEST(CSharpConversionsTupleTest, ITypeOverloadTwoElementIdentityYieldsTupleConversion) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr fromType = ValueTuple2(intType, stringType);
	ITypePtr toType = ValueTuple2(intType, stringType);
	auto result = TupleConversion(Compilation(), *fromType, *toType, /*isExplicit*/ false);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::IdentityConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::IdentityConversion().get());
}

// A `ValueTuple<int, int>` -> `ValueTuple<long, long>` (implicit numeric widening) yields a
// TupleConversion with ImplicitNumericConversion elements.
TEST(CSharpConversionsTupleTest, ITypeOverloadTwoElementImplicitNumericWidening) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType0 = Def(KnownTypeCode::Int64);
	ITypePtr longType1 = Def(KnownTypeCode::Int64);
	ITypePtr fromType = ValueTuple2(intType, intType);
	ITypePtr toType = ValueTuple2(longType0, longType1);
	auto result = TupleConversion(Compilation(), *fromType, *toType, /*isExplicit*/ false);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::ImplicitNumericConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::ImplicitNumericConversion().get());
}

// A `ValueTuple<long, long>` -> `ValueTuple<int, int>` (explicit numeric narrowing) yields a
// TupleConversion with ExplicitNumericConversion elements (the isExplicit=true per-element dispatch).
TEST(CSharpConversionsTupleTest, ITypeOverloadTwoElementExplicitNumericNarrowing) {
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr intType0 = Def(KnownTypeCode::Int32);
	ITypePtr intType1 = Def(KnownTypeCode::Int32);
	ITypePtr fromType = ValueTuple2(longType, longType);
	ITypePtr toType = ValueTuple2(intType0, intType1);
	auto result = TupleConversion(Compilation(), *fromType, *toType, /*isExplicit*/ true);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
	auto elements = result->ElementConversions();
	ASSERT_EQ(elements.size(), 2u);
	EXPECT_EQ(elements[0].get(), Conversions::ExplicitNumericConversion().get());
	EXPECT_EQ(elements[1].get(), Conversions::ExplicitNumericConversion().get());
}

// A length mismatch (2-element -> 3-element tuple) yields None.
TEST(CSharpConversionsTupleTest, ITypeOverloadLengthMismatchYieldsNone) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto def3 = ValueTupleDef(3);
	ITypePtr toType = ValueTupleOf(def3, {intType, intType, intType});
	ITypePtr fromType = ValueTuple2(intType, intType);
	auto result = TupleConversion(Compilation(), *fromType, *toType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A non-tuple fromType (a plain `int`) yields None (GetTupleElementTypes returns nullopt ->
// IsDefaultOrEmpty).
TEST(CSharpConversionsTupleTest, ITypeOverloadNonTupleFromTypeYieldsNone) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr toType = ValueTuple2(longType, longType);
	auto result = TupleConversion(Compilation(), *intType, *toType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A non-tuple toType (a plain `int`) yields None (GetTupleElementTypes returns nullopt -> IsDefault).
TEST(CSharpConversionsTupleTest, ITypeOverloadNonTupleToTypeYieldsNone) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr fromType = ValueTuple2(longType, longType);
	auto result = TupleConversion(Compilation(), *fromType, *intType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// An element that does not convert implicitly (`bool` -> `string`) short-circuits to None.
TEST(CSharpConversionsTupleTest, ITypeOverloadInconvertibleElementYieldsNone) {
	ITypePtr boolType = Def(KnownTypeCode::Boolean);
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr fromType = ValueTuple2(boolType, intType);
	ITypePtr toType = ValueTuple2(stringType, longType);
	auto result = TupleConversion(Compilation(), *fromType, *toType, /*isExplicit*/ false);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// ===========================================================================
// Dispatch wiring: StandardImplicitConversion's tuple arm (D523).
// ===========================================================================

// `StandardImplicitConversion(ValueTuple<int,int>, ValueTuple<long,long>)` -- the identity arm
// fails (int != long structurally), the tuple arm fires with implicit numeric widening elements.
TEST(CSharpConversionsTupleTest, StandardImplicitConversionTupleArmFires) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType0 = Def(KnownTypeCode::Int64);
	ITypePtr longType1 = Def(KnownTypeCode::Int64);
	ITypePtr fromType = ValueTuple2(intType, intType);
	ITypePtr toType = ValueTuple2(longType0, longType1);
	auto result = conversions.StandardImplicitConversion(*fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// `StandardImplicitConversion(int, int)` -- a non-tuple shape does NOT fire the tuple arm (the
// identity arm wins first); the result is the IdentityConversion singleton, NOT a TupleConversion.
TEST(CSharpConversionsTupleTest, StandardImplicitConversionNonTupleDoesNotFireTupleArm) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto result = conversions.StandardImplicitConversion(*intType, *intType);
	EXPECT_EQ(result.get(), Conversions::IdentityConversion().get());
	EXPECT_FALSE(result->IsTupleConversion());
}

// ===========================================================================
// Dispatch wiring: ExplicitConversionImpl's tail (D524) via ExplicitConversion(IType, IType).
// ===========================================================================

// `ExplicitConversion(ValueTuple<long,long>, ValueTuple<int,int>)` -- the implicit check (the
// tuple arm with isExplicit=false) yields None (long->int is not implicit), then ExplicitConversionImpl
// fires the tuple arm with isExplicit=true (ExplicitNumericConversion elements) -> a TupleConversion.
TEST(CSharpConversionsTupleTest, ExplicitConversionITypeTupleArmFires) {
	CSharpConversions conversions(Compilation());
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr intType0 = Def(KnownTypeCode::Int32);
	ITypePtr intType1 = Def(KnownTypeCode::Int32);
	ITypePtr fromType = ValueTuple2(longType, longType);
	ITypePtr toType = ValueTuple2(intType0, intType1);
	auto result = conversions.ExplicitConversion(*fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
}

// ===========================================================================
// Dispatch wiring: ImplicitConversion(ResolveResult, IType) (D528) -- the else-branch tuple arm.
// ===========================================================================

// `ImplicitConversion(TupleResolveResult(int,int), ValueTuple<long,long>)` -- the
// allowTuple=true tuple arm fires with implicit numeric widening elements -> a TupleConversion.
TEST(CSharpConversionsTupleTest, ImplicitConversionResolveResultTupleArmFires) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType0 = Def(KnownTypeCode::Int64);
	ITypePtr longType1 = Def(KnownTypeCode::Int64);
	ITypePtr underlying = ValueTuple2(intType, intType);
	auto fromRR = TupleLiteral({ElementResult(intType), ElementResult(intType)}, underlying);
	ITypePtr toType = ValueTuple2(longType0, longType1);
	auto result = conversions.ImplicitConversion(*fromRR, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// `ImplicitConversion(TupleResolveResult(bool,string), ValueTuple<long,long>)` -- an inconvertible
// element (bool->long is not implicit) makes the tuple arm yield None; the dispatch then falls
// through to the throw / IType-based fallback (the IType fallback ImplicitConversion(TupleType,
// ValueTuple<long,long>) also yields None -- bool->long not implicit) -> None.
TEST(CSharpConversionsTupleTest, ImplicitConversionResolveResultInconvertibleElementYieldsNone) {
	CSharpConversions conversions(Compilation());
	ITypePtr boolType = Def(KnownTypeCode::Boolean);
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr longType0 = Def(KnownTypeCode::Int64);
	ITypePtr longType1 = Def(KnownTypeCode::Int64);
	ITypePtr underlying = ValueTuple2(boolType, stringType);
	auto fromRR = TupleLiteral({ElementResult(boolType), ElementResult(stringType)}, underlying);
	ITypePtr toType = ValueTuple2(longType0, longType1);
	auto result = conversions.ImplicitConversion(*fromRR, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A non-tuple ResolveResult (a plain `int`) does NOT fire the tuple arm (the dynamic_cast yields
// nullptr); the IType-based fallback fires the numeric-widening arm -> ImplicitNumericConversion.
TEST(CSharpConversionsTupleTest, ImplicitConversionResolveResultNonTupleFallsThrough) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	auto rr = ElementResult(intType);
	auto result = conversions.ImplicitConversion(*rr, *longType);
	EXPECT_EQ(result.get(), Conversions::ImplicitNumericConversion().get());
	EXPECT_FALSE(result->IsTupleConversion());
}

// ===========================================================================
// Dispatch wiring: ExplicitConversion(ResolveResult, IType) (line 281) -- the tuple arm.
// ===========================================================================

// `ExplicitConversion(TupleResolveResult(long,long), ValueTuple<int,int>)` -- the dynamic arm
// skips (Type is a TupleType, not Dynamic); the implicit check (allowTuple=false, so the tuple arm
// is skipped) yields None; the tuple arm fires with isExplicit=true (ExplicitNumericConversion
// elements) -> a TupleConversion.
TEST(CSharpConversionsTupleTest, ExplicitConversionResolveResultTupleArmFires) {
	CSharpConversions conversions(Compilation());
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr intType0 = Def(KnownTypeCode::Int32);
	ITypePtr intType1 = Def(KnownTypeCode::Int32);
	ITypePtr underlying = ValueTuple2(longType, longType);
	auto fromRR = TupleLiteral({ElementResult(longType), ElementResult(longType)}, underlying);
	ITypePtr toType = ValueTuple2(intType0, intType1);
	auto result = conversions.ExplicitConversion(*fromRR, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsTupleConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
}
