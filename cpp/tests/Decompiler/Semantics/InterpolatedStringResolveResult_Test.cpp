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
// PURPOSE NONINFRINGEMENT AND AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `InterpolatedStringResolveResult` (cpp/Decompiler/Semantics/
// InterpolatedStringResolveResult.hpp, the D443 port of
// ICSharpCode.Decompiler/Semantics/InterpolatedStringResolveResult.cs) -- the result of an
// interpolated-string expression `$"..."`. Derives directly from `ResolveResult` (D424) and
// adds the `FormatString` (a `string`) and the `Arguments` (a `ResolveResult[]`); overrides
// `GetChildResults` to return the `Arguments` directly.
//
// The tests pin the ctor-stores-all-fields contract, the `FormatString`/`Arguments` accessors,
// the `GetChildResults` returns-Arguments crux (in order), the empty-arguments variant, the
// `ToString` subclass-class-name format, the `ShallowClone` runtime-type preservation plus
// shared-ownership of the `Arguments` and value-copy of the `FormatString`, virtual dispatch
// through the base pointer, the inherited `ResolveResult` defaults, and the `is_base_of` /
// `has_virtual_destructor` / `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"
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

// Convenience: a `KnownType(String)` (the interpolated-string type forwarded to the base;
// its `ReflectionName()` is "System.String").
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// Convenience: a `KnownType(Object)` (a distinct type for an interpolated argument).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: a `KnownType(Int32)` (a distinct type for a second interpolated argument).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: build a two-element argument list (a `TypeResolveResult` over
// `KnownType(Object)` then one over `KnownType(Int32)`).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoArguments()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type())
    };
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor forwards the string type to the base `ResolveResult` and stores the format
// string and arguments. `Type()` is the string type (`KnownType(String)`).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, CtorStoresStringTypeFormatStringAndArguments)
{
    auto stringType = MakeStringType();
    auto* stringTypePtr = stringType.get();
    auto arguments = MakeTwoArguments();
    auto* arg0 = arguments[0].get();
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        stringType, "Hello, {0} {1}!", std::move(arguments));
    EXPECT_EQ(&rr.Type(), stringTypePtr);
    EXPECT_EQ(rr.FormatString(), "Hello, {0} {1}!");
    ASSERT_EQ(rr.Arguments().size(), 2u);
    EXPECT_EQ(rr.Arguments()[0].get(), arg0);
}

// ---------------------------------------------------------------------------
// The `FormatString()` accessor returns the stored format string by reference.
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, FormatStringAccessorReturnsStoredValue)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "x={0}", /*arguments*/ {});
    EXPECT_EQ(rr.FormatString(), "x={0}");
}

// ---------------------------------------------------------------------------
// The `Arguments()` accessor returns the stored shared_ptr vector by reference; the
// elements are pointer-identical to the originals.
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ArgumentsAccessorReturnsStoredArguments)
{
    auto arguments = MakeTwoArguments();
    auto* arg0 = arguments[0].get();
    auto* arg1 = arguments[1].get();
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "", std::move(arguments));
    ASSERT_EQ(rr.Arguments().size(), 2u);
    EXPECT_EQ(rr.Arguments()[0].get(), arg0);
    EXPECT_EQ(rr.Arguments()[1].get(), arg1);
}

// ---------------------------------------------------------------------------
// The ctor accepts an empty argument list (the faithful equivalent of a non-null empty
// `ResolveResult[]`); the C# null-array state has no C++ counterpart (a `std::vector`
// passed by value is never null).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, CtorAcceptsEmptyArguments)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "", /*arguments*/ {});
    EXPECT_EQ(rr.Arguments().size(), 0u);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: returns the `Arguments` directly, in order. A string with two
// arguments yields 2 children in order: arg0, arg1.
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, GetChildResultsReturnsArgumentsInOrder)
{
    auto arguments = MakeTwoArguments();
    auto* arg0 = arguments[0].get();
    auto* arg1 = arguments[1].get();
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "{0}{1}", std::move(arguments));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], arg0);
    EXPECT_EQ(children[1], arg1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with an empty argument list yields an empty snapshot (no arguments to
// return).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, GetChildResultsEmptyWhenNoArguments)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "plain", /*arguments*/ {});
    auto children = rr.GetChildResults();
    EXPECT_EQ(children.size(), 0u);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the string type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override returns
// "InterpolatedStringResolveResult"; the string type is `KnownType(String)` whose
// `ReflectionName()` is "System.String" (the `Namespace.Name` form).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ToStringReportsSubclassClassNameAndStringType)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "", /*arguments*/ {});
    EXPECT_EQ(rr.ToString(), "[InterpolatedStringResolveResult System.String]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is an
// `InterpolatedStringResolveResult`, not the `ResolveResult` base a non-overriding clone
// would slice to). The clone's `ToString()` reports "InterpolatedStringResolveResult" (the
// virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "", /*arguments*/ {});
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[InterpolatedStringResolveResult System.String]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `Arguments` (the default copy ctor copies the `arguments_`
// shared_ptr vector element-wise faithfully mirroring the C# `MemberwiseClone`
// reference-copy). The clone's argument elements are pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ShallowCloneSharesArguments)
{
    auto arguments = MakeTwoArguments();
    auto* arg0 = arguments[0].get();
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "{0}{1}", std::move(arguments));
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult*>(
        clone.get());
    ASSERT_EQ(derived->Arguments().size(), 2u);
    EXPECT_EQ(derived->Arguments()[0].get(), arg0);
}

// ---------------------------------------------------------------------------
// `ShallowClone` value-copies the `FormatString` (the default copy ctor value-copies the
// `std::string` member). The clone's format string equals the original's (a distinct
// string with the same value).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ShallowCloneCopiesFormatString)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "x={0}", /*arguments*/ {});
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->FormatString(), "x={0}");
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `Type()` (the base `type_` shared_ptr) is shared but the
// `InterpolatedStringResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, ShallowCloneIsDistinctInstance)
{
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> emptyArguments;
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>(
        MakeStringType(), "", emptyArguments);
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `InterpolatedStringResolveResult` overrides through a `ResolveResult` reference).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto arguments = MakeTwoArguments();
    auto* arg0 = arguments[0].get();
    auto* arg1 = arguments[1].get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>(
            MakeStringType(), "{0}{1}", std::move(arguments));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], arg0);
    EXPECT_EQ(children[1], arg1);
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved: an interpolated string is not a
// compile-time constant, its `ConstantValue` is empty, and it is not an error (the base
// defaults).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult rr(
        MakeStringType(), "", /*arguments*/ {});
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `InterpolatedStringResolveResult` IS-A `ResolveResult`, is polymorphic
// with a virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(InterpolatedStringResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult>);
}
