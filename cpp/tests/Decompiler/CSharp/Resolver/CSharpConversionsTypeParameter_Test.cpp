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

// Tests for the `CSharpConversions` explicit-type-parameter conversion (CSharpConversions.cs
// line 880, C# spec draft-v11 section 10.3.6): `Detail::ExplicitTypeParameterConversion`. This
// is the explicit counterpart to the implicit `ImplicitTypeParameterConversion` (D519 boxing
// region). It returns a `Conversion` (the second conversion-helper region to do so, after the
// nullable region), so the tests assert the returned `shared_ptr<Conversion>`'s kind flags
// (`IsBoxingConversion` / `IsUnboxingConversion` / `IsImplicit` / `IsExplicit` / `IsValid`)
// and pointer-identity against the `Conversions` factory singletons.
//
// CRUX STUB CONVENTIONS (carried from the D517/D519 reference/boxing tests):
//  * `LookupTypeParameter("T")` has `Kind == TypeParameter` and `IsReferenceType == nullopt`
//    (inherited) -- the faithful stub for an unconstrained type parameter. The `toType is
//    TypeParameter` arm reads `Kind` directly, and the `IsSubtypeOf(toType, fromType, 0)` call
//    traverses `GetAllBaseTypes(T)` which yields only `T` itself (the stub has no
//    `DirectBaseTypes`), so `IsSubtypeOf(T, SomeClass, 0)` is false unless `fromType` carries
//    `KnownTypeCode::Object` (the `IsKnownType(t, Object)` short-circuit in `IsSubtypeOf`).
//  * `RefDef` (IsReferenceType -> true) stands in for a reference type (an interface or a class).
//    A `RefDef` with `KnownTypeCode::Object` makes `IsSubtypeOf(T, object, 0)` short-circuit to
//    true (the `IsKnownType(t, Object)` check reads the type's own `GetDefinition()?.KnownTypeCode`),
//    so `object -> T` is an unboxing conversion without modelling `T`'s effective base-class chain.
//  * `KnownType(Int32)` (IsReferenceType -> false, derived from `Kind == Struct`) stands in for a
//    value type -- a non-interface, non-type-parameter `fromType` for the `None` sentinel cases.
//  * `LookupTypeDefinition::StructuralEquals` is identity (`this == &other`), so the `T -> T`
//    same-instance `IsSubtypeOf(T, T, 0)` case fires the identity arm (`T.Equals(T)` is true) --
//    a genuine crux case pinning the `IsSubtypeOf` arm with a type parameter on both sides.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions (singleton comparison)
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

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitTypeParameterConversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface`/`Delegate`
// IS a reference type). Inherits the `LookupTypeDefinition` ctor. The base `IsReferenceType`
// default is `std::nullopt`, but the explicit-type-parameter conversion does NOT guard on
// `IsReferenceType` (it reads only `Kind`), so a plain `LookupTypeDefinition` would also work for
// the interface/class stubs; the `RefDef` override is kept for consistency with the D517/D519
// tests and to faithfully model a reference-typed interface/class.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object`
// makes `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit),
// so `IsSubtypeOf(T, object, 0)` is true without modelling `T`'s base-type chain.
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

// An interface definition (a reference type, `Kind == Interface`, `KnownTypeCode::None`). Stands
// in for the `fromType.Kind == TypeKind.Interface` arm (interface -> type parameter is an
// unboxing conversion) and the `toType.Kind == TypeKind.Interface` arm (type parameter ->
// interface is a boxing conversion).
std::shared_ptr<RefDef> InterfaceDef() {
    return MakeRefDef(KnownTypeCode::None, TypeKind::Interface);
}

// A plain non-Object class definition (a reference type, `Kind == Class`, `KnownTypeCode::None`).
// `IsKnownType(it, Object)` is false, so `IsSubtypeOf(T, class, 0)` does NOT short-circuit and
// the base-type traversal of `T` yields only `T` itself (the stub has no `DirectBaseTypes`),
// `T.Equals(class)` is false (Kind mismatch) -- so `class -> T` is NOT an unboxing conversion.
std::shared_ptr<RefDef> NonObjectClassDef() {
    return MakeRefDef(KnownTypeCode::None, TypeKind::Class);
}

// A `KnownType` primitive (a value type for `Int32`); NOT an `ITypeDefinition`
// (`GetDefinition() == nullptr`), but the faithful stub for a plain primitive value type.
ITypePtr prim(KnownTypeCode ktc) {
    return std::make_shared<KnownType>(ktc);
}

} // namespace

// ===========================================================================
// ExplicitTypeParameterConversion (CSharpConversions.cs line 880, spec 10.3.6).
//
// toType is a TypeParameter arm:
//   fromType is Interface  -> UnboxingConversion
//   IsSubtypeOf(toType, fromType, 0) is true  -> UnboxingConversion
//   else  -> None
// toType is NOT a TypeParameter arm:
//   fromType is TypeParameter AND toType is Interface  -> BoxingConversion
//   else  -> None
// ===========================================================================

// ---------------------------------------------------------------------------
// toType is a TypeParameter arm -- the UnboxingConversion crux cases.
// ---------------------------------------------------------------------------

// `IInterface -> T`: `toType` is a type parameter, `fromType.Kind == Interface` -- the interface
// arm fires, returning the `UnboxingConversion` singleton.
TEST(CSharpConversionsTypeParameterTest, InterfaceToTypeParameterIsUnboxingConversion) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto iface = InterfaceDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *iface, *tp);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::UnboxingConversion().get());
    EXPECT_TRUE(result->IsUnboxingConversion());
    EXPECT_TRUE(result->IsExplicit());
    EXPECT_FALSE(result->IsImplicit());
    EXPECT_FALSE(result->IsBoxingConversion());
    EXPECT_TRUE(result->IsValid());
}

// `object -> T`: `toType` is a type parameter, `fromType` is NOT an interface but `IsSubtypeOf(T,
// object, 0)` short-circuits to true (`IsKnownType(object, Object)` reads `fromType`'s own
// `KnownTypeCode::Object`) -- the `IsSubtypeOf` arm fires, returning the `UnboxingConversion`
// singleton.
TEST(CSharpConversionsTypeParameterTest, ObjectToTypeParameterIsUnboxingConversionViaSubtype) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto result = ExplicitTypeParameterConversion(Compilation(), *ObjectDef(), *tp);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::UnboxingConversion().get());
    EXPECT_TRUE(result->IsUnboxingConversion());
    EXPECT_TRUE(result->IsExplicit());
    EXPECT_FALSE(result->IsBoxingConversion());
}

// `T -> T` (same instance): `toType` is a type parameter, `fromType` is NOT an interface but
// `IsSubtypeOf(T, T, 0)` is true (the base-type traversal of `T` yields `T` itself, and
// `T.Equals(T)` is identity-true) -- the `IsSubtypeOf` arm fires with a type parameter on both
// sides, returning the `UnboxingConversion` singleton. Pins the `IsSubtypeOf` arm with a type
// parameter `fromType` (not just the Object short-circuit).
TEST(CSharpConversionsTypeParameterTest, TypeParameterToItselfIsUnboxingConversionViaSubtype) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto result = ExplicitTypeParameterConversion(Compilation(), *tp, *tp);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::UnboxingConversion().get());
    EXPECT_TRUE(result->IsUnboxingConversion());
    EXPECT_FALSE(result->IsBoxingConversion());
}

// ---------------------------------------------------------------------------
// toType is a TypeParameter arm -- the None sentinel cases.
// ---------------------------------------------------------------------------

// `SomeNonObjectClass -> T`: `toType` is a type parameter, `fromType.Kind` is NOT `Interface`
// (it is `Class`) and `IsSubtypeOf(T, SomeClass, 0)` is false (the base-type traversal of `T`
// yields only `T` itself; `T.Equals(SomeClass)` is false -- Kind mismatch) -- neither arm fires,
// returning `None`.
TEST(CSharpConversionsTypeParameterTest, NonInterfaceNonObjectClassToTypeParameterIsNone) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto cls = NonObjectClassDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *cls, *tp);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsUnboxingConversion());
    EXPECT_FALSE(result->IsBoxingConversion());
    EXPECT_FALSE(result->IsValid());
}

// `int -> T`: `toType` is a type parameter, `fromType.Kind` is NOT `Interface` (it is `Struct`
// via `KnownType(Int32)`) and `IsSubtypeOf(T, int, 0)` is false (the base-type traversal of `T`
// yields only `T`; `T.Equals(int)` is false -- Kind mismatch) -- neither arm fires, returning
// `None`.
TEST(CSharpConversionsTypeParameterTest, ValueTypeToTypeParameterIsNone) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto result = ExplicitTypeParameterConversion(Compilation(), *intEl, *tp);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsUnboxingConversion());
    EXPECT_FALSE(result->IsBoxingConversion());
}

// ---------------------------------------------------------------------------
// toType is NOT a TypeParameter arm -- the BoxingConversion crux case.
// ---------------------------------------------------------------------------

// `T -> IInterface`: `toType` is NOT a type parameter, `fromType.Kind == TypeParameter` AND
// `toType.Kind == Interface` -- the boxing arm fires, returning the `BoxingConversion` singleton.
TEST(CSharpConversionsTypeParameterTest, TypeParameterToInterfaceIsBoxingConversion) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto iface = InterfaceDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *tp, *iface);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::BoxingConversion().get());
    EXPECT_TRUE(result->IsBoxingConversion());
    EXPECT_TRUE(result->IsImplicit());
    EXPECT_FALSE(result->IsExplicit());
    EXPECT_FALSE(result->IsUnboxingConversion());
    EXPECT_TRUE(result->IsValid());
}

// ---------------------------------------------------------------------------
// toType is NOT a TypeParameter arm -- the None sentinel cases.
// ---------------------------------------------------------------------------

// `T -> SomeClass`: `toType` is NOT a type parameter, `fromType.Kind == TypeParameter` but
// `toType.Kind` is NOT `Interface` (it is `Class`) -- the boxing arm does not fire, returning
// `None`.
TEST(CSharpConversionsTypeParameterTest, TypeParameterToNonInterfaceClassIsNone) {
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto cls = NonObjectClassDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *tp, *cls);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsBoxingConversion());
    EXPECT_FALSE(result->IsUnboxingConversion());
}

// `SomeClass -> IInterface`: `toType` is NOT a type parameter, `fromType.Kind` is NOT
// `TypeParameter` (it is `Class`) -- the boxing arm's `fromType.Kind == TypeParameter` guard
// fails, returning `None`.
TEST(CSharpConversionsTypeParameterTest, NonTypeParameterToInterfaceIsNone) {
    auto cls = NonObjectClassDef();
    auto iface = InterfaceDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *cls, *iface);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsBoxingConversion());
    EXPECT_FALSE(result->IsUnboxingConversion());
}

// `int -> IInterface`: `toType` is NOT a type parameter, `fromType.Kind` is NOT `TypeParameter`
// (it is `Struct` via `KnownType(Int32)`) -- the boxing arm's `fromType.Kind == TypeParameter`
// guard fails, returning `None`.
TEST(CSharpConversionsTypeParameterTest, ValueTypeToInterfaceIsNone) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    auto iface = InterfaceDef();
    auto result = ExplicitTypeParameterConversion(Compilation(), *intEl, *iface);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsBoxingConversion());
    EXPECT_FALSE(result->IsUnboxingConversion());
}
