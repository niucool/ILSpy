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

// Tests for the `CSharpConversions` explicit-enumeration-conversion helper --
// `Detail::ExplicitEnumerationConversion` (a faithful port of the C#
// `CSharpConversions.ExplicitEnumerationConversion`, CSharpConversions.cs line 474, C# spec
// draft-v11 section 10.3.3 "explicit enumeration conversions"). The helper reads `IType.Kind`
// and `Detail::IsNumericType` (no `CSharpConversions` instance state), so it lives as a
// `Detail::` free function taking `const IType&`. It dispatches to the already-ported
// `IsNumericType`, which recognizes the native integers (nint/nuint) via the `Kind`
// short-circuit; the crux cases below pin that the explicit-enum<->native-integer pairs route
// through `IsNumericType`.
//
// The test stubs are `LookupTypeDefinition`s (IS-A `ITypeDefinition`, so `GetTypeCode` resolves
// the `KnownTypeCode`). An enum stub is `Kind == Enum` with `KnownTypeCode == None` (a real enum
// carries no `KnownTypeCode`, so `GetTypeCode == Empty` and `IsNumericType` is false -- the
// helper never calls `IsNumericType` on the enum side, it reads `Kind` directly). The numeric
// and native-integer stubs reuse the `Prim` / `NativeInt` factories from the numeric test
// (`CSharpConversionsNumeric_Test.cpp`).

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

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitEnumerationConversion;
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

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via
// the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Prim(Int32)`
// reports `TypeCode::Int32` -- the observable behavior of a real primitive definition.
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
// `GetTypeCode == Empty`, faithfully -- the real `nint`/`nuint` have no `KnownTypeCode`).
// `IsNumericType` recognizes these via the `Kind` short-circuit.
std::shared_ptr<LookupTypeDefinition> NativeInt(TypeKind k) {
	assert(k == TypeKind::NInt || k == TypeKind::NUInt);
	return MakeDef(KnownTypeCode::None, k);
}

// An enum definition: `Kind == Enum` and `KnownTypeCode == None` (a real enum carries no
// `KnownTypeCode`, so `GetTypeCode == Empty` and `IsNumericType` is false). The helper reads
// `Kind` directly on the enum side, so it never asks `IsNumericType` of an enum.
std::shared_ptr<LookupTypeDefinition> EnumDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Enum);
}

// A non-numeric, non-enum reference type (Object/String report a `TypeCode` outside
// [Char..Decimal]; a plain `Class`-kind stub with `KnownTypeCode == None` reports `Empty`). All
// three are not numeric and not enum, so neither `if` arm fires.
std::shared_ptr<LookupTypeDefinition> Ref(KnownTypeCode ktc) {
	return MakeDef(ktc, TypeKind::Class);
}

} // namespace

// ---------------------------------------------------------------------------
// Enum -> enum: an explicit enumeration conversion exists between any two enum types.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, EnumToEnum) {
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *EnumDef()));
}

// ---------------------------------------------------------------------------
// Enum -> numeric: an explicit enumeration conversion exists from any enum to any numeric
// primitive (the C# spec lists sbyte..decimal; the helper routes through IsNumericType, which
// also admits the native integers).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, EnumToNumeric) {
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::Int32)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::Double)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::Char)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::UInt64)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::Decimal)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::SByte)));
}

// ---------------------------------------------------------------------------
// Numeric -> enum: an explicit enumeration conversion exists from any numeric primitive to any
// enum type.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NumericToEnum) {
	EXPECT_TRUE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Int32), *EnumDef()));
	EXPECT_TRUE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Double), *EnumDef()));
	EXPECT_TRUE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Char), *EnumDef()));
	EXPECT_TRUE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::UInt64), *EnumDef()));
	EXPECT_TRUE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Byte), *EnumDef()));
}

// ---------------------------------------------------------------------------
// Native integer -> enum: nint/nuint are numeric via the IsNumericType `Kind` short-circuit, so
// they convert explicitly to any enum. This pins that the helper dispatches to IsNumericType
// (which recognizes the native integers) rather than a GetTypeCode-only check.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NativeIntegerToEnum) {
	EXPECT_TRUE(ExplicitEnumerationConversion(*NativeInt(TypeKind::NInt), *EnumDef()));
	EXPECT_TRUE(ExplicitEnumerationConversion(*NativeInt(TypeKind::NUInt), *EnumDef()));
}

// ---------------------------------------------------------------------------
// Enum -> native integer: an enum converts explicitly to nint/nuint (the to-side is numeric via
// the IsNumericType `Kind` short-circuit). Symmetric counterpart to the above.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, EnumToNativeInteger) {
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *NativeInt(TypeKind::NInt)));
	EXPECT_TRUE(ExplicitEnumerationConversion(*EnumDef(), *NativeInt(TypeKind::NUInt)));
}

// ---------------------------------------------------------------------------
// Enum -> non-numeric, non-enum: an enum does not explicitly convert to a reference type that is
// neither enum nor numeric (String, Object, a plain Class). The `Kind == Enum` arm fires, then
// `to.Kind == Enum` is false and `IsNumericType(to)` is false.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, EnumToNonNumericNonEnum) {
	EXPECT_FALSE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::String)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*EnumDef(), *Prim(KnownTypeCode::Object)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*EnumDef(), *Ref(KnownTypeCode::None)));
}

// ---------------------------------------------------------------------------
// Non-numeric, non-enum -> enum: a reference type that is neither enum nor numeric does not
// explicitly convert to an enum. The `Kind == Enum` arm does not fire (from is not enum) and
// `IsNumericType(from)` is false, so the `else if` does not fire either.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NonNumericNonEnumToEnum) {
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::String), *EnumDef()));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Object), *EnumDef()));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Ref(KnownTypeCode::None), *EnumDef()));
}

// ---------------------------------------------------------------------------
// Numeric -> numeric: two numeric types do not form an explicit *enumeration* conversion (the
// numeric->numeric direction is the numeric helpers, not this one). from is numeric, so the
// `else if` arm fires, but `to.Kind == Enum` is false.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NumericToNumeric) {
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Int64)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Double)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Char), *Prim(KnownTypeCode::UInt64)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Single), *Prim(KnownTypeCode::Decimal)));
}

// ---------------------------------------------------------------------------
// Numeric -> non-numeric, non-enum: a numeric type does not explicitly convert to a reference
// type that is neither enum nor numeric (String, Object). from is numeric, so the `else if` arm
// fires, but `to.Kind == Enum` is false.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NumericToNonNumericNonEnum) {
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::String)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Int32), *Prim(KnownTypeCode::Object)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*NativeInt(TypeKind::NInt), *Prim(KnownTypeCode::String)));
}

// ---------------------------------------------------------------------------
// Neither enum nor numeric on either side: two reference types (String, Object, a plain Class)
// have no explicit enumeration conversion. Neither `if` arm fires.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsEnumerationTest, NeitherEnumNorNumeric) {
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::String), *Prim(KnownTypeCode::Object)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Prim(KnownTypeCode::Object), *Prim(KnownTypeCode::String)));
	EXPECT_FALSE(ExplicitEnumerationConversion(*Ref(KnownTypeCode::None), *Ref(KnownTypeCode::None)));
}
