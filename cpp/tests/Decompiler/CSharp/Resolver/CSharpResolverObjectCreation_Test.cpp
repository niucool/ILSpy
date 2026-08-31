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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver ResolveObjectCreation region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 2539-2585):
// the public ResolveObjectCreation entry.
//
// The load-bearing cruxes:
//  (a) the DELEGATE arm -- a Delegate-kind target with exactly one argument whose
//      type resolves an Invoke method: the argument is re-wrapped as a
//      MethodGroupResolveResult over that invoke (the delegate-to-delegate
//      conversion routes through the method-group conversion machinery), so the
//      result is a ConversionResolveResult over the target delegate carrying a
//      VALID MethodGroupConversion;
//  (b) the DELEGATE arm without an invoke -- an argument whose type is not a
//      delegate converts AS-IS (no wrap): a ConversionResolveResult over the
//      target carrying the None conversion;
//  (c) the DELEGATE arm's arguments.Length == 1 guard -- two arguments skip the
//      arm and fall to the constructor scan (a delegate type has no constructors,
//      so the result is a FRESH ErrorResolveResult over the type, NOT the
//      UnknownError singleton);
//  (d) the CONSTRUCTOR scan -- the applicable constructor composes the
//      CSharpInvocationResolveResult with a NULL target (constructors have no
//      target result) and the constructor's return type as the result type; the
//      overload selection picks the applicable constructor;
//  (e) the INACCESSIBLE path -- an inaccessible constructor is still added (with
//      the Inaccessible additional error), so the composition carries the
//      Inaccessible bit; the allowProtectedAccess flag threads into
//      MemberLookup::IsAccessible (the constructor-initializer shape: a protected
//      constructor on a base type is accessible only with the flag);
//  (f) the DYNAMIC-ARGUMENTS sub-arm -- more than one applicable constructor makes
//      the creation a DynamicInvocationResolveResult with DynamicInvocationType::
//      ObjectCreation (the method group over the applicable constructors named
//      after the first, the named-wrapped arguments, and the initializer
//      statements); exactly one applicable constructor falls through to the normal
//      composition;
//  (g) the initializer statements thread through both the composition and the
//      dynamic arm.
//
// LIFETIME DISCIPLINE: the resolution paths reach the per-compilation
// CSharpConversions instance (the resolver's Conversions()), so every test builds
// its resolver over a FRESH per-test LookupCompilation (CSharpConversions::Get
// caches its instance on that compilation's CacheManager; the instance and its
// cache die WITH the test -- the iteration-109 discipline). Every method whose
// return type flows into a result type needs a make_shared'd return type (the
// ComputeType shared_from_this trap), and every method's parameters must be kept
// alive in the test scope (the method stores non-owning const IParameter* -- the
// iteration-50 lifetime convention).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
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
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::NamedArgumentResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` subclass whose `GetConstructors` returns the configured
// list, applying the filter faithfully (the D533 MethodHostType precedent, trimmed
// to the constructor family the object-creation scan reads).
class ConstructorHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetConstructors(std::vector<const IMethod*> c) { constructors_ = std::move(c); }

    std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::IgnoreInheritedMembers) const override
    {
        (void)options;
        if (!filter)
            return constructors_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : constructors_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> constructors_;
};

// A `LookupTypeDefinition` subclass whose `GetMethods` returns the configured list,
// applying the filter faithfully (the D533 MethodHostType precedent). Used for the
// DELEGATE types (TypeKind::Delegate with the `Invoke` method the
// `GetDelegateInvokeMethod` filter selects).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
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
};

// A shared-managed return type for the delegate `Invoke` methods (the D578
// CreateResolveResult learning: the LookupMethod stub's DEFAULT return type is an
// inline non-shared-managed KnownType member, and the delegate-compatibility return
// check visits it through the TypeErasure -> AcceptVisitor -> VisitChildren ->
// shared_from_this chain -- without a SetReturnType make_shared'd type the visit
// throws bad_weak_ptr). The single shared instance keeps both Invoke methods'
// return types identical.
ITypePtr RetType()
{
    static const ITypePtr ret = std::make_shared<KnownType>(KnownTypeCode::Object);
    return ret;
}

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the
// file header) plus the registered known-type definitions the object-creation paths
// resolve through FindType (the type-cache model: one shared-managed instance per
// code).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> int64;
    std::shared_ptr<LookupTypeDefinition> stringDef;

    Fixture()
        : objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          int64(MakeDef("Int64", KnownTypeCode::Int64, TypeKind::Struct)),
          stringDef(MakeDef("String", KnownTypeCode::String, TypeKind::Class))
    {
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
        compilation.RegisterKnownType(KnownTypeCode::Int64, int64.get());
        compilation.RegisterKnownType(KnownTypeCode::String, stringDef.get());
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    // A constructor-host definition (the configurable GetConstructors stub).
    std::shared_ptr<ConstructorHostType> MakeHost(const std::string& name) const
    {
        return std::make_shared<ConstructorHostType>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
            Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    }

    // A delegate definition (the configurable GetMethods stub, Kind=Delegate).
    std::shared_ptr<MethodHostType> MakeDelegate(const std::string& name) const
    {
        return std::make_shared<MethodHostType>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Delegate,
            Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    }

    // A one-parameter `LookupMethod` (a `.ctor` or an `Invoke`): the parameter's
    // owning handle is handed to the caller's `keep` vector (the method stores a
    // non-owning `const IParameter*` that must outlive the resolution -- the
    // iteration-50 lifetime convention), the return type is a make_shared'd instance
    // (the ComputeType shared_from_this trap), and the declaring type is wired for
    // the accessibility walk.
    std::shared_ptr<LookupMethod> MakeMethod(std::string name, ITypePtr parameterType,
                                             ITypePtr returnType,
                                             const LookupTypeDefinition* declaringType,
                                             std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>>& keep) const
    {
        auto method = std::make_shared<LookupMethod>(std::move(name), compilation);
        keep.push_back(std::make_shared<
            ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
            std::move(parameterType), "value"));
        method->SetParameters({ keep.back().get() });
        method->SetReturnType(std::move(returnType));
        method->SetDeclaringTypeDefinition(declaringType);
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
// ResolveObjectCreation -- the delegate arm
// ===========================================================================

// The flagship: a Delegate-kind target with one argument whose type is ALSO a
// delegate (with an Invoke method): the argument is re-wrapped as a
// MethodGroupResolveResult over its delegate's Invoke, and the conversion to the
// target routes through the method-group conversion machinery -- a
// ConversionResolveResult over the TARGET delegate carrying a VALID
// MethodGroupConversion.
TEST(CSharpResolverObjectCreationTest, DelegateArmWrapsDelegateTypedArgumentAsMethodGroup)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto sourceInvoke = f.MakeMethod("Invoke", f.stringDef, RetType(), nullptr, keep);
    auto sourceDelegate = f.MakeDelegate("D1");
    sourceDelegate->SetMethods({ sourceInvoke.get() });

    auto targetInvoke = f.MakeMethod("Invoke", f.stringDef, RetType(), nullptr, keep);
    auto targetDelegate = f.MakeDelegate("D2");
    targetDelegate->SetMethods({ targetInvoke.get() });

    auto input = Fixture::MakeExpression(sourceDelegate);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*targetDelegate, { input });

    auto* converted = dynamic_cast<const ConversionResolveResult*>(result.get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), targetDelegate.get());
    EXPECT_TRUE(converted->ConversionProperty()->IsMethodGroupConversion());
    EXPECT_TRUE(converted->ConversionProperty()->IsValid());
}

// An argument whose type is NOT a delegate (no Invoke method resolves): the arm is
// entered but no wrap happens -- the argument converts AS-IS, and with no implicit
// int-to-delegate conversion the result is a ConversionResolveResult over the
// target carrying the None conversion.
TEST(CSharpResolverObjectCreationTest, DelegateArmWithoutInvokeConvertsArgumentAsIs)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto targetInvoke = f.MakeMethod("Invoke", f.stringDef, RetType(), nullptr, keep);
    auto targetDelegate = f.MakeDelegate("D2");
    targetDelegate->SetMethods({ targetInvoke.get() });

    auto input = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*targetDelegate, { input });

    auto* converted = dynamic_cast<const ConversionResolveResult*>(result.get());
    ASSERT_NE(converted, nullptr);
    EXPECT_EQ(&converted->Type(), targetDelegate.get());
    EXPECT_EQ(converted->ConversionProperty(),
              ILSpy::Decompiler::Semantics::Conversions::None().get());
}

// TWO arguments skip the delegate arm (the arguments.Length == 1 guard) and fall to
// the constructor scan: a delegate type has no constructors, so the result is a
// FRESH ErrorResolveResult over the creation type (NOT the UnknownError singleton).
TEST(CSharpResolverObjectCreationTest, DelegateArmSkippedWithTwoArguments)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto targetDelegate = f.MakeDelegate("D2");
    auto a1 = Fixture::MakeExpression(f.int32);
    auto a2 = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*targetDelegate, { a1, a2 });

    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(&result->Type(), targetDelegate.get());
    EXPECT_NE(result.get(),
              &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError());
}

// ===========================================================================
// ResolveObjectCreation -- the constructor scan
// ===========================================================================

// The flagship: an applicable constructor composes the CSharpInvocationResolveResult
// with a NULL target (constructors have no target result), the constructor's return
// type as the result type, and no resolution errors.
TEST(CSharpResolverObjectCreationTest, ApplicableCtorComposesInvocationWithNullTarget)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto ctor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    host->SetConstructors({ ctor.get() });
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), ctor.get());
    EXPECT_EQ(invocation->TargetResult(), nullptr);
    EXPECT_EQ(&invocation->Type(), host.get());
    ASSERT_EQ(invocation->Arguments().size(), 1u);
    EXPECT_EQ(invocation->OverloadResolutionErrors(), OverloadResolutionErrors::None);
}

// Two constructors (int32 and string parameters) with an int32 argument: only the
// int32 constructor is applicable, and the overload selection composes over it.
TEST(CSharpResolverObjectCreationTest, OverloadSelectionBetweenApplicableCtors)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto intCtor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    auto stringCtor = f.MakeMethod(".ctor", f.stringDef, host, host.get(), keep);
    host->SetConstructors({ intCtor.get(), stringCtor.get() });
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), intCtor.get());
}

// A PRIVATE constructor is inaccessible (the resolver has no current type
// definition): it is still added -- with the Inaccessible additional error -- so
// the composition carries the Inaccessible bit.
TEST(CSharpResolverObjectCreationTest, InaccessibleCtorCarriesInaccessibleError)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto ctor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    ctor->SetAccessibility(Accessibility::Private);
    host->SetConstructors({ ctor.get() });
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), ctor.get());
    EXPECT_TRUE((invocation->OverloadResolutionErrors() & OverloadResolutionErrors::Inaccessible)
                != OverloadResolutionErrors::None);
}

// The allowProtectedAccess flag crux (the constructor-initializer shape): a
// PROTECTED constructor on a base type is inaccessible from the derived context
// without the flag (the derived type is not the declaring type and the
// base-reference allowance is off), and accessible with it (the derived type's
// base-type walk finds the declaring type).
TEST(CSharpResolverObjectCreationTest, ProtectedCtorAccessibleOnlyWithAllowProtectedAccess)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Base");
    auto ctor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    ctor->SetAccessibility(Accessibility::Protected);
    host->SetConstructors({ ctor.get() });

    auto derived = f.MakeDef("Derived", KnownTypeCode::None, TypeKind::Class);
    derived->AddDirectBaseType(host);
    auto derivedResolver = resolver.WithCurrentTypeDefinition(derived.get());
    auto argument = Fixture::MakeExpression(f.int32);

    // allowProtectedAccess = false (the default): the protected constructor is
    // inaccessible from the derived context.
    std::shared_ptr<ResolveResult> inaccessible =
        derivedResolver->ResolveObjectCreation(*host, { argument });
    auto* inv1 = dynamic_cast<const CSharpInvocationResolveResult*>(inaccessible.get());
    ASSERT_NE(inv1, nullptr);
    EXPECT_TRUE((inv1->OverloadResolutionErrors() & OverloadResolutionErrors::Inaccessible)
                != OverloadResolutionErrors::None);

    // allowProtectedAccess = true: the derived type's base-type walk finds the
    // declaring type, so the constructor is added plainly.
    std::shared_ptr<ResolveResult> accessible = derivedResolver->ResolveObjectCreation(
        *host, { argument }, std::nullopt, /*allowProtectedAccess*/ true);
    auto* inv2 = dynamic_cast<const CSharpInvocationResolveResult*>(accessible.get());
    ASSERT_NE(inv2, nullptr);
    EXPECT_EQ(inv2->OverloadResolutionErrors(), OverloadResolutionErrors::None);
}

// A host with NO constructors yields a FRESH ErrorResolveResult over the creation
// type (pointer-distinct from the UnknownError singleton).
TEST(CSharpResolverObjectCreationTest, NoCtorsYieldsFreshErrorResolveResultOverTheType)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto host = f.MakeHost("Host");
    auto argument = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument });

    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(&result->Type(), host.get());
    EXPECT_NE(result.get(),
              &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError());
}

// The initializer statements thread through the composition (the
// CSharpInvocationResolveResult carries them).
TEST(CSharpResolverObjectCreationTest, InitializerStatementsThreadThroughTheComposition)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto ctor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    host->SetConstructors({ ctor.get() });
    auto argument = Fixture::MakeExpression(f.int32);
    auto initializer = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument }, std::nullopt, false,
                                       { initializer });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    ASSERT_EQ(invocation->InitializerStatements().size(), 1u);
    EXPECT_EQ(invocation->InitializerStatements()[0].get(), initializer.get());
}

// ===========================================================================
// ResolveObjectCreation -- the dynamic-arguments sub-arm
// ===========================================================================

// A dynamic argument with MORE THAN ONE applicable constructor (the dynamic
// conversion makes both the int32 and int64 constructors applicable) is a dynamic
// invocation with DynamicInvocationType::ObjectCreation: the method group over the
// applicable constructors (named after the FIRST, both in the single
// declaring-type bucket, a null target), the named-wrapped arguments, and the
// initializer statements.
TEST(CSharpResolverObjectCreationTest, DynamicArgumentWithMultipleApplicableCtorsIsDynamic)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto intCtor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    auto longCtor = f.MakeMethod(".ctor", f.int64, host, host.get(), keep);
    host->SetConstructors({ intCtor.get(), longCtor.get() });
    auto argument = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true));
    auto initializer = Fixture::MakeExpression(f.int32);

    std::shared_ptr<ResolveResult> result = resolver.ResolveObjectCreation(
        *host, { argument }, std::vector<std::string>{ "value" }, false, { initializer });

    auto* dynamic = dynamic_cast<const DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dynamic, nullptr);
    EXPECT_EQ(dynamic->InvocationType(), DynamicInvocationType::ObjectCreation);
    auto* group = dynamic_cast<const MethodGroupResolveResult*>(dynamic->Target());
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->MethodName(), ".ctor");
    EXPECT_EQ(group->TargetResult(), nullptr);
    ASSERT_EQ(group->MethodsGroupedByDeclaringType().size(), 1u);
    ASSERT_EQ(group->MethodsGroupedByDeclaringType()[0].size(), 2u);
    EXPECT_EQ(group->MethodsGroupedByDeclaringType()[0][0],
              static_cast<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(intCtor.get()));
    EXPECT_EQ(group->MethodsGroupedByDeclaringType()[0][1],
              static_cast<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(longCtor.get()));
    EXPECT_EQ(&group->MethodsGroupedByDeclaringType()[0].DeclaringType(), host.get());
    // The named-argument wrap and the initializer statements thread through.
    ASSERT_EQ(dynamic->Arguments().size(), 1u);
    const auto* named =
        dynamic_cast<const NamedArgumentResolveResult*>(dynamic->Arguments()[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "value");
    ASSERT_EQ(dynamic->InitializerStatements().size(), 1u);
    EXPECT_EQ(dynamic->InitializerStatements()[0].get(), initializer.get());
}

// A dynamic argument with EXACTLY ONE applicable constructor falls through to the
// normal resolution: the composition is a CSharpInvocationResolveResult over that
// constructor (the dynamic conversion applies, the best candidate exists).
TEST(CSharpResolverObjectCreationTest, DynamicArgumentWithSingleApplicableCtorComposes)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> keep;

    auto host = f.MakeHost("Host");
    auto intCtor = f.MakeMethod(".ctor", f.int32, host, host.get(), keep);
    host->SetConstructors({ intCtor.get() });
    auto argument = Fixture::MakeExpression(
        std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true));

    std::shared_ptr<ResolveResult> result =
        resolver.ResolveObjectCreation(*host, { argument });

    auto* invocation = dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->Member(), intCtor.get());
}
