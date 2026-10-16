// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation, the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Detail::IsImplicitSpanConversion` (CSharpConversions.cs line 1238, the C# 14.0
// first-class-span-types proposal). The helper is gated on
// `compilation.TypeSystemOptions.HasFlag(TypeSystemOptions.FirstClassSpanTypes)` and permits
// conversions between single-dimensional arrays, `System.Span<T>`, `System.ReadOnlySpan<T>`,
// and `string`. The tests cover the flag gate, the array arm (to `Span<T>` by identity, to
// `ReadOnlySpan<T>` by identity OR implicit reference covariance), the `Span`/`ReadOnlySpan`
// source arm (to `ReadOnlySpan<T>` by identity OR implicit reference covariance), the `string`
// arm (to `ReadOnlySpan<char>` only), and the default fallthrough.
//
// Test-stub conventions (the D523-D537 precedent, reused from CSharpConversionsBetterConversion_Test):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `IsKnownType` reads
//    the type's own `GetDefinition()->KnownTypeCode`, so a `Def(String)` / `Def(Char)` /
//    `Def(Int32)` resolves. A `KnownType` placeholder is NOT an `ITypeDefinition`
//    (`GetDefinition() == nullptr`), so `IsKnownType` returns false for it -- the `String`
//    fromType and the `ReadOnlySpan<char>` element MUST be `LookupTypeDefinition`s, NOT `KnownType`s.
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default
//    is `std::nullopt`, which fails the reference-conversion guard). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsImplicitReferenceConversion(Derived, Object)` fire via the
//    `IsSubtypeOf` `IsKnownType(Object)` short-circuit; a `RefDef(None)` is a custom class.
//  * `LookupTypeDefinition::StructuralEquals` is identity (`this == &other`), so the element-
//    identity crux cases MUST reuse the SAME `ITypePtr` instance for the array element and the
//    span type argument; the reference-conversion crux cases use DISTINCT `RefDef` instances
//    (Derived vs Object).
//  * `SpanCompilation` is a `LookupCompilation` subclass overriding `TypeSystemOptions()` to
//    return `FirstClassSpanTypes` (the base returns `None`, which fails the flag gate); the
//    flag-gate test uses the plain `LookupCompilation` (no flag) to pin the gate.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsImplicitSpanConversion
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // ArrayType, ParameterizedType, ITypePtr
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace R = ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::IsImplicitSpanConversion;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a
// reference type -- the faithful value the reference-conversion guard needs; the base
// `LookupTypeDefinition` default is `std::nullopt`, which fails the guard). Inherits the
// `LookupTypeDefinition` ctor; the only override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupCompilation` whose `TypeSystemOptions()` carries `FirstClassSpanTypes` (the base
// returns `None`, which fails the flag gate). The flag is a single bit, so the override returns
// it alone (the minimal mask that satisfies `HasFlag(FirstClassSpanTypes)`). The member
// function `TypeSystemOptions()` shares its name with the `TypeSystemOptions` enum type (the C#
// idiom the D472 port hit); the member hides the enum in the class body, so the return type and
// the body are fully-qualified with `TS::` (no `using`-declaration for the enum here).
class SpanCompilation : public LookupCompilation {
public:
	TS::TypeSystemOptions TypeSystemOptions() const override {
		return TS::TypeSystemOptions::FirstClassSpanTypes;
	}
};

SpanCompilation& Compilation() {
	static SpanCompilation c;
	return c;
}

// A plain `LookupCompilation` (no flags) for the flag-gate test -- `TypeSystemOptions()` returns
// `None`, so `HasFlag(FirstClassSpanTypes)` is false.
LookupCompilation& NoFlagCompilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `IsKnownType` resolves the `KnownTypeCode`
// via the type's own definition. Used for the integral primitives and the `Char`/`String` types
// the span arms read (the D514 `MakeDef` precedent).
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

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object` makes
// `IsImplicitReferenceConversion(Derived, Object)` fire via the `IsSubtypeOf` short-circuit; a
// `KnownTypeCode::None` is a custom reference type (the derived-class stub).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
	int n = static_cast<int>(ktc);
	std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
	return std::make_shared<RefDef>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

std::shared_ptr<RefDef> ObjectDef() {
	static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
	return d;
}

std::shared_ptr<RefDef> DerivedDef() {
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

// `Span<T>` over the supplied element type.
ITypePtr SpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// A single-dimensional array (`ArrayType` SZArray ctor, rank 1) over the supplied element type.
ITypePtr Array1dOf(ITypePtr element) {
	return std::make_shared<ArrayType>(std::move(element));
}

// A multi-dimensional array (`ArrayType` rank-2 ctor) over the supplied element type -- the
// `Dimensions: 1` pattern does NOT match (rank != 1).
ITypePtr Array2dOf(ITypePtr element) {
	return std::make_shared<ArrayType>(std::move(element), 2);
}

} // namespace

// ===========================================================================
// Flag gate (CSharpConversions.cs line 1240): the conversion does not exist unless the type
// system materializes first-class spans.
// ===========================================================================

TEST(CSharpConversionsSpanConversionTest, FlagGateReturnsFalseWhenFirstClassSpanTypesNotSet) {
	// A valid span-conversion shape (1-D int array -> Span<int>, element identity) returns false
	// when the type system does NOT carry FirstClassSpanTypes -- the gate short-circuits before
	// the arms. The NoFlagCompilation returns TypeSystemOptions::None.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr arr = Array1dOf(intType);
	ITypePtr span = SpanOf(intType);
	EXPECT_FALSE(IsImplicitSpanConversion(NoFlagCompilation(), *arr, *span));
}

// ===========================================================================
// ArrayType arm (CSharpConversions.cs line 1259): a single-dimensional array (Dimensions == 1).
// ===========================================================================

TEST(CSharpConversionsSpanConversionTest, ArrayToSpanWithIdentityElementReturnsTrue) {
	// `int[] -> Span<int>`: the array element and the span type argument are the SAME instance,
	// so IdentityConversion(int, int) is true (StructuralEquals is identity). The SpanOfT arm
	// returns true.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr arr = Array1dOf(intType);
	ITypePtr span = SpanOf(intType);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *arr, *span));
}

TEST(CSharpConversionsSpanConversionTest, ArrayToSpanWithNonIdentityElementReturnsFalse) {
	// `int[] -> Span<long>`: distinct int/long instances, IdentityConversion(int, long) is false.
	// The SpanOfT arm checks identity only (no reference conversion), so it returns false.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr arr = Array1dOf(intType);
	ITypePtr span = SpanOf(longType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *arr, *span));
}

TEST(CSharpConversionsSpanConversionTest, ArrayToReadOnlySpanWithIdentityElementReturnsTrue) {
	// `int[] -> ReadOnlySpan<int>`: element identity; the ReadOnlySpanOfT arm's first check
	// (IdentityConversion) is true, so it returns true.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr arr = Array1dOf(intType);
	ITypePtr ros = ReadOnlySpanOf(intType);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *arr, *ros));
}

TEST(CSharpConversionsSpanConversionTest, ArrayToReadOnlySpanWithReferenceConvertibleElementReturnsTrue) {
	// `Derived[] -> ReadOnlySpan<Object>`: identity is false (distinct RefDef instances), but
	// IsImplicitReferenceConversion(Derived, Object) is true (the IsSubtypeOf Object short-circuit),
	// so the ReadOnlySpanOfT arm returns true via the covariance fallback.
	auto derived = DerivedDef();
	auto object = ObjectDef();
	ITypePtr arr = Array1dOf(derived);
	ITypePtr ros = ReadOnlySpanOf(object);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *arr, *ros));
}

TEST(CSharpConversionsSpanConversionTest, ArrayToReadOnlySpanWithInconvertibleElementReturnsFalse) {
	// `int[] -> ReadOnlySpan<long>`: identity is false (distinct int/long) AND
	// IsImplicitReferenceConversion(int, long) is false (int/long are value types, not reference
	// types -- the reference guard fails), so the ReadOnlySpanOfT arm returns false.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr arr = Array1dOf(intType);
	ITypePtr ros = ReadOnlySpanOf(longType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *arr, *ros));
}

TEST(CSharpConversionsSpanConversionTest, ArrayToNonSpanTargetFallsThroughToFalse) {
	// `int[] -> SomeClass`: the toType is neither SpanOfT nor ReadOnlySpanOfT, so the array arm
	// breaks and falls through to the final `return false`.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr arr = Array1dOf(intType);
	auto someClass = DerivedDef();
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *arr, *someClass));
}

TEST(CSharpConversionsSpanConversionTest, MultiDimensionalArrayDoesNotMatchRankOnePattern) {
	// `int[2] -> Span<int>`: a rank-2 array does NOT match the `Dimensions: 1` pattern, so the
	// array arm is skipped and the conversion returns false (a multi-dim array is not a span
	// source).
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr arr2d = Array2dOf(intType);
	ITypePtr span = SpanOf(intType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *arr2d, *span));
}

// ===========================================================================
// ParameterizedType arm (CSharpConversions.cs line 1270): `Span<T>` / `ReadOnlySpan<T>` source.
// ===========================================================================

TEST(CSharpConversionsSpanConversionTest, SpanToReadOnlySpanWithIdentityElementReturnsTrue) {
	// `Span<int> -> ReadOnlySpan<int>`: element identity; the ReadOnlySpanOfT arm returns true.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr span = SpanOf(intType);
	ITypePtr ros = ReadOnlySpanOf(intType);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *span, *ros));
}

TEST(CSharpConversionsSpanConversionTest, SpanToReadOnlySpanWithReferenceConvertibleElementReturnsTrue) {
	// `Span<Derived> -> ReadOnlySpan<Object>`: identity is false, IsImplicitReferenceConversion(
	// Derived, Object) is true (the covariance fallback), so the ReadOnlySpanOfT arm returns true.
	auto derived = DerivedDef();
	auto object = ObjectDef();
	ITypePtr span = SpanOf(derived);
	ITypePtr ros = ReadOnlySpanOf(object);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *span, *ros));
}

TEST(CSharpConversionsSpanConversionTest, ReadOnlySpanToReadOnlySpanWithIdentityElementReturnsTrue) {
	// `ReadOnlySpan<int> -> ReadOnlySpan<int>`: the source arm accepts a ReadOnlySpan source,
	// element identity returns true.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr rosFrom = ReadOnlySpanOf(intType);
	ITypePtr rosTo = ReadOnlySpanOf(intType);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *rosFrom, *rosTo));
}

TEST(CSharpConversionsSpanConversionTest, SpanToSpanFallsThroughToFalse) {
	// `Span<int> -> Span<int>`: the toType is SpanOfT, NOT ReadOnlySpanOfT, so the source arm
	// breaks and falls through to the final `return false` (Span -> Span is not a span
	// conversion).
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr spanFrom = SpanOf(intType);
	ITypePtr spanTo = SpanOf(intType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *spanFrom, *spanTo));
}

TEST(CSharpConversionsSpanConversionTest, SpanToReadOnlySpanWithInconvertibleElementReturnsFalse) {
	// `Span<int> -> ReadOnlySpan<long>`: identity is false AND IsImplicitReferenceConversion(int,
	// long) is false (value types), so the ReadOnlySpanOfT arm returns false.
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr span = SpanOf(intType);
	ITypePtr ros = ReadOnlySpanOf(longType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *span, *ros));
}

// ===========================================================================
// String arm (CSharpConversions.cs line 1276): `string` -> `ReadOnlySpan<char>` only.
// ===========================================================================

TEST(CSharpConversionsSpanConversionTest, StringToReadOnlySpanCharReturnsTrue) {
	// `string -> ReadOnlySpan<char>`: the toType is ReadOnlySpanOfT and its element is Char
	// (a Def(Char) LookupTypeDefinition so IsKnownType(Char) resolves), so the String arm returns
	// true.
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr charType = Def(KnownTypeCode::Char);
	ITypePtr ros = ReadOnlySpanOf(charType);
	EXPECT_TRUE(IsImplicitSpanConversion(Compilation(), *stringType, *ros));
}

TEST(CSharpConversionsSpanConversionTest, StringToReadOnlySpanNonCharReturnsFalse) {
	// `string -> ReadOnlySpan<int>`: the toType is ReadOnlySpanOfT but its element is int, NOT
	// Char, so the String arm returns false.
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr ros = ReadOnlySpanOf(intType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *stringType, *ros));
}

TEST(CSharpConversionsSpanConversionTest, StringToSpanCharReturnsFalse) {
	// `string -> Span<char>`: the toType is SpanOfT, NOT ReadOnlySpanOfT, so the String arm
	// returns false (only ReadOnlySpan is a string span target).
	ITypePtr stringType = Def(KnownTypeCode::String, TypeKind::Class);
	ITypePtr charType = Def(KnownTypeCode::Char);
	ITypePtr span = SpanOf(charType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *stringType, *span));
}

// ===========================================================================
// Default arm (CSharpConversions.cs line 1278): no pattern matched.
// ===========================================================================

TEST(CSharpConversionsSpanConversionTest, NonSpanSourceToReadOnlySpanReturnsFalse) {
	// A non-array, non-span, non-string source (a plain class RefDef) to a ReadOnlySpan target
	// falls through every arm to the final `return false`.
	auto someClass = DerivedDef();
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr ros = ReadOnlySpanOf(intType);
	EXPECT_FALSE(IsImplicitSpanConversion(Compilation(), *someClass, *ros));
}
