// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` implicit constant-expression conversion (CSharpConversions.cs
// line 823, C# 9.0 spec section 10.2.11): `Detail::ImplicitConstantExpressionConversion`. This is
// the first conversion helper to consume a `ResolveResult` (D424) -- it reads
// `ResolveResult.IsCompileTimeConstant` / `ResolveResult.Type` / `ResolveResult.ConstantValue`
// (the `std::any` the D374/D424 `object?` model yields). The constant's `Type` and the `toType`
// are `LookupTypeDefinition`s (IS-A `ITypeDefinition`, so `ReflectionHelper.GetTypeCode` resolves
// the `KnownTypeCode`); a `KnownType` (NOT an `ITypeDefinition`, `GetDefinition() == nullptr`)
// yields `GetTypeCode == Empty`, pinning the non-definition branch.
//
// CRUX STUB CONVENTIONS (carried from the D514 numeric / D515 nullable tests):
//  * `ConstantResolveResult` (D432) is the faithful stub for a compile-time constant -- it
//    overrides `IsCompileTimeConstant` to `true` and `ConstantValue` to the stored boxed value.
//    The constant's `Type` is a `LookupTypeDefinition` (`Def(Int32)` / `Def(Int64)`) so
//    `GetTypeCode` resolves the `KnownTypeCode`; a `KnownType(Int32)` (NOT an `ITypeDefinition`)
//    would yield `GetTypeCode == Empty`, pinning the `NonDefinitionFromTypeReturnsFalse` case.
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` -- the observable behavior of a real primitive
//    definition (`GetTypeCode` resolves via the numeric cast, the `KnownTypeCode` 0-17 align
//    with `TypeCode` 0-17).
//  * `NativeInt(NUInt)` is `Kind == NUInt` and `KnownTypeCode == None` (so `GetTypeCode == Empty`,
//    faithfully -- the real `nuint` has no `KnownTypeCode`); the constant-expression conversion
//    recognizes it via `Kind` and remaps it to `UInt32` (only 32 bits store safely on a 32-bit
//    platform).
//  * `NullableOf(Def(SByte))` is a `ParameterizedType` over the `System.Nullable`1` definition
//    (a 1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the `SByte` type argument -- pinning the
//    nullable-strip crux (the C# `toType = NullableType.GetUnderlyingType(toType)` rebind).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"  // ConstantResolveResult (the compile-time-constant stub)
#include "Decompiler/Semantics/ResolveResult.hpp"          // ResolveResult (the base non-constant stub)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitConstantExpressionConversion;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via the numeric cast
// (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Def(Int32)` reports `TypeCode::Int32`
// -- the observable behavior of a real primitive definition.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive integral definition as an `ITypePtr` (for passing to `ConstantResolveResult` ctor
// which takes the `IType` handle by value, and for dereferencing to the `const IType& toType`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// A native-integer definition: `Kind == NInt`/`NUInt` and `KnownTypeCode == None` (so
// `GetTypeCode == Empty`, faithfully -- the real `nint`/`nuint` have no `KnownTypeCode`). The
// constant-expression conversion recognizes the `NUInt` to-side via `Kind` and remaps it to
// `UInt32`.
ITypePtr NativeInt(TypeKind k) {
    assert(k == TypeKind::NInt || k == TypeKind::NUInt);
    return MakeDef(KnownTypeCode::None, k);
}

// A `LookupTypeDefinition` for the `System.Nullable`1` generic definition (a struct,
// `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
    return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
        FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element.
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A compile-time constant of type `int` (`Int32`) holding the supplied boxed value. The faithful
// stub for the `fromTypeCode == Int32` arm.
std::shared_ptr<ConstantResolveResult> ConstInt32(std::int32_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int32), std::any(val));
}

// A compile-time constant of type `long` (`Int64`) holding the supplied boxed value. The faithful
// stub for the `fromTypeCode == Int64` arm.
std::shared_ptr<ConstantResolveResult> ConstInt64(std::int64_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int64), std::any(val));
}

// A compile-time constant of type `short` (`Int16`) -- a `fromTypeCode` that is neither `Int32`
// nor `Int64`, so neither arm fires (pins the fallthrough-to-false).
std::shared_ptr<ConstantResolveResult> ConstInt16(std::int16_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int16), std::any(val));
}

// A compile-time constant whose `Type` is a `KnownType(Int32)` (NOT an `ITypeDefinition`,
// `GetDefinition() == nullptr`) -- `GetTypeCode` yields `TypeCode::Empty`, so neither arm fires.
// Pins the non-definition-from-type branch (the `ReflectionHelper.GetTypeCode` dynamic_cast skips
// a non-`ITypeDefinition` `IType`).
std::shared_ptr<ConstantResolveResult> ConstKnownInt32(std::int32_t val) {
    return std::make_shared<ConstantResolveResult>(
        std::make_shared<KnownType>(KnownTypeCode::Int32), std::any(val));
}

} // namespace

// ===========================================================================
// ImplicitConstantExpressionConversion (CSharpConversions.cs line 823, spec 10.2.11).
//
//   if (!rr.IsCompileTimeConstant) return false;
//   fromTypeCode = GetTypeCode(rr.Type); toType = GetUnderlyingType(toType); toTypeCode = GetTypeCode(toType);
//   if (toType.Kind == NUInt) toTypeCode = UInt32;
//   if (fromTypeCode == Int64) return val >= 0 && toTypeCode == UInt64;
//   if (fromTypeCode == Int32) switch (toTypeCode) {
//       SByte:  val in [-128, 127];       Byte:   val in [0, 255];
//       Int16:  val in [-32768, 32767];   UInt16: val in [0, 65535];
//       UInt32: val >= 0;                 UInt64: val >= 0;
//       default: false; }
//   return false;
// ===========================================================================

// ---------------------------------------------------------------------------
// IsCompileTimeConstant guard -- a non-constant ResolveResult yields false.
// ---------------------------------------------------------------------------

// A base `ResolveResult` (the default `IsCompileTimeConstant == false`) -- the guard rejects it
// before reading the type or the constant value, so any `toType` yields false.
TEST(CSharpConversionsConstantExpressionTest, NonConstantResolveResultReturnsFalse) {
    ResolveResult base(Def(KnownTypeCode::Int32));
    EXPECT_FALSE(ImplicitConstantExpressionConversion(base, *Def(KnownTypeCode::UInt64)));
}

// ---------------------------------------------------------------------------
// Int64 arm -- `long val` converts to `ulong` iff `val >= 0 && toTypeCode == UInt64`.
// ---------------------------------------------------------------------------

// `5L -> ulong`: val >= 0 and toTypeCode == UInt64 -- the Int64 arm returns true.
TEST(CSharpConversionsConstantExpressionTest, Int64NonNegativeToUInt64IsTrue) {
    auto c = ConstInt64(5);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}

// `5L -> uint`: val >= 0 but toTypeCode != UInt64 (it is UInt32) -- the Int64 arm returns false.
TEST(CSharpConversionsConstantExpressionTest, Int64NonNegativeToNonUInt64IsFalse) {
    auto c = ConstInt64(5);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt32)));
}

// `-5L -> ulong`: val < 0 -- the Int64 arm returns false (even though toTypeCode == UInt64).
TEST(CSharpConversionsConstantExpressionTest, Int64NegativeToUInt64IsFalse) {
    auto c = ConstInt64(-5);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}

// ---------------------------------------------------------------------------
// Int32 arm -- `int val` converts to the target iff `val` fits the target's range.
// ---------------------------------------------------------------------------

// `100 -> sbyte`: 100 is in [-128, 127] -- the SByte case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToSByteInRangeIsTrue) {
    auto c = ConstInt32(100);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::SByte)));
}

// `200 -> sbyte`: 200 > 127 (SByte.MaxValue) -- the SByte case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToSByteOutOfRangeIsFalse) {
    auto c = ConstInt32(200);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::SByte)));
}

// `200 -> byte`: 200 is in [0, 255] -- the Byte case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToByteInRangeIsTrue) {
    auto c = ConstInt32(200);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::Byte)));
}

// `-1 -> byte`: -1 < 0 (Byte.MinValue) -- the Byte case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToByteNegativeIsFalse) {
    auto c = ConstInt32(-1);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::Byte)));
}

// `30000 -> short`: 30000 is in [-32768, 32767] -- the Int16 case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToInt16InRangeIsTrue) {
    auto c = ConstInt32(30000);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::Int16)));
}

// `40000 -> short`: 40000 > 32767 (Int16.MaxValue) -- the Int16 case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToInt16OutOfRangeIsFalse) {
    auto c = ConstInt32(40000);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::Int16)));
}

// `40000 -> ushort`: 40000 is in [0, 65535] -- the UInt16 case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt16InRangeIsTrue) {
    auto c = ConstInt32(40000);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt16)));
}

// `-1 -> ushort`: -1 < 0 (UInt16.MinValue) -- the UInt16 case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt16NegativeIsFalse) {
    auto c = ConstInt32(-1);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt16)));
}

// `100 -> uint`: 100 >= 0 -- the UInt32 case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt32NonNegativeIsTrue) {
    auto c = ConstInt32(100);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt32)));
}

// `-1 -> uint`: -1 < 0 -- the UInt32 case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt32NegativeIsFalse) {
    auto c = ConstInt32(-1);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt32)));
}

// `100 -> ulong`: 100 >= 0 -- the UInt64 case returns true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt64NonNegativeIsTrue) {
    auto c = ConstInt32(100);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}

// `-1 -> ulong`: -1 < 0 -- the UInt64 case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToUInt64NegativeIsFalse) {
    auto c = ConstInt32(-1);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}

// ---------------------------------------------------------------------------
// Int32 arm -- a toType not in the switch (e.g. Int32) falls through to false.
// ---------------------------------------------------------------------------

// `100 -> int`: toTypeCode == Int32 has no switch case -- the switch falls through to false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToInt32FallthroughIsFalse) {
    auto c = ConstInt32(100);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::Int32)));
}

// ---------------------------------------------------------------------------
// Nullable strip -- the to-side is stripped of its `Nullable<T>` wrapper before the range check.
// ---------------------------------------------------------------------------

// `100 -> Nullable<sbyte>`: GetUnderlyingType strips to `sbyte`, 100 is in [-128, 127] -- the SByte
// case returns true. Pins the nullable-strip crux (the C# `toType = GetUnderlyingType(toType)`).
TEST(CSharpConversionsConstantExpressionTest, Int32ToNullableSByteStripsAndChecksRangeIsTrue) {
    auto c = ConstInt32(100);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *NullableOf(Def(KnownTypeCode::SByte))));
}

// ---------------------------------------------------------------------------
// NUInt remap -- a `nuint` to-side is treated as `UInt32` (32 bits store safely).
// ---------------------------------------------------------------------------

// `100 -> nuint`: toType.Kind == NUInt, so toTypeCode is remapped to UInt32, 100 >= 0 -- true.
TEST(CSharpConversionsConstantExpressionTest, Int32ToNUIntNonNegativeIsTrue) {
    auto c = ConstInt32(100);
    EXPECT_TRUE(ImplicitConstantExpressionConversion(*c, *NativeInt(TypeKind::NUInt)));
}

// `-1 -> nuint`: toTypeCode remapped to UInt32, -1 < 0 -- the UInt32 case returns false.
TEST(CSharpConversionsConstantExpressionTest, Int32ToNUIntNegativeIsFalse) {
    auto c = ConstInt32(-1);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *NativeInt(TypeKind::NUInt)));
}

// ---------------------------------------------------------------------------
// Neither arm fires -- a fromType that is not Int32/Int64 yields false.
// ---------------------------------------------------------------------------

// `5 -> ulong` with a `short`-typed constant: fromTypeCode == Int16 (neither Int64 nor Int32),
// so neither arm fires -- returns false regardless of the (otherwise-fitting) toType.
TEST(CSharpConversionsConstantExpressionTest, Int16ConstantReturnsFalse) {
    auto c = ConstInt16(5);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}

// `5 -> ulong` with a `KnownType(Int32)`-typed constant: GetTypeCode yields Empty (a `KnownType`
// is NOT an `ITypeDefinition`), so neither arm fires -- returns false. Pins the non-definition
// from-type branch (the `ReflectionHelper.GetTypeCode` dynamic_cast skips a non-`ITypeDefinition`).
TEST(CSharpConversionsConstantExpressionTest, NonDefinitionFromTypeReturnsFalse) {
    auto c = ConstKnownInt32(5);
    EXPECT_FALSE(ImplicitConstantExpressionConversion(*c, *Def(KnownTypeCode::UInt64)));
}
