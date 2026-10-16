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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ThisResolveResult` (cpp/Decompiler/Semantics/ThisResolveResult.hpp, the
// D426 port of ICSharpCode.Decompiler/Semantics/ThisResolveResult.cs) -- the
// resolved expression is the 'this' reference (also used for the 'base' reference).
// `ThisResolveResult` is the third `Semantics` leaf: the C# source declares only the
// forwarding ctor (with a defaulted `causesNonVirtualInvocation` bool) and the
// `CausesNonVirtualInvocation` property getter; the C++ port additionally overrides
// `ClassName()` (the polymorphic `GetType().Name` crux in the inherited `ToString`)
// and `ShallowClone()` (the runtime-type-preserving clone, avoiding the C++-only
// slicing).
//
// The tests pin the ctor-stores-type contract, the defaulted-bool ctor argument
// (`CausesNonVirtualInvocation` defaults to `false` and flips to `true` when
// passed), the `ToString` subclass-class-name format, the `ShallowClone`
// runtime-type preservation (not sliced) plus shared-ownership clone (and the
// `causesNonVirtualInvocation` bool is carried over), the inherited base defaults
// the subclass does NOT override, and the `is_base_of` / not-`final` (the C# class
// is unsealed and no C# subclass derives from it) class-shape static_asserts.

#include "Decompiler/Semantics/ThisResolveResult.hpp"
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

// A concrete minimal-port `IType`: `KnownType(String)` whose `ReflectionName()` is
// "System.String". Used as the `this` reference's type in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

} // namespace

TEST(ThisResolveResultTest, ConstructorStoresType)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    EXPECT_EQ(&trr.Type(), type.get());
}

TEST(ThisResolveResultTest, CausesNonVirtualInvocationDefaultsToFalse)
{
    // The C# ctor's defaulted `causesNonVirtualInvocation = false` argument: a
    // `ThisResolveResult(type)` construction (the common 'this' case -- virtual
    // invocation) defaults the bool to false.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    EXPECT_FALSE(trr.CausesNonVirtualInvocation());
}

TEST(ThisResolveResultTest, CausesNonVirtualInvocationTrueWhenPassed)
{
    // The 'base' reference case: passing `true` (non-virtual invocation) stores
    // the bool and the accessor returns it.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type, true);
    EXPECT_TRUE(trr.CausesNonVirtualInvocation());
}

TEST(ThisResolveResultTest, CausesNonVirtualInvocationFalseWhenExplicitlyPassed)
{
    // Passing `false` explicitly is equivalent to the default -- the bool is
    // stored as false.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type, false);
    EXPECT_FALSE(trr.CausesNonVirtualInvocation());
}

TEST(ThisResolveResultTest, ToStringReportsThisResolveResultClassName)
{
    // The C# ToString (inherited from ResolveResult) uses GetType().Name which is
    // polymorphic and yields "ThisResolveResult". The C++ port reproduces this via
    // the ClassName() override so the inherited ToString reports the subclass name
    // (not the base "ResolveResult").
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    EXPECT_EQ(trr.ToString(), "[ThisResolveResult System.String]");
}

TEST(ThisResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned ThisResolveResult stays a ThisResolveResult (not sliced to
    // the ResolveResult base). The C++ override reproduces this: the clone is a
    // ThisResolveResult (dynamic_cast succeeds), not a sliced ResolveResult.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type, true);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::ThisResolveResult*>(clone.get()),
              nullptr);
}

TEST(ThisResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the IType via the shared_ptr member (faithful to
    // MemberwiseClone's reference copy): the same IType object backs both the
    // original and the clone.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &trr.Type());
    EXPECT_EQ(clone->Type().ReflectionName(), "System.String");
}

TEST(ThisResolveResultTest, ShallowCloneCarriesCausesNonVirtualInvocation)
{
    // The clone copies the `causesNonVirtualInvocation` bool by value (the C#
    // MemberwiseClone copies value fields). The `true` flag survives the clone.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type, true);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::ThisResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_TRUE(cloned->CausesNonVirtualInvocation());
}

TEST(ThisResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &trr);
}

TEST(ThisResolveResultTest, InheritedDefaultsArePreserved)
{
    // ThisResolveResult does NOT override IsError / IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold (a 'this' reference is not an error, not a compile-time constant).
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::ThisResolveResult trr(type);
    EXPECT_FALSE(trr.IsError());
    EXPECT_FALSE(trr.IsCompileTimeConstant());
    EXPECT_FALSE(trr.ConstantValue().has_value());
    EXPECT_TRUE(trr.GetChildResults().empty());
}

TEST(ThisResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::ThisResolveResult>,
                  "ThisResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ThisResolveResult>,
                  "ThisResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ThisResolveResult>,
                  "ThisResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `ThisResolveResult` is NOT sealed and no C# subclass derives from it,
    // so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::ThisResolveResult>,
                  "ThisResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
