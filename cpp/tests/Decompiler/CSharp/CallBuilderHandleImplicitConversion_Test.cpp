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

// The CallBuilder::HandleImplicitConversion render (the user-defined `op_Implicit`
// arm): the cast to the operator's target type with a ConversionResolveResult,
// and the `in`-direction unwrap -- over a real ExpressionBuilder + FakeMethod.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
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
namespace CSharp = ::ILSpy::Decompiler::CSharp;

struct Fixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    Fixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(), run(&settings, usingScope)
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

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    std::shared_ptr<Impl::FakeMethod> MakeImplicitMethod(TS::KnownTypeCode sourceCode,
                                                         TS::KnownTypeCode targetCode)
    {
        auto method =
            std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Operator);
        method->SetName("op_Implicit");
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(sourceCode));
        method->SetParameters({std::make_shared<Impl::DefaultParameter>(
            TypePtr(sourceCode), "value")});
        method->SetReturnType(TypePtr(targetCode));
        return method;
    }

    static CSharp::TranslatedExpression MakeArg(TS::ITypePtr type)
    {
        auto* ident = new Syntax::IdentifierExpression("x");
        return CSharp::WithRR(CSharp::WithoutILInstruction(*ident),
                              std::make_shared<Sem::ResolveResult>(std::move(type)));
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

TEST(CallBuilderHandleImplicitConversionTest, WrapsTheArgumentInACastToTheTargetType)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeImplicitMethod(TS::KnownTypeCode::Int32,
                                             TS::KnownTypeCode::Int64);
    auto argument = Fixture::MakeArg(fixture.TypePtr(TS::KnownTypeCode::Int32));

    auto result = callBuilder.HandleImplicitConversion(*method, argument);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(result.Expression());
    ASSERT_TRUE(cast != nullptr);
    ASSERT_NE(cast->Type(), nullptr);
    // The expression under the cast is the (unchanged) argument identifier.
    EXPECT_NE(dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression()), nullptr);
    auto* rr = dynamic_cast<const Sem::ConversionResolveResult*>(result.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
}

TEST(CallBuilderHandleImplicitConversionTest, UnwrapsAnInDirectionExpression)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeImplicitMethod(TS::KnownTypeCode::Int32,
                                             TS::KnownTypeCode::Int64);
    auto* inner = new Syntax::IdentifierExpression("x");
    auto* direction =
        new Syntax::DirectionExpression(Syntax::FieldDirection::In, inner);
    auto argument = CSharp::WithRR(
        CSharp::WithoutILInstruction(*direction),
        std::make_shared<Sem::ResolveResult>(fixture.TypePtr(TS::KnownTypeCode::Int32)));

    auto result = callBuilder.HandleImplicitConversion(*method, argument);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(result.Expression());
    ASSERT_TRUE(cast != nullptr);
    // The `in` wrapper is dropped; the identifier is what remains under the cast.
    EXPECT_EQ(cast->Expression(), inner);
}

} // namespace ILSpy::Tests
