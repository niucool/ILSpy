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

// Tests for `ThrowResolveResult` (cpp/Decompiler/Semantics/ThrowResolveResult.hpp,
// the D435 port of ICSharpCode.Decompiler/Semantics/ThrowResolveResult.cs) -- the
// `ResolveResult` (D424) for a `throw` expression (the C# 7 `throw`-expression form
// usable in a conditional or assignment context). The C# source declares only the
// parameterless ctor (`ThrowResolveResult() : base(SpecialType.NoType)`); the C++
// port additionally overrides `ClassName()` (the polymorphic `GetType().Name` crux
// in the inherited `ToString`) and `ShallowClone()` (the runtime-type-preserving
// clone, avoiding the C++-only slicing).
//
// The tests pin the `SpecialType.NoType` base-type forwarding crux (`Type().Kind()
// == TypeKind::None`, distinct from `UnknownType()`'s `TypeKind::Unknown`), the
// inherited base defaults the subclass does NOT override (`IsError` /
// `IsCompileTimeConstant` / `ConstantValue` / `GetChildResults`), the `ToString`
// subclass-class-name format (`[ThrowResolveResult None]` -- the `None` is the
// `NoType()` SpecialType's `ReflectionName`), the `ShallowClone` runtime-type
// preservation (not sliced) plus shared-ownership clone plus distinct-instance, the
// polymorphic `ClassName()` dispatch through a `ResolveResult*` base pointer, and
// the `is_base_of` / not-`final` (the C# class is unsealed) class-shape static-asserts.

#include "Decompiler/Semantics/ThrowResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

TEST(ThrowResolveResultTest, ConstructorForwardsNoTypeToBase)
{
    // The C# ctor forwards `SpecialType.NoType` (the `TypeKind::None` null object)
    // to the `ResolveResult` base. The C++ port uses the `NoType()` convenience
    // (the D433 prerequisite this leaf consumes), so `Type().Kind()` is
    // `TypeKind::None` (no type at all -- a `throw` expression never yields a
    // value), DISTINCT from `UnknownType()`'s `TypeKind::Unknown` (the error-type
    // null object).
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    EXPECT_EQ(trr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
}

TEST(ThrowResolveResultTest, TypeReflectionNameIsNoTypeName)
{
    // The `NoType()` SpecialType's `ReflectionName()` is its `Name()` which the
    // `SpecialType::Name()` switch returns as "None" for `TypeKind::None` (distinct
    // from `UnknownType()`'s "?" for `TypeKind::Unknown`).
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    EXPECT_EQ(trr.Type().ReflectionName(), "None");
}

TEST(ThrowResolveResultTest, InheritedDefaultsArePreserved)
{
    // ThrowResolveResult does NOT override IsError / IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold. A throw expression is not an error, not a compile-time constant.
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    EXPECT_FALSE(trr.IsError());
    EXPECT_FALSE(trr.IsCompileTimeConstant());
    EXPECT_FALSE(trr.ConstantValue().has_value());
    EXPECT_TRUE(trr.GetChildResults().empty());
}

TEST(ThrowResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# ToString (inherited from ResolveResult) uses GetType().Name which is
    // polymorphic and yields "ThrowResolveResult". The C++ port reproduces this via
    // the ClassName() override so the inherited ToString reports the subclass name
    // (not the base "ResolveResult"). The {type} field is the NoType() SpecialType's
    // ReflectionName "None".
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    EXPECT_EQ(trr.ToString(), "[ThrowResolveResult None]");
}

TEST(ThrowResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The ClassName() override dispatches through a ResolveResult* base pointer (the
    // virtual dispatch the C# resolver relies on in ToString via GetType().Name):
    // the base pointer's ToString reports the subclass name, not the base.
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    ILSpy::Decompiler::Semantics::ResolveResult* base = &trr;
    EXPECT_EQ(base->ToString(), "[ThrowResolveResult None]");
}

TEST(ThrowResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned ThrowResolveResult stays a ThrowResolveResult (not sliced to
    // the ResolveResult base). The C++ override reproduces this: the clone is a
    // ThrowResolveResult (dynamic_cast succeeds), not a sliced ResolveResult.
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::ThrowResolveResult*>(clone.get()),
              nullptr);
}

TEST(ThrowResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the NoType() SpecialType via the shared_ptr member (faithful
    // to MemberwiseClone's reference copy): the same IType object backs both the
    // original and the clone, so both report TypeKind::None and ReflectionName "None".
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &trr.Type());
    EXPECT_EQ(clone->Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
    EXPECT_EQ(clone->Type().ReflectionName(), "None");
}

TEST(ThrowResolveResultTest, ShallowCloneIsDistinctInstance)
{
    ILSpy::Decompiler::Semantics::ThrowResolveResult trr;
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &trr);
}

TEST(ThrowResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::ThrowResolveResult>,
                  "ThrowResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ThrowResolveResult>,
                  "ThrowResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ThrowResolveResult>,
                  "ThrowResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `ThrowResolveResult` is NOT sealed (no subclass derives from it but it
    // is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::ThrowResolveResult>,
                  "ThrowResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<ILSpy::Decompiler::Semantics::ThrowResolveResult>,
                  "ThrowResolveResult has a parameterless ctor (forwards NoType to base).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
