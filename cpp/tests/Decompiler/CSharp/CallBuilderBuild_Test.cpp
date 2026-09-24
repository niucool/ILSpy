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

// The CallBuilder main Build integrator (CallBuilder.cs lines 332-566) over
// real IL instructions: the static-invocation render (the cascade give-up
// path feeding the InvocationExpression + CSharpInvocationResolveResult), the
// delegate-Invoke arm (isDelegateInvocation), the NewObj
// HandleConstructorCall render (ObjectCreateExpression over the disambiguated
// ctor), and the vararg rewrite (the __arglist UndocumentedExpression tail
// with the BaseMethod swap). The FakeMethod/LookupMethod fixtures are
// unresolvable by name, so the overload-resolution cascade gives up and the
// renders carry the original method.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

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

struct BuildFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;
    std::vector<std::shared_ptr<Impl::DefaultParameter>> parameters_;

    BuildFixture()
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
        const char* name, std::vector<TS::ITypePtr> paramTypes,
        TS::KnownTypeCode returnType = TS::KnownTypeCode::String)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        method->SetReturnType(TypePtr(returnType));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::ITypePtr& type : paramTypes)
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                std::move(type), "arg"));
        method->SetParameters(parameters);
        return method;
    }

    // A plain `ldstr "a"` / `ldc.i4 N` / `ldnull` argument list.
    std::vector<IL::ILInstruction*> Args(IL::ILInstruction* first)
    {
        return {first};
    }
};

} // namespace

TEST(CallBuilderBuildTest, StaticInvocationRendersInvocation)
{
    BuildFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeStaticMethod(
        "Concat", {fixture.TypePtr(TS::KnownTypeCode::String),
                   fixture.TypePtr(TS::KnownTypeCode::String)});

    IL::LdStr first("a");
    IL::LdStr second("b");
    auto result = callBuilder.Build(IL::OpCode::Call, *method, {&first, &second});

    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_TRUE(invocation != nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), 2);
    const auto* rr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        CSharp::GetResolveResult(*invocation));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), method.get());
    EXPECT_FALSE(rr->IsDelegateInvocation());
    EXPECT_FALSE(rr->IsExpandedForm());
}

TEST(CallBuilderBuildTest, DelegateInvokeRendersDelegateInvocation)
{
    BuildFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // A static `Invoke(string)` on a delegate-kind declaring type: the
    // delegate-Invoke arm fires before the overload-resolution cascade and
    // flags the invocation as a delegate invocation.
    auto delegateType = std::make_shared<TestSupport::LookupTypeDefinition>(
        "MyDelegate", "Test",
        TS::FullTypeName(TS::TopLevelTypeName("Test", "MyDelegate", 0)),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    auto invokeMethod = std::make_shared<TestSupport::LookupMethod>(
        "Invoke", fixture.compilation);
    invokeMethod->SetStatic(true);
    invokeMethod->SetDeclaringTypeDefinition(delegateType.get());
    invokeMethod->SetDeclaringType(std::shared_ptr<TS::IType>(delegateType));
    invokeMethod->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));
    auto parameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::String), "arg");
    std::vector<const TS::IParameter*> parameters{parameter.get()};
    invokeMethod->SetParameters(std::move(parameters));

    IL::LdStr arg("payload");
    auto result = callBuilder.Build(IL::OpCode::Call, *invokeMethod, {&arg});

    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_TRUE(invocation != nullptr);
    const auto* rr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        CSharp::GetResolveResult(*invocation));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_TRUE(rr->IsDelegateInvocation());
}

TEST(CallBuilderBuildTest, NewObjRendersObjectCreation)
{
    BuildFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto ctor = std::make_shared<Impl::FakeMethod>(fixture.compilation,
                                                   TS::SymbolKind::Constructor);
    ctor->SetName(".ctor");
    ctor->SetIsStatic(false);
    ctor->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    ctor->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    ctor->SetParameters({std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::String), "value")});

    IL::LdStr arg("x");
    auto result = callBuilder.Build(IL::OpCode::NewObj, *ctor, {&arg});

    auto* objectCreate =
        dynamic_cast<Syntax::ObjectCreateExpression*>(result.Expression());
    ASSERT_TRUE(objectCreate != nullptr);
    ASSERT_EQ(objectCreate->Arguments().Count(), 1);
    auto* argElement =
        dynamic_cast<Syntax::PrimitiveExpression*>(objectCreate->Arguments().At(0));
    ASSERT_TRUE(argElement != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(argElement->Value()));
    EXPECT_EQ(std::get<std::string>(argElement->Value()), "x");
    const auto* rr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        CSharp::GetResolveResult(*objectCreate));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), ctor.get());
}

TEST(CallBuilderBuildTest, VarArgCallAppendsArgListExpression)
{
    BuildFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // An instance vararg method `M(int, __arglist int)`: the vararg rewrite
    // appends the `__arglist(...)` UndocumentedExpression and re-bases the
    // call on the base method.
    auto baseMethod = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    baseMethod->SetName("M");
    baseMethod->SetIsStatic(false);
    baseMethod->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    baseMethod->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    baseMethod->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Int32), "regular"),
         std::make_shared<Impl::DefaultParameter>(
             TS::ArgList(), "arglistSentinel")});
    auto varArgMethod = std::make_shared<TS::VarArgInstanceMethod>(
        std::static_pointer_cast<TS::IMethod>(baseMethod),
        std::vector<TS::ITypePtr>{fixture.TypePtr(TS::KnownTypeCode::Int32)});

    IL::LdNull thisArg;
    IL::LdcI4 regular(1);
    IL::LdcI4 varArg(2);
    auto result = callBuilder.Build(IL::OpCode::Call, *varArgMethod,
                                    {&thisArg, &regular, &varArg});

    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_TRUE(invocation != nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), 2);
    auto* argList = dynamic_cast<Syntax::UndocumentedExpression*>(
        invocation->Arguments().At(1));
    ASSERT_TRUE(argList != nullptr);
    EXPECT_EQ(argList->UndocumentedExpressionType(),
              Syntax::UndocumentedExpressionType::ArgList);
    ASSERT_EQ(argList->Arguments().Count(), 1);
    const auto* rr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        CSharp::GetResolveResult(*invocation));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), baseMethod.get());
}

} // namespace ILSpy::Tests