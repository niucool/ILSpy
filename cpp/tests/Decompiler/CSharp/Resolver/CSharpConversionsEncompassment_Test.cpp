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
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` user-defined-conversions encompassment helpers
// (CSharpConversions.cs lines 960-970): `Detail::IsEncompassedBy` and
// `Detail::IsEncompassingOrEncompassedBy`. These are the simplest helpers in the User-Defined
// Conversions region -- they delegate to the already-ported `StandardImplicitConversion` (D523)
// dispatch entry point, folding the returned `Conversion`'s `IsValid` flag. A `None` return
// (the `InvalidConversion` singleton, `IsValid` false) means no encompassment; any other return
// (a `BuiltinConversion` / `NumericOrEnumerationConversion` singleton, `IsValid` inherited true)
// means a valid standard implicit conversion exists, so the encompassment holds.
//
//   bool IsEncompassedBy(IType a, IType b)
//       => StandardImplicitConversion(a, b).IsValid;
//
//   bool IsEncompassingOrEncompassedBy(IType a, IType b)
//       => StandardImplicitConversion(a, b).IsValid
//          || StandardImplicitConversion(b, a).IsValid;
//
// CRUX STUB CONVENTIONS (carried from the D514-D523 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric/constant-expression helpers).
//    `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    identity-conversion crux cases must reuse the SAME instance for both sides.
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//    Used for the boxing from-side (needs a definite `false` IsReferenceType).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit), so `int -> object` / `string -> object` are valid
//    boxing/reference conversions without modelling a base-type chain.
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition (a
//    1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the element.
//  * `PointerType` over `KnownType(Void)` has `ReflectionName() == "System.Void*"` (the target the
//    pointer void*-arm checks); a `Def(Int32)` -> `void*` pair fires no arm (int is a Struct, not a
//    pointer), so the dispatch yields `None` -> `IsValid` false -> not encompassed.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsEncompassedBy / IsEncompassingOrEncompassedBy
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the IsValid fold)
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::IsEncompassedBy;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsEncompassingOrEncompassedBy;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
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

// `System.Void*` -- a `PointerType` over `KnownType(Void)`. `PointerType::ReflectionName()` is
// `element->ReflectionName() + "*"`, and `KnownType(Void).ReflectionName()` is `"System.Void"`,
// so the result is `"System.Void*"`. A to-side for the None case (no implicit conversion from a
// value type to a pointer type).
ITypePtr VoidPtr() {
    return std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Void));
}

} // namespace

// ===========================================================================
// IsEncompassedBy (CSharpConversions.cs line 960, spec 10.5.4).
//
//   bool IsEncompassedBy(IType a, IType b)
//       => StandardImplicitConversion(a, b).IsValid;
//
// True iff there is a standard implicit conversion from `a` to `b`. The `IsValid` fold of the
// dispatch return: `None` (InvalidConversion, IsValid false) -> false; any other singleton
// (BuiltinConversion / NumericOrEnumerationConversion, IsValid inherited true) -> true.
// ===========================================================================

// ---------------------------------------------------------------------------
// Identity -- int -> int (same instance): IdentityConversion arm fires -> IsValid true.
// ---------------------------------------------------------------------------

// `int -> int` (same instance): the top-level `IdentityConversion` arm fires (TypeErasure + Equals
// on the same instance), returning the `IdentityConversion` singleton (a `BuiltinConversion`,
// `IsValid` inherited true). So `IsEncompassedBy(int, int)` is true. `Def(Int32)` is a
// `LookupTypeDefinition` (StructuralEquals is identity), so reusing the SAME instance for both
// sides makes the erasure-equality hold.
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByTrueForIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    EXPECT_TRUE(IsEncompassedBy(Compilation(), *intType, *intType));
}

// ---------------------------------------------------------------------------
// Numeric widening -- int -> long: ImplicitNumericConversion arm fires -> IsValid true.
// ---------------------------------------------------------------------------

// `int -> long`: identity fails (distinct instances, different KnownTypeCode), then
// `ImplicitNumericConversion` fires (Int32 -> Int64 is an implicit widening). The dispatch returns
// the `ImplicitNumericConversion` singleton (`IsValid` true). So `IsEncompassedBy(int, long)` is
// true -- `int` is encompassed by `long` (every `int` value is a valid `long`).
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByTrueForNumericWidening) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    EXPECT_TRUE(IsEncompassedBy(Compilation(), *fromInt, *toLong));
}

// ---------------------------------------------------------------------------
// Numeric narrowing -- long -> int: no implicit conversion -> IsValid false.
// ---------------------------------------------------------------------------

// `long -> int`: identity fails, `ImplicitNumericConversion` fails (Int64 -> Int32 is NOT an
// implicit widening -- it is an explicit narrowing), nullable returns None (neither is
// Nullable<T>), null-literal fails (Kind != Null), reference fails (both are Structs),
// boxing fails (neither IsReferenceType == true), type-parameter fails (Kind != TypeParameter),
// pointer fails (neither IsAnyPointer). No arm fires -> `None` -> `IsValid` false. So
// `IsEncompassedBy(long, int)` is false -- `long` is NOT encompassed by `int` (not every `long`
// value fits in an `int`).
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByFalseForNumericNarrowing) {
    ITypePtr fromLong = Def(KnownTypeCode::Int64);
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    EXPECT_FALSE(IsEncompassedBy(Compilation(), *fromLong, *toInt));
}

// ---------------------------------------------------------------------------
// Reference -- string -> object: ImplicitReferenceConversion arm fires -> IsValid true.
// ---------------------------------------------------------------------------

// `string -> object`: identity fails, numeric fails, nullable None, null-literal fails (Kind !=
// Null), then `ImplicitReferenceConversion` fires: both IsReferenceType true (KnownType(String)
// derives IsReferenceType from Kind == Class -> true; ObjectDef is a RefDef -> true),
// `IsSubtypeOf(string, object, 0)` short-circuits via `IsKnownType(object, Object)`. The dispatch
// returns the `ImplicitReferenceConversion` singleton (`IsValid` true). So `IsEncompassedBy(string,
// object)` is true -- `string` is encompassed by `object` (every `string` is an `object`).
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByTrueForReferenceWidening) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_TRUE(IsEncompassedBy(Compilation(), *stringType, *ObjectDef()));
}

// ---------------------------------------------------------------------------
// Reference -- object -> string: no implicit conversion -> IsValid false.
// ---------------------------------------------------------------------------

// `object -> string`: identity fails, numeric fails, nullable None, null-literal fails, reference
// fails (`IsSubtypeOf(object, string, 0)` -- object's base-type chain does not include string),
// boxing fails (object.IsReferenceType == true, the boxing guard needs false on the from-side),
// type-parameter fails, pointer fails. No arm fires -> `None` -> `IsValid` false. So
// `IsEncompassedBy(object, string)` is false -- `object` is NOT encompassed by `string` (not every
// `object` is a `string`).
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByFalseForReferenceNarrowing) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_FALSE(IsEncompassedBy(Compilation(), *ObjectDef(), *stringType));
}

// ---------------------------------------------------------------------------
// Boxing -- int -> object: IsBoxingConversion arm fires -> IsValid true.
// ---------------------------------------------------------------------------

// `int -> object`: identity fails, numeric fails, nullable None, null-literal fails, reference
// fails (KnownType(Int32).IsReferenceType == false, the guard needs true), then `IsBoxingConversion`
// fires: GetUnderlyingType(int)=int (not nullable), int.IsReferenceType == false (KnownType(Int32)
// derived from Kind == Struct), !int.IsByRefLike (KnownType default), object.IsReferenceType ==
// true (ObjectDef), IsSubtypeOf(int, object, 0) short-circuits via IsKnownType(object, Object).
// The dispatch returns the `BoxingConversion` singleton (`IsValid` true). So `IsEncompassedBy(int,
// object)` is true -- `int` is encompassed by `object` (a boxed `int` is an `object`).
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByTrueForBoxing) {
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_TRUE(IsEncompassedBy(Compilation(), *intType, *ObjectDef()));
}

// ---------------------------------------------------------------------------
// Nullable lifted identity -- int -> Nullable<int>: ImplicitNullableConversion -> IsValid true.
// ---------------------------------------------------------------------------

// `int -> Nullable<int>`: identity fails (int is a Struct, Nullable<int> is a ParameterizedType --
// different kinds), numeric fails (Nullable<int> is not numeric), then `ImplicitNullableConversion`
// fires: IsNullable(toType) true, GetUnderlyingType(toType)=int (the SAME instance as fromType),
// GetUnderlyingType(fromType)=int (not nullable, returns fromType), IdentityConversion(int, int)
// on the same instance is true -> ImplicitNullableConversion singleton (`IsValid` true). So
// `IsEncompassedBy(int, Nullable<int>)` is true -- `int` is encompassed by `Nullable<int>`.
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByTrueForNullableLiftedIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    EXPECT_TRUE(IsEncompassedBy(Compilation(), *intType, *nullableInt));
}

// ---------------------------------------------------------------------------
// None case -- int -> void*: no arm fires -> IsValid false.
// ---------------------------------------------------------------------------

// `int -> void*`: no arm fires (int is a Struct, void* is a PointerType with indeterminate
// IsReferenceType -- the reference/boxing guards fail, the pointer arm needs an any-pointer
// from-side or a null literal). The dispatch returns `None` -> `IsValid` false. So
// `IsEncompassedBy(int, void*)` is false -- `int` is NOT encompassed by `void*`.
TEST(CSharpConversionsEncompassmentTest, IsEncompassedByFalseWhenNoArmFires) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr voidPtr = VoidPtr();
    EXPECT_FALSE(IsEncompassedBy(Compilation(), *intType, *voidPtr));
}

// ===========================================================================
// IsEncompassingOrEncompassedBy (CSharpConversions.cs line 965).
//
//   bool IsEncompassingOrEncompassedBy(IType a, IType b)
//       => StandardImplicitConversion(a, b).IsValid
//          || StandardImplicitConversion(b, a).IsValid;
//
// True iff there is a standard implicit conversion in EITHER direction. The forward-direction
// call short-circuits the `||` when it succeeds; the reverse-direction call fires only when the
// forward direction yields `None`.
// ===========================================================================

// ---------------------------------------------------------------------------
// Forward direction -- int <-> long: int->long is valid -> true (reverse not needed).
// ---------------------------------------------------------------------------

// `int <-> long`: the forward call `IsEncompassedBy(int, long)` is true (numeric widening), so the
// `||` short-circuits to true without the reverse call. `IsEncompassingOrEncompassedBy(int, long)`
// is true.
TEST(CSharpConversionsEncompassmentTest, IsEncompassingOrEncompassedByTrueForNumericWidening) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    EXPECT_TRUE(IsEncompassingOrEncompassedBy(Compilation(), *intType, *longType));
}

// ---------------------------------------------------------------------------
// Reverse direction -- long <-> int: long->int is None, int->long is valid -> true.
// ---------------------------------------------------------------------------

// `long <-> int`: the forward call `StandardImplicitConversion(long, int)` yields `None` (no
// implicit narrowing), so the `||` proceeds to the reverse call `StandardImplicitConversion(int,
// long)` which fires the numeric widening arm (`IsValid` true). So
// `IsEncompassingOrEncompassedBy(long, int)` is true -- the pair IS related by an implicit
// conversion in one direction, even though the forward direction alone fails. This is the crux
// case distinguishing `IsEncompassingOrEncompassedBy` (either direction) from `IsEncompassedBy`
// (forward only): `IsEncompassedBy(long, int)` is false but `IsEncompassingOrEncompassedBy(long,
// int)` is true.
TEST(CSharpConversionsEncompassmentTest, IsEncompassingOrEncompassedByTrueForReverseDirection) {
    ITypePtr longType = Def(KnownTypeCode::Int64);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    EXPECT_TRUE(IsEncompassingOrEncompassedBy(Compilation(), *longType, *intType));
}

// ---------------------------------------------------------------------------
// Reference -- string <-> object: string->object is valid -> true.
// ---------------------------------------------------------------------------

// `string <-> object`: the forward call `StandardImplicitConversion(string, object)` fires the
// reference arm (string -> object is an implicit reference conversion), so the `||` short-circuits
// to true. `IsEncompassingOrEncompassedBy(string, object)` is true.
TEST(CSharpConversionsEncompassmentTest, IsEncompassingOrEncompassedByTrueForReferenceWidening) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_TRUE(IsEncompassingOrEncompassedBy(Compilation(), *stringType, *ObjectDef()));
}

// ---------------------------------------------------------------------------
// None -- int <-> void*: neither direction has an implicit conversion -> false.
// ---------------------------------------------------------------------------

// `int <-> void*`: the forward call `StandardImplicitConversion(int, void*)` yields `None` (no arm
// fires -- int is a Struct, void* is a PointerType), and the reverse call `StandardImplicitConversion(
// void*, int)` also yields `None` (no arm fires -- void* is a PointerType, int is a Struct; no
// pointer-to-integer implicit conversion exists). So `IsEncompassingOrEncompassedBy(int, void*)`
// is false -- the pair is NOT related by an implicit conversion in either direction.
TEST(CSharpConversionsEncompassmentTest, IsEncompassingOrEncompassedByFalseWhenNoDirectionFires) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr voidPtr = VoidPtr();
    EXPECT_FALSE(IsEncompassingOrEncompassedBy(Compilation(), *intType, *voidPtr));
}

// ---------------------------------------------------------------------------
// None -- bool <-> string: neither direction has an implicit conversion -> false.
// ---------------------------------------------------------------------------

// `bool <-> string`: the forward call `StandardImplicitConversion(bool, string)` yields `None`
// (bool has TypeCode::Boolean outside the numeric range, string is a Class but IsSubtypeOf(bool,
// string) is false), and the reverse call `StandardImplicitConversion(string, bool)` also yields
// `None` (no reference/boxing conversion from a reference type to a value type). So
// `IsEncompassingOrEncompassedBy(bool, string)` is false -- the pair is NOT related by an implicit
// conversion in either direction. This is a second None case with a different shape (a value type
// vs a reference type with no subtype relation) to pin the `||`-false fold.
TEST(CSharpConversionsEncompassmentTest, IsEncompassingOrEncompassedByFalseForUnrelatedValueAndReference) {
    ITypePtr boolType = std::make_shared<KnownType>(KnownTypeCode::Boolean);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_FALSE(IsEncompassingOrEncompassedBy(Compilation(), *boolType, *stringType));
}
