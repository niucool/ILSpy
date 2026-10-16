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
using ILSpy::Decompiler::TypeSystem::Create;
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

// A `LookupCompilation` with the `System.Nullable`1` definition registered for `FindType`,
// paired with the owning `shared_ptr` to that definition. The `Create` helper resolves the
// nullable definition through `compilation.FindType(KnownTypeCode.NullableOfT)`, so the
// `Create` tests need a compilation with the definition registered (the shared `Compilation()`
// used by the IsNullable/GetUnderlyingType tests is not registered).
//
// `LookupCompilation` is non-copyable and non-movable (its `LookupModule` member holds a
// `const ICompilation&` reference to `*this`), so the struct is HEAP-ALLOCATED via
// `make_unique` and returned by `unique_ptr`: the pointee is never moved, so the `mainModule_`
// reference stays valid for the test's scope.
struct RegisteredNullable {
	LookupCompilation compilation;
	std::shared_ptr<LookupTypeDefinition> def;
};
std::unique_ptr<RegisteredNullable> MakeRegisteredNullable() {
	auto r = std::make_unique<RegisteredNullable>();
	r->def = std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
		FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
		TypeKind::Struct, Accessibility::Public, r->compilation, nullptr,
		KnownTypeCode::NullableOfT);
	r->compilation.RegisterKnownType(KnownTypeCode::NullableOfT, r->def.get());
	return r;
}

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

// ---------------------------------------------------------------------------
// Create -- builds a `Nullable<T>` over the element type via the compilation's
// `Nullable`1` definition (the defensive `else` branch returns the `FindType` result
// itself when the definition cannot be resolved).
// ---------------------------------------------------------------------------

TEST(NullableTypeTest, CreateReturnsParameterizedTypeOverNullableDef) {
    auto r = MakeRegisteredNullable();
    ITypePtr result = Create(r->compilation, *Int32());
    ASSERT_NE(result, nullptr);
    // The `Nullable`1` definition is a struct, so the parameterized type is a struct with 1 arg.
    EXPECT_EQ(result->Kind(), TypeKind::Struct);
    EXPECT_EQ(result->TypeParameterCount(), 1);
    // GetDefinition() delegates to the generic type -> the registered `Nullable`1` definition.
    EXPECT_EQ(result->GetDefinition(), r->def.get());
}

TEST(NullableTypeTest, CreateStoresElementAsTypeArgument) {
    auto r = MakeRegisteredNullable();
    ITypePtr element = Int32();
    ITypePtr result = Create(r->compilation, *element);
    ASSERT_NE(result, nullptr);
    // `GetTypeArgument` is a `ParameterizedType` member (not on `IType`), so down-cast.
    auto* pt = dynamic_cast<ParameterizedType*>(result.get());
    ASSERT_NE(pt, nullptr);
    // The element is stored via `shared_from_this` (a co-owning handle), so the type argument is
    // the SAME instance as the element (no copy).
    EXPECT_EQ(pt->GetTypeArgument(0).get(), element.get());
}

TEST(NullableTypeTest, CreateResultIsNullable) {
    auto r = MakeRegisteredNullable();
    ITypePtr result = Create(r->compilation, *Int32());
    ASSERT_NE(result, nullptr);
    // Round-trip: the constructed type is recognized as `Nullable<T>` by `IsNullable`.
    EXPECT_TRUE(IsNullable(*result));
}

TEST(NullableTypeTest, CreateResultGetUnderlyingTypeReturnsElement) {
    auto r = MakeRegisteredNullable();
    ITypePtr element = Int32();
    ITypePtr result = Create(r->compilation, *element);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&GetUnderlyingType(*result), element.get());
}

TEST(NullableTypeTest, CreateReturnsFreshInstancePerCall) {
    auto r = MakeRegisteredNullable();
    ITypePtr element = Int32();
    ITypePtr a = Create(r->compilation, *element);
    ITypePtr b = Create(r->compilation, *element);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    auto* pa = dynamic_cast<ParameterizedType*>(a.get());
    auto* pb = dynamic_cast<ParameterizedType*>(b.get());
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pb, nullptr);
    // Each call allocates a fresh `ParameterizedType` (the C# `new ParameterizedType(...)`).
    EXPECT_NE(a.get(), b.get());
    // Both wrap the same element and are nullable.
    EXPECT_EQ(pa->GetTypeArgument(0).get(), element.get());
    EXPECT_EQ(pb->GetTypeArgument(0).get(), element.get());
    EXPECT_TRUE(IsNullable(*a));
    EXPECT_TRUE(IsNullable(*b));
}

TEST(NullableTypeTest, CreateReturnsFindTypeResultWhenDefinitionIsNull) {
    // The defensive `else` branch: `FindType` returns a non-definition (a `KnownType` placeholder,
    // NOT an `ITypeDefinition` -- `GetDefinition() == null`), so `Create` returns the `FindType`
    // result itself rather than building a `ParameterizedType`. The local compilation is not
    // moved (a stack object used in place), so its `mainModule_` reference stays valid.
    LookupCompilation comp;
    auto nonDef = std::make_shared<KnownType>(KnownTypeCode::NullableOfT);
    comp.RegisterKnownType(KnownTypeCode::NullableOfT, nonDef.get());
    ITypePtr result = Create(comp, *Int32());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result.get(), nonDef.get());
    // The `KnownType` placeholder is not a `ParameterizedType`, so `IsNullable` is false.
    EXPECT_FALSE(IsNullable(*result));
}
