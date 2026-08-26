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
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `MemberLookup.LookupIndexers` (D502) -- the public indexer lookup. The C#
// `LookupIndexers(ResolveResult targetResolveResult)`:
//  - `filter = p => p.IsIndexer && !p.IsExplicitInterfaceImplementation`;
//  - for each base type: `GetProperties(filter, IgnoreInheritedMembers)`, `AddMembers(..., treatAll:
//    true, ...)`, build a `LookupGroup` per base type if any;
//  - if `targetType.Kind == TypeParameter`: `RemoveInterfaceMembersHiddenByClassMembers`;
//  - remove hidden groups (`MethodsAreHidden || Methods.Count == 0`);
//  - return `MethodListWithDeclaringType[]` (one per group, `DeclaringType` + `Methods`).
//
// The tests pin:
//  (a) a type with no indexers -> empty;
//  (b) a type with an indexer -> one `MethodListWithDeclaringType` bucket (the indexer in `Methods`,
//      the `DeclaringType` is the base type);
//  (c) a type with a non-indexer property -> filtered out (empty);
//  (d) an explicit-interface-implementation indexer -> filtered out (empty).

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal `IProperty` stub (a property definition) with a configurable `IsIndexer` and
// `IsExplicitInterfaceImplementation`. The full `IProperty`/`IParameterizedMember`/`IMember`/`IEntity`
// surface is overridden.
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, bool isIndexer, bool isExplicit,
                 const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), isIndexer_(isIndexer),
          isExplicit_(isExplicit), compilation_(compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return isExplicit_; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }
    std::vector<const IParameter*> Parameters() const override { return {}; }
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    bool IsIndexer() const override { return isIndexer_; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    bool isIndexer_;
    bool isExplicit_;
    const ICompilation& compilation_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// A `TestTypeDefinition : LookupTypeDefinition` that holds a configurable properties vector and
// overrides `GetProperties` (applying the caller's filter, faithful to `MetadataTypeDefinition`).
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, const ICompilation& compilation)
        : LookupTypeDefinition(std::move(name), "",
              ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                  ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName("", name, 0)),
              TypeKind::Class,
              Accessibility::Public, compilation, nullptr) {}

    void SetProperties(std::vector<const IProperty*> p) { properties_ = std::move(p); }

    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        if (!filter) return properties_;
        std::vector<const IProperty*> out;
        for (const IProperty* p : properties_) if (filter(p)) out.push_back(p);
        return out;
    }

private:
    std::vector<const IProperty*> properties_;
};

std::shared_ptr<ResolveResult> MakeTarget(ITypePtr type) {
    return std::make_shared<TypeResolveResult>(std::move(type));
}

} // namespace

// ---------------------------------------------------------------------------
// LookupIndexers: a type with no indexers -> empty.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupIndexersTest, NoIndexersYieldsEmpty) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.LookupIndexers(*target);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// LookupIndexers: a type with an indexer -> one MethodListWithDeclaringType bucket.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupIndexersTest, IndexerYieldsOneBucket) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto indexer = std::make_shared<TestProperty>("Item", Int32(), /*isIndexer=*/true,
                                                  /*isExplicit=*/false, Compilation());
    def->SetProperties({indexer.get()});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.LookupIndexers(*target);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].DeclaringType().GetDefinition(), def.get());
    ASSERT_EQ(result[0].size(), 1u);  // MethodListWithDeclaringType inherits std::vector
    EXPECT_EQ(result[0][0], indexer.get());
}

// ---------------------------------------------------------------------------
// LookupIndexers: a non-indexer property is filtered out (the filter is IsIndexer).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupIndexersTest, NonIndexerPropertyFilteredOut) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto nonIndexer = std::make_shared<TestProperty>("Count", Int32(), /*isIndexer=*/false,
                                                    /*isExplicit=*/false, Compilation());
    def->SetProperties({nonIndexer.get()});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.LookupIndexers(*target);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// LookupIndexers: an explicit-interface-implementation indexer is filtered out.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupIndexersTest, ExplicitInterfaceIndexerFilteredOut) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto explicitIndexer = std::make_shared<TestProperty>("Item", Int32(), /*isIndexer=*/true,
                                                         /*isExplicit=*/true, Compilation());
    def->SetProperties({explicitIndexer.get()});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.LookupIndexers(*target);
    EXPECT_TRUE(result.empty());
}
