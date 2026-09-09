// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The StatementBuilder skeleton tests (the ctor, the Convert/ConvertAsBlock entry
// family, the real Default fallback, and the leaf Visit arms IsInst/StLoc/StObj/Nop/
// IfInstruction) over the BuilderFixture shape -- the MinimalCorlib compilation +
// using scope + DecompileRun the ExpressionBuilder skeleton suite uses, with an
// ILFunction whose Body is a BlockContainer (the C# ctor casts it to the
// currentReturnContainer).

#include "Decompiler/CSharp/StatementBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace IL = ::ILSpy::Decompiler::IL;

// The default settings bag (the C# `new DecompilerSettings()`).
DecompilerSettings DefaultSettings()
{
    return DecompilerSettings{};
}

// The StatementBuilder fixture over MinimalCorlib -- the ExpressionBuilder skeleton
// suite's BuilderFixture shape with an ILFunction whose Body is a BlockContainer.
struct StatementFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function;

    StatementFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(DefaultSettings()), run(&settings, usingScope)
    {
        // The C# `this.currentReturnContainer = (BlockContainer)currentFunction.Body`.
        function.Body = std::make_unique<IL::BlockContainer>();
        function.Body->Parent = &function;
    }

    // The root using scope over the compilation's global namespace (the
    // CSharpTypeResolveContext_Test fixture shape).
    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    // The decompilation context over the compilation (a CSharpTypeResolveContext --
    // the C# `decompilationContext` is a CSharpTypeResolveContext in practice).
    const CSharp::TypeSystem::CSharpTypeResolveContext& FixtureContext()
    {
        if (!context_)
        {
            context_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
                compilation.MainModule(), usingScope);
        }
        return *context_;
    }

    CSharp::StatementBuilder MakeBuilder()
    {
        return CSharp::StatementBuilder(compilation, FixtureContext(), &function, &settings,
                                        &run);
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local,
            std::const_pointer_cast<TS::IType>(
                compilation.FindType(code).shared_from_this()));
        local->Name = name;
        return local;
    }

private:
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
};

// The statement's IL-instruction annotations, read through the
// TranslatedStatement channel the C# TranslatedStatement.ILInstructions models.
std::vector<IL::ILInstruction*> StatementILInstructions(const Syntax::Statement& statement)
{
    return CSharp::GetILInstructions(statement);
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor
// ---------------------------------------------------------------------------

TEST(StatementBuilderTest, CtorGuardsRejectNullArguments)
{
    StatementFixture fixture;
    try
    {
        CSharp::StatementBuilder builder(fixture.compilation, fixture.FixtureContext(),
                                         nullptr, &fixture.settings, &fixture.run);
        FAIL() << "expected std::invalid_argument for a null currentFunction";
    }
    catch (const std::invalid_argument&)
    {
    }
    try
    {
        CSharp::StatementBuilder builder(fixture.compilation, fixture.FixtureContext(),
                                         &fixture.function, nullptr, &fixture.run);
        FAIL() << "expected std::invalid_argument for null settings";
    }
    catch (const std::invalid_argument&)
    {
    }
    try
    {
        CSharp::StatementBuilder builder(fixture.compilation, fixture.FixtureContext(),
                                         &fixture.function, &fixture.settings, nullptr);
        FAIL() << "expected std::invalid_argument for a null decompileRun";
    }
    catch (const std::invalid_argument&)
    {
    }
}

TEST(StatementBuilderTest, CtorCopiesStateAndBuildsExpressionBuilder)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    ASSERT_TRUE(builder.exprBuilder != nullptr);
    ASSERT_TRUE(fixture.function.Body != nullptr);
    EXPECT_EQ(builder.currentReturnContainer, fixture.function.Body.get());
    EXPECT_EQ(builder.currentFunction, &fixture.function);
    // The iterator flag is the YieldReturnDecompiler-set C# field, copied as-is
    // (the seed reader never sets it, so the default is false).
    EXPECT_FALSE(builder.currentIsIterator);
    EXPECT_FALSE(builder.EmitAsRefReadOnly);
    // The result type is the non-async function's own return type (null on the
    // seed path -- the C# ctor's `currentFunction.ReturnType`).
    EXPECT_EQ(builder.currentResultType, fixture.function.ReturnType.get());
}

TEST(StatementBuilderTest, CtorCopiesIteratorAndAsyncResultType)
{
    StatementFixture fixture;
    // The async shape: the C# `currentFunction.IsAsync ? AsyncReturnType! :
    // ReturnType` picks the async return element type.
    fixture.function.AsyncReturnType =
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    fixture.function.IsIterator = true;
    auto builder = fixture.MakeBuilder();
    EXPECT_TRUE(builder.currentIsIterator);
    EXPECT_TRUE(fixture.function.IsAsync());
    ASSERT_TRUE(builder.currentResultType != nullptr);
    EXPECT_EQ(builder.currentResultType->ReflectionName(), "System.Int32");
}

// ---------------------------------------------------------------------------
// The entry family
// ---------------------------------------------------------------------------

// An instruction with no dedicated statement Visit method degrades to the C#'s
// own Default fallback: an ExpressionStatement over the expression translation
// (a bare LdNull renders the null literal).
TEST(StatementBuilderTest, ConvertDefaultRendersExpressionStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdNull ldnull;
    auto* stmt = builder.Convert(&ldnull);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::NullReferenceExpression*>(
                    expressionStatement->Expression()) != nullptr);
    // The Default attaches the IL instruction annotation to the statement.
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &ldnull);
}

// ConvertAsBlock wraps a non-block statement in a fresh BlockStatement (the C#
// `stmt as BlockStatement ?? new BlockStatement { stmt }`).
TEST(StatementBuilderTest, ConvertAsBlockWrapsNonBlockStatements)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdNull ldnull;
    auto* block = builder.ConvertAsBlock(&ldnull);
    ASSERT_TRUE(block != nullptr);
    ASSERT_EQ(block->Statements().Count(), std::size_t(1));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(block->Statements()[0])
                != nullptr);
    // The re-attached IL instruction annotation rides the statement -- and the C#
    // ConvertAsBlock attaches it a SECOND time over the arm-attached one (the C#
    // AddAnnotation does not dedupe), so the statement carries both.
    const auto instructions = StatementILInstructions(*block->Statements()[0]);
    ASSERT_EQ(instructions.size(), std::size_t(2));
    EXPECT_EQ(instructions[0], &ldnull);
    EXPECT_EQ(instructions[1], &ldnull);
}

// ---------------------------------------------------------------------------
// The Visit arms
// ---------------------------------------------------------------------------

TEST(StatementBuilderTest, VisitStLocRendersAssignmentStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::StLoc stLoc(variable, std::make_unique<IL::LdcI4>(42));
    auto* stmt = builder.Convert(&stLoc);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "num");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assign->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &stLoc);
}

// The statement-level store strips the top-level ref on ref re-assignment: the
// ExpressionBuilder's `ref (a = ref b)` DirectionExpression wrapper unwraps to the
// plain assignment expression.
TEST(StatementBuilderTest, VisitStLocStripsTopLevelRef)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto target = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    target->Name = "refLocal";
    auto source = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    source->Name = "other";
    IL::StLoc stLoc(target, std::make_unique<IL::LdLoca>(source));
    auto* stmt = builder.Convert(&stLoc);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    // ref refLocal = ref other (the outer ref wrapper stripped).
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "refLocal");
    auto* right = dynamic_cast<Syntax::DirectionExpression*>(assign->Right());
    ASSERT_TRUE(right != nullptr);
}

// The StObj sibling shares the VisitStLoc shape (the inner ExpressionBuilder's
// StObj arm is its own later slice, so the inner expression renders the
// not-supported error while the statement-level shape and annotation hold).
TEST(StatementBuilderTest, VisitStObjRendersExpressionStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::PointerType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    variable->Name = "p";
    IL::StObj stObj(std::make_unique<IL::LdLoca>(variable),
                    std::make_unique<IL::LdcI4>(7),
                    std::const_pointer_cast<TS::IType>(intType.shared_from_this()));
    auto* stmt = builder.Convert(&stObj);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &stObj);
}

// A nop renders the empty statement; the C# `Nop.Comment` field attaches as
// trailing trivia when set.
TEST(StatementBuilderTest, VisitNopRendersEmptyStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Nop nop;
    auto* stmt = builder.Convert(&nop);
    EXPECT_TRUE(dynamic_cast<Syntax::EmptyStatement*>(stmt) != nullptr);
    EXPECT_TRUE(stmt->TrailingTrivia().empty());
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &nop);
}

TEST(StatementBuilderTest, VisitNopAttachesCommentTrivia)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Nop nop;
    nop.Comment = "TODO";
    auto* stmt = builder.Convert(&nop);
    EXPECT_TRUE(dynamic_cast<Syntax::EmptyStatement*>(stmt) != nullptr);
    const auto trivia = stmt->TrailingTrivia();
    ASSERT_EQ(trivia.size(), std::size_t(1));
    auto* comment = dynamic_cast<Syntax::Comment*>(trivia[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), "TODO");
}

// The C# Nop partial's Kind field renders 'nop.pop' in the IL dump.
TEST(StatementBuilderTest, NopKindRenderCarriesThePopSuffix)
{
    IL::Nop nop;
    std::string dump;
    nop.WriteTo(dump);
    EXPECT_EQ(dump, "nop");
    IL::Nop pop;
    pop.Kind = IL::NopKind::Pop;
    pop.WriteTo(dump);
    EXPECT_NE(dump.find("nop.pop"), std::string::npos);
    EXPECT_EQ(dump.find(" // "), std::string::npos);
    IL::Nop commented;
    commented.Kind = IL::NopKind::Pop;
    commented.Comment = "the comment";
    std::string commentedDump;
    commented.WriteTo(commentedDump);
    EXPECT_EQ(commentedDump, "nop.pop // the comment");
}

// The unused-result isinst test renders `expr is T` with the boolean resolve
// result over the boxing-unwrapped argument.
TEST(StatementBuilderTest, VisitIsInstRendersIsExpression)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The argument is the VALUE on the evaluation stack (an ldloc of an
    // Object-typed local), not an address: an LdLoca argument would translate to
    // a DirectionExpression (ref obj) and the `is` operand would be the ref form.
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::IsInst isInst(std::const_pointer_cast<TS::IType>(
                          fixture.compilation.FindType(TS::KnownTypeCode::String)
                              .shared_from_this()),
                      std::make_unique<IL::LdLoc>(variable));
    auto* stmt = builder.Convert(&isInst);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    auto* isExpr = dynamic_cast<Syntax::IsExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(isExpr != nullptr);
    // The operand is the boxing-unwrapped argument (the local's identifier).
    auto* operand = dynamic_cast<Syntax::IdentifierExpression*>(isExpr->Expression());
    ASSERT_TRUE(operand != nullptr);
    EXPECT_EQ(operand->Identifier(), "obj");
    // The resolve result is the boolean ResolveResult the C# `WithRR` attaches.
    const auto* rr = isExpr->Annotation<Sem::ResolveResult>();
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Type().ReflectionName(), "System.Boolean");
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &isInst);
}

// The if/else statement over the translated condition: a Nop false arm is the
// no-else shape, a real false arm converts recursively.
TEST(StatementBuilderTest, VisitIfInstructionRendersIfElse)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::StLoc stLoc(variable, std::make_unique<IL::LdcI4>(42));
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1),
                             std::make_unique<IL::Nop>(),
                             std::make_unique<IL::StLoc>(variable,
                                                          std::make_unique<IL::LdcI4>(1)));
    auto* stmt = builder.Convert(&ifInst);
    auto* ifElse = dynamic_cast<Syntax::IfElseStatement*>(stmt);
    ASSERT_TRUE(ifElse != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(ifElse->Condition()) != nullptr);
    // The Nop true arm renders the empty statement.
    EXPECT_TRUE(dynamic_cast<Syntax::EmptyStatement*>(ifElse->TrueStatement()) != nullptr);
    // The false arm converts recursively into the assignment expression statement.
    auto* falseStatement = dynamic_cast<Syntax::ExpressionStatement*>(ifElse->FalseStatement());
    ASSERT_TRUE(falseStatement != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::AssignmentExpression*>(falseStatement->Expression())
                != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &ifInst);
    (void)stLoc;
}

// A false arm that is a Nop is the C#'s no-else shape (`inst.FalseInst.OpCode ==
// OpCode.Nop ? null : Convert(inst.FalseInst)`).
TEST(StatementBuilderTest, VisitIfInstructionNopFalseArmIsNoElse)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1),
                             std::make_unique<IL::StLoc>(
                                 fixture.MakeLocal(TS::KnownTypeCode::Int32, "num"),
                                 std::make_unique<IL::LdcI4>(1)),
                             std::make_unique<IL::Nop>());
    auto* stmt = builder.Convert(&ifInst);
    auto* ifElse = dynamic_cast<Syntax::IfElseStatement*>(stmt);
    ASSERT_TRUE(ifElse != nullptr);
    EXPECT_TRUE(ifElse->FalseStatement() == nullptr);
}

} // namespace ILSpy::Tests
