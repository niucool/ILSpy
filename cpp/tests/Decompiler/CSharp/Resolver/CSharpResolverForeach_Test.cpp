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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver ResolveForeach region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 1913-2018,
// C# 4.0 spec section 8.8.4 "The foreach statement"): the public ResolveForeach
// entry + the private CheckForEnumerableInterface helper (widened to public for
// direct TDD).
//
// The load-bearing cruxes:
//  (a) the ARRAY / DYNAMIC arm -- the collection is cast to the registered
//      non-generic IEnumerable (CollectionType / EnumeratorType identity against
//      the registered defs), the element type is the array's element instance
//      (pointer identity) / dynamic, and the GetEnumerator chain
//      (ResolveCast -> ResolveMemberAccess -> ResolveInvocation) produces the
//      GetEnumeratorCall;
//  (b) the ENUMERATOR PATTERN -- a public, non-static, applicable, unambiguous
//      GetEnumerator method on the collection fires the pattern: the collection
//      type is the expression's own type, the enumerator type is the invocation's
//      result type (the method's return type), the element type comes from the
//      Current property, and the CurrentProperty / MoveNextMethod pointers are the
//      looked-up stubs;
//  (c) the GUARD -- a static / non-public / inapplicable / ambiguous GetEnumerator
//      falls back to CheckForEnumerableInterface (each rejection direction);
//  (d) the INTERFACE PATTERN -- the generic arm builds the IEnumerable<T> /
//      IEnumerator<T> ParameterizedTypes over the registered open-generic
//      definitions with the GetElementTypeFromIEnumerable element type; the
//      non-generic arm uses the registered plain interfaces; neither yields the
//      UnknownType null object.
//
// LIFETIME DISCIPLINE: the resolution paths reach the per-compilation
// CSharpConversions instance (the resolver's Conversions()), so every test builds
// its resolver over a FRESH per-test LookupCompilation (CSharpConversions::Get
// caches its instance on that compilation's CacheManager; the instance and its
// cache die WITH the test -- the iteration-109 discipline). Every FindType-resolved
// code (IEnumerable / IEnumerator / IEnumerableOfT / IEnumeratorOfT / Void /
// Object) must be registered with a make_shared'd definition (the bad_weak_ptr
// trap: an unregistered code falls back to the compilation's non-shared
// unknownType_ stub whose shared_from_this throws). Every method whose return
// type flows into a result type needs SetReturnType over a make_shared'd type
// (the ComputeType shared_from_this trap).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ForEachResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::Semantics::ForEachResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` subclass whose `GetMethods` / `GetProperties` return
// configured lists (the D533 MethodHostType precedent, extended with the property
// family the `Current` lookup reads through the composed `IType::GetMembers`).
class MemberHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const TS::IMethod*> m) { methods_ = std::move(m); }
    void SetProperties(std::vector<const TS::IProperty*> p) { properties_ = std::move(p); }

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

    std::vector<const TS::IProperty*> GetProperties(
        std::function<bool(const TS::IProperty*)> filter = nullptr,
        TS::GetMemberOptions options = TS::GetMemberOptions::None) const override
    {
        (void)options;
        if (!filter)
            return properties_;
        std::vector<const TS::IProperty*> r;
        for (const TS::IProperty* p : properties_)
            if (filter(p))
                r.push_back(p);
        return r;
    }

private:
    std::vector<const TS::IMethod*> methods_;
    std::vector<const TS::IProperty*> properties_;
};

// A minimal `IProperty` -- the `Current` property on the enumerator (the
// GetBestCandidateWithSubstitutedTypeArguments TestProperty precedent, with
// `IsIndexer` false so the member filter's `SymbolKind != Indexer` shape is
// faithfully a plain property).
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const TS::ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation)
    {
    }

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const TS::ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
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
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
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
        return &identitySubst_;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
    {
        return {};
    }
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    const TS::ICompilation& compilation_;
    mutable ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution identitySubst_{
        std::nullopt, std::nullopt};
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the
// file header) plus the registered known-type definitions the foreach paths
// resolve through FindType (the type-cache model: one shared-managed instance per
// code).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> ienumerable;
    std::shared_ptr<LookupTypeDefinition> ienumerator;
    std::shared_ptr<LookupTypeDefinition> ienumerableOfT;
    std::shared_ptr<LookupTypeDefinition> ienumeratorOfT;
    std::shared_ptr<LookupTypeDefinition> voidDef;

    Fixture()
        : objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          ienumerable(MakeDef("IEnumerable", KnownTypeCode::IEnumerable, TypeKind::Interface)),
          ienumerator(MakeDef("IEnumerator", KnownTypeCode::IEnumerator, TypeKind::Interface)),
          ienumerableOfT(MakeDef("IEnumerable", KnownTypeCode::IEnumerableOfT, TypeKind::Interface)),
          ienumeratorOfT(MakeDef("IEnumerator", KnownTypeCode::IEnumeratorOfT, TypeKind::Interface)),
          voidDef(MakeDef("Void", KnownTypeCode::Void, TypeKind::Struct))
    {
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
        compilation.RegisterKnownType(KnownTypeCode::IEnumerable, ienumerable.get());
        compilation.RegisterKnownType(KnownTypeCode::IEnumerator, ienumerator.get());
        compilation.RegisterKnownType(KnownTypeCode::IEnumerableOfT, ienumerableOfT.get());
        compilation.RegisterKnownType(KnownTypeCode::IEnumeratorOfT, ienumeratorOfT.get());
        compilation.RegisterKnownType(KnownTypeCode::Void, voidDef.get());
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    // A member-host definition (the configurable GetMethods / GetProperties stub).
    std::shared_ptr<MemberHostType> MakeHost(const std::string& name, TypeKind kind) const
    {
        return std::make_shared<MemberHostType>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    }

    // A method with the given name, a make_shared'd return type (the ComputeType
    // shared_from_this trap), and no parameters. The method is shared-managed and
    // returned for the test scope to hold (the host stores a non-owning pointer).
    std::shared_ptr<LookupMethod> MakeMethod(const std::string& name, ITypePtr returnType) const
    {
        auto method = std::make_shared<LookupMethod>(name, compilation);
        method->SetReturnType(std::move(returnType));
        return method;
    }

    // A plain expression over the given type.
    static std::shared_ptr<ResolveResult> MakeExpression(ITypePtr type)
    {
        return std::make_shared<ResolveResult>(std::move(type));
    }
};

} // namespace

// ===========================================================================
// ResolveForeach -- the array / dynamic arm
// ===========================================================================

// An array-typed expression takes the non-generic IEnumerable pattern: the
// collection / enumerator types are the REGISTERED interface definitions
// (pointer identity, the type-cache model), the element type is the array's
// element INSTANCE (identity), and the registered bare IEnumerator stub has no
// Current / MoveNext members (null property / method).
TEST(CSharpResolverForeachTest, ArrayExpressionUsesNonGenericIEnumerablePattern)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto expression = Fixture::MakeExpression(std::make_shared<ArrayType>(f.int32));

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->CollectionType(), f.ienumerable.get());
    EXPECT_EQ(&result->EnumeratorType(), f.ienumerator.get());
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    EXPECT_NE(result->GetEnumeratorCall(), nullptr);
    EXPECT_EQ(result->CurrentProperty(), nullptr);
    EXPECT_EQ(result->MoveNextMethod(), nullptr);
    // The voidType forwarded to the ResolveResult base.
    EXPECT_EQ(&result->Type(), f.voidDef.get());
}

// A dynamic-typed expression takes the same pattern with the dynamic element type.
TEST(CSharpResolverForeachTest, DynamicExpressionUsesNonGenericIEnumerablePattern)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto expression = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, true));

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->CollectionType(), f.ienumerable.get());
    EXPECT_EQ(&result->EnumeratorType(), f.ienumerator.get());
    EXPECT_EQ(result->ElementType().Kind(), TypeKind::Dynamic);
    EXPECT_NE(result->GetEnumeratorCall(), nullptr);
    EXPECT_EQ(result->CurrentProperty(), nullptr);
    EXPECT_EQ(result->MoveNextMethod(), nullptr);
}

// ===========================================================================
// ResolveForeach -- the enumerator pattern
// ===========================================================================

// The flagship: a public, non-static, applicable GetEnumerator on the collection
// fires the enumerator pattern. The collection type is the expression's own
// type, the enumerator type is the invocation's result type (the method's
// return type), the element type is the Current property's return type, and the
// CurrentProperty / MoveNextMethod pointers are the looked-up stubs. The
// GetEnumeratorCall is the overload-resolution CreateResolveResult composition.
TEST(CSharpResolverForeachTest, PublicInstanceGetEnumeratorFiresEnumeratorPattern)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto enumerator = f.MakeHost("Enumerator", TypeKind::Interface);
    auto currentProperty = std::make_shared<TestProperty>("Current", f.int32, f.compilation);
    auto moveNext = f.MakeMethod("MoveNext", f.objectDef);
    enumerator->SetProperties({ currentProperty.get() });
    enumerator->SetMethods({ moveNext.get() });

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto getEnumerator = f.MakeMethod("GetEnumerator", enumerator);
    collection->SetMethods({ getEnumerator.get() });

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->CollectionType(), collection.get());
    EXPECT_EQ(&result->EnumeratorType(), enumerator.get());
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    EXPECT_EQ(result->CurrentProperty(), currentProperty.get());
    EXPECT_EQ(result->MoveNextMethod(), moveNext.get());
    // The GetEnumeratorCall is the overload-resolution CreateResolveResult
    // composition (a CSharpInvocationResolveResult).
    EXPECT_NE(
        dynamic_cast<const CSharpInvocationResolveResult*>(result->GetEnumeratorCall()),
        nullptr);
}

// A STATIC GetEnumerator fails the guard (the C# `!or.BestCandidate.IsStatic`)
// and falls back to the interface pattern: the generic arm builds the
// IEnumerable<T> ParameterizedType over the registered open-generic definition
// with the base-closure element type.
TEST(CSharpResolverForeachTest, StaticGetEnumeratorFallsBackToInterface)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto getEnumerator = f.MakeMethod("GetEnumerator", f.ienumerator);
    getEnumerator->SetStatic(true);
    collection->SetMethods({ getEnumerator.get() });
    collection->AddDirectBaseType(std::make_shared<ParameterizedType>(
        f.ienumerableOfT, std::vector<ITypePtr>{ f.int32 }));

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    const ParameterizedType* collectionPT =
        dynamic_cast<const ParameterizedType*>(&result->CollectionType());
    ASSERT_NE(collectionPT, nullptr);
    EXPECT_EQ(collectionPT->GenericType().get(), f.ienumerableOfT.get());
    EXPECT_EQ(collectionPT->GetTypeArgument(0).get(), f.int32.get());
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    const ParameterizedType* enumeratorPT =
        dynamic_cast<const ParameterizedType*>(&result->EnumeratorType());
    ASSERT_NE(enumeratorPT, nullptr);
    EXPECT_EQ(enumeratorPT->GenericType().get(), f.ienumeratorOfT.get());
}

// A non-public GetEnumerator fails the guard (the C#
// `or.BestCandidate.Accessibility == Accessibility.Public`) and falls back.
TEST(CSharpResolverForeachTest, NonPublicGetEnumeratorFallsBackToInterface)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto getEnumerator = f.MakeMethod("GetEnumerator", f.ienumerator);
    getEnumerator->SetAccessibility(Accessibility::Internal);
    collection->SetMethods({ getEnumerator.get() });
    collection->AddDirectBaseType(f.ienumerable);

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    // The non-generic arm: the registered plain interfaces (identity) and the
    // Object element type (the FindType(Object) registered instance).
    EXPECT_EQ(&result->CollectionType(), f.ienumerable.get());
    EXPECT_EQ(&result->EnumeratorType(), f.ienumerator.get());
    EXPECT_EQ(&result->ElementType(), f.objectDef.get());
}

// An INAPPLICABLE GetEnumerator (a required parameter no argument fills) fails
// the guard (the C# `or.FoundApplicableCandidate`) and falls back to the
// interface pattern.
TEST(CSharpResolverForeachTest, InapplicableGetEnumeratorFallsBackToInterface)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto getEnumerator = f.MakeMethod("GetEnumerator", f.ienumerator);
    // One required parameter; ResolveForeach resolves the group with NO
    // arguments, so the candidate is inapplicable.
    auto parameter = std::make_shared<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
        f.int32, "x");
    getEnumerator->SetParameters({ parameter.get() });
    collection->SetMethods({ getEnumerator.get() });
    collection->AddDirectBaseType(std::make_shared<ParameterizedType>(
        f.ienumerableOfT, std::vector<ITypePtr>{ f.int32 }));

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    EXPECT_NE(dynamic_cast<const ParameterizedType*>(&result->CollectionType()), nullptr);
}

// An AMBIGUOUS GetEnumerator (two identical-signature applicable methods) fails
// the guard (the C# `!or.IsAmbiguous`) and falls back.
TEST(CSharpResolverForeachTest, AmbiguousGetEnumeratorFallsBackToInterface)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto getEnumerator1 = f.MakeMethod("GetEnumerator", f.ienumerator);
    auto getEnumerator2 = f.MakeMethod("GetEnumerator", f.ienumerator);
    collection->SetMethods({ getEnumerator1.get(), getEnumerator2.get() });
    collection->AddDirectBaseType(std::make_shared<ParameterizedType>(
        f.ienumerableOfT, std::vector<ITypePtr>{ f.int32 }));

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    EXPECT_NE(dynamic_cast<const ParameterizedType*>(&result->CollectionType()), nullptr);
}

// A collection with NO GetEnumerator method (the lookup finds no method group)
// takes the direct interface-pattern arm.
TEST(CSharpResolverForeachTest, NoGetEnumeratorMethodFallsBackToInterface)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    collection->AddDirectBaseType(std::make_shared<ParameterizedType>(
        f.ienumerableOfT, std::vector<ITypePtr>{ f.int32 }));

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->ElementType(), f.int32.get());
    const ParameterizedType* collectionPT =
        dynamic_cast<const ParameterizedType*>(&result->CollectionType());
    ASSERT_NE(collectionPT, nullptr);
    EXPECT_EQ(collectionPT->GenericType().get(), f.ienumerableOfT.get());
    EXPECT_NE(result->GetEnumeratorCall(), nullptr);
}

// A collection implementing only the NON-GENERIC IEnumerable falls back with
// isGeneric == false: the registered plain interfaces and the Object element.
TEST(CSharpResolverForeachTest, NonGenericBaseFallsBackToPlainInterfaces)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    collection->AddDirectBaseType(f.ienumerable);

    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(&result->CollectionType(), f.ienumerable.get());
    EXPECT_EQ(&result->EnumeratorType(), f.ienumerator.get());
    EXPECT_EQ(&result->ElementType(), f.objectDef.get());
}

// A collection with no GetEnumerator and no IEnumerable base takes the neither
// arm: the UnknownType null object for all three types.
TEST(CSharpResolverForeachTest, UnknownCollectionYieldsUnknownTypes)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    auto expression = Fixture::MakeExpression(collection);

    std::shared_ptr<ForEachResolveResult> result = resolver.ResolveForeach(expression);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->CollectionType().Kind(), TypeKind::Unknown);
    EXPECT_EQ(result->EnumeratorType().Kind(), TypeKind::Unknown);
    EXPECT_EQ(result->ElementType().Kind(), TypeKind::Unknown);
    EXPECT_NE(result->GetEnumeratorCall(), nullptr);
}

// ===========================================================================
// CheckForEnumerableInterface (the private helper, direct)
// ===========================================================================

// The direct out-param contract of the generic arm: the collection / enumerator
// are freshly-built ParameterizedTypes over the registered open-generic
// definitions carrying the base-closure element type, and the invocation chain
// produces a non-null GetEnumerator result.
TEST(CSharpResolverForeachTest, CheckForEnumerableInterfaceGenericArmBuildsParameterizedTypes)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto collection = f.MakeHost("Collection", TypeKind::Class);
    collection->AddDirectBaseType(std::make_shared<ParameterizedType>(
        f.ienumerableOfT, std::vector<ITypePtr>{ f.int32 }));
    auto expression = Fixture::MakeExpression(collection);

    ITypePtr collectionType, enumeratorType, elementType;
    std::shared_ptr<ResolveResult> getEnumeratorInvocation;
    resolver.CheckForEnumerableInterface(expression, collectionType, enumeratorType,
                                         elementType, getEnumeratorInvocation);

    ASSERT_NE(collectionType, nullptr);
    ASSERT_NE(enumeratorType, nullptr);
    ASSERT_NE(elementType, nullptr);
    EXPECT_EQ(elementType.get(), f.int32.get());
    const ParameterizedType* collectionPT =
        dynamic_cast<const ParameterizedType*>(collectionType.get());
    ASSERT_NE(collectionPT, nullptr);
    EXPECT_EQ(collectionPT->GenericType().get(), f.ienumerableOfT.get());
    EXPECT_EQ(collectionPT->GetTypeArgument(0).get(), f.int32.get());
    const ParameterizedType* enumeratorPT =
        dynamic_cast<const ParameterizedType*>(enumeratorType.get());
    ASSERT_NE(enumeratorPT, nullptr);
    EXPECT_EQ(enumeratorPT->GenericType().get(), f.ienumeratorOfT.get());
    EXPECT_NE(getEnumeratorInvocation, nullptr);
}
