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

// Tests for the `CSharpConversions.ExplicitConversionImpl` dispatch entry point
// (CSharpConversions.cs line 308, C# spec draft-v11 section 10.4.3): the standard explicit
// conversion dispatch that wires all the already-ported explicit conversion helpers together in
// spec order: numeric (AnyNumericConversion -> ExplicitNumericConversion), enumeration
// (ExplicitEnumerationConversion -> EnumerationConversion(false, false)), nullable
// (ExplicitNullableConversion, returns a Conversion, checked via pointer-identity against
// `Conversions::None()`), reference (ExplicitReferenceConversion -> ExplicitReferenceConversion),
// unboxing (UnboxingConversion -> UnboxingConversion), type-parameter
// (ExplicitTypeParameterConversion, returns a Conversion, checked against None), pointer
// (ExplicitPointerConversion -> ExplicitPointerConversion). The tuple arm is deferred (needs
// TupleResolveResult machinery) and yields None until ported.
//
// CRUX STUB CONVENTIONS (carried from the D514-D520 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric helpers).
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing/unboxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit), so `int -> object` / `T -> object` are valid unboxing/subtype
//    conversions without modelling a base-type chain.
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition (a
//    1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the element. The lifted-identity test
//    (`int -> Nullable<int>`) reuses the SAME int instance for the from-side and the Nullable's
//    type argument.
//  * `LookupTypeParameter` (unconstrained, `IsReferenceType == std::nullopt`) is the faithful stub
//    for the type-parameter arm: it fails the reference/unboxing guards (needs a definite
//    `true`/`false`) but reaches the `ExplicitTypeParameterConversion` `IsSubtypeOf` arm.
//  * `PointerType` over `KnownType(Void)` has `ReflectionName() == "System.Void*"`; `PointerType`
//    over `KnownType(Int32)` has `"System.Int32*"` (an any-pointer source).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::ExplicitConversionImpl
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitConversionImpl;
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

// An unconstrained type parameter (`IsReferenceType == std::nullopt`, the inherited default).
// Fails the reference/unboxing guards (needs a definite `true`/`false`) but reaches the
// `ExplicitTypeParameterConversion` `IsSubtypeOf` arm (the toType-is-TypeParameter path).
ITypePtr TypeParam() {
    return std::make_shared<LookupTypeParameter>("T");
}

// An enum definition (`TypeKind::Enum`, `KnownTypeCode::None`). `GetTypeCode` returns `Empty`
// (None maps to Empty), and `IsNumericType` is false (Kind == Enum is not in the numeric range),
// so `AnyNumericConversion` does not fire for an enum operand.
ITypePtr EnumDef() {
    return MakeDef(KnownTypeCode::None, TypeKind::Enum);
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
// ExplicitConversionImpl (CSharpConversions.cs line 308, spec 10.4.3).
//
//   if (AnyNumericConversion(from, to)) return ExplicitNumericConversion;
//   if (ExplicitEnumerationConversion(from, to)) return EnumerationConversion(false, false);
//   c = ExplicitNullableConversion(from, to); if (c != None) return c;
//   if (ExplicitReferenceConversion(from, to)) return ExplicitReferenceConversion;
//   if (UnboxingConversion(from, to)) return UnboxingConversion;
//   c = ExplicitTypeParameterConversion(from, to); if (c != None) return c;
//   if (ExplicitPointerConversion(from, to)) return ExplicitPointerConversion;
//   // tuple deferred -> None
//   return None;
// ===========================================================================

// ---------------------------------------------------------------------------
// Numeric arm -- long -> int (narrowing) -> ExplicitNumericConversion singleton.
// ---------------------------------------------------------------------------

// `long -> int`: AnyNumericConversion fires (both numeric). The dispatch returns the
// ExplicitNumericConversion singleton. `long -> int` is a narrowing conversion (no implicit
// counterpart), the classic explicit numeric case. The two `Def` instances are distinct
// (StructuralEquals is identity), so this is a pure numeric-arm test.
TEST(CSharpConversionsExplicitTest, NumericConversionFiresForNarrowing) {
    ITypePtr fromLong = Def(KnownTypeCode::Int64);
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    auto c = ExplicitConversionImpl(Compilation(), *fromLong, *toInt);
    EXPECT_EQ(c.get(), Conversions::ExplicitNumericConversion().get());
}

// ---------------------------------------------------------------------------
// Enumeration arm -- enum -> int -> EnumerationConversion(false, false) factory.
// ---------------------------------------------------------------------------

// `enum -> int`: AnyNumericConversion fails (an enum is NOT numeric -- Kind == Enum), then
// ExplicitEnumerationConversion fires (fromType.Kind == Enum, toType is numeric). Returns
// EnumerationConversion(false, false) -- a FACTORY (fresh per-call instance), NOT a singleton, so
// the test asserts flags (IsExplicit, !IsLifted, IsEnumerationConversion, !IsNumericConversion),
// not pointer-identity.
TEST(CSharpConversionsExplicitTest, EnumerationConversionFiresForEnumToNumeric) {
    ITypePtr fromEnum = EnumDef();
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    auto c = ExplicitConversionImpl(Compilation(), *fromEnum, *toInt);
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_FALSE(c->IsLifted());
    EXPECT_TRUE(c->IsEnumerationConversion());
    EXPECT_FALSE(c->IsNumericConversion());
}

// ---------------------------------------------------------------------------
// Nullable arm -- lifted identity: int -> Nullable<int> -> ExplicitNullableConversion singleton.
// ---------------------------------------------------------------------------

// `int -> Nullable<int>`: AnyNumericConversion fails (Nullable<int> not numeric),
// ExplicitEnumerationConversion fails (neither is enum), then ExplicitNullableConversion fires:
// IsNullable(toType) true, GetUnderlyingType(toType)=int (the SAME instance as fromType),
// GetUnderlyingType(fromType)=int (not nullable, returns fromType), IdentityConversion(int, int)
// on the same instance is true -> ExplicitNullableConversion. The SAME int instance is reused for
// the from-side and the Nullable's type argument.
TEST(CSharpConversionsExplicitTest, NullableConversionFiresForLiftedIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    auto c = ExplicitConversionImpl(Compilation(), *intType, *nullableInt);
    EXPECT_EQ(c.get(), Conversions::ExplicitNullableConversion().get());
}

// ---------------------------------------------------------------------------
// Nullable arm -- lifted numeric: int -> Nullable<long> -> ExplicitLiftedNumericConversion singleton.
// ---------------------------------------------------------------------------

// `int -> Nullable<long>`: AnyNumericConversion fails, ExplicitEnumerationConversion fails, then
// ExplicitNullableConversion fires: IsNullable(toType) true, GetUnderlyingType(toType)=long,
// GetUnderlyingType(fromType)=int (not nullable), IdentityConversion(int, long) fails (distinct
// instances), AnyNumericConversion(int, long) true -> ExplicitLiftedNumericConversion.
TEST(CSharpConversionsExplicitTest, NullableConversionFiresForLiftedNumeric) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toNullableLong = NullableOf(Def(KnownTypeCode::Int64));
    auto c = ExplicitConversionImpl(Compilation(), *fromInt, *toNullableLong);
    EXPECT_EQ(c.get(), Conversions::ExplicitLiftedNumericConversion().get());
}

// ---------------------------------------------------------------------------
// Reference arm -- interface -> class -> ExplicitReferenceConversion singleton.
// ---------------------------------------------------------------------------

// `interface -> class`: AnyNumericConversion fails (neither numeric), ExplicitEnumerationConversion
// fails (neither enum), ExplicitNullableConversion returns None (neither nullable), then
// ExplicitReferenceConversion fires: both IsReferenceType true (RefDef), interface on from-side
// always yields true (the unsealed-unsealed arm). Returns the ExplicitReferenceConversion singleton.
TEST(CSharpConversionsExplicitTest, ReferenceConversionFiresForInterfaceToClass) {
    auto iface = MakeRefDef(KnownTypeCode::None, TypeKind::Interface);
    auto cls = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
    auto c = ExplicitConversionImpl(Compilation(), *iface, *cls);
    EXPECT_EQ(c.get(), Conversions::ExplicitReferenceConversion().get());
}

// ---------------------------------------------------------------------------
// Unboxing arm -- object -> int -> UnboxingConversion singleton.
// ---------------------------------------------------------------------------

// `object -> int`: AnyNumericConversion fails (object not numeric), ExplicitEnumerationConversion
// fails, ExplicitNullableConversion None, ExplicitReferenceConversion fails (int.IsReferenceType
// == false, the guard needs true on both sides), then UnboxingConversion fires: object is a
// reference type (ObjectDef), int is a value type (KnownType(Int32).IsReferenceType == false),
// IsSubtypeOf(int, object, 0) short-circuits via IsKnownType(object, Object). Returns the
// UnboxingConversion singleton. `KnownType(Int32)` (NOT an `ITypeDefinition`) is the faithful
// value-type stub (IsReferenceType == false, definite); a `Def(Int32)` has IsReferenceType ==
// nullopt (the default), which fails the unboxing to-side guard (needs a definite false).
TEST(CSharpConversionsExplicitTest, UnboxingConversionFiresForObjectToInt) {
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto c = ExplicitConversionImpl(Compilation(), *ObjectDef(), *intType);
    EXPECT_EQ(c.get(), Conversions::UnboxingConversion().get());
}

// ---------------------------------------------------------------------------
// Type-parameter arm -- object -> T -> UnboxingConversion singleton (via ExplicitTypeParameterConversion).
// ---------------------------------------------------------------------------

// `object -> T`: AnyNumericConversion fails, ExplicitEnumerationConversion fails, ExplicitNullableConversion
// None, ExplicitReferenceConversion fails (T.IsReferenceType == nullopt, the guard needs true),
// UnboxingConversion fails (T.IsReferenceType == nullopt, the to-side guard needs a definite false),
// then ExplicitTypeParameterConversion fires: toType.Kind == TypeParameter, IsSubtypeOf(T, object, 0)
// short-circuits via IsKnownType(object, Object) -> UnboxingConversion. Returns the UnboxingConversion
// singleton (the same singleton the direct unboxing arm returns).
TEST(CSharpConversionsExplicitTest, TypeParameterConversionFiresForObjectToT) {
    ITypePtr t = TypeParam();
    auto c = ExplicitConversionImpl(Compilation(), *ObjectDef(), *t);
    EXPECT_EQ(c.get(), Conversions::UnboxingConversion().get());
}

// ---------------------------------------------------------------------------
// Pointer arm -- int* -> void* -> ExplicitPointerConversion singleton.
// ---------------------------------------------------------------------------

// `int* -> void*`: AnyNumericConversion fails (int* not numeric), ExplicitEnumerationConversion
// fails, ExplicitNullableConversion None, ExplicitReferenceConversion fails (int*.IsReferenceType
// is nullopt, the guard needs true), UnboxingConversion fails (int*.IsReferenceType nullopt),
// ExplicitTypeParameterConversion None (neither is TypeParameter), then ExplicitPointerConversion
// fires: IsAnyPointer(int*.Kind) true (PointerType), IsAnyPointer(void*.Kind) true -> any pointer
// converts to any other pointer. Returns the ExplicitPointerConversion singleton.
TEST(CSharpConversionsExplicitTest, PointerConversionFiresForIntPtrToVoidPtr) {
    ITypePtr intPtr = IntPtr();
    ITypePtr voidPtr = VoidPtr();
    auto c = ExplicitConversionImpl(Compilation(), *intPtr, *voidPtr);
    EXPECT_EQ(c.get(), Conversions::ExplicitPointerConversion().get());
}

// ---------------------------------------------------------------------------
// None case -- bool -> string: no arm fires -> Conversions::None().
// ---------------------------------------------------------------------------

// `bool -> string`: AnyNumericConversion fails (`bool` is not numeric -- TypeCode::Boolean is
// outside the [Char..Decimal] range), ExplicitEnumerationConversion fails (neither is enum),
// ExplicitNullableConversion returns None (neither is nullable), ExplicitReferenceConversion fails
// (the from-side guard: `bool` is not a reference type and not a type parameter), UnboxingConversion
// fails (the from-side guard: `bool` is not a reference type), ExplicitTypeParameterConversion
// returns None (neither is a type parameter), ExplicitPointerConversion fails (`bool` is not a
// pointer and not an integer type -- TypeCode::Boolean is outside the [SByte..UInt64] range; `string`
// is not a pointer). No arm fires -> None. (Note: `int -> void*` would fire the pointer arm because
// `int` is an integer type and `void*` is a pointer type -- any integer converts to any pointer.)
TEST(CSharpConversionsExplicitTest, NoneReturnedWhenNoArmFires) {
    ITypePtr boolType = Def(KnownTypeCode::Boolean);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = ExplicitConversionImpl(Compilation(), *boolType, *stringType);
    EXPECT_EQ(c.get(), Conversions::None().get());
}
