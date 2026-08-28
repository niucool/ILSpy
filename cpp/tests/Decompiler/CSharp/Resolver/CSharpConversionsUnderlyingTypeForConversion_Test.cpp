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

// Tests for the `CSharpConversions.UnderlyingTypeForConversion` helper (CSharpConversions.cs line
// 1164) -- the type to use for looking up user-defined conversion operators. If the type is a
// `ByReferenceType` (a `ref` parameter/local), unwrap to the element type first; then strip the
// `Nullable<T>` wrapper via `NullableType.GetUnderlyingType`. The result is the type whose method
// table `GetApplicableConversionOperators` scans for `op_Implicit` / `op_Explicit` operators.
//
//   static IType UnderlyingTypeForConversion(IType type)
//   {
//       if (type.Kind == TypeKind.ByReference)
//           type = ((ByReferenceType)type).ElementType;
//       return NullableType.GetUnderlyingType(type);
//   }
//
// CRUX STUB CONVENTIONS (carried from the D514-D528 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). A non-nullable value
//    type like `int` is the plain `Def(Int32)` -- `GetUnderlyingType(int)` returns `int` itself
//    (the `else` branch), so `UnderlyingTypeForConversion(int)` returns the SAME object.
//  * `NullableOf(element)` builds `Nullable<T>` as a `ParameterizedType` over the `NullableOfT`
//    definition (the D515 `IsNullable` / `GetUnderlyingType` test precedent). `GetUnderlyingType`
//    returns the type argument (the `element`), so `UnderlyingTypeForConversion(Nullable<int>)`
//    returns the `int` element (address-equal to the `intEl` shared handle).
//  * `ByRefOf(element)` builds a `ByReferenceType` over the element (the D519 boxing test
//    precedent). `UnderlyingTypeForConversion` unwraps the `ByReferenceType` first (the element
//    is owned by the `ByReferenceType`'s `element_`), then calls `GetUnderlyingType` on the
//    element -- so `UnderlyingTypeForConversion(ref int)` returns the `int` element (address-equal
//    to the `intEl` shared handle), and `UnderlyingTypeForConversion(ref Nullable<int>)` returns
//    the `int` underlying the `Nullable<int>`.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::UnderlyingTypeForConversion
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"                            // ByReferenceType, ParameterizedType, ITypePtr
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"                   // GetUnderlyingType (the direct-call crux comparison)
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::UnderlyingTypeForConversion;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`,
// so `Def(Int32)` reports `TypeCode::Int32`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`) -- the
// D515 `IsNullable` / `GetUnderlyingType` test precedent.
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

// A `ByReferenceType` over the supplied element (a `ref` parameter/local -- `IsByRefLike == true`,
// `Kind == TypeKind::ByReference`). The D519 boxing test precedent.
ITypePtr ByRefOf(ITypePtr element) {
    return std::make_shared<ByReferenceType>(std::move(element));
}

} // namespace

// ===========================================================================
// UnderlyingTypeForConversion (CSharpConversions.cs line 1164).
// ===========================================================================

// A non-ByReference, non-Nullable type: `GetUnderlyingType(int)` returns `int` itself (the `else`
// branch returns the original `type`), so `UnderlyingTypeForConversion(int)` returns the SAME
// object (address equality).
TEST(CSharpConversionsUnderlyingTypeForConversionTest, ReturnsTypeItselfForNonNullableNonByReference) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    const IType& result = UnderlyingTypeForConversion(*intType);
    EXPECT_EQ(&result, &*intType);
}

// `Nullable<T>` (non-ByReference): `GetUnderlyingType(Nullable<int>)` returns the type argument
// `int` (the `intEl` shared handle stored as the `ParameterizedType`'s `typeArgs_[0]`), so the
// returned reference is address-equal to the `intEl` element. This pins the nullable-strip arm.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, StripsNullableWrapperForNullableType) {
    ITypePtr intEl = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    const IType& result = UnderlyingTypeForConversion(*nullableInt);
    EXPECT_EQ(&result, &*intEl);
}

// A `ByReferenceType` over a non-Nullable type: the `ByReferenceType` is unwrapped first (the
// element `int` is extracted via `Element()`), then `GetUnderlyingType(int)` returns `int` itself
// (the `else` branch). The returned reference is address-equal to the `intEl` element (owned by the
// `ByReferenceType`'s `element_`). This pins the ByReference-unwrap arm.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, UnwrapsByReferenceThenReturnsElement) {
    ITypePtr intEl = Def(KnownTypeCode::Int32);
    ITypePtr byRefInt = ByRefOf(intEl);
    const IType& result = UnderlyingTypeForConversion(*byRefInt);
    EXPECT_EQ(&result, &*intEl);
}

// A `ByReferenceType` over a `Nullable<T>`: the `ByReferenceType` is unwrapped first (the element
// `Nullable<int>` is extracted), then `GetUnderlyingType(Nullable<int>)` returns the type argument
// `int`. The returned reference is address-equal to the `intEl` element (the `Nullable<int>`'s
// type argument is the `intEl` shared handle). This pins the ByReference-then-nullable double-unwrap.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, UnwrapsByReferenceThenStripsNullable) {
    ITypePtr intEl = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    ITypePtr byRefNullable = ByRefOf(nullableInt);
    const IType& result = UnderlyingTypeForConversion(*byRefNullable);
    EXPECT_EQ(&result, &*intEl);
}

// A `ByReferenceType` over a reference type (e.g. `ref string`): the `ByReferenceType` is unwrapped
// to the element `string`, then `GetUnderlyingType(string)` returns `string` itself (not nullable).
// The returned reference is address-equal to the `stringEl` element. This pins the ByReference
// unwrap over a non-value type.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, UnwrapsByReferenceOverReferenceType) {
    ITypePtr stringEl = Def(KnownTypeCode::String, TypeKind::Class);
    ITypePtr byRefString = ByRefOf(stringEl);
    const IType& result = UnderlyingTypeForConversion(*byRefString);
    EXPECT_EQ(&result, &*stringEl);
}

// `UnderlyingTypeForConversion` agrees with `GetUnderlyingType` for a non-ByReference type -- the
// helper is a thin ByReference-unwrap-then-`GetUnderlyingType` wrapper, so when the type is NOT a
// `ByReferenceType` the result is exactly `GetUnderlyingType(type)`. This pins the delegation.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, AgreesWithGetUnderlyingTypeForNonByReference) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(Def(KnownTypeCode::Int32));
    const IType& resultInt = UnderlyingTypeForConversion(*intType);
    const IType& directInt = TS::GetUnderlyingType(*intType);
    EXPECT_EQ(&resultInt, &directInt);
    const IType& resultNullable = UnderlyingTypeForConversion(*nullableInt);
    const IType& directNullable = TS::GetUnderlyingType(*nullableInt);
    EXPECT_EQ(&resultNullable, &directNullable);
}

// A `ByReferenceType` is NOT itself nullable (`IsNullable` checks for `ParameterizedType` with the
// `NullableOfT` generic -- a `ByReferenceType` has `Kind == ByReference`, not `ParameterizedType`),
// so without the ByReference unwrap, `GetUnderlyingType` would return the `ByReferenceType` itself.
// The unwrap is load-bearing: `UnderlyingTypeForConversion(ref Nullable<int>)` returns `int`, NOT
// the `ByReferenceType` (which `GetUnderlyingType` alone would return). This is the crux that
// distinguishes `UnderlyingTypeForConversion` from a plain `GetUnderlyingType` call.
TEST(CSharpConversionsUnderlyingTypeForConversionTest, ByReferenceUnwrapIsLoadBearingForNullableElement) {
    ITypePtr intEl = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intEl);
    ITypePtr byRefNullable = ByRefOf(nullableInt);
    // `GetUnderlyingType` alone on the `ByReferenceType` returns the `ByReferenceType` itself
    // (a `ByReferenceType` is not a `Nullable<T>`). The unwrap in `UnderlyingTypeForConversion`
    // is what makes it return the `int` underlying the `Nullable<int>` element instead.
    const IType& directResult = TS::GetUnderlyingType(*byRefNullable);
    EXPECT_EQ(&directResult, &*byRefNullable);
    const IType& result = UnderlyingTypeForConversion(*byRefNullable);
    EXPECT_EQ(&result, &*intEl);
    EXPECT_NE(&result, &*byRefNullable);
}
