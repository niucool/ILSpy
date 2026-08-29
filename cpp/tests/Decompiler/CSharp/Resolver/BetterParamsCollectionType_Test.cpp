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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `OverloadResolution` "better params-collection type" tiebreak (the C# 13.0
// params-collections proposal, OverloadResolution.cs `BetterParamsCollectionType`) ported as a
// `Detail::` free function:
//   * `BetterParamsCollectionType(CSharpConversions&, IType&, IType&)` -- returns 1 if the first
//     params-collection type is better, 2 if the second is, 0 if neither. The non-span arm prefers
//     the type that implicitly converts to the other but not vice versa; the span arms prefer
//     `ReadOnlySpan<T>` over `Span<T>` and a `Span<T>`/`ReadOnlySpan<T>` over an array/array-interface
//     when the element types identity-match.
//
// The tests pin the non-span implicit-convertibility arm (the `conversions.ImplicitConversion`
// cached public entry), the four span arms (ReadOnlySpan-vs-Span identity both directions,
// Span-vs-array identity both directions), and the None fall-throughs (non-identity elements,
// inconvertible non-span pair, identity non-span pair, two same-kind spans).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::BetterParamsCollectionType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) that reports itself
// as a reference type -- the shape the `ImplicitReferenceConversion` guard needs (a definite
// `IsReferenceType == true` on both sides, the D517 `RefDef` precedent).
class RefDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32` (the D514 `MakeDef` precedent).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive element definition as an `ITypePtr` (for dereferencing to the `IType&`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	return MakeDef(ktc, kind);
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object` makes
// `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit fires,
// the D517 precedent).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
	int n = static_cast<int>(ktc);
	std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
	return std::make_shared<RefDef>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the `IsSubtypeOf` short-circuit fires). A function-local
// static so the instance (and its address, used as a cache key) persists across tests.
std::shared_ptr<RefDef> ObjectDef() {
	static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
	return d;
}

// A custom reference type (`Class`, `KnownTypeCode::None`) -- a derived-class stub. Distinct from
// `ObjectDef` so `IsSubtypeOf(Derived, Object)` short-circuits via the Object `IsKnownType` but
// `IsSubtypeOf(Object, Derived)` does not.
std::shared_ptr<RefDef> DerivedDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
	return d;
}

// A second custom reference type (`Class`, `KnownTypeCode::None`) -- unrelated to `DerivedDef`
// (distinct instances, neither converts to the other) for the both-false non-span None case.
std::shared_ptr<RefDef> UnrelatedDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
	return d;
}

// The `System.ReadOnlySpan`1` generic definition (a struct, `KnownTypeCode::ReadOnlySpanOfT`).
std::shared_ptr<LookupTypeDefinition> ReadOnlySpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("ReadOnlySpan`1", "System",
		FullTypeName(TopLevelTypeName("System", "ReadOnlySpan`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::ReadOnlySpanOfT);
	return d;
}

// The `System.Span`1` generic definition (a struct, `KnownTypeCode::SpanOfT`).
std::shared_ptr<LookupTypeDefinition> SpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("Span`1", "System",
		FullTypeName(TopLevelTypeName("System", "Span`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::SpanOfT);
	return d;
}

// `ReadOnlySpan<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.ReadOnlySpan`1` definition). `IsKnownType(it, ReadOnlySpanOfT)` resolves via
// `GetDefinition()->KnownTypeCode`; `TypeArguments()[0]` is the element type.
ITypePtr ReadOnlySpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(ReadOnlySpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// `Span<T>` over the supplied element type (a 1-arg `ParameterizedType` over the `System.Span`1`
// definition).
ITypePtr SpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// A single-dimensional array `T[]` (the shape `IsArrayOrArrayInterfaceType` matches, returning the
// element type).
ITypePtr ArrayOf(ITypePtr element) {
	return std::make_shared<ArrayType>(std::move(element));
}

} // namespace

// ===========================================================================
// Non-span arm: `!isSpan1 && !isSpan2` -- the implicit-convertibility check
// (conversions.ImplicitConversion(p1, p2).IsValid both directions).
// ===========================================================================

// `Derived -> Object`: `ImplicitConversion(Derived, Object)` is the implicit reference widening
// (IsSubtypeOf short-circuits on the Object KnownTypeCode); `ImplicitConversion(Object, Derived)`
// is not -> `1to2 && !2to1` -> returns 1.
TEST(BetterParamsCollectionTypeTest, NonSpanImplicitConversionOneWayReturns1) {
	CSharpConversions conversions(Compilation());
	auto derived = DerivedDef();
	auto object = ObjectDef();
	EXPECT_EQ(BetterParamsCollectionType(conversions, *derived, *object), 1);
}

// `Object -> Derived`: the mirror -- `2to1` (Object->Derived is not) but `1to2` swapped: the
// reverse direction is implicit (Derived->Object) so `ImplicitConversion(Object, Derived)` is
// false and `ImplicitConversion(Derived, Object)` is true -> `!1to2 && 2to1` -> returns 2.
TEST(BetterParamsCollectionTypeTest, NonSpanImplicitConversionOneWayReturns2) {
	CSharpConversions conversions(Compilation());
	auto object = ObjectDef();
	auto derived = DerivedDef();
	EXPECT_EQ(BetterParamsCollectionType(conversions, *object, *derived), 2);
}

// Two unrelated classes: neither converts to the other -> both directions false -> returns 0.
TEST(BetterParamsCollectionTypeTest, NonSpanInconvertiblePairReturns0) {
	CSharpConversions conversions(Compilation());
	auto a = DerivedDef();
	auto b = UnrelatedDef();
	EXPECT_EQ(BetterParamsCollectionType(conversions, *a, *b), 0);
}

// Identity (same instance both ways): `ImplicitConversion(int, int)` is identity both ways -> both
// true -> neither `1to2 && !2to1` nor `!1to2 && 2to1` -> returns 0.
TEST(BetterParamsCollectionTypeTest, NonSpanIdentityPairReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *intType, *intType), 0);
}

// ===========================================================================
// Span arms: ReadOnlySpan<T> vs Span<T> (the identity-on-elements tiebreak).
// ===========================================================================

// `ReadOnlySpan<int> vs Span<int>` (same element instance): the ReadOnlySpan-vs-Span identity arm
// -> `IdentityConversion(int, int)` (same instance) is true -> returns 1 (ReadOnlySpan is better).
TEST(BetterParamsCollectionTypeTest, ReadOnlySpanBeatsSpanOnIdentityElements) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto ros = ReadOnlySpanOf(intEl);
	auto span = SpanOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *ros, *span), 1);
}

// `Span<int> vs ReadOnlySpan<int>`: the mirror arm -> returns 2 (ReadOnlySpan, the second arg, is
// better).
TEST(BetterParamsCollectionTypeTest, SpanWorseThanReadOnlySpanOnIdentityElements) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto span = SpanOf(intEl);
	auto ros = ReadOnlySpanOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *span, *ros), 2);
}

// `ReadOnlySpan<int> vs Span<long>` (distinct element instances, non-identity): the
// ReadOnlySpan-vs-Span arm fires but `IdentityConversion(int, long)` is false (distinct
// LookupTypeDefinition instances, structural-equals is identity) -> no return -> falls through to
// returns 0.
TEST(BetterParamsCollectionTypeTest, ReadOnlySpanVsSpanNonIdentityElementsReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	ITypePtr longEl = Def(KnownTypeCode::Int64);
	auto ros = ReadOnlySpanOf(intEl);
	auto span = SpanOf(longEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *ros, *span), 0);
}

// ===========================================================================
// Span arms: Span<T>/ReadOnlySpan<T> vs an array/array-interface (the identity-on-elements
// tiebreak).
// ===========================================================================

// `Span<int> vs int[]` (same element instance): `isSpan1` true, `IsArrayOrArrayInterfaceType(int[])`
// returns the element `int`, `IdentityConversion(int, int)` (same instance) true -> returns 1
// (the Span is better than the array).
TEST(BetterParamsCollectionTypeTest, SpanBeatsArrayOnIdentityElements) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto span = SpanOf(intEl);
	auto arr = ArrayOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *span, *arr), 1);
}

// `int[] vs Span<int>` (same element instance): the mirror -- `isSpan2` true,
// `IsArrayOrArrayInterfaceType(int[])` returns the element, `IdentityConversion(int, int)` true ->
// returns 2 (the Span, the second arg, is better).
TEST(BetterParamsCollectionTypeTest, ArrayWorseThanSpanOnIdentityElements) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto arr = ArrayOf(intEl);
	auto span = SpanOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *arr, *span), 2);
}

// `ReadOnlySpan<int> vs int[]` (same element instance): `isSpan1` true (ReadOnlySpan is a span),
// `IsArrayOrArrayInterfaceType(int[])` returns `int`, `IdentityConversion(int, int)` true -> returns
// 1 (the ReadOnlySpan is better than the array).
TEST(BetterParamsCollectionTypeTest, ReadOnlySpanBeatsArrayOnIdentityElements) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto ros = ReadOnlySpanOf(intEl);
	auto arr = ArrayOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *ros, *arr), 1);
}

// `Span<int> vs long[]` (distinct elements): the `isSpan1 && IsArrayOrArrayInterfaceType(long[])`
// arm fires but `IdentityConversion(int, long)` is false (distinct instances) -> falls through to
// returns 0.
TEST(BetterParamsCollectionTypeTest, SpanVsArrayNonIdentityElementsReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	ITypePtr longEl = Def(KnownTypeCode::Int64);
	auto span = SpanOf(intEl);
	auto arr = ArrayOf(longEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *span, *arr), 0);
}

// ===========================================================================
// None fall-throughs for span shapes that match no arm.
// ===========================================================================

// `Span<int> vs Span<long>` (two spans, neither ReadOnlySpan-vs-Span nor Span-vs-array): both
// `isSpan1` and `isSpan2` true, but neither ReadOnlySpan-vs-Span arm fires (both are plain Spans)
// and neither Span-vs-array arm fires (a Span is not an array/array-interface) -> returns 0.
TEST(BetterParamsCollectionTypeTest, TwoSpansOfDifferentElementsReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	ITypePtr longEl = Def(KnownTypeCode::Int64);
	auto spanInt = SpanOf(intEl);
	auto spanLong = SpanOf(longEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *spanInt, *spanLong), 0);
}

// `ReadOnlySpan<int> vs ReadOnlySpan<int>` (two ReadOnlySpans, same element instance): neither
// ReadOnlySpan-vs-Span arm fires (both are ReadOnlySpan, not Span) and neither Span-vs-array arm
// fires -> returns 0.
TEST(BetterParamsCollectionTypeTest, TwoReadOnlySpansReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	auto ros1 = ReadOnlySpanOf(intEl);
	auto ros2 = ReadOnlySpanOf(intEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *ros1, *ros2), 0);
}

// `int[] vs long[]` (two arrays, no span): `!isSpan1 && !isSpan2` true -> the non-span arm checks
// `ImplicitConversion(int[], long[])` both ways. An array converts to another array only via array
// covariance on the elements (int->long is not a reference conversion -- int/long are value types,
// not reference types, so the reference-conversion guard fails), so both directions are false ->
// returns 0.
TEST(BetterParamsCollectionTypeTest, TwoArraysOfValueElementsReturns0) {
	CSharpConversions conversions(Compilation());
	ITypePtr intEl = Def(KnownTypeCode::Int32);
	ITypePtr longEl = Def(KnownTypeCode::Int64);
	auto arrInt = ArrayOf(intEl);
	auto arrLong = ArrayOf(longEl);
	EXPECT_EQ(BetterParamsCollectionType(conversions, *arrInt, *arrLong), 0);
}
