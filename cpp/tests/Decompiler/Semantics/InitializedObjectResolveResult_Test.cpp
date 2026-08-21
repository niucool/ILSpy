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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `InitializedObjectResolveResult` (cpp/Decompiler/Semantics/
// InitializedObjectResolveResult.hpp, the D434 port of
// ICSharpCode.Decompiler/Semantics/InitializedObjectResolveResult.cs) -- the
// `ResolveResult` (D424) that refers to the object currently being initialized.
// The C# source declares only the forwarding ctor (`InitializedObjectResolveResult(IType
// type) : base(type)`); the C++ port additionally overrides `ClassName()` (the
// polymorphic `GetType().Name` crux in the inherited `ToString`) and `ShallowClone()`
// (the runtime-type-preserving clone, avoiding the C++-only slicing).
//
// The tests pin the ctor-stores-type contract, the inherited base defaults the
// subclass does NOT override (`IsError` / `IsCompileTimeConstant` / `ConstantValue` /
// `GetChildResults`), the `ToString` subclass-class-name format, the `ShallowClone`
// runtime-type preservation (not sliced) plus shared-ownership clone plus
// distinct-instance, the polymorphic `ClassName()` dispatch through a `ResolveResult*`
// base pointer, and the `is_base_of` / not-`final` (the C# class is unsealed) class-shape
// static-asserts.

#include "Decompiler/Semantics/InitializedObjectResolveResult.hpp"
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

// A concrete `IType`: `KnownType(String)` whose `ReflectionName()` is "System.String"
// and whose `Kind()` is `TypeKind::Class`. Used as the initialized object's type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// A second distinct `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used to prove the ctor stores the configured type (not a
// hardcoded one).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

TEST(InitializedObjectResolveResultTest, ConstructorStoresType)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    EXPECT_EQ(&iorr.Type(), type.get());
}

TEST(InitializedObjectResolveResultTest, ConstructorStoresDistinctType)
{
    // The ctor stores the configured type (not a hardcoded one): a second distinct
    // type yields a distinct Type() return.
    auto objectType = MakeObjectType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(objectType);
    EXPECT_EQ(&iorr.Type(), objectType.get());
    EXPECT_EQ(iorr.Type().ReflectionName(), "System.Object");
}

TEST(InitializedObjectResolveResultTest, InheritedDefaultsArePreserved)
{
    // InitializedObjectResolveResult does NOT override IsError / IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold. An initialized object is not an error, not a compile-time constant.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    EXPECT_FALSE(iorr.IsError());
    EXPECT_FALSE(iorr.IsCompileTimeConstant());
    EXPECT_FALSE(iorr.ConstantValue().has_value());
    EXPECT_TRUE(iorr.GetChildResults().empty());
}

TEST(InitializedObjectResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# ToString (inherited from ResolveResult) uses GetType().Name which is
    // polymorphic and yields "InitializedObjectResolveResult". The C++ port
    // reproduces this via the ClassName() override so the inherited ToString reports
    // the subclass name (not the base "ResolveResult").
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    EXPECT_EQ(iorr.ToString(), "[InitializedObjectResolveResult System.String]");
}

TEST(InitializedObjectResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The ClassName() override dispatches through a ResolveResult* base pointer (the
    // virtual dispatch the C# resolver relies on in ToString via GetType().Name):
    // the base pointer's ToString reports the subclass name, not the base.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &iorr;
    EXPECT_EQ(base->ToString(), "[InitializedObjectResolveResult System.String]");
}

TEST(InitializedObjectResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned InitializedObjectResolveResult stays an
    // InitializedObjectResolveResult (not sliced to the ResolveResult base). The C++
    // override reproduces this: the clone is an InitializedObjectResolveResult
    // (dynamic_cast succeeds), not a sliced ResolveResult.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    auto clone = iorr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::InitializedObjectResolveResult*>(clone.get()),
              nullptr);
}

TEST(InitializedObjectResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the IType via the shared_ptr member (faithful to
    // MemberwiseClone's reference copy): the same IType object backs both the
    // original and the clone.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    auto clone = iorr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &iorr.Type());
    EXPECT_EQ(clone->Type().ReflectionName(), "System.String");
}

TEST(InitializedObjectResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult iorr(type);
    auto clone = iorr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &iorr);
}

TEST(InitializedObjectResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::InitializedObjectResolveResult>,
                  "InitializedObjectResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::InitializedObjectResolveResult>,
                  "InitializedObjectResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::InitializedObjectResolveResult>,
                  "InitializedObjectResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `InitializedObjectResolveResult` is NOT sealed (no subclass derives from
    // it but it is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::InitializedObjectResolveResult>,
                  "InitializedObjectResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
