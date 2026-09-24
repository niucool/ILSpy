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

// The CallBuilder string-interpolation render (CallBuilder.cs lines 595-666 +
// 770-941): the TokenizeFormatString tokenizer matrix, the
// TryGetStringInterpolationTokens argument-shape gates, and the
// HandleStringInterpolation InterpolatedStringExpression render (the plain
// string.Format shape and the FormattableStringFactory.Create cast shape) --
// over a real ExpressionBuilder + FakeMethod.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::CallBuilder;
using CSharp::ExpressionBuilder;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace CSharp = ::ILSpy::Decompiler::CSharp;

struct Fixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;

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

    // The format-string argument: an expression carrying a constant string
    // resolve result (the C# `arguments[0] is ConstantResolveResult crr`).
    CSharp::TranslatedExpression MakeFormatArgument(const std::string& format)
    {
        auto* ident = new Syntax::IdentifierExpression("fmt");
        return CSharp::WithRR(
            CSharp::WithoutILInstruction(*ident),
            std::make_shared<Sem::ConstantResolveResult>(
                TypePtr(TS::KnownTypeCode::String), std::any(format)));
    }

    CSharp::TranslatedExpression MakeArg(TS::KnownTypeCode code, const char* name)
    {
        auto* ident = new Syntax::IdentifierExpression(name);
        return CSharp::WithRR(
            CSharp::WithoutILInstruction(*ident),
            std::make_shared<Sem::ResolveResult>(TypePtr(code)));
    }

    // Pushes `argument` and the matching (null) ExpectedParameters slot: a
    // real BuildArgumentList-produced list keeps the two arrays parallel, and
    // GetArgumentResolveResults indexes both.
    static void AddArgument(CallBuilder::ArgumentList& list,
                            CSharp::TranslatedExpression argument)
    {
        list.Arguments.push_back(std::move(argument));
        list.ExpectedParameters.push_back(nullptr);
    }

    std::shared_ptr<Impl::FakeMethod> MakeFormatMethod(const char* name)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        method->SetReturnType(TypePtr(TS::KnownTypeCode::String));
        return method;
    }

private:
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

TEST(CallBuilderStringInterpolationTest, TokenizeFormatStringMatrix)
{
    // Literal-only.
    auto literal = CallBuilder::TokenizeFormatString("Hello");
    ASSERT_EQ(literal.size(), 1u);
    EXPECT_EQ(literal[0].first, CallBuilder::TokenKind::String);
    ASSERT_TRUE(literal[0].second.has_value());
    EXPECT_EQ(*literal[0].second, "Hello");

    // One hole with surrounding literals.
    auto hole = CallBuilder::TokenizeFormatString("Hello {0}!");
    ASSERT_EQ(hole.size(), 3u);
    EXPECT_EQ(hole[0].first, CallBuilder::TokenKind::String);
    EXPECT_EQ(*hole[0].second, "Hello ");
    EXPECT_EQ(hole[1].first, CallBuilder::TokenKind::Argument);
    EXPECT_EQ(hole[1].second, "0");
    EXPECT_EQ(hole[2].first, CallBuilder::TokenKind::String);
    EXPECT_EQ(*hole[2].second, "!");

    // Escaped braces stay in the literal run.
    auto escaped = CallBuilder::TokenizeFormatString("{{x}}");
    ASSERT_EQ(escaped.size(), 1u);
    EXPECT_EQ(escaped[0].first, CallBuilder::TokenKind::String);
    EXPECT_EQ(*escaped[0].second, "{{x}}");

    // Alignment and format suffixes.
    auto aligned = CallBuilder::TokenizeFormatString("{0,-3}");
    ASSERT_EQ(aligned.size(), 1u);
    EXPECT_EQ(aligned[0].first, CallBuilder::TokenKind::ArgumentWithAlignment);
    EXPECT_EQ(aligned[0].second, "0,-3");
    auto formatted = CallBuilder::TokenizeFormatString("{1:N2}");
    ASSERT_EQ(formatted.size(), 1u);
    EXPECT_EQ(formatted[0].first, CallBuilder::TokenKind::ArgumentWithFormat);
    EXPECT_EQ(formatted[0].second, "1:N2");
    auto both = CallBuilder::TokenizeFormatString("{2,5:N2}");
    ASSERT_EQ(both.size(), 1u);
    EXPECT_EQ(both[0].first, CallBuilder::TokenKind::ArgumentWithAlignmentAndFormat);
    EXPECT_EQ(both[0].second, "2,5:N2");

    // An unterminated hole and a stray close brace yield Error tokens (the
    // stray-`}` scan continues: the C# yields the Error, leaves the literal
    // builder running, and the tail run still materializes).
    auto unterminated = CallBuilder::TokenizeFormatString("x{0");
    ASSERT_EQ(unterminated.size(), 2u);
    EXPECT_EQ(unterminated[0].first, CallBuilder::TokenKind::String);
    EXPECT_EQ(unterminated[1].first, CallBuilder::TokenKind::Error);
    auto stray = CallBuilder::TokenizeFormatString("a}b");
    ASSERT_EQ(stray.size(), 2u);
    EXPECT_EQ(stray[0].first, CallBuilder::TokenKind::Error);
    EXPECT_EQ(stray[1].first, CallBuilder::TokenKind::String);
    EXPECT_EQ(*stray[1].second, "ab");
}

TEST(CallBuilderStringInterpolationTest, TryAcceptsSequentialHoles)
{
    Fixture fixture;
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("Hello {0}, {1}!"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int32, "b"));

    std::string format;
    std::vector<CallBuilder::InterpolationToken> tokens;
    ASSERT_TRUE(CallBuilder::TryGetStringInterpolationTokens(list, format, tokens));
    EXPECT_EQ(format, "Hello {0}, {1}!");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].Kind, CallBuilder::TokenKind::String);
    EXPECT_EQ(*tokens[0].Format, "Hello ");
    EXPECT_EQ(tokens[1].Kind, CallBuilder::TokenKind::Argument);
    EXPECT_EQ(tokens[1].Index, 0);
    EXPECT_EQ(tokens[2].Kind, CallBuilder::TokenKind::String);
    EXPECT_EQ(tokens[2].Format, ", ");
    EXPECT_EQ(tokens[3].Kind, CallBuilder::TokenKind::Argument);
    EXPECT_EQ(tokens[3].Index, 1);
    EXPECT_EQ(tokens[4].Kind, CallBuilder::TokenKind::String);
    EXPECT_EQ(*tokens[4].Format, "!");
}

TEST(CallBuilderStringInterpolationTest, TryAcceptsAlignmentAndFormatHoles)
{
    Fixture fixture;
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("{0,-3}{1:N2}"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Double, "b"));

    std::string format;
    std::vector<CallBuilder::InterpolationToken> tokens;
    ASSERT_TRUE(CallBuilder::TryGetStringInterpolationTokens(list, format, tokens));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].Kind, CallBuilder::TokenKind::ArgumentWithAlignment);
    EXPECT_EQ(tokens[0].Index, 0);
    EXPECT_EQ(tokens[0].Alignment, -3);
    EXPECT_EQ(tokens[1].Kind, CallBuilder::TokenKind::ArgumentWithFormat);
    EXPECT_EQ(tokens[1].Index, 1);
    EXPECT_EQ(tokens[1].Alignment, 0);
    ASSERT_TRUE(tokens[1].Format.has_value());
    EXPECT_EQ(*tokens[1].Format, "N2");
}

TEST(CallBuilderStringInterpolationTest, TryRejectsNamedArgumentsAndArgumentMaps)
{
    Fixture fixture;
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("{0}"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    list.ArgumentNames = std::vector<std::string>{"x"};
    std::string format;
    std::vector<CallBuilder::InterpolationToken> tokens;
    EXPECT_FALSE(CallBuilder::TryGetStringInterpolationTokens(list, format, tokens));

    CallBuilder::ArgumentList list2;
    Fixture::AddArgument(list2, fixture.MakeFormatArgument("{0}"));
    Fixture::AddArgument(list2, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    list2.ArgumentToParameterMap = std::vector<int>{0};
    EXPECT_FALSE(CallBuilder::TryGetStringInterpolationTokens(list2, format, tokens));
}

TEST(CallBuilderStringInterpolationTest, TryRejectsStringLiteralAmongArguments)
{
    Fixture fixture;
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("{0}"));
    auto* literal = new Syntax::PrimitiveExpression(std::string("lit"));
    Fixture::AddArgument(list, CSharp::WithRR(
        CSharp::WithoutILInstruction(*literal),
        std::make_shared<Sem::ResolveResult>(fixture.TypePtr(TS::KnownTypeCode::String))));
    std::string format;
    std::vector<CallBuilder::InterpolationToken> tokens;
    EXPECT_FALSE(CallBuilder::TryGetStringInterpolationTokens(list, format, tokens));
}

TEST(CallBuilderStringInterpolationTest, TryRejectsNonSequentialAndNonConstantFormats)
{
    Fixture fixture;
    // Holes out of order.
    CallBuilder::ArgumentList swapped;
    Fixture::AddArgument(swapped, fixture.MakeFormatArgument("{1}{0}"));
    Fixture::AddArgument(swapped, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    Fixture::AddArgument(swapped, fixture.MakeArg(TS::KnownTypeCode::String, "b"));
    std::string format;
    std::vector<CallBuilder::InterpolationToken> tokens;
    EXPECT_FALSE(CallBuilder::TryGetStringInterpolationTokens(swapped, format, tokens));

    // A non-constant first argument.
    CallBuilder::ArgumentList nonConstant;
    Fixture::AddArgument(nonConstant, fixture.MakeArg(TS::KnownTypeCode::String, "fmt"));
    Fixture::AddArgument(nonConstant, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    EXPECT_FALSE(CallBuilder::TryGetStringInterpolationTokens(nonConstant, format, tokens));
}

TEST(CallBuilderStringInterpolationTest, RenderBuildsTheContentAndResolveResult)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto format = fixture.MakeFormatMethod("Format");
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("Hello {0}, {1:N2}!"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::String, "a"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Double, "b"));

    auto result = callBuilder.HandleStringInterpolation(*format, list);

    auto* interpolated =
        dynamic_cast<Syntax::InterpolatedStringExpression*>(result.Expression());
    ASSERT_TRUE(interpolated != nullptr);
    ASSERT_EQ(interpolated->Content().Count(), 5);
    auto* text0 =
        dynamic_cast<Syntax::InterpolatedStringText*>(interpolated->Content().At(0));
    ASSERT_TRUE(text0 != nullptr);
    EXPECT_EQ(text0->Text(), "Hello ");
    auto* first = dynamic_cast<Syntax::Interpolation*>(interpolated->Content().At(1));
    ASSERT_TRUE(first != nullptr);
    EXPECT_EQ(first->Alignment(), 0);
    auto* middle =
        dynamic_cast<Syntax::InterpolatedStringText*>(interpolated->Content().At(2));
    ASSERT_TRUE(middle != nullptr);
    EXPECT_EQ(middle->Text(), ", ");
    auto* second = dynamic_cast<Syntax::Interpolation*>(interpolated->Content().At(3));
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(second->Alignment(), 0);
    ASSERT_TRUE(second->Suffix().has_value());
    EXPECT_EQ(*second->Suffix(), "N2");

    // The expression's resolve result is the InterpolatedStringResolveResult
    // over the arguments past the format string.
    const auto* isrr = dynamic_cast<const Sem::InterpolatedStringResolveResult*>(
        CSharp::GetResolveResult(*interpolated));
    ASSERT_TRUE(isrr != nullptr);
    EXPECT_EQ(isrr->FormatString(), "Hello {0}, {1:N2}!");
    EXPECT_EQ(isrr->Arguments().size(), 2u);
}

TEST(CallBuilderStringInterpolationTest, CastShapeWrapsForFormattableStringCreate)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto create = fixture.MakeFormatMethod("Create");
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("x = {0}"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));

    auto result = callBuilder.HandleStringInterpolation(*create, list);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(result.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_NE(dynamic_cast<Syntax::InterpolatedStringExpression*>(cast->Expression()),
              nullptr);
    const auto* conversion =
        dynamic_cast<const Sem::ConversionResolveResult*>(result.ResolveResult());
    ASSERT_TRUE(conversion != nullptr);
}

TEST(CallBuilderStringInterpolationTest, ReturnsDefaultOnBadTokens)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto format = fixture.MakeFormatMethod("Format");
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeFormatArgument("{0"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::String, "a"));

    auto result = callBuilder.HandleStringInterpolation(*format, list);
    EXPECT_EQ(result.Expression(), nullptr);
}

} // namespace ILSpy::Tests