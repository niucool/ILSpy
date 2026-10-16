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

// The ExpressionBuilder nullable `?.` rewrap/unwrap suite: VisitNullableRewrap
// (the NullConditionalRewrap operator with the non-nullable-value-type
// Nullable<T> wrap), VisitNullableUnwrap (the NullConditional operator with the
// GetUnderlyingType result and the RefInput-but-not-RefOutput managed-reference
// strip), and the Visit dispatch for both opcodes, over the MinimalCorlib
// fixture.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

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

TEST(ExpressionBuilderNullableRewrapUnwrapTest, RewrapNonNullableValueTypeWrapsIntoNullable)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto num = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::NullableRewrap rewrap(std::make_unique<IL::LdLoc>(num));

    auto expr = builder.Translate(&rewrap);

    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(uoe != nullptr);
    EXPECT_EQ(uoe->Operator(), Syntax::UnaryOperatorType::NullConditionalRewrap);
    auto* arg = dynamic_cast<Syntax::IdentifierExpression*>(uoe->Expression());
    ASSERT_TRUE(arg != nullptr);
    EXPECT_EQ(arg->Identifier(), "num");
    // The non-nullable int becomes Nullable<int>.
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::NullableOfT));
}

TEST(ExpressionBuilderNullableRewrapUnwrapTest, RewrapReferenceTypeKeepsType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto text = fixture.MakeLocal(TS::KnownTypeCode::String, "text");
    IL::NullableRewrap rewrap(std::make_unique<IL::LdLoc>(text));

    auto expr = builder.Translate(&rewrap);

    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(uoe != nullptr);
    EXPECT_EQ(uoe->Operator(), Syntax::UnaryOperatorType::NullConditionalRewrap);
    // A reference type is already nullable, so the type is unchanged.
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderNullableRewrapUnwrapTest, UnwrapRendersNullConditionalAndUnderlyingType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto nullableType = TS::Create(
        fixture.compilation, fixture.compilation.FindType(TS::KnownTypeCode::Int32));
    auto num = fixture.MakeLocalOverType(nullableType, "num");
    IL::NullableUnwrap unwrap(IL::StackType::I4, std::make_unique<IL::LdLoc>(num));

    auto expr = builder.Translate(&unwrap);

    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(uoe != nullptr);
    EXPECT_EQ(uoe->Operator(), Syntax::UnaryOperatorType::NullConditional);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderNullableRewrapUnwrapTest, UnwrapRefInputStripsManagedReference)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto arr = fixture.MakeLocalOverType(std::make_shared<TS::ArrayType>(intType), "arr");
    // An LdElema produces a DirectionExpression(Ref); the RefInput-but-not-RefOutput
    // unwrap strips that managed reference before wrapping it.
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    auto elema = std::make_unique<IL::LdElema>(intType, std::make_unique<IL::LdLoc>(arr),
                                               std::move(indices));
    IL::NullableUnwrap unwrap(IL::StackType::I4, std::move(elema), /*refInput=*/true);

    auto expr = builder.Translate(&unwrap);

    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(uoe != nullptr);
    EXPECT_EQ(uoe->Operator(), Syntax::UnaryOperatorType::NullConditional);
    // The DirectionExpression was stripped, leaving the inner indexer.
    EXPECT_NE(dynamic_cast<Syntax::IndexerExpression*>(uoe->Expression()), nullptr);
}

TEST(ExpressionBuilderNullableRewrapUnwrapTest, VisitDispatchRoutesNullableRewrapAndUnwrap)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto num = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::NullableRewrap rewrap(std::make_unique<IL::LdLoc>(num));
    IL::NullableUnwrap unwrap(IL::StackType::I4, std::make_unique<IL::LdLoc>(num));
    CSharp::TranslationContext context;

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(
                  builder.Visit(&rewrap, context).Expression()),
              nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(
                  builder.Visit(&unwrap, context).Expression()),
              nullptr);
}

} // namespace ILSpy::Tests
