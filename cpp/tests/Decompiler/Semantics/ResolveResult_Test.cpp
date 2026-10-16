// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ResolveResult` (cpp/Decompiler/Semantics/ResolveResult.hpp, the D424 port
// of ICSharpCode.Decompiler/Semantics/ResolveResult.cs). `ResolveResult` is the base
// class for all expression-resolution results: it carries the resolved `IType` plus a
// small virtual surface (`IsCompileTimeConstant` / `ConstantValue` / `IsError` /
// `GetChildResults` / `ShallowClone` / `ToString`) the resolver subclasses override.
// It is the root of the `Semantics` namespace and the first `Semantics` leaf toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker).
//
// The tests pin the ctor-stores-type contract, the four virtual defaults
// (`IsCompileTimeConstant` / `ConstantValue` / `IsError` / `GetChildResults`), the
// `ToString` class-name-and-reflection-name format (the polymorphic `ClassName()` crux),
// the `ShallowClone` shared-ownership clone (a distinct instance sharing the `IType` via
// the `shared_ptr` member), and the virtual dispatch of subclass overrides through a
// base pointer. The `IType` is a `KnownType(String)` (a concrete minimal-port `IType`
// whose `ReflectionName()` is "System.String").

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete `ResolveResult` subclass overriding the four virtual defaults to exercise
// polymorphic dispatch through a `ResolveResult*` (the C# subclasses -- `TypeResolveResult`,
// `ConstantResolveResult`, `ErrorResolveResult`, ... -- override the same surface). The
// `ClassName()` override reports "TestResolveResult" so the inherited `ToString` (which
// calls `ClassName()`) reports the subclass name -- the polymorphic `GetType().Name` crux.
class TestResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    explicit TestResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : ResolveResult(std::move(type)) {}

    bool IsCompileTimeConstant() const override { return true; }

    std::any ConstantValue() const override { return 42; }

    bool IsError() const override { return true; }

    std::vector<const ILSpy::Decompiler::Semantics::ResolveResult*> GetChildResults() const override
    {
        return {};
    }

protected:
    std::string ClassName() const override { return "TestResolveResult"; }
};

ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

} // namespace

TEST(ResolveResultTest, ConstructorStoresType)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_EQ(&rr.Type(), type.get());
}

TEST(ResolveResultTest, TypeReturnsNonNullReference)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_NE(&rr.Type(), nullptr);
    EXPECT_EQ(rr.Type().ReflectionName(), "System.String");
}

TEST(ResolveResultTest, IsCompileTimeConstantDefaultsToFalse)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
}

TEST(ResolveResultTest, ConstantValueDefaultsToEmpty)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

TEST(ResolveResultTest, IsErrorDefaultsToFalse)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_FALSE(rr.IsError());
}

TEST(ResolveResultTest, GetChildResultsDefaultsToEmpty)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    EXPECT_TRUE(rr.GetChildResults().empty());
}

TEST(ResolveResultTest, ToStringIncludesClassNameAndReflectionName)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    // The C# `ToString` => "[" + GetType().Name + " " + type + "]" -- the base class name
    // "ResolveResult" followed by the type's ReflectionName.
    EXPECT_EQ(rr.ToString(), "[ResolveResult System.String]");
}

TEST(ResolveResultTest, ShallowCloneSharesType)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    // The clone shares the IType via the shared_ptr member (faithful to MemberwiseClone's
    // reference copy): the same IType object backs both the original and the clone.
    EXPECT_EQ(&clone->Type(), &rr.Type());
    EXPECT_EQ(clone->Type().ReflectionName(), "System.String");
}

TEST(ResolveResultTest, ShallowCloneReturnsDistinctInstance)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    // The clone is a distinct object (not the same instance).
    EXPECT_NE(clone.get(), &rr);
}

TEST(ResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ResolveResult rr(type);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &rr;
    EXPECT_FALSE(base->IsCompileTimeConstant());
    EXPECT_FALSE(base->IsError());
    EXPECT_FALSE(base->ConstantValue().has_value());
    EXPECT_EQ(base->ToString(), "[ResolveResult System.String]");
}

TEST(ResolveResultTest, SubclassOverrideIsDispatchedThroughBasePointer)
{
    auto type = MakeStringType();
    TestResolveResult trr(type);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &trr;
    // The subclass overrides dispatch through the base pointer (the virtual dispatch
    // the C# resolver relies on).
    EXPECT_TRUE(base->IsCompileTimeConstant());
    EXPECT_TRUE(base->IsError());
    ASSERT_TRUE(base->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(base->ConstantValue()), 42);
    // The inherited ToString uses the overridden ClassName() -- the polymorphic
    // GetType().Name crux.
    EXPECT_EQ(base->ToString(), "[TestResolveResult System.String]");
}

TEST(ResolveResultTest, HasVirtualDestructorAndIsPolymorphic)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ResolveResult>,
                  "ResolveResult must have a virtual destructor (it is a polymorphic base "
                  "held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ResolveResult>,
                  "ResolveResult must be polymorphic (the virtual defaults dispatch "
                  "through base pointers).");
    // The base is CONCRETE and instantiable (the C# `ResolveResult` is not abstract),
    // unlike the ported interfaces that are pure-virtual abstract bases.
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
