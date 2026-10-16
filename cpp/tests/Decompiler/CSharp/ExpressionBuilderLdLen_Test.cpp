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

// The ExpressionBuilder VisitLdLen suite: the `arr.Length` / `arr.LongLength`
// member-reference render over the MinimalCorlib fixture -- the I4 -> Length /
// Int32 arm, the I8 and native-I -> LongLength / Int64 arm (the C# folds every
// non-I4 result type to LongLength), the IL-instruction annotation, and the
// Visit dispatch. The MinimalCorlib `System.Array` declares no properties, so the
// C# `arrayType.GetProperties(...).FirstOrDefault()` path takes its null-member
// fallback `ResolveResult(Int32/Int64)` arm; a real corlib's `Array.Length`/
// `LongLength` properties would take the `MemberResolveResult` arm instead.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
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

} // namespace

TEST(ExpressionBuilderLdLenTest, I4ResultRendersLengthProperty)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    IL::LdLen ldLen(IL::StackType::I4, std::make_unique<IL::LdLoc>(arr));

    auto expr = builder.Translate(&ldLen);

    auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(mre != nullptr);
    EXPECT_EQ(mre->MemberName(), "Length");
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(mre->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->Identifier(), "arr");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
    // The arm carries the LdLen IL annotation.
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], &ldLen);
}

TEST(ExpressionBuilderLdLenTest, I8ResultRendersLongLengthProperty)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::String, "arr");
    IL::LdLen ldLen(IL::StackType::I8, std::make_unique<IL::LdLoc>(arr));

    auto expr = builder.Translate(&ldLen);

    auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(mre != nullptr);
    EXPECT_EQ(mre->MemberName(), "LongLength");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
}

TEST(ExpressionBuilderLdLenTest, NativeIResultRendersLongLengthProperty)
{
    // The C# gates only on `ResultType == StackType.I4`, so the raw native-int
    // `ldlen` result also renders `LongLength` (resolved as Int64).
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    IL::LdLen ldLen(IL::StackType::I, std::make_unique<IL::LdLoc>(arr));

    auto expr = builder.Translate(&ldLen);

    auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(mre != nullptr);
    EXPECT_EQ(mre->MemberName(), "LongLength");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
}

TEST(ExpressionBuilderLdLenTest, VisitDispatchRoutesLdLen)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto arr = fixture.MakeArrayLocal(TS::KnownTypeCode::Int32, "arr");
    IL::LdLen ldLen(IL::StackType::I4, std::make_unique<IL::LdLoc>(arr));
    CSharp::TranslationContext context;
    context.TypeHint = &fixture.compilation.FindType(TS::KnownTypeCode::Int32);

    auto expr = builder.Visit(&ldLen, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
