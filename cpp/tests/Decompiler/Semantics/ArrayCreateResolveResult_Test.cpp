// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ArrayCreateResolveResult` (cpp/Decompiler/Semantics/ArrayCreateResolveResult.hpp,
// the D441 port of ICSharpCode.Decompiler/Semantics/ArrayCreateResolveResult.cs) -- the
// result of an array creation expression `new T[sizeArgs] { initializerElements }`. Derives
// directly from `ResolveResult` (D424) and adds the `SizeArguments` (a non-null
// `IReadOnlyList<ResolveResult>`) and the `InitializerElements` (a NULLABLE
// `IReadOnlyList<ResolveResult>` -- null when no initializer was specified); overrides
// `GetChildResults` to concat the `SizeArguments` with the `InitializerElements` only when
// the latter is non-null.
//
// The tests pin the ctor-stores-all-fields contract, the `SizeArguments`/`InitializerElements`
// accessors (incl. the nullable-no-initializer state), the `GetChildResults` concat crux
// (size args plus initializer elements when present; size args only when no initializer),
// the empty-size-arguments variant, the `ToString` subclass-class-name format, the
// `ShallowClone` runtime-type preservation plus shared-ownership, virtual dispatch through
// the base pointer, the inherited `ResolveResult` defaults, and the `is_base_of` /
// `has_virtual_destructor` / `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Convenience: a `KnownType(Int32)` (the array element / size-argument type).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `KnownType(Object)` (a distinct type for the initializer elements).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: build a `TypeResolveResult` over `KnownType(Int32)` -- a size argument.
std::shared_ptr<ILSpy::Decompiler::Semantics::TypeResolveResult> MakeSizeArg()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type());
}

// Convenience: build a two-element size-arguments list (two `TypeResolveResult`s over
// `KnownType(Int32)`).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoSizeArgs()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type())
    };
}

// Convenience: build a two-element initializer list (two `TypeResolveResult`s over
// `KnownType(Object)`).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoInitializers()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType())
    };
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor forwards the array type to the base `ResolveResult` and stores the size
// arguments and the initializer elements. `Type()` is the array type
// (`KnownType(Int32)`).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, CtorStoresArrayTypeSizeArgumentsAndInitializerElements)
{
    auto arrayType = MakeInt32Type();
    auto* arrayTypePtr = arrayType.get();
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto initializers = MakeTwoInitializers();
    auto* initializer0 = initializers[0].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        arrayType, sizeArgs, std::optional(std::move(initializers)));
    EXPECT_EQ(&rr.Type(), arrayTypePtr);
    ASSERT_EQ(rr.SizeArguments().size(), 2u);
    EXPECT_EQ(rr.SizeArguments()[0].get(), sizeArg0);
    ASSERT_TRUE(rr.InitializerElements().has_value());
    ASSERT_EQ(rr.InitializerElements()->size(), 2u);
    EXPECT_EQ(rr.InitializerElements()->operator[](0).get(), initializer0);
}

// ---------------------------------------------------------------------------
// The `SizeArguments()` accessor returns the stored shared_ptr vector by reference; the
// elements are pointer-identical to the originals.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, SizeArgumentsAccessorReturnsStoredSizeArguments)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto* sizeArg1 = sizeArgs[1].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), sizeArgs, /*initializerElements*/ std::nullopt);
    ASSERT_EQ(rr.SizeArguments().size(), 2u);
    EXPECT_EQ(rr.SizeArguments()[0].get(), sizeArg0);
    EXPECT_EQ(rr.SizeArguments()[1].get(), sizeArg1);
}

// ---------------------------------------------------------------------------
// The `InitializerElements()` accessor returns the stored `std::optional` by reference.
// When an initializer is provided, `has_value()` is true and the elements are
// pointer-identical to the originals.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, InitializerElementsAccessorReturnsPresentInitializer)
{
    auto initializers = MakeTwoInitializers();
    auto* initializer0 = initializers[0].get();
    auto* initializer1 = initializers[1].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), std::optional(std::move(initializers)));
    ASSERT_TRUE(rr.InitializerElements().has_value());
    ASSERT_EQ(rr.InitializerElements()->size(), 2u);
    EXPECT_EQ(rr.InitializerElements()->operator[](0).get(), initializer0);
    EXPECT_EQ(rr.InitializerElements()->operator[](1).get(), initializer1);
}

// ---------------------------------------------------------------------------
// The nullable-no-initializer crux: when `initializerElements` is `std::nullopt` (the C#
// `null` "no initializer" state), `InitializerElements().has_value()` is false (the C#
// `InitializerElements != null` gate is false). This is distinct from a present empty
// vector which is a non-null EMPTY initializer.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, InitializerElementsIsNullWhenNoInitializer)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), /*initializerElements*/ std::nullopt);
    EXPECT_FALSE(rr.InitializerElements().has_value());
}

// ---------------------------------------------------------------------------
// A present empty initializer (an empty `std::vector` wrapped in a `std::optional`) is
// DISTINCT from a null initializer (`std::nullopt`): `has_value()` is true but the list
// is empty. The C# `null`-vs-non-null-empty-`IList` distinction the `std::optional`
// representation carries faithfully.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, InitializerElementsPresentEmptyDistinctFromNull)
{
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> emptyInitializers;
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), std::optional(std::move(emptyInitializers)));
    ASSERT_TRUE(rr.InitializerElements().has_value());
    EXPECT_EQ(rr.InitializerElements()->size(), 0u);
}

// ---------------------------------------------------------------------------
// The ctor accepts an empty size-arguments list (the faithful equivalent of a non-null
// empty `IList<ResolveResult>`); the C# `sizeArguments == null` `ArgumentNullException`
// guard has no C++ counterpart (a `std::vector` passed by value is never null).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, CtorAcceptsEmptySizeArguments)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), /*sizeArguments*/ {}, /*initializerElements*/ std::nullopt);
    EXPECT_EQ(rr.SizeArguments().size(), 0u);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: when an initializer IS present, concats the `SizeArguments`
// with the `InitializerElements` (`SizeArguments.Concat(InitializerElements)`). A
// creation with two size arguments and two initializer elements yields 2 + 2 = 4
// children in order: sizeArg0, sizeArg1, initializer0, initializer1.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, GetChildResultsConcatsSizeArgumentsAndInitializerElements)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto* sizeArg1 = sizeArgs[1].get();
    auto initializers = MakeTwoInitializers();
    auto* initializer0 = initializers[0].get();
    auto* initializer1 = initializers[1].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), sizeArgs, std::optional(std::move(initializers)));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 4u);
    EXPECT_EQ(children[0], sizeArg0);
    EXPECT_EQ(children[1], sizeArg1);
    EXPECT_EQ(children[2], initializer0);
    EXPECT_EQ(children[3], initializer1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: when NO initializer is present (`std::nullopt`), yields just
// the `SizeArguments` (no initializer elements to append). A creation with two size
// arguments and no initializer yields 2 children.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, GetChildResultsSizeArgumentsOnlyWhenNoInitializer)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto* sizeArg1 = sizeArgs[1].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), sizeArgs, /*initializerElements*/ std::nullopt);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], sizeArg0);
    EXPECT_EQ(children[1], sizeArg1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with a present empty initializer yields just the `SizeArguments`
// (the present-but-empty initializer contributes zero elements, but `has_value()` is
// true -- the concat runs over an empty list). A creation with two size arguments and
// an empty initializer yields 2 + 0 = 2 children.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, GetChildResultsSizeArgumentsOnlyWhenEmptyInitializer)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto* sizeArg1 = sizeArgs[1].get();
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> emptyInitializers;
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), sizeArgs, std::optional(std::move(emptyInitializers)));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], sizeArg0);
    EXPECT_EQ(children[1], sizeArg1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with an empty size-arguments list and no initializer yields zero
// children (no size args and no initializer elements).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, GetChildResultsEmptyWhenNoSizeArgumentsAndNoInitializer)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), /*sizeArguments*/ {}, /*initializerElements*/ std::nullopt);
    auto children = rr.GetChildResults();
    EXPECT_EQ(children.size(), 0u);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the array type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override
// returns "ArrayCreateResolveResult"; the array type is `KnownType(Int32)` whose
// `ReflectionName()` is "System.Int32" (the `Namespace.Name` form).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, ToStringReportsSubclassClassNameAndArrayType)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), /*initializerElements*/ std::nullopt);
    EXPECT_EQ(rr.ToString(), "[ArrayCreateResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is an `ArrayCreateResolveResult`,
// not the `ResolveResult` base a non-overriding clone would slice to). The clone's
// `ToString()` reports "ArrayCreateResolveResult" (the virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), /*initializerElements*/ std::nullopt);
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[ArrayCreateResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `SizeArguments` and the `InitializerElements` (the default
// copy ctor shares the `sizeArguments_` shared_ptr vector and copies the
// `initializerElements_` optional sharing each present `ResolveResult` faithfully
// mirroring the C# `MemberwiseClone` reference-copy). The clone's size-argument and
// initializer elements are pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, ShallowCloneSharesSizeArgumentsAndInitializerElements)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto initializers = MakeTwoInitializers();
    auto* initializer0 = initializers[0].get();
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), sizeArgs, std::optional(std::move(initializers)));
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::ArrayCreateResolveResult*>(
        clone.get());
    ASSERT_EQ(derived->SizeArguments().size(), 2u);
    EXPECT_EQ(derived->SizeArguments()[0].get(), sizeArg0);
    ASSERT_TRUE(derived->InitializerElements().has_value());
    ASSERT_EQ(derived->InitializerElements()->size(), 2u);
    EXPECT_EQ(derived->InitializerElements()->operator[](0).get(), initializer0);
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `Type()` (the base `type_` shared_ptr) is shared but the
// `ArrayCreateResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, ShallowCloneIsDistinctInstance)
{
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> emptySizeArgs;
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>(
        MakeInt32Type(), emptySizeArgs, std::nullopt);
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `ArrayCreateResolveResult` overrides through a `ResolveResult` reference). A creation
// with two size arguments and two initializer elements yields 4 children through the
// base pointer.
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto sizeArgs = MakeTwoSizeArgs();
    auto* sizeArg0 = sizeArgs[0].get();
    auto initializers = MakeTwoInitializers();
    auto* initializer0 = initializers[0].get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>(
            MakeInt32Type(), sizeArgs, std::optional(std::move(initializers)));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 4u);
    EXPECT_EQ(children[0], sizeArg0);
    EXPECT_EQ(children[2], initializer0);
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved: an array creation is not a
// compile-time constant, its `ConstantValue` is empty, and it is not an error (the base
// defaults).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::ArrayCreateResolveResult rr(
        MakeInt32Type(), MakeTwoSizeArgs(), /*initializerElements*/ std::nullopt);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `ArrayCreateResolveResult` IS-A `ResolveResult`, is polymorphic with a
// virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(ArrayCreateResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::ArrayCreateResolveResult>);
}
