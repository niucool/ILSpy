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

// Tests for the CSharpResolver extension-methods region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 2018-2243):
// the two static IsEligibleExtensionMethod checks, the per-namespace extension-method
// scan, the GetAllExtensionMethods scope-chain walk with the UsingScope.AllExtensionMethods
// LazyInit memoization, and the two public GetExtensionMethods filter entries.
//
// The load-bearing cruxes:
//  (a) the eligibility verdict is NOT just `conversion.IsValid` -- a NUMERIC widening
//      (int -> long) is a valid implicit conversion but none of the four eligible kinds
//      (identity / reference / boxing / implicit-span), so the method is NOT eligible;
//  (b) the `this in`/`this ref` ByReference first parameter is UNWRAPPED to its element
//      type before the conversion check (without the unwrap, int -> `ref int` has no
//      conversion at all);
//  (c) the generic-method inference: the inferred types are reported through the out
//      parameter ONLY when at least one type argument was actually inferred (an
//      uninferable position is fixed up to the method's own type parameter and the out
//      parameter stays null -- the C# comment "do not substitute types that could not be
//      inferred"); a constraint violation on an inferred type rejects the method;
//  (d) the namespace scan filter conjunction -- IsStatic AND HasExtensions AND zero
//      type parameters AND accessibility -- plus the IsExtensionMethod filter on the
//      methods themselves;
//  (e) the scope-chain walk groups by using scope (the own namespace's methods, then
//      the DISTINCT imported namespaces' methods, per scope, innermost first), memoized
//      on the UsingScope.AllExtensionMethods field (a second call returns the SAME
//      shared handle, pointer identity);
//  (f) the public filter keeps ALL of a scope's accessible-by-name methods when the
//      target type is NULL (the null-target eligibility contract) but drops inaccessible
//      methods; a non-empty explicit typeArguments list re-specializes arity-matching
//      generic methods (no inference); substituteInferredTypes specializes over the
//      INFERRED type arguments.
//
// LIFETIME DISCIPLINE: every eligibility check reaches the CACHED public
// CSharpConversions::ImplicitConversion(IType, IType) entry, whose TypePair keys are
// non-owning raw pointers (the D540 dangling-key caveat applies when test-local types
// die while the per-compilation conversions singleton survives). Every test therefore
// builds its resolver over a FRESH per-test LookupCompilation: CSharpConversions::Get
// caches its instance on that compilation's CacheManager, so the conversions instance
// (and its cache) dies WITH the test -- no cross-test dangling is possible regardless
// of which types flowed into the cache.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// A namespace stub with configurable type and child tables (the iteration-108
// ConfigurableNamespace shape, local to this file -- the each-test-file-carries-its-own
// stubs convention). A null type entry is accepted so the D516 null-skip guard in the
// scan is testable.
class TestNamespace : public INamespace {
public:
    TestNamespace(std::string name, const TS::ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddTypeDefinition(const TS::ITypeDefinition* def) { types_.push_back(def); }

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
    std::vector<const TS::ITypeDefinition*> Types() const override { return types_; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const INamespace* GetChildNamespace(const std::string&) const override { return nullptr; }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&,
                                                 int) const override
    {
        return nullptr;
    }

private:
    std::string name_;
    const TS::ICompilation& compilation_;
    std::vector<const TS::ITypeDefinition*> types_;
};

// A method type parameter carrying the AcceptVisitor -> VisitTypeParameter bridge (the
// plain LookupTypeParameter routes to VisitOtherType, so TypeParameterSubstitution's
// VisitTypeParameter never fires for it -- the D564 VisitableTypeParameter precedent);
// the substitution is load-bearing for the generic eligibility tests (the inferred
// type arguments must replace the type parameter in the `this` parameter type).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A definition with a DEFINITE reference-type IsReferenceType (the D517 RefDef
// precedent): the reference-conversion guard needs a definite true on both sides --
// the plain LookupTypeDefinition inherits the IType `std::nullopt` default, which
// fails the guard (an indeterminate type is neither a reference type nor a value
// type).
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A definition with a DEFINITE value-type IsReferenceType (the D519 ByRefLikeDef/
// ValueTypeDef precedent): the boxing guard needs a definite false on the from-side.
class ValueTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
};

// An extension-method stub recording Specialize calls (the iteration-65 TestMethod
// recorder precedent): the explicit-type-arguments and substituteInferredTypes arms
// both route through method->Specialize, which the stub must capture to be observable
// (the identity-Specialize of the plain LookupMethod returns `this` silently).
class RecordingExtMethod : public LookupMethod {
public:
    using LookupMethod::LookupMethod;

    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(
        const TypeParameterSubstitution* substitution) const override {
        ++specializeCallCount_;
        if (substitution != nullptr)
            lastMethodTypeArguments_ = substitution->MethodTypeArguments();
        return this;
    }

    int SpecializeCallCount() const { return specializeCallCount_; }
    const std::optional<std::vector<ITypePtr>>& LastMethodTypeArguments() const {
        return lastMethodTypeArguments_;
    }

private:
    mutable int specializeCallCount_ = 0;
    mutable std::optional<std::vector<ITypePtr>> lastMethodTypeArguments_;
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the file
// header) plus one shared-managed definition per primitive/conversion the eligibility
// paths exercise. Every definition instance doubles as the type the FindType-free
// conversion paths read (GetTypeCode / IsKnownType read the definition's own
// KnownTypeCode; the derived chain uses identity equality, so the base-type handle is
// the SAME instance registered here).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> int64;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> stringDef;
    std::shared_ptr<LookupTypeDefinition> someBase;
    std::shared_ptr<LookupTypeDefinition> derived;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          int64(MakeDef("Int64", KnownTypeCode::Int64, TypeKind::Struct)),
          objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          stringDef(MakeDef("String", KnownTypeCode::String, TypeKind::Class)),
          someBase(MakeDef("SomeBase", KnownTypeCode::None, TypeKind::Class)),
          derived(MakeDef("Derived", KnownTypeCode::None, TypeKind::Class))
    {
        // The derived->object implicit reference conversion walks the direct-base chain
        // (identity equality on the shared base instance).
        derived->AddDirectBaseType(objectDef);
    }

    // An extension-method host: a static, HasExtensions class over the methods.
    std::shared_ptr<LookupTypeDefinition> MakeHost(
        const std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>& methods) const
    {
        auto host = MakeDef("Extensions", KnownTypeCode::None, TypeKind::Class);
        host->SetStatic(true);
        host->SetHasExtensions(true);
        host->SetMethods(methods);
        return host;
    }

    // An extension method with the given first-parameter type (the `this` parameter);
    // the DefaultParameter is shared-managed by the caller's scope (the non-owning
    // IParameter* the method stores stays valid for the test's duration).
    static std::pair<std::shared_ptr<LookupMethod>, std::shared_ptr<DefaultParameter>>
    MakeExtMethod(const std::string& name, const LookupCompilation& compilation,
                  ITypePtr firstParamType)
    {
        auto method = std::make_shared<LookupMethod>(name, compilation);
        method->SetIsExtensionMethod(true);
        auto param = std::make_shared<DefaultParameter>(std::move(firstParamType), "x");
        method->SetParameters({param.get()});
        return {std::move(method), std::move(param)};
    }

private:
    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        // The Class kinds carry a DEFINITE reference-type IsReferenceType, the Struct
        // kinds a definite value-type one (the conversion guards are definite-only).
        if (kind == TypeKind::Class)
            return std::make_shared<RefDef>(
                name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
                Accessibility::Public, compilation, nullptr, code);
        return std::make_shared<ValueTypeDef>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }
};

} // namespace

// ===========================================================================
// IsEligibleExtensionMethod (the two static checks)
// ===========================================================================

TEST(CSharpResolverExtensionMethodsTest, NullTargetTypeIsEligible)
{
    Fixture fix;
    auto method = std::make_shared<LookupMethod>("Ext", fix.compilation);
    method->SetIsExtensionMethod(true);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(nullptr, *method, true, inferred));
    EXPECT_FALSE(inferred.has_value());
}

TEST(CSharpResolverExtensionMethodsTest, ZeroParameterMethodIsNotEligible)
{
    Fixture fix;
    auto method = std::make_shared<LookupMethod>("Ext", fix.compilation);
    method->SetIsExtensionMethod(true);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_FALSE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
    EXPECT_FALSE(inferred.has_value());
}

TEST(CSharpResolverExtensionMethodsTest, IdentityConversionIsEligible)
{
    Fixture fix;
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.int32);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, NumericWideningIsNotEligible)
{
    // int -> long IS a valid implicit conversion (numeric widening) but is NONE of the
    // four eligible kinds, so the extension method is NOT eligible (the verdict crux).
    Fixture fix;
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.int64);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_FALSE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, ReferenceConversionIsEligible)
{
    Fixture fix;
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.objectDef);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.derived.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, BoxingConversionIsEligible)
{
    Fixture fix;
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.objectDef);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, InconvertiblePairIsNotEligible)
{
    Fixture fix;
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.int32);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_FALSE(CSharpResolver::IsEligibleExtensionMethod(fix.stringDef.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, ByReferenceFirstParameterIsUnwrapped)
{
    // `this ref int` -- the ByReference unwrap is load-bearing: int -> `ref int` has no
    // implicit conversion at all, but int -> int (the element) is an identity conversion.
    Fixture fix;
    auto byRef = std::make_shared<ILSpy::Decompiler::TypeSystem::ByReferenceType>(fix.int32);
    auto [method, param] = Fixture::MakeExtMethod("Ext", fix.compilation, byRef);
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
}

TEST(CSharpResolverExtensionMethodsTest, GenericInferenceReportsInferredTypeArguments)
{
    // `Echo<T>(this T x)` with an int target: T is inferred to the TARGET INSTANCE
    // (pointer identity through the TypeInference Fix path), the unconstrained
    // validation passes, and the out parameter receives the inferred array.
    Fixture fix;
    auto tp = std::make_shared<VisitableTypeParameter>("T");
    auto method = std::make_shared<LookupMethod>("Echo", fix.compilation);
    method->SetIsExtensionMethod(true);
    auto param = std::make_shared<DefaultParameter>(tp, "x");
    method->SetParameters({param.get()});
    method->SetTypeParameters({tp.get()});
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
    ASSERT_TRUE(inferred.has_value());
    ASSERT_EQ(inferred->size(), 1u);
    EXPECT_EQ((*inferred)[0].get(), fix.int32.get());
}

TEST(CSharpResolverExtensionMethodsTest, GenericUninferableKeepsTypeParameterAndOutStaysNull)
{
    // `Format<T>(this object o)` -- T does not occur in the `this` parameter type, so
    // nothing can be inferred: the inferred slot is fixed up to the method's own type
    // parameter, the out parameter STAYS null, and the method is still eligible (the
    // object -> object identity conversion on the un-substituted parameter type).
    Fixture fix;
    auto tp = std::make_shared<VisitableTypeParameter>("T");
    auto method = std::make_shared<LookupMethod>("Format", fix.compilation);
    method->SetIsExtensionMethod(true);
    auto param = std::make_shared<DefaultParameter>(fix.objectDef, "o");
    method->SetParameters({param.get()});
    method->SetTypeParameters({tp.get()});
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_TRUE(CSharpResolver::IsEligibleExtensionMethod(fix.objectDef.get(), *method, true, inferred));
    EXPECT_FALSE(inferred.has_value());
}

TEST(CSharpResolverExtensionMethodsTest, GenericConstraintViolationIsNotEligible)
{
    // `Echo<T>(this T x) where T : SomeBase` with an int target: the inference infers T
    // = int, but the constraint validation fails (int is not constraint-convertible to
    // SomeBase), so the method is rejected -- pinning the ValidateConstraints wiring.
    Fixture fix;
    auto tp = std::make_shared<VisitableTypeParameter>("T");
    tp->SetDirectBaseTypes({fix.someBase});
    auto method = std::make_shared<LookupMethod>("Echo", fix.compilation);
    method->SetIsExtensionMethod(true);
    auto param = std::make_shared<DefaultParameter>(tp, "x");
    method->SetParameters({param.get()});
    method->SetTypeParameters({tp.get()});
    std::optional<std::vector<ITypePtr>> inferred;
    EXPECT_FALSE(CSharpResolver::IsEligibleExtensionMethod(fix.int32.get(), *method, true, inferred));
    EXPECT_FALSE(inferred.has_value());
}

// ===========================================================================
// GetExtensionMethods(MemberLookup, INamespace) -- the per-namespace scan
// ===========================================================================

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanFindsExtensionMethods)
{
    Fixture fix;
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto [m2, p2] = Fixture::MakeExtMethod("Ext2", fix.compilation, fix.objectDef);
    auto plain = std::make_shared<LookupMethod>("Plain", fix.compilation);
    auto host = fix.MakeHost({m1.get(), m2.get(), plain.get()});
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    MemberLookup lookup = resolver->CreateMemberLookup();
    auto result = resolver->GetExtensionMethods(lookup, *ns);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(m1.get()));
    EXPECT_EQ(result[1], static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(m2.get()));
}

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanFiltersNonStaticHost)
{
    Fixture fix;
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto host = fix.MakeHost({m1.get()});
    host->SetStatic(false);
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    EXPECT_TRUE(resolver->GetExtensionMethods(resolver->CreateMemberLookup(), *ns).empty());
}

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanFiltersHostWithoutExtensions)
{
    Fixture fix;
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto host = fix.MakeHost({m1.get()});
    host->SetHasExtensions(false);
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    EXPECT_TRUE(resolver->GetExtensionMethods(resolver->CreateMemberLookup(), *ns).empty());
}

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanFiltersGenericHost)
{
    // A generic `class Extensions<T>` cannot carry extension methods; the scan
    // requires a zero-type-parameter host.
    Fixture fix;
    auto tp = std::make_shared<LookupTypeParameter>("T");
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto host = fix.MakeHost({m1.get()});
    host->SetTypeParameters({tp.get()});
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    EXPECT_TRUE(resolver->GetExtensionMethods(resolver->CreateMemberLookup(), *ns).empty());
}

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanFiltersInaccessibleHost)
{
    Fixture fix;
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto host = fix.MakeHost({m1.get()});
    // A private host is not accessible from a resolver with no current type definition.
    host->SetAccessibility(Accessibility::Private);
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    EXPECT_TRUE(resolver->GetExtensionMethods(resolver->CreateMemberLookup(), *ns).empty());
}

TEST(CSharpResolverExtensionMethodsTest, NamespaceScanSkipsNullTypeEntry)
{
    // The D516 null-entry guard: a degenerate null in the namespace's Types() snapshot
    // is skipped, not dereferenced.
    Fixture fix;
    auto [m1, p1] = Fixture::MakeExtMethod("Ext1", fix.compilation, fix.int32);
    auto host = fix.MakeHost({m1.get()});
    auto ns = std::make_shared<TestNamespace>("NS", fix.compilation);
    ns->AddTypeDefinition(nullptr);
    ns->AddTypeDefinition(host.get());
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto result = resolver->GetExtensionMethods(resolver->CreateMemberLookup(), *ns);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(m1.get()));
}

// ===========================================================================
// GetAllExtensionMethods -- the scope-chain walk and the LazyInit memoization
// ===========================================================================

TEST(CSharpResolverExtensionMethodsTest, NoUsingScopeYieldsEmptyGroups)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto all = resolver->GetAllExtensionMethods(resolver->CreateMemberLookup());
    ASSERT_NE(all, nullptr);
    EXPECT_TRUE(all->empty());
}

TEST(CSharpResolverExtensionMethodsTest, ScopeChainGroupsOwnAndImportedNamespaces)
{
    // nsA's own methods form the FIRST group; the DISTINCT imported namespaces form the
    // second. The same namespace appearing TWICE in the usings is deduplicated (the C#
    // `scope.Usings.Distinct()` -- without it, ext2 would appear twice).
    Fixture fix;
    auto [mOwn, pOwn] = Fixture::MakeExtMethod("ExtOwn", fix.compilation, fix.int32);
    auto hostA = fix.MakeHost({mOwn.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(hostA.get());

    auto [mImported, pImported] = Fixture::MakeExtMethod("ExtImported", fix.compilation, fix.int32);
    auto hostB = fix.MakeHost({mImported.get()});
    auto nsB = std::make_shared<TestNamespace>("NSB", fix.compilation);
    nsB->AddTypeDefinition(hostB.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        context, *nsA, std::vector<const INamespace*>{nsB.get(), nsB.get()});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto all = scopedResolver->GetAllExtensionMethods(scopedResolver->CreateMemberLookup());
    ASSERT_EQ(all->size(), 2u);
    ASSERT_EQ((*all)[0].size(), 1u);
    EXPECT_EQ((*all)[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mOwn.get()));
    ASSERT_EQ((*all)[1].size(), 1u);
    EXPECT_EQ((*all)[1][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mImported.get()));
}

TEST(CSharpResolverExtensionMethodsTest, AllExtensionMethodsMemoizedOnTheScope)
{
    // The LazyInit contract: the first call stores the computed groups into the scope's
    // AllExtensionMethods field; a second call returns the SAME memoized handle.
    Fixture fix;
    auto [mOwn, pOwn] = Fixture::MakeExtMethod("ExtOwn", fix.compilation, fix.int32);
    auto host = fix.MakeHost({mOwn.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto all1 = scopedResolver->GetAllExtensionMethods(scopedResolver->CreateMemberLookup());
    auto all2 = scopedResolver->GetAllExtensionMethods(scopedResolver->CreateMemberLookup());
    EXPECT_EQ(all1.get(), all2.get());
    EXPECT_EQ(scope->AllExtensionMethods.get(), all1.get());
    ASSERT_EQ(all1->size(), 1u);
    ASSERT_EQ((*all1)[0].size(), 1u);
    EXPECT_EQ((*all1)[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mOwn.get()));
}

TEST(CSharpResolverExtensionMethodsTest, ParentScopesWalkInnermostFirst)
{
    // The child scope's own group comes first; the parent scope's own group second
    // (the `scope.Parent` chain walk, innermost first).
    Fixture fix;
    auto [mChild, pChild] = Fixture::MakeExtMethod("ExtChild", fix.compilation, fix.int32);
    auto childHost = fix.MakeHost({mChild.get()});
    auto childNs = std::make_shared<TestNamespace>("Child", fix.compilation);
    childNs->AddTypeDefinition(childHost.get());

    auto [mParent, pParent] = Fixture::MakeExtMethod("ExtParent", fix.compilation, fix.int32);
    auto parentHost = fix.MakeHost({mParent.get()});
    auto parentNs = std::make_shared<TestNamespace>("Parent", fix.compilation);
    parentNs->AddTypeDefinition(parentHost.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto outerScope = std::make_shared<UsingScope>(
        outerContext, *parentNs, std::vector<const INamespace*>{});
    auto outerContextWithScope = outerContext->WithUsingScope(outerScope);
    auto childScope = std::make_shared<UsingScope>(
        outerContextWithScope, *childNs, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(childScope);

    auto all = scopedResolver->GetAllExtensionMethods(scopedResolver->CreateMemberLookup());
    ASSERT_EQ(all->size(), 2u);
    ASSERT_EQ((*all)[0].size(), 1u);
    EXPECT_EQ((*all)[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mChild.get()));
    ASSERT_EQ((*all)[1].size(), 1u);
    EXPECT_EQ((*all)[1][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mParent.get()));
}

TEST(CSharpResolverExtensionMethodsTest, AllIncludesInaccessibleMethodsButFilterExcludesThem)
{
    // The GetAll contract comment: "This list includes inaccessible methods" -- the
    // private method IS in the memoized group; the public FILTER then applies
    // lookup.IsAccessible per method and drops it (the crux pair).
    Fixture fix;
    auto [mPublic, pPublic] = Fixture::MakeExtMethod("ExtPublic", fix.compilation, fix.int32);
    auto [mPrivate, pPrivate] = Fixture::MakeExtMethod("ExtPrivate", fix.compilation, fix.int32);
    mPrivate->SetAccessibility(Accessibility::Private);
    auto host = fix.MakeHost({mPublic.get(), mPrivate.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto all = scopedResolver->GetAllExtensionMethods(scopedResolver->CreateMemberLookup());
    ASSERT_EQ(all->size(), 1u);
    EXPECT_EQ((*all)[0].size(), 2u);

    // The null-target filter entry (every method eligible) still applies the
    // accessibility filter.
    auto filtered = scopedResolver->GetExtensionMethods(
        nullptr, std::nullopt, std::nullopt, /*substituteInferredTypes*/ false);
    ASSERT_EQ(filtered.size(), 1u);
    ASSERT_EQ(filtered[0].size(), 1u);
    EXPECT_EQ(filtered[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mPublic.get()));
}

// ===========================================================================
// GetExtensionMethods(IType targetType, ...) -- the public filter
// ===========================================================================

TEST(CSharpResolverExtensionMethodsTest, FilterByNameAndTargetEligibility)
{
    Fixture fix;
    auto [mInt, pInt] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.int32);
    auto [mOther, pOther] = Fixture::MakeExtMethod("Other", fix.compilation, fix.int32);
    auto [mLong, pLong] = Fixture::MakeExtMethod("ExtLong", fix.compilation, fix.int64);
    auto host = fix.MakeHost({mInt.get(), mOther.get(), mLong.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    // int target, name "Ext": only the int-eligible same-named method survives.
    auto byName = scopedResolver->GetExtensionMethods(
        fix.int32.get(), std::optional<std::string>("Ext"), std::nullopt, false);
    ASSERT_EQ(byName.size(), 1u);
    ASSERT_EQ(byName[0].size(), 1u);
    EXPECT_EQ(byName[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(mInt.get()));

    // The numeric-widening verdict crux at the FILTER level: an int target against
    // "ExtLong" (this long) is a valid numeric conversion but NOT eligible, so no
    // group survives (the group is dropped when every method in it is filtered).
    auto byNameLong = scopedResolver->GetExtensionMethods(
        fix.int32.get(), std::optional<std::string>("ExtLong"), std::nullopt, false);
    EXPECT_TRUE(byNameLong.empty());

    // The no-name call returns every ACCESSIBLE eligible method for the target: the
    // int method is eligible, the long method is not (numeric), "Other" is eligible.
    auto noName = scopedResolver->GetExtensionMethods(
        fix.int32.get(), std::nullopt, std::nullopt, false);
    ASSERT_EQ(noName.size(), 1u);
    EXPECT_EQ(noName[0].size(), 2u);
}

TEST(CSharpResolverExtensionMethodsTest, ExplicitTypeArgumentsSpecializeArityMatch)
{
    // A non-empty explicit typeArguments list re-specializes the arity-matching generic
    // method and checks the SPECIALIZED form's eligibility (no inference); the
    // arity-mismatched and non-generic methods are skipped.
    Fixture fix;
    auto tp = std::make_shared<VisitableTypeParameter>("T");
    auto writeMethod = std::make_shared<RecordingExtMethod>("Write", fix.compilation);
    writeMethod->SetIsExtensionMethod(true);
    auto firstParam = std::make_shared<DefaultParameter>(fix.objectDef, "o");
    auto secondParam = std::make_shared<DefaultParameter>(tp, "v");
    writeMethod->SetParameters({firstParam.get(), secondParam.get()});
    writeMethod->SetTypeParameters({tp.get()});

    auto [plain, plainParam] = Fixture::MakeExtMethod("Write", fix.compilation, fix.objectDef);
    auto host = fix.MakeHost({writeMethod.get(), plain.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto filtered = scopedResolver->GetExtensionMethods(
        fix.objectDef.get(), std::optional<std::string>("Write"),
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{fix.int32}}, false);
    ASSERT_EQ(filtered.size(), 1u);
    ASSERT_EQ(filtered[0].size(), 1u);
    // The Specialize recorder: the explicit type arguments flow into the substitution.
    EXPECT_EQ(writeMethod->SpecializeCallCount(), 1);
    ASSERT_TRUE(writeMethod->LastMethodTypeArguments().has_value());
    ASSERT_EQ(writeMethod->LastMethodTypeArguments()->size(), 1u);
    EXPECT_EQ((*writeMethod->LastMethodTypeArguments())[0].get(), fix.int32.get());

    // An arity mismatch (2 explicit arguments vs 1 type parameter) skips the method.
    auto mismatch = scopedResolver->GetExtensionMethods(
        fix.objectDef.get(), std::optional<std::string>("Write"),
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{fix.int32, fix.int64}}, false);
    EXPECT_TRUE(mismatch.empty());
    EXPECT_EQ(writeMethod->SpecializeCallCount(), 1); // no further Specialize call
}

TEST(CSharpResolverExtensionMethodsTest, SubstituteInferredTypesSpecializesOverInference)
{
    // The substituteInferredTypes flag: with a true flag, the generic method is stored
    // SPECIALIZED over the INFERRED type arguments (the recorder sees the inferred
    // int); with a false flag, the unspecialized method is stored and no Specialize
    // call happens.
    Fixture fix;
    auto tp = std::make_shared<VisitableTypeParameter>("T");
    auto echoMethod = std::make_shared<RecordingExtMethod>("Echo", fix.compilation);
    echoMethod->SetIsExtensionMethod(true);
    auto firstParam = std::make_shared<DefaultParameter>(tp, "x");
    echoMethod->SetParameters({firstParam.get()});
    echoMethod->SetTypeParameters({tp.get()});
    auto host = fix.MakeHost({echoMethod.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto withSub = scopedResolver->GetExtensionMethods(
        fix.int32.get(), std::nullopt, std::nullopt, /*substituteInferredTypes*/ true);
    ASSERT_EQ(withSub.size(), 1u);
    ASSERT_EQ(withSub[0].size(), 1u);
    EXPECT_EQ(withSub[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(echoMethod.get()));
    EXPECT_EQ(echoMethod->SpecializeCallCount(), 1);
    ASSERT_TRUE(echoMethod->LastMethodTypeArguments().has_value());
    ASSERT_EQ(echoMethod->LastMethodTypeArguments()->size(), 1u);
    // The INFERRED type argument (the int target instance) reaches the substitution.
    EXPECT_EQ((*echoMethod->LastMethodTypeArguments())[0].get(), fix.int32.get());

    auto withoutSub = scopedResolver->GetExtensionMethods(
        fix.int32.get(), std::nullopt, std::nullopt, /*substituteInferredTypes*/ false);
    ASSERT_EQ(withoutSub.size(), 1u);
    ASSERT_EQ(withoutSub[0].size(), 1u);
    EXPECT_EQ(withoutSub[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(echoMethod.get()));
    // The false flag stores the unspecialized method: no further Specialize call.
    EXPECT_EQ(echoMethod->SpecializeCallCount(), 1);
}

TEST(CSharpResolverExtensionMethodsTest, NoTargetEntryReturnsAccessibleMethods)
{
    // The thin no-target delegate: a null target is eligible for every method (the
    // code-completion shape), so BOTH methods -- including the one whose parameter
    // type no int target could convert to -- survive the filter.
    Fixture fix;
    auto [mInt, pInt] = Fixture::MakeExtMethod("Ext", fix.compilation, fix.int32);
    auto [mLong, pLong] = Fixture::MakeExtMethod("ExtLong", fix.compilation, fix.int64);
    auto host = fix.MakeHost({mInt.get(), mLong.get()});
    auto nsA = std::make_shared<TestNamespace>("NSA", fix.compilation);
    nsA->AddTypeDefinition(host.get());

    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto context = std::make_shared<CSharpTypeResolveContext>(fix.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(context, *nsA, std::vector<const INamespace*>{});
    auto scopedResolver = resolver->WithCurrentUsingScope(scope);

    auto all = scopedResolver->GetExtensionMethods();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].size(), 2u);
}
