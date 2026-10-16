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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `IType::TypeParameters` (D481) -- the `IType` member the
// `SpecializedMember.DeclaringType` `else` arm reads
// (`new ParameterizedType(definitionDeclaringTypeDef, definitionDeclaringTypeDef
// .TypeParameters).AcceptVisitor(substitution)`). The C# `IType.TypeParameters` ("Gets
// the type parameters. Returns an empty list if this type is not generic.") has the
// `AbstractType` default `EmptyList<ITypeParameter>.Instance`; the port flattens that
// default onto `IType` as a virtual-WITH-DEFAULT returning an empty
// `std::vector<const ITypeParameter*>` (the D406 convention; non-owning raw pointers, the
// `NestedTypes` / `GetMethods` "type system owns the entities, caller holds raw pointers"
// convention). A concrete `ITypeDefinition` (the `MetadataTypeDefinition`, not yet
// ported) overrides it to return its real type parameters.
//
// The tests pin:
//  (a) the default returns empty for the existing concrete `IType` subclasses that do
//      NOT override it (`KnownType`, `SpecialType`, `ArrayType`, `ParameterizedType`,
//      `ModifiedType`, `NullabilityAnnotatedType`, `UnknownType`);
//  (b) `ParameterizedType` returns empty -- `TypeParameters` (the formal type
//      parameters) is DISTINCT from `TypeArguments` (the type arguments passed in; the
//      port's `ParameterizedType::TypeArguments()` accessor is the args, NOT the formal
//      params), the C# `ParameterizedType.TypeParameters => genericType.TypeParameters`
//      deferred until the `ITypeDefinition`-with-real-params lands;
//  (c) `DummyTypeParameter` (a real `ITypeParameter`, which IS an `IType`) returns empty
//      -- a type parameter has no type parameters of its own;
//  (d) the dispatch: a stub `IType` overriding `TypeParameters()` returns its configured
//      list, called through an `IType&` base reference;
//  (e) the returned pointers are the configured `ITypeParameter*` instances (identity);
//  (f) `TypeParameterCount()` and `TypeParameters().size()` agree on the stub (the
//      faithful relationship a real `ITypeDefinition` maintains).

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::Implementation::DummyTypeParameter;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A custom-modifier type (System.Runtime.CompilerServices.IsConst), the `modifier` arg of
// `ModifiedType` (the `IsReferenceType_Test` convention).
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

// A minimal concrete `IType` overriding `TypeParameters()` -- the dispatch stub. The 5
// `IType` pure-virtuals (`Kind` / `Name` / `ReflectionName` / `TypeParameterCount` /
// `StructuralEquals`) are implemented trivially; `TypeParameters()` returns the
// configured list. Constructed via `make_shared` so the inherited `shared_from_this`
// -based defaults (`VisitChildren` / `ChangeNullability`) are sound.
class TestType : public IType {
public:
    explicit TestType(std::vector<const ITypeParameter*> typeParameters = {},
                      std::string name = "TestType")
        : typeParameters_(std::move(typeParameters)), name_(std::move(name)) {}

    TypeKind Kind() const override { return TypeKind::Class; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override {
        return static_cast<int>(typeParameters_.size());
    }
    std::vector<const ITypeParameter*> TypeParameters() const override {
        return typeParameters_;
    }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::vector<const ITypeParameter*> typeParameters_;
    std::string name_;
};

} // namespace

// ---------------------------------------------------------------------------
// The default returns empty for concrete IType subclasses that do not override it.
// ---------------------------------------------------------------------------
TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForKnownType)
{
    auto kt = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_TRUE(kt->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForSpecialType)
{
    auto st = std::make_shared<SpecialType>(TypeKind::Dynamic);
    EXPECT_TRUE(st->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForArrayType)
{
    auto arr = std::make_shared<ArrayType>(Int32());
    EXPECT_TRUE(arr->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForParameterizedType)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    EXPECT_TRUE(pt->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForModifiedType)
{
    auto mt = std::make_shared<ModifiedType>(IsConstModifier(), Int32(), /*isRequired*/ false);
    EXPECT_TRUE(mt->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForNullabilityAnnotatedType)
{
    auto nat = std::make_shared<NullabilityAnnotatedType>(Object(), Nullability::NotNullable);
    EXPECT_TRUE(nat->TypeParameters().empty());
}

TEST(ITypeTypeParametersTest, DefaultReturnsEmptyForUnknownType)
{
    auto ut = UnknownType();
    EXPECT_TRUE(ut->TypeParameters().empty());
}

// ---------------------------------------------------------------------------
// DummyTypeParameter (a real ITypeParameter, which IS an IType) returns empty -- a type
// parameter has no type parameters of its own.
// ---------------------------------------------------------------------------
TEST(ITypeTypeParametersTest, DummyTypeParameterReturnsEmpty)
{
    auto tp = DummyTypeParameter::GetClassTypeParameter(0);
    ASSERT_NE(tp, nullptr);
    // Through the IType base surface (DummyTypeParameter : ITypeParameter : IType).
    IType* asIType = tp.get();
    EXPECT_TRUE(asIType->TypeParameters().empty());
}

// ---------------------------------------------------------------------------
// The dispatch: a stub IType overriding TypeParameters() returns its configured list,
// called through an IType& base reference.
// ---------------------------------------------------------------------------
TEST(ITypeTypeParametersTest, DispatchReturnsConfiguredList)
{
    auto tp0 = DummyTypeParameter::GetClassTypeParameter(0);
    auto tp1 = DummyTypeParameter::GetClassTypeParameter(1);
    ASSERT_NE(tp0, nullptr);
    ASSERT_NE(tp1, nullptr);
    const ITypeParameter* params[] = {tp0.get(), tp1.get()};
    auto t = std::make_shared<TestType>(
        std::vector<const ITypeParameter*>{params[0], params[1]}, "Generic<T, U>");
    // Through the IType base reference (virtual dispatch).
    IType& asBase = *t;
    auto result = asBase.TypeParameters();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], params[0]);
    EXPECT_EQ(result[1], params[1]);
}

// ---------------------------------------------------------------------------
// A stub with no configured type parameters returns empty (the override delegates to
// the configured list, which is empty by default).
// ---------------------------------------------------------------------------
TEST(ITypeTypeParametersTest, StubWithNoTypeParametersReturnsEmpty)
{
    auto t = std::make_shared<TestType>(); // empty list
    EXPECT_TRUE(t->TypeParameters().empty());
}

// ---------------------------------------------------------------------------
// TypeParameterCount() and TypeParameters().size() agree on the stub (the faithful
// relationship a real ITypeDefinition maintains).
// ---------------------------------------------------------------------------
TEST(ITypeTypeParametersTest, TypeParameterCountAgreesWithTypeParametersSize)
{
    auto tp0 = DummyTypeParameter::GetClassTypeParameter(0);
    auto tp1 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto tp2 = DummyTypeParameter::GetMethodTypeParameter(1);
    auto t = std::make_shared<TestType>(
        std::vector<const ITypeParameter*>{tp0.get(), tp1.get(), tp2.get()}, "Generic");
    EXPECT_EQ(t->TypeParameterCount(), 3);
    ASSERT_EQ(t->TypeParameters().size(), 3u);
    EXPECT_EQ(static_cast<int>(t->TypeParameters().size()), t->TypeParameterCount());
}
