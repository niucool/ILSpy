// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `AstType` `Make*` builders (cpp/.../Syntax/AstType.hpp + the `ComposedType`
// overrides, the port of the hand-written partials in
// ICSharpCode.Decompiler/CSharp/Syntax/AstType.cs lines 75-105 and ComposedType.cs
// lines 99-123). These are the builders the `TypeSystemAstBuilder.ConvertType` path
// consumes to wrap a type reference in a pointer/array/nullable/ref `ComposedType`:
//   - `MakePointerType()` -- wraps in a fresh `ComposedType` with `PointerRank` 1; the
//     `ComposedType` override EXTENDS the existing pointer run in place when the node
//     carries no array specifiers (`T*` + `*` -> the same node with `PointerRank` 2), and
//     falls back to the base (a fresh wrapper: `T[]` + `*` -> `T[]*`) when it does.
//   - `MakeArrayType(int rank = 1)` -- wraps in a fresh `ComposedType` with one rank
//     specifier; the `ComposedType` override INSERTS the new specifier BEFORE the first
//     existing one, so `T[]` + `[,]` renders `T[,][]` (the new rank is the innermost).
//   - `MakeNullableType()` -- always wraps in a fresh `ComposedType` with
//     `HasNullableSpecifier` (NOT virtual: no in-place override).
//   - `MakeRefType()` -- wraps in a fresh `ComposedType` with `HasRefSpecifier`; the
//     `ComposedType` override sets the flag IN PLACE and returns the same node.
//
// The virtual dispatch is pinned by calling through an `AstType*` static type (a base
// pointer to a `ComposedType` reaches the override). The returned nodes follow the
// D223 non-owning leak model (`new`-ed raw pointers; the tests never free them).

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;

namespace {

// A fresh `SimpleType("T")` -- the target the builders wrap. Heap-allocated so the
// wrapped node's `Parent()` back-pointer stays valid for the test's lifetime (the leak
// model never frees it).
SimpleType* MakeTarget() {
    return new SimpleType(std::string("T"));
}

} // namespace

// ---- MakePointerType ----------------------------------------------------

// `MakePointerType` on a non-composed type wraps it in a fresh `ComposedType` with
// `PointerRank` 1: the base builder constructs `new ComposedType { BaseType = this }` and
// dispatches to the (virtual) `ComposedType.MakePointerType`, which extends the fresh
// (specifier-less) node in place.
TEST(CSharp_AstTypeMakeBuilders, MakePointerTypeWrapsSimpleType) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakePointerType();
    ASSERT_NE(result, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
    EXPECT_EQ(composed->PointerRank(), 1);
    EXPECT_FALSE(composed->HasRefSpecifier());
    EXPECT_FALSE(composed->HasNullableSpecifier());
    EXPECT_EQ(composed->ArraySpecifiers().Count(), 0);
    EXPECT_EQ(composed->GetChildCount(), 1);  // BaseType slot only
}

// The wrapped type is re-parented: the `BaseType` setter attaches it to the fresh
// `ComposedType`.
TEST(CSharp_AstTypeMakeBuilders, MakePointerTypeBaseTypeIsReparented) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakePointerType();
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(t->Parent(), composed);
}

// A second `MakePointerType` on the pointer node (called through the `AstType*` base
// type, so the call virtually dispatches) EXTENDS the existing pointer run in place:
// the same node comes back with `PointerRank` 2. A third call reaches 3.
TEST(CSharp_AstTypeMakeBuilders, MakePointerTypeExtendsExistingPointerInPlace) {
    SimpleType* t = MakeTarget();
    AstType* first = t->MakePointerType();
    AstType* second = first->MakePointerType();
    EXPECT_EQ(second, first);
    AstType* third = second->MakePointerType();
    EXPECT_EQ(third, first);
    auto* composed = dynamic_cast<ComposedType*>(first);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->PointerRank(), 3);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
}

// `MakePointerType` on a composed type that CARRIES array specifiers does NOT extend it
// in place (that would render `T[]*` as one node -- the C# AST always renders a pointer
// to an array as a fresh wrapper): the override takes the base path, returning a NEW
// `ComposedType` with `PointerRank` 1 whose `BaseType` is the array node, and leaving
// the array node untouched (still `PointerRank` 0, still one specifier).
TEST(CSharp_AstTypeMakeBuilders, MakePointerTypeOnArrayWrapsInNewNode) {
    SimpleType* t = MakeTarget();
    AstType* arrayType = t->MakeArrayType(1);
    auto* arrayComposed = dynamic_cast<ComposedType*>(arrayType);
    ASSERT_NE(arrayComposed, nullptr);
    ASSERT_EQ(arrayComposed->ArraySpecifiers().Count(), 1);

    AstType* result = arrayType->MakePointerType();
    ASSERT_NE(result, static_cast<AstType*>(arrayComposed));
    auto* wrapper = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(wrapper, nullptr);
    EXPECT_EQ(wrapper->PointerRank(), 1);
    EXPECT_EQ(wrapper->BaseType(), static_cast<AstType*>(arrayComposed));
    // The array node itself is unchanged.
    EXPECT_EQ(arrayComposed->PointerRank(), 0);
    EXPECT_EQ(arrayComposed->ArraySpecifiers().Count(), 1);
    EXPECT_EQ(arrayComposed->BaseType(), static_cast<AstType*>(t));
}

// The in-place extension also fires for a nullable wrapper (a `ComposedType` with no
// array specifiers): `T?` + `*` is the SAME node carrying both the nullable specifier
// and `PointerRank` 1 (the C# renders `T?*` as one composed node).
TEST(CSharp_AstTypeMakeBuilders, MakePointerTypeOnNullableExtendsInPlace) {
    SimpleType* t = MakeTarget();
    AstType* nullable = t->MakeNullableType();
    AstType* result = nullable->MakePointerType();
    EXPECT_EQ(result, nullable);
    auto* composed = dynamic_cast<ComposedType*>(nullable);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
    EXPECT_EQ(composed->PointerRank(), 1);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
}

// ---- MakeArrayType -----------------------------------------------------

// `MakeArrayType(2)` on a simple type wraps it in a fresh `ComposedType` carrying one
// rank specifier of the given rank.
TEST(CSharp_AstTypeMakeBuilders, MakeArrayTypeWrapsWithRankTwoSpecifier) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakeArrayType(2);
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
    EXPECT_EQ(composed->PointerRank(), 0);
    ASSERT_EQ(composed->ArraySpecifiers().Count(), 1);
    EXPECT_EQ(composed->ArraySpecifiers().At(0)->Dimensions(), 2);
    EXPECT_EQ(t->Parent(), composed);
}

// The default rank argument is 1 (the C# `int rank = 1`).
TEST(CSharp_AstTypeMakeBuilders, MakeArrayTypeDefaultRankIsOne) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakeArrayType();
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    ASSERT_EQ(composed->ArraySpecifiers().Count(), 1);
    EXPECT_EQ(composed->ArraySpecifiers().At(0)->Dimensions(), 1);
}

// `MakeArrayType` on a composed type that already carries a rank specifier inserts the
// new specifier BEFORE the first existing one: `T[].MakeArrayType(2)` yields two
// specifiers `[2, 1]`, which renders `T[,][]` (the new rank is the innermost -- the C#
// doc comment on `MakeArrayType`). This pins the InsertBefore-FirstOrDefault (prepend,
// not append) semantics.
TEST(CSharp_AstTypeMakeBuilders, MakeArrayTypePrependsNewRankBeforeExisting) {
    SimpleType* t = MakeTarget();
    AstType* first = t->MakeArrayType(1);
    AstType* result = first->MakeArrayType(2);
    EXPECT_EQ(result, first);  // in place
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    ASSERT_EQ(composed->ArraySpecifiers().Count(), 2);
    EXPECT_EQ(composed->ArraySpecifiers().At(0)->Dimensions(), 2);
    EXPECT_EQ(composed->ArraySpecifiers().At(1)->Dimensions(), 1);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
}

// ---- MakeRefType -------------------------------------------------------

// `MakeRefType` on a non-composed type wraps it in a fresh `ComposedType` with
// `HasRefSpecifier`.
TEST(CSharp_AstTypeMakeBuilders, MakeRefTypeWrapsSimpleType) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakeRefType();
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_EQ(composed->PointerRank(), 0);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
    EXPECT_EQ(t->Parent(), composed);
}

// `MakeRefType` on a composed type sets the flag IN PLACE and returns the same node
// (the C# `MakeRefType` override): `T*` + `ref` is the same node with `HasRefSpecifier`
// and `PointerRank` 1 coexisting.
TEST(CSharp_AstTypeMakeBuilders, MakeRefTypeOnComposedSetsFlagInPlace) {
    SimpleType* t = MakeTarget();
    AstType* pointer = t->MakePointerType();
    AstType* result = pointer->MakeRefType();
    EXPECT_EQ(result, pointer);
    auto* composed = dynamic_cast<ComposedType*>(pointer);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_EQ(composed->PointerRank(), 1);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
}

// ---- MakeNullableType --------------------------------------------------

// `MakeNullableType` on a non-composed type wraps it in a fresh `ComposedType` with
// `HasNullableSpecifier`.
TEST(CSharp_AstTypeMakeBuilders, MakeNullableTypeWrapsSimpleType) {
    SimpleType* t = MakeTarget();
    AstType* result = t->MakeNullableType();
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
    EXPECT_EQ(composed->PointerRank(), 0);
    EXPECT_EQ(composed->BaseType(), static_cast<AstType*>(t));
    EXPECT_EQ(t->Parent(), composed);
}

// `MakeNullableType` is NOT virtual (no `ComposedType` override): it ALWAYS wraps,
// even over an existing composed type -- `T*` + `?` is a fresh outer node with the
// nullable specifier whose `BaseType` is the pointer node (the pointer node is left
// unchanged). This pins the non-virtual asymmetry against `MakeRefType`.
TEST(CSharp_AstTypeMakeBuilders, MakeNullableTypeAlwaysWrapsNotInPlace) {
    SimpleType* t = MakeTarget();
    AstType* pointer = t->MakePointerType();
    AstType* result = pointer->MakeNullableType();
    ASSERT_NE(result, pointer);
    auto* outer = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(outer, nullptr);
    EXPECT_TRUE(outer->HasNullableSpecifier());
    EXPECT_EQ(outer->BaseType(), static_cast<AstType*>(pointer));
    auto* pointerComposed = dynamic_cast<ComposedType*>(pointer);
    ASSERT_NE(pointerComposed, nullptr);
    EXPECT_FALSE(pointerComposed->HasNullableSpecifier());
    EXPECT_EQ(pointerComposed->PointerRank(), 1);
}

// A chain through the base pointer type: `T` -> `T?` -> `T?*` (the nullable wrapper,
// being a specifier-less composed node, is extended in place by `MakePointerType`).
TEST(CSharp_AstTypeMakeBuilders, MakeNullableThenPointerChainStacks) {
    SimpleType* t = MakeTarget();
    AstType* nullable = t->MakeNullableType();
    AstType* result = nullable->MakePointerType();
    EXPECT_EQ(result, nullable);
    auto* composed = dynamic_cast<ComposedType*>(nullable);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
    EXPECT_EQ(composed->PointerRank(), 1);
}
