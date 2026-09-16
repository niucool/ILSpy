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
#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"  // hmm - check the actual path
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;

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

// ---------------------------------------------------------------------------
// The branch/leave/goto state (StatementBuilder.cs lines 338-373 + 1576-1597)
// ---------------------------------------------------------------------------

// A Branch to a non-continue, non-case block renders `goto IL_xxxx` over the
// target block's label, with the IL annotation on the statement.
TEST(StatementBuilderTest, VisitBranchRendersGotoStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block targetBlock;
    targetBlock.StartILOffset = 0x1234;
    IL::Branch branch(&targetBlock);
    auto* stmt = builder.Convert(&branch);
    auto* gotoStmt = dynamic_cast<Syntax::GotoStatement*>(stmt);
    ASSERT_TRUE(gotoStmt != nullptr);
    ASSERT_TRUE(gotoStmt->Label().has_value());
    EXPECT_EQ(*gotoStmt->Label(), "IL_1234");
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &branch);
}

// A branch whose target is the continue target renders `continue;` and counts.
TEST(StatementBuilderTest, VisitBranchRendersContinueStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block continueBlock;
    continueBlock.StartILOffset = 0x0008;
    builder.continueTarget = &continueBlock;
    IL::Branch branch(&continueBlock);
    auto* stmt = builder.Convert(&branch);
    EXPECT_TRUE(dynamic_cast<Syntax::ContinueStatement*>(stmt) != nullptr);
    // The second visit increments the count the VisitBlockContainer slice reads.
    builder.Convert(&branch);
    EXPECT_EQ(builder.continueCount, 2);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &branch);
}

// A mapped block renders `goto case <value>` / `goto default;` -- the C# nullable
// value distinguishes the two shapes.
TEST(StatementBuilderTest, VisitBranchCaseMappingArms)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block caseBlock;
    caseBlock.StartILOffset = 0x0010;
    IL::Block defaultBlock;
    defaultBlock.StartILOffset = 0x0020;
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    CSharp::StatementBuilder::CaseLabelMapping mapping;
    mapping.emplace(&caseBlock, std::make_shared<Sem::ConstantResolveResult>(intType,
                                                                            std::int32_t(42)));
    mapping.emplace(&defaultBlock, nullptr);
    builder.caseLabelMapping = std::move(mapping);

    IL::Branch caseBranch(&caseBlock);
    auto* caseStmt = builder.Convert(&caseBranch);
    auto* gotoCase = dynamic_cast<Syntax::GotoCaseStatement*>(caseStmt);
    ASSERT_TRUE(gotoCase != nullptr);
    auto* caseLabel = dynamic_cast<Syntax::PrimitiveExpression*>(gotoCase->LabelExpression());
    ASSERT_TRUE(caseLabel != nullptr);
    const std::int32_t* caseValue = std::get_if<std::int32_t>(&caseLabel->Value());
    ASSERT_TRUE(caseValue != nullptr);
    EXPECT_EQ(*caseValue, 42);

    IL::Branch defaultBranch(&defaultBlock);
    auto* defaultStmt = builder.Convert(&defaultBranch);
    EXPECT_TRUE(dynamic_cast<Syntax::GotoDefaultStatement*>(defaultStmt) != nullptr);
}

// A Leave whose target is the break target renders `break;`.
TEST(StatementBuilderTest, VisitLeaveRendersBreakStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    builder.breakTarget = fixture.function.Body.get();
    IL::Leave leave(fixture.function.Body.get());
    auto* stmt = builder.Convert(&leave);
    auto* breakStmt = dynamic_cast<Syntax::BreakStatement*>(stmt);
    ASSERT_TRUE(breakStmt != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &leave);
}

// An iterator's leave of the return container renders `yield break;`.
TEST(StatementBuilderTest, VisitLeaveRendersYieldBreakStatement)
{
    StatementFixture fixture;
    fixture.function.IsIterator = true;
    auto builder = fixture.MakeBuilder();
    ASSERT_TRUE(builder.currentIsIterator);
    IL::Leave leave(fixture.function.Body.get());
    auto* stmt = builder.Convert(&leave);
    EXPECT_TRUE(dynamic_cast<Syntax::YieldBreakStatement*>(stmt) != nullptr);
}

// A value-less leave of the return container renders a bare `return;`.
TEST(StatementBuilderTest, VisitLeaveRendersBareReturnStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A null Value is the port's shape of the C#'s Nop value (the value-less leave).
    IL::Leave leave(fixture.function.Body.get());
    auto* stmt = builder.Convert(&leave);
    auto* returnStmt = dynamic_cast<Syntax::ReturnStatement*>(stmt);
    ASSERT_TRUE(returnStmt != nullptr);
    EXPECT_TRUE(returnStmt->Expression() == nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &leave);
}

// A value-returning leave renders `return <value>;` -- the identity conversion adds
// no cast, and the statement carries the leave annotation.
TEST(StatementBuilderTest, VisitLeaveRendersValueReturnStatement)
{
    StatementFixture fixture;
    fixture.function.ReturnType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto builder = fixture.MakeBuilder();
    ASSERT_TRUE(builder.currentResultType != nullptr);
    IL::Leave leave(fixture.function.Body.get(), std::make_unique<IL::LdcI4>(42));
    auto* stmt = builder.Convert(&leave);
    auto* returnStmt = dynamic_cast<Syntax::ReturnStatement*>(stmt);
    ASSERT_TRUE(returnStmt != nullptr);
    // The identity conversion inserts no cast.
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(returnStmt->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &leave);
}

// A delegate/lambda leave of a null literal into a reference-typed return gains the
// possible-loss cast: the null literal keeps its NullType, which the C# counts as
// possible loss of type information (`givenType == SpecialType.NullType`).
TEST(StatementBuilderTest, VisitLeaveAddsCastForLambdaLossOfTypeInformation)
{
    StatementFixture fixture;
    fixture.function.Kind = IL::ILFunctionKind::Delegate;
    fixture.function.ReturnType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto builder = fixture.MakeBuilder();
    IL::Leave leave(fixture.function.Body.get(), std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&leave);
    auto* returnStmt = dynamic_cast<Syntax::ReturnStatement*>(stmt);
    ASSERT_TRUE(returnStmt != nullptr);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(returnStmt->Expression());
    ASSERT_TRUE(cast != nullptr);
    // The cast's resolve result is the identity ConversionResolveResult over the
    // function's return type.
    const auto* rr = cast->Annotation<Sem::ConversionResolveResult>();
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Type().ReflectionName(), "System.Object");
    ASSERT_TRUE(rr->ConversionProperty() != nullptr);
    EXPECT_TRUE(rr->ConversionProperty()->IsIdentityConversion());
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &leave);
}

// A top-level function's identical shape does NOT gain the cast (the
// lambda/expr-tree gate is false, so only the EquivalentTypes path runs).
TEST(StatementBuilderTest, VisitLeaveTopLevelFunctionSkipsTheLossCast)
{
    StatementFixture fixture;
    fixture.function.ReturnType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto builder = fixture.MakeBuilder();
    EXPECT_EQ(builder.currentFunction->Kind, IL::ILFunctionKind::TopLevelFunction);
    IL::Leave leave(fixture.function.Body.get(), std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&leave);
    auto* returnStmt = dynamic_cast<Syntax::ReturnStatement*>(stmt);
    ASSERT_TRUE(returnStmt != nullptr);
    auto* nullLiteral = dynamic_cast<Syntax::NullReferenceExpression*>(returnStmt->Expression());
    ASSERT_TRUE(nullLiteral != nullptr);
}

// A leave of a foreign container renders `goto end_<label>`, and the second leave
// of the same container reuses the stored label.
TEST(StatementBuilderTest, VisitLeaveGotoEndContainerReusesTheLabel)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::BlockContainer otherContainer;
    auto entryBlock = std::make_unique<IL::Block>();
    entryBlock->StartILOffset = 0x1234;
    otherContainer.AddBlock(std::move(entryBlock));
    IL::Leave firstLeave(&otherContainer);
    auto* firstStmt = builder.Convert(&firstLeave);
    auto* firstGoto = dynamic_cast<Syntax::GotoStatement*>(firstStmt);
    ASSERT_TRUE(firstGoto != nullptr);
    ASSERT_TRUE(firstGoto->Label().has_value());
    EXPECT_EQ(*firstGoto->Label(), "end_IL_1234");
    IL::Leave secondLeave(&otherContainer);
    auto* secondStmt = builder.Convert(&secondLeave);
    auto* secondGoto = dynamic_cast<Syntax::GotoStatement*>(secondStmt);
    ASSERT_TRUE(secondGoto != nullptr);
    ASSERT_TRUE(secondGoto->Label().has_value());
    EXPECT_EQ(*secondGoto->Label(), "end_IL_1234");
}

// Two different containers whose entry blocks share the same offset get distinct
// end labels -- the C# suffixes the duplicate name through the shared
// duplicateLabels count (`end_<label>_<occurrence + 1>`).
TEST(StatementBuilderTest, VisitLeaveSuffixesDuplicateEndLabels)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::BlockContainer firstContainer;
    auto firstBlock = std::make_unique<IL::Block>();
    firstBlock->StartILOffset = 0x0042;
    firstContainer.AddBlock(std::move(firstBlock));
    IL::BlockContainer secondContainer;
    auto secondBlock = std::make_unique<IL::Block>();
    secondBlock->StartILOffset = 0x0042;
    secondContainer.AddBlock(std::move(secondBlock));
    IL::Leave firstLeave(&firstContainer);
    auto* firstStmt = builder.Convert(&firstLeave);
    auto* firstGoto = dynamic_cast<Syntax::GotoStatement*>(firstStmt);
    ASSERT_TRUE(firstGoto != nullptr);
    ASSERT_TRUE(firstGoto->Label().has_value());
    EXPECT_EQ(*firstGoto->Label(), "end_IL_0042");
    IL::Leave secondLeave(&secondContainer);
    auto* secondStmt = builder.Convert(&secondLeave);
    auto* secondGoto = dynamic_cast<Syntax::GotoStatement*>(secondStmt);
    ASSERT_TRUE(secondGoto != nullptr);
    ASSERT_TRUE(secondGoto->Label().has_value());
    EXPECT_EQ(*secondGoto->Label(), "end_IL_0042_2");
}

// EnsureUniqueLabel deduplicates the block label the same way (the `_N` suffix over
// the shared duplicateLabels count).
TEST(StatementBuilderTest, EnsureUniqueLabelSuffixesDuplicates)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Block first;
    first.StartILOffset = 0x0042;
    IL::Block second;
    second.StartILOffset = 0x0042;
    EXPECT_EQ(builder.EnsureUniqueLabel(&first), "IL_0042");
    EXPECT_EQ(builder.EnsureUniqueLabel(&second), "IL_0042_2");
    // The first block's label is memoized.
    EXPECT_EQ(builder.EnsureUniqueLabel(&first), "IL_0042");
}

// `throw <expr>;` renders the throw statement over the translated argument.
TEST(StatementBuilderTest, VisitThrowRendersThrowStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Object, "obj");
    IL::Throw throwInst(std::make_unique<IL::LdLoc>(variable));
    auto* stmt = builder.Convert(&throwInst);
    auto* throwStmt = dynamic_cast<Syntax::ThrowStatement*>(stmt);
    ASSERT_TRUE(throwStmt != nullptr);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(throwStmt->Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(identifier->Identifier(), "obj");
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &throwInst);
}

// A rethrow renders a bare `throw;` (no expression).
TEST(StatementBuilderTest, VisitRethrowRendersBareThrowStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Rethrow rethrow;
    auto* stmt = builder.Convert(&rethrow);
    auto* throwStmt = dynamic_cast<Syntax::ThrowStatement*>(stmt);
    ASSERT_TRUE(throwStmt != nullptr);
    EXPECT_TRUE(throwStmt->Expression() == nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &rethrow);
}

// `yield return <value>;` over an async iterator uses the async return type as the
// element type.
TEST(StatementBuilderTest, VisitYieldReturnUsesTheAsyncReturnType)
{
    StatementFixture fixture;
    fixture.function.AsyncReturnType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto builder = fixture.MakeBuilder();
    IL::YieldReturn yieldReturn(std::make_unique<IL::LdcI4>(42));
    auto* stmt = builder.Convert(&yieldReturn);
    auto* yieldStmt = dynamic_cast<Syntax::YieldReturnStatement*>(stmt);
    ASSERT_TRUE(yieldStmt != nullptr);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(yieldStmt->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &yieldReturn);
}

// A non-async iterator's element type is the IEnumerable unwrap of the function's
// return type (`ReturnType.GetElementTypeFromIEnumerable(typeSystem, true, out _)`).
TEST(StatementBuilderTest, VisitYieldReturnUnwrapsTheIEnumerable)
{
    StatementFixture fixture;
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto enumerableType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::IEnumerableOfT).shared_from_this());
    fixture.function.ReturnType = std::make_shared<TS::ParameterizedType>(
        enumerableType, std::vector<TS::ITypePtr>{intType});
    auto builder = fixture.MakeBuilder();
    IL::YieldReturn yieldReturn(std::make_unique<IL::LdcI4>(7));
    auto* stmt = builder.Convert(&yieldReturn);
    auto* yieldStmt = dynamic_cast<Syntax::YieldReturnStatement*>(stmt);
    ASSERT_TRUE(yieldStmt != nullptr);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(yieldStmt->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 7);
}

// ---------------------------------------------------------------------------
// The IL-node surface the arms read
// ---------------------------------------------------------------------------

// Block.Label() is the DisassemblerHelpers.OffsetToString of the start offset
// (the C# Block.Label property), EntryPoint() is the first block, and
// Leave.TargetLabel() chains them (the empty string for a container-less leave).
TEST(StatementBuilderTest, BlockLabelAndLeaveTargetLabelAccessors)
{
    IL::Block block;
    block.StartILOffset = 0x1234;
    EXPECT_EQ(block.Label(), "IL_1234");
    IL::BlockContainer container;
    container.AddBlock(std::make_unique<IL::Block>());
    container.Blocks.front()->StartILOffset = 0x1234;
    EXPECT_EQ(container.EntryPoint(), container.Blocks.front().get());
    IL::Leave leave(&container);
    EXPECT_EQ(leave.TargetLabel(), "IL_1234");
    IL::Leave containerless;
    EXPECT_EQ(containerless.TargetLabel(), "");
}

// The YieldReturn node: the value child, the dump render, and the clone.
TEST(StatementBuilderTest, YieldReturnNodeShapeAndClone)
{
    IL::YieldReturn yieldReturn(std::make_unique<IL::LdcI4>(5));
    EXPECT_EQ(yieldReturn.Op, IL::OpCode::YieldReturn);
    EXPECT_EQ(yieldReturn.ChildCount(), 1);
    EXPECT_EQ(yieldReturn.ResultType(), IL::StackType::Void);
    std::string dump;
    yieldReturn.WriteTo(dump);
    EXPECT_NE(dump.find("yield.return"), std::string::npos);
    EXPECT_NE(dump.find("ldc.i4(5)"), std::string::npos);
    auto clone = yieldReturn.Clone();
    auto* cloneTyped = dynamic_cast<IL::YieldReturn*>(clone.get());
    ASSERT_TRUE(cloneTyped != nullptr);
    ASSERT_TRUE(cloneTyped->Value != nullptr);
    EXPECT_EQ(cloneTyped->Value->Op, IL::OpCode::LdcI4);
    EXPECT_EQ(cloneTyped->Value->Parent, cloneTyped);
}

// The ILFunctionKind field clones (the VisitLeave lambda/expr-tree gate reads it).
TEST(StatementBuilderTest, ILFunctionKindClones)
{
    IL::ILFunction function;
    function.Kind = IL::ILFunctionKind::Delegate;
    auto clone = function.Clone();
    auto* cloneTyped = dynamic_cast<IL::ILFunction*>(clone.get());
    ASSERT_TRUE(cloneTyped != nullptr);
    EXPECT_EQ(cloneTyped->Kind, IL::ILFunctionKind::Delegate);
    IL::ILFunction topLevel;
    auto topLevelClone = topLevel.Clone();
    auto* topLevelCloneTyped = dynamic_cast<IL::ILFunction*>(topLevelClone.get());
    ASSERT_TRUE(topLevelCloneTyped != nullptr);
    EXPECT_EQ(topLevelCloneTyped->Kind, IL::ILFunctionKind::TopLevelFunction);
}

// ---------------------------------------------------------------------------
// The small leaf statement arms: initblk/cpblk/ckfinite
// ---------------------------------------------------------------------------

// initblk renders the Unsafe.InitBlock intrinsic over the (address, value, size)
// translations with the `// IL initblk instruction` leading-trivia comment.
TEST(StatementBuilderTest, VisitInitblkRendersUnsafeInitBlock)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto pointer = fixture.MakeLocal(TS::KnownTypeCode::IntPtr, "ptr");
    IL::Initblk initblk(std::make_unique<IL::LdLoca>(pointer),
                        std::make_unique<IL::LdcI4>(0), std::make_unique<IL::LdcI4>(8));
    auto* stmt = builder.Convert(&initblk);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "InitBlock");
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(memberRef->Target()) != nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), std::size_t(3));
    // The leading trivia carries the IL comment.
    const auto trivia = stmt->LeadingTrivia();
    ASSERT_EQ(trivia.size(), std::size_t(1));
    auto* comment = dynamic_cast<Syntax::Comment*>(trivia[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), " IL initblk instruction");
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &initblk);
}

// cpblk renders the Unsafe.CopyBlock intrinsic with the `// IL cpblk instruction`
// comment.
TEST(StatementBuilderTest, VisitCpblkRendersUnsafeCopyBlock)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto dest = fixture.MakeLocal(TS::KnownTypeCode::IntPtr, "dest");
    auto source = fixture.MakeLocal(TS::KnownTypeCode::IntPtr, "src");
    IL::Cpblk cpblk(std::make_unique<IL::LdLoca>(dest),
                    std::make_unique<IL::LdLoca>(source), std::make_unique<IL::LdcI4>(16));
    auto* stmt = builder.Convert(&cpblk);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "CopyBlock");
    ASSERT_EQ(invocation->Arguments().Count(), std::size_t(3));
    const auto trivia = stmt->LeadingTrivia();
    ASSERT_EQ(trivia.size(), std::size_t(1));
    auto* comment = dynamic_cast<Syntax::Comment*>(trivia[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), " IL cpblk instruction");
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &cpblk);
}

// A nonzero UnalignedPrefix selects the *Unaligned intrinsic (and renders the
// `unaligned(<n>).` prefix in the node's own dump).
TEST(StatementBuilderTest, UnalignedPrefixSelectsUnalignedIntrinsic)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto pointer = fixture.MakeLocal(TS::KnownTypeCode::IntPtr, "ptr");
    IL::Initblk initblk(std::make_unique<IL::LdLoca>(pointer),
                        std::make_unique<IL::LdcI4>(0), std::make_unique<IL::LdcI4>(4));
    initblk.UnalignedPrefix = 1;
    auto* stmt = builder.Convert(&initblk);
    auto* expressionStatement = dynamic_cast<Syntax::ExpressionStatement*>(stmt);
    ASSERT_TRUE(expressionStatement != nullptr);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expressionStatement->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "InitBlockUnaligned");
    // The node's own dump carries the prefix before the opcode.
    std::string dump;
    initblk.WriteTo(dump);
    EXPECT_NE(dump.find("unaligned(1).initblk("), std::string::npos);
}

// ckfinite renders the `if (!float.IsFinite(<arg>)) throw new
// ArithmeticException();` guard; the exception type annotation is the
// type-system FindType result.
TEST(StatementBuilderTest, VisitCkfiniteRendersFloatIsFiniteGuard)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto value = fixture.MakeLocal(TS::KnownTypeCode::Single, "f");
    IL::Ckfinite ckfinite(std::make_unique<IL::LdLoc>(value));
    auto* stmt = builder.Convert(&ckfinite);
    auto* ifElse = dynamic_cast<Syntax::IfElseStatement*>(stmt);
    ASSERT_TRUE(ifElse != nullptr);
    // The condition is `!float.IsFinite(arg)`.
    auto* notExpr = dynamic_cast<Syntax::UnaryOperatorExpression*>(ifElse->Condition());
    ASSERT_TRUE(notExpr != nullptr);
    EXPECT_EQ(notExpr->Operator(), Syntax::UnaryOperatorType::Not);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(notExpr->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "IsFinite");
    auto* typeRef = dynamic_cast<Syntax::TypeReferenceExpression*>(memberRef->Target());
    ASSERT_TRUE(typeRef != nullptr);
    auto* floatType = dynamic_cast<Syntax::PrimitiveType*>(typeRef->Type());
    ASSERT_TRUE(floatType != nullptr);
    EXPECT_EQ(floatType->Keyword(), "float");
    ASSERT_EQ(invocation->Arguments().Count(), std::size_t(1));
    auto* operand = dynamic_cast<Syntax::IdentifierExpression*>(invocation->Arguments().FirstOrNull());
    ASSERT_TRUE(operand != nullptr);
    EXPECT_EQ(operand->Identifier(), "f");
    // The true arm throws a fresh ArithmeticException; the false arm is absent.
    auto* throwStmt = dynamic_cast<Syntax::ThrowStatement*>(ifElse->TrueStatement());
    ASSERT_TRUE(throwStmt != nullptr);
    auto* create = dynamic_cast<Syntax::ObjectCreateExpression*>(throwStmt->Expression());
    ASSERT_TRUE(create != nullptr);
    auto* simpleType = dynamic_cast<Syntax::SimpleType*>(create->Type());
    ASSERT_TRUE(simpleType != nullptr);
    EXPECT_EQ(simpleType->Identifier(), "ArithmeticException");
    // The type node carries the TypeResolveResult annotation (the type-system
    // FindType result -- an UnknownType over the minimal corlib, which carries
    // the full name).
    const auto* typeRR = simpleType->Annotation<Sem::TypeResolveResult>();
    ASSERT_TRUE(typeRR != nullptr);
    EXPECT_EQ(typeRR->Type().ReflectionName(), "System.ArithmeticException");
    EXPECT_TRUE(ifElse->FalseStatement() == nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &ckfinite);
}

// The node shapes: ckfinite is a Void-result unary instruction, and the block
// memory instructions are Void-result 3-child instructions with
// MayThrow|SideEffect direct flags.
TEST(StatementBuilderTest, BlockMemoryInstructionNodeShapes)
{
    IL::Ckfinite ckfinite(std::make_unique<IL::LdcI4>(1));
    EXPECT_EQ(ckfinite.Op, IL::OpCode::Ckfinite);
    EXPECT_EQ(ckfinite.ChildCount(), 1);
    EXPECT_EQ(ckfinite.ResultType(), IL::StackType::Void);
    EXPECT_EQ(ckfinite.DirectFlags(), IL::InstructionFlags::MayThrow);
    std::string dump;
    ckfinite.WriteTo(dump);
    EXPECT_NE(dump.find("ckfinite(ldc.i4(1)"), std::string::npos) << dump;

    IL::Initblk initblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdcI4>(0),
                        std::make_unique<IL::LdcI4>(8));
    EXPECT_EQ(initblk.Op, IL::OpCode::Initblk);
    EXPECT_EQ(initblk.ChildCount(), 3);
    EXPECT_EQ(initblk.ResultType(), IL::StackType::Void);
    EXPECT_EQ(initblk.DirectFlags(), IL::InstructionFlags::MayThrow | IL::InstructionFlags::SideEffect);
    dump.clear();
    initblk.WriteTo(dump);
    EXPECT_NE(dump.find("initblk("), std::string::npos) << dump;
    EXPECT_EQ(dump.find("volatile."), std::string::npos) << dump;
    EXPECT_EQ(dump.find("unaligned"), std::string::npos) << dump;

    IL::Cpblk cpblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdNull>(),
                    std::make_unique<IL::LdcI4>(4));
    EXPECT_EQ(cpblk.Op, IL::OpCode::Cpblk);
    EXPECT_EQ(cpblk.ChildCount(), 3);
    EXPECT_EQ(cpblk.ResultType(), IL::StackType::Void);
    EXPECT_EQ(cpblk.DirectFlags(), IL::InstructionFlags::MayThrow | IL::InstructionFlags::SideEffect);
    dump.clear();
    cpblk.WriteTo(dump);
    EXPECT_NE(dump.find("cpblk("), std::string::npos) << dump;
}

// The volatile./unaligned(<n>). prefixes render before the opcode, matching the
// C# WriteToCore shape.
TEST(StatementBuilderTest, BlockMemoryInstructionsRenderPrefixes)
{
    IL::Initblk initblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdcI4>(0),
                        std::make_unique<IL::LdcI4>(8));
    initblk.IsVolatile = true;
    initblk.UnalignedPrefix = 2;
    std::string dump;
    initblk.WriteTo(dump);
    EXPECT_NE(dump.find("volatile.unaligned(2).initblk("), std::string::npos) << dump;
    // The child render follows in slot order: address, value, size.
    EXPECT_NE(dump.find("ldnull, ldc.i4(0), ldc.i4(8)"), std::string::npos) << dump;

    IL::Cpblk cpblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdNull>(),
                    std::make_unique<IL::LdcI4>(4));
    cpblk.UnalignedPrefix = 1;
    dump.clear();
    cpblk.WriteTo(dump);
    EXPECT_NE(dump.find("unaligned(1).cpblk("), std::string::npos) << dump;
}

// The clone cases carry every scalar field.
TEST(StatementBuilderTest, BlockMemoryInstructionsClone)
{
    IL::Ckfinite ckfinite(std::make_unique<IL::LdcI4>(1));
    auto ckClone = ckfinite.Clone();
    auto* ckCloneTyped = dynamic_cast<IL::Ckfinite*>(ckClone.get());
    ASSERT_TRUE(ckCloneTyped != nullptr);
    ASSERT_TRUE(ckCloneTyped->Argument != nullptr);
    EXPECT_EQ(ckCloneTyped->Argument->Op, IL::OpCode::LdcI4);
    EXPECT_EQ(ckCloneTyped->Argument->Parent, ckCloneTyped);

    IL::Initblk initblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdcI4>(0),
                        std::make_unique<IL::LdcI4>(8));
    initblk.UnalignedPrefix = 3;
    initblk.IsVolatile = true;
    auto initClone = initblk.Clone();
    auto* initCloneTyped = dynamic_cast<IL::Initblk*>(initClone.get());
    ASSERT_TRUE(initCloneTyped != nullptr);
    EXPECT_EQ(initCloneTyped->UnalignedPrefix, 3);
    EXPECT_EQ(initCloneTyped->IsVolatile, true);
    ASSERT_TRUE(initCloneTyped->Address != nullptr);
    ASSERT_TRUE(initCloneTyped->Value != nullptr);
    ASSERT_TRUE(initCloneTyped->Size != nullptr);
    EXPECT_EQ(initCloneTyped->Address->Parent, initClone.get());
    EXPECT_EQ(initCloneTyped->Value->Op, IL::OpCode::LdcI4);
    EXPECT_EQ(initCloneTyped->Size->Parent, initClone.get());

    IL::Cpblk cpblk(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdNull>(),
                    std::make_unique<IL::LdcI4>(4));
    cpblk.UnalignedPrefix = 1;
    cpblk.IsVolatile = true;
    auto cpClone = cpblk.Clone();
    auto* cpCloneTyped = dynamic_cast<IL::Cpblk*>(cpClone.get());
    ASSERT_TRUE(cpCloneTyped != nullptr);
    EXPECT_EQ(cpCloneTyped->UnalignedPrefix, 1);
    EXPECT_EQ(cpCloneTyped->IsVolatile, true);
    ASSERT_TRUE(cpCloneTyped->SourceAddress != nullptr);
    EXPECT_EQ(cpCloneTyped->SourceAddress->Parent, cpClone.get());
}

// ---------------------------------------------------------------------------
// The try-construction region (StatementBuilder.cs lines 445-532)
// ---------------------------------------------------------------------------

// VisitTryCatch renders the TryCatchStatement with the converted try block and
// one CatchClause per handler; a bare catch with no variable carries no name,
// no type, and no condition (the filter is the ldc.i4 1 constant).
TEST(StatementBuilderTest, VisitTryCatchRendersTryCatchStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::TryCatch tryCatchInst(std::make_unique<IL::LdNull>());
    tryCatchInst.AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdNull>(), nullptr));
    auto* stmt = builder.Convert(&tryCatchInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    // The try block converts to a BlockStatement (the LdNull statement wrapped).
    ASSERT_TRUE(tryCatch->TryBlock() != nullptr);
    ASSERT_EQ(tryCatch->TryBlock()->Statements().Count(), std::size_t(1));
    EXPECT_TRUE(tryCatch->FinallyBlock() == nullptr);
    // One catch clause with the handler's body; no variable, no filter condition.
    ASSERT_EQ(tryCatch->CatchClauses().Count(), std::size_t(1));
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    ASSERT_TRUE(catchClause->Body() != nullptr);
    ASSERT_EQ(catchClause->Body()->Statements().Count(), std::size_t(1));
    EXPECT_FALSE(catchClause->VariableName().has_value());
    EXPECT_TRUE(catchClause->Type() == nullptr);
    EXPECT_TRUE(catchClause->Condition() == nullptr);
    // The IL annotations: the try statement carries the TryCatch instruction;
    // the catch clause carries its handler.
    const auto tryInstructions = StatementILInstructions(*stmt);
    ASSERT_EQ(tryInstructions.size(), std::size_t(1));
    EXPECT_EQ(tryInstructions[0], &tryCatchInst);
    const auto handlerInstructions = CSharp::GetILInstructions(*catchClause);
    ASSERT_EQ(handlerInstructions.size(), std::size_t(1));
    EXPECT_EQ(handlerInstructions[0], tryCatchInst.Handlers[0].get());
}

// A variable with a store besides its use is named and its caught type is
// rendered (`catch (int ex)`); the resolve result rides the clause.
TEST(StatementBuilderTest, VisitTryCatchNamesAndTypesTheCatchVariable)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "ex");
    variable->StoreCount = 2;
    IL::TryCatch tryCatchInst(std::make_unique<IL::LdNull>());
    tryCatchInst.AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdNull>(), variable));
    auto* stmt = builder.Convert(&tryCatchInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    ASSERT_TRUE(catchClause->VariableName().has_value());
    EXPECT_EQ(*catchClause->VariableName(), "ex");
    auto* primitiveType = dynamic_cast<Syntax::PrimitiveType*>(catchClause->Type());
    ASSERT_TRUE(primitiveType != nullptr);
    EXPECT_EQ(primitiveType->Keyword(), "int");
    EXPECT_TRUE(catchClause->Annotation<CSharp::ILVariableResolveResult>() != nullptr);
}

// A variable that is only stored once and typed `object` renders neither name
// nor type (`catch` over `catch (object)`); the resolve result still rides the
// clause (the C# annotates it before the name/type gates).
TEST(StatementBuilderTest, VisitTryCatchOmitsObjectTypedVariable)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Object, "unused");
    variable->StoreCount = 1;
    IL::TryCatch tryCatchInst(std::make_unique<IL::LdNull>());
    tryCatchInst.AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdNull>(), variable));
    auto* stmt = builder.Convert(&tryCatchInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    EXPECT_FALSE(catchClause->VariableName().has_value());
    EXPECT_TRUE(catchClause->Type() == nullptr);
    EXPECT_TRUE(catchClause->Annotation<CSharp::ILVariableResolveResult>() != nullptr);
}

// A single-stored non-object variable is typed but not named
// (`catch (int)` -- the variable is never read).
TEST(StatementBuilderTest, VisitTryCatchTypesUnnamedVariable)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "unused");
    variable->StoreCount = 1;
    IL::TryCatch tryCatchInst(std::make_unique<IL::LdNull>());
    tryCatchInst.AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdNull>(), variable));
    auto* stmt = builder.Convert(&tryCatchInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    EXPECT_FALSE(catchClause->VariableName().has_value());
    EXPECT_TRUE(catchClause->Type() != nullptr);
}

// A filter that is not the ldc.i4 1 constant translates to the `when`
// condition (an I4-typed local render); the ldc.i4 1 filter is the bare catch.
TEST(StatementBuilderTest, VisitTryCatchTranslatesTheWhenFilter)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto flag = fixture.MakeLocal(TS::KnownTypeCode::Boolean, "flag");
    IL::TryCatch tryCatchInst(std::make_unique<IL::LdNull>());
    tryCatchInst.AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdLoc>(flag), std::make_unique<IL::LdNull>(), nullptr));
    auto* stmt = builder.Convert(&tryCatchInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(catchClause->Condition());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(CSharp::GetILVariable(*identifier), flag.get());
}

// VisitTryFinally renders the finally block over the wrapped try block
// (MakeTryCatch wraps a non-block converted statement in a fresh block).
TEST(StatementBuilderTest, VisitTryFinallyRendersFinallyBlock)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::TryFinally tryFinallyInst(std::make_unique<IL::LdNull>(),
                                  std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&tryFinallyInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    ASSERT_TRUE(tryCatch->TryBlock() != nullptr);
    ASSERT_EQ(tryCatch->TryBlock()->Statements().Count(), std::size_t(1));
    ASSERT_TRUE(tryCatch->FinallyBlock() != nullptr);
    ASSERT_EQ(tryCatch->FinallyBlock()->Statements().Count(), std::size_t(1));
    EXPECT_EQ(tryCatch->CatchClauses().Count(), std::size_t(0));
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &tryFinallyInst);
}

// MakeTryCatch's extend-existing path: an inner try-catch (FinallyBlock null)
// is REUSED and the finally block attached to it -- the C#
// `try { try { } catch { } } finally { }` flattens to one statement.
TEST(StatementBuilderTest, VisitTryFinallyExtendsNestedTryCatch)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto inner = std::make_unique<IL::TryCatch>(std::make_unique<IL::LdNull>());
    IL::TryCatch* innerPtr = inner.get();
    inner->AddHandler(std::make_unique<IL::TryCatchHandler>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdNull>(), nullptr));
    IL::TryFinally tryFinallyInst(std::move(inner),
                                  std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&tryFinallyInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    // The reused inner try block, its own catch clause, and the finally block
    // all live on the SAME statement.
    ASSERT_TRUE(tryCatch->TryBlock() != nullptr);
    ASSERT_EQ(tryCatch->CatchClauses().Count(), std::size_t(1));
    ASSERT_TRUE(tryCatch->FinallyBlock() != nullptr);
    ASSERT_TRUE(tryCatch->FinallyBlock()->Statements().Count() == std::size_t(1));
    // The inner TryCatch instruction converts on the try block path, so the
    // reused statement carries BOTH the inner and the outer IL annotations
    // (the C# AddAnnotation does not dedupe).
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(2));
    EXPECT_EQ(instructions[0], innerPtr);
    EXPECT_EQ(instructions[1], &tryFinallyInst);
}

// VisitTryFault renders the fault block as a catch clause body carrying the
// 'try-fault' empty statement (as leading content) and a bare throw.
TEST(StatementBuilderTest, VisitTryFaultRendersTryFaultStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::TryFault tryFaultInst(std::make_unique<IL::LdNull>(),
                              std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&tryFaultInst);
    auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(stmt);
    ASSERT_TRUE(tryCatch != nullptr);
    ASSERT_TRUE(tryCatch->TryBlock() != nullptr);
    EXPECT_TRUE(tryCatch->FinallyBlock() == nullptr);
    ASSERT_EQ(tryCatch->CatchClauses().Count(), std::size_t(1));
    auto* catchClause = tryCatch->CatchClauses()[0];
    ASSERT_TRUE(catchClause != nullptr);
    auto* faultBlock = catchClause->Body();
    ASSERT_TRUE(faultBlock != nullptr);
    // Three statements: the 'try-fault' empty statement inserted at the head,
    // the converted fault body statement, and the appended bare throw.
    ASSERT_EQ(faultBlock->Statements().Count(), std::size_t(3));
    auto* emptyStatement = dynamic_cast<Syntax::EmptyStatement*>(faultBlock->Statements()[0]);
    ASSERT_TRUE(emptyStatement != nullptr);
    // The 'try-fault' comment rides the empty statement as trailing trivia.
    const auto trailing = emptyStatement->TrailingTrivia();
    ASSERT_EQ(trailing.size(), std::size_t(1));
    auto* comment = dynamic_cast<Syntax::Comment*>(trailing[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), "try-fault");
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(faultBlock->Statements()[1]) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::ThrowStatement*>(faultBlock->Statements()[2]) != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &tryFaultInst);
}

// VisitLockInstruction renders the lock statement over the translated monitor
// expression and the converted body block.
TEST(StatementBuilderTest, VisitLockInstructionRendersLockStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto monitor = fixture.MakeLocal(TS::KnownTypeCode::Object, "lockObj");
    IL::LockInstruction lockInst(std::make_unique<IL::LdLoc>(monitor),
                                 std::make_unique<IL::LdNull>());
    auto* stmt = builder.Convert(&lockInst);
    auto* lockStatement = dynamic_cast<Syntax::LockStatement*>(stmt);
    ASSERT_TRUE(lockStatement != nullptr);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(lockStatement->Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(CSharp::GetILVariable(*identifier), monitor.get());
    ASSERT_TRUE(lockStatement->EmbeddedStatement() != nullptr);
    auto* embeddedBlock = dynamic_cast<Syntax::BlockStatement*>(lockStatement->EmbeddedStatement());
    ASSERT_TRUE(embeddedBlock != nullptr);
    ASSERT_EQ(embeddedBlock->Statements().Count(), std::size_t(1));
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &lockInst);
}

// ---------------------------------------------------------------------------
// The switch region (the StringToInt node, GetDefaultSection, the computed
// Branch::TargetContainer, FindClosestSwitchContainer, CreateTypedCaseLabel,
// TranslateSwitchValue, and the TranslateSwitch / VisitSwitchInstruction arm)
// ---------------------------------------------------------------------------

// The StringToInt node: one inlineable Argument child, the I4 result type, the
// Map/ExpectedType payload, the dump render, and the clone.
TEST(StatementBuilderTest, StringToIntNodeRendersAndClones)
{
    StatementFixture fixture;
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    IL::StringToInt strToInt(
        std::make_unique<IL::LdStr>("hello"),
        std::vector<std::pair<std::optional<std::string>, int>>{
            {std::nullopt, 0}, {std::string("world"), 1}},
        stringType);
    EXPECT_EQ(strToInt.ResultType(), IL::StackType::I4);
    ASSERT_EQ(strToInt.ChildCount(), 1);
    EXPECT_EQ(strToInt.GetChild(0), strToInt.Argument.get());
    ASSERT_EQ(strToInt.Map.size(), std::size_t(2));
    EXPECT_EQ(strToInt.Map[0].first, std::nullopt);
    EXPECT_EQ(strToInt.Map[0].second, 0);
    EXPECT_EQ(strToInt.Map[1].first, std::string("world"));
    EXPECT_EQ(strToInt.Map[1].second, 1);
    EXPECT_EQ(strToInt.ExpectedType.get(), stringType.get());
    std::string dump;
    strToInt.WriteTo(dump);
    EXPECT_EQ(dump, "string.to.int System.String(ldstr \"hello\", { [null] = 0, [\"world\"] = 1 })");
    auto clone = strToInt.Clone();
    auto* cloned = dynamic_cast<IL::StringToInt*>(clone.get());
    ASSERT_TRUE(cloned != nullptr);
    ASSERT_EQ(cloned->Map.size(), std::size_t(2));
    EXPECT_EQ(cloned->Map[1].first, std::string("world"));
    EXPECT_EQ(cloned->ExpectedType.get(), stringType.get());
    ASSERT_TRUE(cloned->Argument != nullptr);
    auto* clonedLdStr = dynamic_cast<IL::LdStr*>(cloned->Argument.get());
    ASSERT_TRUE(clonedLdStr != nullptr);
    EXPECT_EQ(clonedLdStr->Value, "hello");
}

// GetDefaultSection picks the section with the most labels.
TEST(StatementBuilderTest, GetDefaultSectionPicksMostLabels)
{
    IL::SwitchInstruction sw(std::make_unique<IL::LdcI4>(0));
    auto secSmall = std::make_unique<IL::SwitchSection>(Util::LongSet(5LL));
    auto secBig = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(1, 4)}));
    IL::SwitchSection* secBigPtr = secBig.get();
    auto secMid = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(10, 11)}));
    sw.AddSection(std::move(secSmall));
    sw.AddSection(std::move(secBig));
    sw.AddSection(std::move(secMid));
    EXPECT_EQ(sw.GetDefaultSection(), secBigPtr);
}

// Branch::TargetContainer computes from the target block's parent container
// (the C# computed property, Branch.cs line 69).
TEST(StatementBuilderTest, BranchTargetContainerComputesFromParent)
{
    IL::BlockContainer container;
    auto block = std::make_unique<IL::Block>();
    IL::Block* blockPtr = block.get();
    container.AddBlock(std::move(block));
    IL::Branch branch(blockPtr);
    EXPECT_EQ(branch.TargetContainer(), &container);
    IL::Branch untargeted(0x1234u);
    EXPECT_EQ(untargeted.TargetContainer(), nullptr);
}

// FindClosestSwitchContainer walks the parent chain for the closest Switch
// container (BlockContainer.cs line 345).
TEST(StatementBuilderTest, FindClosestSwitchContainerWalksParentChain)
{
    IL::BlockContainer outer;
    outer.Kind = IL::ContainerKind::Switch;
    auto block = std::make_unique<IL::Block>();
    IL::Block* blockPtr = block.get();
    // A nested Normal container rides the block's final-instruction slot (the
    // parent chain walks block -> middle container -> block -> outer).
    auto middle = std::make_unique<IL::BlockContainer>();
    IL::BlockContainer* middlePtr = middle.get();
    auto innerBlock = std::make_unique<IL::Block>();
    IL::Block* innerBlockPtr = innerBlock.get();
    middle->AddBlock(std::move(innerBlock));
    middle->Parent = blockPtr;
    block->FinalInstruction = std::move(middle);
    outer.AddBlock(std::move(block));
    EXPECT_EQ(IL::BlockContainer::FindClosestSwitchContainer(innerBlockPtr), &outer);
    EXPECT_EQ(IL::BlockContainer::FindClosestSwitchContainer(middlePtr), &outer);
    IL::BlockContainer plain;
    auto plainBlock = std::make_unique<IL::Block>();
    IL::Block* plainBlockPtr = plainBlock.get();
    plain.AddBlock(std::move(plainBlock));
    EXPECT_EQ(IL::BlockContainer::FindClosestSwitchContainer(plainBlockPtr), nullptr);
    EXPECT_EQ(IL::BlockContainer::FindClosestSwitchContainer(nullptr), nullptr);
}

// CreateTypedCaseLabel's boolean arm: the case value re-boxes as true/false.
TEST(StatementBuilderTest, CreateTypedCaseLabelBooleanArm)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto boolType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this());
    auto labels = builder.CreateTypedCaseLabel(1, *boolType);
    ASSERT_EQ(labels.size(), std::size_t(1));
    EXPECT_EQ(labels[0]->Type().ReflectionName(), boolType->ReflectionName());
    std::any valueBox = labels[0]->ConstantValue();
    const bool* value = std::any_cast<bool>(&valueBox);
    ASSERT_TRUE(value != nullptr);
    EXPECT_TRUE(*value);
    auto zeroLabels = builder.CreateTypedCaseLabel(0, *boolType);
    std::any zeroValueBox = zeroLabels[0]->ConstantValue();
    const bool* zeroValue = std::any_cast<bool>(&zeroValueBox);
    ASSERT_TRUE(zeroValue != nullptr);
    EXPECT_FALSE(*zeroValue);
}

// CreateTypedCaseLabel's string-map arm: one label per key mapping to i.
TEST(StatementBuilderTest, CreateTypedCaseLabelStringMapArm)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    std::vector<std::pair<std::optional<std::string>, int>> map{
        {std::string("alpha"), 0}, {std::string("beta"), 1}, {std::string("gamma"), 0}};
    auto labels = builder.CreateTypedCaseLabel(0, *stringType, &map);
    ASSERT_EQ(labels.size(), std::size_t(2));
    std::any firstBox = labels[0]->ConstantValue();
    std::any secondBox = labels[1]->ConstantValue();
    const std::string* first = std::any_cast<std::string>(&firstBox);
    const std::string* second = std::any_cast<std::string>(&secondBox);
    ASSERT_TRUE(first != nullptr);
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(*first, "alpha");
    EXPECT_EQ(*second, "gamma");
    EXPECT_EQ(labels[0]->Type().ReflectionName(), stringType->ReflectionName());
}

// CreateTypedCaseLabel's enum arm: the value re-boxes through the enum's
// underlying type code while the label keeps the enum type.
TEST(StatementBuilderTest, CreateTypedCaseLabelEnumArm)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto enumDef = std::make_shared<TestSupport::LookupTypeDefinition>(
        "E", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "E")),
        TS::TypeKind::Enum, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    enumDef->SetEnumUnderlyingType(intType);
    auto labels = builder.CreateTypedCaseLabel(5, *enumDef);
    ASSERT_EQ(labels.size(), std::size_t(1));
    EXPECT_EQ(labels[0]->Type().ReflectionName(), "Ns.E");
    std::any enumValueBox = labels[0]->ConstantValue();
    const std::int32_t* value = std::any_cast<std::int32_t>(&enumValueBox);
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 5);
}

// CreateTypedCaseLabel's plain arms: the primitive TypeCode cast, and the raw
// long fallback for a type without a TypeCode (a class type).
TEST(StatementBuilderTest, CreateTypedCaseLabelPrimitiveArms)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto int32Type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto int64Type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    auto int32Labels = builder.CreateTypedCaseLabel(42, *int32Type);
    ASSERT_EQ(int32Labels.size(), std::size_t(1));
    std::any int32ValueBox = int32Labels[0]->ConstantValue();
    const std::int32_t* castValue = std::any_cast<std::int32_t>(&int32ValueBox);
    ASSERT_TRUE(castValue != nullptr);
    EXPECT_EQ(*castValue, 42);
    // A type with no TypeCode: the raw long survives.
    auto classDef = std::make_shared<TestSupport::LookupTypeDefinition>(
        "C", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "C")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    auto classLabels = builder.CreateTypedCaseLabel(7, *classDef);
    ASSERT_EQ(classLabels.size(), std::size_t(1));
    std::any rawValueBox = classLabels[0]->ConstantValue();
    const long long* rawValue = std::any_cast<long long>(&rawValueBox);
    ASSERT_TRUE(rawValue != nullptr);
    EXPECT_EQ(*rawValue, 7);
    // The Int64 arm keeps the int64 box.
    auto int64Labels = builder.CreateTypedCaseLabel(9, *int64Type);
    std::any wideValueBox = int64Labels[0]->ConstantValue();
    const std::int64_t* wideValue = std::any_cast<std::int64_t>(&wideValueBox);
    ASSERT_TRUE(wideValue != nullptr);
    EXPECT_EQ(*wideValue, 9);
}

// TranslateSwitchValue's StringToInt arm: the governing type is the StringToInt's
// ExpectedType (the string fallback), the case type is string, and the strToInt
// rides back for the label map.
TEST(StatementBuilderTest, TranslateSwitchValueStringToIntArm)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    IL::SwitchInstruction sw(std::make_unique<IL::LdStr>("hello"));
    sw.Value = std::make_unique<IL::StringToInt>(
        std::make_unique<IL::LdStr>("hello"),
        std::vector<std::pair<std::optional<std::string>, int>>{{std::string("a"), 0}},
        stringType);
    auto result = builder.exprBuilder->TranslateSwitchValue(sw, false);
    EXPECT_EQ(result.CaseType->ReflectionName(), "System.String");
    ASSERT_TRUE(result.StringToInt != nullptr);
    EXPECT_EQ(result.StringToInt->ExpectedType.get(), stringType.get());
    // The translated value is the StringToInt's argument.
    auto* ldstr = dynamic_cast<Syntax::PrimitiveExpression*>(result.Value.Expression());
    ASSERT_TRUE(ldstr != nullptr);
    const std::string* text = std::get_if<std::string>(&ldstr->Value());
    ASSERT_TRUE(text != nullptr);
    EXPECT_EQ(*text, "hello");
}

// TranslateSwitchValue's governing-type validation: an I8 value over a non-I8
// governing type re-finds the Int64 stack type.
TEST(StatementBuilderTest, TranslateSwitchValueValidatesGoverningType)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto int32Type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto int64Type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    IL::SwitchInstruction sw(std::make_unique<IL::LdcI8>(42LL));
    // A governing type whose stack type is I4 while the value is I8.
    sw.Type = int32Type;
    auto result = builder.exprBuilder->TranslateSwitchValue(sw, false);
    EXPECT_EQ(result.CaseType->ReflectionName(), "System.Int64");
}

// TranslateSwitchValue's small-integer bail: case values outside the small
// governing type's range widen the governing type to Int32.
TEST(StatementBuilderTest, TranslateSwitchValueSmallIntegerBail)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto byteType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Byte).shared_from_this());
    IL::SwitchInstruction sw(std::make_unique<IL::LdcI4>(0));
    auto bigSection = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(0, 1000)}));
    sw.AddSection(std::move(bigSection));
    // A larger section so the out-of-range section is NOT the default (the
    // range check skips the default section -- its labels never constrain the
    // governing type).
    auto biggerSection = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(2000, 4000)}));
    sw.AddSection(std::move(biggerSection));
    sw.Type = byteType;
    auto result = builder.exprBuilder->TranslateSwitchValue(sw, false);
    EXPECT_EQ(result.CaseType->ReflectionName(), "System.Int32");
}

// VisitSwitchInstruction renders the SwitchStatement (the null-container
// shape): the governing expression, the per-section case labels, the default
// label, and the branch bodies converted through the goto-label arm.
TEST(StatementBuilderTest, VisitSwitchInstructionRendersSwitchStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::Block caseBlock;
    caseBlock.StartILOffset = 0x0010;
    IL::SwitchInstruction sw(std::make_unique<IL::LdLoc>(variable));
    auto secCase = std::make_unique<IL::SwitchSection>(Util::LongSet(1LL));
    secCase->SetBody(std::make_unique<IL::Branch>(&caseBlock));
    auto secDefault = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(2, 100)}));
    auto* defaultSection = secDefault.get();
    secDefault->SetBody(std::make_unique<IL::Branch>(&caseBlock));
    sw.AddSection(std::move(secCase));
    sw.AddSection(std::move(secDefault));
    auto* stmt = builder.Convert(&sw);
    auto* switchStatement = dynamic_cast<Syntax::SwitchStatement*>(stmt);
    ASSERT_TRUE(switchStatement != nullptr);
    // The governing expression is the translated switch value.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(switchStatement->Expression())
                != nullptr);
    ASSERT_EQ(switchStatement->SwitchSections().Count(), std::size_t(2));
    auto* first = switchStatement->SwitchSections()[0];
    ASSERT_EQ(first->CaseLabels().Count(), std::size_t(1));
    auto* caseLabel = first->CaseLabels()[0];
    ASSERT_TRUE(caseLabel->Expression() != nullptr);
    auto* caseValue = dynamic_cast<Syntax::PrimitiveExpression*>(caseLabel->Expression());
    ASSERT_TRUE(caseValue != nullptr);
    const std::int32_t* one = std::get_if<std::int32_t>(&caseValue->Value());
    ASSERT_TRUE(one != nullptr);
    EXPECT_EQ(*one, 1);
    // The default section (most labels) renders the bare `default:` label.
    auto* second = switchStatement->SwitchSections()[1];
    ASSERT_EQ(second->CaseLabels().Count(), std::size_t(1));
    EXPECT_TRUE(second->CaseLabels()[0]->Expression() == nullptr);
    // With no switch container, the section bodies convert through the
    // goto-label arm (a Branch is EndPointUnreachable, so no break is appended).
    ASSERT_EQ(second->Statements().Count(), std::size_t(1));
    EXPECT_TRUE(dynamic_cast<Syntax::GotoStatement*>(second->Statements()[0]) != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &sw);
    (void)defaultSection;
}

// TranslateSwitch with a switch container: the branch-target blocks are inlined
// into the sections, the case-label mapping drives goto case/goto default (the
// default section's block maps to null), a Leave-body section renders break
// through the break target, and unmapped blocks get trailing labels.
TEST(StatementBuilderTest, TranslateSwitchInlinesSectionsAndMapsCaseLabels)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer switchContainer;
    switchContainer.Kind = IL::ContainerKind::Switch;
    auto entryBlock = std::make_unique<IL::Block>();
    entryBlock->StartILOffset = 0x0000;
    IL::Block* entryPtr = entryBlock.get();
    switchContainer.AddBlock(std::move(entryBlock));
    auto caseBlock = std::make_unique<IL::Block>();
    caseBlock->StartILOffset = 0x0010;
    IL::Block* casePtr = caseBlock.get();
    caseBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    switchContainer.AddBlock(std::move(caseBlock));
    auto defaultBlock = std::make_unique<IL::Block>();
    defaultBlock->StartILOffset = 0x0020;
    IL::Block* defaultPtr = defaultBlock.get();
    defaultBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(2)));
    switchContainer.AddBlock(std::move(defaultBlock));
    auto leftoverBlock = std::make_unique<IL::Block>();
    leftoverBlock->StartILOffset = 0x0030;
    // The leftover block branches to the case block: converting it during the
    // remaining-blocks loop goes through VisitBranch with the live case-label
    // mapping (the producer->consumer wiring -- a goto case, not a goto label).
    leftoverBlock->Add(std::make_unique<IL::Branch>(casePtr));
    switchContainer.AddBlock(std::move(leftoverBlock));

    IL::SwitchInstruction sw(std::make_unique<IL::LdLoc>(variable));
    auto secCase = std::make_unique<IL::SwitchSection>(Util::LongSet(1LL));
    secCase->SetBody(std::make_unique<IL::Branch>(casePtr));
    // The most-labels section IS the default (the bare `default:` label, the
    // null mapping value for its branch target).
    auto secDefault = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(2, 300)}));
    secDefault->SetBody(std::make_unique<IL::Branch>(defaultPtr));
    // A Leave-body case section: the leave targets the switch container, which
    // is the break target during the translation, so the body renders break.
    auto secLeave = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(400, 401)}));
    secLeave->SetBody(std::make_unique<IL::Leave>(&switchContainer));
    sw.AddSection(std::move(secCase));
    sw.AddSection(std::move(secDefault));
    sw.AddSection(std::move(secLeave));
    entryPtr->FinalInstruction = std::make_unique<IL::Nop>();

    builder.breakTarget = nullptr;
    auto* switchStatement = builder.TranslateSwitch(&switchContainer, sw);
    ASSERT_TRUE(switchStatement != nullptr);
    // All three sections survive (the removal arm is the default-only-Leave
    // shape, a separate test below).
    ASSERT_EQ(switchStatement->SwitchSections().Count(), std::size_t(3));
    // The inlined branch-target block: the section statements hold the block's
    // converted statement -- the block converts through VisitBlock into a nested
    // BlockStatement carrying the converted stloc.
    auto* caseSection = switchStatement->SwitchSections()[0];
    ASSERT_GE(caseSection->Statements().Count(), std::size_t(1));
    auto* caseBody = dynamic_cast<Syntax::BlockStatement*>(caseSection->Statements()[0]);
    ASSERT_TRUE(caseBody != nullptr);
    ASSERT_GE(caseBody->Statements().Count(), std::size_t(1));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(caseBody->Statements()[0])
                != nullptr);
    // The Leave-body section converts its body through the break target; as the
    // LAST section it also receives the remaining-blocks trailing content (the
    // leftover block's label + its converted branch appended after the break).
    auto* leaveSection = switchStatement->SwitchSections()[2];
    ASSERT_EQ(leaveSection->Statements().Count(), std::size_t(3));
    EXPECT_TRUE(dynamic_cast<Syntax::BreakStatement*>(leaveSection->Statements()[0])
                != nullptr);
    {
        auto* label = dynamic_cast<Syntax::LabelStatement*>(leaveSection->Statements()[1]);
        ASSERT_TRUE(label != nullptr);
        EXPECT_EQ(label->Label(), "IL_0030");
    }
    {
        // The leftover block's branch to the case block renders `goto case 1;`
        // through the live mapping (the case-label mapping arm of VisitBranch).
        auto* gotoCase =
            dynamic_cast<Syntax::GotoCaseStatement*>(leaveSection->Statements()[2]);
        ASSERT_TRUE(gotoCase != nullptr);
        auto* caseLabel = dynamic_cast<Syntax::PrimitiveExpression*>(gotoCase->LabelExpression());
        ASSERT_TRUE(caseLabel != nullptr);
        const std::int32_t* caseValue = std::get_if<std::int32_t>(&caseLabel->Value());
        ASSERT_TRUE(caseValue != nullptr);
        EXPECT_EQ(*caseValue, 1);
    }
    // The case-label mapping restores to the pre-switch state after the
    // translation (the C# save/restore pair).
    EXPECT_FALSE(builder.caseLabelMapping.has_value());
    // The break target restores to the pre-switch value (null here).
    EXPECT_EQ(builder.breakTarget, nullptr);
}

// TranslateSwitch's default-only-Leave removal: the default section whose body
// is a Leave to the switch container disappears from the switch statement
// (falling through the switch).
TEST(StatementBuilderTest, TranslateSwitchRemovesDefaultOnlyLeaveSection)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer switchContainer;
    switchContainer.Kind = IL::ContainerKind::Switch;
    auto entryBlock = std::make_unique<IL::Block>();
    entryBlock->StartILOffset = 0x0000;
    switchContainer.AddBlock(std::move(entryBlock));
    auto caseBlock = std::make_unique<IL::Block>();
    caseBlock->StartILOffset = 0x0010;
    IL::Block* casePtr = caseBlock.get();
    switchContainer.AddBlock(std::move(caseBlock));

    IL::SwitchInstruction sw(std::make_unique<IL::LdLoc>(variable));
    auto secCase = std::make_unique<IL::SwitchSection>(Util::LongSet(1LL));
    secCase->SetBody(std::make_unique<IL::Branch>(casePtr));
    // The default section (most labels) leaving the switch container is removed.
    auto secLeave = std::make_unique<IL::SwitchSection>(Util::LongSet(
        std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(2, 300)}));
    secLeave->SetBody(std::make_unique<IL::Leave>(&switchContainer));
    sw.AddSection(std::move(secCase));
    sw.AddSection(std::move(secLeave));

    auto* switchStatement = builder.TranslateSwitch(&switchContainer, sw);
    ASSERT_TRUE(switchStatement != nullptr);
    ASSERT_EQ(switchStatement->SwitchSections().Count(), std::size_t(1));
    // The surviving section is the case section (its label is a case constant,
    // not the bare default).
    auto* survivor = switchStatement->SwitchSections()[0];
    ASSERT_EQ(survivor->CaseLabels().Count(), std::size_t(1));
    EXPECT_TRUE(survivor->CaseLabels()[0]->Expression() != nullptr);
}

// ---------------------------------------------------------------------------
// The block-container region (the IL pattern matchers, MatchConditionBlock /
// MatchIncrementBlock, VisitBlock, and the VisitBlockContainer / ConvertLoop /
// ConvertBlockContainer finale)
// ---------------------------------------------------------------------------

// The shared Branch/Leave/IfInstruction pattern matchers (PatternMatching.cs
// lines 171-260).
TEST(StatementBuilderTest, MatchBranchLeaveIfInstructionPatternArms)
{
    IL::BlockContainer container;
    IL::Block targetBlock;
    IL::Branch branch(&targetBlock);
    IL::Block* matchedTarget = nullptr;
    ASSERT_TRUE(IL::MatchBranch(&branch, matchedTarget));
    EXPECT_EQ(matchedTarget, &targetBlock);
    EXPECT_TRUE(IL::MatchBranch(&branch, &targetBlock));
    EXPECT_FALSE(IL::MatchBranch(&branch, nullptr));
    IL::Nop notABranch;
    EXPECT_FALSE(IL::MatchBranch(&notABranch, matchedTarget));

    IL::Leave leave(&container);
    IL::BlockContainer* matchedContainer = nullptr;
    IL::ILInstruction* leaveValue = nullptr;
    ASSERT_TRUE(IL::MatchLeave(&leave, matchedContainer, leaveValue));
    EXPECT_EQ(matchedContainer, &container);
    // The value-less leave (the port's null Value is the Nop shape) matches the
    // MatchNop-gated form.
    EXPECT_TRUE(IL::MatchLeave(&leave, &container));
    EXPECT_FALSE(IL::MatchLeave(&leave, nullptr));
    // A leave carrying a value fails the MatchNop gate.
    IL::Leave valuedLeave(&container, std::make_unique<IL::LdcI4>(1));
    EXPECT_FALSE(IL::MatchLeave(&valuedLeave, &container));
    EXPECT_FALSE(IL::MatchLeave(&notABranch, matchedContainer));

    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1),
                             std::make_unique<IL::Nop>(),
                             std::make_unique<IL::Nop>());
    IL::ILInstruction* condition = nullptr;
    IL::ILInstruction* trueInst = nullptr;
    IL::ILInstruction* falseInst = nullptr;
    ASSERT_TRUE(IL::MatchIfInstruction(&ifInst, condition, trueInst, falseInst));
    EXPECT_EQ(condition, ifInst.Condition.get());
    EXPECT_EQ(trueInst, ifInst.TrueInst.get());
    EXPECT_EQ(falseInst, ifInst.FalseInst.get());
    EXPECT_FALSE(IL::MatchIfInstruction(&branch, condition, trueInst, falseInst));
}

// MatchConditionBlock: a single-instruction block whose if's false arm leaves
// the container and whose true arm branches to the body.
TEST(StatementBuilderTest, MatchConditionBlockMatchesSingleIfShape)
{
    IL::BlockContainer container;
    IL::Block bodyBlock;
    auto condBlock = std::make_unique<IL::Block>();
    condBlock->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1),
        std::make_unique<IL::Branch>(&bodyBlock),
        std::make_unique<IL::Leave>(&container)));
    condBlock->FinalInstruction = std::make_unique<IL::Nop>();
    IL::Block* condPtr = condBlock.get();
    container.AddBlock(std::move(condBlock));
    IL::ILInstruction* condition = nullptr;
    IL::Block* bodyStart = nullptr;
    ASSERT_TRUE(container.MatchConditionBlock(condPtr, condition, bodyStart));
    EXPECT_EQ(condition, static_cast<IL::IfInstruction*>(condPtr->Instructions[0].get())
                             ->Condition.get());
    EXPECT_EQ(bodyStart, &bodyBlock);
    // A block with more than one instruction does not match.
    IL::Block twoInstrBlock;
    twoInstrBlock.Add(std::make_unique<IL::Nop>());
    twoInstrBlock.Add(std::make_unique<IL::Nop>());
    EXPECT_FALSE(container.MatchConditionBlock(&twoInstrBlock, condition, bodyStart));
}

// MatchIncrementBlock: the block's last instruction branches to the entry point.
TEST(StatementBuilderTest, MatchIncrementBlockMatchesBranchToEntryPoint)
{
    IL::BlockContainer container;
    auto entry = std::make_unique<IL::Block>();
    IL::Block* entryPtr = entry.get();
    container.AddBlock(std::move(entry));
    auto incr = std::make_unique<IL::Block>();
    incr->Add(std::make_unique<IL::StLoc>(nullptr, std::make_unique<IL::LdcI4>(1)));
    incr->Add(std::make_unique<IL::Branch>(entryPtr));
    IL::Block* incrPtr = incr.get();
    container.AddBlock(std::move(incr));
    EXPECT_TRUE(container.MatchIncrementBlock(incrPtr));
    // A block whose last instruction does not branch to the entry point fails
    // (incrPtr, not the moved-from unique_ptr).
    incrPtr->Instructions.pop_back();
    EXPECT_FALSE(container.MatchIncrementBlock(incrPtr));
    // An empty block fails.
    IL::Block empty;
    EXPECT_FALSE(container.MatchIncrementBlock(&empty));
}

// VisitBlock: the ControlFlow block converts to a BlockStatement over its
// instructions plus the non-Nop final instruction; a non-ControlFlow kind
// degrades to the Default fallback.
TEST(StatementBuilderTest, VisitBlockRendersBlockStatement)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::Block block;
    block.Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    block.Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(2)));
    block.FinalInstruction = std::make_unique<IL::Leave>(fixture.function.Body.get());
    auto* stmt = builder.Convert(&block);
    auto* blockStatement = dynamic_cast<Syntax::BlockStatement*>(stmt);
    ASSERT_TRUE(blockStatement != nullptr);
    // Two converted instructions plus the converted final leave (the bare
    // value-less return).
    ASSERT_EQ(blockStatement->Statements().Count(), std::size_t(3));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(blockStatement->Statements()[0])
                != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(blockStatement->Statements()[1])
                != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::ReturnStatement*>(blockStatement->Statements()[2])
                != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &block);

    // A non-ControlFlow block degrades to the Default fallback.
    IL::Block otherKind;
    otherKind.Kind = IL::BlockKind::InterpolatedString;
    otherKind.Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    auto* degraded = builder.Convert(&otherKind);
    EXPECT_TRUE(dynamic_cast<Syntax::BlockStatement*>(degraded) == nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(degraded) != nullptr);
}

// VisitBlockContainer's else arm: a Normal container converts through
// ConvertBlockContainer -- the entry point (single incoming edge) gets no
// label, later blocks get labels, and the container-leaving final leave is
// skipped with the ImplicitReturnAnnotation.
TEST(StatementBuilderTest, VisitBlockContainerNormalConvertsBlocks)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 1;
    entry->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    container.AddBlock(std::move(entry));
    auto tail = std::make_unique<IL::Block>();
    tail->StartILOffset = 0x0010;
    tail->IncomingEdgeCount = 1;
    IL::Block* tailPtr = tail.get();
    auto finalLeave = std::make_unique<IL::Leave>(&container);
    IL::Leave* finalLeavePtr = finalLeave.get();
    tail->Add(std::move(finalLeave));
    tail->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(tail));

    auto* stmt = builder.Convert(&container);
    auto* blockStatement = dynamic_cast<Syntax::BlockStatement*>(stmt);
    ASSERT_TRUE(blockStatement != nullptr);
    // The entry's statement, the tail's label, and NO converted leave (the final
    // leave is skipped and falls out of the block statement).
    ASSERT_EQ(blockStatement->Statements().Count(), std::size_t(2));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(blockStatement->Statements()[0])
                != nullptr);
    auto* label = dynamic_cast<Syntax::LabelStatement*>(blockStatement->Statements()[1]);
    ASSERT_TRUE(label != nullptr);
    EXPECT_EQ(label->Label(), "IL_0010");
    // The skipped leave rides the block statement as the implicit return.
    auto* implicitReturn =
        blockStatement->Annotation<CSharp::ImplicitReturnAnnotation>();
    ASSERT_TRUE(implicitReturn != nullptr);
    EXPECT_EQ(implicitReturn->Leave, finalLeavePtr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &container);
    (void)tailPtr;
}

// VisitBlockContainer's single-switch entry arm: the entry block's only
// instruction is a SwitchInstruction, so the container drives TranslateSwitch.
TEST(StatementBuilderTest, VisitBlockContainerSwitchEntryPointDrivesTranslateSwitch)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    container.Kind = IL::ContainerKind::Switch;
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 1;
    IL::Block* entryPtr = entry.get();
    container.AddBlock(std::move(entry));
    auto caseBlock = std::make_unique<IL::Block>();
    caseBlock->StartILOffset = 0x0010;
    IL::Block* casePtr = caseBlock.get();
    caseBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    container.AddBlock(std::move(caseBlock));

    IL::SwitchInstruction* entrySwitch = nullptr;
    {
        auto fresh = std::make_unique<IL::SwitchInstruction>(std::make_unique<IL::LdLoc>(variable));
        auto freshCase = std::make_unique<IL::SwitchSection>(Util::LongSet(1LL));
        freshCase->SetBody(std::make_unique<IL::Branch>(casePtr));
        auto freshDefault = std::make_unique<IL::SwitchSection>(Util::LongSet(
            std::vector<Util::LongInterval>{Util::LongInterval::Inclusive(2, 100)}));
        freshDefault->SetBody(std::make_unique<IL::Branch>(casePtr));
        fresh->AddSection(std::move(freshCase));
        fresh->AddSection(std::move(freshDefault));
        entrySwitch = fresh.get();
        entryPtr->Add(std::move(fresh));
    }
    entryPtr->FinalInstruction = std::make_unique<IL::Nop>();

    auto* stmt = builder.Convert(&container);
    auto* switchStatement = dynamic_cast<Syntax::SwitchStatement*>(stmt);
    ASSERT_TRUE(switchStatement != nullptr);
    ASSERT_EQ(switchStatement->SwitchSections().Count(), std::size_t(2));
    // The inlined branch-target block: the section statements hold the block's
    // converted statement -- a nested BlockStatement over the converted stloc.
    auto* caseSection = switchStatement->SwitchSections()[0];
    ASSERT_GE(caseSection->Statements().Count(), std::size_t(1));
    auto* caseBody = dynamic_cast<Syntax::BlockStatement*>(caseSection->Statements()[0]);
    ASSERT_TRUE(caseBody != nullptr);
    ASSERT_GE(caseBody->Statements().Count(), std::size_t(1));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(caseBody->Statements()[0])
                != nullptr);
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(1));
    EXPECT_EQ(instructions[0], &container);
    (void)entrySwitch;
}

// ConvertLoop's Loop kind: while (true) over the container blocks, with the
// entry-point label removed when every jump to it became a continue.
TEST(StatementBuilderTest, ConvertLoopLoopKindRendersWhileTrue)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    container.Kind = IL::ContainerKind::Loop;
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 2;  // the outer entry + the continue
    entry->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    IL::Block* entryPtr = entry.get();
    container.AddBlock(std::move(entry));
    auto body = std::make_unique<IL::Block>();
    body->StartILOffset = 0x0010;
    body->IncomingEdgeCount = 1;
    body->Add(std::make_unique<IL::Branch>(entryPtr));  // the continue
    body->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(body));

    auto* stmt = builder.Convert(&container);
    auto* whileStatement = dynamic_cast<Syntax::WhileStatement*>(stmt);
    ASSERT_TRUE(whileStatement != nullptr);
    // The condition is the literal true.
    auto* condition = dynamic_cast<Syntax::PrimitiveExpression*>(whileStatement->Condition());
    ASSERT_TRUE(condition != nullptr);
    const bool* trueValue = std::get_if<bool>(&condition->Value());
    ASSERT_TRUE(trueValue != nullptr);
    EXPECT_TRUE(*trueValue);
    auto* loopBody = dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement());
    ASSERT_TRUE(loopBody != nullptr);
    // The entry's statement, the body's label, and the trailing continue removed
    // (the entry-point label was removed too: all jumps became continues).
    ASSERT_EQ(loopBody->Statements().Count(), std::size_t(2));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(loopBody->Statements()[0])
                != nullptr);
    auto* label = dynamic_cast<Syntax::LabelStatement*>(loopBody->Statements()[1]);
    ASSERT_TRUE(label != nullptr);
    EXPECT_EQ(label->Label(), "IL_0010");
    // The container rides the statement twice: the explicit AddAnnotation plus
    // the WithILInstruction wrap (the C# does not dedupe).
    const auto instructions = StatementILInstructions(*stmt);
    ASSERT_EQ(instructions.size(), std::size_t(2));
    EXPECT_EQ(instructions[0], &container);
    EXPECT_EQ(instructions[1], &container);
}

// ConvertLoop's While kind: while (condition) over the condition-block shape,
// with the body's trailing break and the entry-point label for the jumps that
// were not represented as continues.
TEST(StatementBuilderTest, ConvertLoopWhileKindRendersCondition)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    container.Kind = IL::ContainerKind::While;
    auto bodyBlock = std::make_unique<IL::Block>();
    bodyBlock->StartILOffset = 0x0010;
    bodyBlock->IncomingEdgeCount = 1;
    bodyBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    bodyBlock->FinalInstruction = std::make_unique<IL::Nop>();
    IL::Block* bodyPtr = bodyBlock.get();
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 2;
    entry->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1),
        std::make_unique<IL::Branch>(bodyPtr),
        std::make_unique<IL::Leave>(&container)));
    entry->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(entry));
    container.AddBlock(std::move(bodyBlock));

    auto* stmt = builder.Convert(&container);
    auto* whileStatement = dynamic_cast<Syntax::WhileStatement*>(stmt);
    ASSERT_TRUE(whileStatement != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(whileStatement->Condition())
                != nullptr);
    auto* loopBody = dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement());
    ASSERT_TRUE(loopBody != nullptr);
    // The body's statement, the reachability break, and the entry-point label
    // (the second incoming edge was not a continue).
    ASSERT_EQ(loopBody->Statements().Count(), std::size_t(3));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(loopBody->Statements()[0])
                != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::BreakStatement*>(loopBody->Statements()[1]) != nullptr);
    auto* label = dynamic_cast<Syntax::LabelStatement*>(loopBody->Statements()[2]);
    ASSERT_TRUE(label != nullptr);
    EXPECT_EQ(label->Label(), "IL_0000");
}

// ConvertLoop's DoWhile kind: do { ... } while (condition), with the
// condition-block label for the jumps that were not continues.
TEST(StatementBuilderTest, ConvertLoopDoWhileKindRendersDoWhile)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    container.Kind = IL::ContainerKind::DoWhile;
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 2;
    entry->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    IL::Block* entryPtr = entry.get();
    container.AddBlock(std::move(entry));
    auto condBlock = std::make_unique<IL::Block>();
    condBlock->StartILOffset = 0x0010;
    condBlock->IncomingEdgeCount = 1;
    condBlock->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1),
        std::make_unique<IL::Branch>(entryPtr),
        std::make_unique<IL::Leave>(&container)));
    condBlock->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(condBlock));

    auto* stmt = builder.Convert(&container);
    auto* doWhile = dynamic_cast<Syntax::DoWhileStatement*>(stmt);
    ASSERT_TRUE(doWhile != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(doWhile->Condition()) != nullptr);
    auto* loopBody = dynamic_cast<Syntax::BlockStatement*>(doWhile->EmbeddedStatement());
    ASSERT_TRUE(loopBody != nullptr);
    // The entry's statement and the condition-block label (the entry-point label
    // was removed: only two jumps to the entry point).
    ASSERT_EQ(loopBody->Statements().Count(), std::size_t(2));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(loopBody->Statements()[0])
                != nullptr);
    auto* label = dynamic_cast<Syntax::LabelStatement*>(loopBody->Statements()[1]);
    ASSERT_TRUE(label != nullptr);
    EXPECT_EQ(label->Label(), "IL_0010");
}

// ConvertLoop's For kind: for (; condition; increment) over the condition and
// increment blocks.
TEST(StatementBuilderTest, ConvertLoopForKindRendersFor)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    container.Kind = IL::ContainerKind::For;
    auto bodyBlock = std::make_unique<IL::Block>();
    bodyBlock->StartILOffset = 0x0010;
    bodyBlock->IncomingEdgeCount = 1;
    bodyBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    bodyBlock->FinalInstruction = std::make_unique<IL::Nop>();
    IL::Block* bodyPtr = bodyBlock.get();
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 2;
    entry->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1),
        std::make_unique<IL::Branch>(bodyPtr),
        std::make_unique<IL::Leave>(&container)));
    entry->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(entry));
    container.AddBlock(std::move(bodyBlock));
    auto incrBlock = std::make_unique<IL::Block>();
    incrBlock->StartILOffset = 0x0020;
    incrBlock->IncomingEdgeCount = 1;
    incrBlock->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(2)));
    incrBlock->Add(std::make_unique<IL::Branch>(container.EntryPoint()));
    incrBlock->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(incrBlock));

    auto* stmt = builder.Convert(&container);
    auto* forStatement = dynamic_cast<Syntax::ForStatement*>(stmt);
    ASSERT_TRUE(forStatement != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(forStatement->Condition())
                != nullptr);
    // The increment block's instructions except the final branch become the
    // for-loop iterators.
    ASSERT_EQ(forStatement->Iterators().Count(), std::size_t(1));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(forStatement->Iterators()[0])
                != nullptr);
    auto* loopBody = dynamic_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    ASSERT_TRUE(loopBody != nullptr);
    // The body's statement, the reachability break, and the increment-block
    // label (the increment block's incoming edge was not a continue).
    ASSERT_EQ(loopBody->Statements().Count(), std::size_t(3));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(loopBody->Statements()[0])
                != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::BreakStatement*>(loopBody->Statements()[1]) != nullptr);
    auto* label = dynamic_cast<Syntax::LabelStatement*>(loopBody->Statements()[2]);
    ASSERT_TRUE(label != nullptr);
    EXPECT_EQ(label->Label(), "IL_0020");
}

// ConvertBlockContainer's end-container label: a container an escaped Leave
// named rides gets the trailing label (and the continue/break pair inside a
// loop).
TEST(StatementBuilderTest, ConvertBlockContainerEndContainerLabels)
{
    StatementFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    IL::BlockContainer container;
    auto entry = std::make_unique<IL::Block>();
    entry->StartILOffset = 0x0000;
    entry->IncomingEdgeCount = 1;
    entry->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(1)));
    container.AddBlock(std::move(entry));
    auto tail = std::make_unique<IL::Block>();
    tail->StartILOffset = 0x0010;
    tail->IncomingEdgeCount = 1;
    tail->Add(std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(2)));
    tail->FinalInstruction = std::make_unique<IL::Nop>();
    container.AddBlock(std::move(tail));
    builder.endContainerLabels.emplace(&container, "end_IL_0000");

    // A Normal container (isLoop = false): just the trailing label.
    auto* stmt = builder.Convert(&container);
    auto* blockStatement = dynamic_cast<Syntax::BlockStatement*>(stmt);
    ASSERT_TRUE(blockStatement != nullptr);
    ASSERT_EQ(blockStatement->Statements().Count(), std::size_t(4));
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(blockStatement->Statements()[0])
                != nullptr);
    auto* tailLabel = dynamic_cast<Syntax::LabelStatement*>(blockStatement->Statements()[1]);
    ASSERT_TRUE(tailLabel != nullptr);
    EXPECT_EQ(tailLabel->Label(), "IL_0010");
    EXPECT_TRUE(dynamic_cast<Syntax::ExpressionStatement*>(blockStatement->Statements()[2])
                != nullptr);
    auto* endLabel = dynamic_cast<Syntax::LabelStatement*>(blockStatement->Statements()[3]);
    ASSERT_TRUE(endLabel != nullptr);
    EXPECT_EQ(endLabel->Label(), "end_IL_0000");
}

} // namespace ILSpy::Tests
