// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `TypeIsResolveResult` (cpp/Decompiler/Semantics/TypeIsResolveResult.hpp,
// the D431 port of ICSharpCode.Decompiler/Semantics/TypeIsResolveResult.cs) -- the
// resolved expression is a C# `is` expression ("Input is TargetType").
// `TypeIsResolveResult` is the seventh `Semantics` leaf: the C# source declares a
// forwarding ctor taking a `ResolveResult input`, an `IType targetType`, and an
// `IType booleanType` (the latter forwarded to the base as the expression's own
// type), with `ArgumentNullException` null guards on `input` and `targetType`; it
// exposes the two stored fields as `public readonly` fields (`Input` and
// `TargetType`) and does NOT override any `ResolveResult` virtual. The C++ port
// additionally overrides `ClassName()` (the polymorphic `GetType().Name` crux in
// the inherited `ToString`) and `ShallowClone()` (the runtime-type-preserving
// clone, avoiding the C++-only slicing).
//
// The tests pin the ctor-stores-all-fields contract (the base `Type()` is the
// forwarded `booleanType`; the own `Input()` is the stored operand; the own
// `TargetType()` is the distinct stored `targetType`), the `TargetType`-distinct-
// from-`Type` crux (the load-bearing behavior the `is` result carries a second
// type beyond the base), the `ToString` subclass class-name format, the
// `ShallowClone` runtime-type preservation (not sliced) plus shared-ownership clone
// of ALL three fields (the base `booleanType`, the `input` `ResolveResult`, and the
// `targetType`), the inherited base defaults the subclass does NOT override, and
// the `is_base_of` / not-`final` (the C# class is unsealed and no C# subclass
// derives from it) class-shape static_asserts.

#include "Decompiler/Semantics/TypeIsResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete minimal-port `IType`: `KnownType(Boolean)` whose `ReflectionName()` is
// "System.Boolean". Used as the `booleanType` (the expression's own type, forwarded
// to the `ResolveResult` base) in the tests -- the conventional type of a C# `is`
// expression is `bool`.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeBooleanType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Boolean);
}

// A concrete minimal-port `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used as the operand's type (the left-hand side of the `is`) in
// the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// A concrete minimal-port `IType`: `KnownType(String)` whose `ReflectionName()` is
// "System.String". Used as the `targetType` (the type the operand is compared
// against) in the tests, distinct from the operand's `System.Object` so the
// `TargetType`-distinct-from-operand crux is exercised.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// A concrete `ResolveResult` operand wrapping `KnownType(Object)`: a
// `TypeResolveResult` (the D425 port) used as the `Input` of the `is` expression in
// the tests. Held as a `shared_ptr<ResolveResult>` so it can be shared with the
// `TypeIsResolveResult` (the D428 shared-ownership model) and reach-tested for
// pointer-identity after a `ShallowClone`.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> MakeObjectOperand()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType());
}

} // namespace

TEST(TypeIsResolveResultTest, ConstructorStoresAllFields)
{
    // The C# ctor `TypeIsResolveResult(ResolveResult input, IType targetType,
    // IType booleanType) : base(booleanType)` forwards `booleanType` to the
    // `ResolveResult` base and stores `input` and `targetType`. The C++ port
    // mirrors this: `Type()` returns the forwarded `booleanType`
    // (pointer-identity), `Input()` returns the stored `input`
    // (pointer-identity), and `TargetType()` returns the stored `targetType`
    // (pointer-identity).
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_EQ(&tirr.Type(), booleanType.get());
    EXPECT_EQ(tirr.Input(), input.get());
    EXPECT_EQ(&tirr.TargetType(), targetType.get());
}

TEST(TypeIsResolveResultTest, BooleanTypeIsForwardedToBase)
{
    // The `booleanType` is the `ResolveResult` base `Type()` (the `is`
    // expression's own type, conventionally `bool`); it is NOT the `targetType`
    // nor the operand's type.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_EQ(tirr.Type().ReflectionName(), "System.Boolean");
}

TEST(TypeIsResolveResultTest, InputReturnsConfiguredOperand)
{
    // The C# `public readonly ResolveResult Input` returns the ctor-stored
    // `input` (the resolved left-hand operand). The C++ port mirrors this:
    // `Input()` returns the stored `input` `ResolveResult` by pointer-identity.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_EQ(tirr.Input(), input.get());
    EXPECT_EQ(tirr.Input()->Type().ReflectionName(), "System.Object");
}

TEST(TypeIsResolveResultTest, TargetTypeReturnsConfiguredType)
{
    // The C# `public readonly IType TargetType` returns the ctor-stored
    // `targetType` (the type compared against). The C++ port mirrors this:
    // `TargetType()` returns the stored `targetType` (here `System.String`).
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_EQ(tirr.TargetType().ReflectionName(), "System.String");
}

TEST(TypeIsResolveResultTest, TargetTypeIsDistinctFromBaseType)
{
    // The load-bearing crux: `TargetType` returns the stored `targetType`,
    // DISTINCT from the base `Type()` (the `booleanType`). A `obj is string`
    // expression's `Type()` is `bool` (here `System.Boolean`) while its
    // `TargetType()` is `System.String`; the two are different objects and
    // different reflection names. (Neutering `TargetType` to `return Type()` would
    // make this test fail -- the crux-targeted RED.)
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_NE(&tirr.TargetType(), &tirr.Type());
    EXPECT_NE(tirr.TargetType().ReflectionName(), tirr.Type().ReflectionName());
}

TEST(TypeIsResolveResultTest, InputAndTargetTypeAreDistinctMembers)
{
    // The two own members (`input_` a `ResolveResult` and `targetType_` an
    // `IType`) are distinct objects: `Input()` returns the operand
    // `ResolveResult` (whose `Type()` is `System.Object`), `TargetType()` returns
    // the compared-against `IType` (`System.String`); neither aliases the other.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    static_cast<void>(booleanType);
    EXPECT_NE(tirr.Input()->Type().ReflectionName(), tirr.TargetType().ReflectionName());
    EXPECT_NE(reinterpret_cast<const void*>(tirr.Input()),
              reinterpret_cast<const void*>(&tirr.TargetType()));
}

TEST(TypeIsResolveResultTest, ToStringReportsTypeIsResolveResultClassName)
{
    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "TypeIsResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "ResolveResult"). The format is
    // "[<class-name> <booleanType reflection name>]" -- the base `Type()` is the
    // `booleanType` (here `System.Boolean`).
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_EQ(tirr.ToString(), "[TypeIsResolveResult System.Boolean]");
}

TEST(TypeIsResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `TypeIsResolveResult` stays a
    // `TypeIsResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this: the clone is a `TypeIsResolveResult`
    // (`dynamic_cast` succeeds), not a sliced `ResolveResult`.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    auto clone = tirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::TypeIsResolveResult*>(clone.get()),
              nullptr);
}

TEST(TypeIsResolveResultTest, ShallowCloneSharesAllFields)
{
    // The clone shares ALL three fields via the `shared_ptr` members (faithful to
    // `MemberwiseClone`'s reference copy of the base `booleanType`, the `input`
    // `ResolveResult`, and the own `targetType`): the same `booleanType`,
    // `input`, and `targetType` objects back both the original and the clone. The
    // clone's `Input()` and `TargetType()` are reached by downcasting the
    // `unique_ptr<ResolveResult>` to a `TypeIsResolveResult*`.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    auto clone = tirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &tirr.Type());
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::TypeIsResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->Input(), input.get());
    EXPECT_EQ(cloned->Input(), tirr.Input());
    EXPECT_EQ(&cloned->TargetType(), targetType.get());
    EXPECT_EQ(&cloned->TargetType(), &tirr.TargetType());
}

TEST(TypeIsResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    auto clone = tirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &tirr);
}

TEST(TypeIsResolveResultTest, InheritedDefaultsArePreserved)
{
    // `TypeIsResolveResult` does NOT override `IsError` /
    // `IsCompileTimeConstant` / `ConstantValue` / `GetChildResults`, so the
    // inherited `ResolveResult` base defaults hold (an `is` expression is not an
    // error, not a compile-time constant). Note the C# does NOT override
    // `GetChildResults` (the `Input` is a `public readonly` field, NOT exposed as
    // a child result), so the inherited empty default holds too.
    auto input = MakeObjectOperand();
    auto targetType = MakeStringType();
    auto booleanType = MakeBooleanType();
    ILSpy::Decompiler::Semantics::TypeIsResolveResult tirr(input, targetType, booleanType);
    EXPECT_FALSE(tirr.IsError());
    EXPECT_FALSE(tirr.IsCompileTimeConstant());
    EXPECT_FALSE(tirr.ConstantValue().has_value());
    EXPECT_TRUE(tirr.GetChildResults().empty());
}

TEST(TypeIsResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::TypeIsResolveResult>,
                  "TypeIsResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::TypeIsResolveResult>,
                  "TypeIsResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::TypeIsResolveResult>,
                  "TypeIsResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `TypeIsResolveResult` is NOT sealed and no C# subclass derives from
    // it, so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::TypeIsResolveResult>,
                  "TypeIsResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
