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
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `TypeOfResolveResult` (cpp/Decompiler/Semantics/TypeOfResolveResult.hpp,
// the D427 port of ICSharpCode.Decompiler/Semantics/TypeOfResolveResult.cs) -- the
// resolved expression is the `typeof` operator. `TypeOfResolveResult` is the fourth
// `Semantics` leaf: the C# source declares a forwarding ctor taking two `IType`
// arguments (the `systemType` forwarded to the base, and the `referencedType`
// stored with an `ArgumentNullException` null guard) and a non-virtual
// `ReferencedType` property getter; the C++ port additionally overrides
// `ClassName()` (the polymorphic `GetType().Name` crux in the inherited `ToString`)
// and `ShallowClone()` (the runtime-type-preserving clone, avoiding the C++-only
// slicing).
//
// The tests pin the ctor-stores-both-types contract (the base `Type()` is the
// `systemType`; the own `ReferencedType()` is the distinct `referencedType`), the
// `ReferencedType`-distinct-from-`Type` crux (the load-bearing behavior the
// `typeof` result carries a second type beyond the base), the `ToString` subclass
// class-name format, the `ShallowClone` runtime-type preservation (not sliced) plus
// shared-ownership clone of BOTH `IType` fields, the inherited base defaults the
// subclass does NOT override, and the `is_base_of` / not-`final` (the C# class is
// unsealed and no C# subclass derives from it) class-shape static_asserts.

#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
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

// A concrete minimal-port `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used as the `typeof`'s system type (the `System.Type` the
// `typeof` expression evaluates to) in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// A concrete minimal-port `IType`: `KnownType(String)` whose `ReflectionName()` is
// "System.String". Used as the `referencedType` (the type the `typeof` names) in
// the tests, distinct from the system `Object` type so the
// `ReferencedType`-distinct-from-`Type` crux is exercised.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

} // namespace

TEST(TypeOfResolveResultTest, ConstructorStoresBothTypes)
{
    // The C# ctor `TypeOfResolveResult(IType systemType, IType referencedType)
    // : base(systemType)` forwards `systemType` to the `ResolveResult` base and
    // stores `referencedType`. The C++ port mirrors this: `Type()` returns the
    // forwarded `systemType` (pointer-identity), and `ReferencedType()` returns
    // the stored `referencedType` (pointer-identity).
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_EQ(&trr.Type(), systemType.get());
    EXPECT_EQ(&trr.ReferencedType(), referencedType.get());
}

TEST(TypeOfResolveResultTest, SystemTypeIsForwardedToBase)
{
    // The `systemType` is the `ResolveResult` base `Type()` (the `typeof`
    // expression's own type is `System.Type`); it is NOT the `referencedType`.
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_EQ(trr.Type().ReflectionName(), "System.Object");
}

TEST(TypeOfResolveResultTest, ReferencedTypeReturnsConfiguredType)
{
    // The C# `IType ReferencedType { get; }` returns the ctor-stored
    // `referencedType`. The C++ port mirrors this: `ReferencedType()` returns the
    // stored `referencedType` (here `System.String`).
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_EQ(trr.ReferencedType().ReflectionName(), "System.String");
}

TEST(TypeOfResolveResultTest, ReferencedTypeIsDistinctFromSystemType)
{
    // The load-bearing crux: `ReferencedType` returns the stored `referencedType`,
    // DISTINCT from the base `systemType`. A `typeof(string)` expression's
    // `Type()` is `System.Type` (here modeled as `System.Object`) while its
    // `ReferencedType()` is `System.String`; the two are different objects and
    // different reflection names. (Neutering `ReferencedType` to `return Type()`
    // would make this test fail -- the crux-targeted RED.)
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_NE(&trr.ReferencedType(), &trr.Type());
    EXPECT_NE(trr.ReferencedType().ReflectionName(), trr.Type().ReflectionName());
}

TEST(TypeOfResolveResultTest, ToStringReportsTypeOfResolveResultClassName)
{
    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "TypeOfResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "ResolveResult"). The format is
    // "[<class-name> <systemType reflection name>]" -- the base `Type()` is the
    // `systemType` (here `System.Object`).
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_EQ(trr.ToString(), "[TypeOfResolveResult System.Object]");
}

TEST(TypeOfResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `TypeOfResolveResult` stays a
    // `TypeOfResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this: the clone is a `TypeOfResolveResult`
    // (`dynamic_cast` succeeds), not a sliced `ResolveResult`.
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::TypeOfResolveResult*>(clone.get()),
              nullptr);
}

TEST(TypeOfResolveResultTest, ShallowCloneSharesBothTypes)
{
    // The clone shares BOTH `IType` fields via the `shared_ptr` members (faithful
    // to `MemberwiseClone`'s reference copy of both the base `type` and the own
    // `referencedType`): the same `systemType` and `referencedType` objects back
    // both the original and the clone. The clone's `ReferencedType` is reached by
    // downcasting the `unique_ptr<ResolveResult>` to a `TypeOfResolveResult*`.
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &trr.Type());
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::TypeOfResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(&cloned->ReferencedType(), referencedType.get());
    EXPECT_EQ(&cloned->ReferencedType(), &trr.ReferencedType());
}

TEST(TypeOfResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    auto clone = trr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &trr);
}

TEST(TypeOfResolveResultTest, InheritedDefaultsArePreserved)
{
    // `TypeOfResolveResult` does NOT override `IsError` /
    // `IsCompileTimeConstant` / `ConstantValue` / `GetChildResults`, so the
    // inherited `ResolveResult` base defaults hold (a `typeof` expression is not
    // an error, not a compile-time constant).
    auto systemType = MakeObjectType();
    auto referencedType = MakeStringType();
    ILSpy::Decompiler::Semantics::TypeOfResolveResult trr(systemType, referencedType);
    EXPECT_FALSE(trr.IsError());
    EXPECT_FALSE(trr.IsCompileTimeConstant());
    EXPECT_FALSE(trr.ConstantValue().has_value());
    EXPECT_TRUE(trr.GetChildResults().empty());
}

TEST(TypeOfResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::TypeOfResolveResult>,
                  "TypeOfResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::TypeOfResolveResult>,
                  "TypeOfResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::TypeOfResolveResult>,
                  "TypeOfResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `TypeOfResolveResult` is NOT sealed and no C# subclass derives from
    // it, so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::TypeOfResolveResult>,
                  "TypeOfResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
