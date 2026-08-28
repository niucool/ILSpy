// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. CAUSED IN ON THE WHICHEVER THEORY OF LIABILITY, WHETHER IN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `GetTupleElementTypes` (TupleType.cs line 168) -- the static helper that flattens a
// C# 7 tuple type (a `TypeKind::Tuple` `TupleType` or an underlying `System.ValueTuple<...>`
// parameterized type) into its element types, threading the 8-ary
// `ValueTuple<T1..T7,TRest>` nesting through the `Rest` (the 8th type argument). It is the
// prerequisite the `CSharpConversions.TupleConversion` conversion consumes; it lands here as a
// tested-but-not-yet-wired foundation (the D63 LongSet / D66 LoopContext / D68 NullableLifting /
// D533 GetDelegateInvokeMethod precedent: port the helper the next in-order transform needs
// ahead of the transform, so the transform port is a separate lower-risk iteration).
//
// The faithful port returns `std::optional<std::vector<ITypePtr>>`: `std::nullopt` is the C#
// `default(ImmutableArray<IType>)` (the `IsDefault` sentinel, not-a-tuple); a populated vector
// is the element types. The tests construct the two input shapes the helper recognises:
//   * a `TupleType` (Kind::Tuple) -- the port's `TupleType` class (IType.hpp), carrying
//     `ElementTypes`;
//   * an instantiated `System.ValueTuple<...>` `ParameterizedType` over a `LookupTypeDefinition`
//     stub (Namespace="System", Name="ValueTuple", Kind=Struct) -- the underlying representation
//     a real C# tuple type resolves to. A `ParameterizedType` over a `SimpleType` (no definition,
//     `GetDefinition()`=null) is the degenerate "no resolvable definition" shape the namespace
//     check rejects.

#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetTupleElementTypes;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr ObjectType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

// The `System.ValueTuple`1`..`ValueTuple`8` generic definition stub: a `LookupTypeDefinition`
// (IS-A `ITypeDefinition`, `GetDefinition()` returns `this`) with `Namespace`="System",
// `Name`="ValueTuple", `Kind`=Struct, so `GetTupleElementTypes` recognises an instantiated
// `ParameterizedType` over it. The `FullTypeName` carries the arity (the real `ValueTuple`8`
// arity is 8), though the `ParameterizedType::TypeParameterCount()` override returns the
// instantiation's argument count, not the definition's arity.
std::shared_ptr<LookupTypeDefinition> ValueTupleDef(int arity, TypeKind kind = TypeKind::Struct) {
	return std::make_shared<LookupTypeDefinition>(
		"ValueTuple", "System",
		FullTypeName(TopLevelTypeName("System", "ValueTuple", arity)),
		kind, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// An instantiated `System.ValueTuple<...>` over the definition stub with the given element
// type arguments. `Kind()` delegates to the definition's `Kind()` (Struct), `Name()` to
// "ValueTuple", `GetDefinition()` to the definition (Namespace="System").
ITypePtr ValueTupleOf(std::shared_ptr<LookupTypeDefinition> def, std::vector<ITypePtr> args) {
	return std::make_shared<ParameterizedType>(std::move(def), std::move(args));
}

// A `TupleType` (Kind::Tuple) wrapping an underlying `ValueTuple<...>` and the element types.
TupleType MakeTuple(ITypePtr underlying, std::vector<ITypePtr> elements) {
	return TupleType(std::move(underlying), std::move(elements), std::vector<std::string>{});
}

}  // namespace

// ---------------------------------------------------------------------------
// A `TupleType` (Kind::Tuple) -> its `ElementTypes`, by pointer identity.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, TupleTypeReturnsItsElementTypes) {
	ITypePtr e0 = Int32Type();
	ITypePtr e1 = StringType();
	auto def = ValueTupleDef(2);
	ITypePtr underlying = ValueTupleOf(def, {e0, e1});
	TupleType tuple = MakeTuple(underlying, {e0, e1});
	auto result = GetTupleElementTypes(tuple);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 2u);
	EXPECT_EQ((*result)[0].get(), e0.get());
	EXPECT_EQ((*result)[1].get(), e1.get());
}

// ---------------------------------------------------------------------------
// A `ValueTuple<int, string>` (ParameterizedType, tpc=2, in 1..7) -> [int, string].
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, ValueTupleTwoArgsReturnsBoth) {
	ITypePtr e0 = Int32Type();
	ITypePtr e1 = StringType();
	auto def = ValueTupleDef(2);
	ITypePtr vt = ValueTupleOf(def, {e0, e1});
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 2u);
	EXPECT_EQ((*result)[0].get(), e0.get());
	EXPECT_EQ((*result)[1].get(), e1.get());
}

// ---------------------------------------------------------------------------
// A `ValueTuple<int>` (tpc=1, the 1-element tuple boundary) -> [int].
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, ValueTupleOneArgReturnsSingle) {
	ITypePtr e0 = Int32Type();
	auto def = ValueTupleDef(1);
	ITypePtr vt = ValueTupleOf(def, {e0});
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 1u);
	EXPECT_EQ((*result)[0].get(), e0.get());
}

// ---------------------------------------------------------------------------
// A 7-element `ValueTuple<...>` (tpc=7, the upper boundary of the direct-args arm) -> 7
// elements.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, ValueTupleSevenArgsReturnsAllSeven) {
	auto def = ValueTupleDef(7);
	std::vector<ITypePtr> args;
	for (int i = 0; i < 7; i++) args.push_back(Int32Type());
	ITypePtr vt = ValueTupleOf(def, args);
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 7u);
}

// ---------------------------------------------------------------------------
// An 8-element tuple: the outer `ValueTuple<T1..T7,TRest>` (tpc=8) carries the first 7 elements
// directly and the 8th in the `Rest` (a nested `ValueTuple<int>`, tpc=1). The recursion
// flattens both levels -> 8 elements.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, EightElementTupleFlattensNestedRest) {
	auto outerDef = ValueTupleDef(8);
	auto innerDef = ValueTupleDef(1);
	ITypePtr rest = ValueTupleOf(innerDef, {Int32Type()});
	std::vector<ITypePtr> outerArgs = {
		Int32Type(), Int32Type(), Int32Type(), Int32Type(),
		Int32Type(), Int32Type(), Int32Type(), rest
	};
	ITypePtr vt = ValueTupleOf(outerDef, outerArgs);
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 8u);
	// The first 7 are the direct args; the 8th is the Rest's single element.
	for (int i = 0; i < 8; i++) {
		EXPECT_EQ((*result)[i]->Kind(), TypeKind::Struct);
		EXPECT_EQ((*result)[i]->Name(), "Int32");
	}
}

// ---------------------------------------------------------------------------
// A 9-element tuple: the outer `ValueTuple<T1..T7,TRest>` (tpc=8) + a nested
// `ValueTuple<int,int>` (tpc=2) -> 9 elements (7 direct + 2 from the Rest).
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, NineElementTupleFlattensTwoLevelRest) {
	auto outerDef = ValueTupleDef(8);
	auto innerDef = ValueTupleDef(2);
	ITypePtr rest = ValueTupleOf(innerDef, {Int32Type(), Int32Type()});
	std::vector<ITypePtr> outerArgs = {
		Int32Type(), Int32Type(), Int32Type(), Int32Type(),
		Int32Type(), Int32Type(), Int32Type(), rest
	};
	ITypePtr vt = ValueTupleOf(outerDef, outerArgs);
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 9u);
}

// ---------------------------------------------------------------------------
// A `ValueTuple<...>` over a Class-kind definition (the C# accepts `TypeKind.Class` too --
// `case TypeKind.Class: case TypeKind.Struct:`) -> the type args.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, ClassKindValueTupleIsAccepted) {
	ITypePtr e0 = Int32Type();
	auto def = ValueTupleDef(2, TypeKind::Class);
	ITypePtr vt = ValueTupleOf(def, {e0, StringType()});
	auto result = GetTupleElementTypes(*vt);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 2u);
	EXPECT_EQ((*result)[0].get(), e0.get());
}

// ---------------------------------------------------------------------------
// A non-tuple, non-ValueTuple type (a primitive `int`, Kind=Struct, Name="Int32") -> nullopt.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, NonTuplePrimitiveReturnsNullopt) {
	ITypePtr i = Int32Type();
	auto result = GetTupleElementTypes(*i);
	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// A Struct-kind type that is not named "ValueTuple" -> nullopt (the Name check fails before
// the namespace check).
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, NonValueTupleStructReturnsNullopt) {
	auto def = std::make_shared<LookupTypeDefinition>(
		"SomeStruct", "System",
		FullTypeName(TopLevelTypeName("System", "SomeStruct", 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	ITypePtr vt = ValueTupleOf(def, {Int32Type()});
	auto result = GetTupleElementTypes(*vt);
	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// A "ValueTuple"-named type in the wrong namespace -> nullopt.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, WrongNamespaceReturnsNullopt) {
	auto def = std::make_shared<LookupTypeDefinition>(
		"ValueTuple", "MyApp",
		FullTypeName(TopLevelTypeName("MyApp", "ValueTuple", 2)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	ITypePtr vt = ValueTupleOf(def, {Int32Type(), StringType()});
	auto result = GetTupleElementTypes(*vt);
	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// A `ParameterizedType` over a `SimpleType` (no resolvable definition, `GetDefinition()`=null)
// named "ValueTuple" -> nullopt (the namespace check fails on the null definition). This is
// the degenerate "no resolvable definition" shape.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, DefinitionlessValueTupleReturnsNullopt) {
	ITypePtr simple = std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple", 2));
	ITypePtr vt = std::make_shared<ParameterizedType>(simple, std::vector<ITypePtr>{Int32Type(), StringType()});
	auto result = GetTupleElementTypes(*vt);
	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// The bare `ValueTuple`8` definition itself (a `LookupTypeDefinition`, Kind=Struct, NOT a
// `ParameterizedType`) -> nullopt. In the C# its `TypeArguments` returns the type parameters
// and the `tpc == RestPosition` recursion on the `TRest` type parameter (a `TypeKind::TypeParameter`,
// not a tuple) returns false, so the C# returns `default` for the bare definition too; the
// port's `dynamic_cast<ParameterizedType>` nullptr-guard returns false for the same degenerate
// shape, faithfully matching the C# result.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, BareValueTupleDefinitionReturnsNullopt) {
	auto def = ValueTupleDef(8);
	auto result = GetTupleElementTypes(*def);
	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// A `TupleType` whose underlying is a `ParameterizedType` over a `SimpleType` (no definition)
// is still a `TypeKind::Tuple`, so the Kind-match arm fires and the `ElementTypes` are
// returned regardless of the underlying's resolvability -- the `TupleType` carries its own
// element types.
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, TupleTypeReturnsElementsEvenWithDefinitionlessUnderlying) {
	ITypePtr e0 = Int32Type();
	ITypePtr e1 = StringType();
	ITypePtr underlying = std::make_shared<ParameterizedType>(
		std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple", 2)),
		std::vector<ITypePtr>{e0, e1});
	TupleType tuple = MakeTuple(underlying, {e0, e1});
	auto result = GetTupleElementTypes(tuple);
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->size(), 2u);
	EXPECT_EQ((*result)[0].get(), e0.get());
	EXPECT_EQ((*result)[1].get(), e1.get());
}

// ---------------------------------------------------------------------------
// An interface / array / pointer kind is not a tuple -> nullopt (the default arm).
// ---------------------------------------------------------------------------
TEST(GetTupleElementTypesTest, NonTupleNonValueTupleKindsReturnNullopt) {
	// An interface-kind type (the C# `default` arm returns false).
	auto ifaceDef = std::make_shared<LookupTypeDefinition>(
		"IValueTuple", "System",
		FullTypeName(TopLevelTypeName("System", "IValueTuple", 0)),
		TypeKind::Interface, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	EXPECT_FALSE(GetTupleElementTypes(*ifaceDef).has_value());
}
