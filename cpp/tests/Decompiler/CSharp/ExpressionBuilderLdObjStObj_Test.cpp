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

// The ExpressionBuilder VisitLdObj / VisitStObj suite: the typed memory load and
// store arms -- the pointer dereference, the managed-reference strip, the TypeHint
// override of the load type, the Unsafe.Read/Write<T> helper arm for a
// non-unmanaged-through-pointer access, the Assignment render, and the IL-instruction
// annotation -- all over the MinimalCorlib fixture shape of the sibling suites.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/PointerTypeReference.hpp"
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

TEST(ExpressionBuilderLdObjStObjTest, LdObjPointerTargetRendersDereference)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(intType), "p");
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(p), intType);

    auto expr = builder.Translate(&ldObj);

    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(uoe != nullptr);
    EXPECT_EQ(uoe->Operator(), Syntax::UnaryOperatorType::Dereference);
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(uoe->Expression());
    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->Identifier(), "p");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
    // The arm carries the LdObj IL annotation.
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], &ldObj);
}

TEST(ExpressionBuilderLdObjStObjTest, LdObjManagedRefTargetStripsRef)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto num = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::LdObj ldObj(std::make_unique<IL::LdLoca>(num), intType);

    auto expr = builder.Translate(&ldObj);

    // The managed `ref` is stripped by the dereference.
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->Identifier(), "num");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderLdObjStObjTest, LdObjTypeHintOverridesLoadTypeForUnsafeRead)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto byteType = fixture.TypePtr(TS::KnownTypeCode::Byte);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(byteType), "p");
    // The node's own load type is Object; a String type hint is a compatible
    // reference-type access, so the TypeHint override replaces the load type.
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(p), fixture.TypePtr(TS::KnownTypeCode::Object));

    auto expr = builder.Translate(&ldObj, &fixture.compilation.FindType(TS::KnownTypeCode::String));

    // byte* is incompatible with String and String is not unmanaged, so the
    // Unsafe.Read<string>(void*) intrinsic is emitted over the overridden type.
    auto* invoke = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invoke != nullptr);
    ASSERT_EQ(invoke->Arguments().Count(), std::size_t{1});
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderLdObjStObjTest, LdObjIncompatiblePointerManagedTypeUsesUnsafeRead)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto byteType = fixture.TypePtr(TS::KnownTypeCode::Byte);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(byteType), "p");
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(p), fixture.TypePtr(TS::KnownTypeCode::String));

    auto expr = builder.Translate(&ldObj);

    auto* invoke = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invoke != nullptr);
    ASSERT_EQ(invoke->Arguments().Count(), std::size_t{1});
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderLdObjStObjTest, StObjPointerTargetRendersAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(intType), "p");
    IL::StObj stObj(std::make_unique<IL::LdLoc>(p), std::make_unique<IL::LdcI4>(1), intType);

    auto expr = builder.Translate(&stObj);

    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::UnaryOperatorExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Operator(), Syntax::UnaryOperatorType::Dereference);
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assign->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 1);
    // The arm carries the StObj IL annotation.
    const auto annotations = expr.ILInstructions();
    ASSERT_EQ(annotations.size(), std::size_t{1});
    EXPECT_EQ(annotations[0], &stObj);
}

TEST(ExpressionBuilderLdObjStObjTest, StObjManagedRefTargetRendersAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto num = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::StObj stObj(std::make_unique<IL::LdLoca>(num), std::make_unique<IL::LdcI4>(2),
                    intType);

    auto expr = builder.Translate(&stObj);

    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "num");
}

TEST(ExpressionBuilderLdObjStObjTest, StObjNonUnmanagedTypeUsesUnsafeWrite)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto byteType = fixture.TypePtr(TS::KnownTypeCode::Byte);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(byteType), "p");
    auto s = fixture.MakeLocal(TS::KnownTypeCode::String, "s");
    // A non-managed-reference store of the non-unmanaged String type falls through
    // to the Unsafe.Write<T>(void*, T) helper.
    IL::StObj stObj(std::make_unique<IL::LdLoc>(p), std::make_unique<IL::LdLoc>(s),
                    fixture.TypePtr(TS::KnownTypeCode::String));

    auto expr = builder.Translate(&stObj);

    auto* invoke = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invoke != nullptr);
    ASSERT_EQ(invoke->Arguments().Count(), std::size_t{2});
    EXPECT_EQ(expr.Type().ReflectionName(), "System.String");
}

TEST(ExpressionBuilderLdObjStObjTest, VisitDispatchRoutesLdObjAndStObj)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = fixture.TypePtr(TS::KnownTypeCode::Int32);
    auto p = fixture.MakeLocalOverType(std::make_shared<TS::PointerType>(intType), "p");
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(p), intType);
    IL::StObj stObj(std::make_unique<IL::LdLoc>(p), std::make_unique<IL::LdcI4>(3), intType);
    CSharp::TranslationContext context;
    context.TypeHint = &fixture.compilation.FindType(TS::KnownTypeCode::Int32);

    auto ldExpr = builder.Visit(&ldObj, context);
    auto stExpr = builder.Visit(&stObj, context);

    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(ldExpr.Expression()), nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ErrorExpression*>(stExpr.Expression()), nullptr);
}

} // namespace ILSpy::Tests
