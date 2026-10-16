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

// Tests for `SizeOfResolveResult` (cpp/Decompiler/Semantics/SizeOfResolveResult.hpp,
// the D430 port of ICSharpCode.Decompiler/Semantics/SizeOfResolveResult.cs) -- the
// resolved expression is the `sizeof` operator. `SizeOfResolveResult` is the sixth
// `Semantics` leaf: the C# source declares a forwarding ctor taking two `IType`
// arguments (the `int32` forwarded to the base, and the `referencedType` stored with
// an `ArgumentNullException` null guard) plus an `int?` `constantValue`, a
// non-virtual `ReferencedType` getter, and three `ResolveResult` virtual overrides
// (`IsCompileTimeConstant` = `constantValue != null`; `ConstantValue` = the boxed
// `constantValue`; `IsError` = `referencedType.IsReferenceType != false`). The
// `IsError` override is the consumer the D429 `IsReferenceType` leaf was ported for.
//
// The tests pin the ctor-stores-all-fields contract (the base `Type()` is the
// `int32`; the own `ReferencedType()` is the distinct `referencedType`; the
// `constantValue` is stored), the `ReferencedType`-distinct-from-`Type` crux, the
// `IsCompileTimeConstant`/`ConstantValue` constant-pair (true + boxed int when
// provided; false + empty any when null), the `IsError`
// `IsReferenceType != false` crux (true for a reference type or unknown; false for a
// value type), the `ToString` subclass class-name format, the `ShallowClone`
// runtime-type preservation (not sliced) plus shared-ownership clone of BOTH
// `IType` fields and value-copy of the `int?`, the inherited `GetChildResults`
// default (the subclass does NOT override it), and the `is_base_of` / not-`final`
// class-shape static_asserts.

#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete minimal-port `IType`: `KnownType(Int32)` whose `ReflectionName()` is
// "System.Int32". Used as the `sizeof` expression's own type (the `int32` forwarded
// to the `ResolveResult` base) in the tests -- the C# `sizeof` expression always
// has type `System.Int32`.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// A concrete minimal-port `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". A reference type (`IsReferenceType() == true`), used as the
// `referencedType` to exercise the `IsError` reference-type branch (`true != false`
// is `true`).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// A concrete minimal-port `IType`: `KnownType(Byte)` whose `ReflectionName()` is
// "System.Byte". A value type (`IsReferenceType() == false`), used as the
// `referencedType` to exercise the `IsError` value-type branch (`false != false`
// is `false`).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeByteType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Byte);
}

} // namespace

TEST(SizeOfResolveResultTest, ConstructorStoresAllFields)
{
    // The C# ctor `SizeOfResolveResult(IType int32, IType referencedType, int?
    // constantValue) : base(int32)` forwards `int32` to the `ResolveResult` base,
    // stores `referencedType`, and stores `constantValue`. The C++ port mirrors
    // this: `Type()` returns the forwarded `int32` (pointer-identity),
    // `ReferencedType()` returns the stored `referencedType` (pointer-identity),
    // and `IsCompileTimeConstant()` reflects the provided `constantValue`.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_EQ(&srr.Type(), int32.get());
    EXPECT_EQ(&srr.ReferencedType(), referencedType.get());
    EXPECT_TRUE(srr.IsCompileTimeConstant());
}

TEST(SizeOfResolveResultTest, Int32TypeIsForwardedToBase)
{
    // The `int32` is the `ResolveResult` base `Type()` (the `sizeof` expression's
    // own type is `System.Int32`); it is NOT the `referencedType`.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_EQ(srr.Type().ReflectionName(), "System.Int32");
}

TEST(SizeOfResolveResultTest, ReferencedTypeReturnsConfiguredType)
{
    // The C# `IType ReferencedType { get; }` returns the ctor-stored
    // `referencedType`. The C++ port mirrors this: `ReferencedType()` returns the
    // stored `referencedType` (here `System.Byte`).
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_EQ(srr.ReferencedType().ReflectionName(), "System.Byte");
}

TEST(SizeOfResolveResultTest, ReferencedTypeIsDistinctFromInt32Type)
{
    // The load-bearing crux: `ReferencedType` returns the stored `referencedType`,
    // DISTINCT from the base `int32`. A `sizeof(byte)` expression's `Type()` is
    // `System.Int32` (the result size) while its `ReferencedType()` is `System.Byte`
    // (the type sized); the two are different objects and different reflection
    // names. (Neutering `ReferencedType` to `return Type()` would make this test
    // fail -- the crux-targeted RED, the D427 precedent.)
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_NE(&srr.ReferencedType(), &srr.Type());
    EXPECT_NE(srr.ReferencedType().ReflectionName(), srr.Type().ReflectionName());
}

TEST(SizeOfResolveResultTest, IsCompileTimeConstantTrueWhenConstantValueProvided)
{
    // The C# `override bool IsCompileTimeConstant => constantValue != null`: true
    // when the size is a compile-time constant. The C++ port mirrors this: a
    // provided `std::optional<int>` (non-null `int?`) yields `true`.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(4));
    EXPECT_TRUE(srr.IsCompileTimeConstant());
}

TEST(SizeOfResolveResultTest, IsCompileTimeConstantFalseWhenConstantValueNull)
{
    // The C# `override bool IsCompileTimeConstant => constantValue != null`: false
    // when the size is NOT a compile-time constant. The C++ port mirrors this: an
    // empty `std::optional<int>` (null `int?`) yields `false`.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeObjectType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::nullopt);
    EXPECT_FALSE(srr.IsCompileTimeConstant());
}

TEST(SizeOfResolveResultTest, ConstantValueReturnsBoxedIntWhenProvided)
{
    // The C# `override object? ConstantValue => constantValue`: the boxed `int?`.
    // A non-null `int?` boxes to a boxed `int` (the D374 `std::any`-for-`object?`
    // convention); the C++ port boxes the int as `std::any(*constantValue_)`, so
    // `any_cast<int>` extracts the value and `has_value()` is true.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(4));
    ASSERT_TRUE(srr.ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(srr.ConstantValue()), 4);
}

TEST(SizeOfResolveResultTest, ConstantValueEmptyWhenConstantValueNull)
{
    // The C# `override object? ConstantValue => constantValue`: a null `int?`
    // boxes to `null` (the empty `std::any`, matching the base default).
    auto int32 = MakeInt32Type();
    auto referencedType = MakeObjectType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::nullopt);
    EXPECT_FALSE(srr.ConstantValue().has_value());
}

TEST(SizeOfResolveResultTest, IsErrorIsTrueForReferenceType)
{
    // The C# `override bool IsError => referencedType.IsReferenceType != false`:
    // a `sizeof` of a reference type is an error (a reference type's
    // `IsReferenceType` is `true`, so `true != false` is `true`). `System.Object`
    // is a reference type. (Neutering `IsError` to `return false` would make this
    // test fail -- the crux-targeted RED.)
    auto int32 = MakeInt32Type();
    auto referencedType = MakeObjectType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::nullopt);
    EXPECT_TRUE(srr.IsError());
}

TEST(SizeOfResolveResultTest, IsErrorIsFalseForValueType)
{
    // The C# `override bool IsError => referencedType.IsReferenceType != false`:
    // a `sizeof` of a value type is NOT an error (a value type's
    // `IsReferenceType` is `false`, so `false != false` is `false`). `System.Byte`
    // is a value type.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_FALSE(srr.IsError());
}

TEST(SizeOfResolveResultTest, IsErrorIsTrueForUnknownReferenceNess)
{
    // The C# `override bool IsError => referencedType.IsReferenceType != false`:
    // a `sizeof` of a type whose reference-ness is unknown (an `UnknownType`,
    // `IsReferenceType == null`) is an error -- the C# `bool? != bool` lifted
    // comparison yields `null != false` = `true`, faithfully mirrored by the C++
    // `std::nullopt != false` (the D429 `ConsumerPattern*` shape). The
    // `UnknownType()` convenience is a `SpecialType(TypeKind::Unknown)` whose
    // `IsReferenceType()` returns `std::nullopt`.
    auto int32 = MakeInt32Type();
    auto referencedType = ILSpy::Decompiler::TypeSystem::UnknownType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::nullopt);
    EXPECT_EQ(referencedType->IsReferenceType(), std::nullopt);
    EXPECT_TRUE(srr.IsError());
}

TEST(SizeOfResolveResultTest, ToStringReportsSizeOfResolveResultClassName)
{
    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "SizeOfResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "ResolveResult"). The format is
    // "[<class-name> <int32 reflection name>]" -- the base `Type()` is the `int32`
    // (here `System.Int32`).
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_EQ(srr.ToString(), "[SizeOfResolveResult System.Int32]");
}

TEST(SizeOfResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `SizeOfResolveResult` stays a
    // `SizeOfResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this: the clone is a `SizeOfResolveResult`
    // (`dynamic_cast` succeeds), not a sliced `ResolveResult`.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    auto clone = srr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::SizeOfResolveResult*>(clone.get()),
              nullptr);
}

TEST(SizeOfResolveResultTest, ShallowCloneSharesBothTypesAndCopiesConstantValue)
{
    // The clone shares BOTH `IType` fields via the `shared_ptr` members (faithful
    // to `MemberwiseClone`'s reference copy of both the base `type` and the own
    // `referencedType`) AND copies the `constantValue_` value member (the C# value
    // copy of the `int?`). The same `int32` and `referencedType` objects back both
    // the original and the clone, and the clone's `IsCompileTimeConstant` /
    // `ConstantValue` match the original's.
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(4));
    auto clone = srr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &srr.Type());
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::SizeOfResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(&cloned->ReferencedType(), referencedType.get());
    EXPECT_EQ(&cloned->ReferencedType(), &srr.ReferencedType());
    EXPECT_TRUE(cloned->IsCompileTimeConstant());
    ASSERT_TRUE(cloned->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(cloned->ConstantValue()), 4);
}

TEST(SizeOfResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    auto clone = srr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &srr);
}

TEST(SizeOfResolveResultTest, GetChildResultsIsEmptyByDefault)
{
    // `SizeOfResolveResult` does NOT override `GetChildResults`, so the inherited
    // `ResolveResult` base default (empty) holds (a `sizeof` expression has no
    // child resolve results).
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    EXPECT_TRUE(srr.GetChildResults().empty());
}

TEST(SizeOfResolveResultTest, DispatchesPolymorphicallyThroughBasePointer)
{
    // The three `ResolveResult` virtual overrides (`IsError`,
    // `IsCompileTimeConstant`, `ConstantValue`) dispatch through a base
    // `ResolveResult*` pointer (the virtual dispatch the C# resolver relies on).
    // The `IsError` override reaches the `referencedType`'s `IsReferenceType`; the
    // `IsCompileTimeConstant`/`ConstantValue` overrides reach the stored
    // `constantValue`. Here a `sizeof(byte)` with a constant value of 1 is a
    // compile-time constant of value 1 and not an error (Byte is a value type).
    auto int32 = MakeInt32Type();
    auto referencedType = MakeByteType();
    ILSpy::Decompiler::Semantics::SizeOfResolveResult srr(
        int32, referencedType, std::optional<int>(1));
    ILSpy::Decompiler::Semantics::ResolveResult* base = &srr;
    EXPECT_FALSE(base->IsError());
    EXPECT_TRUE(base->IsCompileTimeConstant());
    ASSERT_TRUE(base->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(base->ConstantValue()), 1);
    EXPECT_EQ(base->ToString(), "[SizeOfResolveResult System.Int32]");
}

TEST(SizeOfResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::SizeOfResolveResult>,
                  "SizeOfResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::SizeOfResolveResult>,
                  "SizeOfResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::SizeOfResolveResult>,
                  "SizeOfResolveResult is polymorphic (the IsError/IsCompileTimeConstant/"
                  "ConstantValue/ClassName/ShallowClone overrides dispatch through base pointers).");
    // The C# `SizeOfResolveResult` is NOT sealed and no C# subclass derives from
    // it, so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::SizeOfResolveResult>,
                  "SizeOfResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
