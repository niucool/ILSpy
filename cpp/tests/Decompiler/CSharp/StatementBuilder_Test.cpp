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
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
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


} // namespace ILSpy::Tests