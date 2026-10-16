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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ConstantResolveResult` (cpp/Decompiler/Semantics/ConstantResolveResult.hpp,
// the D432 port of ICSharpCode.Decompiler/Semantics/ConstantResolveResult.cs) -- the
// resolved expression is a compile-time constant (mainly a literal). The C# source
// declares a ctor taking an `IType` (forwarded to the base) plus an `object
// constantValue` (the boxed compile-time value, which may be `null` for a `null`
// literal), and overrides two `ResolveResult` virtuals (`IsCompileTimeConstant` ->
// `true`, `ConstantValue` -> the stored value) plus a custom `ToString` with the
// format `"[<class-name> <type> = <value>]"`.
//
// The tests pin the ctor-stores-type-and-value contract, the `IsCompileTimeConstant`
// always-true override (distinct from the base default `false` and the
// `SizeOfResolveResult` conditional), the `ConstantValue` boxed-value round-trip
// across the common literal types (int / string / bool / null), the custom
// `ToString` format for each literal type (the `std::any` stringification crux --
// the load-bearing behavior this leaf introduces), the `ShallowClone` runtime-type
// preservation (not sliced) plus shared-ownership of the base `type` and value-copy
// of the boxed `constantValue` (distinct from the shared `shared_ptr` fields of the
// prior siblings -- a `std::any` is a value member), the inherited base defaults the
// subclass does NOT override, and the `is_base_of` / not-`final` class-shape
// static_asserts.

#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete minimal-port `IType`: `KnownType(Int32)` whose `ReflectionName()` is
// "System.Int32". Used as the type of an integer literal constant in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// A concrete minimal-port `IType`: `KnownType(Boolean)` whose `ReflectionName()` is
// "System.Boolean". Used as the type of a boolean literal constant in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeBooleanType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Boolean);
}

// A concrete minimal-port `IType`: `KnownType(String)` whose `ReflectionName()` is
// "System.String". Used as the type of a string literal constant in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// A concrete minimal-port `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used as the type of a `null` literal constant in the tests (a
// `null` literal's type is the target type, conventionally `object`).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

TEST(ConstantResolveResultTest, ConstructorStoresTypeAndConstantValue)
{
    // The C# ctor `ConstantResolveResult(IType type, object constantValue) :
    // base(type)` forwards `type` to the `ResolveResult` base and stores
    // `constantValue`. The C++ port mirrors this: `Type()` returns the forwarded
    // `type` (pointer-identity), and `ConstantValue()` returns the stored boxed
    // value (round-tripped via `std::any_cast`).
    auto type = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(type, std::any(std::int32_t(42)));
    EXPECT_EQ(&crr.Type(), type.get());
    EXPECT_TRUE(crr.ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(crr.ConstantValue()), 42);
}

TEST(ConstantResolveResultTest, IsCompileTimeConstantIsAlwaysTrue)
{
    // The C# `override bool IsCompileTimeConstant => true`: a constant is always
    // a compile-time constant, regardless of the boxed value (here an `int`, a
    // `string`, and a `null` literal all yield `true`). This is distinct from the
    // base default `false` (D424) and from the `SizeOfResolveResult` (D430)
    // `constantValue != null` conditional.
    ILSpy::Decompiler::Semantics::ConstantResolveResult intC(MakeInt32Type(), std::any(std::int32_t(42)));
    ILSpy::Decompiler::Semantics::ConstantResolveResult strC(MakeStringType(), std::any(std::string("hello")));
    ILSpy::Decompiler::Semantics::ConstantResolveResult nullC(MakeObjectType(), std::any());
    EXPECT_TRUE(intC.IsCompileTimeConstant());
    EXPECT_TRUE(strC.IsCompileTimeConstant());
    EXPECT_TRUE(nullC.IsCompileTimeConstant());
}

TEST(ConstantResolveResultTest, ConstantValueReturnsBoxedInt)
{
    // The C# `override object? ConstantValue => constantValue`: the stored boxed
    // value. For an integer literal, the `std::any` holds an `std::int32_t`;
    // `std::any_cast` recovers it. The returned `any` is a COPY (the stored value
    // stays immutable).
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(-7)));
    auto v = crr.ConstantValue();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(v), -7);
}

TEST(ConstantResolveResultTest, ConstantValueReturnsBoxedString)
{
    // For a string literal, the `std::any` holds a `std::string`; `std::any_cast`
    // recovers it (the C# boxed `string`).
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeStringType(), std::any(std::string("hello")));
    auto v = crr.ConstantValue();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::any_cast<std::string>(v), "hello");
}

TEST(ConstantResolveResultTest, ConstantValueReturnsBoxedBool)
{
    // For a boolean literal, the `std::any` holds a `bool`; `std::any_cast`
    // recovers it (the C# boxed `bool`). Both `true` and `false` are stored
    // faithfully.
    ILSpy::Decompiler::Semantics::ConstantResolveResult trueC(MakeBooleanType(), std::any(true));
    ILSpy::Decompiler::Semantics::ConstantResolveResult falseC(MakeBooleanType(), std::any(false));
    EXPECT_EQ(std::any_cast<bool>(trueC.ConstantValue()), true);
    EXPECT_EQ(std::any_cast<bool>(falseC.ConstantValue()), false);
}

TEST(ConstantResolveResultTest, ConstantValueIsNullForNullLiteral)
{
    // The C# ctor does NOT guard `constantValue` with `ArgumentNullException`, so
    // a `null` literal stores a `null` `constantValue`. The C++ port mirrors
    // this: an empty `std::any` is the C# `null`, and `ConstantValue()` returns
    // an empty `any` (no `assert`, no throw). This is the faithful null-handling
    // distinct from the `SizeOfResolveResult` D430 / `TypeOfResolveResult` D427
    // `assert`-guarded `IType` fields.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeObjectType(), std::any());
    EXPECT_FALSE(crr.ConstantValue().has_value());
    // A `null` literal is still a compile-time constant (the C# IsCompileTimeConstant is unconditional).
    EXPECT_TRUE(crr.IsCompileTimeConstant());
}

TEST(ConstantResolveResultTest, ToStringIntConstant)
{
    // The C# `ToString` format `"[ConstantResolveResult <type> = <value>]"` for
    // an integer literal: the type is `System.Int32`, the value is the decimal
    // digits (the C# invariant-culture integer `ToString`). The `std::any`
    // stringification (the load-bearing crux of this leaf) reproduces the C#
    // `constantValue.ToString()` polymorphic dispatch for an `int32`.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(42)));
    EXPECT_EQ(crr.ToString(), "[ConstantResolveResult System.Int32 = 42]");
}

TEST(ConstantResolveResultTest, ToStringBoolConstant)
{
    // The C# `Boolean.ToString` (invariant culture) yields the capitalised
    // "True" / "False" (NOT the C++ `std::to_string` "1"/"0"); the
    // `StringifyConstantValue` `bool` arm reproduces this faithfully for both
    // values.
    ILSpy::Decompiler::Semantics::ConstantResolveResult trueC(MakeBooleanType(), std::any(true));
    ILSpy::Decompiler::Semantics::ConstantResolveResult falseC(MakeBooleanType(), std::any(false));
    EXPECT_EQ(trueC.ToString(), "[ConstantResolveResult System.Boolean = True]");
    EXPECT_EQ(falseC.ToString(), "[ConstantResolveResult System.Boolean = False]");
}

TEST(ConstantResolveResultTest, ToStringStringConstant)
{
    // The C# `String.ToString` is the identity, so a string literal renders as
    // its own text in the `ToString` format (no quoting, faithful to the C#
    // `string.Format` which does not quote string arguments).
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeStringType(), std::any(std::string("hello")));
    EXPECT_EQ(crr.ToString(), "[ConstantResolveResult System.String = hello]");
}

TEST(ConstantResolveResultTest, ToStringNullConstant)
{
    // The C# `string.Format` prints the empty string for a `null` argument, so a
    // `null` literal renders with the value slot empty: `"[ConstantResolveResult
    // System.Object = ]"` (the ` = ` separator is present, the value after it is
    // empty). The `StringifyConstantValue` empty-`any` arm reproduces this.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeObjectType(), std::any());
    EXPECT_EQ(crr.ToString(), "[ConstantResolveResult System.Object = ]");
}

TEST(ConstantResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `ConstantResolveResult` stays a
    // `ConstantResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this: the clone is a `ConstantResolveResult`
    // (`dynamic_cast` succeeds), not a sliced `ResolveResult`.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(42)));
    auto clone = crr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::ConstantResolveResult*>(clone.get()),
              nullptr);
}

TEST(ConstantResolveResultTest, ShallowCloneSharesTypeAndCopiesValue)
{
    // The clone SHARES the base `type_` `shared_ptr` (faithful to the C#
    // `MemberwiseClone` reference-copy of the `IType`) but COPIES the
    // `constantValue_` `std::any` value member (faithful to the C# value-copy of
    // the boxed `object`). The shared `type` is reached by pointer-identity
    // (`&clone->Type() == &crr.Type()`); the copied value is round-tripped via
    // `std::any_cast` (the clone's `ConstantValue()` holds an equal but distinct
    // `std::any`). This is the structural distinction from the prior siblings
    // (D425-D431) whose ShallowClone shared `shared_ptr` members only -- a
    // `std::any` is a value member, so the clone's value is a copy.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(42)));
    auto clone = crr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &crr.Type());
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::ConstantResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    ASSERT_TRUE(cloned->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(cloned->ConstantValue()), 42);
    // The clone's ToString matches (the value and type are preserved).
    EXPECT_EQ(cloned->ToString(), crr.ToString());
}

TEST(ConstantResolveResultTest, ShallowCloneIsDistinctInstance)
{
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(42)));
    auto clone = crr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &crr);
}

TEST(ConstantResolveResultTest, InheritedDefaultsArePreserved)
{
    // `ConstantResolveResult` does NOT override `IsError` / `GetChildResults`,
    // so the inherited `ResolveResult` base defaults hold (a constant is not an
    // error, has no child results). It DOES override `IsCompileTimeConstant`
    // (true) and `ConstantValue` (the stored value), exercised by the dedicated
    // tests above.
    ILSpy::Decompiler::Semantics::ConstantResolveResult crr(MakeInt32Type(), std::any(std::int32_t(42)));
    EXPECT_FALSE(crr.IsError());
    EXPECT_TRUE(crr.GetChildResults().empty());
}

TEST(ConstantResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::ConstantResolveResult>,
                  "ConstantResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ConstantResolveResult>,
                  "ConstantResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ConstantResolveResult>,
                  "ConstantResolveResult is polymorphic (the ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `ConstantResolveResult` is NOT sealed and no C# subclass derives
    // from it, so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::ConstantResolveResult>,
                  "ConstantResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
