// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the IntroduceExtensionMethods transform (the port of
// CSharp/Transforms/IntroduceExtensionMethods.cs) plus the NamespaceDeclaration.Identifiers
// computed read it consumes:
//   * the `Identifiers` walk over a dotted `MemberType`/`SimpleType` chain;
//   * the static `CanTransformToExtensionMethodCall` shape gate (no symbol, non-extension
//     method, no arguments, an unsupported target kind, a named first argument, the
//     constant-null -> `ConversionResolveResult` and `DirectionExpression` -> inner-result
//     target rewrites, and the resolver verdict over a real extension-method fixture);
//   * the `VisitInvocationExpression` rewrite through a full `Run` (the identifier- and
//     member-reference-target forms, the `ref`/`out` direction unwrap/skip, the
//     `NullReferenceExpression` cast wrap, the settings gate, and the
//     `CSharpInvocationResolveResult.IsExtensionMethodInvocation` annotation update).
//
// The extension-method fixture (a `LookupCompilation` + a `TestNamespace` host carrying a
// static `HasExtensions` type) mirrors the CSharpResolverCanTransform_Test suite, whose
// resolver method this transform wraps.

#include "Decompiler/CSharp/Transforms/IntroduceExtensionMethods.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/UsingScopeAnnotation.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Transforms = ILSpy::Decompiler::CSharp::Transforms;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::CSharp::WithUsingScope;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

// A namespace stub with a configurable type table (the extension-method scan enumerates
// Types()).
class TestNamespace : public INamespace {
public:
    TestNamespace(std::string name, const TS::ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddTypeDefinition(const TS::ITypeDefinition* def) { types_.push_back(def); }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }
    const TS::ICompilation& Compilation() const override { return compilation_; }
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return {}; }
    std::vector<const TS::ITypeDefinition*> Types() const override { return types_; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const INamespace* GetChildNamespace(const std::string&) const override { return nullptr; }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&, int) const override
    {
        return nullptr;
    }

private:
    std::string name_;
    const TS::ICompilation& compilation_;
    std::vector<const TS::ITypeDefinition*> types_;
};

// The per-test fixture: a fresh LookupCompilation (the CSharpConversions cache lifetime
// discipline) plus the shared-managed type definitions and settings.
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> receiver;
    ILSpy::Decompiler::DecompilerSettings settings;
    Syntax::TypeSystemAstBuilder astBuilder;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          receiver(MakeDef("C", KnownTypeCode::None, TypeKind::Class)) {}

    std::shared_ptr<LookupTypeDefinition> MakeHost(
        const std::vector<const TS::IMethod*>& methods) const {
        auto host = MakeDef("Extensions", KnownTypeCode::None, TypeKind::Class);
        host->SetStatic(true);
        host->SetHasExtensions(true);
        host->SetMethods(methods);
        return host;
    }

    // A two-parameter extension method `M(this <firstType> c, <secondType> x)` whose
    // method and parameters are shared-managed in the returned tuple.
    std::tuple<std::shared_ptr<LookupMethod>,
               std::shared_ptr<TS::Implementation::DefaultParameter>,
               std::shared_ptr<TS::Implementation::DefaultParameter>>
    MakeExtensionMethod(ITypePtr firstType, ITypePtr secondType) const {
        auto method = std::make_shared<LookupMethod>("M", compilation);
        method->SetIsExtensionMethod(true);
        auto first = std::make_shared<TS::Implementation::DefaultParameter>(
            std::move(firstType), "c");
        auto second = std::make_shared<TS::Implementation::DefaultParameter>(
            std::move(secondType), "x");
        method->SetParameters({first.get(), second.get()});
        method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Void));
        return {std::move(method), std::move(first), std::move(second)};
    }

    std::shared_ptr<TestNamespace> MakeNamespace(
        const std::shared_ptr<LookupTypeDefinition>& host) const {
        auto ns = std::make_shared<TestNamespace>("NS", compilation);
        ns->AddTypeDefinition(host.get());
        return ns;
    }

    std::shared_ptr<UsingScope> MakeScope(const std::shared_ptr<TestNamespace>& ns) const {
        auto root = std::make_shared<CSharpTypeResolveContext>(compilation.MainModule());
        return std::make_shared<UsingScope>(root, *ns, std::vector<const INamespace*>{});
    }

    std::shared_ptr<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver> MakeScopedResolver(
        const std::shared_ptr<TestNamespace>& ns) const {
        auto resolver =
            std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver>(compilation);
        return resolver->WithCurrentUsingScope(MakeScope(ns));
    }

private:
    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind, Accessibility::Public,
            compilation, nullptr, code);
    }
};

// Attaches a plain `ResolveResult` of the given type (the argument/receiver shape).
void WithType(Syntax::Expression* expression, ITypePtr type) {
    expression->AddAnnotation(std::make_shared<Sem::ResolveResult>(std::move(type)));
}

// Attaches the method as the invocation's resolve-result symbol. The explicit return-type
// override keeps the resolve result from computing its type through
// `member->ReturnType().shared_from_this()`, which would throw for a `LookupMethod` whose
// default return type is a plain member rather than a shared-managed instance.
void ResolveInvocation(Syntax::InvocationExpression* invocation, const TS::IMethod* method) {
    invocation->AddAnnotation(std::make_shared<Sem::InvocationResolveResult>(
        nullptr, method, std::vector<std::shared_ptr<Sem::ResolveResult>>{},
        std::vector<std::shared_ptr<Sem::ResolveResult>>{},
        std::make_shared<TS::KnownType>(KnownTypeCode::Void)));
}

// Builds an invocation with the given target and arguments.
Syntax::InvocationExpression* Invoke(Syntax::Expression* target,
                                     std::vector<Syntax::Expression*> arguments) {
    auto* invocation = new Syntax::InvocationExpression(target);
    for (Syntax::Expression* argument : arguments)
        invocation->Arguments().Add(argument);
    return invocation;
}

// Runs the transform over a fresh block holding one expression statement.
Syntax::InvocationExpression* RunOnInvocation(Fixture& fixture,
                                              const std::shared_ptr<UsingScope>& scope,
                                              Syntax::InvocationExpression* invocation) {
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));
    WithUsingScope(*block, scope);
    ILSpy::Decompiler::DecompileRun run(&fixture.settings, scope);
    CSharpTypeResolveContext decompilationContext(fixture.compilation.MainModule());
    Transforms::TransformContext context(fixture.compilation, run, decompilationContext,
                                         fixture.astBuilder);
    Transforms::IntroduceExtensionMethods transform;
    transform.Run(*block, context);
    return invocation;
}

} // namespace

// ===========================================================================
// NamespaceDeclaration.Identifiers (the namespace-scope descent's input)
// ===========================================================================

TEST(IntroduceExtensionMethodsTest, NamespaceIdentifiersWalksDottedName)
{
    Syntax::NamespaceDeclaration ns;
    ns.NamespaceName(new Syntax::MemberType(
        new Syntax::MemberType(new Syntax::SimpleType("A"), "B"), "C"));
    EXPECT_EQ(ns.Identifiers(), (std::vector<std::string>{"A", "B", "C"}));
}

TEST(IntroduceExtensionMethodsTest, NamespaceIdentifiersOfSimpleName)
{
    Syntax::NamespaceDeclaration ns(new Syntax::SimpleType("Only"));
    EXPECT_EQ(ns.Identifiers(), (std::vector<std::string>{"Only"}));
}

// ===========================================================================
// The static CanTransformToExtensionMethodCall shape gate
// ===========================================================================

TEST(IntroduceExtensionMethodsTest, PredicateWithoutSymbolYieldsFalse)
{
    Fixture fixture;
    auto resolver =
        std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver>(fixture.compilation);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"),
                              {new Syntax::IdentifierExpression("x")});
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_FALSE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
}

TEST(IntroduceExtensionMethodsTest, PredicateOnNonExtensionMethodYieldsFalse)
{
    Fixture fixture;
    auto resolver =
        std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver>(fixture.compilation);
    auto method = std::make_shared<LookupMethod>("M", fixture.compilation);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"),
                              {new Syntax::IdentifierExpression("x")});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_FALSE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
}

TEST(IntroduceExtensionMethodsTest, PredicateWithoutArgumentsYieldsFalse)
{
    Fixture fixture;
    auto resolver =
        std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver>(fixture.compilation);
    auto method = std::make_shared<LookupMethod>("M", fixture.compilation);
    method->SetIsExtensionMethod(true);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_FALSE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
}

TEST(IntroduceExtensionMethodsTest, PredicateWithUnsupportedTargetYieldsFalse)
{
    Fixture fixture;
    auto resolver =
        std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpResolver>(fixture.compilation);
    auto method = std::make_shared<LookupMethod>("M", fixture.compilation);
    method->SetIsExtensionMethod(true);
    auto* invocation = Invoke(new Syntax::NullReferenceExpression(),
                              {new Syntax::IdentifierExpression("x")});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_FALSE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
}

TEST(IntroduceExtensionMethodsTest, PredicateWithNamedFirstArgumentYieldsFalse)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto resolver = fixture.MakeScopedResolver(ns);
    auto* invocation = Invoke(
        new Syntax::IdentifierExpression("M"),
        {new Syntax::NamedArgumentExpression("c", new Syntax::IdentifierExpression("x"))});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_FALSE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
}

TEST(IntroduceExtensionMethodsTest, PredicateIdentifierTargetLeavesMemberRefNull)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto resolver = fixture.MakeScopedResolver(ns);
    auto* receiverArg = new Syntax::IdentifierExpression("x");
    WithType(receiverArg, fixture.receiver);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation =
        Invoke(new Syntax::IdentifierExpression("M"), {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_TRUE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
    EXPECT_EQ(memberRefExpr, nullptr);
    EXPECT_EQ(firstArgument, receiverArg);
    EXPECT_EQ(target.get(), receiverArg->Annotation<Sem::ResolveResult>());
}

TEST(IntroduceExtensionMethodsTest, PredicateMemberReferenceTargetReportsMemberRef)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto resolver = fixture.MakeScopedResolver(ns);
    auto* receiverArg = new Syntax::IdentifierExpression("x");
    WithType(receiverArg, fixture.receiver);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* target = new Syntax::MemberReferenceExpression(
        new Syntax::IdentifierExpression("Extensions"), "M");
    auto* invocation = Invoke(target, {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> targetResult;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_TRUE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, targetResult, firstArgument));
    EXPECT_EQ(memberRefExpr, target);
}

TEST(IntroduceExtensionMethodsTest, PredicateDirectionTargetUsesInnerResolveResult)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto resolver = fixture.MakeScopedResolver(ns);
    auto* inner = new Syntax::IdentifierExpression("x");
    WithType(inner, fixture.receiver);
    auto* receiverArg =
        new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, inner);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation =
        Invoke(new Syntax::IdentifierExpression("M"), {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_TRUE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
    EXPECT_EQ(firstArgument, receiverArg);
    EXPECT_EQ(target.get(), inner->Annotation<Sem::ResolveResult>());
}

TEST(IntroduceExtensionMethodsTest, PredicateNullConstantTargetBecomesConversion)
{
    // A constant-null first argument has its target replaced by a
    // `ConversionResolveResult` over the extension method's `this` parameter type.
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto resolver = fixture.MakeScopedResolver(ns);
    auto* nullArg = new Syntax::NullReferenceExpression();
    nullArg->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        fixture.receiver, std::any()));
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {nullArg, secondArg});
    ResolveInvocation(invocation, method.get());
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    EXPECT_TRUE(Transforms::IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
        *resolver, *invocation, memberRefExpr, target, firstArgument));
    EXPECT_NE(dynamic_cast<const Sem::ConversionResolveResult*>(target.get()), nullptr);
    EXPECT_EQ(firstArgument, nullArg);
}

// ===========================================================================
// VisitInvocationExpression (the full Run rewrite)
// ===========================================================================

TEST(IntroduceExtensionMethodsTest, IdentifierTargetBecomesExtensionSyntax)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* receiverArg = new Syntax::IdentifierExpression("x");
    WithType(receiverArg, fixture.receiver);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    auto* newTarget = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_NE(newTarget, nullptr);
    EXPECT_EQ(newTarget->Target(), receiverArg);
    EXPECT_EQ(newTarget->MemberName(), "M");
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_EQ(invocation->Arguments()[0], secondArg);
}

TEST(IntroduceExtensionMethodsTest, MemberReferenceTargetReusesMemberReference)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* receiverArg = new Syntax::IdentifierExpression("x");
    WithType(receiverArg, fixture.receiver);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* target =
        new Syntax::MemberReferenceExpression(new Syntax::IdentifierExpression("Extensions"), "M");
    auto* invocation = Invoke(target, {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    EXPECT_EQ(invocation->Target(), target);
    EXPECT_EQ(target->Target(), receiverArg);
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_EQ(invocation->Arguments()[0], secondArg);
}

TEST(IntroduceExtensionMethodsTest, DirectionRefArgumentIsUnwrapped)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* inner = new Syntax::IdentifierExpression("x");
    WithType(inner, fixture.receiver);
    auto* receiverArg = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, inner);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    auto* newTarget = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_NE(newTarget, nullptr);
    EXPECT_EQ(newTarget->Target(), inner);
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_EQ(invocation->Arguments()[0], secondArg);
}

TEST(IntroduceExtensionMethodsTest, DirectionOutArgumentIsKept)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* inner = new Syntax::IdentifierExpression("x");
    WithType(inner, fixture.receiver);
    auto* receiverArg = new Syntax::DirectionExpression(Syntax::FieldDirection::Out, inner);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* target = new Syntax::IdentifierExpression("M");
    auto* invocation = Invoke(target, {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    EXPECT_EQ(invocation->Target(), target);
    ASSERT_EQ(invocation->Arguments().Count(), 2);
    EXPECT_EQ(invocation->Arguments()[0], receiverArg);
}

TEST(IntroduceExtensionMethodsTest, RefExtensionMethodsSettingOffIsKept)
{
    Fixture fixture;
    fixture.settings.SetRefExtensionMethods(false);
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* inner = new Syntax::IdentifierExpression("x");
    WithType(inner, fixture.receiver);
    auto* receiverArg = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, inner);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* target = new Syntax::IdentifierExpression("M");
    auto* invocation = Invoke(target, {receiverArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    EXPECT_EQ(invocation->Target(), target);
    ASSERT_EQ(invocation->Arguments().Count(), 2);
}

TEST(IntroduceExtensionMethodsTest, NullReferenceArgumentIsWrappedInCast)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* nullArg = new Syntax::NullReferenceExpression();
    nullArg->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        fixture.receiver, std::any()));
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {nullArg, secondArg});
    ResolveInvocation(invocation, method.get());

    RunOnInvocation(fixture, scope, invocation);

    auto* newTarget = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_NE(newTarget, nullptr);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(newTarget->Target());
    ASSERT_NE(cast, nullptr);
    EXPECT_NE(dynamic_cast<Syntax::NullReferenceExpression*>(cast->Expression()), nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_EQ(invocation->Arguments()[0], secondArg);
}

TEST(IntroduceExtensionMethodsTest, ResolveResultMarkedExtensionInvocation)
{
    Fixture fixture;
    auto [method, p1, p2] = fixture.MakeExtensionMethod(fixture.receiver, fixture.int32);
    auto host = fixture.MakeHost({method.get()});
    auto ns = fixture.MakeNamespace(host);
    auto scope = fixture.MakeScope(ns);
    auto* receiverArg = new Syntax::IdentifierExpression("x");
    WithType(receiverArg, fixture.receiver);
    auto* secondArg = new Syntax::IdentifierExpression("y");
    WithType(secondArg, fixture.int32);
    auto* invocation = Invoke(new Syntax::IdentifierExpression("M"), {receiverArg, secondArg});
    invocation->AddAnnotation(
        std::make_shared<ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult>(
            nullptr, method.get(),
            std::vector<std::shared_ptr<Sem::ResolveResult>>{}, 
            ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors::None,
            /*isExtensionMethodInvocation*/ false));

    RunOnInvocation(fixture, scope, invocation);

    const auto* updated =
        invocation->Annotation<ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult>();
    ASSERT_NE(updated, nullptr);
    EXPECT_TRUE(updated->IsExtensionMethodInvocation());
    // The symbol is preserved.
    EXPECT_EQ(ILSpy::Decompiler::CSharp::GetSymbol(*invocation), method.get());
}

TEST(IntroduceExtensionMethodsTest, RunWithoutUsingScopeThrows)
{
    Fixture fixture;
    auto* root = new Syntax::BlockStatement();
    ILSpy::Decompiler::DecompileRun run(&fixture.settings,
                                        fixture.MakeScope(fixture.MakeNamespace(
                                            fixture.MakeHost({}))));
    CSharpTypeResolveContext decompilationContext(fixture.compilation.MainModule());
    Transforms::TransformContext context(fixture.compilation, run, decompilationContext,
                                         fixture.astBuilder);
    Transforms::IntroduceExtensionMethods transform;
    EXPECT_THROW(transform.Run(*root, context), std::logic_error);
}
