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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions.ExplicitConversionNotUserDefined` helper
// (CSharpConversions.cs line 331): the explicit conversion MINUS the user-defined-conversion
// fallback. The C# body is:
//
//   Conversion c = ImplicitConversion(fromType, toType, allowUserDefined: false, allowTuple: false);
//   if (c != Conversion.None) return c;
//   return ExplicitConversionImpl(fromType, toType);
//
// The private `ImplicitConversion(IType, IType, bool allowUserDefined, bool allowTuple)` overload
// (line 166) it calls is `StandardImplicitConversion(fromType, toType, allowTuple)` followed by an
// `allowUserDefined`-gated `UserDefinedImplicitConversion`; with `allowUserDefined: false` the
// user-defined branch is skipped, and the tuple arm is deferred (yields None for tuple shapes),
// so the faithful port is the already-ported `Detail::StandardImplicitConversion` (D523) then, if
// no implicit conversion exists, the already-ported `Detail::ExplicitConversionImpl` (D524).
//
// The load-bearing crux is the IMPLICIT-CHECK-FIRST ordering: an implicit conversion is returned
// even though the name says "ExplicitConversion". This is what distinguishes
// `ExplicitConversionNotUserDefined` from `ExplicitConversionImpl`: for `int -> long`,
// `ExplicitConversionImpl` returns `ExplicitNumericConversion` (the explicit narrowing-aware
// dispatch), but `ExplicitConversionNotUserDefined` returns `ImplicitNumericConversion` (the
// implicit widening wins because the implicit check is first). The crux tests (Group A) pin this:
// under a neuter that skips the implicit check (always returns `ExplicitConversionImpl`), the
// Group A tests fail (the explicit dispatch returns a different / None answer) while the Group B/C
// tests pass (the explicit fallback gives the right answer when no implicit conversion exists).
//
// CRUX STUB CONVENTIONS (carried from the D514-D524 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric helpers).
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//    Used for the boxing from-side (needs a definite `false` IsReferenceType) and the null-literal
//    to-side reference-type check.
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing/unboxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit), so `int -> object` / `string -> object` / `null -> object` are
//    valid boxing/reference/null-literal conversions without modelling a base-type chain.
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition (a
//    1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the element. The lifted-identity test
//    (`int -> Nullable<int>`) reuses the SAME int instance for the from-side and the Nullable's
//    type argument.
//  * `SpecialType(TypeKind::Null, true)` is the null-literal type (`Kind == Null`,
//    `IsReferenceType == true`); the null-literal arm checks `fromType.Kind == Null`.
//  * `LookupTypeParameter` (unconstrained, `IsReferenceType == std::nullopt`) is the faithful stub
//    for the type-parameter arm.
//  * `PointerType` over `KnownType(Void)` has `ReflectionName() == "System.Void*"`; `PointerType`
//    over `KnownType(Int32)` has `"System.Int32*"` (an any-pointer source).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::ExplicitConversionNotUserDefined
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitConversionNotUserDefined;
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
// ExplicitConversionNotUserDefined (CSharpConversions.cs line 331).
//
//   c = StandardImplicitConversion(from, to);  // (the allowUserDefined:false ImplicitConversion)
//   if (c != None) return c;
//   return ExplicitConversionImpl(from, to);
//
// The implicit check is FIRST: an implicit conversion is returned even though the name says
// "ExplicitConversion" (the crux that distinguishes this from ExplicitConversionImpl).
// ===========================================================================

// ---------------------------------------------------------------------------
// Group A -- the implicit check fires: returns the IMPLICIT conversion singleton (NOT the
// explicit one). These are the CRUX tests: under a neuter that skips the implicit check (always
// returns ExplicitConversionImpl), each returns a different / None answer, so these fail.
// ---------------------------------------------------------------------------

// `int -> long`: StandardImplicitConversion fires the numeric widening arm -> ImplicitNumericConversion.
// The crux: `ExplicitConversionImpl(int, long)` would return `ExplicitNumericConversion` (the
// explicit dispatch's numeric arm), but `ExplicitConversionNotUserDefined` returns the implicit
// widening because the implicit check is first. The two `Def` instances are distinct.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsImplicitNumericForWidening) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *fromInt, *toLong);
    EXPECT_EQ(c.get(), Conversions::ImplicitNumericConversion().get());
}

// `int -> int` (same instance): StandardImplicitConversion fires the identity arm ->
// IdentityConversion. The crux: `ExplicitConversionImpl(int, int)` would return
// `ExplicitNumericConversion` (AnyNumericConversion is true for two ints), but
// `ExplicitConversionNotUserDefined` returns identity because the implicit check is first.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsIdentityForSameInstance) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *intType, *intType);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// `int -> object`: StandardImplicitConversion fires the boxing arm -> BoxingConversion. The
// crux: `ExplicitConversionImpl(int, object)` returns None (boxing is implicit, not in the
// explicit dispatch -- int.IsReferenceType is false so the reference/unboxing guards fail), but
// `ExplicitConversionNotUserDefined` returns the boxing conversion because the implicit check is
// first. `KnownType(Int32)` (NOT an `ITypeDefinition`) is the faithful value-type stub
// (IsReferenceType == false, definite); a `Def(Int32)` has IsReferenceType == nullopt, which fails
// the boxing guard.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsBoxingForIntToObject) {
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *intType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::BoxingConversion().get());
}

// `string -> object`: StandardImplicitConversion fires the reference-widening arm ->
// ImplicitReferenceConversion. The crux: `ExplicitConversionImpl(string, object)` returns
// `ExplicitReferenceConversion` (the unsealed-unsealed arm: an implicit reference conversion in
// either direction suffices), but `ExplicitConversionNotUserDefined` returns the implicit
// reference conversion because the implicit check is first. `KnownType(String)` has
// IsReferenceType == true (derived from Kind == Class), so the guard passes.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsImplicitReferenceForStringToObject) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *stringType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::ImplicitReferenceConversion().get());
}

// `null -> object`: StandardImplicitConversion fires the null-literal arm -> NullLiteralConversion
// (fromType.Kind == Null, toType is a reference type). The crux: `ExplicitConversionImpl(null,
// object)` returns `ExplicitReferenceConversion` (null.IsReferenceType == true, the reference
// guard passes, IsSubtypeOf(object) short-circuits), but `ExplicitConversionNotUserDefined`
// returns the null-literal conversion because the implicit check is first.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsNullLiteralForNullToReferenceType) {
    ITypePtr nullType = NullType();
    auto c = ExplicitConversionNotUserDefined(Compilation(), *nullType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::NullLiteralConversion().get());
}

// `int -> Nullable<int>`: StandardImplicitConversion fires the lifted-identity nullable arm ->
// ImplicitNullableConversion. The crux: `ExplicitConversionImpl(int, Nullable<int>)` returns
// `ExplicitNullableConversion` (ExplicitNullableConversion strips the to-side to int, identity on
// the same instance), but `ExplicitConversionNotUserDefined` returns the implicit nullable
// conversion because the implicit check is first. The SAME int instance is reused for the
// from-side and the Nullable's type argument.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsImplicitNullableForLiftedIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *intType, *nullableInt);
    EXPECT_EQ(c.get(), Conversions::ImplicitNullableConversion().get());
}

// ---------------------------------------------------------------------------
// Group B -- the implicit check returns None, so the ExplicitConversionImpl fallback fires:
// returns the EXPLICIT conversion singleton / factory. These are the SENTINEL tests under the
// skip-implicit neuter (the explicit fallback gives the right answer when no implicit exists).
// ---------------------------------------------------------------------------

// `long -> int`: StandardImplicitConversion returns None (no implicit narrowing), then
// ExplicitConversionImpl fires the numeric arm -> ExplicitNumericConversion. `long -> int` is the
// classic explicit narrowing.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsExplicitNumericForNarrowing) {
    ITypePtr fromLong = Def(KnownTypeCode::Int64);
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *fromLong, *toInt);
    EXPECT_EQ(c.get(), Conversions::ExplicitNumericConversion().get());
}

// `object -> int`: StandardImplicitConversion returns None (object -> int is not implicit), then
// ExplicitConversionImpl fires the unboxing arm -> UnboxingConversion. `KnownType(Int32)` is the
// faithful value-type stub (IsReferenceType == false, definite); `ObjectDef` is the reference type.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsUnboxingForObjectToInt) {
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *ObjectDef(), *intType);
    EXPECT_EQ(c.get(), Conversions::UnboxingConversion().get());
}

// `enum -> int`: StandardImplicitConversion returns None (an enum is not implicitly convertible to
// int), then ExplicitConversionImpl fires the enumeration arm -> EnumerationConversion(false, false)
// -- a FACTORY (fresh per-call instance), NOT a singleton, so the test asserts flags (IsExplicit,
// !IsLifted, IsEnumerationConversion, !IsNumericConversion), not pointer-identity.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsEnumerationForEnumToNumeric) {
    ITypePtr fromEnum = EnumDef();
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *fromEnum, *toInt);
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_FALSE(c->IsLifted());
    EXPECT_TRUE(c->IsEnumerationConversion());
    EXPECT_FALSE(c->IsNumericConversion());
}

// `void* -> int*`: StandardImplicitConversion returns None (the implicit pointer arm converts any
// pointer TO void*, never FROM void* -- `void* -> int*` is not an implicit pointer conversion), then
// ExplicitConversionImpl fires the pointer arm -> ExplicitPointerConversion (any pointer converts to
// any other pointer). This is the SENTINEL for the explicit pointer fallback: the explicit dispatch
// gives the right answer when no implicit conversion exists.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsExplicitPointerForVoidPtrToIntPtr) {
    ITypePtr voidPtr = VoidPtr();
    ITypePtr intPtr = IntPtr();
    auto c = ExplicitConversionNotUserDefined(Compilation(), *voidPtr, *intPtr);
    EXPECT_EQ(c.get(), Conversions::ExplicitPointerConversion().get());
}

// ---------------------------------------------------------------------------
// Group C -- both the implicit and explicit checks return None -> None. SENTINEL under both
// neuters.
// ---------------------------------------------------------------------------

// `bool -> string`: StandardImplicitConversion returns None (bool is not implicitly convertible to
// string), and ExplicitConversionImpl returns None (bool is not numeric/enum/nullable/ref/pointer,
// string is not matching). Returns None.
TEST(CSharpConversionsExplicitNotUserDefinedTest, ReturnsNoneWhenNoConversionExists) {
    ITypePtr boolType = Def(KnownTypeCode::Boolean);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = ExplicitConversionNotUserDefined(Compilation(), *boolType, *stringType);
    EXPECT_EQ(c.get(), Conversions::None().get());
}
