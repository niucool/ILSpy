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

// Tests for the `CSharpConversions` nullable-conversion helpers and the null-literal
// conversion -- `Detail::ImplicitNullableConversion` (CSharpConversions.cs line 490, C# 9.0
// spec section 10.2.6), `Detail::ExplicitNullableConversion` (line 505, C# spec draft-v11
// section 10.3.4) and `Detail::NullLiteralConversion` (line 524, C# 9.0 spec section 10.2.7).
// These are the first conversion helpers to RETURN a `Conversion` (the prior numeric /
// identity / enumeration helpers returned bool), so the tests assert the returned
// `shared_ptr<Conversion>`'s kind flags (IsNullableConversion / IsNumericConversion /
// IsEnumerationConversion / IsImplicit / IsExplicit / IsLifted / IsValid) and, for the
// singleton-backed returns, pointer-identity against the `Conversions` factory singleton.
//
// They are pure (no `CSharpConversions` instance state), so they land as `Detail::` free
// functions. The nullable pair takes `IType&` (non-const) because it feeds the underlying
// types to `IdentityConversion(IType&, IType&)` (the non-const `AcceptVisitor`); the
// null-literal helper takes `const IType&` (only reads `Kind` / `IsReferenceType` / `IsNullable`).
//
// CRUX STUB CONVENTIONS (carried from the D514 numeric/enumeration tests and the D515
// NullableType test):
//  * The underlying element types MUST be `LookupTypeDefinition`s (IS-A `ITypeDefinition`, so
//    `ReflectionHelper.GetTypeCode` resolves the `KnownTypeCode`) -- a `KnownType` is NOT an
//    `ITypeDefinition` and yields `TypeCode::Empty`, which would break `ImplicitNumericConversion`
//    / `AnyNumericConversion`. The `Prim` / `EnumDef` factories build `LookupTypeDefinition`s.
//  * `LookupTypeDefinition::StructuralEquals` is identity equality (`this == &other`), so the
//    IDENTITY-conversion crux cases (Nullable<T> -> Nullable<T>, T -> Nullable<T>, Nullable<T> ->
//    T) reuse the SAME element instance for both sides, so the erased-underlying comparison is
//    pointer-equal (the D514 IdentityConversion test's same-instance precedent). The numeric
//    crux cases use DISTINCT instances (int vs long), so the identity check is false and the
//    numeric / enumeration arm fires.
//  * A plain `LookupTypeDefinition` passes through `NormalizeTypeVisitor.TypeErasure` unchanged
//    (AcceptVisitor -> VisitOtherType -> VisitChildren -> shared_from_this; the underlying types
//    here are int/long/enum, none of the object/IntPtr/UIntPtr special-erasure cases), so the
//    `VisitableDefinition` (AcceptVisitor -> VisitTypeDefinition) override the identity test
//    uses is NOT needed here.
//  * `Nullable<T>` is a `ParameterizedType` over the `System.Nullable`1` definition
//    (`KnownTypeCode::NullableOfT`); `NullableType.IsNullable` / `GetUnderlyingType` (D515)
//    recognize it and return `T`.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions (singleton comparison)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitNullableConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitNullableConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::NullLiteralConversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
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

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via
// the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Prim(Int32)`
// reports `TypeCode::Int32` -- the observable behavior of a real primitive definition. Mirrors
// the `MakeDef` factory in the D514 enumeration test.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive numeric definition (Int32, Int64, ...). `GetTypeCode` resolves the `KnownTypeCode`
// so the numeric helpers dispatch over the real `TypeCode`.
std::shared_ptr<LookupTypeDefinition> Prim(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Struct);
}

// An enum definition: `Kind == Enum`, `KnownTypeCode == None` (so `GetTypeCode == Empty` and
// `IsNumericType` is false). `ExplicitEnumerationConversion` reads `Kind` directly on the enum
// side, so it never asks `IsNumericType` of an enum.
std::shared_ptr<LookupTypeDefinition> EnumDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Enum);
}

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
	return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
		FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type.
ITypePtr NullableOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// The null-literal type (`TypeKind::Null`). Faithful to the C# `SpecialType.NullType` singleton
// (`isReferenceType: true` -- see the `SpecialType` ctor comment); `NullLiteralConversion` only
// reads `Kind` on the from-side, so the `isReferenceType` value is not load-bearing here.
ITypePtr NullType() {
	return std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
}

} // namespace

// ===========================================================================
// ImplicitNullableConversion (C# 9.0 spec section 10.2.6) -- the implicit (lifted) nullable
// conversion. Acts ONLY when `toType` is nullable; an identity conversion on the underlying
// types yields `ImplicitNullableConversion`, an implicit numeric conversion yields
// `ImplicitLiftedNumericConversion`, else `None`.
// ===========================================================================

// Nullable<int> -> Nullable<int> (same int instance): the lifted identity conversion. Both
// sides are nullable; the underlying `int`s are the same instance, so `IdentityConversion` is
// true (the erased comparison is pointer-equal).
TEST(CSharpConversionsNullableTest, ImplicitNullableIdentityLiftedBothNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ImplicitNullableConversion(*NullableOf(intEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ImplicitNullableConversion().get());
	EXPECT_TRUE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_FALSE(result->IsExplicit());
	EXPECT_FALSE(result->IsLifted());
	EXPECT_FALSE(result->IsNumericConversion());
	EXPECT_TRUE(result->IsValid());
}

// int -> Nullable<int> (same int instance): the non-nullable-to-nullable identity lift. The
// to-side is nullable; `GetUnderlyingType(int)` returns `int` itself, so the underlying `int`s
// are the same instance -> identity.
TEST(CSharpConversionsNullableTest, ImplicitNullableIdentityLiftedFromNonNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ImplicitNullableConversion(*intEl, *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ImplicitNullableConversion().get());
	EXPECT_TRUE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsImplicit());
}

// Nullable<int> -> Nullable<long> (distinct int/long instances): the lifted implicit numeric
// conversion (int -> long is a widening implicit numeric conversion). Identity on the
// distinct underlying types is false; `ImplicitNumericConversion(int, long)` is true.
TEST(CSharpConversionsNullableTest, ImplicitNullableLiftedNumericWidening) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	auto result = ImplicitNullableConversion(*NullableOf(intEl), *NullableOf(longEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ImplicitLiftedNumericConversion().get());
	EXPECT_TRUE(result->IsNumericConversion());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsLifted());
	EXPECT_FALSE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsValid());
}

// int -> Nullable<long> (distinct int/long instances): the lifted implicit numeric conversion
// from a non-nullable source. `GetUnderlyingType(int)` returns `int`; `ImplicitNumericConversion
// (int, long)` is the widening conversion.
TEST(CSharpConversionsNullableTest, ImplicitNullableLiftedNumericFromNonNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	auto result = ImplicitNullableConversion(*intEl, *NullableOf(longEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ImplicitLiftedNumericConversion().get());
	EXPECT_TRUE(result->IsNumericConversion());
	EXPECT_TRUE(result->IsLifted());
}

// Nullable<int> -> int: the to-side is NOT nullable, so there is no implicit nullable
// conversion (implicit nullable conversions go only TO a nullable type) -> None.
TEST(CSharpConversionsNullableTest, ImplicitNullableNoneWhenToTypeNotNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ImplicitNullableConversion(*NullableOf(intEl), *intEl);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// int -> int: neither side is nullable -> None.
TEST(CSharpConversionsNullableTest, ImplicitNullableNoneWhenNeitherNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	auto result = ImplicitNullableConversion(*intEl, *longEl);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// Nullable<long> -> Nullable<int>: long -> int is a NARROWING numeric conversion, which is NOT
// implicit, so the lifted implicit numeric conversion does not fire; identity on the distinct
// underlying types is false too -> None. (This is the explicit-nullable direction.)
TEST(CSharpConversionsNullableTest, ImplicitNullableNoneForNarrowingNumeric) {
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ImplicitNullableConversion(*NullableOf(longEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// Nullable<enum> -> Nullable<int>: the implicit nullable conversion does NOT lift the explicit
// enumeration conversion (enum -> int is explicit, not implicit), and the enum is not numeric,
// so neither arm fires -> None. Pins that implicit-nullable dispatches to `ImplicitNumericConversion`
// (which rejects the enum) and never to the enumeration helper.
TEST(CSharpConversionsNullableTest, ImplicitNullableNoneDoesNotLiftEnumeration) {
	ITypePtr enumEl = EnumDef();
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ImplicitNullableConversion(*NullableOf(enumEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// ===========================================================================
// ExplicitNullableConversion (C# spec draft-v11 section 10.3.4) -- the explicit (lifted)
// nullable conversion. Acts when EITHER operand is nullable; identity -> ExplicitNullableConversion,
// any-numeric -> ExplicitLiftedNumericConversion, explicit-enumeration -> EnumerationConversion
// (false, true) (explicit + lifted + enumeration), else None.
// ===========================================================================

// Nullable<int> -> Nullable<int> (same int instance): the lifted identity conversion.
TEST(CSharpConversionsNullableTest, ExplicitNullableIdentityLiftedBothNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ExplicitNullableConversion(*NullableOf(intEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ExplicitNullableConversion().get());
	EXPECT_TRUE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_FALSE(result->IsImplicit());
	EXPECT_FALSE(result->IsLifted());
	EXPECT_TRUE(result->IsValid());
}

// Nullable<long> -> Nullable<int> (distinct long/int instances): the lifted explicit numeric
// conversion (long -> int is a narrowing explicit numeric conversion). `AnyNumericConversion` is
// true for the underlying types.
TEST(CSharpConversionsNullableTest, ExplicitNullableLiftedNumericNarrowing) {
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ExplicitNullableConversion(*NullableOf(longEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ExplicitLiftedNumericConversion().get());
	EXPECT_TRUE(result->IsNumericConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsLifted());
	EXPECT_FALSE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsValid());
}

// Nullable<int> -> int (same int instance): the from-side is nullable (the to-side is not); the
// underlying `int`s are the same instance -> identity -> ExplicitNullableConversion. Exercises
// the `IsNullable(fromType)` arm of the `||` guard.
TEST(CSharpConversionsNullableTest, ExplicitNullableIdentityFromNullableToPlain) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ExplicitNullableConversion(*NullableOf(intEl), *intEl);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ExplicitNullableConversion().get());
	EXPECT_TRUE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsExplicit());
}

// int -> Nullable<int> (same int instance): the to-side is nullable (the from-side is not); the
// underlying `int`s are the same instance -> identity -> ExplicitNullableConversion. Exercises
// the `IsNullable(toType)` arm of the `||` guard.
TEST(CSharpConversionsNullableTest, ExplicitNullableIdentityFromPlainToNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ExplicitNullableConversion(*intEl, *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::ExplicitNullableConversion().get());
	EXPECT_TRUE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsExplicit());
}

// Nullable<enum> -> Nullable<int>: the lifted explicit enumeration conversion. The underlying
// enum -> int is an explicit enumeration conversion, so the result is the
// `EnumerationConversion(false, true)` FACTORY (explicit + lifted + enumeration), a fresh
// per-call instance (NOT a singleton), so the test asserts the kind flags, not pointer-identity.
TEST(CSharpConversionsNullableTest, ExplicitNullableLiftedEnumeration) {
	ITypePtr enumEl = EnumDef();
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto result = ExplicitNullableConversion(*NullableOf(enumEl), *NullableOf(intEl));
	ASSERT_NE(result, nullptr);
	EXPECT_TRUE(result->IsEnumerationConversion());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsLifted());
	EXPECT_FALSE(result->IsNumericConversion()); // isEnumeration flips IsNumericConversion off
	EXPECT_FALSE(result->IsNullableConversion());
	EXPECT_TRUE(result->IsValid());
	// The factory builds a fresh instance distinct from the singleton fields.
	EXPECT_NE(result.get(), Conversions::ExplicitNullableConversion().get());
	EXPECT_NE(result.get(), Conversions::ExplicitLiftedNumericConversion().get());
}

// int -> int: neither side is nullable -> None.
TEST(CSharpConversionsNullableTest, ExplicitNullableNoneWhenNeitherNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	auto result = ExplicitNullableConversion(*intEl, *longEl);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// Nullable<int> -> Nullable<string>: the underlying int -> string is neither identity, nor
// numeric (string is not numeric), nor an explicit enumeration conversion (neither side is an
// enum) -> None. Pins that the enumeration arm rejects a numeric-to-reference-type pair.
TEST(CSharpConversionsNullableTest, ExplicitNullableNoneForNumericToReference) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr strEl = MakeDef(KnownTypeCode::String, TypeKind::Class);
	auto result = ExplicitNullableConversion(*NullableOf(intEl), *NullableOf(strEl));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());
}

// ===========================================================================
// NullLiteralConversion (C# 9.0 spec section 10.2.7) -- the null literal (`TypeKind.Null`)
// converts to any nullable type or any reference type. A non-nullable value type (int) and an
// indeterminate-reference-ness type (UnknownType) do NOT accept the null literal.
// ===========================================================================

// null -> String: a reference type accepts the null literal.
TEST(CSharpConversionsNullableTest, NullLiteralToReferenceType) {
	EXPECT_TRUE(NullLiteralConversion(*NullType(), *std::make_shared<KnownType>(KnownTypeCode::String)));
}

// null -> int: a non-nullable value type does NOT accept the null literal.
TEST(CSharpConversionsNullableTest, NullLiteralToValueTypeRejected) {
	EXPECT_FALSE(NullLiteralConversion(*NullType(), *std::make_shared<KnownType>(KnownTypeCode::Int32)));
}

// null -> Nullable<int>: a nullable type accepts the null literal.
TEST(CSharpConversionsNullableTest, NullLiteralToNullable) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	EXPECT_TRUE(NullLiteralConversion(*NullType(), *NullableOf(intEl)));
}

// int -> String: a non-null from-type does not form a null-literal conversion.
TEST(CSharpConversionsNullableTest, NullLiteralRejectedForNonNullFromType) {
	EXPECT_FALSE(NullLiteralConversion(*std::make_shared<KnownType>(KnownTypeCode::Int32),
		*std::make_shared<KnownType>(KnownTypeCode::String)));
}

// null -> UnknownType: an indeterminate reference-ness (`IsReferenceType == nullopt`) does NOT
// satisfy `== true`, so the null literal does not convert. Pins the `bool? == true` port
// (`has_value() && *... == true`), not a truthiness check.
TEST(CSharpConversionsNullableTest, NullLiteralToIndeterminateReferenceTypeRejected) {
	ITypePtr unknown = std::make_shared<SpecialType>(TypeKind::Unknown);
	EXPECT_FALSE(NullLiteralConversion(*NullType(), *unknown));
}
