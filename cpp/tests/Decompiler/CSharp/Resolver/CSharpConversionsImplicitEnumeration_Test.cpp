// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` implicit-enumeration-conversion helper --
// `Detail::ImplicitEnumerationConversion` (a faithful port of the C#
// `CSharpConversions.ImplicitEnumerationConversion(ResolveResult, IType)`,
// CSharpConversions.cs line 459, C# 9.0 spec section 10.2.4 "implicit enumeration conversions"
// plus the enum part of section 10.2.6). A compile-time constant whose type is a numeric
// primitive (`TypeCode` in [SByte, Decimal]) and whose value is zero
// (`Convert.ToDouble(ConstantValue) == 0`) converts implicitly to any enum type (the to-side
// is stripped of its nullable wrapper first, so `0 -> E?` is the lifted form). Returns
// `EnumerationConversion(true, IsNullable(toType))` (a factory -- a fresh per-call
// `NumericOrEnumerationConversion`) when the conversion fires, else `None`.
//
// The test stubs:
//  * `ConstantResolveResult` (D432) is the faithful stub for a compile-time constant -- it
//    overrides `IsCompileTimeConstant` to `true` and `ConstantValue` to the stored boxed value.
//    The constant's `Type` is a `LookupTypeDefinition` (`Def(Int32)` / `Def(Int64)` / etc.) so
//    `GetTypeCode` resolves the `KnownTypeCode`; a `KnownType(Int32)` (NOT an `ITypeDefinition`)
//    would yield `GetTypeCode == Empty`, pinning the `NativeIntZeroToEnum` out-of-range case.
//  * `Def(ktc, kind)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() ==
//    this`) with a configurable `KnownTypeCode` and `TypeKind` -- the observable behavior of a
//    real primitive/enum definition (`GetTypeCode` resolves via the numeric cast, the
//    `KnownTypeCode` 0-17 align with `TypeCode` 0-17).
//  * `EnumDef()` is `Kind == Enum` and `KnownTypeCode == None` (a real enum carries no
//    `KnownTypeCode`, so `GetTypeCode == Empty`); `GetUnderlyingType(enumDef).Kind() == Enum`
//    is true (a non-nullable enum's underlying is itself).
//  * `NullableOf(Def(Enum))` is a `ParameterizedType` over the `System.Nullable`1` definition
//    (a 1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the `Enum` type argument and
//    `IsNullable` returns true -- pinning the lifted-form crux (`0 -> E?`).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"  // ConstantResolveResult (the compile-time-constant stub)
#include "Decompiler/Semantics/Conversion.hpp"              // Conversion (the flag accessors)
#include "Decompiler/Semantics/ConversionFactories.hpp"     // Conversions (the None singleton / EnumerationConversion factory)
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

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitEnumerationConversion;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
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

// A primitive or special definition as an `ITypePtr` (for passing to `ConstantResolveResult` ctor
// which takes the `IType` handle by value, and for dereferencing to the `const IType& toType`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// An enum definition: `Kind == Enum` and `KnownTypeCode == None` (a real enum carries no
// `KnownTypeCode`, so `GetTypeCode == Empty`). `GetUnderlyingType(enumDef)` returns the enum
// itself (a non-nullable type), and `Kind() == Enum` -- the to-side guard the helper checks.
ITypePtr EnumDef() {
    return MakeDef(KnownTypeCode::None, TypeKind::Enum);
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
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element;
// `NullableType.IsNullable` returns true -- the lifted-form crux (`0 -> E?`).
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A compile-time constant of type `int` (`Int32`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstInt32(std::int32_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int32), std::any(val));
}

// A compile-time constant of type `long` (`Int64`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstInt64(std::int64_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int64), std::any(val));
}

// A compile-time constant of type `byte` (`Byte`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstByte(std::uint8_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Byte), std::any(val));
}

// A compile-time constant of type `float` (`Single`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstFloat(float val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Single), std::any(val));
}

// A compile-time constant of type `double` (`Double`) holding the supplied boxed value.
std::shared_ptr<ConstantResolveResult> ConstDouble(double val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Double), std::any(val));
}

// A compile-time constant of type `char` (`Char`) holding the supplied boxed value. `TypeCode::Char`
// (4) is below `SByte` (5), so the range check fails -- the value is never read by `ConvertToDouble`.
std::shared_ptr<ConstantResolveResult> ConstChar(char val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Char), std::any(val));
}

// A compile-time constant of type `string` (`String`) -- `TypeCode::String` (17) is above `Decimal`
// (15), so the range check fails. The value is a dummy (the guard short-circuits before reading it).
std::shared_ptr<ConstantResolveResult> ConstString() {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::String, TypeKind::Class), std::any());
}

// A compile-time constant whose `Type` is a native integer (`nint`, `Kind == NInt`,
// `KnownTypeCode == None`) -- `GetTypeCode` yields `TypeCode::Empty` (0), below `SByte` (5), so the
// range check fails. Pins the `NativeIntZeroToEnum` out-of-range case (a `nint 0` does NOT convert
// to an enum via the implicit enumeration conversion -- the C# spec section 10.2.4 lists only the
// decimal-integer-literal 0, and `nint`'s `TypeCode` is `Empty`, not in [SByte, Decimal]).
std::shared_ptr<ConstantResolveResult> ConstNInt(std::int32_t val) {
    return std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::None, TypeKind::NInt), std::any(val));
}

} // namespace

// ===========================================================================
// ImplicitEnumerationConversion (CSharpConversions.cs line 459, spec 10.2.4 + enum part of 10.2.6).
//
//   Debug.Assert(rr.IsCompileTimeConstant);
//   constantType = GetTypeCode(rr.Type);
//   if (constantType >= SByte && constantType <= Decimal && Convert.ToDouble(ConstantValue) == 0)
//       if (GetUnderlyingType(toType).Kind == Enum)
//           return EnumerationConversion(true, IsNullable(toType));
//   return None;
// ===========================================================================

// ---------------------------------------------------------------------------
// The constant-0-to-enum crux -- `int 0 -> E` returns a non-lifted EnumerationConversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32ZeroToEnumIsImplicitEnumerationConversion) {
    auto c = ConstInt32(0);
    auto result = ImplicitEnumerationConversion(*c, *EnumDef());
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
    EXPECT_TRUE(result->IsImplicit());
    EXPECT_FALSE(result->IsExplicit());
    EXPECT_FALSE(result->IsLifted());
    EXPECT_FALSE(result->IsNumericConversion());
    EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// The lifted crux -- `int 0 -> E?` returns a LIFTED EnumerationConversion (IsNullable -> isLifted).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32ZeroToNullableEnumIsLiftedEnumerationConversion) {
    auto c = ConstInt32(0);
    auto result = ImplicitEnumerationConversion(*c, *NullableOf(EnumDef()));
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
    EXPECT_TRUE(result->IsImplicit());
    EXPECT_FALSE(result->IsExplicit());
    EXPECT_TRUE(result->IsLifted());
    EXPECT_FALSE(result->IsNumericConversion());
    EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// A non-zero constant does NOT convert -- `int 5 -> E` returns None.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32NonZeroToEnumReturnsNone) {
    auto c = ConstInt32(5);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// A negative constant does NOT convert -- `int -1 -> E` returns None (-1 != 0).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32NegativeToEnumReturnsNone) {
    auto c = ConstInt32(-1);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// `long 0 -> E` -- the Int64 TypeCode is in [SByte, Decimal], the value is 0.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int64ZeroToEnumIsImplicitEnumerationConversion) {
    auto c = ConstInt64(0);
    auto result = ImplicitEnumerationConversion(*c, *EnumDef());
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
    EXPECT_FALSE(result->IsLifted());
}

// ---------------------------------------------------------------------------
// `byte 0 -> E` -- the Byte TypeCode (6) is in [SByte(5), Decimal(15)].
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, ByteZeroToEnumIsImplicitEnumerationConversion) {
    auto c = ConstByte(0);
    auto result = ImplicitEnumerationConversion(*c, *EnumDef());
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
}

// ---------------------------------------------------------------------------
// `float 0.0 -> E` -- the Single TypeCode (13) is in range, Convert.ToDouble(0.0f) == 0.0.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, SingleZeroToEnumIsImplicitEnumerationConversion) {
    auto c = ConstFloat(0.0f);
    auto result = ImplicitEnumerationConversion(*c, *EnumDef());
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
}

// ---------------------------------------------------------------------------
// `double 0.0 -> E` -- the Double TypeCode (14) is in range, Convert.ToDouble(0.0) == 0.0.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, DoubleZeroToEnumIsImplicitEnumerationConversion) {
    auto c = ConstDouble(0.0);
    auto result = ImplicitEnumerationConversion(*c, *EnumDef());
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsEnumerationConversion());
}

// ---------------------------------------------------------------------------
// `float 1.0 -> E` -- the value is NOT 0, so the conversion does not fire (returns None).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, SingleNonZeroToEnumReturnsNone) {
    auto c = ConstFloat(1.0f);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// The to-side must be an enum -- `int 0 -> int` returns None (toType.Kind != Enum).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32ZeroToNonEnumTypeReturnsNone) {
    auto c = ConstInt32(0);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *Def(KnownTypeCode::Int32)).get(),
              Conversions::None().get());
}

// ---------------------------------------------------------------------------
// The to-side's underlying must be an enum -- `int 0 -> Nullable<int>` returns None
// (GetUnderlyingType(Nullable<int>) is int, Kind != Enum).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, Int32ZeroToNullableNonEnumReturnsNone) {
    auto c = ConstInt32(0);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *NullableOf(Def(KnownTypeCode::Int32))).get(),
              Conversions::None().get());
}

// ---------------------------------------------------------------------------
// A string-typed constant -- TypeCode::String (17) is above Decimal (15), outside the range.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, StringConstantToEnumReturnsNone) {
    auto c = ConstString();
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// A char-typed constant -- TypeCode::Char (4) is below SByte (5), outside the range.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, CharZeroToEnumReturnsNone) {
    auto c = ConstChar('\0');
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// A nint-typed constant -- TypeCode::Empty (0) is below SByte (5), outside the range.
// Pins that a native-integer 0 does NOT convert to an enum (the GetTypeCode range check fails
// for the synthetic nint/nuint kinds whose KnownTypeCode is None).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsImplicitEnumerationTest, NativeIntZeroToEnumReturnsNone) {
    auto c = ConstNInt(0);
    EXPECT_EQ(ImplicitEnumerationConversion(*c, *EnumDef()).get(), Conversions::None().get());
}
