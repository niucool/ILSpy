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

// Tests for the `OverloadResolution` "more specific formal parameter" tiebreak cluster (the C#
// `BetterFunctionMember` region, OverloadResolution.cs) ported as `Detail::` free functions:
//   * `IsArrayOrArrayInterfaceType(IType, out IType)` -- whether a type is an array or an
//     array-interface (`IEnumerable<T>`/`ICollection<T>`/`IList<T>`/`IReadOnlyCollection<T>`/
//     `IReadOnlyList<T>`), returning the element type.
//   * `MoreSpecificFormalParameter(IType, IType)` -- the recursive tiebreak: 1 if t1 is more
//     specific, 2 if t2 is, 0 if neither (C# spec 7.5.3.3 / draft-v11 12.6.4.4). A type parameter is
//     less specific than a non-type-parameter; two `ParameterizedType`s of equal arity recurse on
//     the type arguments; two `TypeWithElementType` types (Array/ByReference/Pointer/ModOpt/ModReq)
//     recurse on the element types.
//   * `MoreSpecificFormalParameters(IEnumerable<IType>, IEnumerable<IType>)` -- zips the two
//     sequences (stopping at the shorter, the C# `Zip` semantics) and reduces the per-pair verdicts.
//
// The tests pin the type-parameter-less-specific rule, the parameterized-type recursion (with the
// arity guard and the equal-type-args fallthrough), the TypeWithElementType element recursion
// (Array/Pointer/ByReference), the array/array-interface element extraction, and the zip-stops-at-
// shorter / mixed-verdict / no-decisive-pair reductions.

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::IsArrayOrArrayInterfaceType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MoreSpecificFormalParameter;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MoreSpecificFormalParameters;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode`/`TypeKind`/`TypeParameterCount` (carried by the `FullTypeName`).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind, int typeParamCount) {
	std::string name = "T" + std::to_string(static_cast<int>(ktc));
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, typeParamCount)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive element definition (`Int32`/`Int64`/...) -- a struct with the supplied `KnownTypeCode`
// so the type is observable as a distinct `IType` instance (and `GetTypeCode` resolves for callers
// that read it).
std::shared_ptr<LookupTypeDefinition> Prim(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Struct, 0);
}

// A non-generic class definition -- a plain reference type for the "neither type parameter nor
// parameterized nor TypeWithElementType" baseline.
std::shared_ptr<LookupTypeDefinition> ClassDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Class, 0);
}

// A generic class definition (`C<T>`) with `TypeParameterCount == 1` -- the shape `C<int>`/`C<T>`
// take in the C# source.
std::shared_ptr<LookupTypeDefinition> GenericDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Class, 1);
}

// A generic interface definition (`IList<T>`/...) with the supplied `KnownTypeCode` and
// `TypeParameterCount == 1` -- the shape `IsArrayInterfaceType` switches on.
std::shared_ptr<LookupTypeDefinition> InterfaceDef(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Interface, 1);
}

// A `ParameterizedType` over the supplied generic definition with a single type argument.
ITypePtr ParameterizedOver(std::shared_ptr<LookupTypeDefinition> genericDef, ITypePtr arg) {
	return std::make_shared<ParameterizedType>(std::move(genericDef),
		std::vector<ITypePtr>{std::move(arg)});
}

// A type parameter stub named "T".
ITypePtr TParam() {
	return std::make_shared<LookupTypeParameter>("T");
}

} // namespace

// ===========================================================================
// IsArrayOrArrayInterfaceType (OverloadResolution.cs)
// ===========================================================================

// A single-dimensional array `int[]` -> true, element is `int`.
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeArrayReturnsElement) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr arr = std::make_shared<ArrayType>(intEl);
	const IType* elementType = nullptr;
	EXPECT_TRUE(IsArrayOrArrayInterfaceType(*arr, elementType));
	EXPECT_EQ(elementType, intEl.get());
}

// A multi-dimensional array `int[*,*]` is still an `ArrayType` -> true, element is `int`.
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeMultiDimArrayReturnsElement) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr arr = std::make_shared<ArrayType>(intEl, 2);
	const IType* elementType = nullptr;
	EXPECT_TRUE(IsArrayOrArrayInterfaceType(*arr, elementType));
	EXPECT_EQ(elementType, intEl.get());
}

// An array-interface `IList<int>` -> true, element is `int` (the single type argument).
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeIListOfTReturnsTypeArgument) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ilist = ParameterizedOver(InterfaceDef(KnownTypeCode::IListOfT), intEl);
	const IType* elementType = nullptr;
	EXPECT_TRUE(IsArrayOrArrayInterfaceType(*ilist, elementType));
	EXPECT_EQ(elementType, intEl.get());
}

// `IEnumerable<int>` -> true, element is `int`.
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeIEnumerableOfTReturnsTypeArgument) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ienumerable = ParameterizedOver(InterfaceDef(KnownTypeCode::IEnumerableOfT), intEl);
	const IType* elementType = nullptr;
	EXPECT_TRUE(IsArrayOrArrayInterfaceType(*ienumerable, elementType));
	EXPECT_EQ(elementType, intEl.get());
}

// A non-array, non-array-interface type (a plain class) -> false, element null.
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeFalseForPlainClass) {
	ITypePtr cls = ClassDef();
	const IType* elementType = reinterpret_cast<const IType*>(0x1);  // sentinel: must be reset
	EXPECT_FALSE(IsArrayOrArrayInterfaceType(*cls, elementType));
	EXPECT_EQ(elementType, nullptr);
}

// A `ParameterizedType` over a non-array-interface generic (`C<int>`, `KnownTypeCode::None`) ->
// false (it is not an `ArrayType` and `IsArrayInterfaceType` is false).
TEST(MoreSpecificFormalParameterTest, IsArrayOrArrayInterfaceTypeFalseForNonArrayInterfaceParameterized) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr cOfInt = ParameterizedOver(GenericDef(), intEl);
	const IType* elementType = nullptr;
	EXPECT_FALSE(IsArrayOrArrayInterfaceType(*cOfInt, elementType));
	EXPECT_EQ(elementType, nullptr);
}

// ===========================================================================
// MoreSpecificFormalParameter (OverloadResolution.cs)
// ===========================================================================

// A type parameter is LESS specific than a non-type-parameter, so the non-type-parameter side
// (t2) wins -> 2.
TEST(MoreSpecificFormalParameterTest, TypeParameterLosesToNonTypeParameter) {
	ITypePtr t = TParam();
	ITypePtr cls = ClassDef();
	EXPECT_EQ(MoreSpecificFormalParameter(*t, *cls), 2);
}

// The mirror: non-type-parameter (t1) vs type parameter (t2) -> 1.
TEST(MoreSpecificFormalParameterTest, NonTypeParameterBeatsTypeParameter) {
	ITypePtr cls = ClassDef();
	ITypePtr t = TParam();
	EXPECT_EQ(MoreSpecificFormalParameter(*cls, *t), 1);
}

// Two type parameters -> neither is more specific -> 0.
TEST(MoreSpecificFormalParameterTest, TwoTypeParametersAreEquallySpecific) {
	ITypePtr t1 = TParam();
	ITypePtr t2 = TParam();
	EXPECT_EQ(MoreSpecificFormalParameter(*t1, *t2), 0);
}

// Two equal non-type-parameter, non-parameterized, non-TypeWithElementType types -> 0.
TEST(MoreSpecificFormalParameterTest, TwoPlainClassesAreEquallySpecific) {
	ITypePtr c1 = ClassDef();
	ITypePtr c2 = ClassDef();
	EXPECT_EQ(MoreSpecificFormalParameter(*c1, *c2), 0);
}

// `C<int>` vs `C<T>`: same arity, recurse on the type args; `int` beats `T` -> 1.
TEST(MoreSpecificFormalParameterTest, ParameterizedSameArityIntBeatsTypeParameter) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr cOfInt = ParameterizedOver(GenericDef(), intEl);
	ITypePtr cOfT = ParameterizedOver(GenericDef(), tEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*cOfInt, *cOfT), 1);
}

// `C<T>` vs `C<int>`: recurse; `T` loses to `int` -> 2.
TEST(MoreSpecificFormalParameterTest, ParameterizedSameArityTypeParameterLosesToInt) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr cOfT = ParameterizedOver(GenericDef(), tEl);
	ITypePtr cOfInt = ParameterizedOver(GenericDef(), intEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*cOfT, *cOfInt), 2);
}

// `C<int>` vs `C<long>`: recurse; `int` vs `long` -> neither more specific -> 0 (the parameterized
// recursion yields 0 and the TypeWithElementType fall-through is a no-op for a class).
TEST(MoreSpecificFormalParameterTest, ParameterizedSameArityTwoPrimitivesEquallySpecific) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr cOfInt = ParameterizedOver(GenericDef(), intEl);
	ITypePtr cOfLong = ParameterizedOver(GenericDef(), longEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*cOfInt, *cOfLong), 0);
}

// Different arity (`C<int>` tpc 1 vs `D<int,int>` tpc 2) -> the arity guard skips the recursion;
// neither is a `TypeWithElementType` -> 0.
TEST(MoreSpecificFormalParameterTest, ParameterizedDifferentArityIsEquallySpecific) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto def1 = MakeDef(KnownTypeCode::None, TypeKind::Class, 1);
	auto def2 = MakeDef(KnownTypeCode::None, TypeKind::Class, 2);
	ITypePtr cOfInt = std::make_shared<ParameterizedType>(def1, std::vector<ITypePtr>{intEl});
	ITypePtr dOfIntInt = std::make_shared<ParameterizedType>(def2,
		std::vector<ITypePtr>{intEl, intEl});
	EXPECT_EQ(MoreSpecificFormalParameter(*cOfInt, *dOfIntInt), 0);
}

// `int*` vs `T*` (two `PointerType`s) -> recurse on the elements; `int` beats `T` -> 1.
TEST(MoreSpecificFormalParameterTest, PointerTypeIntBeatsTypeParameter) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr intPtr = std::make_shared<PointerType>(intEl);
	ITypePtr tPtr = std::make_shared<PointerType>(tEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*intPtr, *tPtr), 1);
}

// `T*` vs `int*` -> recurse; `T` loses to `int` -> 2.
TEST(MoreSpecificFormalParameterTest, PointerTypeTypeParameterLosesToInt) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr tPtr = std::make_shared<PointerType>(tEl);
	ITypePtr intPtr = std::make_shared<PointerType>(intEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*tPtr, *intPtr), 2);
}

// `int*` vs `long*` -> recurse on elements; `int` vs `long` -> 0.
TEST(MoreSpecificFormalParameterTest, PointerTypeTwoPrimitivesEquallySpecific) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr intPtr = std::make_shared<PointerType>(intEl);
	ITypePtr longPtr = std::make_shared<PointerType>(longEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*intPtr, *longPtr), 0);
}

// `int[]` vs `T[]` (two `ArrayType`s) -> recurse on the elements; `int` beats `T` -> 1.
TEST(MoreSpecificFormalParameterTest, ArrayTypeIntBeatsTypeParameter) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr intArr = std::make_shared<ArrayType>(intEl);
	ITypePtr tArr = std::make_shared<ArrayType>(tEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*intArr, *tArr), 1);
}

// `ref int` vs `ref T` (two `ByReferenceType`s) -> recurse on the elements; `int` beats `T` -> 1.
TEST(MoreSpecificFormalParameterTest, ByReferenceTypeIntBeatsTypeParameter) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	ITypePtr refInt = std::make_shared<ByReferenceType>(intEl);
	ITypePtr refT = std::make_shared<ByReferenceType>(tEl);
	EXPECT_EQ(MoreSpecificFormalParameter(*refInt, *refT), 1);
}

// One `TypeWithElementType` (`int*`) and one not (a plain class) -> 0 (the `tew1 != null &&
// tew2 != null` guard fails).
TEST(MoreSpecificFormalParameterTest, OneTypeWithElementTypeOneNotIsEquallySpecific) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr intPtr = std::make_shared<PointerType>(intEl);
	ITypePtr cls = ClassDef();
	EXPECT_EQ(MoreSpecificFormalParameter(*intPtr, *cls), 0);
	EXPECT_EQ(MoreSpecificFormalParameter(*cls, *intPtr), 0);
}

// ===========================================================================
// MoreSpecificFormalParameters (OverloadResolution.cs) -- the IEnumerable<IType> overload
// ===========================================================================

// Empty sequences -> 0.
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersEmptySequences) {
	std::vector<const IType*> v1;
	std::vector<const IType*> v2;
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 0);
}

// `[int]` vs `[T]` -> 1 (the single pair favours t1).
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersSinglePairIntBeatsT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	std::vector<const IType*> v1{intEl.get()};
	std::vector<const IType*> v2{tEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 1);
}

// `[T]` vs `[int]` -> 2.
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersSinglePairTLosesToInt) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	std::vector<const IType*> v1{tEl.get()};
	std::vector<const IType*> v2{intEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 2);
}

// `[int, int]` vs `[int, int]` -> 0 (no decisive pair).
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersNoDecisivePair) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	std::vector<const IType*> v1{intEl.get(), intEl.get()};
	std::vector<const IType*> v2{intEl.get(), intEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 0);
}

// Mixed: `[int, T]` vs `[T, int]` -> first pair favours t1 (`int` beats `T`), second pair favours
// t2 (`T` loses to `int`) -> mixed -> 0.
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersMixedVerdictIsZero) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr tEl = TParam();
	std::vector<const IType*> v1{intEl.get(), tEl.get()};
	std::vector<const IType*> v2{tEl.get(), intEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 0);
}

// Zip stops at the shorter: `[int, long, T]` vs `[T]` -> only the first pair is considered
// (`int` beats `T`) -> 1.
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersZipStopsAtShorter) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr tEl = TParam();
	std::vector<const IType*> v1{intEl.get(), longEl.get(), tEl.get()};
	std::vector<const IType*> v2{tEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 1);
}

// `[int, long]` vs `[T, T]` -> both pairs favour t1 -> 1.
TEST(MoreSpecificFormalParameterTest, MoreSpecificFormalParametersBothPairsFavourFirst) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr longEl = Prim(KnownTypeCode::Int64);
	ITypePtr tEl = TParam();
	std::vector<const IType*> v1{intEl.get(), longEl.get()};
	std::vector<const IType*> v2{tEl.get(), tEl.get()};
	EXPECT_EQ(MoreSpecificFormalParameters(v1, v2), 1);
}
