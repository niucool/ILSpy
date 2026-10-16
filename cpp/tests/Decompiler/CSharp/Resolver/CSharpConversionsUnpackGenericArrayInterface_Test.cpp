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

// Tests for `Detail::UnpackGenericArrayInterface` (CSharpConversions.cs line 586) -- the pure
// helper the (deferred) `ImplicitReferenceConversion` / `ExplicitReferenceConversion` arms use
// to unpack a single-dimensional array's generic-interface base. For `IList<T>` /
// `ICollection<T>` / `IEnumerable<T>` / `IReadOnlyList<T>` / `IReadOnlyCollection<T>` it returns
// the type argument `T`; for any other type (a non-array-interface parameterized type, a plain
// non-parameterized type, a `ParameterizedType` whose generic has no definition) it returns
// null. It is pure (no `CSharpConversions` instance state), so it lands as a `Detail::` free
// function taking `const IType&` like the numeric helpers.
//
// CRUX STUB CONVENTIONS (carried from the D514/D515 tests):
//  * A `ParameterizedType`'s `GetDefinition()` delegates to its generic type's `GetDefinition()`.
//    A `LookupTypeDefinition` IS-A `ITypeDefinition` with `GetDefinition() == this`, so a
//    `ParameterizedType` over a `LookupTypeDefinition` resolves the definition (and its
//    `KnownTypeCode`); a `ParameterizedType` over a `KnownType` (NOT an `ITypeDefinition`,
//    `GetDefinition() == nullptr`) resolves no definition -- the `?.` guard arm.
//  * The returned `const IType*` is a non-owning raw pointer to the managed `IType` in the
//    `ParameterizedType`'s `typeArgs_`; the tests assert pointer-identity against the element
//    instance passed in (`result == intEl.get()`), pinning that `GetTypeArgument(0)` returns
//    the stored element (not a copy / not a fresh instance).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::UnpackGenericArrayInterface;
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
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind`. The `FullTypeName` carries the
// `TypeParameterCount` (1 for the generic interface definitions, 0 for a non-generic stub).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind, int typeParamCount) {
	std::string name = "T" + std::to_string(static_cast<int>(ktc));
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, typeParamCount)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A generic interface definition (`IList<T>` / `ICollection<T>` / ...) with the supplied
// `KnownTypeCode` and `TypeKind::Interface`, `TypeParameterCount == 1`. The `KnownTypeCode` is
// what `UnpackGenericArrayInterface` switches on (the definition's kind is not read).
std::shared_ptr<LookupTypeDefinition> InterfaceDef(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Interface, 1);
}

// A primitive element definition (`Int32` / `String` / ...) -- a struct with the supplied
// `KnownTypeCode` so the returned type argument is observable as a distinct `IType` instance.
std::shared_ptr<LookupTypeDefinition> Prim(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Struct, 0);
}

// A `ParameterizedType` over the supplied generic definition with the supplied single type
// argument -- the shape `IList<int>` / `List<int>` / `Nullable<int>` take in the C# source.
ITypePtr ParameterizedOver(std::shared_ptr<LookupTypeDefinition> genericDef, ITypePtr arg) {
	return std::make_shared<ParameterizedType>(std::move(genericDef),
		std::vector<ITypePtr>{std::move(arg)});
}

} // namespace

// ===========================================================================
// UnpackGenericArrayInterface (CSharpConversions.cs line 586) -- for the five generic
// collection interfaces, returns the type argument `T`; otherwise null.
// ===========================================================================

// IList<int> -> int: the type argument is returned (pointer-identity against the int instance).
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, UnpacksIListOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ilist = ParameterizedOver(InterfaceDef(KnownTypeCode::IListOfT), intEl);
	const IType* result = UnpackGenericArrayInterface(*ilist);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, intEl.get());
}

// ICollection<int> -> int.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, UnpacksICollectionOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr icollection = ParameterizedOver(InterfaceDef(KnownTypeCode::ICollectionOfT), intEl);
	const IType* result = UnpackGenericArrayInterface(*icollection);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, intEl.get());
}

// IEnumerable<int> -> int.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, UnpacksIEnumerableOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ienumerable = ParameterizedOver(InterfaceDef(KnownTypeCode::IEnumerableOfT), intEl);
	const IType* result = UnpackGenericArrayInterface(*ienumerable);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, intEl.get());
}

// IReadOnlyList<int> -> int.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, UnpacksIReadOnlyListOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ireadonlylist = ParameterizedOver(InterfaceDef(KnownTypeCode::IReadOnlyListOfT), intEl);
	const IType* result = UnpackGenericArrayInterface(*ireadonlylist);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, intEl.get());
}

// IReadOnlyCollection<int> -> int.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, UnpacksIReadOnlyCollectionOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ireadonlycollection = ParameterizedOver(InterfaceDef(KnownTypeCode::IReadOnlyCollectionOfT), intEl);
	const IType* result = UnpackGenericArrayInterface(*ireadonlycollection);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, intEl.get());
}

// IList<string> -> string: the helper returns the ACTUAL type argument, not a fixed type. Pins
// that the return is `pt.GetTypeArgument(0)` (the stored element), not a constant.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, ReturnsTheActualTypeArgument) {
	ITypePtr stringEl = Prim(KnownTypeCode::String);
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	ITypePtr ilistOfString = ParameterizedOver(InterfaceDef(KnownTypeCode::IListOfT), stringEl);
	const IType* result = UnpackGenericArrayInterface(*ilistOfString);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result, stringEl.get());
	EXPECT_NE(result, intEl.get());
}

// A non-array-interface generic (List<T> with KnownTypeCode::None): the switch matches no case,
// so the helper returns null. Pins that only the five collection-interface codes unpack.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, NullForNonArrayInterfaceGeneric) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto listDef = MakeDef(KnownTypeCode::None, TypeKind::Class, 1);
	ITypePtr listOfInt = ParameterizedOver(listDef, intEl);
	const IType* result = UnpackGenericArrayInterface(*listOfInt);
	EXPECT_EQ(result, nullptr);
}

// Nullable<int> (KnownTypeCode::NullableOfT): a parameterized generic, but NOT one of the five
// array-interface codes -> null. Pins that `Nullable<T>` is not mistaken for an array interface.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, NullForNullableOfT) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	auto nullableDef = MakeDef(KnownTypeCode::NullableOfT, TypeKind::Struct, 1);
	ITypePtr nullableOfInt = ParameterizedOver(nullableDef, intEl);
	const IType* result = UnpackGenericArrayInterface(*nullableOfInt);
	EXPECT_EQ(result, nullptr);
}

// A plain non-parameterized type (int): the `is ParameterizedType` test fails, so the helper
// returns null without reaching the definition switch.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, NullForNonParameterizedType) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	const IType* result = UnpackGenericArrayInterface(*intEl);
	EXPECT_EQ(result, nullptr);
}

// A `ParameterizedType` whose generic has NO definition (`KnownType` is NOT an
// `ITypeDefinition`; `GetDefinition()` returns nullptr). The C# `pt.GetDefinition()?.KnownTypeCode`
// skips the switch when `GetDefinition()` is null; the port's `def != nullptr` guard mirrors that.
TEST(CSharpConversionsUnpackGenericArrayInterfaceTest, NullWhenDefinitionIsNull) {
	ITypePtr intEl = Prim(KnownTypeCode::Int32);
	// A `KnownType` carries a `KnownTypeCode` but is NOT an `ITypeDefinition` -- its
	// `GetDefinition()` returns nullptr, so a `ParameterizedType` over it resolves no definition.
	ITypePtr genericWithNoDef = std::make_shared<KnownType>(KnownTypeCode::IListOfT);
	ITypePtr parameterizedOverKnown = std::make_shared<ParameterizedType>(
		genericWithNoDef, std::vector<ITypePtr>{intEl});
	const IType* result = UnpackGenericArrayInterface(*parameterizedOverKnown);
	EXPECT_EQ(result, nullptr);
}
