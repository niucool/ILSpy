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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` boxing-conversion region (CSharpConversions.cs lines
// 790-821, C# 9.0 spec section 10.2.9 / draft-v11 section 10.3.7): `IsBoxingConversion`,
// `UnboxingConversion`, and the public `IsBoxingConversionOrInvolvingTypeParameter` entry
// point; plus the `ImplicitTypeParameterConversion` helper (line 870, section 10.2.12) the
// entry point consumes. The boxing helpers consume the already-ported `NullableType.GetUnderlyingType`
// (D515), `IsSubtypeOf` (D517), and the new `IType.IsByRefLike` virtual (this iteration's
// prerequisite -- the `!fromType.IsByRefLike` guard that excludes ref structs, which cannot be
// boxed).
//
// CRUX STUB CONVENTIONS (carried from the D517/D518 reference tests):
//  * `LookupTypeDefinition::IsReferenceType()` defaults to `std::nullopt` (the `IType` default),
//    but the boxing guard needs a DEFINITE `false` on the from-side (value type) and a DEFINITE
//    `true` on the to-side (reference type). The local `RefDef` stub (IsReferenceType -> true)
//    stands in for a reference type (Object / an interface / a class); `KnownType(Int32)`
//    (IsReferenceType -> false, derived from Kind == Struct) stands in for a value type.
//  * `IsSubtypeOf(s, t, 0)` short-circuits to true when `t` carries `KnownTypeCode::Object`
//    (`IsKnownType(t, Object)` -- the type's own `GetDefinition()?.KnownTypeCode`, no `FindType`
//    registration), so a `RefDef` with `KnownTypeCode::Object` makes `int -> object` a valid
//    boxing/unboxing subtype without modelling `int`'s base-type chain.
//  * `LookupTypeDefinition::IsByRefLike()` inherits the `IType` default `false` (this iteration's
//    new virtual); the local `ByRefLikeDef` stub (IsReferenceType -> false, IsByRefLike -> true)
//    stands in for a `ref struct` to pin the by-ref-like exclusion -- a by-ref-like value type
//    cannot be boxed (the `!IsByRefLike` guard fails).
//  * `LookupTypeParameter::IsReferenceType()` is `std::nullopt` (inherited), so an unconstrained
//    type parameter FAILS the `IsBoxingConversion` from-side guard (needs a definite `false`)
//    but REACHES the `ImplicitTypeParameterConversion` `IsSubtypeOf` arm (the `!HasValue` guard
//    passes); the local `ClassConstrainedTypeParameter` stub (IsReferenceType -> true) pins the
//    `HasValue` guard (a `class`-constrained type parameter is already handled by
//    `ImplicitReferenceConversion`).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitTypeParameterConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsBoxingConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsBoxingConversionOrInvolvingTypeParameter;
using ILSpy::Decompiler::CSharp::Resolver::Detail::UnboxingConversion;
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
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface`/`Delegate`
// IS a reference type -- the faithful value the boxing to-side guard needs; the base default is
// `std::nullopt`, which fails the guard). Inherits the `LookupTypeDefinition` ctor.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupTypeDefinition` (a struct) whose `IsReferenceType()` is `false` (a value type) AND
// `IsByRefLike()` is `true` (a `ref struct` -- e.g. `Span<T>`). Stands in for a by-ref-like value
// type to pin the `!IsByRefLike` exclusion in `IsBoxingConversion` (a ref struct cannot be boxed).
class ByRefLikeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
    bool IsByRefLike() const override { return true; }
};

// A `LookupTypeParameter` whose `IsReferenceType()` is `true` (a `class`-constrained type
// parameter). Stands in for a type parameter with a definite reference-ness to pin the
// `IsReferenceType.HasValue` guard in `ImplicitTypeParameterConversion` (a type parameter whose
// reference-ness is known is already handled by `ImplicitReferenceConversion`/`IsBoxingConversion`).
class ClassConstrainedTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object`
// makes `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the `IsSubtypeOf` short-circuit fires for `T -> object`).
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
    return d;
}

// A plain reference-type definition (an interface or a non-Object class) carrying
// `KnownTypeCode::None`, so `IsKnownType(it, Object)` is false -- `IsSubtypeOf` does NOT
// short-circuit and the subtype traversal runs.
std::shared_ptr<RefDef> NonObjectRefDef(TypeKind kind) {
    return MakeRefDef(KnownTypeCode::None, kind);
}

// A by-ref-like value-type definition (a `ref struct`). `IsReferenceType == false` (a value type)
// AND `IsByRefLike == true` (a ref struct) -- pins the `!IsByRefLike` exclusion.
std::shared_ptr<ByRefLikeDef> MakeByRefLikeDef() {
    return std::make_shared<ByRefLikeDef>(
        "RefStruct", "",
        FullTypeName(TopLevelTypeName("", "RefStruct", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
    return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
        FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type.
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A `KnownType` primitive (a value type for `Int32`, a reference type for `String`); NOT an
// `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful stub for a plain primitive.
ITypePtr prim(KnownTypeCode ktc) {
    return std::make_shared<KnownType>(ktc);
}

// A `ByReferenceType` over the supplied element (a `ref` parameter/local -- `IsByRefLike == true`,
// `IsReferenceType == nullopt`).
ITypePtr ByRefOf(ITypePtr element) {
    return std::make_shared<ILSpy::Decompiler::TypeSystem::ByReferenceType>(std::move(element));
}

} // namespace

// ===========================================================================
// IsBoxingConversion (CSharpConversions.cs line 791, spec 10.2.9) -- direct crux cases.
// ===========================================================================

// `int -> object`: a value type (IsReferenceType == false) to a reference type
// (IsReferenceType == true), the value type is a subtype of `object` (IsKnownType(Object) -> true).
TEST(CSharpConversionsBoxingTest, IsBoxingConversionValueTypeToObjectType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_TRUE(IsBoxingConversion(Compilation(), *intEl, *ObjectDef()));
}

// `Nullable<int> -> object`: the nullable wrapper is stripped from the from-side first
// (GetUnderlyingType(Nullable<int>) == int), then `int -> object` is a boxing conversion.
TEST(CSharpConversionsBoxingTest, IsBoxingConversionNullableValueTypeToObjectType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    EXPECT_TRUE(IsBoxingConversion(Compilation(), *nullableInt, *ObjectDef()));
}

// `object -> int`: the from-side is a reference type (IsReferenceType == true, not false) -- the
// boxing guard rejects a reference-typed source (boxing is value -> reference, not the reverse).
TEST(CSharpConversionsBoxingTest, IsBoxingConversionRejectsReferenceSourceType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(IsBoxingConversion(Compilation(), *ObjectDef(), *intEl));
}

// `int -> int`: the to-side is a value type (IsReferenceType == false, not true) -- the boxing
// guard rejects a value-typed target (boxing requires a reference-typed destination).
TEST(CSharpConversionsBoxingTest, IsBoxingConversionRejectsValueTargetType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(IsBoxingConversion(Compilation(), *intEl, *intEl));
}

// A by-ref-like value type (a `ref struct`: IsReferenceType == false, IsByRefLike == true) does
// NOT box to `object` -- the `!IsByRefLike` guard fails (ref structs cannot be boxed). Pins the
// IsByRefLike exclusion.
TEST(CSharpConversionsBoxingTest, IsBoxingConversionRejectsByRefLikeValueType) {
    auto byRefLike = MakeByRefLikeDef();
    EXPECT_FALSE(IsBoxingConversion(Compilation(), *byRefLike, *ObjectDef()));
}

// A `ref` parameter/local (ByReferenceType: IsReferenceType == nullopt, IsByRefLike == true) does
// NOT box -- the indeterminate IsReferenceType fails the from-side guard (a `bool? == false` check
// is true ONLY when IsReferenceType holds `false`, not `std::nullopt`).
TEST(CSharpConversionsBoxingTest, IsBoxingConversionRejectsIndeterminateFromType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr byRef = ByRefOf(intEl);
    EXPECT_FALSE(IsBoxingConversion(Compilation(), *byRef, *ObjectDef()));
}

// ===========================================================================
// UnboxingConversion (CSharpConversions.cs line 812, spec 10.3.7) -- direct crux cases.
// ===========================================================================

// `object -> int`: a reference type (IsReferenceType == true) to a value type
// (IsReferenceType == false), the value type is a subtype of `object` (IsSubtypeOf(int, object) --
// note the SWAPPED order, IsSubtypeOf(toType, fromType)).
TEST(CSharpConversionsBoxingTest, UnboxingConversionReferenceTypeToValueType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_TRUE(UnboxingConversion(Compilation(), *ObjectDef(), *intEl));
}

// `object -> Nullable<int>`: the nullable wrapper is stripped from the TO-side first
// (GetUnderlyingType(Nullable<int>) == int), then `object -> int` is an unboxing conversion.
TEST(CSharpConversionsBoxingTest, UnboxingConversionReferenceTypeToNullableValueType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    EXPECT_TRUE(UnboxingConversion(Compilation(), *ObjectDef(), *nullableInt));
}

// `int -> int`: the from-side is a value type (IsReferenceType == false, not true) -- the
// unboxing guard rejects a value-typed source (unboxing is reference -> value).
TEST(CSharpConversionsBoxingTest, UnboxingConversionRejectsValueSourceType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(UnboxingConversion(Compilation(), *intEl, *intEl));
}

// `object -> string`: the to-side is a reference type (IsReferenceType == true, not false) -- the
// unboxing guard rejects a reference-typed target (unboxing requires a value-typed destination).
TEST(CSharpConversionsBoxingTest, UnboxingConversionRejectsReferenceTargetType) {
    auto stringDef = NonObjectRefDef(TypeKind::Class);
    EXPECT_FALSE(UnboxingConversion(Compilation(), *ObjectDef(), *stringDef));
}

// ===========================================================================
// ImplicitTypeParameterConversion (CSharpConversions.cs line 870, spec 10.2.12) -- direct crux cases.
// ===========================================================================

// `T -> object` where T is an unconstrained type parameter (IsReferenceType == nullopt): T is a
// type parameter (Kind == TypeParameter) and its reference-ness is indeterminate (HasValue == false),
// so the IsSubtypeOf arm runs -- T is a subtype of `object` (IsKnownType(Object) -> true).
TEST(CSharpConversionsBoxingTest, ImplicitTypeParameterConversionTypeParameterToObjectType) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    EXPECT_TRUE(ImplicitTypeParameterConversion(Compilation(), *tp, *ObjectDef()));
}

// `int -> object`: `int` is not a type parameter (Kind != TypeParameter) -- the first guard rejects.
TEST(CSharpConversionsBoxingTest, ImplicitTypeParameterConversionRejectsNonTypeParameter) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(ImplicitTypeParameterConversion(Compilation(), *intEl, *ObjectDef()));
}

// `T -> object` where T is a `class`-constrained type parameter (IsReferenceType == true): the
// `HasValue` guard rejects it -- a type parameter with a known reference-ness is already handled
// by `ImplicitReferenceConversion`/`IsBoxingConversion`.
TEST(CSharpConversionsBoxingTest, ImplicitTypeParameterConversionRejectsClassConstrainedTypeParameter) {
    auto tp = std::make_shared<ClassConstrainedTypeParameter>("T");
    EXPECT_FALSE(ImplicitTypeParameterConversion(Compilation(), *tp, *ObjectDef()));
}

// `T -> SomeInterface` where T is an unconstrained type parameter and the target is NOT `object`:
// IsSubtypeOf(T, SomeInterface, 0) does not short-circuit (IsKnownType(interface, Object) == false),
// the base-type traversal of T yields only T itself, and T.Equals(interface) is false (Kind mismatch)
// -- so the type parameter is NOT implicitly convertible to a non-object type it does not implement.
TEST(CSharpConversionsBoxingTest, ImplicitTypeParameterConversionTypeParameterToNonObjectType) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto iface = NonObjectRefDef(TypeKind::Interface);
    EXPECT_FALSE(ImplicitTypeParameterConversion(Compilation(), *tp, *iface));
}

// ===========================================================================
// IsBoxingConversionOrInvolvingTypeParameter (CSharpConversions.cs line 806) -- the public entry.
// ===========================================================================

// `int -> object`: a concrete boxing conversion (IsBoxingConversion arm).
TEST(CSharpConversionsBoxingTest, BoxingOrTypeParameterValueTypeToObjectType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_TRUE(IsBoxingConversionOrInvolvingTypeParameter(Compilation(), *intEl, *ObjectDef()));
}

// `T -> object` where T is an unconstrained type parameter: NOT a boxing conversion (T's
// IsReferenceType is indeterminate, failing the IsBoxingConversion from-side guard), but IS an
// implicit type-parameter conversion (the type parameter might be a value type when instantiated,
// in which case it would box) -- the ImplicitTypeParameterConversion arm fires.
TEST(CSharpConversionsBoxingTest, BoxingOrTypeParameterTypeParameterToObjectType) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    EXPECT_TRUE(IsBoxingConversionOrInvolvingTypeParameter(Compilation(), *tp, *ObjectDef()));
}

// `object -> int`: neither a boxing conversion (the from-side is a reference type) nor an
// implicit type-parameter conversion (`object` is not a type parameter).
TEST(CSharpConversionsBoxingTest, BoxingOrTypeParameterRejectsReferenceSourceToValueType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(IsBoxingConversionOrInvolvingTypeParameter(Compilation(), *ObjectDef(), *intEl));
}

// `int -> int`: neither a boxing conversion (the to-side is a value type) nor an implicit
// type-parameter conversion (`int` is not a type parameter).
TEST(CSharpConversionsBoxingTest, BoxingOrTypeParameterRejectsValueTypeToValueType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_FALSE(IsBoxingConversionOrInvolvingTypeParameter(Compilation(), *intEl, *intEl));
}
