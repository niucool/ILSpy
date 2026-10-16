// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver simple-name lookup region
// (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 1462-1790 + the two CreateMemberLookup factories at
// lines 1887-1910).
//
// The load-bearing cruxes:
//  (a) The lookup ORDER: local variables (innermost block first), then the current
//      member's parameters, then the current method's type parameters -- all gated
//      on k == 0 (no type arguments);
//  (b) The per-current-type-definition cache: the second lookup for the same
//      identifier in the same mode does NOT re-run LookInCurrentType (the member
//      fetch count stays put) and returns a ShallowClone (a fresh result object
//      over the same resolved entity); a MISSING name is cached as the known
//      negative (an empty handle) and the second lookup skips the fetch too;
//  (c) The using-scope chain: the scope's own namespace first, then the parent
//      scopes; the per-scope ResolveCache memoizes the k == 0 non-using-declaration
//      lookups (the stored result is observable through the public ResolveCache
//      member); the imported namespaces' types resolve with the
//      first-accessible-wins + second-accessible-is-ambiguous reduction;
//  (d) The parameterizeResultType gate: type arguments parameterize the result
//      UNLESS they are all UnboundTypeArgument placeholders;
//  (e) The accessibility gate: an Internal type from a non-friend module is
//      skipped (the lookup falls through), from a friend module accepted;
//  (f) The `dynamic` keyword and the UnknownIdentifierResolveResult fallback;
//  (g) CreateMemberLookup: the enum-member-initializer flag (a field member inside
//      an enum type) and the BaseTypeReference mode's DeclaringTypeDefinition swap
//      (observable through IsAccessible: a private member ON the current type is
//      accessible through the normal factory but NOT through the BaseTypeReference
//      factory, which starts the walk at the outer type);
//  (h) ResolveAlias: `global` yields the compilation's root namespace; anything
//      else (the port tracks no aliases) yields the ErrorResult singleton.

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/LocalResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::NameLookupMode;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::CSharp::Resolver::VariableMap;
using ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::LocalResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnboundTypeArgument;

// A configurable `INamespace`: child namespaces and type definitions keyed by
// name, with call counters so the cache-memoization cruxes can pin that the
// second lookup does not re-consult the namespace (the D533 MethodHostType
// call-count convention).
class ConfigurableNamespace : public INamespace {
public:
    ConfigurableNamespace(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddChildNamespace(const INamespace* child) { children_.push_back(child); }
    void AddTypeDefinition(const ITypeDefinition* def) { types_.push_back(def); }
    int GetTypeDefinitionCallCount() const { return getTypeDefinitionCallCount_; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return children_; }
    std::vector<const ITypeDefinition*> Types() const override { return types_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules() const override
    {
        return {};
    }
    const INamespace* GetChildNamespace(const std::string& name) const override
    {
        for (const INamespace* child : children_) {
            if (child->Name() == name)
                return child;
        }
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string& name,
                                             int typeParameterCount) const override
    {
        ++getTypeDefinitionCallCount_;
        (void)typeParameterCount;
        for (const ITypeDefinition* def : types_) {
            if (def->Name() == name)
                return def;
        }
        return nullptr;
    }

private:
    std::string name_;
    const ICompilation& compilation_;
    std::vector<const INamespace*> children_;
    std::vector<const ITypeDefinition*> types_;
    mutable int getTypeDefinitionCallCount_ = 0;
};

// A `LookupTypeDefinition` subclass whose `GetMethods` returns a configured list
// (the D533 MethodHostType precedent); the call count records every fetch (the
// composed `GetMembers` calls through to `GetMethods`), so the
// TypeDefinitionCache memoization crux can pin that the second lookup does not
// re-run `LookInCurrentType`.
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    int GetMethodsCallCount() const { return getMethodsCallCount_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        ++getMethodsCallCount_;
        (void)options;
        if (!filter)
            return methods_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> methods_;
    mutable int getMethodsCallCount_ = 0;
};

// A minimal `IParameter` with a configurable name and type (the D524 TestParameter
// precedent; the name is configurable here because the parameter arm matches on
// it).
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    {
        return nullptr;
    }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
};

// A minimal `IField` (the GetMembersHelper TestField precedent) -- the
// enum-member-initializer flag's `CurrentMember.SymbolKind == Field` check needs a
// field-kind member.
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation)
    {
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
};

// A minimal `IVariable` (the CSharpResolverSkeleton TestVariable precedent).
class TestVariable : public ILSpy::Decompiler::TypeSystem::IVariable {
public:
    TestVariable(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }

private:
    std::string name_;
    ITypePtr type_;
};

// A fresh per-test compilation (the namespace/type stubs are per-test state, so a
// shared compilation would leak lookups across tests -- the iteration-85
// module-table learning).
struct Fixture {
    std::unique_ptr<LookupCompilation> compilation = std::make_unique<LookupCompilation>();
};

Fixture& Fix() { static Fixture fixture; return fixture; }

LookupCompilation& Compilation() { return *Fix().compilation; }

std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                              TypeKind kind = TypeKind::Class,
                                              int typeParameterCount = 0)
{
    return std::make_shared<LookupTypeDefinition>(
        name, "", FullTypeName(TopLevelTypeName("", name, typeParameterCount)), kind,
        Accessibility::Public, Compilation(), nullptr);
}

std::shared_ptr<MethodHostType> MakeHost(const std::string& name,
                                         TypeKind kind = TypeKind::Class)
{
    return std::make_shared<MethodHostType>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind, Accessibility::Public,
        Compilation(), nullptr);
}

std::shared_ptr<CSharpResolver> MakeResolver()
{
    return std::make_shared<CSharpResolver>(Compilation());
}

// A using scope over the given namespace (a fresh context over the compilation's
// main module; the usings default to empty).
std::shared_ptr<UsingScope> MakeScope(const INamespace* ns,
                                      std::vector<const INamespace*> usings = {})
{
    auto context = std::make_shared<CSharpTypeResolveContext>(Compilation().MainModule());
    return std::make_shared<UsingScope>(std::move(context), *ns, std::move(usings));
}

ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

} // namespace

// --- The local-variable / parameter / type-parameter arms ------------------------------------

TEST(CSharpResolverSimpleNameTest, LocalVariableWins)
{
    auto resolver = MakeResolver();
    auto variable = std::make_shared<TestVariable>("x", Int32Type());
    auto map = std::make_shared<VariableMap>();
    map->emplace("x", variable);
    resolver = resolver->AddVariables(map);

    auto result = resolver->ResolveSimpleName("x", {});
    auto local = dynamic_cast<const LocalResolveResult*>(result.get());
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->Variable(), variable.get());  // pointer identity
    EXPECT_EQ(&result->Type(), variable->Type().shared_from_this().get());
    EXPECT_FALSE(result->IsError());
}

TEST(CSharpResolverSimpleNameTest, InnermostBlockLocalVariableWins)
{
    auto resolver = MakeResolver();
    auto outer = std::make_shared<TestVariable>("x", Int32Type());
    auto inner = std::make_shared<TestVariable>("x", Int32Type());
    auto outerMap = std::make_shared<VariableMap>();
    outerMap->emplace("x", outer);
    auto innerMap = std::make_shared<VariableMap>();
    innerMap->emplace("x", inner);
    resolver = resolver->AddVariables(outerMap)->AddVariables(innerMap);

    auto result = resolver->ResolveSimpleName("x", {});
    auto local = dynamic_cast<const LocalResolveResult*>(result.get());
    ASSERT_NE(local, nullptr);
    // The ImmutableStack enumerates LIFO, so the innermost block's dictionary is
    // consulted first.
    EXPECT_EQ(local->Variable(), inner.get());
}

TEST(CSharpResolverSimpleNameTest, ParameterOfCurrentMemberWins)
{
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto parameter = std::make_shared<TestParameter>("p", Int32Type());
    method->SetParameters({parameter.get()});
    auto resolver = MakeResolver()->WithCurrentMember(method.get());

    auto result = resolver->ResolveSimpleName("p", {});
    auto local = dynamic_cast<const LocalResolveResult*>(result.get());
    ASSERT_NE(local, nullptr);
    EXPECT_TRUE(local->IsParameter());
    EXPECT_EQ(local->Variable(), parameter.get());  // pointer identity
}

TEST(CSharpResolverSimpleNameTest, TypeParameterOfCurrentMethodWins)
{
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto tp = std::make_shared<LookupTypeParameter>("T");
    method->SetTypeParameters({tp.get()});
    auto resolver = MakeResolver()->WithCurrentMember(method.get());

    auto result = resolver->ResolveSimpleName("T", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), tp.get());  // pointer identity
}

TEST(CSharpResolverSimpleNameTest, TypeParameterOfCurrentTypeWins)
{
    auto def = MakeDef("C");
    auto tp = std::make_shared<LookupTypeParameter>("T");
    def->SetTypeParameters({tp.get()});
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(def.get());

    auto result = resolver->LookupSimpleNameOrTypeName("T", {}, NameLookupMode::Type);
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), tp.get());  // pointer identity
}

TEST(CSharpResolverSimpleNameTest, TypeArgumentsSkipTheVariableAndTypeParameterArms)
{
    // k != 0 skips the local-variable / parameter / type-parameter arms entirely,
    // so "T" (a method type parameter) with a type argument falls through to the
    // UnknownIdentifier fallback.
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto tp = std::make_shared<LookupTypeParameter>("T");
    method->SetTypeParameters({tp.get()});
    auto resolver = MakeResolver()->WithCurrentMember(method.get());

    auto result = resolver->ResolveSimpleName("T", {Int32Type()});
    auto unknown = dynamic_cast<const UnknownIdentifierResolveResult*>(result.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Identifier(), "T");
    EXPECT_EQ(unknown->TypeArgumentCount(), 1);
    EXPECT_TRUE(result->IsError());
}

// --- The per-current-type-definition cache ---------------------------------------------------

TEST(CSharpResolverSimpleNameTest, TypeDefinitionCacheMemoizesAndClones)
{
    auto def = MakeHost("C");
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    def->SetMethods({method.get()});
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(def.get());

    auto first = resolver->ResolveSimpleName("M", {});
    ASSERT_NE(dynamic_cast<const MethodGroupResolveResult*>(first.get()), nullptr);
    const int fetchesAfterFirst = def->GetMethodsCallCount();
    ASSERT_GE(fetchesAfterFirst, 1);  // the first lookup fetched the members

    auto second = resolver->ResolveSimpleName("M", {});
    ASSERT_NE(dynamic_cast<const MethodGroupResolveResult*>(second.get()), nullptr);
    // The cache hit short-circuits LookInCurrentType: no additional member fetch.
    EXPECT_EQ(def->GetMethodsCallCount(), fetchesAfterFirst);
    // The cached result is a ShallowClone: a fresh result object (not the same
    // instance) over the same resolved entity.
    EXPECT_NE(second.get(), first.get());
}

TEST(CSharpResolverSimpleNameTest, TypeDefinitionCacheStoresTheKnownNegative)
{
    auto def = MakeHost("C");
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(def.get());

    auto first = resolver->ResolveSimpleName("NoSuchMember", {});
    ASSERT_NE(dynamic_cast<const UnknownIdentifierResolveResult*>(first.get()), nullptr);
    const int fetchesAfterFirst = def->GetMethodsCallCount();
    ASSERT_GE(fetchesAfterFirst, 1);

    // The miss was cached as the known negative (an empty handle), so the second
    // lookup skips LookInCurrentType too.
    auto second = resolver->ResolveSimpleName("NoSuchMember", {});
    ASSERT_NE(dynamic_cast<const UnknownIdentifierResolveResult*>(second.get()), nullptr);
    EXPECT_EQ(def->GetMethodsCallCount(), fetchesAfterFirst);
}

// --- The using-scope chain -------------------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, ScopeNamespaceTypeWins)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto foo = MakeDef("Foo");
    ns->AddTypeDefinition(foo.get());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), foo.get());  // pointer identity
    EXPECT_FALSE(result->IsError());
}

TEST(CSharpResolverSimpleNameTest, ScopeResolveCacheMemoizesTheLookup)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto foo = MakeDef("Foo");
    ns->AddTypeDefinition(foo.get());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto first = resolver->ResolveSimpleName("Foo", {});
    ASSERT_NE(dynamic_cast<const TypeResolveResult*>(first.get()), nullptr);
    const int fetchesAfterFirst = ns->GetTypeDefinitionCallCount();
    ASSERT_GE(fetchesAfterFirst, 1);

    // The per-scope ResolveCache stored the result (observable through the public
    // member) and the second lookup is served from it without re-consulting the
    // namespace.
    std::shared_ptr<ResolveResult> cached;
    EXPECT_TRUE(scope->ResolveCache.TryGetValue("Foo", cached));
    ASSERT_NE(cached, nullptr);
    auto second = resolver->ResolveSimpleName("Foo", {});
    ASSERT_NE(dynamic_cast<const TypeResolveResult*>(second.get()), nullptr);
    EXPECT_EQ(ns->GetTypeDefinitionCallCount(), fetchesAfterFirst);
    // The cache hit returns a ShallowClone (a fresh instance over the same type).
    EXPECT_NE(second.get(), first.get());
    EXPECT_EQ(&second->Type(), foo.get());
}

TEST(CSharpResolverSimpleNameTest, ScopeResolveCacheStoresTheKnownNegative)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto first = resolver->ResolveSimpleName("NoSuch", {});
    ASSERT_NE(dynamic_cast<const UnknownIdentifierResolveResult*>(first.get()), nullptr);
    const int fetchesAfterFirst = ns->GetTypeDefinitionCallCount();
    ASSERT_GE(fetchesAfterFirst, 1);

    // The miss was cached as the known negative; the second lookup does not
    // re-consult the namespace.
    std::shared_ptr<ResolveResult> cached;
    EXPECT_TRUE(scope->ResolveCache.TryGetValue("NoSuch", cached));
    EXPECT_EQ(cached, nullptr);
    auto second = resolver->ResolveSimpleName("NoSuch", {});
    ASSERT_NE(dynamic_cast<const UnknownIdentifierResolveResult*>(second.get()), nullptr);
    EXPECT_EQ(ns->GetTypeDefinitionCallCount(), fetchesAfterFirst);
}

TEST(CSharpResolverSimpleNameTest, ParentScopeIsWalkedWhenTheInnerScopeMisses)
{
    auto innerNs = std::make_shared<ConfigurableNamespace>("N1", Compilation());
    auto outerNs = std::make_shared<ConfigurableNamespace>("N2", Compilation());
    auto foo = MakeDef("Foo");
    outerNs->AddTypeDefinition(foo.get());

    // The child scope's parent context carries the outer scope, so the child's
    // Parent() is the outer scope (the context-chain shape).
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(Compilation().MainModule());
    auto outerScope = std::make_shared<UsingScope>(outerContext, *outerNs, std::vector<const INamespace*>{});
    auto childContext = outerContext->WithUsingScope(outerScope);
    auto childScope = std::make_shared<UsingScope>(childContext, *innerNs, std::vector<const INamespace*>{});
    auto resolver = MakeResolver()->WithCurrentUsingScope(childScope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), foo.get());
}

TEST(CSharpResolverSimpleNameTest, ChildNamespaceOfTheScopeNamespaceWins)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto child = std::make_shared<ConfigurableNamespace>("Sub", Compilation());
    ns->AddChildNamespace(child.get());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Sub", {});
    auto nsResult = dynamic_cast<const NamespaceResolveResult*>(result.get());
    ASSERT_NE(nsResult, nullptr);
    EXPECT_EQ(nsResult->Namespace(), child.get());  // pointer identity
}

TEST(CSharpResolverSimpleNameTest, ImportedNamespaceTypeResolves)
{
    auto ownNs = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto importedNs = std::make_shared<ConfigurableNamespace>("I", Compilation());
    auto foo = MakeDef("Foo");
    importedNs->AddTypeDefinition(foo.get());
    auto scope = MakeScope(ownNs.get(), {importedNs.get()});
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), foo.get());
}

TEST(CSharpResolverSimpleNameTest, TwoImportedNamespacesWithTheSameTypeAreAmbiguous)
{
    auto ownNs = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto ns1 = std::make_shared<ConfigurableNamespace>("I1", Compilation());
    auto ns2 = std::make_shared<ConfigurableNamespace>("I2", Compilation());
    auto foo1 = MakeDef("Foo");
    auto foo2 = MakeDef("Foo");
    ns1->AddTypeDefinition(foo1.get());
    ns2->AddTypeDefinition(foo2.get());
    auto scope = MakeScope(ownNs.get(), {ns1.get(), ns2.get()});
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    // The second accessible type in a different imported namespace makes the
    // resolution ambiguous (an error result over the FIRST type).
    auto ambiguous = dynamic_cast<const AmbiguousTypeResolveResult*>(result.get());
    ASSERT_NE(ambiguous, nullptr);
    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(&result->Type(), foo1.get());
}

TEST(CSharpResolverSimpleNameTest, InaccessibleFirstImportedResultIsReplaced)
{
    auto ownNs = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto ns1 = std::make_shared<ConfigurableNamespace>("I1", Compilation());
    auto ns2 = std::make_shared<ConfigurableNamespace>("I2", Compilation());
    // An Internal type whose parent module does not grant internals to the main
    // module (a fresh non-friend module).
    auto friendlessModule = std::make_unique<LookupModule>(Compilation(), "Friendless");
    auto internalFoo = std::make_shared<LookupTypeDefinition>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Internal, Compilation(), friendlessModule.get());
    auto publicFoo = MakeDef("Foo");
    ns1->AddTypeDefinition(internalFoo.get());
    ns2->AddTypeDefinition(publicFoo.get());
    auto scope = MakeScope(ownNs.get(), {ns1.get(), ns2.get()});
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    // The inaccessible first result is replaced by the accessible second one.
    EXPECT_EQ(&result->Type(), publicFoo.get());
}

TEST(CSharpResolverSimpleNameTest, InternalTypeFromAFriendModuleIsAccessible)
{
    auto ownNs = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto friendModule = std::make_unique<LookupModule>(Compilation(), "Friend");
    // The main module's assembly name is "LookupTests"; the friend module grants
    // internals to it.
    friendModule->AddFriendAssembly("LookupTests");
    auto internalFoo = std::make_shared<LookupTypeDefinition>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Internal, Compilation(), friendModule.get());
    ownNs->AddTypeDefinition(internalFoo.get());
    auto scope = MakeScope(ownNs.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->ResolveSimpleName("Foo", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(&result->Type(), internalFoo.get());
}

// --- The parameterizeResultType gate ---------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, TypeArgumentsParameterizeTheResultType)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto foo = MakeDef("Foo");
    ns->AddTypeDefinition(foo.get());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    auto result = resolver->LookupSimpleNameOrTypeName("Foo", {Int32Type()}, NameLookupMode::Type);
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    auto parameterized = dynamic_cast<const ParameterizedType*>(&result->Type());
    ASSERT_NE(parameterized, nullptr);
    EXPECT_EQ(parameterized->GetDefinition(), foo.get());
    ASSERT_EQ(parameterized->TypeArguments().size(), 1u);
    // The type argument is the SAME instance the caller passed (the vector is
    // forwarded through); a fresh `Int32Type()` would be a distinct instance
    // (KnownType equality is value-based, but the pointer identity here pins the
    // forwarding).
    auto int32 = Int32Type();
    auto second = resolver->LookupSimpleNameOrTypeName("Foo", {int32}, NameLookupMode::Type);
    auto secondParameterized = dynamic_cast<const ParameterizedType*>(&second->Type());
    ASSERT_NE(secondParameterized, nullptr);
    ASSERT_EQ(secondParameterized->TypeArguments().size(), 1u);
    EXPECT_EQ(secondParameterized->TypeArguments()[0].get(), int32.get());
}

TEST(CSharpResolverSimpleNameTest, AllUnboundTypeArgumentsDoNotParameterize)
{
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto foo = MakeDef("Foo");
    ns->AddTypeDefinition(foo.get());
    auto scope = MakeScope(ns.get());
    auto resolver = MakeResolver()->WithCurrentUsingScope(scope);

    // `Foo<>` (all type arguments are the UnboundTypeArgument placeholder) keeps
    // the BARE definition as the result type.
    auto result =
        resolver->LookupSimpleNameOrTypeName("Foo", {UnboundTypeArgument()}, NameLookupMode::Type);
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(&result->Type()), nullptr);
    EXPECT_EQ(&result->Type(), foo.get());
}

// --- The fallbacks ---------------------------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, DynamicKeywordYieldsTheDynamicType)
{
    auto resolver = MakeResolver();

    auto result = resolver->ResolveSimpleName("dynamic", {});
    auto typeResult = dynamic_cast<const TypeResolveResult*>(result.get());
    ASSERT_NE(typeResult, nullptr);
    EXPECT_EQ(result->Type().Kind(), TypeKind::Dynamic);
    EXPECT_FALSE(result->IsError());
}

TEST(CSharpResolverSimpleNameTest, UnknownIdentifierFallsThrough)
{
    auto resolver = MakeResolver();

    auto result = resolver->ResolveSimpleName("NoSuch", {});
    auto unknown = dynamic_cast<const UnknownIdentifierResolveResult*>(result.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Identifier(), "NoSuch");
    EXPECT_EQ(unknown->TypeArgumentCount(), 0);
    EXPECT_TRUE(result->IsError());
}

// --- IsVariableReferenceWithSameType ---------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, IsVariableReferenceWithSameTypeTrueForMatchingType)
{
    // A local variable of the type-parameter type T, with "T" resolving (in Type
    // mode) to the current type's type parameter: the types are identical.
    auto def = MakeDef("C");
    auto tp = std::make_shared<LookupTypeParameter>("T");
    def->SetTypeParameters({tp.get()});
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(def.get());

    auto variable = std::make_shared<TestVariable>("v", tp);
    auto rr = std::make_shared<LocalResolveResult>(variable.get());

    std::shared_ptr<TypeResolveResult> trr;
    EXPECT_TRUE(resolver->IsVariableReferenceWithSameType(*rr, "T", trr));
    ASSERT_NE(trr, nullptr);
    EXPECT_EQ(&trr->Type(), tp.get());
}

TEST(CSharpResolverSimpleNameTest, IsVariableReferenceWithSameTypeFalseForMismatchedType)
{
    auto def = MakeDef("C");
    auto tp = std::make_shared<LookupTypeParameter>("T");
    def->SetTypeParameters({tp.get()});
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(def.get());

    // A local of type int: "T" resolves to the type parameter, int != T.
    auto variable = std::make_shared<TestVariable>("v", Int32Type());
    auto rr = std::make_shared<LocalResolveResult>(variable.get());

    std::shared_ptr<TypeResolveResult> trr;
    EXPECT_FALSE(resolver->IsVariableReferenceWithSameType(*rr, "T", trr));
    // The out-param still carries the resolved type (the C# assigns it before the
    // comparison).
    ASSERT_NE(trr, nullptr);
}

TEST(CSharpResolverSimpleNameTest, IsVariableReferenceWithSameTypeRejectsNonMemberNonLocal)
{
    auto resolver = MakeResolver();
    auto rr = std::make_shared<TypeResolveResult>(Int32Type());

    std::shared_ptr<TypeResolveResult> trr = std::make_shared<TypeResolveResult>(Int32Type());
    EXPECT_FALSE(resolver->IsVariableReferenceWithSameType(*rr, "T", trr));
    EXPECT_EQ(trr, nullptr);  // reset by the early out
}

// --- CreateMemberLookup ----------------------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, CreateMemberLookupFlagsTheEnumMemberInitializer)
{
    // A field member inside an enum type: the enum-member-initializer flag is on.
    auto enumDef = MakeDef("E", TypeKind::Enum);
    auto field = std::make_shared<TestField>("F", Int32Type(), Compilation());
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(enumDef.get())->WithCurrentMember(field.get());
    EXPECT_TRUE(resolver->CreateMemberLookup().IsInEnumMemberInitializer());

    // A METHOD member inside the same enum type: the flag is off (the SymbolKind
    // check).
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto methodResolver = MakeResolver()->WithCurrentTypeDefinition(enumDef.get())->WithCurrentMember(method.get());
    EXPECT_FALSE(methodResolver->CreateMemberLookup().IsInEnumMemberInitializer());

    // A field member inside a NON-enum type: the flag is off (the Kind check).
    auto classDef = MakeDef("C", TypeKind::Class);
    auto classResolver = MakeResolver()->WithCurrentTypeDefinition(classDef.get())->WithCurrentMember(field.get());
    EXPECT_FALSE(classResolver->CreateMemberLookup().IsInEnumMemberInitializer());
}

TEST(CSharpResolverSimpleNameTest, BaseTypeReferenceLookupStartsAtTheDeclaringType)
{
    // A private member ON the current type is accessible through the normal
    // factory (the accessibility walk starts at the current type) but NOT through
    // the BaseTypeReference factory (which starts at the DECLARING type -- the
    // stack-overflow avoidance).
    auto outer = MakeDef("Outer");
    auto derived = MakeDef("Derived");
    derived->SetDeclaringTypeDefinition(outer.get());
    // A private method on Derived (LookupMethod's accessibility is configurable).
    auto privateMethod = std::make_shared<LookupMethod>("Private", Compilation());
    privateMethod->SetAccessibility(Accessibility::Private);
    privateMethod->SetDeclaringTypeDefinition(derived.get());

    auto resolver = MakeResolver()->WithCurrentTypeDefinition(derived.get());
    EXPECT_TRUE(resolver->CreateMemberLookup().IsAccessible(*privateMethod, false));
    EXPECT_FALSE(
        resolver->CreateMemberLookup(NameLookupMode::BaseTypeReference).IsAccessible(*privateMethod, false));
}

// --- ResolveAlias / ResolveExternAlias -------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, ResolveAliasGlobalYieldsTheRootNamespace)
{
    auto resolver = MakeResolver();

    auto result = resolver->ResolveAlias("global");
    auto nsResult = dynamic_cast<const NamespaceResolveResult*>(result.get());
    ASSERT_NE(nsResult, nullptr);
    EXPECT_EQ(nsResult->Namespace(), &Compilation().RootNamespace());
    EXPECT_FALSE(result->IsError());
}

TEST(CSharpResolverSimpleNameTest, ResolveAliasUnknownYieldsTheErrorResultSingleton)
{
    auto resolver = MakeResolver();

    auto result = resolver->ResolveAlias("Foo");
    // The port tracks no aliases, so the fallback is the ErrorResult singleton
    // (pointer identity with ErrorResolveResult::UnknownError).
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

TEST(CSharpResolverSimpleNameTest, ResolveExternAliasWithoutAliasYieldsTheErrorResultSingleton)
{
    auto resolver = MakeResolver();

    auto result = resolver->ResolveExternAlias("extern");
    // The LookupCompilation resolves no extern aliases.
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

// --- LookInUsingScopeNamespace direct --------------------------------------------------------

TEST(CSharpResolverSimpleNameTest, LookInUsingScopeNamespaceNullNamespaceYieldsNull)
{
    auto resolver = MakeResolver();
    EXPECT_EQ(resolver->LookInUsingScopeNamespace(nullptr, nullptr, "Foo", {}, true), nullptr);
}

TEST(CSharpResolverSimpleNameTest, LookInUsingScopeNamespaceSkipsTheTypeWhenTypeArgumentsArePresent)
{
    // k != 0 skips the child-namespace arm: "Sub" with a type argument does not
    // resolve to the namespace.
    auto ns = std::make_shared<ConfigurableNamespace>("N", Compilation());
    auto child = std::make_shared<ConfigurableNamespace>("Sub", Compilation());
    ns->AddChildNamespace(child.get());
    auto resolver = MakeResolver();

    EXPECT_EQ(resolver->LookInUsingScopeNamespace(nullptr, ns.get(), "Sub", {Int32Type()}, true),
              nullptr);
}
