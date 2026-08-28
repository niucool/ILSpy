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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` pointer-conversion region (CSharpConversions.cs lines
// 898-940, C# spec draft-v11 section 24.5): `ImplicitPointerConversion`, `ExplicitPointerConversion`,
// and the `IsIntegerType` helper (line 927) `ExplicitPointerConversion` consumes. The pointer
// helpers consume the already-ported `IdentityConversion` (D514), `ImplicitReferenceConversion`
// (D517), and this iteration's new `IsAnyPointer(TypeKind)` prerequisite (TypeSystemExtensions.cs
// line 448) -- a free function true for `TypeKind::Pointer` or `TypeKind::FunctionPointer`.
//
// CRUX STUB CONVENTIONS (carried from the D517/D519 reference/boxing tests):
//  * `IsAnyPointer(Kind)` is the free `TypeSystemExtensions` helper; `PointerType` (Kind ==
//    Pointer) and `FunctionPointerType` (Kind == FunctionPointer) are the two pointer kinds.
//    `PointerType::ReflectionName()` is `element->ReflectionName() + "*"`; a `PointerType` over
//    `KnownType(Void)` renders `"System.Void*"` (the `System.Void*` target the void*-arm checks).
//  * `KnownType` primitives carry their `KnownTypeCode` (a value type for `Int32`/`Void`, a
//    reference type for `String`); they are NOT `ITypeDefinition`s (`GetDefinition() == nullptr`),
//    but the faithful stubs for plain element types. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true`.
//  * `SpecialType(TypeKind::Null, true)` is the null-literal type (the `TypeKind.Null` the
//    null-arm checks); its `IsReferenceType` is `true` (the C# `SpecialType.NullType` singleton).
//  * `IsSubtypeOf(s, t, 0)` short-circuits to true when `t` carries `KnownTypeCode::Object`
//    (`IsKnownType(t, Object)` -- the type's own `GetDefinition()?.KnownTypeCode`), so a `RefDef`
//    (a `LookupTypeDefinition` with `IsReferenceType == true`) carrying `KnownTypeCode::Object`
//    makes a contravariant param `ImplicitReferenceConversion(Class, Object)` resolve without
//    modelling a base-type chain. A non-Object `RefDef` (a Class) stands in for a plain
//    reference type on the other side of the contravariant pair.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::ExplicitPointerConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitPointerConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsIntegerType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a
// reference type -- the faithful value the `ImplicitReferenceConversion` guard needs for the
// contravariant function-pointer param case; the base `LookupTypeDefinition` default is
// `std::nullopt`, which fails the guard). Inherits the `LookupTypeDefinition` ctor.
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
// makes `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit
// that the contravariant param `ImplicitReferenceConversion(Class, Object)` arm hits).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
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

// A plain reference-type definition (a non-Object class) carrying `KnownTypeCode::None`.
std::shared_ptr<RefDef> ClassDef() {
    return MakeRefDef(KnownTypeCode::None, TypeKind::Class);
}

// A `KnownType` primitive (a value type for `Int32`/`Void`, a reference type for `String`);
// NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful stub for a plain
// element / return / param type. `KnownType::StructuralEquals` is value-based (by `KnownTypeCode`),
// so two distinct instances compare equal -- the identity-conversion crux cases (function-pointer
// return/param types) reuse the value-based equality rather than requiring the same instance.
// Use `Prim` (a `LookupTypeDefinition`) instead when `GetTypeCode` must resolve (the `IsIntegerType`
// range check reads `GetTypeCode`, which `dynamic_cast`s to `ITypeDefinition` -- a `KnownType` yields
// `TypeCode::Empty`, NOT the primitive's `TypeCode`).
ITypePtr prim(KnownTypeCode ktc) {
    return std::make_shared<KnownType>(ktc);
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind`. `GetTypeCode` resolves the `KnownTypeCode` via the `ITypeDefinition`
// cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Prim(Int32)` reports
// `TypeCode::Int32` -- the observable behavior of a real primitive definition. The D514 numeric-test
// `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive definition (Int32, Char, String, ...) -- a `LookupTypeDefinition` whose `GetTypeCode`
// resolves the `KnownTypeCode` to the matching `TypeCode`. Use this (NOT `prim`) where `IsIntegerType`
// reads `GetTypeCode` (the range check `c >= SByte && c <= UInt64`).
std::shared_ptr<LookupTypeDefinition> Prim(KnownTypeCode ktc) {
    return MakeDef(ktc, TypeKind::Struct);
}

// The null-literal type (`TypeKind::Null`). Faithful to the C# `SpecialType.NullType` singleton
// (`isReferenceType: true`); `ImplicitPointerConversion`'s null-arm checks `Kind == Null`.
ITypePtr NullType() {
    return std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
}

// `System.Void*` -- a `PointerType` over `KnownType(Void)`. `PointerType::ReflectionName()` is
// `element->ReflectionName() + "*"`, and `KnownType(Void).ReflectionName()` is `"System.Void"`,
// so the result is `"System.Void*"` (the target the void*-arm checks).
ITypePtr VoidPtr() {
    return std::make_shared<PointerType>(prim(KnownTypeCode::Void));
}

// `int*` -- a `PointerType` over `KnownType(Int32)`. `ReflectionName()` is `"System.Int32*"`.
ITypePtr IntPtr() {
    return std::make_shared<PointerType>(prim(KnownTypeCode::Int32));
}

// `delegate*<T>(params...)` -- a `FunctionPointerType` with the supplied calling convention,
// return type, and by-value parameter types. The `customCallingConventions` is empty (the
// default managed calling convention shape); `returnIsRefReadOnly` is false; every parameter
// is `ReferenceKind::None` (a plain by-value parameter).
ITypePtr FnPtr(SignatureCallingConvention cc, ITypePtr ret, std::vector<ITypePtr> params) {
    std::vector<ReferenceKind> refKinds(params.size(), ReferenceKind::None);
    std::vector<ITypePtr> emptyCustom;
    return std::make_shared<FunctionPointerType>(cc, emptyCustom, std::move(ret), false,
        std::move(params), std::move(refKinds));
}

// `delegate*<T>(params...)` with the default calling convention (the common case).
ITypePtr FnPtr(ITypePtr ret, std::vector<ITypePtr> params) {
    return FnPtr(SignatureCallingConvention::Default, std::move(ret), std::move(params));
}

} // namespace

// ===========================================================================
// IsIntegerType (CSharpConversions.cs line 927) -- direct crux cases.
// ===========================================================================

// `nint` (a native integer): recognized via `Kind == NInt` before the `GetTypeCode` range check.
TEST(CSharpConversionsPointerTest, IsIntegerTypeNativeInt) {
    ITypePtr nint = std::make_shared<SpecialType>(TypeKind::NInt, std::optional<bool>(false));
    EXPECT_TRUE(IsIntegerType(*nint));
}

// `nuint` (an unsigned native integer): recognized via `Kind == NUInt`.
TEST(CSharpConversionsPointerTest, IsIntegerTypeNativeUInt) {
    ITypePtr nuint = std::make_shared<SpecialType>(TypeKind::NUInt, std::optional<bool>(false));
    EXPECT_TRUE(IsIntegerType(*nuint));
}

// `sbyte` (the low edge of the `GetTypeCode` integral range, `TypeCode::SByte`).
TEST(CSharpConversionsPointerTest, IsIntegerTypeSByteIsRangeLowEdge) {
    EXPECT_TRUE(IsIntegerType(*Prim(KnownTypeCode::SByte)));
}

// `int` (a mid-range integral primitive, `TypeCode::Int32`).
TEST(CSharpConversionsPointerTest, IsIntegerTypeInt32) {
    EXPECT_TRUE(IsIntegerType(*Prim(KnownTypeCode::Int32)));
}

// `ulong` (the high edge of the `GetTypeCode` integral range, `TypeCode::UInt64`).
TEST(CSharpConversionsPointerTest, IsIntegerTypeUInt64IsRangeHighEdge) {
    EXPECT_TRUE(IsIntegerType(*Prim(KnownTypeCode::UInt64)));
}

// `char` (`TypeCode::Char`, ordinal 4) is BELOW `TypeCode::SByte` (ordinal 5) -- not an integer.
TEST(CSharpConversionsPointerTest, IsIntegerTypeCharIsNotInteger) {
    EXPECT_FALSE(IsIntegerType(*Prim(KnownTypeCode::Char)));
}

// `string` (`TypeCode::String`, ordinal 17) is ABOVE `TypeCode::UInt64` (ordinal 12) -- not an
// integer. (`Prim` resolves `GetTypeCode` to `TypeCode::String`; a `KnownType(String)` would yield
// `TypeCode::Empty` instead -- both are outside the `[SByte..UInt64]` range, but `Prim` is the
// faithful path.)
TEST(CSharpConversionsPointerTest, IsIntegerTypeStringIsNotInteger) {
    EXPECT_FALSE(IsIntegerType(*Prim(KnownTypeCode::String)));
}

// An unknown type (`TypeCode::Empty`) is not an integer -- the `GetTypeCode` range check fails.
TEST(CSharpConversionsPointerTest, IsIntegerTypeUnknownIsNotInteger) {
    ITypePtr unknown = std::make_shared<SpecialType>(TypeKind::Unknown);
    EXPECT_FALSE(IsIntegerType(*unknown));
}

// ===========================================================================
// ImplicitPointerConversion (CSharpConversions.cs line 898, spec 24.5) -- the void* arm.
// ===========================================================================

// `int* -> void*`: `int*` is a pointer (`IsAnyPointer(Pointer)` true), `void*` is a `PointerType`
// whose `ReflectionName` is `"System.Void*"` -- the void*-arm fires.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionPointerToVoidPointer) {
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *IntPtr(), *VoidPtr()));
}

// `delegate*<int>(int) -> void*`: a function pointer is `IsAnyPointer(FunctionPointer)` true, so
// it converts to `void*` (the void*-arm fires for any pointer kind, not just `PointerType`).
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionFunctionPointerToVoidPointer) {
    ITypePtr fn = FnPtr(prim(KnownTypeCode::Int32), {prim(KnownTypeCode::Int32)});
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *fn, *VoidPtr()));
}

// `int -> void*`: `int` is not a pointer (`IsAnyPointer(Struct)` false) -- the void*-arm's
// from-side guard fails; `int` is not null and not a function pointer -- the other arms fail too.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsNonPointerFromType) {
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *prim(KnownTypeCode::Int32), *VoidPtr()));
}

// `int* -> int*`: the to-side `ReflectionName` is `"System.Int32*"` (not `"System.Void*"`) -- the
// void*-arm fails; `int*` is not null and not a function pointer -- the other arms fail too.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsNonVoidPointerToType) {
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *IntPtr(), *IntPtr()));
}

// ===========================================================================
// ImplicitPointerConversion (line 898, spec 24.5) -- the null-literal arm.
// ===========================================================================

// `null -> int*`: the null literal (`TypeKind.Null`) converts to any pointer (`IsAnyPointer(Pointer)`
// true) -- the null-arm fires.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionNullToPointer) {
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *NullType(), *IntPtr()));
}

// `null -> delegate*<int>(int)`: the null literal converts to a function pointer too
// (`IsAnyPointer(FunctionPointer)` true).
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionNullToFunctionPointer) {
    ITypePtr fn = FnPtr(prim(KnownTypeCode::Int32), {prim(KnownTypeCode::Int32)});
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *NullType(), *fn));
}

// `null -> int`: the null literal does NOT convert to a non-pointer type (`IsAnyPointer(Struct)`
// false) -- the null-arm fails; `null` is not a function pointer -- the function-pointer arm fails.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionNullToNonPointer) {
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *NullType(), *prim(KnownTypeCode::Int32)));
}

// ===========================================================================
// ImplicitPointerConversion (line 898, spec 24.5) -- the function-pointer-variance arm.
// ===========================================================================

// `delegate*<int>(int) -> delegate*<int>(int)`: same calling convention, same param count, the
// return types are identical (`IdentityConversion(int, int)` true), and the (contravariantly
// swapped) param types are identical (`IdentityConversion(int, int)` true) -- the arm returns true.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionFunctionPointerIdentity) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr from = FnPtr(intEl, {intEl});
    ITypePtr to = FnPtr(intEl, {intEl});
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `delegate*<int>(int) -> delegate*<int>(int)` with a DIFFERENT calling convention: the
// `CallingConvention` equality check fails -- the arm does not fire.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsDifferentCallingConvention) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr from = FnPtr(SignatureCallingConvention::Default, intEl, {intEl});
    ITypePtr to = FnPtr(SignatureCallingConvention::CDecl, intEl, {intEl});
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `delegate*<int>(int) -> delegate*<int>(int, int)`: different parameter count -- the `Length`
// equality check fails -- the arm does not fire.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsDifferentParameterCount) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr from = FnPtr(intEl, {intEl});
    ITypePtr to = FnPtr(intEl, {intEl, intEl});
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `delegate*<int>(int) -> delegate*<string>(int)`: the return types are NOT convertible
// (`IdentityConversion(int, string)` false; `ImplicitReferenceConversion(int, string)` false --
// `int` is not a reference type, the guard fails) -- the return-type check fails, the arm
// returns false.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsNonConvertibleReturnType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr strEl = prim(KnownTypeCode::String);
    ITypePtr from = FnPtr(intEl, {intEl});
    ITypePtr to = FnPtr(strEl, {intEl});
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `delegate*<void>(int) -> delegate*<void>(string)`: the return types are identical (void == void),
// but the (contravariantly swapped) param types are NOT convertible
// (`IdentityConversion(string, int)` false; `ImplicitReferenceConversion(string, int)` false --
// `int` is not a reference type, the guard fails) -- the param check fails, the arm returns false.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsNonConvertibleParameterType) {
    ITypePtr voidEl = prim(KnownTypeCode::Void);
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr strEl = prim(KnownTypeCode::String);
    ITypePtr from = FnPtr(voidEl, {intEl});
    ITypePtr to = FnPtr(voidEl, {strEl});
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `delegate*<void>(object) -> delegate*<void>(SomeClass)`: the return types are identical, and
// the (contravariantly swapped) param types are implicitly reference-convertible
// (`ImplicitReferenceConversion(SomeClass, object)` -- `IsSubtypeOf(SomeClass, object, 0)`
// short-circuits via `IsKnownType(object, Object)` true). Pins the SWAPPED order: the check is
// `ImplicitReferenceConversion(toPT, fromPT)`, i.e. the TARGET's param (`SomeClass`) must be
// convertible to the SOURCE's param (`object`) -- the contravariant direction.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionContravariantParameterIsTrue) {
    ITypePtr voidEl = prim(KnownTypeCode::Void);
    auto objectRef = ObjectDef();
    auto classRef = ClassDef();
    ITypePtr from = FnPtr(voidEl, {objectRef});
    ITypePtr to = FnPtr(voidEl, {classRef});
    EXPECT_TRUE(ImplicitPointerConversion(Compilation(), *from, *to));
}

// `int -> delegate*<int>(int)`: `int` is not a pointer (the void*-arm fails), not null (the
// null-arm fails), and not a function pointer (the function-pointer arm's `dynamic_cast` yields
// null) -- every arm fails, returns false.
TEST(CSharpConversionsPointerTest, ImplicitPointerConversionRejectsNonPointerNonFunctionFromType) {
    ITypePtr intEl = prim(KnownTypeCode::Int32);
    ITypePtr fn = FnPtr(intEl, {intEl});
    EXPECT_FALSE(ImplicitPointerConversion(Compilation(), *intEl, *fn));
}

// ===========================================================================
// ExplicitPointerConversion (CSharpConversions.cs line 917, spec 24.5) -- direct crux cases.
// ===========================================================================

// `int* -> void*`: a pointer converts to any other pointer (`IsAnyPointer(Pointer)` true on the
// to-side) -- the pointer-to-pointer arm.
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionPointerToPointer) {
    EXPECT_TRUE(ExplicitPointerConversion(*IntPtr(), *VoidPtr()));
}

// `int* -> int`: a pointer converts to any integer type (`IsIntegerType(int)` true) -- the
// pointer-to-integer arm.
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionPointerToInteger) {
    EXPECT_TRUE(ExplicitPointerConversion(*IntPtr(), *Prim(KnownTypeCode::Int32)));
}

// `delegate*<int>(int) -> int`: a function pointer is `IsAnyPointer(FunctionPointer)` true, so it
// converts to an integer (the `IsAnyPointer(fromType.Kind())` guard covers both pointer kinds).
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionFunctionPointerToInteger) {
    ITypePtr fn = FnPtr(prim(KnownTypeCode::Int32), {prim(KnownTypeCode::Int32)});
    EXPECT_TRUE(ExplicitPointerConversion(*fn, *Prim(KnownTypeCode::Int32)));
}

// `int -> int*`: an integer converts to a pointer (`IsAnyPointer(Pointer)` true on the to-side
// AND `IsIntegerType(int)` true on the from-side) -- the integer-to-pointer arm.
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionIntegerToPointer) {
    EXPECT_TRUE(ExplicitPointerConversion(*Prim(KnownTypeCode::Int32), *IntPtr()));
}

// `int -> delegate*<int>(int)`: an integer converts to a function pointer too (`IsAnyPointer(FunctionPointer)`
// true on the to-side).
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionIntegerToFunctionPointer) {
    ITypePtr fn = FnPtr(prim(KnownTypeCode::Int32), {prim(KnownTypeCode::Int32)});
    EXPECT_TRUE(ExplicitPointerConversion(*Prim(KnownTypeCode::Int32), *fn));
}

// `int -> string`: `int` IS an integer (`IsIntegerType(Prim(Int32))` true), but `string` is not a
// pointer (`IsAnyPointer(Class)` false) -- the else-arm `IsAnyPointer(toType) && IsIntegerType(fromType)`
// fails because the to-side is not a pointer. Pins that an integer-to-non-pointer conversion is NOT
// an explicit pointer conversion.
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionRejectsIntegerToNonPointer) {
    EXPECT_FALSE(ExplicitPointerConversion(*Prim(KnownTypeCode::Int32), *prim(KnownTypeCode::String)));
}

// `string -> int*`: `string` is not an integer (`IsIntegerType(String)` false) -- the integer-to-
// pointer arm fails (`IsAnyPointer(toType.Kind()) && IsIntegerType(fromType)` -- the `&&` fails).
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionRejectsNonIntegerToPointer) {
    EXPECT_FALSE(ExplicitPointerConversion(*prim(KnownTypeCode::String), *IntPtr()));
}

// `int* -> string`: `string` is neither a pointer nor an integer -- the if-arm's
// `IsAnyPointer(toType.Kind()) || IsIntegerType(toType)` fails on both disjuncts.
TEST(CSharpConversionsPointerTest, ExplicitPointerConversionRejectsPointerToNonPointerNonInteger) {
    EXPECT_FALSE(ExplicitPointerConversion(*IntPtr(), *prim(KnownTypeCode::String)));
}
