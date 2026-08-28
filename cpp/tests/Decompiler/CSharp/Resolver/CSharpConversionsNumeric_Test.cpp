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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` numeric-conversion helpers (the first conversion helpers to
// land after the D512 skeleton) -- `Detail::IsNumericType` / `Detail::AnyNumericConversion` /
// `Detail::ImplicitNumericConversion` (faithful ports of the C# private instance methods). These
// read `ReflectionHelper.GetTypeCode` (D513) + `IType.Kind`, so the test stubs are
// `LookupTypeDefinition`s (IS-A `ITypeDefinition`, so `GetTypeCode` resolves the `KnownTypeCode`)
// with a configurable `KnownTypeCode`/`TypeKind`. A `KnownType` (NOT an `ITypeDefinition`) yields
// `GetTypeCode == Empty`, pinning the non-definition branch.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::AnyNumericConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitNumericConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsNumericType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via the numeric cast
// (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Prim(Int32)` reports `TypeCode::Int32`
// -- the observable behavior of a real primitive definition.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive numeric definition (Int32, Double, Char, ...). Each call returns a fresh shared
// pointer kept alive for the duration of the full expression (`*Prim(...)` binds the pointee to
// the helper's `const IType&` parameter).
std::shared_ptr<LookupTypeDefinition> Prim(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Struct);
}

// A native-integer definition: `Kind == NInt`/`NUInt` and `KnownTypeCode == None` (so
// `GetTypeCode == Empty`, faithfully -- the real `nint`/`nuint` have no `KnownTypeCode`). The
// numeric helpers recognize these via `Kind` (the `IsNumericType` short-circuit) and remap them
// to 64-bit (from-side) / 32-bit (to-side) in `ImplicitNumericConversion`.
std::shared_ptr<LookupTypeDefinition> NativeInt(TypeKind k) {
	assert(k == TypeKind::NInt || k == TypeKind::NUInt);
	return MakeDef(KnownTypeCode::None, k);
}

} // namespace

// ---------------------------------------------------------------------------
// IsNumericType: the numeric primitives (char, integrals, floating-point, decimal) are numeric.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, IsNumericType_PrimitivesAreNumeric) {
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Char)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::SByte)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Byte)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Int16)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::UInt16)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Int32)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::UInt32)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Int64)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::UInt64)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Single)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Double)));
	EXPECT_TRUE(IsNumericType(*Prim(KnownTypeCode::Decimal)));
}

// ---------------------------------------------------------------------------
// IsNumericType: the native integers (nint/nuint) are numeric via the Kind short-circuit
// (they have no KnownTypeCode, so GetTypeCode returns Empty).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, IsNumericType_NativeIntegersAreNumeric) {
	EXPECT_TRUE(IsNumericType(*NativeInt(TypeKind::NInt)));
	EXPECT_TRUE(IsNumericType(*NativeInt(TypeKind::NUInt)));
}

// ---------------------------------------------------------------------------
// IsNumericType: non-numeric primitives (Boolean, DateTime are outside [Char..Decimal]) and
// non-numeric reference types (Object, String) are not numeric.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, IsNumericType_NonNumericAreNotNumeric) {
	EXPECT_FALSE(IsNumericType(*Prim(KnownTypeCode::Boolean)));  // TypeCode 3 < Char(4)
	EXPECT_FALSE(IsNumericType(*Prim(KnownTypeCode::DateTime))); // TypeCode 16 > Decimal(15)
	EXPECT_FALSE(IsNumericType(*Prim(KnownTypeCode::Object)));    // TypeCode 1 < Char(4)
	EXPECT_FALSE(IsNumericType(*Prim(KnownTypeCode::String)));    // TypeCode 17 > Decimal(15)
}

// ---------------------------------------------------------------------------
// IsNumericType: a non-definition type (a `KnownType`, not an `ITypeDefinition`) yields
// `GetTypeCode == Empty` (the dynamic_cast to ITypeDefinition fails), so it is not numeric --
// the `KnownType(Int32)` is NOT recognized as Int32 here.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, IsNumericType_NonDefinitionYieldsEmpty) {
	auto k = std::make_shared<KnownType>(KnownTypeCode::Int32);
	EXPECT_FALSE(IsNumericType(*k));
	auto k2 = std::make_shared<KnownType>(KnownTypeCode::Char);
	EXPECT_FALSE(IsNumericType(*k2));
}

// ---------------------------------------------------------------------------
// AnyNumericConversion: true iff both types are numeric.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, AnyNumericConversion_BothNumeric) {
	EXPECT_TRUE(AnyNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Int64)));
	EXPECT_TRUE(AnyNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::Double)));
	EXPECT_TRUE(AnyNumericConversion(*NativeInt(TypeKind::NInt), *NativeInt(TypeKind::NUInt)));
	EXPECT_TRUE(AnyNumericConversion(*Prim(KnownTypeCode::Int32), *NativeInt(TypeKind::NInt)));
}

// ---------------------------------------------------------------------------
// AnyNumericConversion: false when either type is not numeric.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, AnyNumericConversion_OneNotNumeric) {
	EXPECT_FALSE(AnyNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::String)));
	EXPECT_FALSE(AnyNumericConversion(*Prim(KnownTypeCode::Object), *Prim(KnownTypeCode::Int32)));
	EXPECT_FALSE(AnyNumericConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::String)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: the integral-to-integral table cases that HAVE a conversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_IntegralTableHasConversion) {
	// char -> {ushort, int, long} (row 0: true, true, true)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::UInt16)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::Int32)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::Int64)));
	// sbyte -> {short, int, long} (row 1: true, true, true)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::Int16)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::Int32)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::Int64)));
	// byte -> {short, ushort, int, uint, long, ulong} (row 2: all true)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Byte), *Prim(KnownTypeCode::Int16)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Byte), *Prim(KnownTypeCode::UInt16)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Byte), *Prim(KnownTypeCode::UInt64)));
	// int -> {long} (row 5: only long true)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Int64)));
	// uint -> {long, ulong} (row 6)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt32), *Prim(KnownTypeCode::Int64)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt32), *Prim(KnownTypeCode::UInt64)));
	// long -> {long} (row 7, to=long)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int64), *Prim(KnownTypeCode::Int64)));
	// ulong -> {ulong} (row 8)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt64), *Prim(KnownTypeCode::UInt64)));
	// identity conversions: short->short, int->int (diagonal)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int16), *Prim(KnownTypeCode::Int16)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Int32)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: the integral-to-integral table cases that DO NOT have a conversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_IntegralTableNoConversion) {
	// char -> short (row 0, col 0: false -- char does not implicitly convert to short)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::Int16)));
	// sbyte -> {ushort, uint, ulong} (row 1: false, false, false)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::UInt16)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::UInt32)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::SByte), *Prim(KnownTypeCode::UInt64)));
	// short -> {ushort, uint, ulong} (row 3: false, false, false)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int16), *Prim(KnownTypeCode::UInt16)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int16), *Prim(KnownTypeCode::UInt32)));
	// int -> {short, ushort, uint, ulong} (row 5: false, false, false, false)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Int16)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::UInt16)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::UInt32)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::UInt64)));
	// long -> {short, int, uint, ulong} (row 7: false, false, false, false)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int64), *Prim(KnownTypeCode::Int32)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int64), *Prim(KnownTypeCode::UInt64)));
	// ulong -> {short, int, long} (row 8: false, false, false)
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt64), *Prim(KnownTypeCode::Int64)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: the float/double/decimal branch -- conversions exist from all
// integral types (Char..UInt64) plus float->double.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_FloatDecimalHasConversion) {
	// integral -> float/double/decimal
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Single)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Double)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Decimal)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::Single)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt64), *Prim(KnownTypeCode::Double)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int64), *Prim(KnownTypeCode::Single)));
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt64), *Prim(KnownTypeCode::Decimal)));
	// float -> double (the one floating-point-within-floating-point implicit conversion)
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Single), *Prim(KnownTypeCode::Double)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: the float/double/decimal branch cases that DO NOT exist.
// float->float, double->double are identity (not the Single->Double promotion);
// double->float, float->decimal, decimal->double, decimal->int are not implicit.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_FloatDecimalNoConversion) {
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Single), *Prim(KnownTypeCode::Single)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Double), *Prim(KnownTypeCode::Double)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Double), *Prim(KnownTypeCode::Single)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Single), *Prim(KnownTypeCode::Decimal)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Decimal), *Prim(KnownTypeCode::Double)));
	// decimal -> int: to=Int32 (integral), from=Decimal (15 > UInt64 12) -> not in table range
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Decimal), *Prim(KnownTypeCode::Int32)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: native-integer conversions that EXIST. nint (from-side Int64) ->
// long; int -> nint (to-side Int32); nuint (from-side UInt64) -> ulong; uint -> nuint; nint ->
// float/double (from-side Int64 is integral).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_NativeIntegersHaveConversion) {
	// nint (from-side Int64) -> long (Int64): lookup[long=7][long=4] = true
	EXPECT_TRUE(ImplicitNumericConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::Int64)));
	// nint (from-side Int64) -> float/double (Int64 is integral)
	EXPECT_TRUE(ImplicitNumericConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::Single)));
	EXPECT_TRUE(ImplicitNumericConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::Double)));
	// int (Int32) -> nint (to-side Int32): lookup[int=5][int=2] = true
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *NativeInt(TypeKind::NInt)));
	// nuint (from-side UInt64) -> ulong (UInt64): lookup[ulong=8][ulong=5] = true
	EXPECT_TRUE(ImplicitNumericConversion(*NativeInt(TypeKind::NUInt), *Prim(KnownTypeCode::UInt64)));
	// uint (UInt32) -> nuint (to-side UInt32): lookup[uint=6][uint=3] = true
	EXPECT_TRUE(ImplicitNumericConversion(*Prim(KnownTypeCode::UInt32), *NativeInt(TypeKind::NUInt)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: native-integer conversions that DO NOT EXIST. nint (from-side
// Int64) -> int narrows (64->32); long -> nint (to-side Int32) narrows; nuint -> uint narrows;
// float -> nint is not implicit (float->integral is explicit).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_NativeIntegersNoConversion) {
	// nint (from-side Int64) -> int (Int32): lookup[long=7][int=2] = false (long does not -> int)
	EXPECT_FALSE(ImplicitNumericConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::Int32)));
	// long (Int64) -> nint (to-side Int32): lookup[long=7][int=2] = false
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int64), *NativeInt(TypeKind::NInt)));
	// nuint (from-side UInt64) -> uint (UInt32): lookup[ulong=8][uint=3] = false
	EXPECT_FALSE(ImplicitNumericConversion(*NativeInt(TypeKind::NUInt), *Prim(KnownTypeCode::UInt32)));
	// float (Single) -> nint (to-side Int32): float is not integral -> not in table range
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Single), *NativeInt(TypeKind::NInt)));
}

// ---------------------------------------------------------------------------
// ImplicitNumericConversion: non-numeric types have no numeric conversion.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsNumericTest, ImplicitNumericConversion_NonNumericNoConversion) {
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::String)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::String), *Prim(KnownTypeCode::Int32)));
	EXPECT_FALSE(ImplicitNumericConversion(*Prim(KnownTypeCode::Object), *Prim(KnownTypeCode::Int32)));
}
