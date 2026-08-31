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

// Tests for the CSharpResolver invocation region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines
// 2227-2443): AddArgumentNamesIfNecessary + ResolveInvocation (the private 4-arg
// core + the public 3-arg entry collapsed) + CreateParameters + the static
// GuessParameterName / MakeParameterName pair, plus the additive
// owning-parameters ctor on UnknownMethodResolveResult the synthesized
// CreateParameters results flow through.
//
// The load-bearing cruxes:
//  (a) the dynamic-target arm -- a Dynamic-typed target yields the
//      DynamicInvocationResolveResult (Invocation) with the named-wrapped
//      arguments, without consulting the method group machinery;
//  (b) the method-group arm -- PerformOverloadResolution with the resolver's
//      checkForOverflow/conversions and the given allowOptionalParameters; a
//      STATIC non-extension method over a VALUE target (not a TypeResolveResult)
//      re-targets the result to a TypeResolveResult over the group's target
//      type, an instance method keeps the group's own target;
//  (c) the dynamic-arguments sub-arm -- MORE THAN ONE applicable method with a
//      dynamic argument yields the dynamic invocation over a FRESH method group
//      carrying the applicable methods (the static-methods re-target crux); a
//      single applicable method falls through to the regular resolution with
//      the Dynamic return-type override;
//  (d) the empty-method-group / UnknownMember / UnknownIdentifier fallbacks --
//      the UnknownMethodResolveResult with the SYNTHESIZED parameters (the
//      guessed, disambiguated names; the by-reference ReferenceKind; the
//      null-literal-to-object fold) kept alive by the owning-parameters ctor;
//  (e) the delegate-invoke arm -- a delegate-typed target resolves through its
//      Invoke method into the CSharpInvocationResolveResult marking
//      isDelegateInvocation;
//  (f) anything else is the ErrorResult singleton.
//
// LIFETIME DISCIPLINE: the method-group resolution path reaches the per-
// compilation CSharpConversions instance (the resolver's Conversions()), so
// every test builds its resolver over a FRESH per-test LookupCompilation
// (CSharpConversions::Get caches its instance on that compilation's
// CacheManager; the instance and its cache die WITH the test -- the
// iteration-109 discipline). Every method reaching a MemberResolveResult
// composition needs SetReturnType over a make_shared'd type (the
// ComputeType shared_from_this trap; the LookupMethod default return type is
// an inline non-shared-managed KnownType member).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/LocalResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
using ILSpy::Decompiler::Semantics::LocalResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::NamedArgumentResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMethodResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;

// A `LookupTypeDefinition` subclass whose `GetMethods` returns a configured list
// (the D533 MethodHostType precedent): the delegate-invoke arm resolves the
// target type's `Invoke` method through the method table.
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

// A minimal `IVariable` for the `GuessParameterName` local-variable arm (the
// each-test-file-carries-its-own-stubs convention).
class TestVariable : public IVariable {
public:
    TestVariable(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type))
    {
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool throwOnInvalidMetadata = false) const override
    {
        (void)throwOnInvalidMetadata;
        return {};
    }

private:
    std::string name_;
    ITypePtr type_;
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in
// the file header) plus the shared-managed definitions the invocation paths
// resolve over (the type-cache model: one instance per primitive, shared by the
// registry, the targets, and the method parameters).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> classDef;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          classDef(MakeDef("C", KnownTypeCode::None, TypeKind::Class))
    {
        // The null-literal-to-object fold in CreateParameters resolves
        // `compilation.FindType(KnownTypeCode.Object)` -- the code must be
        // registered with a shared-managed definition (the bad_weak_ptr trap:
        // an unregistered code falls back to the compilation's non-shared
        // unknownType_ stub whose shared_from_this throws).
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    // A method with the given name, a make_shared'd return type (the ComputeType
    // shared_from_this trap), and one parameter over the given type. The method
    // and the parameter are shared-managed and returned for the test scope to
    // hold (the candidate stores non-owning pointers).
    struct MethodWithParam {
        std::shared_ptr<LookupMethod> method;
        std::shared_ptr<DefaultParameter> parameter;
    };

    MethodWithParam MakeMethod(const std::string& name, ITypePtr parameterType,
                               ITypePtr returnType) const
    {
        auto method = std::make_shared<LookupMethod>(name, compilation);
        method->SetReturnType(std::move(returnType));
        auto parameter = std::make_shared<DefaultParameter>(std::move(parameterType), "x");
        method->SetParameters({ parameter.get() });
        return { std::move(method), std::move(parameter) };
    }

    // A plain argument over the given type.
    static std::shared_ptr<ResolveResult> MakeArgument(ITypePtr type)
    {
        return std::make_shared<ResolveResult>(std::move(type));
    }

    // A method group over the given target result carrying the methods of the
    // single declaring-type bucket. The methods vector is converted to the
    // `IParameterizedMember` element type the bucket stores (std::vector is
    // invariant -- a `vector<const IMethod*>` does not convert implicitly).
    static std::shared_ptr<MethodGroupResolveResult> MakeGroup(
        std::shared_ptr<ResolveResult> target, const std::string& methodName,
        ITypePtr declaringType, std::vector<const IMethod*> methods)
    {
        std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*> members(
            methods.begin(), methods.end());
        return std::make_shared<MethodGroupResolveResult>(
            std::move(target), methodName,
            std::vector<MethodListWithDeclaringType>{
                MethodListWithDeclaringType(std::move(declaringType), std::move(members)) },
            std::vector<ITypePtr>{});
    }
};

} // namespace

// ===========================================================================
// MakeParameterName (the static, direct)
// ===========================================================================

TEST(CSharpResolverInvocationTest, MakeParameterNameEmptyYieldsParameter)
{
    EXPECT_EQ(CSharpResolver::MakeParameterName(""), "parameter");
}

TEST(CSharpResolverInvocationTest, MakeParameterNameLowercasesFirstLetter)
{
    EXPECT_EQ(CSharpResolver::MakeParameterName("Foo"), "foo");
    EXPECT_EQ(CSharpResolver::MakeParameterName("MyVar"), "myVar");
}

TEST(CSharpResolverInvocationTest, MakeParameterNameStripsLeadingUnderscore)
{
    // The C# `variableName.Length > 1 && variableName[0] == '_'` -- only a
    // multi-char name starting with '_' is stripped.
    EXPECT_EQ(CSharpResolver::MakeParameterName("_foo"), "foo");
    EXPECT_EQ(CSharpResolver::MakeParameterName("__x"), "_x");
}

TEST(CSharpResolverInvocationTest, MakeParameterNameSingleUnderscoreStays)
{
    // A single '_' is NOT stripped (Length is 1, not > 1) and lower-cases to
    // itself.
    EXPECT_EQ(CSharpResolver::MakeParameterName("_"), "_");
}

TEST(CSharpResolverInvocationTest, MakeParameterNameAlreadyLowercaseStays)
{
    EXPECT_EQ(CSharpResolver::MakeParameterName("x1"), "x1");
}

// ===========================================================================
// GuessParameterName (the static, direct)
// ===========================================================================

TEST(CSharpResolverInvocationTest, GuessParameterNameMemberYieldsMemberName)
{
    Fixture fix;
    auto method = fix.MakeMethod("M", fix.int32, fix.int32);
    // A MemberResolveResult over the method (the target is irrelevant here).
    auto mrr = std::make_shared<MemberResolveResult>(nullptr, method.method.get());
    EXPECT_EQ(CSharpResolver::GuessParameterName(*mrr), "M");
}

TEST(CSharpResolverInvocationTest, GuessParameterNameUnknownMemberYieldsMemberName)
{
    Fixture fix;
    UnknownMemberResolveResult umrr(fix.int32, "Foo", std::vector<ITypePtr>{});
    EXPECT_EQ(CSharpResolver::GuessParameterName(umrr), "Foo");
}

TEST(CSharpResolverInvocationTest, GuessParameterNameMethodGroupYieldsMethodName)
{
    Fixture fix;
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "Bar", fix.classDef, {});
    EXPECT_EQ(CSharpResolver::GuessParameterName(*group), "Bar");
}

TEST(CSharpResolverInvocationTest, GuessParameterNameLocalVariableYieldsNormalizedName)
{
    Fixture fix;
    TestVariable variable("MyVar", fix.int32);
    LocalResolveResult vrr(&variable);
    // The local-variable arm normalizes through MakeParameterName (the C#
    // `MakeParameterName(vrr.Variable.Name)`).
    EXPECT_EQ(CSharpResolver::GuessParameterName(vrr), "myVar");
}

TEST(CSharpResolverInvocationTest, GuessParameterNameTypedResultYieldsNormalizedTypeName)
{
    Fixture fix;
    auto rr = Fixture::MakeArgument(fix.int32);
    // The fallback arm normalizes the TYPE's name ("Int32" -> "int32").
    EXPECT_EQ(CSharpResolver::GuessParameterName(*rr), "int32");
}

TEST(CSharpResolverInvocationTest, GuessParameterNameUnknownTypeYieldsParameter)
{
    // The C# `rr.Type.Kind != TypeKind.Unknown && !string.IsNullOrEmpty(rr.Type.
    // Name)` -- the UnknownType null object fails the kind check and yields the
    // fixed "parameter".
    auto rr = Fixture::MakeArgument(ILSpy::Decompiler::TypeSystem::UnknownType());
    EXPECT_EQ(CSharpResolver::GuessParameterName(*rr), "parameter");
}

// ===========================================================================
// AddArgumentNamesIfNecessary (the private helper, direct)
// ===========================================================================

TEST(CSharpResolverInvocationTest, AddArgumentNamesNullArrayReturnsArgumentsAsIs)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);
    auto a1 = Fixture::MakeArgument(fix.objectDef);
    std::vector<std::shared_ptr<ResolveResult>> arguments = { a0, a1 };

    // The C# `if (argumentNames == null) return arguments;` -- the null ARRAY
    // (the empty optional) shares the very same handles.
    auto result = resolver->AddArgumentNamesIfNecessary(arguments, std::nullopt);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), a0.get());
    EXPECT_EQ(result[1].get(), a1.get());
}

TEST(CSharpResolverInvocationTest, AddArgumentNamesWrapsNamedArguments)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);
    std::vector<std::shared_ptr<ResolveResult>> arguments = { a0 };

    auto result = resolver->AddArgumentNamesIfNecessary(
        arguments, std::vector<std::string>{ "x" });
    ASSERT_EQ(result.size(), 1u);
    auto* named = dynamic_cast<NamedArgumentResolveResult*>(result[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
}

TEST(CSharpResolverInvocationTest, AddArgumentNamesPositionalEntriesPassThrough)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);
    std::vector<std::shared_ptr<ResolveResult>> arguments = { a0 };

    // A null ENTRY (the empty string) keeps the original handle.
    auto result = resolver->AddArgumentNamesIfNecessary(
        arguments, std::vector<std::string>{ "" });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), a0.get());
}

TEST(CSharpResolverInvocationTest, AddArgumentNamesOutOfRangeEntryTreatedAsPositional)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);
    auto a1 = Fixture::MakeArgument(fix.objectDef);
    std::vector<std::shared_ptr<ResolveResult>> arguments = { a0, a1 };

    // A mismatched-length names vector (the C# would throw
    // IndexOutOfRangeException) -- the out-of-range entry is positional (the
    // D516 safe-fallback convention).
    auto result = resolver->AddArgumentNamesIfNecessary(
        arguments, std::vector<std::string>{ "x" });
    ASSERT_EQ(result.size(), 2u);
    EXPECT_NE(result[0].get(), a0.get());  // wrapped
    EXPECT_EQ(result[1].get(), a1.get());  // positional
}

// ===========================================================================
// CreateParameters (the private helper, direct)
// ===========================================================================

TEST(CSharpResolverInvocationTest, CreateParametersEmptyArgumentsYieldEmptyList)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto list = resolver->CreateParameters({}, std::nullopt);
    EXPECT_TRUE(list.empty());
}

TEST(CSharpResolverInvocationTest, CreateParametersGuessesNamesFromArgumentTypes)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto arg = Fixture::MakeArgument(fix.int32);

    auto list = resolver->CreateParameters({ arg }, std::nullopt);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0]->Name(), "int32");
    EXPECT_EQ(&list[0]->Type(), static_cast<const IType*>(fix.int32.get()));
}

TEST(CSharpResolverInvocationTest, CreateParametersDisambiguatesDuplicateGuessedNames)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);
    auto a1 = Fixture::MakeArgument(fix.int32);

    // Two arguments of the same type guess the same name; the second is
    // disambiguated with a numeric suffix (the C# do/while).
    auto list = resolver->CreateParameters({ a0, a1 }, std::nullopt);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0]->Name(), "int32");
    EXPECT_EQ(list[1]->Name(), "int321");
}

TEST(CSharpResolverInvocationTest, CreateParametersGivenNamesAreUsedVerbatim)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto list = resolver->CreateParameters({ a0 }, std::vector<std::string>{ "first" });
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0]->Name(), "first");
}

TEST(CSharpResolverInvocationTest, CreateParametersByReferenceArgumentKeepsReferenceKind)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // A by-reference argument: the parameter keeps the ReferenceKind and the
    // argument's (by-reference) type.
    auto arg = std::make_shared<ByReferenceResolveResult>(
        Fixture::MakeArgument(fix.int32), ReferenceKind::Ref);

    auto list = resolver->CreateParameters({ arg }, std::nullopt);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0]->ReferenceKind(), ReferenceKind::Ref);
    EXPECT_EQ(list[0]->Type().Kind(), TypeKind::ByReference);
}

TEST(CSharpResolverInvocationTest, CreateParametersNullLiteralArgumentBecomesObject)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // The null-literal type (TypeKind::Null) -- the C# `type.Kind == TypeKind.
    // Null || type.Kind == TypeKind.None` arm folds the parameter type to the
    // registered `object`.
    auto arg = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true)));

    auto list = resolver->CreateParameters({ arg }, std::nullopt);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(&list[0]->Type(), static_cast<const IType*>(fix.objectDef.get()));
}

TEST(CSharpResolverInvocationTest, CreateParametersLengthMismatchThrows)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto a0 = Fixture::MakeArgument(fix.int32);

    // The C# `throw new ArgumentException()` on a mismatched-length
    // argumentNames.
    EXPECT_THROW(
        resolver->CreateParameters({ a0 }, std::vector<std::string>{ "a", "b" }),
        std::invalid_argument);
}

// ===========================================================================
// ResolveInvocation (the dispatch)
// ===========================================================================

TEST(CSharpResolverInvocationTest, DynamicTargetYieldsDynamicInvocation)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Dynamic, std::optional<bool>(true)));
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(target, { a0 }, std::nullopt);
    auto* dirr = dynamic_cast<DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dirr, nullptr);
    EXPECT_EQ(dirr->InvocationType(), DynamicInvocationType::Invocation);
    EXPECT_FALSE(dirr->IsError());
    ASSERT_EQ(dirr->Arguments().size(), 1u);
    // A null argumentNames array keeps the arguments unwrapped.
    EXPECT_EQ(dirr->Arguments()[0].get(), a0.get());
}

TEST(CSharpResolverInvocationTest, DynamicTargetWrapsNamedArguments)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Dynamic, std::optional<bool>(true)));
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(
        target, { a0 }, std::vector<std::string>{ "x" });
    auto* dirr = dynamic_cast<DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dirr, nullptr);
    ASSERT_EQ(dirr->Arguments().size(), 1u);
    auto* named = dynamic_cast<NamedArgumentResolveResult*>(dirr->Arguments()[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
}

TEST(CSharpResolverInvocationTest, MethodGroupResolvesThroughOverloadResolution)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto method = fix.MakeMethod("M", fix.int32, fix.int32);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "M", fix.classDef, { method.method.get() });
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* cirr = dynamic_cast<CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(cirr, nullptr);
    EXPECT_FALSE(cirr->IsError());
    EXPECT_EQ(cirr->Member(), static_cast<const ILSpy::Decompiler::TypeSystem::IMember*>(
                                  method.method.get()));
    // The result type is the method's return type (no dynamic override).
    EXPECT_EQ(&cirr->Type(), static_cast<const IType*>(fix.int32.get()));
    EXPECT_FALSE(cirr->IsDelegateInvocation());
}

TEST(CSharpResolverInvocationTest, StaticMethodOverValueTargetRetargetsToType)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto method = fix.MakeMethod("M", fix.int32, fix.int32);
    method.method->SetStatic(true);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "M", fix.classDef, { method.method.get() });
    auto a0 = Fixture::MakeArgument(fix.int32);

    // The C# `or.BestCandidate.IsStatic && !or.IsExtensionMethodInvocation &&
    // !(mgrr.TargetResult is TypeResolveResult)` -- a STATIC method invoked over
    // a VALUE target re-targets the result to the type itself.
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* cirr = dynamic_cast<CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(cirr, nullptr);
    auto* trr = dynamic_cast<TypeResolveResult*>(cirr->TargetResult());
    ASSERT_NE(trr, nullptr);
    EXPECT_EQ(&trr->Type(), static_cast<const IType*>(fix.classDef.get()));
}

TEST(CSharpResolverInvocationTest, InstanceMethodKeepsTheGroupsTarget)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto method = fix.MakeMethod("M", fix.int32, fix.int32);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "M", fix.classDef, { method.method.get() });
    auto a0 = Fixture::MakeArgument(fix.int32);

    // An INSTANCE method keeps the group's own target result (the aliasing
    // handle points at the very same target object).
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* cirr = dynamic_cast<CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(cirr, nullptr);
    EXPECT_EQ(cirr->TargetResult(), target.get());
}

TEST(CSharpResolverInvocationTest, EmptyMethodGroupYieldsUnknownMethod)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "M", fix.classDef, {});
    auto a0 = Fixture::MakeArgument(fix.int32);

    // No candidate found at all (not even an inapplicable one) -- the
    // UnknownMethodResolveResult with the SYNTHESIZED parameters (kept alive by
    // the owning-parameters ctor).
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* umrr = dynamic_cast<UnknownMethodResolveResult*>(result.get());
    ASSERT_NE(umrr, nullptr);
    EXPECT_EQ(umrr->MemberName(), "M");
    EXPECT_EQ(&umrr->TargetType(), static_cast<const IType*>(fix.classDef.get()));
    ASSERT_EQ(umrr->Parameters().size(), 1u);
    EXPECT_EQ(umrr->Parameters()[0]->Name(), "int32");
    EXPECT_EQ(&umrr->Parameters()[0]->Type(), static_cast<const IType*>(fix.int32.get()));
}

TEST(CSharpResolverInvocationTest, UnknownMemberTargetYieldsUnknownMethod)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = std::make_shared<UnknownMemberResolveResult>(
        fix.classDef, "Foo", std::vector<ITypePtr>{});
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(target, { a0 }, std::nullopt);
    auto* umrr = dynamic_cast<UnknownMethodResolveResult*>(result.get());
    ASSERT_NE(umrr, nullptr);
    EXPECT_EQ(umrr->MemberName(), "Foo");
    EXPECT_EQ(&umrr->TargetType(), static_cast<const IType*>(fix.classDef.get()));
    ASSERT_EQ(umrr->Parameters().size(), 1u);
}

TEST(CSharpResolverInvocationTest, UnknownIdentifierTargetYieldsUnknownMethodOverCurrentType)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // The UnknownIdentifier arm requires a current type definition (the C#
    // `uirr != null && CurrentTypeDefinition != null`).
    auto withType = resolver->WithCurrentTypeDefinition(fix.classDef.get());
    auto target = std::make_shared<UnknownIdentifierResolveResult>("Bar");
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = withType->ResolveInvocation(target, { a0 }, std::nullopt);
    auto* umrr = dynamic_cast<UnknownMethodResolveResult*>(result.get());
    ASSERT_NE(umrr, nullptr);
    EXPECT_EQ(umrr->MemberName(), "Bar");
    // The target type is the CURRENT TYPE DEFINITION (not the identifier's own
    // unknown type).
    EXPECT_EQ(&umrr->TargetType(), static_cast<const IType*>(fix.classDef.get()));
}

TEST(CSharpResolverInvocationTest, UnknownIdentifierWithoutCurrentTypeFallsThrough)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // No current type definition: the UnknownIdentifier arm is skipped; the
    // UnknownIdentifierResolveResult's own type is UnknownType (not a delegate),
    // so the invocation falls to the ErrorResult.
    auto target = std::make_shared<UnknownIdentifierResolveResult>("Bar");
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(target, { a0 }, std::nullopt);
    EXPECT_EQ(result.get(),
              static_cast<ResolveResult*>(
                  const_cast<ILSpy::Decompiler::Semantics::ErrorResolveResult*>(
                      &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError())));
}

TEST(CSharpResolverInvocationTest, DelegateTargetInvokesThroughInvokeMethod)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // A delegate-typed target whose method table holds the Invoke method (the
    // D533 MethodHostType shape).
    auto delegateType = std::make_shared<MethodHostType>(
        "D", "", FullTypeName(TopLevelTypeName("", "D", 0)), TypeKind::Delegate,
        Accessibility::Public, fix.compilation, nullptr);
    auto invoke = fix.MakeMethod("Invoke", fix.int32, fix.int32);
    delegateType->SetMethods({ invoke.method.get() });
    auto target = Fixture::MakeArgument(delegateType);
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(target, { a0 }, std::nullopt);
    auto* cirr = dynamic_cast<CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(cirr, nullptr);
    EXPECT_TRUE(cirr->IsDelegateInvocation());
    EXPECT_EQ(cirr->Member(), static_cast<const ILSpy::Decompiler::TypeSystem::IMember*>(
                                  invoke.method.get()));
    EXPECT_FALSE(cirr->IsError());
    // The identity conversion keeps the argument unwrapped; the map maps
    // argument 0 to parameter 0.
    ASSERT_EQ(cirr->Arguments().size(), 1u);
    EXPECT_EQ(cirr->Arguments()[0].get(), a0.get());
    ASSERT_TRUE(cirr->GetArgumentToParameterMap().has_value());
    ASSERT_EQ(cirr->GetArgumentToParameterMap()->size(), 1u);
    EXPECT_EQ((*cirr->GetArgumentToParameterMap())[0], 0);
}

TEST(CSharpResolverInvocationTest, InconvertibleTargetYieldsErrorResult)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // An int-typed target: not dynamic, not a method group, not an unknown
    // member/identifier, not a delegate -- the ErrorResult singleton.
    auto target = Fixture::MakeArgument(fix.int32);
    auto a0 = Fixture::MakeArgument(fix.int32);

    auto result = resolver->ResolveInvocation(target, { a0 }, std::nullopt);
    EXPECT_EQ(result.get(),
              static_cast<ResolveResult*>(
                  const_cast<ILSpy::Decompiler::Semantics::ErrorResolveResult*>(
                      &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError())));
}

TEST(CSharpResolverInvocationTest, DynamicArgumentWithMultipleApplicableMethodsYieldsDynamicInvocation)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    // Two methods, both applicable to a dynamic argument (a dynamic operand
    // converts to anything via ImplicitDynamicConversion).
    auto m1 = fix.MakeMethod("M", fix.objectDef, fix.int32);
    auto m2 = fix.MakeMethod("M", fix.int32, fix.int32);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(
        target, "M", fix.classDef, { m1.method.get(), m2.method.get() });
    auto a0 = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Dynamic, std::optional<bool>(true)));

    // More than one applicable method with a dynamic argument: the dynamic
    // invocation over a FRESH method group carrying the applicable methods.
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* dirr = dynamic_cast<DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dirr, nullptr);
    EXPECT_EQ(dirr->InvocationType(), DynamicInvocationType::Invocation);
    auto* innerGroup = dynamic_cast<MethodGroupResolveResult*>(dirr->Target());
    ASSERT_NE(innerGroup, nullptr);
    ASSERT_EQ(innerGroup->Methods().size(), 2u);
    // The instance methods keep the group's own target result.
    EXPECT_EQ(innerGroup->TargetResult(), target.get());
}

TEST(CSharpResolverInvocationTest, DynamicArgumentWithStaticMethodsRetargetsToType)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto m1 = fix.MakeMethod("M", fix.objectDef, fix.int32);
    m1.method->SetStatic(true);
    auto m2 = fix.MakeMethod("M", fix.int32, fix.int32);
    m2.method->SetStatic(true);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(
        target, "M", fix.classDef, { m1.method.get(), m2.method.get() });
    auto a0 = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Dynamic, std::optional<bool>(true)));

    // The C# `applicableMethods.All(x => x.Method.IsStatic) && !(mgrr.
    // TargetResult is TypeResolveResult)` -- all-static applicable methods over
    // a VALUE target re-target the fresh group to the type itself.
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* dirr = dynamic_cast<DynamicInvocationResolveResult*>(result.get());
    ASSERT_NE(dirr, nullptr);
    auto* innerGroup = dynamic_cast<MethodGroupResolveResult*>(dirr->Target());
    ASSERT_NE(innerGroup, nullptr);
    auto* trr = dynamic_cast<TypeResolveResult*>(innerGroup->TargetResult());
    ASSERT_NE(trr, nullptr);
    EXPECT_EQ(&trr->Type(), static_cast<const IType*>(fix.classDef.get()));
}

TEST(CSharpResolverInvocationTest, DynamicArgumentWithSingleMethodResolvesWithDynamicOverride)
{
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto method = fix.MakeMethod("M", fix.int32, fix.int32);
    auto target = Fixture::MakeArgument(fix.classDef);
    auto group = Fixture::MakeGroup(target, "M", fix.classDef, { method.method.get() });
    auto a0 = Fixture::MakeArgument(
        std::make_shared<SpecialType>(TypeKind::Dynamic, std::optional<bool>(true)));

    // A single applicable method falls through the dynamic-arguments sub-arm to
    // the regular resolution; the isDynamic return-type override makes the
    // result type dynamic.
    auto result = resolver->ResolveInvocation(group, { a0 }, std::nullopt);
    auto* cirr = dynamic_cast<CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(cirr, nullptr);
    EXPECT_EQ(cirr->Type().Kind(), TypeKind::Dynamic);
}
