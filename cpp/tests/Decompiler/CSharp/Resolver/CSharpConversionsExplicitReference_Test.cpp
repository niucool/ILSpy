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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for the `CSharpConversions` explicit-reference-conversion region (CSharpConversions.cs
// lines 669-786, C# spec draft-v11 section 10.3.5): the recursive `ExplicitReferenceConversion`
// worker and the pure `IsSealedReferenceType` helper it consults. The worker consumes the
// already-ported implicit-reference cluster (`IsImplicitReferenceConversion` / `IsSubtypeOf` /
// `IdentityOrVarianceConversion`, D517), the numeric-region `IdentityConversion` (D514) and
// `UnpackGenericArrayInterface` (D516); it recurses on itself WITHOUT a nesting-depth parameter
// (the depth guard lives only inside `IsSubtypeOf`), so the signature mirrors
// `IsImplicitReferenceConversion` minus the depth.
//
// CRUX STUB CONVENTIONS (carried from the D517 reference test):
//  * `LookupTypeDefinition::IsReferenceType()` defaults to `std::nullopt` (the `IType` default),
//    but the explicit-reference guard needs a DEFINITE `true` on the to-side (and the from-side,
//    modulo the type-parameter special case). The local `RefDef` stub derives from
//    `LookupTypeDefinition` and overrides `IsReferenceType()` to `true` (a `Class`/`Interface`/
//    `Delegate` IS a reference type -- the faithful value).
//  * `LookupTypeDefinition::IsSealed()` returns `false` by default; the `SealedDef` stub derives
//    from `RefDef` and overrides `IsSealed()` to `true` (a sealed class -- the faithful value the
//    sealed-source / sealed-target arms consult).
//  * `IsSealedReferenceType` adds a `def != nullptr` guard before `def->IsSealed()` for the class
//    arm: a class-kind type whose definition is unresolved (e.g. a `KnownType` placeholder,
//    `GetDefinition() == nullptr`) would NRE in the C#; the guard returns false (not-sealed), the
//    safe faithful fallback, pinned by `ClassWithoutDefinitionIsNotSealedReferenceType`.
//  * `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    variance / delegate-identity crux cases reuse the SAME definition instance for the matching
//    sides (the `Func<string>` / `Func<object>` pair share the one `Func` definition).
//  * `LookupTypeParameter::IsReferenceType()` is `std::nullopt` (inherited), so a type parameter
//    FAILS the from-side guard and enters the type-parameter special case (`IsSubtypeOf(toType,
//    fromType, 0)`); a class that lists the type parameter among its `DirectBaseTypes` makes the
//    special case a subtype, so the downcast `T -> Tclass` holds.

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
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitReferenceConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsSealedReferenceType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface`/`Delegate` IS
// a reference type -- the faithful value the explicit-reference guard needs; the base default is
// `std::nullopt`, which fails the guard). Inherits the `LookupTypeDefinition` ctor; the only
// override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `RefDef` whose `IsSealed()` is `true` (a sealed class -- the faithful value the sealed-source /
// sealed-target arms consult; the base default is `false`). Inherits the `RefDef` ctor.
class SealedDef : public RefDef {
public:
    using RefDef::RefDef;
    bool IsSealed() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `RefDef` with a configurable `KnownTypeCode`, `TypeKind`, and `TypeParameterCount`. The
// `FullTypeName` carries the `TypeParameterCount` (1 for a generic definition, 0 for a non-generic
// stub). Registered `KnownTypeCode`s (Object/Array) make `IsKnownType` resolve on the type itself.
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind, int typeParamCount) {
    std::string name = "T" + std::to_string(static_cast<int>(ktc)) + "_" + std::to_string(typeParamCount);
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, typeParamCount)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the type's own `GetDefinition()?.KnownTypeCode`, no
// `FindType` registration). Used as the array-covariance target and the inheritance base.
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class, 0);
    return d;
}

// The `System.Array` definition -- a reference type (`Class`) carrying `KnownTypeCode::Array`,
// with `Object` as its direct base. Registered in the `LookupCompilation` so the array-to-
// `System.Array` arm's `compilation.FindType(KnownTypeCode.Array)` resolves it (rather than the
// unknown-type fallback).
std::shared_ptr<RefDef> ArrayDef() {
    static auto d = MakeRefDef(KnownTypeCode::Array, TypeKind::Class, 0);
    static bool wired = false;
    if (!wired) {
        d->AddDirectBaseType(ObjectDef());
        Compilation().RegisterKnownType(KnownTypeCode::Array, d.get());
        wired = true;
    }
    return d;
}

// A `KnownType` primitive element (a value type for `Int32`, a reference type for `String`); NOT an
// `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful stub for a plain element.
ITypePtr Prim(KnownTypeCode ktc) {
    return std::make_shared<KnownType>(ktc);
}

// An `ArrayType` over the supplied element with the supplied rank (1 = SZArray).
ITypePtr ArrayOf(ITypePtr element, int rank = 1) {
    return std::make_shared<ArrayType>(std::move(element), rank);
}

// A `ParameterizedType` over the supplied generic definition with the supplied type arguments.
ITypePtr ParameterizedOver(std::shared_ptr<LookupTypeDefinition> genericDef, std::vector<ITypePtr> args) {
    return std::make_shared<ParameterizedType>(std::move(genericDef), std::move(args));
}

// A 1-arg generic definition (interface or delegate) with the supplied variance on its single type
// parameter. The static type-parameter singletons persist for the program lifetime, so the
// non-owning pointers the definition stores remain valid across calls.
std::shared_ptr<RefDef> VariantDef(TypeKind kind, VarianceModifier variance) {
    static auto cov = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Covariant);
    static auto con = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Contravariant);
    static auto inv = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Invariant);
    LookupTypeParameter* tp = (variance == VarianceModifier::Covariant) ? cov.get()
                              : (variance == VarianceModifier::Contravariant) ? con.get()
                              : inv.get();
    auto d = MakeRefDef(KnownTypeCode::None, kind, 1);
    d->SetTypeParameters({tp});
    return d;
}

} // namespace

// ===========================================================================
// IsSealedReferenceType (CSharpConversions.cs line 784) -- direct crux cases.
// ===========================================================================

// A sealed class is a sealed reference type (`kind == Class && GetDefinition().IsSealed`).
TEST(CSharpConversionsExplicitReferenceTest, SealedClassIsSealedReferenceType) {
    auto d = std::make_shared<SealedDef>(
        "Sealed", "", FullTypeName(TopLevelTypeName("", "Sealed", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    EXPECT_TRUE(IsSealedReferenceType(*d));
}

// A non-sealed class is NOT a sealed reference type (the `IsSealed` flag is false).
TEST(CSharpConversionsExplicitReferenceTest, NonSealedClassIsNotSealedReferenceType) {
    EXPECT_FALSE(IsSealedReferenceType(*ObjectDef()));
}

// A delegate is a sealed reference type (delegates are implicitly sealed: `kind == Delegate`).
TEST(CSharpConversionsExplicitReferenceTest, DelegateIsSealedReferenceType) {
    auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Delegate, 0);
    EXPECT_TRUE(IsSealedReferenceType(*d));
}

// A struct is NOT a sealed reference type (neither a class nor a delegate).
TEST(CSharpConversionsExplicitReferenceTest, StructIsNotSealedReferenceType) {
    EXPECT_FALSE(IsSealedReferenceType(*Prim(KnownTypeCode::Int32)));
}

// An interface is NOT a sealed reference type (neither a class nor a delegate).
TEST(CSharpConversionsExplicitReferenceTest, InterfaceIsNotSealedReferenceType) {
    auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Interface, 0);
    EXPECT_FALSE(IsSealedReferenceType(*d));
}

// A class-kind type whose definition is unresolved (a `KnownType` placeholder,
// `GetDefinition() == nullptr`) does NOT crash and is NOT a sealed reference type -- the
// `def != nullptr` guard before `def->IsSealed()` (the C# would NRE; the port returns false).
TEST(CSharpConversionsExplicitReferenceTest, ClassWithoutDefinitionIsNotSealedReferenceType) {
    EXPECT_FALSE(IsSealedReferenceType(*Prim(KnownTypeCode::Object)));
}

// ===========================================================================
// ExplicitReferenceConversion (CSharpConversions.cs line 669, spec 10.3.5) -- the guard and the
// type-parameter special case.
// ===========================================================================

// The to-side guard rejects a non-reference to-type (`int`, `IsReferenceType == false`).
TEST(CSharpConversionsExplicitReferenceTest, GuardRejectsNonReferenceToType) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *stringEl, *intEl));
}

// The from-side guard rejects a non-reference from-type that is not a type parameter (`int`).
TEST(CSharpConversionsExplicitReferenceTest, GuardRejectsNonReferenceFromTypeNotTypeParameter) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *intEl, *stringEl));
}

// The type-parameter special case: converting from `T` to a class `Tclass : T` is an explicit
// reference conversion (a downcast) -- `IsSubtypeOf(Tclass, T, 0)` finds `T` among `Tclass`'s base
// types (the identity arm of `IdentityOrVarianceConversion` on the same `T` instance).
TEST(CSharpConversionsExplicitReferenceTest, TypeParameterSpecialCaseSubtypeHolds) {
    auto tParam = std::make_shared<LookupTypeParameter>("T");
    auto tclass = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    tclass->AddDirectBaseType(tParam); // `Tclass : T`
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *tParam, *tclass));
}

// The type-parameter special case returns false when `toType` is unrelated to `T`
// (`IsSubtypeOf(ObjectDef, T, 0)` traverses no base equal to `T`).
TEST(CSharpConversionsExplicitReferenceTest, TypeParameterSpecialCaseUnrelatedFalse) {
    auto tParam = std::make_shared<LookupTypeParameter>("T");
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *tParam, *ObjectDef()));
}

// ===========================================================================
// ExplicitReferenceConversion -- the array arms.
// ===========================================================================

// Array covariance (explicit): `string[] -> object[]` holds -- same dimensions + a recursive
// explicit reference conversion on the elements (`string -> object` is a reference conversion).
TEST(CSharpConversionsExplicitReferenceTest, ArrayCovarianceExplicitHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromStringArr = ArrayOf(stringEl);
    ITypePtr toObjectArr = ArrayOf(objectEl);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *fromStringArr, *toObjectArr));
}

// `int[] -> object[]` is rejected: the element recursion's from-side guard rejects the value-type
// element `int` (array covariance requires reference elements).
TEST(CSharpConversionsExplicitReferenceTest, ArrayCovarianceRejectsValueElement) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromIntArr = ArrayOf(intEl);
    ITypePtr toObjectArr = ArrayOf(objectEl);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *fromIntArr, *toObjectArr));
}

// `string[] -> object[,]` is rejected: the dimension-count guard fails (`1 != 2`).
TEST(CSharpConversionsExplicitReferenceTest, ArrayCovarianceRejectsDifferentDimensions) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromStringArr = ArrayOf(stringEl, 1);
    ITypePtr toObjectArr = ArrayOf(objectEl, 2);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *fromStringArr, *toObjectArr));
}

// `IList<ElDef> -> ElDef[]` holds: the to-array arm unpacks `IList<ElDef>` to `ElDef`, then a
// recursive explicit reference conversion (or identity) on `ElDef -> ElDef` succeeds.
TEST(CSharpConversionsExplicitReferenceTest, ArrayToGenericArrayInterfaceHolds) {
    auto elDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfEl = ParameterizedOver(ilistDef, {elDef});
    ITypePtr elArr = ArrayOf(elDef);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *ilistOfEl, *elArr));
}

// `IList<int> -> string[]` is rejected: the unpacked argument `int` is a value type, so the
// recursive `ExplicitReferenceConversion(int, string)` and `IdentityConversion(int, string)` both
// fail.
TEST(CSharpConversionsExplicitReferenceTest, ArrayToGenericArrayInterfaceRejectsValueArgument) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfInt = ParameterizedOver(ilistDef, {intEl});
    ITypePtr stringArr = ArrayOf(stringEl);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *ilistOfInt, *stringArr));
}

// `A -> string[]` where `A` is an unrelated reference class is rejected: the to-array arm's
// fallback `IsImplicitReferenceConversion(string[], A)` (swapped) is false.
TEST(CSharpConversionsExplicitReferenceTest, ArrayFallbackToImplicitSwappedFalse) {
    (void)ArrayDef(); // register Array so the System.Array recursion is well-defined
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr stringArr = ArrayOf(stringEl);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *a, *stringArr));
}

// `ElDef[] -> IList<ElDef>` holds: the from-array arm unpacks `IList<ElDef>` to `ElDef`, then a
// recursive explicit reference conversion on `ElDef -> ElDef` succeeds.
TEST(CSharpConversionsExplicitReferenceTest, FromArrayToGenericArrayInterfaceHolds) {
    auto elDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr elArr = ArrayOf(elDef);
    ITypePtr ilistOfEl = ParameterizedOver(ilistDef, {elDef});
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *elArr, *ilistOfEl));
}

// `int[] -> IList<string>` is rejected: the unpacked argument `string` is a reference type but the
// element `int` is a value type, so `ExplicitReferenceConversion(int, string)` fails.
TEST(CSharpConversionsExplicitReferenceTest, FromArrayToGenericArrayInterfaceRejectsValueElement) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr intArr = ArrayOf(intEl);
    ITypePtr ilistOfString = ParameterizedOver(ilistDef, {stringEl});
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *intArr, *ilistOfString));
}

// `string[] -> A` (unrelated reference class) is rejected: the from-array arm's fallback
// `IsImplicitReferenceConversion(string[], A)` is false.
TEST(CSharpConversionsExplicitReferenceTest, FromArrayFallbackToImplicitFalse) {
    (void)ArrayDef();
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr stringArr = ArrayOf(stringEl);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *stringArr, *a));
}

// ===========================================================================
// ExplicitReferenceConversion -- the delegate arms.
// ===========================================================================

// Non-generic delegate identity: `Del -> Del` (the same instance) holds -- the `def.Equals(tDef)`
// identity and the `ps == null && pt == null` (both non-parameterized) arm return true.
TEST(CSharpConversionsExplicitReferenceTest, NonGenericDelegateIdentityHolds) {
    auto del = MakeRefDef(KnownTypeCode::None, TypeKind::Delegate, 0);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *del, *del));
}

// Different non-generic delegate definitions are rejected -- `def.Equals(tDef)` is false (identity
// `StructuralEquals`), so the `def == null || !def.Equals(tDef)` guard returns false.
TEST(CSharpConversionsExplicitReferenceTest, DifferentDelegateDefinitionsFalse) {
    auto delA = MakeRefDef(KnownTypeCode::None, TypeKind::Delegate, 0);
    auto delB = MakeRefDef(KnownTypeCode::None, TypeKind::Delegate, 0);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *delA, *delB));
}

// Covariant delegate: `Func<string> -> Func<object>` holds -- same definition, the covariant type
// parameter recurses `ExplicitReferenceConversion(string, object)` which is true.
TEST(CSharpConversionsExplicitReferenceTest, CovariantDelegateHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto funcDef = VariantDef(TypeKind::Delegate, VarianceModifier::Covariant);
    ITypePtr funcOfString = ParameterizedOver(funcDef, {stringEl});
    ITypePtr funcOfObject = ParameterizedOver(funcDef, {objectEl});
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *funcOfString, *funcOfObject));
}

// Contravariant delegate: `Action<object> -> Action<string>` holds -- same definition, the
// contravariant type parameter checks `si.IsReferenceType == true && ti.IsReferenceType == true`
// (both `object` and `string` are reference types), so the loop continues and the conversion holds.
TEST(CSharpConversionsExplicitReferenceTest, ContravariantDelegateHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto actionDef = VariantDef(TypeKind::Delegate, VarianceModifier::Contravariant);
    ITypePtr actionOfObject = ParameterizedOver(actionDef, {objectEl});
    ITypePtr actionOfString = ParameterizedOver(actionDef, {stringEl});
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *actionOfObject, *actionOfString));
}

// Contravariant delegate with a non-reference argument is rejected: `Action<object> ->
// Action<int>` fails the `si.IsReferenceType == true && ti.IsReferenceType == true` check (`int` is
// a value type).
TEST(CSharpConversionsExplicitReferenceTest, ContravariantDelegateRejectsNonReferenceArgument) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr objectEl = ObjectDef();
    auto actionDef = VariantDef(TypeKind::Delegate, VarianceModifier::Contravariant);
    ITypePtr actionOfObject = ParameterizedOver(actionDef, {objectEl});
    ITypePtr actionOfInt = ParameterizedOver(actionDef, {intEl});
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *actionOfObject, *actionOfInt));
}

// Invariant delegate with different type arguments is rejected -- the variance switch's `default`
// case returns false (no variance conversion is possible for an invariant parameter).
TEST(CSharpConversionsExplicitReferenceTest, InvariantDelegateRejectsDifferentArguments) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto invDef = VariantDef(TypeKind::Delegate, VarianceModifier::Invariant);
    ITypePtr ofString = ParameterizedOver(invDef, {stringEl});
    ITypePtr ofObject = ParameterizedOver(invDef, {objectEl});
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *ofString, *ofObject));
}

// One-side-parameterized delegate is rejected: `def.Equals(tDef)` is true (same generic definition)
// but `ps == null || pt == null` is true while `ps == null && pt == null` is false, so the arm returns
// false.
TEST(CSharpConversionsExplicitReferenceTest, OneSideParameterizedDelegateFalse) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto funcDef = VariantDef(TypeKind::Delegate, VarianceModifier::Covariant);
    ITypePtr parameterized = ParameterizedOver(funcDef, {stringEl});
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *funcDef, *parameterized));
}

// ===========================================================================
// ExplicitReferenceConversion -- the sealed-source / sealed-target arms.
// ===========================================================================

// Sealed source falls back to the implicit reference conversion: `Sealed -> object` holds because
// `IsImplicitReferenceConversion(Sealed, object)` is true (`object` is reachable via `IsKnownType`).
TEST(CSharpConversionsExplicitReferenceTest, SealedSourceFallsBackToImplicitHolds) {
    auto sealedDef = std::make_shared<SealedDef>(
        "Sealed", "", FullTypeName(TopLevelTypeName("", "Sealed", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *sealedDef, *ObjectDef()));
}

// Sealed source to an unrelated type is rejected: `IsImplicitReferenceConversion(Sealed, A)` is
// false (`Sealed` is not a subtype of the unrelated `A`).
TEST(CSharpConversionsExplicitReferenceTest, SealedSourceUnrelatedFalse) {
    auto sealedDef = std::make_shared<SealedDef>(
        "Sealed", "", FullTypeName(TopLevelTypeName("", "Sealed", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *sealedDef, *a));
}

// Sealed target falls back to the implicit reference conversion in the OPPOSITE direction:
// `object -> Sealed` holds because `IsImplicitReferenceConversion(Sealed, object)` is true (a
// downcast to a sealed type requires the sealed type to be a subtype of the source).
TEST(CSharpConversionsExplicitReferenceTest, SealedTargetFallsBackToImplicitSwappedHolds) {
    auto sealedDef = std::make_shared<SealedDef>(
        "Sealed", "", FullTypeName(TopLevelTypeName("", "Sealed", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *ObjectDef(), *sealedDef));
}

// Sealed target from an unrelated type is rejected: `IsImplicitReferenceConversion(Sealed, A)` is
// false.
TEST(CSharpConversionsExplicitReferenceTest, SealedTargetUnrelatedFalse) {
    auto sealedDef = std::make_shared<SealedDef>(
        "Sealed", "", FullTypeName(TopLevelTypeName("", "Sealed", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *a, *sealedDef));
}

// ===========================================================================
// ExplicitReferenceConversion -- the unsealed-unsealed arm.
// ===========================================================================

// An interface on the from-side always yields an explicit reference conversion (a downcast to any
// interface is always possible).
TEST(CSharpConversionsExplicitReferenceTest, InterfaceOnFromSideAlwaysTrue) {
    auto iface = MakeRefDef(KnownTypeCode::None, TypeKind::Interface, 0);
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *iface, *a));
}

// An interface on the to-side always yields an explicit reference conversion (an upcast to any
// interface is always possible).
TEST(CSharpConversionsExplicitReferenceTest, InterfaceOnToSideAlwaysTrue) {
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto iface = MakeRefDef(KnownTypeCode::None, TypeKind::Interface, 0);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *a, *iface));
}

// Two unrelated unsealed non-interface classes are NOT explicitly convertible: neither
// `IsImplicitReferenceConversion(B, A)` nor `IsImplicitReferenceConversion(A, B)` holds.
TEST(CSharpConversionsExplicitReferenceTest, UnsealedUnrelatedFalse) {
    auto a = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto b = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_FALSE(ExplicitReferenceConversion(Compilation(), *a, *b));
}

// `Derived -> Base` (unsealed, non-interface) holds: `IsImplicitReferenceConversion(Derived, Base)`
// is true (the inheritance arm), so the unsealed arm's `||` is satisfied.
TEST(CSharpConversionsExplicitReferenceTest, UnsealedDerivedToBaseHolds) {
    auto baseDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto derivedDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    derivedDef->AddDirectBaseType(baseDef);
    EXPECT_TRUE(ExplicitReferenceConversion(Compilation(), *derivedDef, *baseDef));
}
