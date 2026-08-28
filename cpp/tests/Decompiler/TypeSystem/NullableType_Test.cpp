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

// Tests for the `NullableType` helpers (the D515 port of ICSharpCode.Decompiler/
// TypeSystem/NullableType.cs) -- the `Nullable<T>` static helpers the CSharpConversions
// nullable / null-literal conversion helpers consume. `IsNullable(IType)` is true iff the
// type is a 1-arg `ParameterizedType` over the known `NullableOfT` (custom modifiers
// unwrapped first); `GetUnderlyingType(IType)` returns the type argument for a nullable,
// else the type itself (modifiers preserved); `IsNonNullableValueType(IType)` is
// `IsReferenceType == false && !IsNullable`. The stubs build the `ParameterizedType` /
// `ModifiedType` shapes the helpers dispatch over (`Nullable<int>`, `modopt(Nullable<int>)`,
// `List<int>`, a wrong-arity `Nullable<int,string>`, ...).

#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsNonNullableValueType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) for the
// `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
    return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
        FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::NullableOfT);
}

// A `LookupTypeDefinition` for a non-`Nullable` 1-arg generic (e.g. `List`1`, a class with
// no `KnownTypeCode`) -- the `ParameterizedType` whose generic is NOT `NullableOfT`.
std::shared_ptr<LookupTypeDefinition> ListDef() {
    return std::make_shared<LookupTypeDefinition>("List`1",
        "System.Collections.Generic",
        FullTypeName(TopLevelTypeName("System.Collections.Generic", "List`1", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
}

// `Nullable<T>` over the supplied element type.
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// `List<T>` over the supplied element type (a non-nullable 1-arg `ParameterizedType`).
ITypePtr ListOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(ListDef(), std::vector<ITypePtr>{std::move(element)});
}

// `modopt(<modifier>)<element>` -- a custom-modifier-decorated type.
ITypePtr ModOptOf(ITypePtr modifier, ITypePtr element) {
    return std::make_shared<ModifiedType>(std::move(modifier), std::move(element), /*isRequired=*/false);
}

// A typical custom modifier: `System.Runtime.CompilerServices.IsConst`.
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

} // namespace

// ---------------------------------------------------------------------------
// IsNullable -- true iff a 1-arg ParameterizedType over NullableOfT (modifiers unwrapped).
// ---------------------------------------------------------------------------

TEST(NullableTypeTest, IsNullableTrueForNullableOfT) {
    EXPECT_TRUE(IsNullable(*NullableOf(Int32())));
}

TEST(NullableTypeTest, IsNullableTrueForModifiedNullable) {
    // modopt(IsConst) Nullable<int> -- SkipModifiers unwraps the modopt, leaving Nullable<int>.
    EXPECT_TRUE(IsNullable(*ModOptOf(IsConstModifier(), NullableOf(Int32()))));
}

TEST(NullableTypeTest, IsNullableFalseForNonParameterizedType) {
    EXPECT_FALSE(IsNullable(*Int32()));
    EXPECT_FALSE(IsNullable(*String()));
    EXPECT_FALSE(IsNullable(*UnknownType()));
}

TEST(NullableTypeTest, IsNullableFalseForNonNullableParameterizedType) {
    // List<int> -- a 1-arg ParameterizedType whose generic is NOT NullableOfT.
    EXPECT_FALSE(IsNullable(*ListOf(Int32())));
}

TEST(NullableTypeTest, IsNullableFalseForWrongArity) {
    // Nullable<int, string> -- a 2-arg ParameterizedType over the Nullable`1 definition.
    // TypeParameterCount() == 2, so the == 1 guard rejects it.
    std::vector<ITypePtr> args;
    args.push_back(Int32());
    args.push_back(String());
    auto twoArgNullable = std::make_shared<ParameterizedType>(NullableDef(), std::move(args));
    EXPECT_FALSE(IsNullable(*twoArgNullable));
}

TEST(NullableTypeTest, IsNullableFalseForModifiedNonNullable) {
    // modopt(IsConst) int -- SkipModifiers unwraps to int, which is not a ParameterizedType.
    EXPECT_FALSE(IsNullable(*ModOptOf(IsConstModifier(), Int32())));
}

// ---------------------------------------------------------------------------
// GetUnderlyingType -- the type argument for a nullable, else the type itself.
// ---------------------------------------------------------------------------

TEST(NullableTypeTest, GetUnderlyingTypeReturnsTypeArgumentForNullable) {
    ITypePtr element = Int32();
    ITypePtr nullable = NullableOf(element);
    EXPECT_EQ(&GetUnderlyingType(*nullable), element.get());
}

TEST(NullableTypeTest, GetUnderlyingTypeReturnsTypeArgumentForModifiedNullable) {
    ITypePtr element = Int32();
    ITypePtr modified = ModOptOf(IsConstModifier(), NullableOf(element));
    EXPECT_EQ(&GetUnderlyingType(*modified), element.get());
}

TEST(NullableTypeTest, GetUnderlyingTypeReturnsOriginalForNonNullableType) {
    ITypePtr int32 = Int32();
    EXPECT_EQ(&GetUnderlyingType(*int32), int32.get());
    ITypePtr list = ListOf(Int32());
    EXPECT_EQ(&GetUnderlyingType(*list), list.get());
}

TEST(NullableTypeTest, GetUnderlyingTypeReturnsOriginalPreservingModifiersForModifiedNonNullable) {
    // modopt(IsConst) int -- not a nullable, so GetUnderlyingType returns the ORIGINAL
    // (the ModifiedType, modifiers preserved), NOT SkipModifiers(int).
    ITypePtr modified = ModOptOf(IsConstModifier(), Int32());
    EXPECT_EQ(&GetUnderlyingType(*modified), modified.get());
}

// ---------------------------------------------------------------------------
// IsNonNullableValueType -- IsReferenceType == false && !IsNullable.
// ---------------------------------------------------------------------------

TEST(NullableTypeTest, IsNonNullableValueTypeTrueForValueType) {
    // Int32: IsReferenceType == false, not nullable.
    EXPECT_TRUE(IsNonNullableValueType(*Int32()));
}

TEST(NullableTypeTest, IsNonNullableValueTypeTrueForModifiedValueType) {
    // modopt(IsConst) int: IsReferenceType delegates to the element (false), not nullable.
    EXPECT_TRUE(IsNonNullableValueType(*ModOptOf(IsConstModifier(), Int32())));
}

TEST(NullableTypeTest, IsNonNullableValueTypeFalseForReferenceType) {
    // String: IsReferenceType == true, so the == false guard rejects it.
    EXPECT_FALSE(IsNonNullableValueType(*String()));
}

TEST(NullableTypeTest, IsNonNullableValueTypeFalseForNullableValueType) {
    // Nullable<int> IS nullable, so !IsNullable is false.
    EXPECT_FALSE(IsNonNullableValueType(*NullableOf(Int32())));
}

TEST(NullableTypeTest, IsNonNullableValueTypeFalseForIndeterminateType) {
    // UnknownType: IsReferenceType == nullopt (indeterminate), so `== false` is false.
    EXPECT_FALSE(IsNonNullableValueType(*UnknownType()));
}
