// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the DeclareVariables analysis-half tests
// (ICSharpCode.Decompiler/CSharp/Transforms/DeclareVariables.cs): the
// VariableNeedsDeclaration kind table, the insertion-point computation
// (first-use statement, common-parent merge over sibling blocks and nested
// uses, the for-initializer special case, and the UsesInitialValue hoist out
// of loops), the same-block name-collision merge, and ClearAnalysisResults.

#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using CS::Transforms::DeclareVariables;

// A `name`-named local of unknown type (the annotation the analysis reads is
// attached to the identifier expressions, not the variable).
IL::ILVariablePtr Local(const std::string& name) {
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                     TS::UnknownType());
    variable->Name = name;
    return variable;
}

// A `name` identifier carrying the variable annotation.
Syntax::IdentifierExpression* Var(const std::string& name,
                                  const IL::ILVariablePtr& variable) {
    auto* expression = new Syntax::IdentifierExpression(name);
    expression->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(variable));
    return expression;
}

// A bare `name();` statement using the variable.
Syntax::ExpressionStatement* Use(const std::string& name,
                                 const IL::ILVariablePtr& variable) {
    return new Syntax::ExpressionStatement(Var(name, variable));
}

// The kind table: the kinds that carry their own declaration are excluded.
TEST(DeclareVariablesTest, VariableNeedsDeclarationKindTable)
{
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PinnedRegionLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::Parameter));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ExceptionLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ExceptionStackSlot));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::UsingLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ForeachLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PatternLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::Local));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::StackSlot));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PinnedLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::InitializerTarget));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::NamedArgument));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::DisplayClassLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::DeconstructionInitTemporary));
}

// A variable used twice in one block is declared before its first use's
// statement.
TEST(DeclareVariablesTest, AnalyzeFindsFirstUseStatement)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("v", v);
    block->Statements().Add(stmt1);
    block->Statements().Add(Use("v", v));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), stmt1);
    EXPECT_FALSE(declareVariables.WasMerged(v.get()));
}

// A variable used in both arms of an if is declared before the whole if
// statement (the common parent of the two uses).
TEST(DeclareVariablesTest, AnalyzeMergesSiblingBlocksToCommonParent)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* thenBlock = new Syntax::BlockStatement();
    thenBlock->Statements().Add(Use("v", v));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(Use("v", v));
    auto* ifStatement = new Syntax::IfElseStatement(
        new Syntax::IdentifierExpression("c"), thenBlock);
    ifStatement->FalseStatement(elseBlock);
    block->Statements().Add(ifStatement);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), ifStatement)
        << "the declaration moves before the if statement";
}

// A variable used before and inside a nested block is declared before the
// first (outer) use.
TEST(DeclareVariablesTest, AnalyzeMergesNestedUseToOuterStatement)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("v", v);
    block->Statements().Add(stmt1);
    auto* nestedBlock = new Syntax::BlockStatement();
    nestedBlock->Statements().Add(Use("v", v));
    block->Statements().Add(
        new Syntax::IfElseStatement(new Syntax::IdentifierExpression("c"), nestedBlock));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), stmt1);
}

// Two same-named variables used in the same block cannot both be declared
// there: ResolveCollisions merges them into one declaration before the first
// use.
TEST(DeclareVariablesTest, SameBlockUsesOfSameNamedVariablesMerge)
{
    IL::ILVariablePtr i1 = Local("i");
    IL::ILVariablePtr i2 = Local("i");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("i", i1);
    block->Statements().Add(stmt1);
    block->Statements().Add(Use("i", i2));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_TRUE(declareVariables.WasMerged(i1.get()));
    EXPECT_TRUE(declareVariables.WasMerged(i2.get()));
    EXPECT_EQ(declareVariables.GetDeclarationPoint(i1.get()), stmt1);
    EXPECT_EQ(declareVariables.GetDeclarationPoint(i2.get()), stmt1)
        << "the merged declaration sits before the first use";
}

// Distinctly-named variables in the same block do not collide.
TEST(DeclareVariablesTest, DistinctNamedVariablesDoNotMerge)
{
    IL::ILVariablePtr a = Local("a");
    IL::ILVariablePtr b = Local("b");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("a", a);
    Syntax::Statement* stmt2 = Use("b", b);
    block->Statements().Add(stmt1);
    block->Statements().Add(stmt2);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_FALSE(declareVariables.WasMerged(a.get()));
    EXPECT_FALSE(declareVariables.WasMerged(b.get()));
    EXPECT_EQ(declareVariables.GetDeclarationPoint(a.get()), stmt1);
    EXPECT_EQ(declareVariables.GetDeclarationPoint(b.get()), stmt2);
}

// The initializer of a for statement can itself declare the variable (with
// scope local to the loop), so a matching `i = ...` initializer keeps the
// insertion point inside the for's initializer list instead of walking up to
// the enclosing block.
TEST(DeclareVariablesTest, ForInitializerKeepsLocalInsertionPoint)
{
    IL::ILVariablePtr i = Local("i");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* forStatement = new Syntax::ForStatement();
    auto* initializer = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("i", i),
                                         Syntax::AssignmentOperatorType::Assign,
                                         new Syntax::IdentifierExpression("zero")));
    forStatement->Initializers().Add(initializer);
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var("i", i), Syntax::BinaryOperatorType::LessThan,
        new Syntax::IdentifierExpression("n")));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("i", i),
                                         Syntax::AssignmentOperatorType::Assign,
                                         new Syntax::IdentifierExpression("one"))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());
    block->Statements().Add(forStatement);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(i.get()), initializer)
        << "the for-initializer special case keeps the local insertion point";
}

// A variable whose initial value is used (a possible loop-carried dependency)
// is declared OUTSIDE any loop whose scope annotation marks it as a loop (the
// entry point has more than one incoming edge).
TEST(DeclareVariablesTest, UsesInitialValueHoistsOutOfLoop)
{
    IL::ILVariablePtr v = Local("v");
    v->UsesInitialValue = true;
    // The loop's scope annotation: a BlockContainer whose entry point is a
    // multi-predecessor block (the ILAst loop shape).
    auto container = std::make_shared<IL::BlockContainer>();
    container->Blocks.push_back(std::make_unique<IL::Block>());
    container->Blocks.front()->IncomingEdgeCount = 2;

    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* before = new Syntax::ExpressionStatement(
        new Syntax::IdentifierExpression("work0"));
    block->Statements().Add(before);
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(Use("v", v));
    auto* loop = new Syntax::WhileStatement(
        new Syntax::IdentifierExpression("cond"), body);
    loop->AddAnnotation(std::make_shared<CS::ILInstructionAnnotation>(container.get()));
    block->Statements().Add(loop);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), loop)
        << "the loop-carried dependency is declared before the loop";
}

// ClearAnalysisResults drops the analysis state (a later GetDeclarationPoint
// is the unknown-variable throw).
TEST(DeclareVariablesTest, ClearAnalysisResultsDropsTheDict)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(Use("v", v));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);
    declareVariables.ClearAnalysisResults();

    EXPECT_THROW(declareVariables.GetDeclarationPoint(v.get()), std::out_of_range);
    EXPECT_THROW(declareVariables.WasMerged(v.get()), std::out_of_range);
}

} // namespace
