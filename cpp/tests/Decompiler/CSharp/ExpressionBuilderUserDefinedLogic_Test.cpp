// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The ExpressionBuilder VisitUserDefinedLogicOperator suite: the user-defined
// `&&`/`||` render over a real FakeMethod -- the op_BitwiseAnd ->
// ConditionalAnd and op_BitwiseOr -> ConditionalOr arms with the converted
// operands and the InvocationResolveResult, the invalid-method-name throw, the
// seed string-stand-in deferral, and the Visit dispatch.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace IL = ::ILSpy::Decompiler::IL;

// The default settings bag (the C# `new DecompilerSettings()`).
DecompilerSettings DefaultSettings()
{
    return DecompilerSettings{};
}

// The BuilderFixture (the ExpressionBuilderSkeleton_Test shape).
struct BuilderFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    BuilderFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(DefaultSettings()), run(&settings, usingScope)
    {
    }

    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    ExpressionBuilder MakeBuilder()
    {
        return ExpressionBuilder(nullptr, compilation, FixtureContext(), &function_,
                                 &settings, &run);
    }

    // The shared IType handle for a known type code.
    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                      TypePtr(code));
        local->Name = name;
        return local;
    }

    // The resolved operator method (`op_BitwiseAnd` / `op_BitwiseOr` over the
    // given (reference) parameter type, returning that same type -- the C#
    // user-defined short-circuit operator signature).
    std::shared_ptr<Impl::FakeMethod> MakeOperatorMethod(const char* name,
                                                         TS::KnownTypeCode paramCode)
    {
        auto method =
            std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Operator);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(paramCode));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        parameters.push_back(
            std::make_shared<Impl::DefaultParameter>(TypePtr(paramCode), "left"));
        parameters.push_back(
            std::make_shared<Impl::DefaultParameter>(TypePtr(paramCode), "right"));
        method->SetParameters(parameters);
        method->SetReturnType(TypePtr(paramCode));
        return method;
    }

private:
    IL::ILFunction function_;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;

    const CSharp::TypeSystem::CSharpTypeResolveContext& FixtureContext()
    {
        if (!context_)
        {
            context_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
                compilation.MainModule(), usingScope);
        }
        return *context_;
    }
};

} // namespace

TEST(ExpressionBuilderUserDefinedLogicTest, BitwiseAndRendersConditionalAnd)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto method = fixture.MakeOperatorMethod("op_BitwiseAnd", TS::KnownTypeCode::String);
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    IL::UserDefinedLogicOperator logicOp(method, std::make_unique<IL::LdLoc>(a),
                                         std::make_unique<IL::LdLoc>(b));

    auto expr = builder.Translate(&logicOp);

    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::ConditionalAnd);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(bin->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "a");
    auto* right = dynamic_cast<Syntax::IdentifierExpression*>(bin->Right());
    ASSERT_TRUE(right != nullptr);
    EXPECT_EQ(right->Identifier(), "b");
    // The resolve result is the invocation of the operator method.
    auto* rr = dynamic_cast<const Sem::InvocationResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    ASSERT_EQ(rr->Arguments().size(), std::size_t{2});
}

TEST(ExpressionBuilderUserDefinedLogicTest, BitwiseOrRendersConditionalOr)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto method = fixture.MakeOperatorMethod("op_BitwiseOr", TS::KnownTypeCode::String);
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    IL::UserDefinedLogicOperator logicOp(method, std::make_unique<IL::LdLoc>(a),
                                         std::make_unique<IL::LdLoc>(b));

    auto expr = builder.Translate(&logicOp);

    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::ConditionalOr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderUserDefinedLogicTest, InvalidMethodNameThrows)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto method = fixture.MakeOperatorMethod("op_Addition", TS::KnownTypeCode::String);
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    // The string ctor keeps the seed shape; assign the resolved method directly to
    // bypass the ctor's name assert and drive the invalid-name arm.
    IL::UserDefinedLogicOperator logicOp("System.Int32::op_Addition", nullptr,
                                         std::make_unique<IL::LdLoc>(a),
                                         std::make_unique<IL::LdLoc>(b));
    logicOp.Method = method;

    EXPECT_THROW(builder.Translate(&logicOp), std::logic_error);
}

TEST(ExpressionBuilderUserDefinedLogicTest, SeedStandInWithoutMethodThrows)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = fixture.MakeLocal(TS::KnownTypeCode::Int32, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::Int32, "b");
    IL::UserDefinedLogicOperator logicOp("System.Int32::op_BitwiseAnd", nullptr,
                                         std::make_unique<IL::LdLoc>(a),
                                         std::make_unique<IL::LdLoc>(b));

    EXPECT_THROW(builder.Translate(&logicOp), std::logic_error);
}

TEST(ExpressionBuilderUserDefinedLogicTest, VisitDispatchRoutesUserDefinedLogicOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto method = fixture.MakeOperatorMethod("op_BitwiseAnd", TS::KnownTypeCode::String);
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    IL::UserDefinedLogicOperator logicOp(method, std::make_unique<IL::LdLoc>(a),
                                         std::make_unique<IL::LdLoc>(b));
    CSharp::TranslationContext context;

    auto expr = builder.Visit(&logicOp, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
