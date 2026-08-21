// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ErrorResolveResult` (cpp/Decompiler/Semantics/ErrorResolveResult.hpp,
// the D436 port of ICSharpCode.Decompiler/Semantics/ErrorResolveResult.cs) -- the
// `ResolveResult` (D424) that represents a resolve error. The C# source declares
// two ctors (1-arg forwarding, 3-arg with a diagnostic message + source location),
// the `IsError => true` override (the load-bearing crux distinguishing an error
// result from every other `ResolveResult`), the `Message` / `Location` properties,
// and the `UnknownError` singleton (with `Type` = `SpecialType.UnknownType`).
//
// The tests pin the ctor-stores-type-and-diagnostic contract (both ctors), the
// `IsError` always-true override (the crux, distinct from the base default `false`
// and the `TypeResolveResult` D425 conditional), the virtual dispatch of `IsError`
// through a `ResolveResult*` base pointer, the `Message` / `Location` defaults
// (empty / `Empty`) for the 1-arg ctor and configured values for the 3-arg ctor,
// the `UnknownError` singleton (a `TypeKind::Unknown` null object), the `ToString`
// subclass-class-name format, the `ShallowClone` runtime-type preservation plus
// shared-ownership of the base `type` and value-copy of the `Message` / `Location`
// fields, the inherited base defaults the subclass does NOT override, and the
// `is_base_of` / not-`final` (the C# class is unsealed) class-shape static-asserts.

#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete minimal-port `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used as the error type in the tests (an error result's type is
// the type the resolver was trying to resolve to, or `UnknownType` when the type
// itself is unknown).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

TEST(ErrorResolveResultTest, OneArgConstructorForwardsTypeToBase)
{
    // The C# `ErrorResolveResult(IType type) : base(type)` -- forwards the type to
    // the `ResolveResult` base. The C++ port mirrors this: `Type()` returns the
    // forwarded `type` (pointer-identity).
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(type);
    EXPECT_EQ(&err.Type(), type.get());
}

TEST(ErrorResolveResultTest, ThreeArgConstructorStoresMessageAndLocation)
{
    // The C# `ErrorResolveResult(IType type, string message, TextLocation
    // location)` -- forwards the type to the base AND stores the diagnostic
    // `message` and source `location`. The C++ port mirrors this: `Type()` is the
    // forwarded type, `Message()` is the stored string, `Location()` is the stored
    // `TextLocation`.
    auto type = MakeObjectType();
    ILSpy::Decompiler::CSharp::Syntax::TextLocation loc(3, 7);
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(type, "some error", loc);
    EXPECT_EQ(&err.Type(), type.get());
    EXPECT_EQ(err.Message(), "some error");
    EXPECT_EQ(err.Location().Line, 3);
    EXPECT_EQ(err.Location().Column, 7);
}

TEST(ErrorResolveResultTest, IsErrorIsAlwaysTrue)
{
    // The C# `override bool IsError => true` -- the load-bearing crux: an error
    // result is ALWAYS an error, unconditionally (regardless of the stored type or
    // whether a message/location was supplied). This is distinct from the base
    // default `false` (D424) and from `TypeResolveResult` (D425) which returns
    // `true` only when `Type().Kind() == TypeKind::Unknown`.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err1(MakeObjectType());
    ILSpy::Decompiler::Semantics::ErrorResolveResult err2(
        MakeObjectType(), "msg", ILSpy::Decompiler::CSharp::Syntax::TextLocation(1, 1));
    EXPECT_TRUE(err1.IsError());
    EXPECT_TRUE(err2.IsError());
}

TEST(ErrorResolveResultTest, IsErrorDispatchesThroughBasePointer)
{
    // The `IsError` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the resolver relies on to detect a failed
    // resolution): the base pointer's `IsError()` returns `true`, not the base
    // default `false`.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    ILSpy::Decompiler::Semantics::ResolveResult* base = &err;
    EXPECT_TRUE(base->IsError());
}

TEST(ErrorResolveResultTest, MessageDefaultsToEmptyForOneArgCtor)
{
    // The C# `Message` property defaults to `null` for the 1-arg ctor (no
    // diagnostic). The C++ port mirrors this: `Message()` returns the empty
    // `std::string` (the C# `null`-to-empty-`std::string` convention).
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    EXPECT_TRUE(err.Message().empty());
}

TEST(ErrorResolveResultTest, LocationDefaultsToEmptyForOneArgCtor)
{
    // The C# `Location` property defaults to `default(TextLocation)` =
    // `TextLocation.Empty` = (0, 0) for the 1-arg ctor (no source location). The
    // C++ `TextLocation()` default ctor reproduces `Empty` = (0, 0).
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    EXPECT_EQ(err.Location().Line, 0);
    EXPECT_EQ(err.Location().Column, 0);
    EXPECT_TRUE(err.Location().IsEmpty());
}

TEST(ErrorResolveResultTest, MessageReturnsConfiguredValue)
{
    // The 3-arg ctor stores the `message` and `Message()` returns it verbatim.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(
        MakeObjectType(), "type not found", ILSpy::Decompiler::CSharp::Syntax::TextLocation());
    EXPECT_EQ(err.Message(), "type not found");
}

TEST(ErrorResolveResultTest, LocationReturnsConfiguredValue)
{
    // The 3-arg ctor stores the `location` and `Location()` returns it verbatim
    // (the `TextLocation` value struct is returned by value, faithful to the C#
    // property which returns the struct by value).
    ILSpy::Decompiler::CSharp::Syntax::TextLocation loc(10, 20);
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType(), "", loc);
    EXPECT_EQ(err.Location().Line, 10);
    EXPECT_EQ(err.Location().Column, 20);
}

TEST(ErrorResolveResultTest, UnknownErrorSingletonReturnsUnknownType)
{
    // The C# `public static readonly ErrorResolveResult UnknownError` singleton is
    // constructed with `SpecialType.UnknownType` (the `TypeKind::Unknown` null
    // object). The C++ port uses the D417 `UnknownType()` convenience, so
    // `UnknownError().Type().Kind()` is `TypeKind::Unknown` and
    // `ReflectionName()` is "?" (the `SpecialType::Name()` switch for
    // `TypeKind::Unknown`).
    const auto& err = ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
    EXPECT_EQ(err.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Unknown);
    EXPECT_EQ(err.Type().ReflectionName(), "?");
}

TEST(ErrorResolveResultTest, UnknownErrorIsAnError)
{
    // The `UnknownError` singleton is an `ErrorResolveResult`, so `IsError()` is
    // `true` (the `IsError => true` override).
    const auto& err = ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
    EXPECT_TRUE(err.IsError());
}

TEST(ErrorResolveResultTest, UnknownErrorIsStableSingleton)
{
    // The `UnknownError` accessor returns the SAME instance each call (the
    // Meyers-singleton pattern, the D398 `StringComparer::Ordinal` precedent):
    // two calls yield pointer-identical results.
    const auto& a = ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
    const auto& b = ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
    EXPECT_EQ(&a, &b);
}

TEST(ErrorResolveResultTest, UnknownErrorHasNoMessageOrLocation)
{
    // The `UnknownError` singleton uses the 1-arg ctor (no message/location), so
    // `Message()` is empty and `Location()` is `Empty` = (0, 0).
    const auto& err = ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
    EXPECT_TRUE(err.Message().empty());
    EXPECT_TRUE(err.Location().IsEmpty());
}

TEST(ErrorResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "ErrorResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "ResolveResult"). The `{type}` field
    // is the `KnownType(Object)` `ReflectionName` "System.Object".
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    EXPECT_EQ(err.ToString(), "[ErrorResolveResult System.Object]");
}

TEST(ErrorResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The `ClassName()` override dispatches through a `ResolveResult*` base
    // pointer (the virtual dispatch the C# resolver relies on in `ToString` via
    // `GetType().Name`): the base pointer's `ToString` reports the subclass name,
    // not the base.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    ILSpy::Decompiler::Semantics::ResolveResult* base = &err;
    EXPECT_EQ(base->ToString(), "[ErrorResolveResult System.Object]");
}

TEST(ErrorResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `ErrorResolveResult` stays an `ErrorResolveResult`
    // (not sliced to the `ResolveResult` base). The C++ override reproduces this:
    // the clone is an `ErrorResolveResult` (`dynamic_cast` succeeds).
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    auto clone = err.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::ErrorResolveResult*>(clone.get()),
              nullptr);
}

TEST(ErrorResolveResultTest, ShallowCloneSharesType)
{
    // The clone SHARES the base `type_` `shared_ptr` (faithful to the C#
    // `MemberwiseClone` reference-copy of the `IType`): the same `IType` object
    // backs both the original and the clone, so `&clone->Type() == &err.Type()`.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    auto clone = err.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &err.Type());
}

TEST(ErrorResolveResultTest, ShallowCloneCopiesMessageAndLocation)
{
    // The clone COPIES the `message_` `std::string` and `location_` `TextLocation`
    // value members (faithful to the C# `MemberwiseClone` value-copy of the
    // `string` and `TextLocation` fields, distinct from the shared `shared_ptr`
    // `type_` field). The clone's `Message()` / `Location()` are equal to the
    // original's, and the clone's `IsError()` is still `true` (the override is
    // preserved through the clone).
    ILSpy::Decompiler::CSharp::Syntax::TextLocation loc(5, 9);
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(
        MakeObjectType(), "diagnostic", loc);
    auto clone = err.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::ErrorResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->Message(), "diagnostic");
    EXPECT_EQ(cloned->Location().Line, 5);
    EXPECT_EQ(cloned->Location().Column, 9);
    EXPECT_TRUE(cloned->IsError());
}

TEST(ErrorResolveResultTest, ShallowCloneIsDistinctInstance)
{
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    auto clone = err.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &err);
}

TEST(ErrorResolveResultTest, InheritedDefaultsArePreserved)
{
    // `ErrorResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `GetChildResults`, so the inherited `ResolveResult` base
    // defaults hold (an error is not a compile-time constant, has no constant
    // value, has no child results). It DOES override `IsError` (true), exercised
    // by the dedicated tests above.
    ILSpy::Decompiler::Semantics::ErrorResolveResult err(MakeObjectType());
    EXPECT_FALSE(err.IsCompileTimeConstant());
    EXPECT_FALSE(err.ConstantValue().has_value());
    EXPECT_TRUE(err.GetChildResults().empty());
}

TEST(ErrorResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::ErrorResolveResult>,
                  "ErrorResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ErrorResolveResult>,
                  "ErrorResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ErrorResolveResult>,
                  "ErrorResolveResult is polymorphic (the IsError/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `ErrorResolveResult` is NOT sealed (no C# subclass derives from it
    // but it is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::ErrorResolveResult>,
                  "ErrorResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
