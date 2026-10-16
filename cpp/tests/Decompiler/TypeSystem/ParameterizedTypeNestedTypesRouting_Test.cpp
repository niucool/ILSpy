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

// Tests for the `ParameterizedType.GetNestedTypes` ROUTING arm (D494) -- the `else` branch of each
// `ParameterizedType.cs` `GetNestedTypes` override: `GetMembersHelper.GetNestedTypes(this, ...)`,
// which builds parameterized nested types. The D492 leaf ported the `ReturnMemberDefinitions` arm
// (delegate to the generic type); this leaf ports the routing arm. KEY: unlike the member families
// (which return non-owning `const T*` and so cache the owning `Specialized*` in `mutable` members),
// `GetNestedTypes` returns OWNING `ITypePtr` (shared_ptr) -- the routing arm needs NO owning cache:
// `GetMembersHelper.GetNestedTypes` returns `std::vector<ITypePtr>` directly, which is exactly what
// the override returns.
//
// The tests pin:
//  (a) `GetNestedTypes(IgnoreInheritedMembers)` (no `ReturnMemberDefinitions`) over a
//      `ParameterizedType` outer with a non-generic nested -> the unspecialized definition (the
//      routing arm fires);
//  (b) a `ParameterizedType` outer with a GENERIC nested -> a `ParameterizedType` over the nested
//      with the outer's type arguments + `UnboundTypeArgument` for the nested's own params;
//  (c) the typeArguments overload routes too (`pt->GetNestedTypes(args, filter, IgnoreInherited)`);
//  (d) the `ITypeDefinition` filter is applied at return time;
//  (e) the `ReturnMemberDefinitions` arm still delegates (D492 unchanged) -- the nested type is
//      NOT parameterized when `ReturnMemberDefinitions` is set.

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A minimal `ITypeParameter` stub for the outer type's type parameters (a class type param at index i).
class TestTypeParameter : public ITypeParameter {
public:
    explicit TestTypeParameter(std::string name, int index)
        : name_(std::move(name)), index_(index) {}
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    ITypePtr AcceptVisitor(::ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    ::ILSpy::Decompiler::TypeSystem::VarianceModifier Variance() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::VarianceModifier::Invariant;
    }
    ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    std::vector<::ILSpy::Decompiler::TypeSystem::TypeConstraint> TypeConstraints() const override
    {
        return {};
    }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::string name_;
    int index_;
};

// A `TestTypeDefinition : LookupTypeDefinition` with a configurable `NestedTypes()` + `TypeParameters()`.
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, int typeParamCount, const ICompilation& compilation)
        : LookupTypeDefinition(name, "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName(
                                       "", name, typeParamCount)),
                               TypeKind::Class, Accessibility::Public, compilation, nullptr) {}

    void SetNestedTypes(std::vector<const ITypeDefinition*> n) { nested_ = std::move(n); }
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }

    std::vector<const ITypeDefinition*> NestedTypes() const override { return nested_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }

    // The D492 `ReturnMemberDefinitions` arm delegates to `genericType->GetNestedTypes(filter,
    // options)`; the stub returns the configured nested definitions as `ITypePtr` (an aliasing cast
    // the outer definition's ownership backs) -- standing in for `MetadataTypeDefinition.GetNestedTypes`.
    std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override {
        (void)options;
        std::vector<ITypePtr> out;
        for (const ITypeDefinition* n : nested_) {
            if (!filter || filter(n)) {
                out.push_back(std::const_pointer_cast<IType>(n->shared_from_this()));
            }
        }
        return out;
    }
    std::vector<ITypePtr> GetNestedTypes(
        const std::vector<ITypePtr>& /*typeArguments*/,
        std::function<bool(const ITypeDefinition*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override {
        (void)options;
        std::vector<ITypePtr> out;
        for (const ITypeDefinition* n : nested_) {
            if (!filter || filter(n)) {
                out.push_back(std::const_pointer_cast<IType>(n->shared_from_this()));
            }
        }
        return out;
    }

private:
    std::vector<const ITypeDefinition*> nested_;
    std::vector<const ITypeParameter*> typeParameters_;
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const auto kIgnoreInherited = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers;
const auto kReturnDefs = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions;

std::shared_ptr<TestTypeDefinition> MakeNested(std::string name, int tpc) {
    return std::make_shared<TestTypeDefinition>(std::move(name), tpc, Compilation());
}

} // namespace

// ---------------------------------------------------------------------------
// GetNestedTypes (routing arm): IgnoreInheritedMembers over a ParameterizedType outer with a
// non-generic nested -> the unspecialized definition (the routing arm fires).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesRoutingTest, NonGenericNestedYieldsDefinition) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner = MakeNested("Inner", 0);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = pt->GetNestedTypes(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), inner.get());
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(result[0].get()), nullptr);
}

// ---------------------------------------------------------------------------
// GetNestedTypes (routing arm): a ParameterizedType outer with a GENERIC nested -> a
// ParameterizedType over the nested with the outer's type arguments + UnboundTypeArgument for the
// nested's own params.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesRoutingTest, GenericNestedBuildsParameterized) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner = MakeNested("Inner`2", 2);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = pt->GetNestedTypes(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    auto* nestedPt = dynamic_cast<const ParameterizedType*>(result[0].get());
    ASSERT_NE(nestedPt, nullptr);
    EXPECT_EQ(nestedPt->GetDefinition(), inner.get());
    ASSERT_EQ(nestedPt->TypeArguments().size(), 2u);
    EXPECT_EQ(nestedPt->TypeArguments()[0]->Name(), "String");
    EXPECT_EQ(nestedPt->TypeArguments()[1]->Kind(), TypeKind::UnboundTypeArgument);
}

// ---------------------------------------------------------------------------
// GetNestedTypes (routing arm): the typeArguments overload routes too.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesRoutingTest, TypeArgsOverloadRoutes) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner0 = MakeNested("Inner0", 1);
    auto inner1 = MakeNested("Inner1`2", 2);
    outer->SetNestedTypes({inner0.get(), inner1.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    std::vector<ITypePtr> oneArg{Int32()};
    auto result = pt->GetNestedTypes(oneArg, nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), inner1.get());
}

// ---------------------------------------------------------------------------
// GetNestedTypes (routing arm): the ITypeDefinition filter is applied at return time.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesRoutingTest, AppliesFilter) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 0, Compilation());
    auto keep = MakeNested("Keep", 0);
    auto drop = MakeNested("Drop", 0);
    outer->SetNestedTypes({keep.get(), drop.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = pt->GetNestedTypes(
        [](const ITypeDefinition* d) { return d->Name() == "Keep"; }, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), keep.get());
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the ReturnMemberDefinitions arm still delegates (D492 unchanged) -- the nested
// type is NOT parameterized when ReturnMemberDefinitions is set, even though it has type parameters.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesRoutingTest, ReturnDefsStillDelegates) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner = MakeNested("Inner`1", 1);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = pt->GetNestedTypes(nullptr, kReturnDefs | kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(result[0].get()), nullptr);
    EXPECT_EQ(result[0]->GetDefinition(), inner.get());
}
