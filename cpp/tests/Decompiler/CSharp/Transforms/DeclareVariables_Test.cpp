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

// Tests for the DeclareVariables analysis phase (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/DeclareVariables.cs): the
// VariableNeedsDeclaration / IsValidInStatementExpression statics, the InsertionPoint
// navigation, the FindInsertionPoints common-parent computation (including the loop-scope
// UsesInitialValue hoist and the local-function captured-variable walk), and
// ResolveCollisions with the GetDeclarationPoint / WasMerged / ResolveVariableToDeclare
// surface.

#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CSharp = ILSpy::Decompiler::CSharp;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ILSpy::Decompiler::CSharp::Transforms;
namespace IL = ILSpy::Decompiler::IL;
namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Impl = ILSpy::Decompiler::TypeSystem::Implementation;
using ILSpy::Decompiler::DecompileRun;
using ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the AddCheckedBlocks suite shape): a compilation +
// settings + resolve context the mutation-phase helpers read their settings from.
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope)) {}

    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

Syntax::BlockStatement* Block() {
    return new Syntax::BlockStatement();
}

Syntax::ExpressionStatement* Stmt(Syntax::Expression* expression) {
    return new Syntax::ExpressionStatement(expression);
}

Syntax::IdentifierExpression* Id(const char* name) {
    return new Syntax::IdentifierExpression(name);
}

IL::ILVariablePtr Var(const std::string& name = "x",
                      IL::VariableKind kind = IL::VariableKind::Local) {
    auto variable = std::make_shared<IL::ILVariable>(kind, TS::UnknownType());
    variable->Name = name;
    return variable;
}

Syntax::IdentifierExpression* Use(const IL::ILVariablePtr& variable) {
    auto* identifier = Id(variable->Name.c_str());
    identifier->AddAnnotation(std::make_shared<CSharp::ILVariableResolveResult>(variable));
    return identifier;
}

} // namespace

// ---- VariableNeedsDeclaration ---------------------------------------------

TEST(DeclareVariablesTest, VariableNeedsDeclarationMatchesKindMatrix)
{
    using IL::VariableKind;
    // true: the declaration is emitted by this transform.
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::Local));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::PinnedLocal));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::InitializerTarget));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::StackSlot));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::NamedArgument));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::DisplayClassLocal));
    EXPECT_TRUE(Transforms::DeclareVariables::VariableNeedsDeclaration(
        VariableKind::DeconstructionInitTemporary));
    // false: declared by the higher-level construct.
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::PinnedRegionLocal));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::Parameter));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::ExceptionLocal));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::ExceptionStackSlot));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::UsingLocal));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::ForeachLocal));
    EXPECT_FALSE(Transforms::DeclareVariables::VariableNeedsDeclaration(VariableKind::PatternLocal));
}

// ---- IsValidInStatementExpression -----------------------------------------

TEST(DeclareVariablesTest, IsValidInStatementExpressionAcceptsTheValidForms)
{
    Syntax::InvocationExpression invocation;
    EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(invocation));

    Syntax::ObjectCreateExpression objectCreate;
    EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(objectCreate));

    Syntax::AssignmentExpression assignment(Id("a"), Id("b"));
    EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(assignment));

    Syntax::ErrorExpression error;
    EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(error));

    for (Syntax::UnaryOperatorType op : {Syntax::UnaryOperatorType::PostIncrement,
                                         Syntax::UnaryOperatorType::PostDecrement,
                                         Syntax::UnaryOperatorType::Increment,
                                         Syntax::UnaryOperatorType::Decrement,
                                         Syntax::UnaryOperatorType::Await}) {
        Syntax::UnaryOperatorExpression unary(new Syntax::IdentifierExpression("a"), op);
        EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(unary))
            << static_cast<int>(op);
    }
}

TEST(DeclareVariablesTest, IsValidInStatementExpressionRejectsAndRecurses)
{
    // A null-conditional rewrap recurses into its operand.
    Syntax::UnaryOperatorExpression rewrapped(new Syntax::InvocationExpression(), 
                                             Syntax::UnaryOperatorType::NullConditionalRewrap);
    EXPECT_TRUE(Transforms::DeclareVariables::IsValidInStatementExpression(rewrapped));

    Syntax::UnaryOperatorExpression rewrappedBad(Id("a"),
                                                 Syntax::UnaryOperatorType::NullConditionalRewrap);
    EXPECT_FALSE(Transforms::DeclareVariables::IsValidInStatementExpression(rewrappedBad));

    // The remaining unary operators are invalid as statement expressions.
    Syntax::UnaryOperatorExpression minus(Id("a"), Syntax::UnaryOperatorType::Minus);
    EXPECT_FALSE(Transforms::DeclareVariables::IsValidInStatementExpression(minus));

    // A bare identifier is not a valid statement expression.
    Syntax::IdentifierExpression identifier("a");
    EXPECT_FALSE(Transforms::DeclareVariables::IsValidInStatementExpression(identifier));
}

// ---- InsertionPoint navigation --------------------------------------------

TEST(DeclareVariablesTest, InsertionPointUpAndUpToWalkParents)
{
    // block(0) -> statement(1) -> assignment(2) -> identifier(3)
    auto* block = Block();
    auto* identifier = Id("x");
    auto* assignment = new Syntax::AssignmentExpression(identifier, Id("y"));
    auto* statement = Stmt(assignment);
    block->Statements().Add(statement);

    Transforms::DeclareVariables::InsertionPoint deep{3, identifier};
    auto up = deep.Up();
    EXPECT_EQ(up.level, 2);
    EXPECT_EQ(up.nextNode, static_cast<Syntax::AstNode*>(assignment));

    auto shallow = deep.UpTo(1);
    EXPECT_EQ(shallow.nextNode, static_cast<Syntax::AstNode*>(statement));
    EXPECT_EQ(shallow.level, 1);

    auto same = deep.UpTo(3);
    EXPECT_EQ(same.nextNode, static_cast<Syntax::AstNode*>(identifier));
    EXPECT_EQ(same.level, 3);
}

// ---- FindInsertionPoints / FindCommonParent -------------------------------

TEST(DeclareVariablesTest, AnalyzeComputesTheCommonParentOfAllUses)
{
    auto variable = Var("x");
    auto* block = Block();
    auto* first = Stmt(new Syntax::AssignmentExpression(Use(variable), Id("1")));
    auto* second = Stmt(Use(variable));
    block->Statements().Add(first);
    block->Statements().Add(second);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);

    // The common parent of the two uses is the first statement.
    EXPECT_EQ(analysis.GetDeclarationPoint(*variable), static_cast<Syntax::AstNode*>(first));
    EXPECT_FALSE(analysis.WasMerged(*variable));
}

TEST(DeclareVariablesTest, AnalyzeRecordedVariableOnlyOnce)
{
    auto variable = Var("x");
    auto* block = Block();
    auto* statement = Stmt(Use(variable));
    block->Statements().Add(statement);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    EXPECT_EQ(analysis.GetDeclarationPoint(*variable), static_cast<Syntax::AstNode*>(statement));
}

TEST(DeclareVariablesTest, AnalyzeIgnoresVariablesThatDoNotNeedDeclaration)
{
    auto parameter = Var("p", IL::VariableKind::Parameter);
    auto* block = Block();
    block->Statements().Add(Stmt(Use(parameter)));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    EXPECT_THROW(analysis.GetDeclarationPoint(*parameter), std::out_of_range);
}

TEST(DeclareVariablesTest, AnalyzeUsesInitialValueHoistsOutOfLoopScope)
{
    auto variable = Var("x");
    variable->UsesInitialValue = true;

    // A loop-scope BlockContainer (IncomingEdgeCount > 1) attached to the inner block.
    IL::BlockContainer container;
    auto entryPoint = std::make_unique<IL::Block>();
    entryPoint->IncomingEdgeCount = 2;
    container.AddBlock(std::move(entryPoint));

    auto* outer = Block();
    auto* loop = Block();
    outer->Statements().Add(loop);
    loop->Statements().Add(Stmt(Use(variable)));
    loop->AddAnnotation(std::make_shared<CSharp::BlockContainerAnnotation>(&container));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*outer);

    // The declaration is hoisted to the loop block (outside the loop's entry).
    EXPECT_EQ(analysis.GetDeclarationPoint(*variable), static_cast<Syntax::AstNode*>(loop));
}

TEST(DeclareVariablesTest, AnalyzeWalksLocalFunctionCapturedVariables)
{
    auto captured = Var("captured");
    IL::ILFunction localFunction;
    localFunction.Kind = IL::ILFunctionKind::LocalFunction;
    localFunction.CapturedVariables.push_back(captured.get());

    auto* block = Block();
    auto* statement = Stmt(Id("local"));
    CSharp::WithILFunction(*statement->Expression(), &localFunction);
    block->Statements().Add(statement);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    EXPECT_EQ(analysis.GetDeclarationPoint(*captured), static_cast<Syntax::AstNode*>(statement));
}

// ---- ResolveCollisions -----------------------------------------------------

TEST(DeclareVariablesTest, ResolveCollisionsMergesSameNameVariables)
{
    auto firstVariable = Var("x");
    auto secondVariable = Var("x");

    auto* block = Block();
    auto* first = Stmt(Use(firstVariable));
    auto* second = Stmt(Use(secondVariable));
    block->Statements().Add(first);
    block->Statements().Add(second);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);

    // The two declarations collide and merge into the first statement.
    EXPECT_TRUE(analysis.WasMerged(*firstVariable));
    EXPECT_TRUE(analysis.WasMerged(*secondVariable));
    EXPECT_EQ(analysis.GetDeclarationPoint(*firstVariable), static_cast<Syntax::AstNode*>(first));
    EXPECT_EQ(analysis.GetDeclarationPoint(*secondVariable), static_cast<Syntax::AstNode*>(first));

    // ResolveVariableToDeclare follows the replacement chain to the surviving variable.
    auto* resolved = analysis.ResolveVariableToDeclare(firstVariable.get());
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->ILVariable(), secondVariable.get());
}

TEST(DeclareVariablesTest, ResolveCollisionsKeepsDifferentNamesSeparate)
{
    auto firstVariable = Var("x");
    auto secondVariable = Var("y");

    auto* block = Block();
    auto* first = Stmt(Use(firstVariable));
    auto* second = Stmt(Use(secondVariable));
    block->Statements().Add(first);
    block->Statements().Add(second);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    EXPECT_FALSE(analysis.WasMerged(*firstVariable));
    EXPECT_FALSE(analysis.WasMerged(*secondVariable));
    EXPECT_EQ(analysis.GetDeclarationPoint(*firstVariable), static_cast<Syntax::AstNode*>(first));
    EXPECT_EQ(analysis.GetDeclarationPoint(*secondVariable), static_cast<Syntax::AstNode*>(second));
}

TEST(DeclareVariablesTest, ResolveVariableToDeclareReturnsNullForUnknownOrNull)
{
    Transforms::DeclareVariables analysis;
    EXPECT_EQ(analysis.ResolveVariableToDeclare(nullptr), nullptr);

    auto variable = Var("x");
    EXPECT_EQ(analysis.ResolveVariableToDeclare(variable.get()), nullptr);
}

// ---- ClearAnalysisResults / Run -------------------------------------------

TEST(DeclareVariablesTest, ClearAnalysisResultsDropsTheAnalysis)
{
    auto variable = Var("x");
    auto* block = Block();
    block->Statements().Add(Stmt(Use(variable)));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    EXPECT_NO_THROW(analysis.GetDeclarationPoint(*variable));

    analysis.ClearAnalysisResults();
    EXPECT_THROW(analysis.GetDeclarationPoint(*variable), std::out_of_range);
    EXPECT_THROW(analysis.WasMerged(*variable), std::out_of_range);
}

TEST(DeclareVariablesTest, DefaultInitializationKind)
{
    auto none = Var("none");
    auto skipInit = Var("skip");
    skipInit->UsesInitialValue = true;
    auto defaultValue = Var("default");
    defaultValue->UsesInitialValue = true;
    defaultValue->InitialValueIsInitialized = true;

    auto* block = Block();
    block->Statements().Add(Stmt(Use(defaultValue)));
    block->Statements().Add(Stmt(Use(skipInit)));
    block->Statements().Add(Stmt(Use(none)));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);

    ASSERT_NE(analysis.ResolveVariableToDeclare(defaultValue.get()), nullptr);
    EXPECT_EQ(analysis.ResolveVariableToDeclare(defaultValue.get())->DefaultInitialization,
              Transforms::DeclareVariables::VariableInitKind::NeedsDefaultValue);
    EXPECT_EQ(analysis.ResolveVariableToDeclare(skipInit.get())->DefaultInitialization,
              Transforms::DeclareVariables::VariableInitKind::NeedsSkipInit);
    EXPECT_EQ(analysis.ResolveVariableToDeclare(none.get())->DefaultInitialization,
              Transforms::DeclareVariables::VariableInitKind::None);
}

// ---- IsMatchingAssignment / for-initializer special case -------------------

TEST(DeclareVariablesTest, GetDeclarationPointThrowsForUnknownVariable)
{
    auto variable = Var("x");
    Transforms::DeclareVariables analysis;
    EXPECT_THROW(analysis.GetDeclarationPoint(*variable), std::out_of_range);
    EXPECT_THROW(analysis.WasMerged(*variable), std::out_of_range);
}

// ---- Mutation-phase helpers -------------------------------------------------

TEST(DeclareVariablesTest, CombineDeclarationAndInitializerFollowsSetting)
{
    TransformFixture fixture;
    auto variable = Var("x");
    Syntax::IdentifierExpression firstUse("x");
    Transforms::DeclareVariables::VariableToDeclare v(
        variable.get(), Transforms::DeclareVariables::InsertionPoint{0, nullptr}, &firstUse, 0);
    auto context = fixture.MakeContext();

    // The default (SeparateLocalVariableDeclarations == false) combines.
    EXPECT_FALSE(fixture.settings.SeparateLocalVariableDeclarations());
    EXPECT_TRUE(Transforms::DeclareVariables::CombineDeclarationAndInitializer(v, context));

    fixture.settings.SetSeparateLocalVariableDeclarations(true);
    EXPECT_FALSE(Transforms::DeclareVariables::CombineDeclarationAndInitializer(v, context));
}

TEST(DeclareVariablesTest, CombineDeclarationAndInitializerTrueForByRefLike)
{
    TransformFixture fixture;
    fixture.settings.SetSeparateLocalVariableDeclarations(true);
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(TS::UnknownType()));
    variable->Name = "x";
    Syntax::IdentifierExpression firstUse("x");
    Transforms::DeclareVariables::VariableToDeclare v(
        variable.get(), Transforms::DeclareVariables::InsertionPoint{0, nullptr}, &firstUse, 0);
    auto context = fixture.MakeContext();

    EXPECT_TRUE(Transforms::DeclareVariables::CombineDeclarationAndInitializer(v, context));
}

TEST(DeclareVariablesTest, CombineDeclarationAndInitializerTrueForForInitializer)
{
    TransformFixture fixture;
    fixture.settings.SetSeparateLocalVariableDeclarations(true);
    auto variable = Var("x");
    auto* statement = Stmt(Use(variable));
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Initializers().Add(statement);

    Transforms::DeclareVariables::VariableToDeclare v(
        variable.get(), Transforms::DeclareVariables::InsertionPoint{0, statement},
        static_cast<Syntax::IdentifierExpression*>(statement->Expression()), 0);
    auto context = fixture.MakeContext();

    EXPECT_TRUE(Transforms::DeclareVariables::CombineDeclarationAndInitializer(v, context));
}

TEST(DeclareVariablesTest, CanBeDeclaredAsOutVariablePositive)
{
    TransformFixture fixture;
    auto variable = Var("x");
    auto* firstUse = Use(variable);
    auto* invocation = new Syntax::InvocationExpression();
    invocation->Arguments().Add(
        new Syntax::DirectionExpression(Syntax::FieldDirection::Out, firstUse));
    auto* statement = Stmt(invocation);
    auto* block = Block();
    block->Statements().Add(statement);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    auto* v = analysis.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->InsertionPoint.nextNode, static_cast<Syntax::AstNode*>(statement));

    auto context = fixture.MakeContext();
    Syntax::DirectionExpression* dirExpr = nullptr;
    EXPECT_TRUE(analysis.CanBeDeclaredAsOutVariable(*v, dirExpr, context));
    ASSERT_NE(dirExpr, nullptr);
    EXPECT_EQ(dirExpr->FieldDirection(), Syntax::FieldDirection::Out);
}

TEST(DeclareVariablesTest, CanBeDeclaredAsOutVariableKeeps)
{
    TransformFixture fixture;
    auto variable = Var("x");
    auto* firstUse = Use(variable);
    auto* invocation = new Syntax::InvocationExpression();
    invocation->Arguments().Add(
        new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, firstUse));
    auto* statement = Stmt(invocation);
    auto* block = Block();
    block->Statements().Add(statement);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    auto* v = analysis.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v, nullptr);
    auto context = fixture.MakeContext();
    Syntax::DirectionExpression* dirExpr = nullptr;
    // A `ref` direction is not an out variable.
    EXPECT_FALSE(analysis.CanBeDeclaredAsOutVariable(*v, dirExpr, context));

    // The OutVariables setting gates the promotion.
    fixture.settings.SetOutVariables(false);
    auto* firstUse2 = Use(variable);
    auto* invocation2 = new Syntax::InvocationExpression();
    invocation2->Arguments().Add(
        new Syntax::DirectionExpression(Syntax::FieldDirection::Out, firstUse2));
    auto* block2 = Block();
    block2->Statements().Add(Stmt(invocation2));
    Transforms::DeclareVariables analysis2;
    analysis2.Analyze(*block2);
    auto* v2 = analysis2.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v2, nullptr);
    EXPECT_FALSE(analysis2.CanBeDeclaredAsOutVariable(*v2, dirExpr, context));
}

TEST(DeclareVariablesTest, CanBeDeclaredAsOutVariableFalseWhenInitializationNeeded)
{
    TransformFixture fixture;
    auto variable = Var("x");
    variable->UsesInitialValue = true;
    auto* firstUse = Use(variable);
    auto* invocation = new Syntax::InvocationExpression();
    invocation->Arguments().Add(
        new Syntax::DirectionExpression(Syntax::FieldDirection::Out, firstUse));
    auto* statement = Stmt(invocation);
    auto* block = Block();
    block->Statements().Add(statement);

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    auto* v = analysis.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v, nullptr);
    EXPECT_NE(v->DefaultInitialization, Transforms::DeclareVariables::VariableInitKind::None);

    auto context = fixture.MakeContext();
    Syntax::DirectionExpression* dirExpr = nullptr;
    EXPECT_FALSE(analysis.CanBeDeclaredAsOutVariable(*v, dirExpr, context));
}

TEST(DeclareVariablesTest, IsReferencedWithinDeclaringCallDetectsSiblingUse)
{
    TransformFixture fixture;
    auto variable = Var("x");
    auto* firstUse = Use(variable);
    auto* invocation = new Syntax::InvocationExpression();
    auto* dirExpr = new Syntax::DirectionExpression(Syntax::FieldDirection::Out, firstUse);
    invocation->Arguments().Add(dirExpr);
    invocation->Arguments().Add(Use(variable));
    auto* block = Block();
    block->Statements().Add(Stmt(invocation));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    auto* v = analysis.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(analysis.IsReferencedWithinDeclaringCall(*dirExpr, *v));
}

TEST(DeclareVariablesTest, IsReferencedWithinDeclaringCallKeepsForUnrelatedUse)
{
    TransformFixture fixture;
    auto variable = Var("x");
    auto* firstUse = Use(variable);
    auto* invocation = new Syntax::InvocationExpression();
    auto* dirExpr = new Syntax::DirectionExpression(Syntax::FieldDirection::Out, firstUse);
    invocation->Arguments().Add(dirExpr);
    // A different ILVariable in the sibling argument does not reference `v`.
    invocation->Arguments().Add(Use(Var("y")));
    auto* block = Block();
    block->Statements().Add(Stmt(invocation));

    Transforms::DeclareVariables analysis;
    analysis.Analyze(*block);
    auto* v = analysis.ResolveVariableToDeclare(variable.get());
    ASSERT_NE(v, nullptr);
    EXPECT_FALSE(analysis.IsReferencedWithinDeclaringCall(*dirExpr, *v));

    // No parent call at all.
    Syntax::DirectionExpression lone(Syntax::FieldDirection::Out, Id("x"));
    EXPECT_FALSE(analysis.IsReferencedWithinDeclaringCall(lone, *v));
}
