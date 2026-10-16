// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
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

// Tests for `GetMembersHelper.GetNestedTypes` (D493) -- the routing that builds parameterized nested
// types for an `ITypeDefinition` / `ParameterizedType` base. The C# `GetNestedTypesImpl` enumerates
// the outer type's definition's `NestedTypes`; for each nested type it:
//  - skips if `nestedTypeArguments` is non-null and the nested's ADDITIONAL type-parameter count
//    (`nested.TypeParameterCount - outer.TypeParameterCount`) != `nestedTypeArguments.size()`;
//  - applies the `ITypeDefinition` filter;
//  - if the nested has no type parameters OR `ReturnMemberDefinitions` is set, yields the
//    unspecialized `ITypeDefinition` (an `ITypePtr`);
//  - else builds a `ParameterizedType` over the nested definition with the outer type arguments
//    (from the `ParameterizedType`'s `GetTypeArgument(i)` or the outer definition's
//    `TypeParameters[i]`) and the nested's OWN type parameters (beyond the outer's) from
//    `nestedTypeArguments` (or `UnboundTypeArgument` when the caller supplied none).
//
// The tests pin:
//  (a) `IgnoreInheritedMembers` -> the declared nested types only;
//  (b) a non-parameterized outer with a non-generic nested -> the unspecialized definition;
//  (c) a `ParameterizedType` outer with a non-generic nested -> the unspecialized definition
//      (the nested has no type parameters);
//  (d) a `ParameterizedType` outer with a GENERIC nested -> a `ParameterizedType` over the nested
//      with the outer's type arguments + `UnboundTypeArgument` for the nested's own params;
//  (e) `ReturnMemberDefinitions` over a `ParameterizedType` outer -> the unspecialized definitions;
//  (f) the `nestedTypeArguments` count filter (skips nested types whose additional type-parameter
//      count does not match);
//  (g) the `ITypeDefinition` filter (skips nested types the filter rejects).

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
#include "Decompiler/TypeSystem/Implementation/GetMembersHelper.hpp"

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
namespace GMH = ILSpy::Decompiler::TypeSystem::Implementation::GetMembersHelper;

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

// A `TestTypeDefinition : LookupTypeDefinition` with a configurable `NestedTypes()` + `TypeParameters()`
// (the nested type definitions the outer declares, and the outer's own type parameters). Overrides
// `NestedTypes()` and `TypeParameters()` to return the configured vectors.
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, int typeParamCount, const ICompilation& compilation)
        : LookupTypeDefinition(name, "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName(
                                       "", name, typeParamCount)),
                               TypeKind::Class, Accessibility::Public, compilation, nullptr),
          typeParamCount_(typeParamCount) {}

    void SetNestedTypes(std::vector<const ITypeDefinition*> n) { nested_ = std::move(n); }
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }

    std::vector<const ITypeDefinition*> NestedTypes() const override { return nested_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }

private:
    std::vector<const ITypeDefinition*> nested_;
    std::vector<const ITypeParameter*> typeParameters_;
    int typeParamCount_;
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const auto kIgnoreInherited = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers;
const auto kReturnDefs = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions;

// Make a nested-type definition named `name` with `tpc` type parameters, owned by a shared_ptr.
std::shared_ptr<TestTypeDefinition> MakeNested(std::string name, int tpc) {
    return std::make_shared<TestTypeDefinition>(std::move(name), tpc, Compilation());
}

} // namespace

// ---------------------------------------------------------------------------
// GetNestedTypes: IgnoreInheritedMembers -> the declared nested types (the unspecialized
// definitions for a non-parameterized outer).
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, IgnoreInheritedReturnsDeclared) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 0, Compilation());
    auto inner = MakeNested("Inner", 0);
    outer->SetNestedTypes({inner.get()});
    auto result = GMH::GetNestedTypes(outer.get(), nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), inner.get());
    // The unspecialized definition (no ParameterizedType -- the nested has no type parameters).
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(result[0].get()), nullptr);
}

// ---------------------------------------------------------------------------
// GetNestedTypes: a ParameterizedType outer with a non-generic nested -> the unspecialized
// definition (the nested has no type parameters, so no parameterization).
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, ParameterizedOuterNonGenericNestedYieldsDefinition) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner = MakeNested("Inner", 0);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = GMH::GetNestedTypes(pt.get(), nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), inner.get());
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(result[0].get()), nullptr);
}

// ---------------------------------------------------------------------------
// GetNestedTypes: a ParameterizedType outer with a GENERIC nested -> a ParameterizedType over the
// nested with the outer's type arguments + UnboundTypeArgument for the nested's own params.
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, ParameterizedOuterGenericNestedBuildsParameterized) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    // A nested type with 2 type parameters: 1 inherited from the outer + 1 of its own.
    auto inner = MakeNested("Inner`2", 2);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = GMH::GetNestedTypes(pt.get(), nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    auto* nestedPt = dynamic_cast<const ParameterizedType*>(result[0].get());
    ASSERT_NE(nestedPt, nullptr);
    EXPECT_EQ(nestedPt->GetDefinition(), inner.get());
    ASSERT_EQ(nestedPt->TypeArguments().size(), 2u);
    // The first type argument is the outer's type argument (String).
    EXPECT_EQ(nestedPt->TypeArguments()[0]->Name(), "String");
    // The second is UnboundTypeArgument (the nested's own param, not supplied by the caller).
    EXPECT_EQ(nestedPt->TypeArguments()[1]->Kind(), TypeKind::UnboundTypeArgument);
}

// ---------------------------------------------------------------------------
// GetNestedTypes: ReturnMemberDefinitions over a ParameterizedType outer -> the unspecialized
// definitions (the nested type is NOT parameterized even though it has type parameters).
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, ReturnMemberDefinitionsSkipsParameterization) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner = MakeNested("Inner`1", 1);
    outer->SetNestedTypes({inner.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    auto result = GMH::GetNestedTypes(pt.get(), nullptr, kReturnDefs | kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(result[0].get()), nullptr);
    EXPECT_EQ(result[0]->GetDefinition(), inner.get());
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the nestedTypeArguments count filter -- a non-null nestedTypeArguments with
// size 1 selects nested types whose ADDITIONAL type-parameter count is 1 (skips the count-0 nested).
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, NestedTypeArgumentsCountFilter) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    outer->SetTypeParameters({std::make_shared<TestTypeParameter>("T", 0).get()});
    auto inner0 = MakeNested("Inner0", 1);   // 1 tp, 0 additional -> filtered out by size-1 args
    auto inner1 = MakeNested("Inner1`2", 2); // 2 tp, 1 additional -> matches size-1 args
    outer->SetNestedTypes({inner0.get(), inner1.get()});
    auto pt = std::make_shared<ParameterizedType>(outer, std::vector<ITypePtr>{String()});
    std::vector<ITypePtr> oneArg{Int32()};
    auto result = GMH::GetNestedTypes(pt.get(), &oneArg, nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), inner1.get());
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the ITypeDefinition filter (skips nested types the filter rejects).
// ---------------------------------------------------------------------------
TEST(GetMembersHelperNestedTypesTest, DefinitionFilter) {
    auto outer = std::make_shared<TestTypeDefinition>("Outer", 0, Compilation());
    auto keep = MakeNested("Keep", 0);
    auto drop = MakeNested("Drop", 0);
    outer->SetNestedTypes({keep.get(), drop.get()});
    auto result = GMH::GetNestedTypes(
        outer.get(), [](const ITypeDefinition* d) { return d->Name() == "Keep"; }, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->GetDefinition(), keep.get());
}
