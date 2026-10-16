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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ArrayAccessResolveResult` (cpp/Decompiler/Semantics/ArrayAccessResolveResult.hpp,
// the D439 port of ICSharpCode.Decompiler/Semantics/ArrayAccessResolveResult.cs) -- the
// result of an array access expression `array[indexes]`. Derives directly from
// `ResolveResult` (D424) and adds the accessed `Array` (a `ResolveResult`) and the `Indexes`
// (an `IList<ResolveResult>`); overrides `GetChildResults` to concat the `Array` with the
// `Indexes` (`new[] { Array }.Concat(Indexes)`).
//
// The tests pin the ctor-stores-all-fields contract, the `Array`/`Indexes` accessors, the
// `GetChildResults` concat crux (Array + Indexes in order), the empty-indexes variant, the
// `ToString` subclass-class-name format, the `ShallowClone` runtime-type preservation plus
// shared-ownership of the `Array` and `Indexes`, virtual dispatch through the base pointer,
// the inherited `ResolveResult` defaults, and the `is_base_of` / `has_virtual_destructor` /
// `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/ArrayAccessResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Convenience: a `KnownType(Int32)` (the array element type).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `KnownType(Object)` (a distinct type for the array expression).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: build a `TypeResolveResult` over `KnownType(Object)` -- the array expression.
std::shared_ptr<ILSpy::Decompiler::Semantics::TypeResolveResult> MakeArray()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType());
}

// Convenience: build a two-element index list (two `TypeResolveResult`s over
// `KnownType(Int32)`).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoIndexes()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type())
    };
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor forwards the element type to the base `ResolveResult` and stores the array
// and indexes. `Type()` is the element type (`KnownType(Int32)`).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, CtorStoresElementTypeArrayAndIndexes)
{
    auto elementType = MakeInt32Type();
    auto* elementTypePtr = elementType.get();
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    auto indexes = MakeTwoIndexes();
    auto* index0 = indexes[0].get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        elementType, array, std::move(indexes));
    EXPECT_EQ(&rr.Type(), elementTypePtr);
    EXPECT_EQ(rr.Array(), arrayPtr);
    ASSERT_EQ(rr.Indexes().size(), 2u);
    EXPECT_EQ(rr.Indexes()[0].get(), index0);
}

// ---------------------------------------------------------------------------
// The `Array()` accessor returns the stored array expression by pointer identity.
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, ArrayAccessorReturnsStoredArray)
{
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), array, /*indexes*/ {});
    EXPECT_EQ(rr.Array(), arrayPtr);
}

// ---------------------------------------------------------------------------
// The `Indexes()` accessor returns the stored shared_ptr vector by reference; the
// elements are pointer-identical to the originals.
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, IndexesAccessorReturnsStoredIndexes)
{
    auto indexes = MakeTwoIndexes();
    auto* index0 = indexes[0].get();
    auto* index1 = indexes[1].get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), MakeArray(), std::move(indexes));
    ASSERT_EQ(rr.Indexes().size(), 2u);
    EXPECT_EQ(rr.Indexes()[0].get(), index0);
    EXPECT_EQ(rr.Indexes()[1].get(), index1);
}

// ---------------------------------------------------------------------------
// The ctor accepts an empty index list (the faithful equivalent of a non-null empty
// `IList<ResolveResult>`); the C# `indexes == null` `ArgumentNullException` guard has no
// C++ counterpart (a `std::vector` passed by value is never null).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, CtorAcceptsEmptyIndexes)
{
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), MakeArray(), /*indexes*/ {});
    EXPECT_EQ(rr.Indexes().size(), 0u);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: concats the `Array` with the `Indexes` (`new[] { Array
// }.Concat(Indexes)`). An access with one array and two indexes yields 1 + 2 = 3
// children in order: array, index0, index1.
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, GetChildResultsConcatsArrayAndIndexes)
{
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    auto indexes = MakeTwoIndexes();
    auto* index0 = indexes[0].get();
    auto* index1 = indexes[1].get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), array, std::move(indexes));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 3u);
    EXPECT_EQ(children[0], arrayPtr);
    EXPECT_EQ(children[1], index0);
    EXPECT_EQ(children[2], index1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with an empty index list yields just the `Array` (a one-element
// snapshot, no indexes to append).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, GetChildResultsArrayOnlyWhenNoIndexes)
{
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), array, /*indexes*/ {});
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], arrayPtr);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the element type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override
// returns "ArrayAccessResolveResult"; the element type is `KnownType(Int32)` whose
// `ReflectionName()` is "System.Int32" (the `Namespace.Name` form).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, ToStringReportsSubclassClassNameAndElementType)
{
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), MakeArray(), /*indexes*/ {});
    EXPECT_EQ(rr.ToString(), "[ArrayAccessResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is an `ArrayAccessResolveResult`,
// not the `ResolveResult` base a non-overriding clone would slice to). The clone's
// `ToString()` reports "ArrayAccessResolveResult" (the virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), MakeArray(), /*indexes*/ {});
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[ArrayAccessResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `Array` and the `Indexes` (the default copy ctor shares the
// `array_` shared_ptr and the `indexes_` shared_ptr vector faithfully mirroring the C#
// `MemberwiseClone` reference-copy). The clone's array and index elements are
// pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, ShallowCloneSharesArrayAndIndexes)
{
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    auto indexes = MakeTwoIndexes();
    auto* index0 = indexes[0].get();
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), array, std::move(indexes));
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::ArrayAccessResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->Array(), arrayPtr);
    ASSERT_EQ(derived->Indexes().size(), 2u);
    EXPECT_EQ(derived->Indexes()[0].get(), index0);
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `Type()` (the base `type_` shared_ptr) is shared but the
// `ArrayAccessResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, ShallowCloneIsDistinctInstance)
{
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> emptyIndexes;
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>(
        MakeInt32Type(), MakeArray(), emptyIndexes);
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `ArrayAccessResolveResult` overrides through a `ResolveResult` reference).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto array = MakeArray();
    auto* arrayPtr = array.get();
    auto indexes = MakeTwoIndexes();
    auto* index0 = indexes[0].get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>(
            MakeInt32Type(), array, std::move(indexes));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 3u);
    EXPECT_EQ(children[0], arrayPtr);
    EXPECT_EQ(children[1], index0);
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved: an array access is not a
// compile-time constant, its `ConstantValue` is empty, and it is not an error (the base
// defaults).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::ArrayAccessResolveResult rr(
        MakeInt32Type(), MakeArray(), /*indexes*/ {});
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `ArrayAccessResolveResult` IS-A `ResolveResult`, is polymorphic with a
// virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(ArrayAccessResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::ArrayAccessResolveResult>);
}
