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

// The ExpressionBuilder VisitNullCoalescingInstruction suite: the `a ?? b`
// BinaryOperatorExpression render over the MinimalCorlib fixture -- the Ref kind
// over reference types, the Nullable kind over Nullable<T>, the throw-fallback
// arm whose NoType fallback recovers the value's underlying type, and the Visit
// dispatch. The resolver's ResolveBinaryOperator drives the resolve result (the
// IsError fallback re-types through NullableType).

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
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

    std::shared_ptr<IL::ILVariable> MakeLocalOverType(TS::ITypePtr type, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                      std::move(type));
        local->Name = name;
        return local;
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

TEST(ExpressionBuilderNullCoalescingTest, RefKindRendersNullCoalescingOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    IL::NullCoalescingInstruction inst(IL::NullCoalescingKind::Ref,
                                       std::make_unique<IL::LdLoc>(a),
                                       std::make_unique<IL::LdLoc>(b));

    auto expr = builder.Translate(&inst);

    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(bin->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "a");
    auto* right = dynamic_cast<Syntax::IdentifierExpression*>(bin->Right());
    ASSERT_TRUE(right != nullptr);
    EXPECT_EQ(right->Identifier(), "b");
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], static_cast<IL::ILInstruction*>(&inst));
}

TEST(ExpressionBuilderNullCoalescingTest, NullableKindRendersNullCoalescingOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto nullableType = TS::Create(
        fixture.compilation, fixture.compilation.FindType(TS::KnownTypeCode::Int32));
    auto a = fixture.MakeLocalOverType(nullableType, "a");
    auto b = fixture.MakeLocalOverType(nullableType, "b");
    IL::NullCoalescingInstruction inst(IL::NullCoalescingKind::Nullable,
                                       std::make_unique<IL::LdLoc>(a),
                                       std::make_unique<IL::LdLoc>(b));

    auto expr = builder.Translate(&inst);

    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(bin->Left()), nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(bin->Right()), nullptr);
}

TEST(ExpressionBuilderNullCoalescingTest, ThrowFallbackRecoversUnderlyingValueType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto nullableType = TS::Create(
        fixture.compilation, fixture.compilation.FindType(TS::KnownTypeCode::Int32));
    auto a = fixture.MakeLocalOverType(nullableType, "a");
    IL::NullCoalescingInstruction inst(IL::NullCoalescingKind::Nullable,
                                       std::make_unique<IL::LdLoc>(a),
                                       std::make_unique<IL::Throw>(
                                           std::make_unique<IL::LdLoc>(a)));
    inst.UnderlyingResultType = IL::StackType::I4;

    auto expr = builder.Translate(&inst);

    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    EXPECT_NE(dynamic_cast<Syntax::ThrowExpression*>(bin->Right()), nullptr);
    // The NoType throw fallback recovers the value's underlying Int32.
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderNullCoalescingTest, VisitDispatchRoutesNullCoalescingInstruction)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = fixture.MakeLocal(TS::KnownTypeCode::String, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::String, "b");
    IL::NullCoalescingInstruction inst(IL::NullCoalescingKind::Ref,
                                       std::make_unique<IL::LdLoc>(a),
                                       std::make_unique<IL::LdLoc>(b));
    CSharp::TranslationContext context;

    auto expr = builder.Visit(&inst, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
