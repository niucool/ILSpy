// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the CSharpConversions "better conversion" region (CSharpConversions.cs lines
// 1620-1710, C# 9.0 spec section 12.6.4.5-12.6.4.7): `Detail::IsBetterIntegralType` (the pure
// integral-type tiebreak), `Detail::BetterConversionTarget` (the recursive worker -- the
// ReadOnlySpan/Span tiebreak arms, the core implicit-convertibility check, the deferred
// UnpackTask recursion, and the integral tiebreak), and the public
// `CSharpConversions::BetterConversion(IType, IType, IType)` (the "better conversion from
// type" entry point).
//
// Test-stub conventions (the D523-D532 precedent, reused from CSharpConversionsImplicitExplicit_Test):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    for the integral primitives the integral tiebreak reads.
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference-conversion guard). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit fires); a `RefDef` with `KnownTypeCode::None` is a custom class.
//  * `ReadOnlySpanDef` / `SpanDef` are `LookupTypeDefinition`s carrying `KnownTypeCode::
//    ReadOnlySpanOfT` / `SpanOfT` (structs); `ReadOnlySpanOf(T)` / `SpanOf(T)` are 1-arg
//    `ParameterizedType`s over them, so `IsKnownType(it, ReadOnlySpanOfT)` resolves via
//    `GetDefinition()->KnownTypeCode` and `TypeArguments()[0]` is the element type.
//  * `LookupTypeDefinition::StructuralEquals` is identity (`this == &other`), so the identity and
//    Span-element-equality crux cases MUST reuse the SAME `ITypePtr` instance for both sides; the
//    integral-widening / reference-widening crux cases use DISTINCT instances (int vs long,
//    RefDef(None) vs ObjectDef).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public BetterConversion)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsBetterIntegralType / BetterConversionTarget
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the dispatch return singletons)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // KnownType, ParameterizedType
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // TypeCode
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace R = ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::BetterConversionTarget;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsBetterIntegralType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a
// reference type -- the faithful value the reference-conversion guard needs; the base
// `LookupTypeDefinition` default is `std::nullopt`, which fails the guard). Inherits the
// `LookupTypeDefinition` ctor; the only override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Def(Int32)`
// reports `TypeCode::Int32`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive integral definition as an `ITypePtr` (for dereferencing to the `const IType&`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object` makes
// `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit fires).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
    int n = static_cast<int>(ktc);
    std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the `IsSubtypeOf` short-circuit fires).
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
    return d;
}

// A custom reference type (`Class`, `KnownTypeCode::None`) -- a derived-class stub. Distinct from
// `ObjectDef` so `IsSubtypeOf(Derived, Object)` short-circuits via the Object `IsKnownType` but
// `IsSubtypeOf(Object, Derived)` does not (Object's only base type is itself).
std::shared_ptr<RefDef> DerivedDef() {
    static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
    return d;
}

// The `System.ReadOnlySpan`1` generic definition (a struct, `KnownTypeCode::ReadOnlySpanOfT`).
std::shared_ptr<LookupTypeDefinition> ReadOnlySpanDef() {
    static auto d = std::make_shared<LookupTypeDefinition>("ReadOnlySpan`1", "System",
        FullTypeName(TopLevelTypeName("System", "ReadOnlySpan`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::ReadOnlySpanOfT);
    return d;
}

// The `System.Span`1` generic definition (a struct, `KnownTypeCode::SpanOfT`).
std::shared_ptr<LookupTypeDefinition> SpanDef() {
    static auto d = std::make_shared<LookupTypeDefinition>("Span`1", "System",
        FullTypeName(TopLevelTypeName("System", "Span`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::SpanOfT);
    return d;
}

// `ReadOnlySpan<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.ReadOnlySpan`1` definition). `IsKnownType(it, ReadOnlySpanOfT)` resolves via
// `GetDefinition()->KnownTypeCode`; `TypeArguments()[0]` is the element type.
ITypePtr ReadOnlySpanOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(ReadOnlySpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// `Span<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Span`1` definition).
ITypePtr SpanOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}

} // namespace

// ===========================================================================
// Detail::IsBetterIntegralType(TypeCode, TypeCode) (CSharpConversions.cs line 1697).
//
//   switch (t1) {
//     case SByte: return t2 == Byte || t2 == UInt16 || t2 == UInt32 || t2 == UInt64;
//     case Int16: return t2 == UInt16 || t2 == UInt32 || t2 == UInt64;
//     case Int32: return t2 == UInt32 || t2 == UInt64;
//     case Int64: return t2 == UInt64;
//     default: return false;
//   }
//
// The C# 9.0 spec section 12.6.4.7 rule: a signed integral type is a better conversion target than
// an unsigned integral type when the signed type's range fully overlaps the unsigned type's. The
// pure helper takes two `TypeCode` values; no `IType` is consulted.
// ===========================================================================

TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeSByteBeatsUnsignedTypes) {
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::SByte, TypeCode::Byte));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::SByte, TypeCode::UInt16));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::SByte, TypeCode::UInt32));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::SByte, TypeCode::UInt64));
}

TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeInt16BeatsWiderUnsignedTypes) {
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int16, TypeCode::UInt16));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int16, TypeCode::UInt32));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int16, TypeCode::UInt64));
}

TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeInt32BeatsUInt32AndUInt64) {
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int32, TypeCode::UInt32));
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int32, TypeCode::UInt64));
}

TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeInt64BeatsUInt64) {
	EXPECT_TRUE(IsBetterIntegralType(TypeCode::Int64, TypeCode::UInt64));
}

// A signed type does NOT beat another signed type (the rule is signed-better-than-unsigned only).
TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeSignedDoesNotBeatSigned) {
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::SByte, TypeCode::Int16));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Int32, TypeCode::Int64));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Int32, TypeCode::Int32));
}

// A signed type does NOT beat an unsigned type SMALLER than it (Int32 does not beat Byte/UInt16 --
// the unsigned type's range is not a subset of the signed type's).
TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeSignedDoesNotBeatSmallerUnsigned) {
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Int32, TypeCode::Byte));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Int32, TypeCode::UInt16));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Int64, TypeCode::UInt16));
}

// An unsigned type is NEVER a better conversion target than a signed type (the `t1` switch has no
// unsigned cases -> the default returns false).
TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeUnsignedIsNeverBetter) {
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Byte, TypeCode::SByte));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::UInt32, TypeCode::Int32));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::UInt64, TypeCode::Int64));
}

// A non-integral `TypeCode` (Boolean, Char, Empty) as `t1` hits the default -> false.
TEST(CSharpConversionsBetterConversionTest, IsBetterIntegralTypeNonIntegralIsNeverBetter) {
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Boolean, TypeCode::Int32));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Char, TypeCode::UInt16));
	EXPECT_FALSE(IsBetterIntegralType(TypeCode::Empty, TypeCode::Int32));
}

// ===========================================================================
// Detail::BetterConversionTarget(IType, IType) (CSharpConversions.cs line 1660).
//
//   // ReadOnlySpan/Span tiebreak arms (the C# 9 ref-struct preference)
//   if (t1.IsKnownType(ReadOnlySpanOfT)) { ... return 1; }
//   if (t2.IsKnownType(ReadOnlySpanOfT)) { ... return 2; }
//   // core implicit-convertibility check
//   { if (ImplicitConversion(t1,t2).IsValid && !ImplicitConversion(t2,t1).IsValid) return 1;
//     if (ImplicitConversion(t2,t1).IsValid && !ImplicitConversion(t1,t2).IsValid) return 2; }
//   // UnpackTask recursion (DEFERRED -- TaskType not ported)
//   // integral-type tiebreak
//   if (IsBetterIntegralType(t1Code, t2Code)) return 1;
//   if (IsBetterIntegralType(t2Code, t1Code)) return 2;
//   return 0;
//
// Returns 0 (neither), 1 (t1 better), or 2 (t2 better). The "better" target is the more specific
// (smaller) type: the one the OTHER converts to implicitly but not back, or the signed integral
// type in a signed/unsigned pair.
// ===========================================================================

// `int -> int` (same instance): the core check is identity both ways (neither wins); the integral
// tiebreak is `Int32` vs `Int32` (neither signed-beats-unsigned) -> 0.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetSameTypeIsNeither) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *intType, *intType), 0);
}

// `(int, long)`: `int` converts to `long` (numeric widening) but `long` does not convert to `int`
// (no implicit narrowing) -> the core check returns 1 (`int` is the better, more specific target).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetIntBeatsLong) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *intType, *longType), 1);
}

// `(long, int)`: the mirror -- `int` is better -> returns 2.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetLongWorseThanInt) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *longType, *intType), 2);
}

// `(Derived, Object)`: `Derived` converts to `Object` (reference widening via the `IsKnownType`
// Object short-circuit) but `Object` does not convert to `Derived` (no implicit downcast) -> the
// core check returns 1 (`Derived` is the better, more specific target).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetDerivedBeatsObject) {
	auto derived = DerivedDef();
	auto object = ObjectDef();
	EXPECT_EQ(BetterConversionTarget(Compilation(), *derived, *object), 1);
}

// `(Object, Derived)`: the mirror -- `Derived` is better -> returns 2.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetObjectWorseThanDerived) {
	auto derived = DerivedDef();
	auto object = ObjectDef();
	EXPECT_EQ(BetterConversionTarget(Compilation(), *object, *derived), 2);
}

// `(int, uint)`: no implicit conversion either way (no implicit signed<->unsigned); the integral
// tiebreak fires -- `IsBetterIntegralType(Int32, UInt32)` is true -> returns 1 (`int` is better,
// the signed-better-than-unsigned rule).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetIntBeatsUintViaIntegralTiebreak) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr uintType = Def(KnownTypeCode::UInt32);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *intType, *uintType), 1);
}

// `(uint, int)`: the mirror -- the integral tiebreak makes `int` better -> returns 2.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetUintWorseThanIntViaIntegralTiebreak) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr uintType = Def(KnownTypeCode::UInt32);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *uintType, *intType), 2);
}

// `(bool, string)`: no conversion either way (bool is not implicitly convertible to string and
// vice versa), and neither is an integral type (`GetTypeCode` yields `Boolean`/`Empty`-ish outside
// the integral range) -> neither tiebreak fires -> 0.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetInconvertiblePairIsNeither) {
	ITypePtr boolType = Def(KnownTypeCode::Boolean);
	ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *boolType, *stringType), 0);
}

// `(uint, long)`: `uint` converts to `long` (numeric widening -- the unsigned 32-bit type
// widens to the signed 64-bit type, whose range fully contains it) but `long` does not convert to
// `uint` (no implicit signed->unsigned narrowing) -> the core check returns 1 (`uint` is the better,
// more specific target -- it is the one whose range is a subset, so the OTHER converts TO it via
// widening but not back). The integral tiebreak is NOT reached (the core check already decided).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetUintBeatsLongViaCoreConvertibility) {
	ITypePtr uintType = Def(KnownTypeCode::UInt32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *uintType, *longType), 1);
}

// ---------------------------------------------------------------------------
// ReadOnlySpan/Span tiebreak arms (C# 9 spec section 12.6.4.7 -- the ref-struct preference).
// ---------------------------------------------------------------------------

// `(ReadOnlySpan<int>, Span<int>)`: the ReadOnlySpan-vs-Span identity arm -- `IdentityConversion`
// on the element types (`int` == `int`, same instance) is true -> returns 1 (`ReadOnlySpan` is the
// better target; a `ReadOnlySpan` is preferred over a `Span` when the element types match).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetReadOnlySpanBeatsSpan) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto ros = ReadOnlySpanOf(intType);
	auto span = SpanOf(intType);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *ros, *span), 1);
}

// `(Span<int>, ReadOnlySpan<int>)`: the mirror arm -- returns 2 (`ReadOnlySpan` is better).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetSpanWorseThanReadOnlySpan) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto ros = ReadOnlySpanOf(intType);
	auto span = SpanOf(intType);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *span, *ros), 2);
}

// `(ReadOnlySpan<int>, ReadOnlySpan<long>)`: the ReadOnlySpan-vs-ReadOnlySpan arm -- `int` converts
// to `long` (numeric widening) but `long` does not convert to `int` -> `t1To2 && !t2To1` -> returns
// 1 (`ReadOnlySpan<int>` is the better, more specific target).
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetReadOnlySpanIntBeatsLong) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	auto rosInt = ReadOnlySpanOf(intType);
	auto rosLong = ReadOnlySpanOf(longType);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *rosInt, *rosLong), 1);
}

// `(ReadOnlySpan<long>, ReadOnlySpan<int>)`: the mirror arm -- returns 2.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetReadOnlySpanLongWorseThanInt) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	auto rosInt = ReadOnlySpanOf(intType);
	auto rosLong = ReadOnlySpanOf(longType);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *rosLong, *rosInt), 2);
}

// `(ReadOnlySpan<int>, ReadOnlySpan<int>)` (same element instance): both ReadOnlySpan-vs-ReadOnlySpan
// arms find the element identity both ways (neither wins), the core check is identity both ways
// (neither wins), and the integral tiebreak is `Empty` vs `Empty` (ReadOnlySpan is not an integral
// type) -> 0.
TEST(CSharpConversionsBetterConversionTest, BetterConversionTargetSameReadOnlySpanIsNeither) {
	ITypePtr intType = Def(KnownTypeCode::Int32);
	auto ros = ReadOnlySpanOf(intType);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *ros, *ros), 0);
}

// ===========================================================================
// CSharpConversions::BetterConversion(IType, IType, IType) (CSharpConversions.cs line 1620).
//
//   bool ident1 = IdentityConversion(s, t1);
//   bool ident2 = IdentityConversion(s, t2);
//   if (ident1 && !ident2) return 1;
//   if (ident2 && !ident1) return 2;
//   return BetterConversionTarget(t1, t2);
//
// The public "better conversion from type" entry point: an identity conversion from the source `s`
// to a target beats a non-identity conversion; when neither (or both) is identity, the verdict
// falls to `BetterConversionTarget`.
// ===========================================================================

// `s=int, t1=int (same instance), t2=long`: `ident1` is identity (true), `ident2` is not (false) ->
// returns 1 (`t1` is the better target -- the source exactly matches it).
TEST(CSharpConversionsBetterConversionTest, PublicBetterConversionIdentityBeatsNonIdentity) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	EXPECT_EQ(conversions.BetterConversion(*intType, *intType, *longType), 1);
}

// `s=int, t1=long, t2=int (same instance)`: the mirror -- `ident2` is identity, `ident1` is not ->
// returns 2.
TEST(CSharpConversionsBetterConversionTest, PublicBetterConversionNonIdentityWorseThanIdentity) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr longType = Def(KnownTypeCode::Int64);
	EXPECT_EQ(conversions.BetterConversion(*intType, *longType, *intType), 2);
}

// `s=int, t1=int, t2=int` (all the same instance): both `ident1` and `ident2` are identity -> falls
// to `BetterConversionTarget(int, int)` which is 0 (same type, neither better).
TEST(CSharpConversionsBetterConversionTest, PublicBetterConversionBothIdentityFallsToTarget) {
	CSharpConversions conversions(Compilation());
	ITypePtr intType = Def(KnownTypeCode::Int32);
	EXPECT_EQ(conversions.BetterConversion(*intType, *intType, *intType), 0);
}

// `s=byte, t1=int, t2=uint`: neither target is identity with the source (`byte` != `int`,
// `byte` != `uint`) -> falls to `BetterConversionTarget(int, uint)` which is 1 (the signed
// integral tiebreak). The CRUX: the public method delegates to `BetterConversionTarget` when
// neither target is identity.
TEST(CSharpConversionsBetterConversionTest, PublicBetterConversionNeitherIdentityDelegatesToTarget) {
	CSharpConversions conversions(Compilation());
	ITypePtr byteType = Def(KnownTypeCode::Byte);
	ITypePtr intType = Def(KnownTypeCode::Int32);
	ITypePtr uintType = Def(KnownTypeCode::UInt32);
	EXPECT_EQ(conversions.BetterConversion(*byteType, *intType, *uintType), 1);
}

// `s=long, t1=int, t2=long (same instance as s)`: `ident1 = IdentityConversion(long, int)` is false,
// `ident2 = IdentityConversion(long, long)` is true (same instance) -> returns 2. The CRUX: the
// identity check on `t2` uses the SAME instance as `s` so `IdentityConversion` (which erases both
// types then compares with `IType::Equals` / `StructuralEquals` identity) is true.
TEST(CSharpConversionsBetterConversionTest, PublicBetterConversionIdentityOnSecondTarget) {
	CSharpConversions conversions(Compilation());
	ITypePtr longType = Def(KnownTypeCode::Int64);
	ITypePtr intType = Def(KnownTypeCode::Int32);
	EXPECT_EQ(conversions.BetterConversion(*longType, *intType, *longType), 2);
}
