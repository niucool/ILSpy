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

// Tests for the `CSharpConversions.StandardImplicitConversion` dispatch entry point
// (CSharpConversions.cs line 201, C# 9.0 spec section 10.4.2): `Detail::StandardImplicitConversion`
// and the public `CSharpConversions::StandardImplicitConversion` method. This is the standard
// implicit conversion dispatch that wires all the already-ported conversion helpers together in
// spec order: identity, numeric, nullable (returns a Conversion, checked via pointer-identity
// against `Conversions::None()`), null-literal, reference, boxing, type-parameter (yields a
// boxing conversion when not also a reference conversion), pointer. The tuple/inline-array/span
// arms are deferred (need TupleResolveResult/IsInlineArrayType/Span machinery) and yield None for
// those shapes until ported.
//
// CRUX STUB CONVENTIONS (carried from the D514-D520 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric/constant-expression helpers).
//    `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    identity-conversion crux cases must reuse the SAME instance for both sides.
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//    Used for the boxing from-side (needs a definite `false` IsReferenceType) and the null-literal
//    to-side (a reference type).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit), so `int -> object` / `T -> object` are valid boxing/subtype
//    conversions without modelling a base-type chain.
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition (a
//    1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the element. The lifted-identity test
//    (`int -> Nullable<int>`) reuses the SAME int instance for the from-side and the Nullable's
//    type argument (a distinct pair would fire identity on the underlying types after the strip).
//  * `SpecialType(TypeKind::Null, true)` is the null-literal type (`Kind == Null`,
//    `IsReferenceType == true`); the null-literal arm checks `fromType.Kind == Null`.
//  * `LookupTypeParameter` (unconstrained, `IsReferenceType == std::nullopt`) is the faithful stub
//    for the type-parameter arm: it fails the reference/boxing guards (needs a definite
//    `true`/`false`) but reaches the `ImplicitTypeParameterConversion` `IsSubtypeOf` arm (the
//    `!HasValue` guard passes).
//  * `PointerType` over `KnownType(Void)` has `ReflectionName() == "System.Void*"` (the target the
//    pointer void*-arm checks); `PointerType` over `KnownType(Int32)` has `"System.Int32*"` (an
//    any-pointer source).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public method)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::StandardImplicitConversion
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the dispatch return singletons)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::StandardImplicitConversion;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

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
// `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit).
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

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
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

// The null-literal type (`TypeKind::Null`). Faithful to the C# `SpecialType.NullType` singleton
// (`isReferenceType: true`); the null-literal arm checks `fromType.Kind == Null`.
ITypePtr NullType() {
    return std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
}

// An unconstrained type parameter (`IsReferenceType == std::nullopt`, the inherited default).
// Fails the reference/boxing guards (needs a definite `true`/`false`) but reaches the
// `ImplicitTypeParameterConversion` `IsSubtypeOf` arm (the `!HasValue` guard passes).
ITypePtr TypeParam() {
    return std::make_shared<LookupTypeParameter>("T");
}

// `System.Void*` -- a `PointerType` over `KnownType(Void)`. `PointerType::ReflectionName()` is
// `element->ReflectionName() + "*"`, and `KnownType(Void).ReflectionName()` is `"System.Void"`,
// so the result is `"System.Void*"` (the target the pointer void*-arm checks).
ITypePtr VoidPtr() {
    return std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Void));
}

// `int*` -- a `PointerType` over `KnownType(Int32)`. `ReflectionName()` is `"System.Int32*"`.
// An any-pointer source for the pointer arm (IsAnyPointer(Kind) is true for a PointerType).
ITypePtr IntPtr() {
    return std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
}

} // namespace

// ===========================================================================
// StandardImplicitConversion (CSharpConversions.cs line 201, spec 10.4.2).
//
//   if (IdentityConversion(from, to)) return IdentityConversion;
//   if (ImplicitNumericConversion(from, to)) return ImplicitNumericConversion;
//   c = ImplicitNullableConversion(from, to); if (c != None) return c;
//   if (NullLiteralConversion(from, to)) return NullLiteralConversion;
//   if (ImplicitReferenceConversion(from, to, 0)) return ImplicitReferenceConversion;
//   if (IsBoxingConversion(from, to)) return BoxingConversion;
//   if (ImplicitTypeParameterConversion(from, to)) return BoxingConversion;
//   if (ImplicitPointerConversion(from, to)) return ImplicitPointerConversion;
//   // tuple/inline-array/span deferred -> None
//   return None;
// ===========================================================================

// ---------------------------------------------------------------------------
// Identity arm -- same type instance -> IdentityConversion singleton.
// ---------------------------------------------------------------------------

// `int -> int` (same instance): IdentityConversion fires first (TypeErasure + Equals on the
// same instance is true). The dispatch returns the IdentityConversion singleton. `Def(Int32)`
// is a `LookupTypeDefinition` (StructuralEquals is identity), so reusing the SAME instance for
// both sides makes the erasure-equality hold.
TEST(CSharpConversionsStandardImplicitTest, IdentityConversionFiresForSameType) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = StandardImplicitConversion(Compilation(), *intType, *intType);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// ---------------------------------------------------------------------------
// Numeric arm -- int -> long (widening) -> ImplicitNumericConversion singleton.
// ---------------------------------------------------------------------------

// `int -> long`: identity fails (distinct instances, different KnownTypeCode), then
// ImplicitNumericConversion fires (Int32 -> Int64 is an implicit widening). Returns the
// ImplicitNumericConversion singleton. The two `Def` instances are distinct (StructuralEquals is
// identity), so the top-level IdentityConversion arm does NOT short-circuit.
TEST(CSharpConversionsStandardImplicitTest, NumericConversionFiresForWidening) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    auto c = StandardImplicitConversion(Compilation(), *fromInt, *toLong);
    EXPECT_EQ(c.get(), Conversions::ImplicitNumericConversion().get());
}

// ---------------------------------------------------------------------------
// Nullable arm -- lifted identity: int -> Nullable<int> -> ImplicitNullableConversion singleton.
// ---------------------------------------------------------------------------

// `int -> Nullable<int>`: identity fails (int is a Struct, Nullable<int> is a ParameterizedType --
// different kinds), numeric fails (Nullable<int> is not numeric), then ImplicitNullableConversion
// fires: IsNullable(toType) true, GetUnderlyingType(toType)=int (the SAME instance as fromType),
// GetUnderlyingType(fromType)=int (not nullable, returns fromType), IdentityConversion(int, int)
// on the same instance is true -> ImplicitNullableConversion. The SAME int instance is reused for
// the from-side and the Nullable's type argument (a distinct pair would still work, but the same
// instance pins the lifted-identity crux).
TEST(CSharpConversionsStandardImplicitTest, NullableConversionFiresForLiftedIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    auto c = StandardImplicitConversion(Compilation(), *intType, *nullableInt);
    EXPECT_EQ(c.get(), Conversions::ImplicitNullableConversion().get());
}

// ---------------------------------------------------------------------------
// Nullable arm -- lifted numeric: int -> Nullable<long> -> ImplicitLiftedNumericConversion singleton.
// ---------------------------------------------------------------------------

// `int -> Nullable<long>`: identity fails, numeric fails (Nullable<long> not numeric), then
// ImplicitNullableConversion fires: IsNullable(toType) true, GetUnderlyingType(toType)=long,
// GetUnderlyingType(fromType)=int (not nullable), IdentityConversion(int, long) fails (distinct
// instances), ImplicitNumericConversion(int, long) true (widening) -> ImplicitLiftedNumericConversion.
TEST(CSharpConversionsStandardImplicitTest, NullableConversionFiresForLiftedNumeric) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toNullableLong = NullableOf(Def(KnownTypeCode::Int64));
    auto c = StandardImplicitConversion(Compilation(), *fromInt, *toNullableLong);
    EXPECT_EQ(c.get(), Conversions::ImplicitLiftedNumericConversion().get());
}

// ---------------------------------------------------------------------------
// Null-literal arm -- null -> object -> NullLiteralConversion singleton.
// ---------------------------------------------------------------------------

// `null -> object`: identity fails (null != object), numeric fails, nullable returns None (null is
// not Nullable<T>), then NullLiteralConversion fires: fromType.Kind == Null, toType is a reference
// type (ObjectDef). Returns the NullLiteralConversion singleton.
TEST(CSharpConversionsStandardImplicitTest, NullLiteralConversionFiresForNullToReferenceType) {
    ITypePtr nullType = NullType();
    auto c = StandardImplicitConversion(Compilation(), *nullType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::NullLiteralConversion().get());
}

// ---------------------------------------------------------------------------
// Reference arm -- string -> object -> ImplicitReferenceConversion singleton.
// ---------------------------------------------------------------------------

// `string -> object`: identity fails, numeric fails, nullable None, null-literal fails (Kind !=
// Null), then ImplicitReferenceConversion fires: both IsReferenceType true (KnownType(String) and
// ObjectDef), IsSubtypeOf(string, object, 0) short-circuits via IsKnownType(object, Object).
// Returns the ImplicitReferenceConversion singleton. `KnownType(String)` has IsReferenceType ==
// true (derived from Kind == Class), so the guard passes.
TEST(CSharpConversionsStandardImplicitTest, ReferenceConversionFiresForStringToObject) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = StandardImplicitConversion(Compilation(), *stringType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::ImplicitReferenceConversion().get());
}

// ---------------------------------------------------------------------------
// Boxing arm -- int -> object -> BoxingConversion singleton.
// ---------------------------------------------------------------------------

// `int -> object`: identity fails, numeric fails, nullable None, null-literal fails, reference
// fails (KnownType(Int32).IsReferenceType == false, the guard needs true), then IsBoxingConversion
// fires: GetUnderlyingType(int)=int (not nullable), int.IsReferenceType == false (KnownType(Int32)
// derived from Kind == Struct), !int.IsByRefLike (KnownType default), object.IsReferenceType ==
// true (ObjectDef), IsSubtypeOf(int, object, 0) short-circuits via IsKnownType(object, Object).
// Returns the BoxingConversion singleton. `KnownType(Int32)` (NOT an `ITypeDefinition`) is the
// faithful value-type stub (IsReferenceType == false, definite); a `Def(Int32)` (a
// `LookupTypeDefinition`) has IsReferenceType == nullopt (the default), which fails the boxing guard.
TEST(CSharpConversionsStandardImplicitTest, BoxingConversionFiresForIntToObject) {
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto c = StandardImplicitConversion(Compilation(), *intType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::BoxingConversion().get());
}

// ---------------------------------------------------------------------------
// Type-parameter arm (as boxing) -- T -> object -> BoxingConversion singleton.
// ---------------------------------------------------------------------------

// `T -> object`: identity fails, numeric fails, nullable None, null-literal fails, reference fails
// (T.IsReferenceType == nullopt, the guard needs true), boxing fails (T.IsReferenceType ==
// nullopt, the boxing guard needs false), then ImplicitTypeParameterConversion fires:
// T.Kind == TypeParameter, T.IsReferenceType.HasValue == false (nullopt), IsSubtypeOf(T, object, 0)
// short-circuits via IsKnownType(object, Object). Returns BoxingConversion (the C# comment:
// implicit type parameter conversions that aren't also reference conversions are boxing).
TEST(CSharpConversionsStandardImplicitTest, TypeParameterConversionFiresAsBoxingForTToObject) {
    ITypePtr t = TypeParam();
    auto c = StandardImplicitConversion(Compilation(), *t, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::BoxingConversion().get());
}

// ---------------------------------------------------------------------------
// Pointer arm -- int* -> void* -> ImplicitPointerConversion singleton.
// ---------------------------------------------------------------------------

// `int* -> void*`: identity fails (distinct types), numeric fails, nullable None, null-literal
// fails, reference fails (PointerType IsReferenceType is nullopt, the guard needs true), boxing
// fails, type-parameter fails (int*.Kind != TypeParameter), then ImplicitPointerConversion fires:
// IsAnyPointer(int*.Kind) true, toType is a PointerType, toType.ReflectionName() == "System.Void*".
// Returns the ImplicitPointerConversion singleton.
TEST(CSharpConversionsStandardImplicitTest, PointerConversionFiresForIntPtrToVoidPtr) {
    ITypePtr intPtr = IntPtr();
    ITypePtr voidPtr = VoidPtr();
    auto c = StandardImplicitConversion(Compilation(), *intPtr, *voidPtr);
    EXPECT_EQ(c.get(), Conversions::ImplicitPointerConversion().get());
}

// ---------------------------------------------------------------------------
// None case -- int -> void*: no arm fires -> Conversions::None().
// ---------------------------------------------------------------------------

// `int -> void*`: identity fails, numeric fails (void* not numeric), nullable None, null-literal
// fails (Kind != Null), reference fails (int.IsReferenceType == false), boxing fails
// (void*.IsReferenceType is nullopt, the to-side guard needs true), type-parameter fails
// (int.Kind != TypeParameter), pointer fails (IsAnyPointer(int.Kind) false -- int is a Struct,
// not a pointer; fromType.Kind != Null; no function-pointer match). No arm fires -> None.
TEST(CSharpConversionsStandardImplicitTest, NoneReturnedWhenNoArmFires) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr voidPtr = VoidPtr();
    auto c = StandardImplicitConversion(Compilation(), *intType, *voidPtr);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// Public method delegation -- CSharpConversions::StandardImplicitConversion delegates to Detail.
// ---------------------------------------------------------------------------

// The public `CSharpConversions::StandardImplicitConversion(from, to)` delegates to
// `Detail::StandardImplicitConversion(*compilation_, from, to)`. Construct a `CSharpConversions`
// over the `LookupCompilation` and verify the public method returns the same singleton the Detail
// dispatch does for the identity case (int -> int, same instance).
TEST(CSharpConversionsStandardImplicitTest, PublicMethodDelegatesToDetail) {
    CSharpConversions conversions(Compilation());
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = conversions.StandardImplicitConversion(*intType, *intType);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}
