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

// The ExpressionBuilder boxing/unboxing/cast-arm suite: VisitUnboxAny (the
// `unbox.any T(isinst T(expr))` -> `expr as T` rewrite and the general
// `(T)expr` cast with the UnboxingConversion resolve result), VisitBox (the
// `(object)expr` cast with the BoxingConversion resolve result), and
// VisitCastClass (the ConvertTo passthrough) over the MinimalCorlib fixture,
// plus the Visit dispatch for the three opcodes. The port has no separate
// `Unbox` node -- the IL reader folds `unbox` into `UnboxAny` -- so there is no
// VisitUnbox arm to port.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
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

// The ConversionResolveResult's conversion, or null.
const Sem::Conversion* ConversionOf(const CSharp::TranslatedExpression& expr)
{
    if (const auto* crr =
            dynamic_cast<const Sem::ConversionResolveResult*>(expr.ResolveResult()))
        return crr->ConversionProperty();
    return nullptr;
}

} // namespace

TEST(ExpressionBuilderUnboxBoxCastTest, UnboxAnyIsInstSameReferenceTypeRendersAsExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    auto strType = fixture.TypePtr(TS::KnownTypeCode::String);
    auto isInst = std::make_unique<IL::IsInst>(strType,
                                               std::make_unique<IL::LdLoc>(obj));
    IL::UnboxAny unboxAny(strType, std::move(isInst));

    auto expr = builder.Translate(&unboxAny);

    // unbox.any T(isinst T(expr)) => expr as T
    auto* asExpr = dynamic_cast<Syntax::AsExpression*>(expr.Expression());
    ASSERT_TRUE(asExpr != nullptr);
    auto* arg = dynamic_cast<Syntax::IdentifierExpression*>(asExpr->Expression());
    ASSERT_TRUE(arg != nullptr);
    EXPECT_EQ(arg->Identifier(), "obj");
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], static_cast<IL::ILInstruction*>(&unboxAny));
}

TEST(ExpressionBuilderUnboxBoxCastTest, UnboxAnyGeneralRendersUnboxingCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::UnboxAny unboxAny(fixture.TypePtr(TS::KnownTypeCode::Int32),
                          std::make_unique<IL::LdLoc>(obj));

    auto expr = builder.Translate(&unboxAny);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* arg = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(arg != nullptr);
    EXPECT_EQ(arg->Identifier(), "obj");
    const Sem::Conversion* conversion = ConversionOf(expr);
    ASSERT_TRUE(conversion != nullptr);
    EXPECT_TRUE(conversion->IsUnboxingConversion());
}

TEST(ExpressionBuilderUnboxBoxCastTest, BoxRendersBoxingCastToObject)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Box box(fixture.TypePtr(TS::KnownTypeCode::Int32),
                std::make_unique<IL::LdcI4>(1));

    auto expr = builder.Translate(&box);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* arg = dynamic_cast<Syntax::PrimitiveExpression*>(cast->Expression());
    ASSERT_TRUE(arg != nullptr);
    const Sem::Conversion* conversion = ConversionOf(expr);
    ASSERT_TRUE(conversion != nullptr);
    EXPECT_TRUE(conversion->IsBoxingConversion());
}

TEST(ExpressionBuilderUnboxBoxCastTest, CastClassRendersConvertTo)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::CastClass castClass(fixture.TypePtr(TS::KnownTypeCode::String),
                            std::make_unique<IL::LdLoc>(obj));

    auto expr = builder.Translate(&castClass);

    // Object -> String is a reference cast.
    EXPECT_NE(dynamic_cast<Syntax::CastExpression*>(expr.Expression()), nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderUnboxBoxCastTest, VisitDispatchRoutesUnboxAnyBoxAndCastClass)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto obj = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    auto strType = fixture.TypePtr(TS::KnownTypeCode::String);
    IL::UnboxAny unboxAny(strType, std::make_unique<IL::LdLoc>(obj));
    IL::Box box(fixture.TypePtr(TS::KnownTypeCode::Int32),
                std::make_unique<IL::LdcI4>(2));
    IL::CastClass castClass(strType, std::make_unique<IL::LdLoc>(obj));
    CSharp::TranslationContext context;
    context.TypeHint = &fixture.compilation.FindType(TS::KnownTypeCode::Object);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(
                  builder.Visit(&unboxAny, context).Expression()),
              nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(
                  builder.Visit(&box, context).Expression()),
              nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(
                  builder.Visit(&castClass, context).Expression()),
              nullptr);
}

} // namespace ILSpy::Tests
