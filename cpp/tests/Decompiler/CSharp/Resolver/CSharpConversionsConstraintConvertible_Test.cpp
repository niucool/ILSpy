// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `CSharpConversions.IsConstraintConvertible` (CSharpConversions.cs line 261, C# spec
// section 8.4.5 "satisfying constraints") -- whether `fromType` is convertible to `toType` using
// one of the conversions allowed when satisfying type parameter constraints. The allowed
// conversions are a strict subset of the implicit conversions: identity, implicit reference,
// boxing (for a non-nullable from-type), the nullable-value-type-to-`object` special case, and
// implicit type-parameter conversion. NOT allowed: numeric, nullable-lifted, pointer, constant-
// expression, user-defined, or tuple conversions.
//
// The `Detail::IsConstraintConvertible` free function (D536) delegates entirely to the
// already-ported helpers (`IdentityConversion` D514, `ImplicitReferenceConversion` D517,
// `NullableType.IsNullable` D515, `IsKnownType`, `IsBoxingConversion` D519,
// `ImplicitTypeParameterConversion` D519); the public `CSharpConversions::IsConstraintConvertible`
// method threads the instance's compilation to the free function.
//
// CRUX STUB CONVENTIONS (carried from the D517/D519 boxing tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). Use this for the
//    value-type primitives the identity/boxing arms read (a `Def(Int32)` reports `TypeCode::Int32`
//    via `GetTypeCode`, and `IsReferenceType` is `std::nullopt` -- NOT `false`; the boxing guard
//    needs a definite `false`, so use `KnownType(Int32)` for the boxing-arm from-side).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference-conversion guard). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit fires); a `RefDef` with `KnownTypeCode::None` is a custom class.
//  * `KnownType(Int32)` (NOT an `ITypeDefinition`, `GetDefinition() == nullptr`) derives
//    `IsReferenceType` from `Kind == Struct` (a definite `false`), so it stands in for a value
//    type where the boxing guard's definite-`false` requirement must be met.
//  * `LookupTypeParameter` (`IsReferenceType == std::nullopt`, inherited) stands in for an
//    unconstrained type parameter; it FAILS the boxing guard (needs a definite `false`) but
//    REACHES the `ImplicitTypeParameterConversion` `IsSubtypeOf` arm (the `!HasValue` guard
//    passes), which short-circuits via `IsKnownType(object, Object)`.
//  * `Nullable<T>` is a `ParameterizedType` over the `System.Nullable`1` definition
//    (`KnownTypeCode::NullableOfT`); `IsNullable(Nullable<T>)` is true, so the nullable branch
//    fires (NOT the else/boxing branch).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public IsConstraintConvertible)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"   // Detail::IsConstraintConvertible
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // KnownType, ParameterizedType
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
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
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsConstraintConvertible;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
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

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the
// `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive value-type definition as an `ITypePtr` (a `Def(Int32)` struct).
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
// `IsSubtypeOf(Object, Derived)` does not.
std::shared_ptr<RefDef> DerivedDef() {
    static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
    return d;
}

// A non-Object reference type (an interface or a custom class) carrying `KnownTypeCode::None` --
// `IsKnownType(it, Object)` is false, so the nullable-to-object special case does NOT fire and
// `IsSubtypeOf` does not short-circuit.
std::shared_ptr<RefDef> NonObjectRefDef(TypeKind kind) {
    return MakeRefDef(KnownTypeCode::None, kind);
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
// `IsReferenceType` derives from `Kind` (a definite `false` for `Int32`/`Struct`), so it meets
// the boxing guard's definite-`false` requirement.
ITypePtr prim(KnownTypeCode ktc) {
    return std::make_shared<KnownType>(ktc);
}

// An unconstrained type parameter -- `IsReferenceType == std::nullopt` (inherited), so it FAILS
// the boxing guard (needs a definite `false`) but REACHES the `ImplicitTypeParameterConversion`
// `IsSubtypeOf` arm (the `!HasValue` guard passes).
std::shared_ptr<LookupTypeParameter> MakeTypeParam() {
    return std::make_shared<LookupTypeParameter>("T");
}

} // namespace

// ===========================================================================
// Detail::IsConstraintConvertible (CSharpConversions.cs line 261, spec 8.4.5).
//
//   if (IdentityConversion(fromType, toType)) return true;
//   if (ImplicitReferenceConversion(fromType, toType, 0)) return true;
//   if (NullableType.IsNullable(fromType)) {
//       if (toType.IsKnownType(KnownTypeCode.Object)) return true;
//   } else {
//       if (IsBoxingConversion(fromType, toType)) return true;
//   }
//   if (ImplicitTypeParameterConversion(fromType, toType)) return true;
//   return false;
//
// The allowed conversions are a strict subset: identity, implicit reference, boxing (non-nullable
// from-type), nullable-to-object (nullable from-type), and implicit type-parameter. NOT allowed:
// numeric, nullable-lifted, pointer, constant-expression, user-defined, or tuple conversions.
// ===========================================================================

// `int -> int` (same instance): the identity arm fires (IdentityConversion(int, int) is true).
TEST(CSharpConversionsConstraintConvertibleTest, IdentityConversionFiresForSameType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    EXPECT_TRUE(IsConstraintConvertible(Compilation(), *intEl, *intEl));
}

// `int -> long`: there is NO numeric conversion in the constraint-convertible set -- identity
// fails (int != long), reference fails (int is a value type), the nullable/boxing arms fail (int
// is not nullable, and boxing to long fails since long is not a reference type), and the type-
// parameter arm fails (int is not a type parameter). The result is `false` -- the load-bearing
// crux that numeric conversions are NOT constraint-convertible.
TEST(CSharpConversionsConstraintConvertibleTest, NumericConversionIsNotAllowed) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr longEl = prim(KnownTypeCode::Int64);
    EXPECT_FALSE(IsConstraintConvertible(Compilation(), *intEl, *longEl));
}

// `Derived -> object`: the implicit-reference arm fires (IsSubtypeOf(Object) short-circuits via
// IsKnownType(object, Object) -- the D517 RefDef + IsKnownType precedent).
TEST(CSharpConversionsConstraintConvertibleTest, ImplicitReferenceConversionFiresForDerivedToBase) {
    auto derived = DerivedDef();
    auto object_ = ObjectDef();
    EXPECT_TRUE(IsConstraintConvertible(Compilation(), *derived, *object_));
}

// `object -> Derived`: a reference conversion in the REVERSE direction is NOT an implicit reference
// conversion (Derived is not a base type of Object). The identity arm fails, the reference arm
// fails (Object -> Derived is a narrowing reference conversion, not implicit), the nullable arm
// fails (Object is not nullable), the boxing arm is skipped (the nullable branch is not taken --
// Object is not nullable, but IsBoxingConversion(Object, Derived) fails since Object is a
// reference type, not a value type), and the type-parameter arm fails. The result is `false`.
TEST(CSharpConversionsConstraintConvertibleTest, ReverseReferenceIsNotAllowed) {
    auto object_ = ObjectDef();
    auto derived = DerivedDef();
    EXPECT_FALSE(IsConstraintConvertible(Compilation(), *object_, *derived));
}

// `int -> object` (non-nullable value type): the boxing arm fires (the else branch -- int is not
// nullable, IsBoxingConversion(int, object) is true via the IsSubtypeOf Object short-circuit).
TEST(CSharpConversionsConstraintConvertibleTest, BoxingConversionFiresForValueTypeToObject) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto object_ = ObjectDef();
    EXPECT_TRUE(IsConstraintConvertible(Compilation(), *intEl, *object_));
}

// `Nullable<int> -> object` (nullable value type): the nullable branch fires (IsNullable is true),
// and the to-object special case fires (IsKnownType(object, Object) is true). This is the
// nullable-value-type-to-object conversion the `DefaultResolvedTypeParameter.DirectBaseTypes`
// `object` constraint inserts.
TEST(CSharpConversionsConstraintConvertibleTest, NullableToObjectsSpecialCaseFires) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    auto object_ = ObjectDef();
    EXPECT_TRUE(IsConstraintConvertible(Compilation(), *nullableInt, *object_));
}

// `Nullable<int> -> string` (nullable value type to a non-Object reference type): the nullable
// branch fires (IsNullable is true), but the to-object special case does NOT fire (string is a
// RefDef(None), IsKnownType(string, Object) is false). The boxing arm is NOT checked (the nullable
// branch does NOT fall through to the else/boxing arm -- the C# `if/else` structure). The type-
// parameter arm fails (Nullable<int> is not a type parameter). The result is `false` -- the
// load-bearing crux that the nullable branch checks ONLY the to-object special case, not boxing.
TEST(CSharpConversionsConstraintConvertibleTest, NullableToNonObjectIsNotAllowed) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    auto nonObject = NonObjectRefDef(TypeKind::Class);
    EXPECT_FALSE(IsConstraintConvertible(Compilation(), *nullableInt, *nonObject));
}

// `T -> object` (unconstrained type parameter): the type-parameter arm fires
// (ImplicitTypeParameterConversion(T, object) is true via the IsSubtypeOf Object short-circuit --
// T's IsReferenceType is std::nullopt, so the `!HasValue` guard passes and the `IsSubtypeOf(T,
// object, 0)` short-circuits via IsKnownType(object, Object)).
TEST(CSharpConversionsConstraintConvertibleTest, TypeParameterConversionFiresForUnconstrainedT) {
    auto t = MakeTypeParam();
    auto object_ = ObjectDef();
    EXPECT_TRUE(IsConstraintConvertible(Compilation(), *t, *object_));
}

// `int -> string` (value type to a non-Object reference type): no arm fires -- identity fails,
// reference fails (int is a value type), the nullable/boxing arms fail (int is not nullable, and
// IsBoxingConversion(int, string) fails since string is a RefDef(None), not Object, so
// IsSubtypeOf(int, string) does not short-circuit and int's base types don't include string), and
// the type-parameter arm fails (int is not a type parameter). The result is `false`.
TEST(CSharpConversionsConstraintConvertibleTest, ValueTypeToNonObjectReferenceIsNotAllowed) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto nonObject = NonObjectRefDef(TypeKind::Class);
    EXPECT_FALSE(IsConstraintConvertible(Compilation(), *intEl, *nonObject));
}

// `object -> int` (reference type to a value type): no arm fires -- identity fails, reference
// fails (int is a value type, not a reference type), the nullable branch fails (Object is not
// nullable), the boxing arm fails (Object is a reference type, not a value type -- the boxing guard
// requires the from-side IsReferenceType == false), and the type-parameter arm fails (Object is not
// a type parameter). The result is `false`.
TEST(CSharpConversionsConstraintConvertibleTest, ReferenceTypeToValueTypeIsNotAllowed) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto object_ = ObjectDef();
    EXPECT_FALSE(IsConstraintConvertible(Compilation(), *object_, *intEl));
}

// ===========================================================================
// The public CSharpConversions::IsConstraintConvertible(IType, IType) entry point -- a thin
// wrapper that delegates to Detail::IsConstraintConvertible(*compilation_, ...). Verified for
// both a true case (boxing) and a false case (numeric), proving the delegation threads the
// compilation to the helpers that need FindType/IsSubtypeOf.
// ===========================================================================

TEST(CSharpConversionsConstraintConvertibleTest, PublicMethodDelegatesForTrueCase) {
    CSharpConversions conversions(Compilation());
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto object_ = ObjectDef();
    EXPECT_TRUE(conversions.IsConstraintConvertible(*intEl, *object_));
}

TEST(CSharpConversionsConstraintConvertibleTest, PublicMethodDelegatesForFalseCase) {
    CSharpConversions conversions(Compilation());
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr longEl = prim(KnownTypeCode::Int64);
    EXPECT_FALSE(conversions.IsConstraintConvertible(*intEl, *longEl));
}
