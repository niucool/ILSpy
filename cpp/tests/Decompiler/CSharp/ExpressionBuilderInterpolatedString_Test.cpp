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

// The ExpressionBuilder VisitBlock / TranslateInterpolatedString suite: the
// DefaultInterpolatedStringHandler block render over the MinimalCorlib fixture --
// the AppendLiteral brace-escaping, the AppendFormatted value, alignment, and
// suffix arms, the String resolve result, the Visit dispatch, and the
// unsupported-BlockKind error. The other BlockKinds are deferred (Match*/CallBuilder).

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
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

    // An instance handler call (`DefaultInterpolatedStringHandler.<name>`), with the
    // receiver as Arguments[0] and the given parameter types. Variadic so the
    // move-only argument nodes can be passed directly (a braced init-list would
    // require copying the unique_ptrs).
    template <typename... Args>
    std::unique_ptr<IL::Call> MakeHandlerCall(const std::string& name,
                                              std::vector<TS::ITypePtr> paramTypes,
                                              Args&&... args)
    {
        auto call = std::make_unique<IL::Call>(
            "System.Runtime.CompilerServices.DefaultInterpolatedStringHandler::" + name);
        call->IsInstanceCall = true;
        call->ParameterIType = std::move(paramTypes);
        (call->AddArg(std::forward<Args>(args)), ...);
        return call;
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

TEST(ExpressionBuilderInterpolatedStringTest, RendersLiteralAndFormattedContent)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto value = fixture.MakeLocal(TS::KnownTypeCode::Int32, "value");

    IL::Block block;
    block.Kind = IL::BlockKind::InterpolatedString;
    block.Add(fixture.MakeHandlerCall(".ctor", {}, std::make_unique<IL::LdNull>()));
    block.Add(fixture.MakeHandlerCall(
        "AppendLiteral", {fixture.TypePtr(TS::KnownTypeCode::String)},
        std::make_unique<IL::LdNull>(), std::make_unique<IL::LdStr>("{x}")));
    block.Add(fixture.MakeHandlerCall(
        "AppendFormatted", {fixture.TypePtr(TS::KnownTypeCode::Int32)},
        std::make_unique<IL::LdNull>(), std::make_unique<IL::LdLoc>(value)));
    block.SetFinal(fixture.MakeHandlerCall("ToStringAndClear", {}));

    auto expr = builder.Translate(&block);

    auto* ise = dynamic_cast<Syntax::InterpolatedStringExpression*>(expr.Expression());
    ASSERT_TRUE(ise != nullptr);
    ASSERT_EQ(ise->Content().Count(), std::size_t{2});
    auto* text = dynamic_cast<Syntax::InterpolatedStringText*>(ise->Content()[0]);
    ASSERT_TRUE(text != nullptr);
    // The literal braces are escaped.
    EXPECT_EQ(text->Text(), "{{x}}");
    auto* interpolation = dynamic_cast<Syntax::Interpolation*>(ise->Content()[1]);
    ASSERT_TRUE(interpolation != nullptr);
    EXPECT_EQ(interpolation->Alignment(), 0);
    EXPECT_FALSE(interpolation->Suffix().has_value());
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderInterpolatedStringTest, RendersAlignmentAndSuffix)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto value = fixture.MakeLocal(TS::KnownTypeCode::Int32, "value");

    IL::Block block;
    block.Kind = IL::BlockKind::InterpolatedString;
    block.Add(fixture.MakeHandlerCall(".ctor", {}, std::make_unique<IL::LdNull>()));
    block.Add(fixture.MakeHandlerCall(
        "AppendFormatted", {fixture.TypePtr(TS::KnownTypeCode::Int32)},
        std::make_unique<IL::LdNull>(), std::make_unique<IL::LdLoc>(value),
        std::make_unique<IL::LdcI4>(5), std::make_unique<IL::LdStr>("X")));
    block.SetFinal(fixture.MakeHandlerCall("ToStringAndClear", {}));

    auto expr = builder.Translate(&block);

    auto* ise = dynamic_cast<Syntax::InterpolatedStringExpression*>(expr.Expression());
    ASSERT_TRUE(ise != nullptr);
    ASSERT_EQ(ise->Content().Count(), std::size_t{1});
    auto* interpolation = dynamic_cast<Syntax::Interpolation*>(ise->Content()[0]);
    ASSERT_TRUE(interpolation != nullptr);
    EXPECT_EQ(interpolation->Alignment(), 5);
    ASSERT_TRUE(interpolation->Suffix().has_value());
    EXPECT_EQ(*interpolation->Suffix(), "X");
}

TEST(ExpressionBuilderInterpolatedStringTest, VisitDispatchRoutesInterpolatedString)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block block;
    block.Kind = IL::BlockKind::InterpolatedString;
    block.Add(fixture.MakeHandlerCall(".ctor", {}, std::make_unique<IL::LdNull>()));
    block.Add(fixture.MakeHandlerCall(
        "AppendLiteral", {fixture.TypePtr(TS::KnownTypeCode::String)},
        std::make_unique<IL::LdNull>(), std::make_unique<IL::LdStr>("hello")));
    block.SetFinal(fixture.MakeHandlerCall("ToStringAndClear", {}));
    CSharp::TranslationContext context;

    auto expr = builder.Visit(&block, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

TEST(ExpressionBuilderInterpolatedStringTest, UnsupportedBlockKindReturnsError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block block;
    block.Kind = IL::BlockKind::ControlFlow;

    auto expr = builder.Translate(&block);

    EXPECT_NE(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
