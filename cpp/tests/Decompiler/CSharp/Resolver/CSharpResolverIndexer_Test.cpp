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

// Tests for the CSharpResolver ResolveIndexer region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 2456-2522,
// C# 4.0 spec sections 7.6.6.1 / 18.5.3 / 7.6.6.2): the public ResolveIndexer
// entry + the private AdjustArrayAccessArguments helper (widened to public for
// direct TDD).
//
// The load-bearing cruxes:
//  (a) the DYNAMIC target arm -- a DynamicInvocationResolveResult with
//      DynamicInvocationType::Indexing whose Arguments carry the
//      AddArgumentNamesIfNecessary named-argument wrap;
//  (b) the ARRAY / POINTER target arm -- an ArrayAccessResolveResult whose
//      ElementType is the array's / pointer's element INSTANCE (pointer
//      identity) and whose Indexes are the ADJUSTED arguments (the
//      int32-identity passthrough, the int16->int32 widening wrap, the
//      inconvertible argument's Convert-to-int32 fallback, and the constant
//      re-fold through the target);
//  (c) the INDEXER ACCESS -- LookupIndexers supplies the candidate lists, the
//      applicable-candidate OVERLOAD SELECTION composes the
//      CSharpInvocationResolveResult over the looked-up property member, no
//      candidate yields the ErrorResult singleton;
//  (d) the DYNAMIC-ARGUMENTS sub-arm -- more than one applicable indexer makes
//      the invocation dynamic (Indexing); exactly one applicable indexer falls
//      through to the normal resolution.
//
// LIFETIME DISCIPLINE: the resolution paths reach the per-compilation
// CSharpConversions instance (the resolver's Conversions()), so every test builds
// its resolver over a FRESH per-test LookupCompilation (CSharpConversions::Get
// caches its instance on that compilation's CacheManager; the instance and its
// cache die WITH the test -- the iteration-109 discipline). Every FindType-resolved
// code the paths touch (Int32 / UInt32 / Int64 / UInt64 / Object) must be
// registered with a make_shared'd definition (the bad_weak_ptr trap: an unregistered
// code falls back to the compilation's non-shared unknownType_ stub whose
// shared_from_this throws). Every member whose return type flows into a result
// type needs a make_shared'd return type (the ComputeType shared_from_this trap).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/Semantics/ArrayAccessResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
using ILSpy::Decompiler::Semantics::ArrayAccessResolveResult;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::NamedArgumentResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` subclass whose `GetProperties` return the configured
// list, applying the filter faithfully (the D533 MethodHostType / the foreach
// MemberHostType precedent, trimmed to the property family LookupIndexers reads).
class PropertyHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetProperties(std::vector<const TS::IProperty*> p) { properties_ = std::move(p); }

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
    std::vector<const TS::IProperty*> properties_;
};

// A minimal INDEXER `IProperty` (the foreach TestProperty precedent): `IsIndexer`
// true, `SymbolKind::Indexer` (the C# `AbstractProperty.SymbolKind` -- an indexer
// reports the Indexer kind, which is what the LookupIndexers-adjacent member
// filter keys on), configurable parameters and return type. The return type must
// be a make_shared'd instance (the ComputeType shared_from_this trap).
class IndexerProperty : public IProperty {
public:
    IndexerProperty(std::string name, ITypePtr returnType,
                    std::vector<std::shared_ptr<const IParameter>> parameters,
                    const TS::ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          parameters_(std::move(parameters)), compilation_(compilation)
    {
    }

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer;
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
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
    {
        std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> r;
        for (const std::shared_ptr<const IParameter>& p : parameters_)
            r.push_back(p.get());
        return r;
    }
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    bool IsIndexer() const override { return true; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Getter() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Setter() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    std::vector<std::shared_ptr<const IParameter>> parameters_;
    const TS::ICompilation& compilation_;
    mutable ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution identitySubst_{
        std::nullopt, std::nullopt};
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the
// file header) plus the registered known-type definitions the indexer paths resolve
// through FindType (the type-cache model: one shared-managed instance per code).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> uint32;
    std::shared_ptr<LookupTypeDefinition> int64;
    std::shared_ptr<LookupTypeDefinition> uint64;
    std::shared_ptr<LookupTypeDefinition> int16;
    std::shared_ptr<LookupTypeDefinition> stringDef;

    Fixture()
        : objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          uint32(MakeDef("UInt32", KnownTypeCode::UInt32, TypeKind::Struct)),
          int64(MakeDef("Int64", KnownTypeCode::Int64, TypeKind::Struct)),
          uint64(MakeDef("UInt64", KnownTypeCode::UInt64, TypeKind::Struct)),
          int16(MakeDef("Int16", KnownTypeCode::Int16, TypeKind::Struct)),
          stringDef(MakeDef("String", KnownTypeCode::String, TypeKind::Class))
    {
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
        compilation.RegisterKnownType(KnownTypeCode::UInt32, uint32.get());
        compilation.RegisterKnownType(KnownTypeCode::Int64, int64.get());
        compilation.RegisterKnownType(KnownTypeCode::UInt64, uint64.get());
        compilation.RegisterKnownType(KnownTypeCode::Int16, int16.get());
        compilation.RegisterKnownType(KnownTypeCode::String, stringDef.get());
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    // A property-host definition (the configurable GetProperties stub).
    std::shared_ptr<PropertyHostType> MakeHost(const std::string& name, TypeKind kind) const
    {
        return std::make_shared<PropertyHostType>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    }

    // An indexer property with the given parameter type and a make_shared'd return
    // type (the ComputeType shared_from_this trap). The parameter list holds one
    // DefaultParameter over the given type.
    std::shared_ptr<IndexerProperty> MakeIndexer(ITypePtr parameterType,
                                                  ITypePtr returnType) const
    {
        std::vector<std::shared_ptr<const IParameter>> parameters;
        parameters.push_back(std::make_shared<
            ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
            parameterType, "index"));
        return std::make_shared<IndexerProperty>(
            "Item", std::move(returnType), std::move(parameters), compilation);
    }

    // A plain expression over the given type.
    static std::shared_ptr<ResolveResult> MakeExpression(ITypePtr type)
    {
        return std::make_shared<ResolveResult>(std::move(type));
    }
};

} // namespace

// ===========================================================================
// ResolveIndexer -- the dynamic target arm
// ===========================================================================

// A dynamic-typed target is a dynamic invocation with DynamicInvocationType::
// Indexing, and the argumentNames entry wraps the argument in a
// NamedArgumentResolveResult (the AddArgumentNamesIfNecessary composition).
TEST(CSharpResolverIndexerTest, DynamicTargetYieldsIndexingWithNamedArgumentWrap)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto target = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true));
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result = resolver.ResolveIndexer(
        target, { argument }, std::vector<std::string>{ "index" });

    auto* dynamic = dynamic_cast<const DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dynamic, nullptr);
    EXPECT_EQ(dynamic->InvocationType(),
              ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType::Indexing);
    ASSERT_EQ(dynamic->Arguments().size(), 1u);
    const auto* named =
        dynamic_cast<const NamedArgumentResolveResult*>(dynamic->Arguments()[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "index");
}

// ===========================================================================
// ResolveIndexer -- the array / pointer target arm
// ===========================================================================

// An array-typed target is an ArrayAccessResolveResult over the array's ELEMENT
// instance (pointer identity), with the int32-identity argument passing through
// the adjustment unchanged (TryConvert identity leaves the result unwrapped).
TEST(CSharpResolverIndexerTest, ArrayTargetYieldsArrayAccessWithElementIdentity)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto target = Fixture::MakeExpression(std::make_shared<ArrayType>(f.int32));
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* access = dynamic_cast<const ArrayAccessResolveResult*>(result.get());
    ASSERT_NE(access, nullptr);
    EXPECT_EQ(&access->Type(), f.int32.get());
    ASSERT_EQ(access->Indexes().size(), 1u);
    EXPECT_EQ(access->Indexes()[0].get(), argument.get());
    EXPECT_EQ(access->Array(), target.get());
}

// A pointer-typed target takes the same arm (section 18.5.3 pointer element
// access): the element type is the pointer's element instance.
TEST(CSharpResolverIndexerTest, PointerTargetYieldsArrayAccess)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto target = Fixture::MakeExpression(std::make_shared<PointerType>(f.int32));
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* access = dynamic_cast<const ArrayAccessResolveResult*>(result.get());
    ASSERT_NE(access, nullptr);
    EXPECT_EQ(&access->Type(), f.int32.get());
    ASSERT_EQ(access->Indexes().size(), 1u);
    EXPECT_EQ(&access->Indexes()[0]->Type(), f.int32.get());
}

// An int16-typed index argument is ADJUSTED: the TryConvert(int32) widening
// succeeds and the index lands as a ConversionResolveResult over the registered
// Int32 (observed through the result -- the by-value-arguments convention).
TEST(CSharpResolverIndexerTest, ArrayTargetAdjustsWideningIndexArgument)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto target = Fixture::MakeExpression(std::make_shared<ArrayType>(f.int32));
    auto argument = Fixture::MakeExpression(f.int16);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* access = dynamic_cast<const ArrayAccessResolveResult*>(result.get());
    ASSERT_NE(access, nullptr);
    ASSERT_EQ(access->Indexes().size(), 1u);
    auto* converted =
        dynamic_cast<const ConversionResolveResult*>(access->Indexes()[0].get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), f.int32.get());
    EXPECT_TRUE(converted->ConversionProperty()->IsNumericConversion());
}

// An inconvertible index argument (string) falls back to the Convert-to-int32
// wrap: no TryConvert applies, so the argument is Convert-ed under Conversion.
// None -- a ConversionResolveResult over the registered Int32 carrying the None
// conversion (the error-preserving wrap).
TEST(CSharpResolverIndexerTest, ArrayTargetInconvertibleIndexFallsBackToInt32)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto target = Fixture::MakeExpression(std::make_shared<ArrayType>(f.int32));
    auto argument = Fixture::MakeExpression(f.stringDef);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* access = dynamic_cast<const ArrayAccessResolveResult*>(result.get());
    ASSERT_NE(access, nullptr);
    ASSERT_EQ(access->Indexes().size(), 1u);
    auto* converted =
        dynamic_cast<const ConversionResolveResult*>(access->Indexes()[0].get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), f.int32.get());
    EXPECT_EQ(converted->ConversionProperty(),
              ILSpy::Decompiler::Semantics::Conversions::None().get());
}

// ===========================================================================
// ResolveIndexer -- the indexer access
// ===========================================================================

// The flagship: a host type with an int32 indexer composes the
// CSharpInvocationResolveResult over the LOOKED-UP property member (pointer
// identity), with the target as the target result and the property's return
// type as the result type.
TEST(CSharpResolverIndexerTest, IndexerAccessComposesInvocationOverLookedUpIndexer)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host", TypeKind::Class);
    auto indexer = f.MakeIndexer(f.int32, f.int32);
    host->SetProperties({ indexer.get() });
    auto target = Fixture::MakeExpression(host);
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), indexer.get());
    EXPECT_EQ(invocation->TargetResult(), target.get());
    EXPECT_EQ(&invocation->Type(), f.int32.get());
    ASSERT_EQ(invocation->Arguments().size(), 1u);
}

// Two indexers on the host (int32 and string parameters) with an int32 argument:
// only the int32 indexer is applicable, and the overload selection composes over
// it (the string-parameter candidate is inapplicable -- TooManyPositional-
// Arguments-wise irrelevant here, the type mismatch loses).
TEST(CSharpResolverIndexerTest, IndexerAccessSelectsApplicableIndexer)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host", TypeKind::Class);
    auto intIndexer = f.MakeIndexer(f.int32, f.int32);
    auto stringIndexer = f.MakeIndexer(f.stringDef, f.stringDef);
    host->SetProperties({ intIndexer.get(), stringIndexer.get() });
    auto target = Fixture::MakeExpression(host);
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), intIndexer.get());
}

// A host with no indexers yields the ErrorResult singleton (pointer identity
// against ErrorResolveResult::UnknownError).
TEST(CSharpResolverIndexerTest, IndexerAccessWithoutIndexersYieldsErrorResult)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host", TypeKind::Class);
    auto target = Fixture::MakeExpression(host);
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(result.get(),
              &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError());
}

// ===========================================================================
// ResolveIndexer -- the dynamic-arguments sub-arm
// ===========================================================================

// A dynamic argument with MORE THAN ONE applicable indexer (the dynamic
// conversion makes both the int32 and the int64 indexers applicable) is a
// dynamic invocation with DynamicInvocationType::Indexing.
TEST(CSharpResolverIndexerTest, DynamicArgumentWithMultipleApplicableIndexersIsDynamic)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host", TypeKind::Class);
    auto intIndexer = f.MakeIndexer(f.int32, f.int32);
    auto longIndexer = f.MakeIndexer(f.int64, f.int64);
    host->SetProperties({ intIndexer.get(), longIndexer.get() });
    auto target = Fixture::MakeExpression(host);
    auto argument = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true));

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* dynamic = dynamic_cast<const DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dynamic, nullptr);
    EXPECT_EQ(dynamic->InvocationType(),
              ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType::Indexing);
}

// A dynamic argument with EXACTLY ONE applicable indexer falls through to the
// normal resolution: the composition is a CSharpInvocationResolveResult over
// that indexer (the dynamic conversion applies, the best candidate exists).
TEST(CSharpResolverIndexerTest, DynamicArgumentWithSingleApplicableIndexerComposes)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host", TypeKind::Class);
    auto intIndexer = f.MakeIndexer(f.int32, f.int32);
    host->SetProperties({ intIndexer.get() });
    auto target = Fixture::MakeExpression(host);
    auto argument = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true));

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveIndexer(target, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), intIndexer.get());
}

// ===========================================================================
// AdjustArrayAccessArguments -- the direct helper surface
// ===========================================================================

// An already-int32 argument passes through unchanged (pointer identity -- the
// identity TryConvert leaves the result unwrapped).
TEST(CSharpResolverIndexerTest, AdjustArrayAccessArgumentsIdentityStays)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto argument = Fixture::MakeExpression(f.int32);
    std::vector<std::shared_ptr<ResolveResult>> arguments{ argument };

    resolver.AdjustArrayAccessArguments(arguments);

    ASSERT_EQ(arguments.size(), 1u);
    EXPECT_EQ(arguments[0].get(), argument.get());
}

// An int16 argument is rebound to a ConversionResolveResult over the registered
// Int32 (the TryConvert widening).
TEST(CSharpResolverIndexerTest, AdjustArrayAccessArgumentsWideningRebinds)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto argument = Fixture::MakeExpression(f.int16);
    std::vector<std::shared_ptr<ResolveResult>> arguments{ argument };

    resolver.AdjustArrayAccessArguments(arguments);

    ASSERT_EQ(arguments.size(), 1u);
    EXPECT_NE(arguments[0].get(), argument.get());
    auto* converted =
        dynamic_cast<const ConversionResolveResult*>(arguments[0].get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), f.int32.get());
    EXPECT_TRUE(converted->ConversionProperty()->IsNumericConversion());
}

// A string argument matches none of the four targets and falls back to the
// Convert-to-int32 wrap under Conversion.None.
TEST(CSharpResolverIndexerTest, AdjustArrayAccessArgumentsInconvertibleFallsBackToInt32)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto argument = Fixture::MakeExpression(f.stringDef);
    std::vector<std::shared_ptr<ResolveResult>> arguments{ argument };

    resolver.AdjustArrayAccessArguments(arguments);

    ASSERT_EQ(arguments.size(), 1u);
    auto* converted =
        dynamic_cast<const ConversionResolveResult*>(arguments[0].get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), f.int32.get());
    EXPECT_EQ(converted->ConversionProperty(),
              ILSpy::Decompiler::Semantics::Conversions::None().get());
}

// A compile-time-constant int16 argument is CONSTANT-FOLDED through the target
// (the Convert constant-folding route): the adjusted argument is a
// ConstantResolveResult over the registered Int32 with the re-folded value.
TEST(CSharpResolverIndexerTest, AdjustArrayAccessArgumentsConstantFolds)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto argument = std::make_shared<ConstantResolveResult>(f.int16, std::int16_t{ 5 });
    std::vector<std::shared_ptr<ResolveResult>> arguments{ argument };

    resolver.AdjustArrayAccessArguments(arguments);

    ASSERT_EQ(arguments.size(), 1u);
    auto* constant = dynamic_cast<const ConstantResolveResult*>(arguments[0].get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&constant->Type(), f.int32.get());
    const std::int32_t* value = std::any_cast<std::int32_t>(&constant->ConstantValue());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 5);
}
