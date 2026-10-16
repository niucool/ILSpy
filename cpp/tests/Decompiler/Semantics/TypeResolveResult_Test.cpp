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

// Tests for `TypeResolveResult` (cpp/Decompiler/Semantics/TypeResolveResult.hpp, the
// D425 port of ICSharpCode.Decompiler/Semantics/TypeResolveResult.cs) -- the resolved
// expression refers to a type name. `TypeResolveResult` is the simplest `ResolveResult`
// (D424) subclass: the C# source declares only the forwarding ctor and the `IsError`
// override (`this.Type.Kind == TypeKind.Unknown`); the C++ port additionally overrides
// `ClassName()` (the polymorphic `GetType().Name` crux in the inherited `ToString`) and
// `ShallowClone()` (the runtime-type-preserving clone, avoiding the C++-only slicing).
//
// The tests pin the ctor-stores-type contract, the `IsError` Kind-based discrimination
// (false for a non-Unknown kind like `KnownType(String)` whose `Kind` is `Class`; true
// for an Unknown kind -- both the `UnknownType()` null object and a `SimpleType` whose
// `Kind` is `TypeKind::Unknown`, proving the override keys on `Kind` not on the concrete
// `IType` type), the polymorphic dispatch of `IsError` through a `ResolveResult*` base
// pointer, the `ToString` subclass-class-name format, the `ShallowClone` runtime-type
// preservation (not sliced) plus shared-ownership clone, the inherited base defaults the
// subclass does NOT override, and the `is_base_of` / not-`final` (the C# class is
// unsealed; `AmbiguousTypeResolveResult` subclasses it) class-shape static_asserts.

#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A non-Unknown kind `IType`: `KnownType(String)` whose `Kind()` is `TypeKind::Class`
// (per the known-type table). `IsError` must be `false` for this.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// The Unknown-kind null object: the `UnknownType()` convenience function returns a
// `SpecialType(TypeKind::Unknown)` whose `Kind()` is `TypeKind::Unknown`. `IsError`
// must be `true` for this (the canonical unknown-type the resolver produces).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeUnknownNullObject()
{
    return ILSpy::Decompiler::TypeSystem::UnknownType();
}

// A second Unknown-kind `IType` (distinct concrete type from the null object): a
// `SimpleType` constructed with `TypeKind::Unknown` explicitly. This proves the
// `IsError` override keys on `Kind() == TypeKind::Unknown`, NOT on the concrete
// `IType` type identity -- any `IType` whose `Kind()` is `Unknown` is an error.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeUnknownKindSimpleType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::SimpleType>(
        ILSpy::Decompiler::TypeSystem::TopLevelTypeName("System", "Foo", 0),
        ILSpy::Decompiler::TypeSystem::TypeKind::Unknown);
}

} // namespace

TEST(TypeResolveResultTest, ConstructorStoresType)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    EXPECT_EQ(&trr.Type(), type.get());
}

TEST(TypeResolveResultTest, IsErrorIsFalseForNonUnknownType)
{
    // KnownType(String) has Kind == TypeKind::Class (not Unknown), so IsError is false.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    EXPECT_FALSE(trr.IsError());
}

TEST(TypeResolveResultTest, IsErrorIsTrueForUnknownNullObject)
{
    // The UnknownType() null object has Kind == TypeKind::Unknown, so IsError is true.
    auto type = MakeUnknownNullObject();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    EXPECT_TRUE(trr.IsError());
}

TEST(TypeResolveResultTest, IsErrorIsTrueForUnknownKindSimpleType)
{
    // A SimpleType with Kind == TypeKind::Unknown is also an error -- the override
    // keys on Kind, not on the concrete IType type identity.
    auto type = MakeUnknownKindSimpleType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    EXPECT_TRUE(trr.IsError());
}

TEST(TypeResolveResultTest, IsErrorDispatchesThroughBasePointer)
{
    // The IsError override dispatches through a ResolveResult* base pointer (the
    // virtual dispatch the C# resolver relies on): the unknown-kind type is an
    // error, the class-kind type is not.
    auto unknownType = MakeUnknownNullObject();
    ILSpy::Decompiler::Semantics::TypeResolveResult trrUnknown(unknownType);
    ILSpy::Decompiler::Semantics::ResolveResult* baseUnknown = &trrUnknown;
    EXPECT_TRUE(baseUnknown->IsError());

    auto stringType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trrString(stringType);
    ILSpy::Decompiler::Semantics::ResolveResult* baseString = &trrString;
    EXPECT_FALSE(baseString->IsError());
}

TEST(TypeResolveResultTest, ToStringReportsTypeResolveResultClassName)
{
    // The C# ToString (inherited from ResolveResult) uses GetType().Name which is
    // polymorphic and yields "TypeResolveResult". The C++ port reproduces this via
    // the ClassName() override so the inherited ToString reports the subclass name
    // (not the base "ResolveResult").
    auto stringType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trrString(stringType);
    EXPECT_EQ(trrString.ToString(), "[TypeResolveResult System.String]");

    // The UnknownType() null object's ReflectionName is "?" (the SpecialType Name).
    auto unknownType = MakeUnknownNullObject();
    ILSpy::Decompiler::Semantics::TypeResolveResult trrUnknown(unknownType);
    EXPECT_EQ(trrUnknown.ToString(), "[TypeResolveResult ?]");
}

TEST(TypeResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned TypeResolveResult stays a TypeResolveResult (not sliced to
    // the ResolveResult base). The C++ override reproduces this: the clone is a
    // TypeResolveResult (dynamic_cast succeeds), not a sliced ResolveResult.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::TypeResolveResult*>(clone.get()),
              nullptr);
}

TEST(TypeResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the IType via the shared_ptr member (faithful to
    // MemberwiseClone's reference copy): the same IType object backs both the
    // original and the clone.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &trr.Type());
    EXPECT_EQ(clone->Type().ReflectionName(), "System.String");
}

TEST(TypeResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &trr);
}

TEST(TypeResolveResultTest, InheritedDefaultsArePreserved)
{
    // TypeResolveResult does NOT override IsCompileTimeConstant / ConstantValue /
    // GetChildResults, so the inherited ResolveResult base defaults hold.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    EXPECT_FALSE(trr.IsCompileTimeConstant());
    EXPECT_FALSE(trr.ConstantValue().has_value());
    EXPECT_TRUE(trr.GetChildResults().empty());
}

TEST(TypeResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::TypeResolveResult>,
                  "TypeResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::TypeResolveResult>,
                  "TypeResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::TypeResolveResult>,
                  "TypeResolveResult is polymorphic (the IsError/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `TypeResolveResult` is NOT sealed (`AmbiguousTypeResolveResult` subclasses
    // it in Semantics/AmbiguousResolveResult.cs), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::TypeResolveResult>,
                  "TypeResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
