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

// The ExpressionBuilder VisitRefAnyType suite: the
// `__reftype(arg).TypeHandle` render over the MinimalCorlib fixture -- the
// UndocumentedExpression(RefType) wrapper with the translated argument, the
// TypeHandle MemberReferenceExpression, the RuntimeTypeHandle resolve result, the
// IL annotation, and the Visit dispatch.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
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

TEST(ExpressionBuilderRefAnyTypeTest, RendersTypeHandleMemberOverRefType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::RefAnyType refAnyType(std::make_unique<IL::LdLoc>(obj));

    auto expr = builder.Translate(&refAnyType);

    auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(mre != nullptr);
    EXPECT_EQ(mre->MemberName(), "TypeHandle");
    auto* undoc = dynamic_cast<Syntax::UndocumentedExpression*>(mre->Target());
    ASSERT_TRUE(undoc != nullptr);
    EXPECT_EQ(undoc->UndocumentedExpressionType(),
              Syntax::UndocumentedExpressionType::RefType);
    ASSERT_EQ(undoc->Arguments().Count(), std::size_t{1});
    auto* arg = dynamic_cast<Syntax::IdentifierExpression*>(undoc->Arguments()[0]);
    ASSERT_TRUE(arg != nullptr);
    EXPECT_EQ(arg->Identifier(), "obj");
    // The resolved RuntimeTypeHandle (the MinimalCorlib UnknownType fallback
    // carrying the requested full name).
    EXPECT_EQ(expr.Type().ReflectionName(), "System.RuntimeTypeHandle");
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], static_cast<IL::ILInstruction*>(&refAnyType));
}

TEST(ExpressionBuilderRefAnyTypeTest, VisitDispatchRoutesRefAnyType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::RefAnyType refAnyType(std::make_unique<IL::LdLoc>(obj));
    CSharp::TranslationContext context;

    auto expr = builder.Visit(&refAnyType, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
