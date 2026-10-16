// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver member-access region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines
// 1795-1912): ResolveMemberAccess + the private ResolveMemberAccessOnNamespace
// helper + ResolveIdentifierInObjectInitializer, plus the MethodGroupResolveResult
// extensionMethods/resolver wiring the region installs.
//
// The load-bearing cruxes:
//  (a) the namespace-target delegation -- a NamespaceResolveResult target routes to
//      ResolveMemberAccessOnNamespace (the child namespace first, then the type
//      definition parameterized only when type arguments are present AND
//      parameterizeResultType);
//  (b) the dynamic-target short-circuit -- a Dynamic-typed target yields the
//      DynamicMemberResolveResult without consulting the member lookup;
//  (c) the UnknownMemberResolveResult extension-method fallback -- a member that is
//      not found on the target falls back to a FRESH MethodGroupResolveResult (empty
//      method lists) with the extensionMethods set DIRECTLY (no resolver attached:
//      GetExtensionMethods() yields the set groups without a fetch), but only when
//      extension methods of that name exist in the current using scope;
//  (d) the MethodGroupResolveResult resolver attachment -- a method-group lookup
//      result gets the resolver attached (mgrr.resolver = this), so its
//      GetExtensionMethods() lazily FETCHES through the resolver's using scope on
//      first call and detaches afterwards (the cached second call needs no resolver);
//  (e) the type modes (Type / TypeInUsingDeclaration / BaseTypeReference) skip the
//      UnknownMemberResolveResult/MethodGroupResolveResult processing entirely (it
//      is only relevant for expressions) -- an unknown member under a Type lookup
//      stays UnknownMember even when extension methods exist;
//  (f) ResolveIdentifierInObjectInitializer looks the identifier up against the
//      CURRENT object-initializer target (the pushed initializedObject result).
//
// LIFETIME DISCIPLINE: the extension-method fallback path reaches the CACHED public
// CSharpConversions::ImplicitConversion(IType, IType) entry (the eligibility check
// inside GetExtensionMethods), so every test builds its resolver over a FRESH
// per-test LookupCompilation (CSharpConversions::Get caches its instance on that
// compilation's CacheManager; the instance and its cache die WITH the test -- the
// iteration-109 discipline).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/DynamicMemberResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::DynamicMemberResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::NameLookupMode;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A namespace stub with CONFIGURABLE child-namespace and type-definition tables (the
// iteration-108 ConfigurableNamespace shape, local to this file -- the
// each-test-file-carries-its-own-stubs convention). GetTypeDefinition matches on the
// (name, typeParameterCount) pair.
class MemberAccessNamespace : public INamespace {
public:
    MemberAccessNamespace(std::string name, const TS::ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetChildNamespace(const std::string& name, const INamespace* child)
    {
        children_[name] = child;
    }
    // Registers a type BOTH in the GetTypeDefinition (name, arity) table AND in the
    // Types() enumeration (a real namespace's type is both enumerable and resolvable
    // by name; the extension-method scan reads Types() while the member-access type
    // lookup reads GetTypeDefinition).
    void SetTypeDefinition(const std::string& name, int typeParameterCount,
                           const TS::ITypeDefinition* def)
    {
        types_.push_back({name, typeParameterCount, def});
        enumeratedTypes_.push_back(def);
    }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return {}; }
    std::vector<const TS::ITypeDefinition*> Types() const override { return enumeratedTypes_; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const INamespace* GetChildNamespace(const std::string& name) const override
    {
        auto it = children_.find(name);
        return it != children_.end() ? it->second : nullptr;
    }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string& name,
                                                 int typeParameterCount) const override
    {
        for (const auto& entry : types_)
            if (entry.name == name && entry.typeParameterCount == typeParameterCount)
                return entry.def;
        return nullptr;
    }

private:
    struct TypeEntry {
        std::string name;
        int typeParameterCount;
        const TS::ITypeDefinition* def;
    };
    std::string name_;
    const TS::ICompilation& compilation_;
    std::unordered_map<std::string, const INamespace*> children_;
    std::vector<TypeEntry> types_;
    std::vector<const TS::ITypeDefinition*> enumeratedTypes_;
};

// A `LookupTypeDefinition` subclass whose `GetMethods` returns a configured list (the
// D533 MethodHostType precedent): the composed `GetMembers` calls through to
// `GetMethods`, so `MemberLookup::Lookup` over a target of this type finds the
// methods (yielding a MethodGroupResolveResult).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const TS::IMethod*> m) { methods_ = std::move(m); }

    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter = nullptr,
        TS::GetMemberOptions options = TS::GetMemberOptions::None) const override
    {
        (void)options;
        if (!filter)
            return methods_;
        std::vector<const TS::IMethod*> r;
        for (const TS::IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const TS::IMethod*> methods_;
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the
// file header) plus the shared-managed definitions the member-access paths resolve
// over (the type-cache model: one instance per primitive, shared by the registry,
// the targets, and the extension-method `this` parameters).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> objectDef;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class))
    {
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                 KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    // An extension method with the given name over the `this` parameter type (the
    // iteration-109 MakeExtMethod shape): shared-managed method + parameter so the
    // non-owning pointers the scan stores stay valid for the test's duration.
    static std::pair<std::shared_ptr<LookupMethod>,
                     std::shared_ptr<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>>
    MakeExtMethod(const std::string& name, const LookupCompilation& compilation,
                  ITypePtr firstParamType)
    {
        auto method = std::make_shared<LookupMethod>(name, compilation);
        method->SetIsExtensionMethod(true);
        auto param = std::make_shared<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
            std::move(firstParamType), "x");
        method->SetParameters({param.get()});
        return {std::move(method), std::move(param)};
    }

    // A host type carrying the extension methods (the iteration-109 MakeHost shape).
    std::shared_ptr<LookupTypeDefinition> MakeHost(
        const std::vector<const TS::IMethod*>& methods) const
    {
        auto host = MakeDef("Extensions", KnownTypeCode::None, TypeKind::Class);
        host->SetStatic(true);
        host->SetHasExtensions(true);
        host->SetMethods(methods);
        return host;
    }
};

// A target over the given type (the `targetResolveResult` argument -- `Lookup` reads
// its `Type()`).
std::shared_ptr<ResolveResult> MakeTarget(ITypePtr type)
{
    return std::make_shared<ResolveResult>(std::move(type));
}

} // namespace

// ===========================================================================
// ResolveMemberAccessOnNamespace (the private helper, direct)
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, ChildNamespaceWithNoTypeArguments)
{
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    MemberAccessNamespace child("Child", fix.compilation);
    ns.SetChildNamespace("Child", &child);

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(target, "Child", {}, true);

    auto* nrr = dynamic_cast<NamespaceResolveResult*>(result.get());
    ASSERT_NE(nrr, nullptr);
    EXPECT_EQ(nrr->Namespace(), static_cast<const INamespace*>(&child));
}

TEST(CSharpResolverMemberAccessTest, ChildNamespaceIgnoredWithTypeArguments)
{
    // A non-empty typeArguments list skips the child-namespace arm entirely (the C#
    // `if (typeArguments.Count == 0)` guard), so even a registered child yields the
    // type-definition lookup (null here) -> the ErrorResult.
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    MemberAccessNamespace child("Child", fix.compilation);
    ns.SetChildNamespace("Child", &child);

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(target, "Child", {fix.int32}, true);

    EXPECT_TRUE(result->IsError());
}

TEST(CSharpResolverMemberAccessTest, TypeDefinitionWithoutTypeArguments)
{
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    ns.SetTypeDefinition("Foo", 0, fix.int32.get());

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(target, "Foo", {}, true);

    auto* trr = dynamic_cast<TypeResolveResult*>(result.get());
    ASSERT_NE(trr, nullptr);
    EXPECT_EQ(&trr->Type(), static_cast<const IType*>(fix.int32.get()));
}

TEST(CSharpResolverMemberAccessTest, TypeDefinitionWithTypeArgumentsParameterizes)
{
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    ns.SetTypeDefinition("Foo", 1, fix.objectDef.get());

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(
        target, "Foo", {fix.int32}, /*parameterizeResultType=*/true);

    auto* trr = dynamic_cast<TypeResolveResult*>(result.get());
    ASSERT_NE(trr, nullptr);
    auto* pt = dynamic_cast<const ParameterizedType*>(&trr->Type());
    ASSERT_NE(pt, nullptr);
    // The generic is the definition, the single type argument the int32 instance.
    EXPECT_EQ(pt->GetDefinition(), static_cast<const ITypeDefinition*>(fix.objectDef.get()));
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0].get(), fix.int32.get());
}

TEST(CSharpResolverMemberAccessTest, TypeDefinitionWithTypeArgumentsNotParameterized)
{
    // parameterizeResultType=false keeps the definition itself as the result type
    // (the `new TypeResolveResult(def)` arm).
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    ns.SetTypeDefinition("Foo", 1, fix.objectDef.get());

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(
        target, "Foo", {fix.int32}, /*parameterizeResultType=*/false);

    auto* trr = dynamic_cast<TypeResolveResult*>(result.get());
    ASSERT_NE(trr, nullptr);
    EXPECT_EQ(&trr->Type(), static_cast<const IType*>(fix.objectDef.get()));
}

TEST(CSharpResolverMemberAccessTest, NothingFoundYieldsErrorResult)
{
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(target, "NoSuchName", {}, true);

    EXPECT_TRUE(result->IsError());
}

TEST(CSharpResolverMemberAccessTest, WrongArityTypeDefinitionNotFound)
{
    // The (name, typeParameterCount) pair must BOTH match: "Foo" registered at arity 1
    // is not found for an arity-0 lookup.
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    ns.SetTypeDefinition("Foo", 1, fix.objectDef.get());

    NamespaceResolveResult target(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccessOnNamespace(target, "Foo", {}, true);

    EXPECT_TRUE(result->IsError());
}

// ===========================================================================
// ResolveMemberAccess -- the namespace/dynamic targets
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, NamespaceTargetDelegatesToNamespaceAccess)
{
    Fixture fix;
    MemberAccessNamespace ns("NS", fix.compilation);
    MemberAccessNamespace child("Child", fix.compilation);
    ns.SetChildNamespace("Child", &child);

    auto target = std::make_shared<NamespaceResolveResult>(&ns);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccess(target, "Child", {});

    auto* nrr = dynamic_cast<NamespaceResolveResult*>(result.get());
    ASSERT_NE(nrr, nullptr);
    EXPECT_EQ(nrr->Namespace(), static_cast<const INamespace*>(&child));
}

TEST(CSharpResolverMemberAccessTest, DynamicTargetYieldsDynamicMemberResult)
{
    Fixture fix;
    auto dynamicType = std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType=*/true);
    auto target = MakeTarget(dynamicType);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccess(target, "Foo", {});

    auto* dmr = dynamic_cast<DynamicMemberResolveResult*>(result.get());
    ASSERT_NE(dmr, nullptr);
    EXPECT_EQ(dmr->Member(), "Foo");
    EXPECT_EQ(dmr->Target(), target.get());
    // The dynamic member access is never an error (the C# does not run the lookup).
    EXPECT_FALSE(result->IsError());
}

// ===========================================================================
// ResolveMemberAccess -- the UnknownMemberResolveResult extension-method fallback
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, UnknownMemberWithoutExtensionMethodsPassesThrough)
{
    // No using scope -> no extension methods -> the UnknownMemberResolveResult is
    // returned as-is.
    Fixture fix;
    auto host = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    auto target = MakeTarget(host);

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccess(target, "Bar", {});

    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

TEST(CSharpResolverMemberAccessTest, UnknownMemberWithExtensionMethodsYieldsFallbackGroup)
{
    // The fallback crux: the member "Bar" is not found on the target, but an extension
    // method "Bar" exists in the using scope -- the result is a FRESH
    // MethodGroupResolveResult with EMPTY method lists and the extensionMethods set
    // DIRECTLY (no resolver attached: a second GetExtensionMethods() call yields the
    // same cached groups without any fetch).
    Fixture fix;
    auto [extMethod, extParam] = Fixture::MakeExtMethod("Bar", fix.compilation, fix.int32);
    auto host = fix.MakeHost({extMethod.get()});
    auto ns = std::make_shared<MemberAccessNamespace>("NS", fix.compilation);
    ns->SetTypeDefinition("Extensions", 0, host.get());

    auto targetDef = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    auto target = MakeTarget(targetDef);

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *ns,
                                               std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto result = scopedResolver->ResolveMemberAccess(target, "Bar", {});

    auto* mgrr = dynamic_cast<MethodGroupResolveResult*>(result.get());
    ASSERT_NE(mgrr, nullptr);
    // The fresh fallback group carries NO methods of its own (the C#
    // EmptyList<MethodListWithDeclaringType>.Instance).
    EXPECT_TRUE(mgrr->Methods().empty());
    // The extensionMethods were set directly (the HasExtensionMethods observable).
    EXPECT_TRUE(mgrr->HasExtensionMethods());
    // GetExtensionMethods() yields the set groups (pointer identity with the
    // extension method, no fetch involved).
    auto groups = mgrr->GetExtensionMethods();
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].size(), 1u);
    EXPECT_EQ(groups[0][0], static_cast<const TS::IMethod*>(extMethod.get()));
    // The original target is preserved as the group's target result.
    EXPECT_EQ(mgrr->TargetResult(), target.get());
}

TEST(CSharpResolverMemberAccessTest, TypeModeSkipsTheExtensionMethodFallback)
{
    // The crux differential: with the SAME using scope and the SAME unknown member,
    // the Type mode returns the UnknownMemberResolveResult (no extension-method
    // processing -- it is only relevant for expressions), while the Expression mode
    // built the fallback group.
    Fixture fix;
    auto [extMethod, extParam] = Fixture::MakeExtMethod("Bar", fix.compilation, fix.int32);
    auto host = fix.MakeHost({extMethod.get()});
    auto ns = std::make_shared<MemberAccessNamespace>("NS", fix.compilation);
    ns->SetTypeDefinition("Extensions", 0, host.get());

    auto targetDef = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    auto target = MakeTarget(targetDef);

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *ns,
                                               std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto result = scopedResolver->ResolveMemberAccess(target, "Bar", {},
                                                      NameLookupMode::Type);

    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

TEST(CSharpResolverMemberAccessTest, InvalidLookupModeThrows)
{
    // The C# `throw new NotSupportedException("Invalid value for NameLookupMode")`
    // ports to std::logic_error (the runtime-exception convention).
    Fixture fix;
    auto target = MakeTarget(fix.int32);
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    EXPECT_THROW(
        resolver->ResolveMemberAccess(target, "Foo", {},
                                      static_cast<NameLookupMode>(99)),
        std::logic_error);
}

// ===========================================================================
// ResolveMemberAccess -- the MethodGroupResolveResult resolver attachment
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, MethodLookupAttachesTheResolver)
{
    // The lookup finds the instance method "M" on the target type -> a
    // MethodGroupResolveResult whose resolver gets ATTACHED by ResolveMemberAccess:
    // GetExtensionMethods() lazily fetches through the resolver's using scope (an
    // extension method "M" in the scope is found by name), then detaches (the second
    // call yields the cached groups).
    Fixture fix;
    auto method = std::make_shared<LookupMethod>("M", fix.compilation);
    method->SetParameters({});
    auto targetDef = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    targetDef->SetMethods({method.get()});
    auto target = MakeTarget(targetDef);

    auto [extMethod, extParam] = Fixture::MakeExtMethod("M", fix.compilation, fix.int32);
    auto host = fix.MakeHost({extMethod.get()});
    auto ns = std::make_shared<MemberAccessNamespace>("NS", fix.compilation);
    ns->SetTypeDefinition("Extensions", 0, host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *ns,
                                               std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto result = scopedResolver->ResolveMemberAccess(target, "M", {});

    auto* mgrr = dynamic_cast<MethodGroupResolveResult*>(result.get());
    ASSERT_NE(mgrr, nullptr);
    // The group's own methods: the one instance method found by the lookup.
    ASSERT_EQ(mgrr->Methods().size(), 1u);
    EXPECT_EQ(mgrr->Methods()[0], static_cast<const TS::IMethod*>(method.get()));
    // The extensionMethods were NOT set directly (the lookup-produced group) -- they
    // come from the lazy resolver-attached fetch.
    EXPECT_FALSE(mgrr->HasExtensionMethods());

    // The first GetExtensionMethods() fetches through the attached resolver (the
    // scope's extension method "M" is found) and caches.
    auto groups = mgrr->GetExtensionMethods();
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].size(), 1u);
    EXPECT_EQ(groups[0][0], static_cast<const TS::IMethod*>(extMethod.get()));
    // After the fetch the groups are cached (HasExtensionMethods flips to true).
    EXPECT_TRUE(mgrr->HasExtensionMethods());

    // The second call yields the SAME cached groups (the resolver detached -- no
    // second fetch; the identical contents prove the cache).
    auto groups2 = mgrr->GetExtensionMethods();
    ASSERT_EQ(groups2.size(), 1u);
    ASSERT_EQ(groups2[0].size(), 1u);
    EXPECT_EQ(groups2[0][0], static_cast<const TS::IMethod*>(extMethod.get()));
}

TEST(CSharpResolverMemberAccessTest, MethodGroupWithoutResolverYieldsEmptyGroups)
{
    // A resolver-less group (constructed by hand, the pre-wiring state) still yields
    // empty groups -- the `?? Enumerable.Empty` fallback. (The braced empty lists are
    // spelled explicitly: a `{}` cannot deduce through make_shared's perfect-forwarding
    // parameter pack, the iteration-94 learning.)
    auto mgrr = std::make_shared<MethodGroupResolveResult>(
        nullptr, "M", std::vector<ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType>{},
        std::vector<ITypePtr>{});
    EXPECT_FALSE(mgrr->HasExtensionMethods());
    EXPECT_TRUE(mgrr->GetExtensionMethods().empty());
}

// ===========================================================================
// ResolveMemberAccess -- the InvocationTarget mode
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, InvocationTargetModeKeepsInvocableMembers)
{
    // The InvocationTarget mode runs the lookup with isInvocation=true: the method
    // "M" is invocable, so the method group survives (and the resolver is attached).
    Fixture fix;
    auto method = std::make_shared<LookupMethod>("M", fix.compilation);
    method->SetParameters({});
    auto targetDef = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    targetDef->SetMethods({method.get()});
    auto target = MakeTarget(targetDef);

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveMemberAccess(target, "M", {},
                                                 NameLookupMode::InvocationTarget);

    auto* mgrr = dynamic_cast<MethodGroupResolveResult*>(result.get());
    ASSERT_NE(mgrr, nullptr);
    ASSERT_EQ(mgrr->Methods().size(), 1u);
    EXPECT_EQ(mgrr->Methods()[0], static_cast<const TS::IMethod*>(method.get()));
}

// ===========================================================================
// ResolveIdentifierInObjectInitializer
// ===========================================================================

TEST(CSharpResolverMemberAccessTest, IdentifierResolvesAgainstCurrentInitializer)
{
    // The pushed object-initializer target: the identifier "M" resolves against the
    // TARGET TYPE of the pushed result (the method "M" on it -> a method group).
    Fixture fix;
    auto method = std::make_shared<LookupMethod>("M", fix.compilation);
    method->SetParameters({});
    auto targetDef = std::make_shared<MethodHostType>(
        "Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, fix.compilation, nullptr);
    targetDef->SetMethods({method.get()});

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto pushed = resolver->PushObjectInitializer(MakeTarget(targetDef));
    auto result = pushed->ResolveIdentifierInObjectInitializer("M");

    auto* mgrr = dynamic_cast<MethodGroupResolveResult*>(result.get());
    ASSERT_NE(mgrr, nullptr);
    ASSERT_EQ(mgrr->Methods().size(), 1u);
    EXPECT_EQ(mgrr->Methods()[0], static_cast<const TS::IMethod*>(method.get()));
}

TEST(CSharpResolverMemberAccessTest, IdentifierWithoutInitializerYieldsUnknownMember)
{
    // No initializer pushed: the CurrentObjectInitializer is the UnknownError
    // singleton, whose UnknownType has no members -> the UnknownMemberResolveResult.
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->ResolveIdentifierInObjectInitializer("M");

    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}
