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

// The CallBuilder Build(CallInstruction) dispatch (CallBuilder.cs lines
// 202-232) over a real resolved Call node: the ldftn delegate-construction
// arm (the ObjectCreateExpression over the method group), the
// plain-call default arm (the InvocationExpression with the IL-instruction
// annotations), and the `tail.` inline comment. The string-concat arm is
// covered by the CallBuilderStringConcat tests; the tuple arm is deferred
// with the TupleTransform surface.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::CallBuilder;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
namespace Resolver = ::ILSpy::Decompiler::CSharp::Resolver;

struct BuildCallFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;
    std::vector<std::shared_ptr<Impl::DefaultParameter>> parameters_;

    BuildCallFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          scopelessContext(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharp::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          context_(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope)),
          settings(), run(&settings, usingScope)
    {
    }

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    CSharp::ExpressionBuilder MakeBuilder()
    {
        return CSharp::ExpressionBuilder(nullptr, compilation, *context_, &function_,
                                         &settings, &run);
    }

    // A static method `name` on the String type with the given parameter types
    // (the resolver cannot resolve the FakeMethod by simple name, so the
    // cascade gives up and the render carries the method).
    std::shared_ptr<Impl::FakeMethod> MakeStaticMethod(
        const char* name, std::vector<TS::ITypePtr> paramTypes)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        method->SetReturnType(TypePtr(TS::KnownTypeCode::String));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::ITypePtr& type : paramTypes)
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                std::move(type), "arg"));
        method->SetParameters(parameters);
        return method;
    }
};

} // namespace

TEST(CallBuilderBuildCallTest, PlainCallRendersInvocationWithTailComment)
{
    BuildCallFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeStaticMethod(
        "Concat", {fixture.TypePtr(TS::KnownTypeCode::String),
                   fixture.TypePtr(TS::KnownTypeCode::String)});

    IL::LdStr first("a");
    IL::LdStr second("b");
    IL::Call call(std::static_pointer_cast<TS::IMethod>(method));
    call.Arguments.push_back(std::make_unique<IL::LdStr>("a"));
    call.Arguments.push_back(std::make_unique<IL::LdStr>("b"));
    call.IsTail = true;

    auto result = callBuilder.BuildCall(call);

    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_TRUE(invocation != nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), 2);
    const auto* rr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        CSharp::GetResolveResult(*invocation));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), method.get());
    // The IL `tail.` prefix surfaces as the leading inline comment.
    EXPECT_FALSE(invocation->LeadingTrivia().empty());
}

TEST(CallBuilderBuildCallTest, DelegateConstructionRendersObjectCreation)
{
    BuildCallFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // `newobj MyDelegate(target, ldftn Method)` over an instance method with
    // the matching invoke shape: the delegate-construction arm renders the
    // ObjectCreateExpression over the method-group reference.
    auto delegateType = std::make_shared<TestSupport::LookupTypeDefinition>(
        "MyDelegate", "Test",
        TS::FullTypeName(TS::TopLevelTypeName("Test", "MyDelegate", 0)),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    auto targetMethod = std::make_shared<TestSupport::LookupMethod>(
        "Method", fixture.compilation);
    targetMethod->SetStatic(false);
    targetMethod->SetDeclaringTypeDefinition(delegateType.get());
    targetMethod->SetDeclaringType(std::shared_ptr<TS::IType>(delegateType));
    targetMethod->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));
    auto parameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int32), "arg");
    targetMethod->SetParameters({parameter.get()});

    // The newobj's resolved method: the delegate type's own constructor (its
    // declaring type is the delegate, which GetDelegateInvokeMethod reads).
    auto ctor = std::make_shared<Impl::FakeMethod>(fixture.compilation,
                                                   TS::SymbolKind::Constructor);
    ctor->SetName(".ctor");
    ctor->SetIsStatic(false);
    ctor->SetDeclaringType(std::shared_ptr<TS::IType>(delegateType));
    ctor->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    // The delegate constructor's own shape: `(object target, IntPtr method)`
    // (the C# delegate ctor the newobj resolves to; the ldftn argument is the
    // second).
    ctor->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Object), "target"),
         std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Int32), "method")});

    IL::LdNull thisArg;
    IL::LdFtn func(std::static_pointer_cast<const TS::IMethod>(targetMethod));
    IL::Call call(std::static_pointer_cast<TS::IMethod>(ctor), /*isNewObj=*/true);
    call.Arguments.push_back(std::make_unique<IL::LdNull>());
    call.Arguments.push_back(std::make_unique<IL::LdFtn>(func));

    auto result = callBuilder.BuildCall(call);

    auto* objectCreate =
        dynamic_cast<Syntax::ObjectCreateExpression*>(result.Expression());
    ASSERT_TRUE(objectCreate != nullptr);
    ASSERT_EQ(objectCreate->Arguments().Count(), 1);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(
        objectCreate->Arguments().At(0));
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Method");
}

} // namespace ILSpy::Tests