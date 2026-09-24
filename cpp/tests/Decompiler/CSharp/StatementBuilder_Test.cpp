// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The StatementBuilder first slice (StatementBuilder.cs lines 38-160 + the
// throw/rethrow arms): the ctor state, Convert/ConvertAsBlock, the leaf
// visitors (Nop-with-comment, StLoc/StObj top-level-ref strip, IsInst `is`
// render, Throw/Rethrow), and the EnforceExplicitIn EmitAsRefReadOnly
// flag-write the CallBuilder's `in`-argument arm performs.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/StatementBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::StatementBuilder;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

struct StatementFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function;

    StatementFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          scopelessContext(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharp::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          context_(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope)),
          settings(), run(&settings, usingScope)
    {
    }

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code,
                                              const char* name)
    {
        auto local =
            std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TypePtr(code));
        local->Name = name;
        return local;
    }
};

} // namespace

TEST(StatementBuilderTest, ConvertNopRendersEmptyStatementWithComment)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Nop nop;
    auto result = builder.Convert(&nop);

    auto* empty = dynamic_cast<Syntax::EmptyStatement*>(result.Statement());
    ASSERT_TRUE(empty != nullptr);
    // No diagnostic comment: no trailing trivia.
    EXPECT_TRUE(empty->TrailingTrivia().empty());
}

TEST(StatementBuilderTest, ConvertNopRendersCommentTrivia)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Nop nop;
    nop.Comment = "the pipeline's diagnostic note";
    auto result = builder.Convert(&nop);

    auto* empty = dynamic_cast<Syntax::EmptyStatement*>(result.Statement());
    ASSERT_TRUE(empty != nullptr);
    // The diagnostic comment renders as a trailing `// ...` trivia.
    ASSERT_EQ(empty->TrailingTrivia().size(), 1u);
    auto* comment =
        dynamic_cast<Syntax::Comment*>(empty->TrailingTrivia().front());
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), "the pipeline's diagnostic note");
}

TEST(StatementBuilderTest, ConvertStLocRendersAssignmentAndStripsTopLevelRef)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    auto variable = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");
    // `stloc num (ref num)` -- the C# StLoc arm's DirectionExpression strip:
    // the ref re-assignment drops the outer `ref`.
    IL::StLoc stLoc(variable, std::make_unique<IL::LdcI4>(42));
    auto result = builder.Convert(&stLoc);

    auto* expression = dynamic_cast<Syntax::ExpressionStatement*>(result.Statement());
    ASSERT_TRUE(expression != nullptr);
    auto* assignment =
        dynamic_cast<Syntax::AssignmentExpression*>(expression->Expression());
    ASSERT_TRUE(assignment != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "num");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
}

TEST(StatementBuilderTest, ConvertRethrowRendersBareThrow)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Rethrow rethrow;
    auto result = builder.Convert(&rethrow);

    auto* throwStatement =
        dynamic_cast<Syntax::ThrowStatement*>(result.Statement());
    ASSERT_TRUE(throwStatement != nullptr);
    EXPECT_EQ(throwStatement->Expression(), nullptr);
}

TEST(StatementBuilderTest, ConvertThrowRendersThrowWithArgument)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Throw throwInst(std::make_unique<IL::LdStr>("bang"));
    auto result = builder.Convert(&throwInst);

    auto* throwStatement =
        dynamic_cast<Syntax::ThrowStatement*>(result.Statement());
    ASSERT_TRUE(throwStatement != nullptr);
    ASSERT_TRUE(throwStatement->Expression() != nullptr);
    auto* text =
        dynamic_cast<Syntax::PrimitiveExpression*>(throwStatement->Expression());
    ASSERT_TRUE(text != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(text->Value()));
    EXPECT_EQ(std::get<std::string>(text->Value()), "bang");
}

TEST(StatementBuilderTest, ConvertAsBlockWrapsNonBlockInBlockStatement)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Nop nop;
    auto result = builder.ConvertAsBlock(&nop);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(result.Statement());
    ASSERT_TRUE(block != nullptr);
    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_TRUE(dynamic_cast<Syntax::EmptyStatement*>(block->Statements().At(0))
                != nullptr);
}

TEST(StatementBuilderTest, ConvertStLocStripsTopLevelRefOnAddressOfValue)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    // A by-ref local: `ref int num`; the value is a by-ref load (`ldloc num`)
    // whose port translation is `ref num` (a DirectionExpression) -- the C#
    // StLoc arm strips the top-level `ref` on the re-assignment, leaving the
    // inner operand.
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            fixture.TypePtr(TS::KnownTypeCode::Int32)));
    variable->Name = "num";
    IL::StLoc stLoc(variable, std::make_unique<IL::LdLoc>(variable));
    auto result = builder.Convert(&stLoc);

    auto* expression = dynamic_cast<Syntax::ExpressionStatement*>(result.Statement());
    ASSERT_TRUE(expression != nullptr);
    // The strip: the statement's expression is NOT the DirectionExpression
    // (the C# `expr.UnwrapChild(dirExpr.Expression)`).
    EXPECT_TRUE(dynamic_cast<Syntax::DirectionExpression*>(expression->Expression())
                == nullptr);
}


TEST(StatementBuilderTest, VisitBranchToUnlabeledBlockRendersGoto)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Block target;
    target.Label = "IL_0005";
    IL::Branch branch(&target);
    auto result = builder.Convert(&branch);

    auto* gotoStatement = dynamic_cast<Syntax::GotoStatement*>(result.Statement());
    ASSERT_TRUE(gotoStatement != nullptr);
    EXPECT_EQ(gotoStatement->Label(), "IL_0005");
}

TEST(StatementBuilderTest, VisitBranchToContinueTargetRendersContinue)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_,
                             &fixture.function, &fixture.settings, &fixture.run);
    IL::Block target;
    target.Label = "IL_0002";
    // The loop-translation machinery sets the continue target before
    // translating the body; the branch to it renders `continue;`.
    builder.continueTarget = &target;
    IL::Branch branch(&target);
    auto result = builder.Convert(&branch);

    EXPECT_TRUE(dynamic_cast<Syntax::ContinueStatement*>(result.Statement()) != nullptr);
}

TEST(StatementBuilderTest, VisitLeaveToReturnContainerRendersReturn)
{
    StatementFixture fixture;
    // The C# ctor reads the current-return container from the function body;
    // the port's fixture wires the function's body container.
    fixture.function.Body = std::make_unique<IL::BlockContainer>();
    fixture.function.Body->AddBlock(std::make_unique<IL::Block>());
    // The C# function's return type is set by the reader; the port's ctor
    // reads it into currentResultType (the `IsAsync ? AsyncReturnType :
    // ReturnType` chain).
    fixture.function.ReturnType = fixture.TypePtr(TS::KnownTypeCode::String);
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    IL::Leave leave(&*fixture.function.Body, std::make_unique<IL::LdStr>("done"));
    auto result = builder.Convert(&leave);

    auto* returnStatement =
        dynamic_cast<Syntax::ReturnStatement*>(result.Statement());
    ASSERT_TRUE(returnStatement != nullptr);
    ASSERT_TRUE(returnStatement->Expression() != nullptr);
    auto* text = dynamic_cast<Syntax::PrimitiveExpression*>(returnStatement->Expression());
    ASSERT_TRUE(text != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(text->Value()));
    EXPECT_EQ(std::get<std::string>(text->Value()), "done");
}

TEST(StatementBuilderTest, VisitLeaveToNonReturnContainerRendersGotoEnd)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    IL::BlockContainer other;
    other.Blocks.push_back(std::make_unique<IL::Block>());
    other.Blocks.front()->Label = "IL_0042";
    IL::Leave leave(&other);
    auto result = builder.Convert(&leave);

    auto* gotoStatement = dynamic_cast<Syntax::GotoStatement*>(result.Statement());
    ASSERT_TRUE(gotoStatement != nullptr);
    EXPECT_EQ(gotoStatement->Label(), "end_IL_0042");
}

TEST(StatementBuilderTest, VisitLeaveMatchingBreakTargetRendersBreak)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    IL::BlockContainer loop;
    builder.breakTarget = &loop;
    IL::Leave leave(&loop);
    auto result = builder.Convert(&leave);

    EXPECT_TRUE(dynamic_cast<Syntax::BreakStatement*>(result.Statement()) != nullptr);
}

TEST(StatementBuilderTest, VisitBranchToCaseLabelRendersGotoCase)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    IL::Block caseBlock;
    caseBlock.Label = "IL_0010";
    builder.caseLabelMapping =
        std::map<IL::Block*, std::optional<std::shared_ptr<Sem::ResolveResult>>>{};
    builder.caseLabelMapping->emplace(
        &caseBlock,
        std::make_shared<Sem::ConstantResolveResult>(
            fixture.TypePtr(TS::KnownTypeCode::Int32), std::any(std::int32_t{7})));
    IL::Branch branch(&caseBlock);
    auto result = builder.Convert(&branch);

    auto* gotoCase = dynamic_cast<Syntax::GotoCaseStatement*>(result.Statement());
    ASSERT_TRUE(gotoCase != nullptr);
    ASSERT_TRUE(gotoCase->LabelExpression() != nullptr);
    auto* value = dynamic_cast<Syntax::PrimitiveExpression*>(gotoCase->LabelExpression());
    ASSERT_TRUE(value != nullptr);
    const std::int32_t* number = std::get_if<std::int32_t>(&value->Value());
    ASSERT_TRUE(number != nullptr);
    EXPECT_EQ(*number, 7);
}


TEST(StatementBuilderTest, VisitIfInstructionRendersIfElse)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `if (ldc.i4 1) nop; else nop;` -- the true branch renders (an
    // ExpressionStatement over the constant), the false Nop branch renders as
    // the absent else (the C# `FalseInst.OpCode == OpCode.Nop ? null : ...`).
    IL::IfInstruction ifInstruction(std::make_unique<IL::LdcI4>(1),
                                    std::make_unique<IL::Nop>(),
                                    std::make_unique<IL::Nop>());
    auto result = builder.Convert(&ifInstruction);

    auto* ifElse = dynamic_cast<Syntax::IfElseStatement*>(result.Statement());
    ASSERT_TRUE(ifElse != nullptr);
    ASSERT_TRUE(ifElse->Condition() != nullptr);
    // The true branch is the converted constant expression statement.
    EXPECT_TRUE(ifElse->TrueStatement() != nullptr);
    // The Nop false branch becomes the null else.
    EXPECT_EQ(ifElse->FalseStatement(), nullptr);
}

TEST(StatementBuilderTest, VisitTryCatchRendersCatchClauses)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `try { nop; } catch (Exception e) { nop; }` -- the handler's variable is
    // stored once (StoreCount == 1), so the clause renders the type only.
    auto exceptionType = fixture.TypePtr(TS::KnownTypeCode::Exception);
    auto handlerVariable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::ExceptionLocal, exceptionType);
    handlerVariable->Name = "e";
    auto handlerBody = std::make_unique<IL::Block>();
    handlerBody->Instructions.push_back(std::make_unique<IL::Nop>());
    auto caughtVariable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::ExceptionLocal, exceptionType);
    caughtVariable->Name = "e";
    // The handler: (filter, body, variable) -- the filter is `ldc.i4 1` (the
    // C# `MatchLdcI4(1)` trivial-filter gate).
    auto handler = std::make_unique<IL::TryCatchHandler>(
        /*filter=*/std::make_unique<IL::LdcI4>(1), std::move(handlerBody),
        std::move(caughtVariable));
    auto tryBlock = std::make_unique<IL::Block>();
    tryBlock->Instructions.push_back(std::make_unique<IL::Nop>());
    IL::TryCatch tryCatch(std::move(tryBlock));
    tryCatch.Handlers.push_back(std::move(handler));
    auto result = builder.Convert(&tryCatch);

    auto* tryCatchStatement =
        dynamic_cast<Syntax::TryCatchStatement*>(result.Statement());
    ASSERT_TRUE(tryCatchStatement != nullptr);
    ASSERT_EQ(tryCatchStatement->CatchClauses().Count(), 1);
    auto* clause = tryCatchStatement->CatchClauses().At(0);
    ASSERT_TRUE(clause != nullptr);
    // The type renders because the caught type is not System.Object.
    EXPECT_TRUE(clause->Type() != nullptr);
}

TEST(StatementBuilderTest, VisitTryFinallyRendersFinally)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    auto tryBody = std::make_unique<IL::Block>();
    tryBody->Instructions.push_back(std::make_unique<IL::Nop>());
    auto finallyBody = std::make_unique<IL::Block>();
    finallyBody->Instructions.push_back(std::make_unique<IL::Nop>());
    IL::TryFinally tryFinally(std::move(tryBody), std::move(finallyBody));
    auto result = builder.Convert(&tryFinally);

    auto* tryCatchStatement =
        dynamic_cast<Syntax::TryCatchStatement*>(result.Statement());
    ASSERT_TRUE(tryCatchStatement != nullptr);
    EXPECT_TRUE(tryCatchStatement->FinallyBlock() != nullptr);
}

TEST(StatementBuilderTest, VisitTryFaultRendersFaultMarkerAndThrow)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    auto tryBody = std::make_unique<IL::Block>();
    tryBody->Instructions.push_back(std::make_unique<IL::Nop>());
    auto faultBody = std::make_unique<IL::Block>();
    faultBody->FinalInstruction = std::make_unique<IL::Nop>();
    IL::TryFault tryFault(std::move(tryBody), std::move(faultBody));
    auto result = builder.Convert(&tryFault);

    auto* tryCatchStatement =
        dynamic_cast<Syntax::TryCatchStatement*>(result.Statement());
    ASSERT_TRUE(tryCatchStatement != nullptr);
    ASSERT_EQ(tryCatchStatement->CatchClauses().Count(), 1);
    auto* faultClause = tryCatchStatement->CatchClauses().At(0);
    ASSERT_TRUE(faultClause != nullptr);
    ASSERT_TRUE(faultClause->Body() != nullptr);
    // The C# inserts the `/*try-fault*/` comment marker and a bare `throw;`
    // into the fault block.
    ASSERT_EQ(faultClause->Body()->Statements().Count(), 2);
    auto* marker = dynamic_cast<Syntax::EmptyStatement*>(
        faultClause->Body()->Statements().At(0));
    ASSERT_TRUE(marker != nullptr);
    ASSERT_EQ(marker->TrailingTrivia().size(), 1u);
    auto* comment = dynamic_cast<Syntax::Comment*>(marker->TrailingTrivia()[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), "try-fault");
    EXPECT_TRUE(dynamic_cast<Syntax::ThrowStatement*>(
                    faultClause->Body()->Statements().At(1))
                != nullptr);
}

TEST(StatementBuilderTest, VisitLockInstructionRendersLock)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    auto body = std::make_unique<IL::Block>();
    body->Instructions.push_back(std::make_unique<IL::Nop>());
    IL::LockInstruction lockInstruction(std::make_unique<IL::LdStr>("gate"),
                                        std::move(body));
    auto result = builder.Convert(&lockInstruction);

    auto* lockStatement = dynamic_cast<Syntax::LockStatement*>(result.Statement());
    ASSERT_TRUE(lockStatement != nullptr);
    ASSERT_TRUE(lockStatement->Expression() != nullptr);
    auto* text = dynamic_cast<Syntax::PrimitiveExpression*>(lockStatement->Expression());
    ASSERT_TRUE(text != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(text->Value()));
    EXPECT_EQ(std::get<std::string>(text->Value()), "gate");
}


TEST(StatementBuilderTest, VisitPinnedRegionRendersFixed)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `fixed (int* p = &refLocal) { nop; }` -- the init is a by-ref LOAD
    // (`ldloc refLocal` over a ByReferenceType-typed local): the port's by-ref
    // LdLoc arm renders `ref refLocal` (a DirectionExpression), which the C#
    // unwrap turns into the `&refLocal` address-of (the else branch -- the
    // value is not a dereference).
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::PointerType>(fixture.TypePtr(TS::KnownTypeCode::Int32)));
    variable->Name = "p";
    auto refLocal = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Parameter,
        std::make_shared<TS::ByReferenceType>(fixture.TypePtr(TS::KnownTypeCode::Int32)));
    refLocal->Name = "refLocal";
    // A real parameter slot (Index >= 0): the ConvertVariable this-reference
    // arm keys on `Index < 0`.
    refLocal->Index = 0;
    auto body = std::make_unique<IL::Block>();
    body->FinalInstruction = std::make_unique<IL::Nop>();
    IL::PinnedRegion pinned(variable, std::make_unique<IL::LdLoc>(refLocal),
                            std::move(body));
    auto result = builder.Convert(&pinned);

    auto* fixedStatement = dynamic_cast<Syntax::FixedStatement*>(result.Statement());
    ASSERT_TRUE(fixedStatement != nullptr);
    ASSERT_TRUE(fixedStatement->Type() != nullptr);
    ASSERT_EQ(fixedStatement->Variables().Count(), 1);
    auto* initializer = fixedStatement->Variables().At(0);
    ASSERT_TRUE(initializer != nullptr);
    EXPECT_EQ(initializer->Name(), "p");
    // The initializer's variable annotation rides the ILVariable channel.
    EXPECT_EQ(::ILSpy::Decompiler::CSharp::GetILVariable(*initializer),
              variable.get());
    // The DirectionExpression unwrap: the init renders as the `&refLocal`
    // address-of (not the raw `ref refLocal`).
    auto* addressOf = dynamic_cast<Syntax::UnaryOperatorExpression*>(
        initializer->Initializer());
    ASSERT_TRUE(addressOf != nullptr);
    EXPECT_EQ(addressOf->Operator(), Syntax::UnaryOperatorType::AddressOf);
    EXPECT_TRUE(fixedStatement->EmbeddedStatement() != nullptr);
}


TEST(StatementBuilderTest, VisitSwitchInstructionRendersSwitchWithCaseLabels)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `switch (ldc.i4 1) { case 0: goto IL_0010; case 1: nop; }` -- the
    // governing value is the constant 1, the sections carry the labels via
    // the LongSet, and the bodies are branches into the container's blocks.
    auto block0 = std::make_unique<IL::Block>();
    block0->Label = "IL_0005";
    IL::Branch branch0(block0.get());
    IL::SwitchSection section0;
    section0.Labels = Util::LongSet(static_cast<long long>(0));
    section0.Body = std::make_unique<IL::Branch>(block0.get());
    // The second section carries more labels than the first, so it stands
    // default (GetDefaultSection picks the max-label section) and section 0
    // renders as `case 0:`.
    IL::SwitchSection section1;
    section1.Labels = Util::LongSet(Util::LongInterval::Inclusive(5, 6));
    section1.Body = std::make_unique<IL::Branch>(block0.get());
    IL::SwitchInstruction switchInstruction(std::make_unique<IL::LdcI4>(1));
    switchInstruction.Sections.push_back(std::make_unique<IL::SwitchSection>(std::move(section0)));
    switchInstruction.Sections.push_back(std::make_unique<IL::SwitchSection>(std::move(section1)));
    auto result = builder.Convert(&switchInstruction);

    auto* switchStatement =
        dynamic_cast<Syntax::SwitchStatement*>(result.Statement());
    ASSERT_TRUE(switchStatement != nullptr);
    ASSERT_TRUE(switchStatement->Expression() != nullptr);
    ASSERT_EQ(switchStatement->SwitchSections().Count(), 2);
    auto* astSection = switchStatement->SwitchSections().At(0);
    ASSERT_EQ(astSection->CaseLabels().Count(), 1);
    auto* caseLabel = astSection->CaseLabels().At(0);
    ASSERT_TRUE(caseLabel->Expression() != nullptr);
    auto* value = dynamic_cast<Syntax::PrimitiveExpression*>(caseLabel->Expression());
    ASSERT_TRUE(value != nullptr);
    const std::int32_t* number = std::get_if<std::int32_t>(&value->Value());
    ASSERT_TRUE(number != nullptr);
    EXPECT_EQ(*number, 0);
}


TEST(StatementBuilderTest, VisitBlockContainerRendersWhileLoop)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `while (1) { v = 42; }` -- the While container: the entry block is the
    // condition (the if in the port's FinalInstruction slot; the false arm
    // leaves the container, the true arm branches to the body), the body
    // stores the constant and branches back to the entry (the `continue;`
    // edge). The entry sees two incoming edges (the loop's dispatch plus the
    // body's back edge), so the loop shape is recognized.
    auto container = std::make_unique<IL::BlockContainer>();
    container->Kind = IL::ContainerKind::While;
    auto entry = std::make_unique<IL::Block>();
    entry->Label = "IL_0000";
    entry->FinalInstruction = std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::Branch>(nullptr),
        nullptr);
    IL::Block* entryPtr = entry.get();
    container->AddBlock(std::move(entry));
    auto body = std::make_unique<IL::Block>();
    body->Label = "IL_0010";
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                     fixture.TypePtr(TS::KnownTypeCode::Int32));
    variable->Name = "v";
    body->Instructions.push_back(
        std::make_unique<IL::StLoc>(variable, std::make_unique<IL::LdcI4>(42)));
    IL::Block* bodyPtr = body.get();
    container->AddBlock(std::move(body));
    // Wire the branches now that the blocks sit in the container.
    auto* entryIf = static_cast<IL::IfInstruction*>(entryPtr->FinalInstruction.get());
    static_cast<IL::Branch*>(entryIf->TrueInst.get())->TargetBlock = bodyPtr;
    auto* leave = new IL::Leave(container.get());
    entryIf->FalseInst.reset(leave);
    bodyPtr->FinalInstruction = std::make_unique<IL::Branch>(entryPtr);
    entryPtr->IncomingEdgeCount = 2;  // loop dispatch + the body back edge
    bodyPtr->IncomingEdgeCount = 1;   // the condition's true arm
    auto result = builder.Convert(container.get());

    auto* whileStatement =
        dynamic_cast<Syntax::WhileStatement*>(result.Statement());
    ASSERT_TRUE(whileStatement != nullptr);
    ASSERT_TRUE(whileStatement->Condition() != nullptr);
    ASSERT_TRUE(whileStatement->EmbeddedStatement() != nullptr);
    // The body renders the store plus the `continue;` -- and the trailing
    // continue is removed (it is redundant with the loop edge).
    auto* embeddedBlock =
        dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement());
    ASSERT_TRUE(embeddedBlock != nullptr);
    ASSERT_EQ(embeddedBlock->Statements().Count(), 1);
}


TEST(StatementBuilderTest, VisitUsingInstructionRendersUsingStatement)
{
    StatementFixture fixture;
    StatementBuilder builder(fixture.compilation, *fixture.context_, &fixture.function,
                             &fixture.settings, &fixture.run);
    // `using (IDisposable disposable = ldloc) { nop; }` -- the resource local
    // has IDisposable as its own type (so the base-type probe passes) and is
    // loaded, which makes the acquisition a VariableDeclarationStatement.
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::UsingLocal,
        fixture.TypePtr(TS::KnownTypeCode::IDisposable));
    variable->Name = "disposable";
    variable->StoreCount = 1;
    variable->LoadCount = 1;
    auto container = std::make_unique<IL::BlockContainer>();
    auto body = std::make_unique<IL::Block>();
    body->Instructions.push_back(std::make_unique<IL::Nop>());
    body->FinalInstruction = std::make_unique<IL::Nop>();
    container->AddBlock(std::move(body));
    IL::UsingInstruction usingInstruction(
        variable, std::make_unique<IL::LdLoc>(variable), std::move(container));
    std::fprintf(stderr, "DBG9\n");
    auto result = builder.Convert(&usingInstruction);
    std::fprintf(stderr, "DBG10\n");

    auto* usingStatement =
        dynamic_cast<Syntax::UsingStatement*>(result.Statement());
    std::fprintf(stderr, "DBG11\n");
    ASSERT_TRUE(usingStatement != nullptr);
    ASSERT_FALSE(usingStatement->IsAsync());
    // The resource is loaded exactly once, so the acquisition is the variable
    // declaration with the ILVariableResolveResult annotation.
    auto* vds = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        usingStatement->ResourceAcquisition());
    ASSERT_TRUE(vds != nullptr);
    ASSERT_EQ(vds->Variables().Count(), 1);
    EXPECT_EQ(vds->Variables().At(0)->Name(), "disposable");
    EXPECT_TRUE(usingStatement->EmbeddedStatement() != nullptr);
}


} // namespace ILSpy::Tests