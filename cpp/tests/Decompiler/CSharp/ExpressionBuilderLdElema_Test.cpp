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

// The ExpressionBuilder VisitLdElema suite: the managed-reference `ref arr[i]`
// render over the MinimalCorlib fixture -- the compatible-element passthrough,
// the multi-index argument list, the incompatible-element array-type conversion,
// the LdElema annotation on the indexer, and the Visit dispatch. The C# node's
// `WithSystemIndex` arm is unreachable in the port (the port's LdElema carries no
// such field), so every index goes through TranslateArrayIndex.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
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

    std::shared_ptr<IL::ILVariable> MakeArrayLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local, std::make_shared<TS::ArrayType>(TypePtr(code)));
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

// The `ref arr[index...]` shape: an outer DirectionExpression(Ref) over an
// IndexerExpression with the given identifier target.
Syntax::IndexerExpression* AsRefIndexer(Syntax::Expression* expr)
{
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr);
    if (direction == nullptr || direction->FieldDirection() != Syntax::FieldDirection::Ref)
        return nullptr;
    return dynamic_cast<Syntax::IndexerExpression*>(direction->Expression());
}

} // namespace

TEST(ExpressionBuilderLdElemaTest, SingleIndexRendersRefIndexer)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    IL::LdElema ldElema(fixture.TypePtr(TS::KnownTypeCode::Int32),
                        std::make_unique<IL::LdLoc>(arr), std::move(indices));

    auto expr = builder.Translate(&ldElema);

    // The arm returns a managed reference: DirectionExpression(Ref) over the indexer.
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::ByReference);
    auto* indexer = AsRefIndexer(expr.Expression());
    ASSERT_TRUE(indexer != nullptr);
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(indexer->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->Identifier(), "arr");
    ASSERT_EQ(indexer->Arguments().Count(), std::size_t{1});
    auto* index = dynamic_cast<Syntax::PrimitiveExpression*>(indexer->Arguments()[0]);
    ASSERT_TRUE(index != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&index->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 0);
}

TEST(ExpressionBuilderLdElemaTest, IndexerCarriesLdElemaAnnotation)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(1));
    IL::LdElema ldElema(fixture.TypePtr(TS::KnownTypeCode::Int32),
                        std::make_unique<IL::LdLoc>(arr), std::move(indices));

    auto expr = builder.Translate(&ldElema);

    auto* indexer = AsRefIndexer(expr.Expression());
    ASSERT_TRUE(indexer != nullptr);
    const auto annotations = CSharp::GetILInstructions(*indexer);
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], static_cast<IL::ILInstruction*>(&ldElema));
}

TEST(ExpressionBuilderLdElemaTest, MultiIndexRendersAllArguments)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(1));
    indices.push_back(std::make_unique<IL::LdcI4>(2));
    IL::LdElema ldElema(fixture.TypePtr(TS::KnownTypeCode::Int32),
                        std::make_unique<IL::LdLoc>(arr), std::move(indices));

    auto expr = builder.Translate(&ldElema);

    auto* indexer = AsRefIndexer(expr.Expression());
    ASSERT_TRUE(indexer != nullptr);
    EXPECT_EQ(indexer->Arguments().Count(), std::size_t{2});
}

TEST(ExpressionBuilderLdElemaTest, IncompatibleElementTypeConvertsArrayType)
{
    // `object[]` read as `int` elements: the element types are incompatible, so the
    // C# rebuilds an `int[]` array type and converts the operand (a cast).
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Object, "arr");
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    IL::LdElema ldElema(fixture.TypePtr(TS::KnownTypeCode::Int32),
                        std::make_unique<IL::LdLoc>(arr), std::move(indices));

    auto expr = builder.Translate(&ldElema);

    auto* indexer = AsRefIndexer(expr.Expression());
    ASSERT_TRUE(indexer != nullptr);
    // The operand was converted to the rebuilt element array type.
    EXPECT_NE(dynamic_cast<Syntax::CastExpression*>(indexer->Target()), nullptr);
}

TEST(ExpressionBuilderLdElemaTest, VisitDispatchRoutesLdElema)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    IL::LdElema ldElema(fixture.TypePtr(TS::KnownTypeCode::Int32),
                        std::make_unique<IL::LdLoc>(arr), std::move(indices));
    CSharp::TranslationContext context;
    context.TypeHint = &fixture.compilation.FindType(TS::KnownTypeCode::Int32);

    auto expr = builder.Visit(&ldElema, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
