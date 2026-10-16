// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` implicit-reference-conversion region (CSharpConversions.cs
// lines 534-665, C# 9.0 spec section 10.2.8): the public `IsImplicitReferenceConversion` entry,
// the recursive `ImplicitReferenceConversion` worker, the `IsSubtypeOf` inheritance-chain
// traversal, and the `IdentityOrVarianceConversion` per-base-type check. The cluster is
// mutually recursive (`ImplicitReferenceConversion` -> `IsSubtypeOf` -> `IdentityOrVarianceConversion`
// -> `ImplicitReferenceConversion`), so it lands together as `Detail::` free functions taking a
// threaded `const ICompilation&` (the recursion's `compilation.FindType(KnownTypeCode.Array)` arm).
//
// CRUX STUB CONVENTIONS (carried from the D514-D516 tests):
//  * `LookupTypeDefinition::IsReferenceType()` defaults to `std::nullopt` (the `IType` default),
//    but the reference-conversion guard `fromType.IsReferenceType == true && toType.IsReferenceType != false`
//    needs a DEFINITE `true` on both sides. The local `RefDef` stub derives from
//    `LookupTypeDefinition` and overrides `IsReferenceType()` to `true` (a `Class`/`Interface` IS a
//    reference type -- the faithful value), so the guard passes for the reference-type definitions.
//  * `KnownType(String)` has `IsReferenceType() == true` (derived from `Kind`); `KnownType(Int32)`
//    has `IsReferenceType() == false`. A `KnownType` is NOT an `ITypeDefinition`
//    (`GetDefinition() == nullptr`), so it cannot stand where a definition is needed, but it is the
//    faithful stub for a plain reference/value element type (the D515 precedent).
//  * `IsKnownType(type, KnownTypeCode::Object)` reads the TYPE's own `GetDefinition()?.KnownTypeCode`
//    (NOT `compilation.FindType`), so a `RefDef` carrying `KnownTypeCode::Object` makes
//    `IsKnownType(it, Object)` true without any `FindType` registration. The cluster's ONLY
//    `FindType` call is `compilation.FindType(KnownTypeCode::Array)` in the array-to-`System.Array`
//    arm, so only `Array` needs to be registered in the `LookupCompilation`.
//  * `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    variance crux cases must reuse the SAME definition instance for the matching sides (the
//    `IGeneral<string>` / `IGeneral<object>` pair share the one `IGeneral` definition).
//  * `LookupTypeParameter` now accepts a `VarianceModifier` (the D517 extension -- default
//    `Invariant`), so the variance cases configure `Covariant`/`Contravariant`/`Invariant` type
//    parameters directly. `LookupTypeDefinition::SetTypeParameters` (the D517 extension) wires the
//    declared type parameters the variance loop reads via `def->TypeParameters()`.

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

using ILSpy::Decompiler::CSharp::Resolver::Detail::IdentityOrVarianceConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsImplicitReferenceConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsSubtypeOf;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a reference
// type -- the faithful value the reference-conversion guard needs; `LookupTypeDefinition`'s default
// is `std::nullopt`, which fails the guard). Inherits the `LookupTypeDefinition` ctor; the only
// override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
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
// `FindType` registration). Used as the variance/array-covariance target and the `Array` base type.
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class, 0);
    return d;
}

// The `System.Array` definition -- a reference type (`Class`) carrying `KnownTypeCode::Array`,
// with `Object` as its direct base (so `GetAllBaseTypes(Array)` yields `[Object, Array]`).
// Registered in the `LookupCompilation` so the array-to-`System.Array` arm's
// `compilation.FindType(KnownTypeCode.Array)` resolves it.
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

// A 1-arg generic interface definition with the supplied variance on its single type parameter.
std::shared_ptr<RefDef> VariantInterfaceDef(std::string name, VarianceModifier variance) {
    static auto cov = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Covariant);
    static auto con = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Contravariant);
    static auto inv = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Invariant);
    LookupTypeParameter* tp = (variance == VarianceModifier::Covariant) ? cov.get()
                              : (variance == VarianceModifier::Contravariant) ? con.get()
                              : inv.get();
    auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Interface, 1);
    // Rename for distinctness across the three variants (the `MakeRefDef` name is typecode-based).
    d->SetTypeParameters({tp});
    (void)name; // the definition identity is the instance, not the name (StructuralEquals is identity)
    return d;
}

} // namespace

// ===========================================================================
// IsImplicitReferenceConversion (CSharpConversions.cs line 534, spec 10.2.8) -- the public entry.
// ===========================================================================

// The reference-conversion guard: both types must be known reference types. `int` is a value type
// (`IsReferenceType == false`), so any conversion involving it on a non-array path returns false.
TEST(CSharpConversionsReferenceTest, GuardRejectsNonReferenceFromType) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *intEl, *stringEl));
}

TEST(CSharpConversionsReferenceTest, GuardRejectsNonReferenceToType) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *stringEl, *intEl));
}

// Array covariance: `S[] -> T[]` when `S -> T` is an implicit reference conversion. Same dimensions
// + identity elements (`string[] -> string[]`) is the identity arm of the covariance recursion.
TEST(CSharpConversionsReferenceTest, ArrayCovarianceIdentityElements) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr fromStringArr = ArrayOf(stringEl);
    ITypePtr toStringArr = ArrayOf(stringEl);
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *fromStringArr, *toStringArr));
}

// `string[] -> object[]`: same dimensions + `string -> object` is a reference conversion (the
// `IsSubtypeOf` arm reaches `object` because `toType.IsKnownType(Object)` is true).
TEST(CSharpConversionsReferenceTest, ArrayCovarianceConvertibleElements) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromStringArr = ArrayOf(stringEl);
    ITypePtr toObjectArr = ArrayOf(objectEl);
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *fromStringArr, *toObjectArr));
}

// `int[] -> object[]`: `int` is a value type, so the element recursion's guard rejects it. Array
// covariance requires the ELEMENT conversion to be a reference conversion, which `int -> object` is not.
TEST(CSharpConversionsReferenceTest, ArrayCovarianceRejectsValueElement) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromIntArr = ArrayOf(intEl);
    ITypePtr toObjectArr = ArrayOf(objectEl);
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *fromIntArr, *toObjectArr));
}

// `string[] -> object[,]`: different dimension counts -- the covariance arm requires
// `fromArray.Dimensions == toArray.Dimensions`, which `1 != 2` fails.
TEST(CSharpConversionsReferenceTest, ArrayCovarianceRejectsDifferentDimensions) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromStringArr = ArrayOf(stringEl, 1);
    ITypePtr toObjectArr = ArrayOf(objectEl, 2);
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *fromStringArr, *toObjectArr));
}

// Single-dimensional array to `IList<T>`: `int[] -> IList<int>` unpacks `IList<int>` to `int`, then
// `IdentityConversion(int, int)` is true (the identity arm of the array-to-interface path).
TEST(CSharpConversionsReferenceTest, ArrayToGenericInterfaceIdentityArgument) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr fromIntArr = ArrayOf(intEl);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfInt = ParameterizedOver(ilistDef, {intEl});
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *fromIntArr, *ilistOfInt));
}

// `string[] -> IList<object>`: unpacks to `object`, `IdentityConversion(string, object)` is false,
// but `ImplicitReferenceConversion(string, object)` is true (the recursive arm), so the conversion holds.
TEST(CSharpConversionsReferenceTest, ArrayToGenericInterfaceConvertibleArgument) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    ITypePtr fromStringArr = ArrayOf(stringEl);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfObject = ParameterizedOver(ilistDef, {objectEl});
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *fromStringArr, *ilistOfObject));
}

// `int[] -> IList<string>`: `int -> string` is neither identity nor a reference conversion, so the
// array-to-interface path returns false.
TEST(CSharpConversionsReferenceTest, ArrayToGenericInterfaceRejectsValueToReferenceArgument) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr fromIntArr = ArrayOf(intEl);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfString = ParameterizedOver(ilistDef, {stringEl});
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *fromIntArr, *ilistOfString));
}

// Multi-dimensional array to `IList<T>`: the `Dimensions == 1` guard fails (rank 2), so the
// array-to-interface arm does not fire; the fallback `System.Array` recursion does not reach
// `IList<T>` (it is not a base of `Array` in the stub universe).
TEST(CSharpConversionsReferenceTest, MultiDimArrayToGenericInterfaceRejected) {
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr fromIntArr2d = ArrayOf(intEl, 2);
    auto ilistDef = MakeRefDef(KnownTypeCode::IListOfT, TypeKind::Interface, 1);
    ITypePtr ilistOfInt = ParameterizedOver(ilistDef, {intEl});
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *fromIntArr2d, *ilistOfInt));
}

// Any array to `System.Array`: the arm recurses with `compilation.FindType(KnownTypeCode.Array)`,
// and `IsSubtypeOf(Array, Array)` is true (`Array` is its own base type -- the `IdentityOrVarianceConversion`
// identity arm fires with the same definition instance).
TEST(CSharpConversionsReferenceTest, AnyArrayConvertsToSystemArray) {
    (void)ArrayDef(); // register Array in the compilation's FindType
    ITypePtr intEl = Prim(KnownTypeCode::Int32);
    ITypePtr fromIntArr = ArrayOf(intEl);
    ITypePtr systemArray = ArrayDef();
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *fromIntArr, *systemArray));
}

// Inheritance: `Derived -> Base` when `Derived : Base`. `IsSubtypeOf` traverses
// `GetAllBaseTypes(Derived)` = `[Base, Derived]`, and `IdentityOrVarianceConversion(Base, Base)`
// fires the identity arm (same definition instance).
TEST(CSharpConversionsReferenceTest, SubtypeViaDirectBaseInheritance) {
    auto baseDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto derivedDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    derivedDef->AddDirectBaseType(baseDef);
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *derivedDef, *baseDef));
}

// `Base -> Derived` (the REVERSE direction): `Base` is not a subtype of `Derived`, so the
// conversion is false -- the inheritance is one-directional.
TEST(CSharpConversionsReferenceTest, NotSubtypeInReverseDirection) {
    auto baseDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto derivedDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    derivedDef->AddDirectBaseType(baseDef);
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *baseDef, *derivedDef));
}

// ===========================================================================
// IsSubtypeOf (CSharpConversions.cs line 607) -- direct crux cases.
// ===========================================================================

// Conversion to `dynamic` is always possible (the `t.Kind == TypeKind.Dynamic` short-circuit).
TEST(CSharpConversionsReferenceTest, SubtypeOfDynamicAlwaysTrue) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto dynamicType = std::make_shared<SpecialType>(TypeKind::Dynamic, true);
    EXPECT_TRUE(IsSubtypeOf(Compilation(), *stringEl, *dynamicType, 0));
}

// Conversion to `object` is always possible (the `t.IsKnownType(KnownTypeCode.Object)` short-circuit).
TEST(CSharpConversionsReferenceTest, SubtypeOfObjectAlwaysTrue) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    EXPECT_TRUE(IsSubtypeOf(Compilation(), *stringEl, *objectEl, 0));
}

// The depth guard: C# subtyping is undecidable (Kennedy & Pierce); a nesting depth > 10
// short-circuits to false even when the types would otherwise be subtypes.
TEST(CSharpConversionsReferenceTest, SubtypeOfDepthGuardReturnsFalse) {
    auto baseDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto derivedDef = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    derivedDef->AddDirectBaseType(baseDef);
    // `Derived` IS a subtype of `Base` at depth 0, but the depth guard returns false at depth 11
    // (the guard fires BEFORE the base-type traversal).
    EXPECT_FALSE(IsSubtypeOf(Compilation(), *derivedDef, *baseDef, 11));
}

// ===========================================================================
// IdentityOrVarianceConversion (CSharpConversions.cs line 635) -- direct crux cases.
// ===========================================================================

// Identity: same definition, neither parameterized -> true (the `def != null` + `def.Equals(tDef)` +
// `ps == null && pt == null` arm returns true).
TEST(CSharpConversionsReferenceTest, IdentityOrVarianceIdentitySameDefinition) {
    auto def = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_TRUE(IdentityOrVarianceConversion(Compilation(), *def, *def, 0));
}

// Different definitions: `def.Equals(tDef)` is false (identity-equality `StructuralEquals`), so the
// `if (!def.Equals(t.GetDefinition())) return false;` arm fires.
TEST(CSharpConversionsReferenceTest, IdentityOrVarianceDifferentDefinitionsFalse) {
    auto defA = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    auto defB = MakeRefDef(KnownTypeCode::None, TypeKind::Class, 0);
    EXPECT_FALSE(IdentityOrVarianceConversion(Compilation(), *defA, *defB, 0));
}

// Covariant variance: `IGeneral<out T>` with `IGeneral<string> -> IGeneral<object>`. The type
// arguments differ (`string != object`), so the loop checks `xi.Variance == Covariant` and recurses
// `ImplicitReferenceConversion(string, object)` -- which is true via `IsSubtypeOf` reaching `Object`.
TEST(CSharpConversionsReferenceTest, CovariantVarianceConversionHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto igeneralDef = VariantInterfaceDef("IGeneral", VarianceModifier::Covariant);
    ITypePtr igeneralOfString = ParameterizedOver(igeneralDef, {stringEl});
    ITypePtr igeneralOfObject = ParameterizedOver(igeneralDef, {objectEl});
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *igeneralOfString, *igeneralOfObject));
}

// Contravariant variance: `IAction<in T>` with `IAction<object> -> IAction<string>`. The type
// arguments differ; the loop checks `xi.Variance == Contravariant` and recurses
// `ImplicitReferenceConversion(ti, si)` = `ImplicitReferenceConversion(string, object)` -- true.
TEST(CSharpConversionsReferenceTest, ContravariantVarianceConversionHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto iactionDef = VariantInterfaceDef("IAction", VarianceModifier::Contravariant);
    ITypePtr iactionOfObject = ParameterizedOver(iactionDef, {objectEl});
    ITypePtr iactionOfString = ParameterizedOver(iactionDef, {stringEl});
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *iactionOfObject, *iactionOfString));
}

// Invariant variance with different type arguments: the `default` case in the variance switch
// returns false (no variance conversion is possible for an invariant parameter).
TEST(CSharpConversionsReferenceTest, InvariantVarianceRejectsDifferentArguments) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    ITypePtr objectEl = ObjectDef();
    auto iinvariantDef = VariantInterfaceDef("IInvariant", VarianceModifier::Invariant);
    ITypePtr ofString = ParameterizedOver(iinvariantDef, {stringEl});
    ITypePtr ofObject = ParameterizedOver(iinvariantDef, {objectEl});
    EXPECT_FALSE(IsImplicitReferenceConversion(Compilation(), *ofString, *ofObject));
}

// Same generic, same type arguments (identity): the variance loop's `IdentityConversion(si, ti)`
// continues for each parameter, so the loop completes and the conversion is true.
TEST(CSharpConversionsReferenceTest, VarianceIdentitySameArgumentsHolds) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto igeneralDef = VariantInterfaceDef("IGeneral", VarianceModifier::Covariant);
    ITypePtr a = ParameterizedOver(igeneralDef, {stringEl});
    ITypePtr b = ParameterizedOver(igeneralDef, {stringEl});
    EXPECT_TRUE(IsImplicitReferenceConversion(Compilation(), *a, *b));
}

// Only one side parameterized: `def.Equals(tDef)` is true (same generic definition), but
// `ps != null && pt != null` is false and `ps != null || pt != null` is true -> returns false.
TEST(CSharpConversionsReferenceTest, OneSideParameterizedIsFalse) {
    ITypePtr stringEl = Prim(KnownTypeCode::String);
    auto igeneralDef = VariantInterfaceDef("IGeneral", VarianceModifier::Covariant);
    ITypePtr parameterized = ParameterizedOver(igeneralDef, {stringEl});
    EXPECT_FALSE(IdentityOrVarianceConversion(Compilation(), *parameterized, *igeneralDef, 0));
}

// No definition on either side (two type parameters): the `else` arm returns `s.Equals(t)`. Two
// DISTINCT type-parameter instances are NOT equal (`StructuralEquals` is identity), so false; the
// SAME instance is equal, so true.
TEST(CSharpConversionsReferenceTest, TypeParameterEqualityElseBranch) {
    auto t1 = std::make_shared<LookupTypeParameter>("T");
    auto t2 = std::make_shared<LookupTypeParameter>("T");
    EXPECT_TRUE(IdentityOrVarianceConversion(Compilation(), *t1, *t1, 0));
    EXPECT_FALSE(IdentityOrVarianceConversion(Compilation(), *t1, *t2, 0));
}
