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

// The CallBuilder-prerequisite suite: Call::ExpectedTypeForThisPointer (the
// reference/value/constrained/unknown stack-type dispatch) and
// ExpressionBuilder::TranslateTarget (the instance identifier, the static type
// reference, the value-type managed-reference unwrap, and the base reference)
// over the MinimalCorlib fixture.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

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

    // The builder over a context whose CurrentTypeDefinition is `td` (the
    // base-reference arm needs a current type distinct from the member's).
    ExpressionBuilder MakeBuilderWithCurrentType(const TS::ITypeDefinition* td)
    {
        CSharp::TypeSystem::CSharpTypeResolveContext context(
            compilation.MainModule(), usingScope, td);
        return ExpressionBuilder(nullptr, compilation, context, &function_, &settings, &run);
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

    // The `this` parameter (Kind Parameter + a negative index, the reader
    // convention).
    std::shared_ptr<IL::ILVariable> MakeThis()
    {
        auto self = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter,
                                                     TypePtr(TS::KnownTypeCode::Object));
        self->Name = "this";
        self->Index = -1;
        return self;
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

TEST(ExpressionBuilderTranslateTargetTest, ExpectedTypeForThisPointerReferenceType)
{
    BuilderFixture fixture;
    EXPECT_EQ(IL::Call::ExpectedTypeForThisPointer(
                  fixture.compilation.FindType(TS::KnownTypeCode::String)),
              IL::StackType::O);
}

TEST(ExpressionBuilderTranslateTargetTest, ExpectedTypeForThisPointerValueType)
{
    BuilderFixture fixture;
    EXPECT_EQ(IL::Call::ExpectedTypeForThisPointer(
                  fixture.compilation.FindType(TS::KnownTypeCode::Int32)),
              IL::StackType::Ref);
}

TEST(ExpressionBuilderTranslateTargetTest, ExpectedTypeForThisPointerConstrained)
{
    BuilderFixture fixture;
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    EXPECT_EQ(IL::Call::ExpectedTypeForThisPointer(intType, &intType), IL::StackType::Ref);
}

TEST(ExpressionBuilderTranslateTargetTest, ExpectedTypeForThisPointerUnknown)
{
    BuilderFixture fixture;
    (void)fixture;
    EXPECT_EQ(IL::Call::ExpectedTypeForThisPointer(*TS::UnknownType()), IL::StackType::Unknown);
}

TEST(ExpressionBuilderTranslateTargetTest, InstanceTargetRendersIdentifier)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    const TS::IType& declaring = fixture.compilation.FindType(TS::KnownTypeCode::Object);
    IL::LdLoc target(obj);

    auto expr = builder.TranslateTarget(&target, /*nonVirtualInvocation=*/false,
                                        /*memberStatic=*/false,
                                        const_cast<TS::IType&>(declaring));

    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->Identifier(), "obj");
}

TEST(ExpressionBuilderTranslateTargetTest, StaticMemberRendersTypeReference)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    const TS::IType& declaring = fixture.compilation.FindType(TS::KnownTypeCode::String);

    auto expr = builder.TranslateTarget(nullptr, /*nonVirtualInvocation=*/false,
                                        /*memberStatic=*/true,
                                        const_cast<TS::IType&>(declaring));

    EXPECT_NE(dynamic_cast<Syntax::TypeReferenceExpression*>(expr.Expression()), nullptr);
    EXPECT_NE(dynamic_cast<const Sem::TypeResolveResult*>(expr.ResolveResult()), nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderTranslateTargetTest, ValueTypeTargetUnwrapsManagedReference)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto arr = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::make_shared<TS::ArrayType>(intType));
    arr->Name = "arr";
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    IL::LdElema target(intType, std::make_unique<IL::LdLoc>(arr), std::move(indices));
    // A value-type declaring type -> the ref hint -> the managed-reference unwrap.
    const TS::IType& declaring = fixture.compilation.FindType(TS::KnownTypeCode::Int32);

    auto expr = builder.TranslateTarget(&target, /*nonVirtualInvocation=*/false,
                                        /*memberStatic=*/false,
                                        const_cast<TS::IType&>(declaring));

    // The DirectionExpression(Ref) was unwrapped, leaving the inner indexer.
    EXPECT_NE(dynamic_cast<Syntax::IndexerExpression*>(expr.Expression()), nullptr);
}

TEST(ExpressionBuilderTranslateTargetTest, NonVirtualThisTargetRendersBaseReference)
{
    BuilderFixture fixture;
    const TS::ITypeDefinition* currentType =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    ASSERT_TRUE(currentType != nullptr);
    auto builder = fixture.MakeBuilderWithCurrentType(currentType);
    auto self = fixture.MakeThis();
    IL::LdLoc target(self);
    // The member is declared on String, the current type is Object -> base.
    const TS::IType& declaring = fixture.compilation.FindType(TS::KnownTypeCode::String);

    auto expr = builder.TranslateTarget(&target, /*nonVirtualInvocation=*/true,
                                        /*memberStatic=*/false,
                                        const_cast<TS::IType&>(declaring));

    EXPECT_NE(dynamic_cast<Syntax::BaseReferenceExpression*>(expr.Expression()), nullptr);
    auto* rr = dynamic_cast<const Sem::ThisResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    EXPECT_TRUE(rr->CausesNonVirtualInvocation());
}

} // namespace ILSpy::Tests
