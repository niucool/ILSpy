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

// Port of the first PatternStatementTransform arms' tests
// (ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs): the
// cascading if-else simplification (`if (a) A else { if (b) B }` collapses the
// else block to a bare `else if`), the conditional-logic reassociation
// (`a && (b && c)` becomes `(a && b) && c`, same for `||`), and the negated
// equality rewrite (`!(a == b)` becomes `a != b`). The deep-nesting case pins
// the ContextTrackingVisitor re-visit loop (a replacement is visited again
// until it stops changing).

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
namespace TSImpl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;

// The fixture: the compilation + the using scope the DecompileRun requires (the
// NormalizeBlockStatements_Test pattern), plus the type renderer the foreach
// arms consume.
struct PatternStatementFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;
    Syntax::TypeSystemAstBuilder astBuilder;

    PatternStatementFixture()
        : scopelessContext(std::make_shared<CSharpTS::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharpTS::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

// A `name` identifier reference (the AnyNode stand-in on the candidate side).
Syntax::IdentifierExpression* Id(const std::string& name) {
    return new Syntax::IdentifierExpression(name);
}

// A bare `name();` call statement (an arbitrary embedded statement).
Syntax::ExpressionStatement* Call(const std::string& name) {
    return new Syntax::ExpressionStatement(Id(name));
}

// `a OP b`.
Syntax::BinaryOperatorExpression* Bin(Syntax::Expression* left,
                                      Syntax::BinaryOperatorType op,
                                      Syntax::Expression* right) {
    return new Syntax::BinaryOperatorExpression(left, op, right);
}

// The identifier name of an expression (`"a"` for an `a` reference).
std::string NameOf(const Syntax::Expression* expression) {
    const auto* identifier = dynamic_cast<const Syntax::IdentifierExpression*>(expression);
    if (identifier == nullptr)
        return "<not-an-identifier>";
    return identifier->Identifier();
}

// Runs the transform over a root node with the fixture's context.
void RunTransform(Syntax::AstNode& root, const PatternStatementFixture& fx,
                  bool forStatementSetting = true,
                  bool forEachStatementSetting = true,
                  const TS::ITypeDefinition* currentTypeDefinition = nullptr,
                  bool useEnhancedUsingSetting = true,
                  bool getterOnlyAutomaticPropertiesSetting = true) {
    DecompilerSettings settings;
    settings.SetForStatement(forStatementSetting);
    settings.SetForEachStatement(forEachStatementSetting);
    settings.SetUseEnhancedUsing(useEnhancedUsingSetting);
    settings.SetGetterOnlyAutomaticProperties(getterOnlyAutomaticPropertiesSetting);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    context.CurrentTypeDefinition = currentTypeDefinition;
    context.TypeSystemAstBuilder = &const_cast<PatternStatementFixture&>(fx).astBuilder;
    CS::Transforms::PatternStatementTransform transform;
    transform.Run(root, context);
}

} // namespace

// ---- The cascading if-else simplification ---------------------------------------------

// `if (c1) A; else { if (c2) B; }` becomes `if (c1) A; else if (c2) B;`: the
// else slot holds the nested if directly (the wrapper block is gone).
TEST(PatternStatementTransformTest, CascadingIfElseIsSimplified)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));
    auto* elseBlock = new Syntax::BlockStatement();
    auto* inner = new Syntax::IfElseStatement(Id("c2"), Call("B"));
    elseBlock->Statements().Add(inner);
    outer->FalseStatement(elseBlock);

    RunTransform(*outer, fx);

    ASSERT_NE(outer->FalseStatement(), nullptr);
    EXPECT_EQ(outer->FalseStatement(), inner)
        << "the else block collapses to the nested if-else";
}

// The nested if keeping its own else branch still simplifies (the
// OptionalNode matches a present FalseStatement).
TEST(PatternStatementTransformTest, CascadingIfElseKeepsNestedElse)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));
    auto* elseBlock = new Syntax::BlockStatement();
    auto* inner = new Syntax::IfElseStatement(Id("c2"), Call("B"));
    auto* innerElse = Call("C");
    inner->FalseStatement(innerElse);
    elseBlock->Statements().Add(inner);
    outer->FalseStatement(elseBlock);

    RunTransform(*outer, fx);

    ASSERT_NE(outer->FalseStatement(), nullptr);
    EXPECT_EQ(outer->FalseStatement(), inner);
    EXPECT_EQ(inner->FalseStatement(), innerElse)
        << "the nested if keeps its own else branch";
}

// An else block with two statements does not match the pattern.
TEST(PatternStatementTransformTest, TwoStatementElseBlockIsNotSimplified)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(Call("B"));
    elseBlock->Statements().Add(
        new Syntax::IfElseStatement(Id("c2"), Call("C")));
    outer->FalseStatement(elseBlock);

    RunTransform(*outer, fx);

    ASSERT_NE(outer->FalseStatement(), nullptr);
    EXPECT_EQ(outer->FalseStatement(), elseBlock)
        << "a two-statement else block keeps its shape";
    EXPECT_EQ(elseBlock->Statements().Count(), 2);
}

// A single-statement else block whose statement is not an if-else does not
// match the pattern.
TEST(PatternStatementTransformTest, NonIfSingleStatementElseBlockIsNotSimplified)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));
    auto* elseBlock = new Syntax::BlockStatement();
    auto* body = Call("B");
    elseBlock->Statements().Add(body);
    outer->FalseStatement(elseBlock);

    RunTransform(*outer, fx);

    ASSERT_NE(outer->FalseStatement(), nullptr);
    EXPECT_EQ(outer->FalseStatement(), elseBlock);
    ASSERT_EQ(elseBlock->Statements().Count(), 1);
    EXPECT_EQ(elseBlock->Statements().At(0), body);
}

// An if without an else has nothing to simplify.
TEST(PatternStatementTransformTest, IfWithoutElseIsNotSimplified)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));

    RunTransform(*outer, fx);

    EXPECT_EQ(outer->FalseStatement(), nullptr);
}

// ---- The conditional-logic reassociation ----------------------------------------------

// `a && (b && c)` becomes `(a && b) && c`.
TEST(PatternStatementTransformTest, ConditionalAndReassociatesLeft)
{
    PatternStatementFixture fx;
    auto* bAndC = Bin(Id("b"), Syntax::BinaryOperatorType::ConditionalAnd, Id("c"));
    auto* whole = Bin(Id("a"), Syntax::BinaryOperatorType::ConditionalAnd, bAndC);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(whole);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->Operator(), Syntax::BinaryOperatorType::ConditionalAnd);
    EXPECT_EQ(NameOf(top->Right()), "c");
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr) << "the left operand is the re-associated pair";
    EXPECT_EQ(left->Operator(), Syntax::BinaryOperatorType::ConditionalAnd);
    EXPECT_EQ(NameOf(left->Left()), "a");
    EXPECT_EQ(NameOf(left->Right()), "b");
}

// `a || (b || c)` becomes `(a || b) || c`.
TEST(PatternStatementTransformTest, ConditionalOrReassociatesLeft)
{
    PatternStatementFixture fx;
    auto* bOrC = Bin(Id("b"), Syntax::BinaryOperatorType::ConditionalOr, Id("c"));
    auto* whole = Bin(Id("a"), Syntax::BinaryOperatorType::ConditionalOr, bOrC);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(whole);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->Operator(), Syntax::BinaryOperatorType::ConditionalOr);
    EXPECT_EQ(NameOf(top->Right()), "c");
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(NameOf(left->Left()), "a");
    EXPECT_EQ(NameOf(left->Right()), "b");
}

// `a && (b || c)` keeps its shape (the operators differ).
TEST(PatternStatementTransformTest, MixedConditionalOperatorsAreNotReassociated)
{
    PatternStatementFixture fx;
    auto* bOrC = Bin(Id("b"), Syntax::BinaryOperatorType::ConditionalOr, Id("c"));
    auto* whole = Bin(Id("a"), Syntax::BinaryOperatorType::ConditionalAnd, bOrC);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(whole);

    RunTransform(*statement, fx);

    EXPECT_EQ(statement->Expression(), whole)
        << "a mismatched inner operator is not re-associated";
    EXPECT_EQ(whole->Right(), bOrC);
}

// `a && (b && (c && d))` becomes `((a && b) && c) && d`: the re-visit loop
// keeps visiting the replacement until it stops changing (the first rewrite
// yields `(a && b) && (c && d)`, whose own right operand is re-associated on
// the second visit).
TEST(PatternStatementTransformTest, DeepConditionalAndReassociatesThroughRevisit)
{
    PatternStatementFixture fx;
    auto* cAndD = Bin(Id("c"), Syntax::BinaryOperatorType::ConditionalAnd, Id("d"));
    auto* bNested = Bin(Id("b"), Syntax::BinaryOperatorType::ConditionalAnd, cAndD);
    auto* whole = Bin(Id("a"), Syntax::BinaryOperatorType::ConditionalAnd, bNested);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(whole);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->Operator(), Syntax::BinaryOperatorType::ConditionalAnd);
    EXPECT_EQ(NameOf(top->Right()), "d");
    auto* mid = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(mid, nullptr) << "((a && b) && c) sits under the top node";
    EXPECT_EQ(NameOf(mid->Right()), "c");
    auto* inner = dynamic_cast<Syntax::BinaryOperatorExpression*>(mid->Left());
    ASSERT_NE(inner, nullptr) << "(a && b) sits under the middle node";
    EXPECT_EQ(NameOf(inner->Left()), "a");
    EXPECT_EQ(NameOf(inner->Right()), "b");
}

// ---- The negated-equality rewrite -----------------------------------------------------

// `!(a == b)` becomes `a != b`.
TEST(PatternStatementTransformTest, NegatedEqualityBecomesInequality)
{
    PatternStatementFixture fx;
    auto* equality = Bin(Id("a"), Syntax::BinaryOperatorType::Equality, Id("b"));
    auto* negation = new Syntax::UnaryOperatorExpression(
        equality, Syntax::UnaryOperatorType::Not);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(negation);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr) << "the unary wrapper is replaced by the comparison";
    EXPECT_EQ(top, equality) << "the replacement is the detached inner node";
    EXPECT_EQ(top->Operator(), Syntax::BinaryOperatorType::InEquality);
    EXPECT_EQ(NameOf(top->Left()), "a");
    EXPECT_EQ(NameOf(top->Right()), "b");
}

// `!(a != b)` keeps its shape (only equality is flipped).
TEST(PatternStatementTransformTest, NegatedInequalityIsNotRewritten)
{
    PatternStatementFixture fx;
    auto* inequality = Bin(Id("a"), Syntax::BinaryOperatorType::InEquality, Id("b"));
    auto* negation = new Syntax::UnaryOperatorExpression(
        inequality, Syntax::UnaryOperatorType::Not);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(negation);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::UnaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, negation);
    EXPECT_EQ(top->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(inequality->Operator(), Syntax::BinaryOperatorType::InEquality);
}

// `!(a < b)` keeps its shape (a relational operator is not equality).
TEST(PatternStatementTransformTest, NegatedRelationalIsNotRewritten)
{
    PatternStatementFixture fx;
    auto* less = Bin(Id("a"), Syntax::BinaryOperatorType::LessThan, Id("b"));
    auto* negation = new Syntax::UnaryOperatorExpression(
        less, Syntax::UnaryOperatorType::Not);
    auto statement = std::make_unique<Syntax::ExpressionStatement>(negation);

    RunTransform(*statement, fx);

    auto* top = dynamic_cast<Syntax::UnaryOperatorExpression*>(statement->Expression());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, negation);
    EXPECT_EQ(less->Operator(), Syntax::BinaryOperatorType::LessThan);
}

// ---- The for-loop reshape ------------------------------------------------------------------

// An `name` identifier carrying the variable annotation (the variable identity the
// reshape's checks compare through GetILVariable).
Syntax::IdentifierExpression* Var(const std::string& name, const IL::ILVariablePtr& variable) {
    auto* expression = new Syntax::IdentifierExpression(name);
    expression->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(variable));
    return expression;
}

// `v = <init>; while (v < n) { <body...>; v = v + 1; }` -- the canonical convertible shape
// (the variable, the condition's left operand, and the iterator's left/right operands all
// carry the same ILVariable annotation).
Syntax::BlockStatement* MakeWhileOverVariable(const IL::ILVariablePtr& v,
                                             Syntax::Statement* extraBody) {
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("zero"))));
    auto* iterator = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          new Syntax::BinaryOperatorExpression(
                                              Var("v", v), Syntax::BinaryOperatorType::Add,
                                              Id("one"))));
    auto* body = new Syntax::BlockStatement();
    if (extraBody != nullptr)
        body->Statements().Add(extraBody);
    body->Statements().Add(iterator);
    auto* loop = new Syntax::WhileStatement(
        new Syntax::BinaryOperatorExpression(Var("v", v),
                                              Syntax::BinaryOperatorType::LessThan,
                                              Id("n")),
        body);
    block->Statements().Add(loop);
    return block;
}

// `v = 0; while (v < n) { work; v = v + 1; }` becomes
// `for (v = 0; v < n; v = v + 1) { work; }`: the declaration moves into the
// initializers, the condition and iterator detach into the for statement, and
// the body keeps the matched statements in a fresh block.
TEST(PatternStatementTransformTest, WhileLoopBecomesFor)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    Syntax::BlockStatement* block = MakeWhileOverVariable(v, Call("Work"));
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);
    Syntax::Statement* declaration = block->Statements().At(0);
    Syntax::WhileStatement* loop =
        dynamic_cast<Syntax::WhileStatement*>(block->Statements().At(1));
    ASSERT_NE(loop, nullptr);
    Syntax::Expression* condition = loop->Condition();
    auto* body = dynamic_cast<Syntax::BlockStatement*>(loop->EmbeddedStatement());
    ASSERT_NE(body, nullptr);
    Syntax::Statement* work = body->Statements().At(0);
    Syntax::Statement* iterator = body->Statements().At(1);
    // The annotation-copy channel is pinned on the replacement node.
    loop->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(v));

    RunTransform(*root, fx);

    ASSERT_EQ(block->Statements().Count(), 1) << "the declaration is absorbed";
    auto* forStatement = dynamic_cast<Syntax::ForStatement*>(block->Statements().At(0));
    ASSERT_NE(forStatement, nullptr) << "the while is replaced by a for";
    ASSERT_EQ(forStatement->Initializers().Count(), 1);
    EXPECT_EQ(forStatement->Initializers().At(0), declaration);
    EXPECT_EQ(forStatement->Condition(), condition)
        << "the condition detaches into the for statement";
    ASSERT_EQ(forStatement->Iterators().Count(), 1);
    EXPECT_EQ(forStatement->Iterators().At(0), iterator);
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    EXPECT_NE(newBody, body) << "the body block is fresh (the iterator is stripped)";
    ASSERT_EQ(newBody->Statements().Count(), 1);
    EXPECT_EQ(newBody->Statements().At(0), work);
    EXPECT_NE(forStatement->Annotation<CS::ILVariableResolveResult>(), nullptr)
        << "CopyAnnotationsFrom carries the loop's annotations";
}

// The reshape requires the declared variable and the loop variable to be the
// same ILVariable: a mismatch keeps the while.
TEST(PatternStatementTransformTest, WhileLoopToForRequiresTheSameVariable)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr a = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    IL::ILVariablePtr b = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    // a = 0; while (b < n) { work; b = b + 1; }
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("a", a),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("zero"))));
    auto* iterator = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("b", b),
                                          Syntax::AssignmentOperatorType::Assign,
                                          new Syntax::BinaryOperatorExpression(
                                              Var("b", b), Syntax::BinaryOperatorType::Add,
                                              Id("one"))));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(Call("Work"));
    body->Statements().Add(iterator);
    auto* loop = new Syntax::WhileStatement(
        new Syntax::BinaryOperatorExpression(Var("b", b),
                                              Syntax::BinaryOperatorType::LessThan,
                                              Id("n")),
        body);
    block->Statements().Add(loop);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(1), loop)
        << "a variable mismatch keeps the while loop";
}

// A `continue` in the loop body blocks the reshape (continue jumps to the
// condition in a while but to the iterator in a for).
TEST(PatternStatementTransformTest, ContinueInBodyBlocksTheFor)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    auto* continueIf = new Syntax::IfElseStatement(
        Id("c"), new Syntax::ContinueStatement());
    Syntax::BlockStatement* block = MakeWhileOverVariable(v, continueIf);
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);
    Syntax::WhileStatement* loop =
        dynamic_cast<Syntax::WhileStatement*>(block->Statements().At(1));
    ASSERT_NE(loop, nullptr);

    RunTransform(*root, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(1), loop)
        << "a continue in the body keeps the while loop";
}

// A `continue` inside a NESTED loop does not block the reshape (it targets the
// nested loop, not the outer one).
TEST(PatternStatementTransformTest, ContinueInNestedLoopDoesNotBlockTheFor)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    auto* nested = new Syntax::WhileStatement(
        Id("m"), new Syntax::ContinueStatement());
    Syntax::BlockStatement* block = MakeWhileOverVariable(
        v, new Syntax::ExpressionStatement(Id("work")));
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);
    // Put the nested loop in the body before the iterator.
    auto* body = dynamic_cast<Syntax::BlockStatement*>(
        dynamic_cast<Syntax::WhileStatement*>(block->Statements().At(1))
            ->EmbeddedStatement());
    ASSERT_NE(body, nullptr);
    body->Statements().InsertAfter(nullptr, nested);

    RunTransform(*root, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* forStatement = dynamic_cast<Syntax::ForStatement*>(block->Statements().At(0));
    ASSERT_NE(forStatement, nullptr)
        << "a continue inside a nested loop still converts";
    ASSERT_EQ(forStatement->Initializers().Count(), 1);
    ASSERT_EQ(forStatement->Iterators().Count(), 1);
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    ASSERT_EQ(newBody->Statements().Count(), 2);
    EXPECT_EQ(newBody->Statements().At(0), nested);
}

// A by-ref-like iteration variable used after the loop blocks the reshape (the
// hoisted declaration would leave a headless for whose only initialization is
// the for-initializer ref-assignment, which cannot be split from a ref local).
TEST(PatternStatementTransformTest, RefLocalUsedAfterLoopStaysWhile)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(TS::UnknownType()));
    Syntax::BlockStatement* block = MakeWhileOverVariable(v, Call("Work"));
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);
    // use(v); -- after the loop.
    block->Statements().Add(
        new Syntax::ExpressionStatement(Var("v", v)));
    Syntax::WhileStatement* loop =
        dynamic_cast<Syntax::WhileStatement*>(block->Statements().At(1));
    ASSERT_NE(loop, nullptr);

    RunTransform(*root, fx);

    ASSERT_EQ(block->Statements().Count(), 3);
    EXPECT_EQ(block->Statements().At(1), loop)
        << "a ref local used after the loop keeps the while loop";
}

// A by-ref-like iteration variable NOT used after the loop still converts.
TEST(PatternStatementTransformTest, RefLocalNotUsedAfterConverts)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(TS::UnknownType()));
    Syntax::BlockStatement* block = MakeWhileOverVariable(v, Call("Work"));
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);

    RunTransform(*root, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* forStatement = dynamic_cast<Syntax::ForStatement*>(block->Statements().At(0));
    ASSERT_NE(forStatement, nullptr)
        << "a ref local not used after the loop still converts";
}

// A variable used in the iterator part whose declaration point sits INSIDE
// the loop body blocks the reshape: hoisting the initializer into the for's
// iterator slot would move the use out of the body scope that declares it.
// (x is declared and used only inside the body, and the iterator reads x.)
TEST(PatternStatementTransformTest, IteratorVariableDeclaredInsideBodyKeepsWhile)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    IL::ILVariablePtr x = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    x->Name = "x";
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("zero"))));
    // body: x = one; work(x); v = v + x;   -- the iterator reads x, whose
    // uses are all inside the body.
    auto* iterator = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var("v", v), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Var("v", v), Syntax::BinaryOperatorType::Add, Var("x", x))));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("x", x),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("one"))));
    body->Statements().Add(new Syntax::ExpressionStatement(Var("x", x)));
    body->Statements().Add(iterator);
    auto* loop = new Syntax::WhileStatement(
        new Syntax::BinaryOperatorExpression(Var("v", v),
                                              Syntax::BinaryOperatorType::LessThan,
                                              Id("n")),
        body);
    block->Statements().Add(loop);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(1), loop)
        << "an iterator variable declared inside the body keeps the while loop";
}

// `v = 0;` immediately before an existing for statement that uses `v` in its
// condition or iterators moves the declaration into the for's initializers.
TEST(PatternStatementTransformTest, DeclarationMergesIntoForInitializer)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* declaration = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("zero")));
    block->Statements().Add(declaration);
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var("v", v), Syntax::BinaryOperatorType::LessThan, Id("n")));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("one"))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());
    block->Statements().Add(forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), forStatement);
    ASSERT_EQ(forStatement->Initializers().Count(), 1);
    EXPECT_EQ(forStatement->Initializers().At(0), declaration)
        << "the declaration moves into the for initializer slot";
}

// The initializer merge requires the for statement to use the declared
// variable: a mismatch keeps the two statements apart (and the for-pattern
// half does not match a for statement).
TEST(PatternStatementTransformTest, ForInitializerMergeRequiresVariableUse)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr a = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    IL::ILVariablePtr b = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* declaration = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("a", a),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("zero")));
    block->Statements().Add(declaration);
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var("b", b), Syntax::BinaryOperatorType::LessThan, Id("n")));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("b", b),
                                          Syntax::AssignmentOperatorType::Assign,
                                          Id("one"))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());
    block->Statements().Add(forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(0), declaration);
    EXPECT_EQ(block->Statements().At(1), forStatement);
    EXPECT_EQ(forStatement->Initializers().Count(), 0);
}

// The ForStatement setting gates the whole arm.
TEST(PatternStatementTransformTest, ForSettingGatesTheReshape)
{
    PatternStatementFixture fx;
    IL::ILVariablePtr v = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType());
    Syntax::BlockStatement* block = MakeWhileOverVariable(v, Call("Work"));
    auto root = std::unique_ptr<Syntax::BlockStatement>(block);
    Syntax::WhileStatement* loop =
        dynamic_cast<Syntax::WhileStatement*>(block->Statements().At(1));
    ASSERT_NE(loop, nullptr);

    RunTransform(*root, fx, /*forStatementSetting=*/false);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(1), loop)
        << "the ForStatement setting turns the arm off";
}

// ---- The foreach-over-array reshape --------------------------------------------------------

// A `name`-named local of the given type.
IL::ILVariablePtr LocalOf(const std::string& name, TS::ITypePtr type) {
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                     std::move(type));
    variable->Name = name;
    return variable;
}

// The convertible foreach-over-array shape: `for (i = 0; i < array.Length;
// i = i + 1) { item = array[i]; work; }` with the three ILVariables
// annotated and the use counts the arm checks (the C# IL pipeline maintains
// them; the test sets them directly).
struct ForeachArrayLoop {
    IL::ILVariablePtr index;
    IL::ILVariablePtr array_;
    IL::ILVariablePtr item;
    Syntax::ForStatement* forStatement = nullptr;
    Syntax::Statement* work = nullptr;
    Syntax::IdentifierExpression* arrayIdentifier = nullptr;
};

ForeachArrayLoop MakeForeachArrayLoop(TS::ITypePtr arrayType = nullptr) {
    ForeachArrayLoop loop;
    loop.index = LocalOf("i", TS::UnknownType());
    loop.index->StoreCount = 2;
    loop.index->LoadCount = 3;
    loop.array_ =
        LocalOf("array",
               arrayType != nullptr
                   ? std::move(arrayType)
                   : TS::ITypePtr(std::make_shared<TS::KnownType>(
                         TS::KnownTypeCode::String)));
    loop.item = LocalOf("item", TS::ITypePtr(std::make_shared<TS::KnownType>(
                                     TS::KnownTypeCode::Int32)));
    // The single store is the `item = array[i]` assignment (IsSingleDefinition).
    loop.item->StoreCount = 1;

    loop.forStatement = new Syntax::ForStatement();
    loop.forStatement->Initializers().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var("i", loop.index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(0)))));
    loop.arrayIdentifier = Var("array", loop.array_);
    loop.forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var("i", loop.index), Syntax::BinaryOperatorType::LessThan,
        new Syntax::MemberReferenceExpression(loop.arrayIdentifier, "Length")));
    loop.forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var("i", loop.index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Var("i", loop.index), Syntax::BinaryOperatorType::Add,
                new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(1))))));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var("item", loop.item), Syntax::AssignmentOperatorType::Assign,
            [&]() {
                auto* elementAccess =
                    new Syntax::IndexerExpression(Var("array", loop.array_));
                elementAccess->Arguments().Add(Var("i", loop.index));
                return elementAccess;
            }())));
    loop.work = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("x"),
            Syntax::AssignmentOperatorType::Assign,
            new Syntax::IdentifierExpression("y")));
    body->Statements().Add(loop.work);
    loop.forStatement->EmbeddedStatement(body);
    return loop;
}

// `for (i = 0; i < array.Length; i = i + 1) { item = array[i]; work; }`
// becomes `foreach (int item in array) { work; }`.
TEST(PatternStatementTransformTest, ForeachOnArrayIsIntroduced)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop();
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* foreachStmt =
        dynamic_cast<Syntax::ForeachStatement*>(block->Statements().At(0));
    ASSERT_NE(foreachStmt, nullptr) << "the for loop becomes a foreach";
    ASSERT_NE(foreachStmt->VariableType(), nullptr);
    auto* designation = dynamic_cast<Syntax::SingleVariableDesignation*>(
        foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    const auto* designationAnnotation =
        designation->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(designationAnnotation, nullptr);
    EXPECT_EQ(designationAnnotation->Variable(), loop.item.get());
    EXPECT_EQ(foreachStmt->InExpression(), loop.arrayIdentifier)
        << "the in-expression is the array variable's identifier";
    auto* body = dynamic_cast<Syntax::BlockStatement*>(
        foreachStmt->EmbeddedStatement());
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->Statements().Count(), 1);
    EXPECT_EQ(body->Statements().At(0), loop.work);
    EXPECT_EQ(loop.item->Kind, IL::VariableKind::ForeachLocal)
        << "the item variable is re-kinded as the foreach local";
}

// The in-expression must be an array (or a string): a differently-typed
// collection variable keeps the for loop.
TEST(PatternStatementTransformTest, ForeachOnArrayRequiresArrayOrString)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop(
        TS::ITypePtr(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)));
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), loop.forStatement)
        << "a non-array non-string collection keeps the for loop";
}

// The item variable must be single-definition (assignable to the foreach
// designation): a second store keeps the for loop.
TEST(PatternStatementTransformTest, ForeachOnArrayRequiresSingleDefinitionItem)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop();
    loop.item->StoreCount = 2;
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), loop.forStatement);
}

// The index must be a pure counter (2 stores, 3 loads, no addresses).
TEST(PatternStatementTransformTest, ForeachOnArrayRequiresTheIndexCounts)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop();
    loop.index->StoreCount = 1;
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), loop.forStatement);
}

// The ForEachStatement setting gates the arm.
TEST(PatternStatementTransformTest, ForeachSettingGatesTheArrayArm)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop();
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx, /*forStatementSetting=*/true,
                 /*forEachStatementSetting=*/false);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), loop.forStatement)
        << "the ForEachStatement setting turns the arm off";
}

// A merged item variable cannot become the foreach local (the merged
// declaration covers a wider scope).
TEST(PatternStatementTransformTest, ForeachOnArrayKeepsMergedItemVariable)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachArrayLoop();
    // Another `item`-named variable used before the loop: the collision
    // resolution merges the two into one outer declaration (the same type:
    // same-named colliding variables share their type by the name-assignment
    // invariant the C# ResolveCollisions asserts).
    IL::ILVariablePtr outerItem =
        LocalOf("item", TS::ITypePtr(std::make_shared<TS::KnownType>(
                              TS::KnownTypeCode::Int32)));
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("x"),
            Syntax::AssignmentOperatorType::Assign,
            Var("item", outerItem))));
    block->Statements().Add(loop.forStatement);

    RunTransform(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(1), loop.forStatement)
        << "a merged item variable keeps the for loop";
}

// ---- The foreach-over-multi-dim-array reshape ----------------------------------------------

// A `$result = $collection.$methodName($index);` bound-call statement
// (the GetUpperBound/GetLowerBound shapes).
Syntax::ExpressionStatement* MakeBoundCall(
    const std::string& methodName, int index, const IL::ILVariablePtr& result,
    const IL::ILVariablePtr& collection,
    Syntax::IdentifierExpression** collectionOut = nullptr) {
    auto* collectionIdentifier = Var(collection->Name, collection);
    if (collectionOut != nullptr)
        *collectionOut = collectionIdentifier;
    auto* call = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(collectionIdentifier, methodName));
    call->Arguments().Add(
        new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(index)));
    return new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Var(result->Name, result), Syntax::AssignmentOperatorType::Assign, call));
}

// A multi-dim for round: `for (; $index <= $upperBound; $index = $index + 1)`
// (empty initializers -- the lower-bound assignment precedes the loop).
Syntax::ForStatement* MakeMultiDimFor(const IL::ILVariablePtr& index,
                                      const IL::ILVariablePtr& upperBound) {
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var(index->Name, index), Syntax::BinaryOperatorType::LessThanOrEqual,
        Var(upperBound->Name, upperBound)));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var(index->Name, index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Var(index->Name, index), Syntax::BinaryOperatorType::Add,
                new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(1))))));
    return forStatement;
}

// The convertible 2-d foreach-over-multi-dimensional-array shape:
//   ub0 = arr.GetUpperBound(0); ub1 = arr.GetUpperBound(1);
//   lb0 = arr.GetLowerBound(0);
//   for (; i0 <= ub0; i0 = i0 + 1) {
//       lb1 = arr.GetLowerBound(1);
//       for (; i1 <= ub1; i1 = i1 + 1) { item = arr[i0, i1]; work; }
//   }
struct ForeachMultiDimLoop {
    IL::ILVariablePtr collection;
    IL::ILVariablePtr item;
    IL::ILVariablePtr lower1;
    Syntax::ExpressionStatement* entry = nullptr;
    Syntax::Statement* work = nullptr;
    Syntax::IdentifierExpression* collectionInUpper1 = nullptr;
    std::unique_ptr<Syntax::BlockStatement> block;
};

ForeachMultiDimLoop MakeForeachMultiDimLoop(TS::ITypePtr collectionType = nullptr) {
    ForeachMultiDimLoop loop;
    auto boundVar = [](const std::string& name) {
        auto v = LocalOf(name, TS::UnknownType());
        v->StoreCount = 1;
        v->LoadCount = 1;
        return v;
    };
    auto lowerVar = [](const std::string& name) {
        auto v = LocalOf(name, TS::UnknownType());
        v->StoreCount = 2;
        v->LoadCount = 3;
        return v;
    };
    loop.collection = LocalOf(
        "arr", collectionType != nullptr
                     ? std::move(collectionType)
                     : TS::ITypePtr(std::make_shared<TS::ArrayType>(
                           TS::ITypePtr(std::make_shared<TS::KnownType>(
                               TS::KnownTypeCode::Int32)),
                           2)));
    IL::ILVariablePtr upper0 = boundVar("ub0");
    IL::ILVariablePtr upper1 = boundVar("ub1");
    IL::ILVariablePtr lower0 = lowerVar("lb0");
    IL::ILVariablePtr lower1 = lowerVar("lb1");
    IL::ILVariablePtr index0 = LocalOf("i0", TS::UnknownType());
    IL::ILVariablePtr index1 = LocalOf("i1", TS::UnknownType());
    loop.item = LocalOf("item", TS::ITypePtr(std::make_shared<TS::KnownType>(
                                     TS::KnownTypeCode::Int32)));
    loop.item->StoreCount = 1;

    loop.entry = MakeBoundCall("GetUpperBound", 0, upper0, loop.collection);
    auto* upper1Statement =
        MakeBoundCall("GetUpperBound", 1, upper1, loop.collection,
                      &loop.collectionInUpper1);
    auto* lower0Statement =
        MakeBoundCall("GetLowerBound", 0, lower0, loop.collection);

    auto* outerFor = MakeMultiDimFor(index0, upper0);
    auto* outerBody = new Syntax::BlockStatement();
    outerBody->Statements().Add(
        MakeBoundCall("GetLowerBound", 1, lower1, loop.collection));
    auto* innerFor = MakeMultiDimFor(index1, upper1);
    auto* innerBody = new Syntax::BlockStatement();
    auto* elementAccess = new Syntax::IndexerExpression(
        Var(loop.collection->Name, loop.collection));
    elementAccess->Arguments().Add(Var(index0->Name, index0));
    elementAccess->Arguments().Add(Var(index1->Name, index1));
    innerBody->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var(loop.item->Name, loop.item),
            Syntax::AssignmentOperatorType::Assign, elementAccess)));
    loop.work = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("x"),
            Syntax::AssignmentOperatorType::Assign,
            new Syntax::IdentifierExpression("y")));
    innerBody->Statements().Add(loop.work);
    innerFor->EmbeddedStatement(innerBody);
    outerBody->Statements().Add(innerFor);
    outerFor->EmbeddedStatement(outerBody);

    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(loop.entry);
    block->Statements().Add(upper1Statement);
    block->Statements().Add(lower0Statement);
    block->Statements().Add(outerFor);
    loop.lower1 = lower1;
    loop.block = std::move(block);
    return loop;
}

// The shape above becomes `foreach (int item in arr) { work; }`: the
// upper-bound chain, the first lower-bound assignment, and the whole nested
// for-loop structure are absorbed (the entry statement is replaced).
TEST(PatternStatementTransformTest, ForeachOnMultiDimArrayIsIntroduced)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachMultiDimLoop();

    RunTransform(*loop.block, fx);

    ASSERT_EQ(loop.block->Statements().Count(), 1)
        << "the upper-bound chain, the lower-bound assignment, and the nested"
        " fors are absorbed";
    auto* foreachStmt =
        dynamic_cast<Syntax::ForeachStatement*>(loop.block->Statements().At(0));
    ASSERT_NE(foreachStmt, nullptr);
    ASSERT_NE(foreachStmt->VariableType(), nullptr);
    auto* designation = dynamic_cast<Syntax::SingleVariableDesignation*>(
        foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    const auto* designationAnnotation =
        designation->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(designationAnnotation, nullptr);
    EXPECT_EQ(designationAnnotation->Variable(), loop.item.get());
    EXPECT_EQ(foreachStmt->InExpression(), loop.collectionInUpper1)
        << "the in-expression is the collection identifier of the last"
        " upper-bound match";
    auto* body = dynamic_cast<Syntax::BlockStatement*>(
        foreachStmt->EmbeddedStatement());
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->Statements().Count(), 1);
    EXPECT_EQ(body->Statements().At(0), loop.work);
    EXPECT_EQ(loop.item->Kind, IL::VariableKind::ForeachLocal);
}

// The collection must be an array type.
TEST(PatternStatementTransformTest, ForeachOnMultiDimRequiresArrayType)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachMultiDimLoop(
        TS::ITypePtr(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)));

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 4)
        << "a non-array collection keeps the loop structure";
    EXPECT_EQ(loop.block->Statements().At(0), loop.entry);
}

// The lower-bound variables must have the pure-counter counts (2 stores,
// 3 loads, no addresses).
TEST(PatternStatementTransformTest, ForeachOnMultiDimRequiresLowerBoundCounts)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachMultiDimLoop();
    loop.lower1->StoreCount = 1;

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 4)
        << "an impure lower-bound variable keeps the loop structure";
    EXPECT_EQ(loop.block->Statements().At(0), loop.entry);
}

// The upper-bound variables must be single-definition single-load.
TEST(PatternStatementTransformTest, ForeachOnMultiDimRequiresUpperBoundCounts)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachMultiDimLoop();
    // Give the entry's upper-bound variable a second load.
    auto* entryAssignment = dynamic_cast<Syntax::AssignmentExpression*>(
        loop.entry->Expression());
    ASSERT_NE(entryAssignment, nullptr);
    IL::ILVariable* upper0 =
        CS::GetILVariable(*dynamic_cast<Syntax::IdentifierExpression*>(
            entryAssignment->Left()));
    ASSERT_NE(upper0, nullptr);
    upper0->LoadCount = 2;

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 4)
        << "an impure upper-bound variable keeps the loop structure";
    EXPECT_EQ(loop.block->Statements().At(0), loop.entry);
}

// ---- The foreach-over-inline-array reshape -----------------------------------------------

// The `[InlineArray(N)]` attribute stub (the InlineArrayTransform_Test
// InlineArrayTestAttribute shape).
class PatternInlineArrayAttribute : public TS::IAttribute {
public:
    explicit PatternInlineArrayAttribute(int length) : length_(length) {}
    const TS::IType& AttributeType() const override { return attrType_; }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
        return {TS::CustomAttributeTypedArgument(
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
            std::any(length_))};
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
        return {};
    }

private:
    int length_;
    TS::KnownType attrType_{TS::KnownTypeCode::Object};
};

// An `[InlineArray(N)]` struct type definition (the InlineArrayTypeDefinition
// precedent in InlineArrayTransform_Test.cpp): a LookupTypeDefinition whose
// GetAttribute surfaces the InlineArray attribute.
class PatternInlineArrayType : public TS::TestSupport::LookupTypeDefinition {
public:
    PatternInlineArrayType(int length, const TS::ICompilation& compilation)
        : TS::TestSupport::LookupTypeDefinition(
              "Buffer8", std::string(), TS::FullTypeName("Buffer8"),
              TS::TypeKind::Struct, TS::Accessibility::Public, compilation,
              nullptr),
          attr_(length) {}

    using LookupTypeDefinition::GetAttribute;
    const TS::IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        if (attribute == TS::KnownAttribute::InlineArray)
            return &attr_;
        return nullptr;
    }

private:
    mutable PatternInlineArrayAttribute attr_;
};

// The convertible foreach-over-inline-array shape:
//   for (i = 0; i < 8; i = i + 1) {
//       item = <PID>.InlineArrayElementRef(ref buffer, i);
//       work;
//   }
// The loop bound must equal the buffer's `[InlineArray(N)]` length (the
// soundness argument: InlineArrayElementRef is unchecked, the C# indexer is
// bounds-checked).
struct ForeachInlineArrayLoop {
    IL::ILVariablePtr item;
    Syntax::ForStatement* forStatement = nullptr;
    Syntax::Statement* work = nullptr;
    std::unique_ptr<Syntax::BlockStatement> block;
};

ForeachInlineArrayLoop MakeForeachInlineArrayLoop(
    PatternStatementFixture& fx, const std::string& helperName =
                                       "InlineArrayElementRef",
    int loopBound = 8, int indexStoreCount = 2, int indexLoadCount = 3,
    bool attachSymbol = true) {
    ForeachInlineArrayLoop loop;
    auto bufferType = std::make_shared<PatternInlineArrayType>(8, fx.compilation);
    IL::ILVariablePtr buffer = LocalOf("buffer", bufferType);
    IL::ILVariablePtr index = LocalOf("i", TS::UnknownType());
    index->StoreCount = indexStoreCount;
    index->LoadCount = indexLoadCount;
    loop.item = LocalOf("item", TS::ITypePtr(std::make_shared<TS::KnownType>(
                                   TS::KnownTypeCode::Int32)));
    loop.item->StoreCount = 1;

    auto* forStatement = new Syntax::ForStatement();
    forStatement->Initializers().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var(index->Name, index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(0)))));
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var(index->Name, index), Syntax::BinaryOperatorType::LessThan,
        new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(loopBound))));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var(index->Name, index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Var(index->Name, index), Syntax::BinaryOperatorType::Add,
                new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(1))))));

    // `item = InlineArrayElementRef(ref buffer, i);` -- the compiler's
    // unchecked element accessor, resolved through the symbol annotation.
    auto* helperCall = new Syntax::InvocationExpression(
        new Syntax::IdentifierExpression(helperName));
    auto* bufferRef = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Ref, Var(buffer->Name, buffer));
    helperCall->Arguments().Add(bufferRef);
    helperCall->Arguments().Add(Var(index->Name, index));
    if (attachSymbol) {
        auto helperMethod = std::make_shared<TSImpl::FakeMethod>(
            fx.compilation, TS::SymbolKind::Method);
        helperMethod->SetName(helperName);
        helperMethod->SetDeclaringType(std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName(std::string(),
                                 "<PrivateImplementationDetails>")));
        helperMethod->SetReturnType(TS::ITypePtr(std::make_shared<TS::KnownType>(
            TS::KnownTypeCode::Int32)));
        auto target = std::make_shared<Sem::TypeResolveResult>(
            helperMethod->DeclaringType());
        helperCall->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            target,
            // FakeMethod derives IMember twice (FakeMember and IMethod);
            // pick the FakeMember subobject's IMember base.
            static_cast<TSImpl::FakeMember*>(helperMethod.get())));
        // Keep the stub method alive for the program's lifetime (the
        // resolve-result annotation stores a raw pointer).
        static std::vector<std::shared_ptr<TSImpl::FakeMethod>> keepAlive;
        keepAlive.push_back(std::move(helperMethod));
    }
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Var(loop.item->Name, loop.item),
            Syntax::AssignmentOperatorType::Assign, helperCall)));
    loop.work = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("x"),
            Syntax::AssignmentOperatorType::Assign,
            new Syntax::IdentifierExpression("y")));
    body->Statements().Add(loop.work);
    forStatement->EmbeddedStatement(body);
    loop.forStatement = forStatement;

    loop.block = std::make_unique<Syntax::BlockStatement>();
    loop.block->Statements().Add(forStatement);
    return loop;
}

// The shape above becomes `foreach (int item in buffer) { work; }` -- the
// bounds-checked surface is sound because the loop bound equals the inline
// array length.
TEST(PatternStatementTransformTest, ForeachOnInlineArrayIsIntroduced)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachInlineArrayLoop(fx);

    RunTransform(*loop.block, fx);

    ASSERT_EQ(loop.block->Statements().Count(), 1);
    auto* foreachStmt =
        dynamic_cast<Syntax::ForeachStatement*>(loop.block->Statements().At(0));
    ASSERT_NE(foreachStmt, nullptr);
    auto* designation = dynamic_cast<Syntax::SingleVariableDesignation*>(
        foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    const auto* designationAnnotation =
        designation->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(designationAnnotation, nullptr);
    EXPECT_EQ(designationAnnotation->Variable(), loop.item.get());
    auto* inIdentifier =
        dynamic_cast<Syntax::IdentifierExpression*>(foreachStmt->InExpression());
    ASSERT_NE(inIdentifier, nullptr);
    EXPECT_EQ(inIdentifier->Identifier(), "buffer");
    auto* body = dynamic_cast<Syntax::BlockStatement*>(
        foreachStmt->EmbeddedStatement());
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->Statements().Count(), 1);
    EXPECT_EQ(body->Statements().At(0), loop.work)
        << "the element-access statement is dropped, the work survives";
    EXPECT_EQ(loop.item->Kind, IL::VariableKind::ForeachLocal);
}

// Only the compiler's own InlineArrayElementRef(ReadOnly) helpers qualify.
TEST(PatternStatementTransformTest, ForeachOnInlineArrayRequiresHelperName)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachInlineArrayLoop(fx, /*helperName=*/"SomeOtherHelper");

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 1);
    EXPECT_EQ(loop.block->Statements().At(0), loop.forStatement);
}

// Without a resolved symbol the call keeps the (faithful) helper form.
TEST(PatternStatementTransformTest, ForeachOnInlineArrayRequiresSymbol)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachInlineArrayLoop(fx, "InlineArrayElementRef",
                                           /*loopBound=*/8, /*indexStoreCount=*/2,
                                           /*indexLoadCount=*/3,
                                           /*attachSymbol=*/false);

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 1);
    EXPECT_EQ(loop.block->Statements().At(0), loop.forStatement);
}

// The loop bound must equal the inline array length exactly.
TEST(PatternStatementTransformTest, ForeachOnInlineArrayRequiresLengthMatch)
{
    PatternStatementFixture fx;
    // The buffer's [InlineArray(8)] length stays 8; the loop counts to 7.
    auto loop = MakeForeachInlineArrayLoop(fx, "InlineArrayElementRef",
                                           /*loopBound=*/7);

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 1);
    EXPECT_EQ(loop.block->Statements().At(0), loop.forStatement);
}

// The index must be a pure counter (stored at init + increment, loaded at
// the condition, the increment, and the element access).
TEST(PatternStatementTransformTest, ForeachOnInlineArrayRequiresIndexCounts)
{
    PatternStatementFixture fx;
    auto loop = MakeForeachInlineArrayLoop(fx, "InlineArrayElementRef", 8,
                                           /*indexStoreCount=*/3);

    RunTransform(*loop.block, fx);

    EXPECT_EQ(loop.block->Statements().Count(), 1);
    EXPECT_EQ(loop.block->Statements().At(0), loop.forStatement);
}

// ---- The destructor reshape -------------------------------------------------------------

// The `try { body } finally { base.Finalize(); }` shape the compiler emits
// for a Finalize override.
Syntax::TryCatchStatement* MakeFinalizeBody(Syntax::BlockStatement** bodyOut = nullptr) {
    auto* tryStatement = new Syntax::TryCatchStatement();
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ReturnStatement());
    tryStatement->TryBlock(body);
    auto* finallyBlock = new Syntax::BlockStatement();
    finallyBlock->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::InvocationExpression(new Syntax::MemberReferenceExpression(
            new Syntax::BaseReferenceExpression(), "Finalize"))));
    tryStatement->FinallyBlock(finallyBlock);
    if (bodyOut != nullptr)
        *bodyOut = body;
    return tryStatement;
}

// The C# shape `protected override void Finalize() { try { body } finally
// { base.Finalize(); } }` inside a type declaration becomes the destructor
// `~MyClass() { body }`.
TEST(PatternStatementTransformTest, FinalizeMethodBecomesDestructor)
{
    PatternStatementFixture fx;
    auto typeDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "MyClass", std::string(), TS::FullTypeName("MyClass"),
        TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, nullptr);

    auto* method = new Syntax::MethodDeclaration();
    method->Name("Finalize");
    auto* voidType = new Syntax::PrimitiveType();
    voidType->Keyword("void");
    method->ReturnType(voidType);
    method->Modifiers(Syntax::Modifiers::Protected | Syntax::Modifiers::Override);
    auto* methodBody = new Syntax::BlockStatement();
    Syntax::BlockStatement* work = nullptr;
    methodBody->Statements().Add(MakeFinalizeBody(&work));
    method->Body(methodBody);
    auto* attributeSection = new Syntax::AttributeSection();
    method->Attributes().Add(attributeSection);

    auto type = std::make_unique<Syntax::TypeDeclaration>();
    type->Members().Add(method);
    // The ContextTrackingVisitor reads the enclosing type through the type
    // declaration's symbol annotation.
    type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(typeDef));

    RunTransform(*type, fx, /*forStatementSetting=*/true,
                 /*forEachStatementSetting=*/true, typeDef.get());

    ASSERT_EQ(type->Members().Count(), 1);
    auto* destructor =
        dynamic_cast<Syntax::DestructorDeclaration*>(type->Members().At(0));
    ASSERT_NE(destructor, nullptr) << "the Finalize method becomes a destructor";
    EXPECT_EQ(destructor->Name(), "MyClass");
    EXPECT_EQ(destructor->Body(), work)
        << "the try body moves to the destructor body";
    EXPECT_EQ(destructor->Modifiers(), Syntax::Modifiers::None)
        << "Protected and Override are cleared";
    ASSERT_EQ(destructor->Attributes().Count(), 1)
        << "the method's attributes move to the destructor";
}

// A Finalize method that does not match the try-finally-base call shape
// keeps its method form.
TEST(PatternStatementTransformTest, FinalizeMethodRequiresTheExactShape)
{
    PatternStatementFixture fx;
    auto typeDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "MyClass", std::string(), TS::FullTypeName("MyClass"),
        TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, nullptr);

    auto* method = new Syntax::MethodDeclaration();
    method->Name("Finalize");
    auto* voidType = new Syntax::PrimitiveType();
    voidType->Keyword("void");
    method->ReturnType(voidType);
    method->Modifiers(Syntax::Modifiers::Protected | Syntax::Modifiers::Override);
    auto* methodBody = new Syntax::BlockStatement();
    auto* tryStatement = new Syntax::TryCatchStatement();
    tryStatement->TryBlock(new Syntax::BlockStatement());
    auto* finallyBlock = new Syntax::BlockStatement();
    finallyBlock->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::InvocationExpression(new Syntax::MemberReferenceExpression(
            new Syntax::BaseReferenceExpression(), "Dispose"))));
    tryStatement->FinallyBlock(finallyBlock);
    methodBody->Statements().Add(tryStatement);
    method->Body(methodBody);

    auto type = std::make_unique<Syntax::TypeDeclaration>();
    type->Members().Add(method);
    type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(typeDef));

    RunTransform(*type, fx, true, true, typeDef.get());

    ASSERT_EQ(type->Members().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::MethodDeclaration*>(type->Members().At(0)), nullptr)
        << "a non-matching Finalize keeps the method form";
}

// A destructor carrying the try-finally-base call body is unwrapped to just
// the body.
TEST(PatternStatementTransformTest, DestructorBodyIsSimplified)
{
    PatternStatementFixture fx;
    auto typeDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "MyClass", std::string(), TS::FullTypeName("MyClass"),
        TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, nullptr);

    auto* destructor = new Syntax::DestructorDeclaration();
    destructor->Name("MyClass");
    auto* outerBody = new Syntax::BlockStatement();
    Syntax::BlockStatement* work = nullptr;
    outerBody->Statements().Add(MakeFinalizeBody(&work));
    destructor->Body(outerBody);

    auto type = std::make_unique<Syntax::TypeDeclaration>();
    type->Members().Add(destructor);
    type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(typeDef));

    RunTransform(*type, fx, true, true, typeDef.get());

    EXPECT_EQ(destructor->Body(), work)
        << "the try body unwraps into the destructor body";
}

// ---- The try-catch-finally merge --------------------------------------------------------

// `try { try { work } catch { c } } finally { f }` becomes
// `try { work } catch { c } finally { f }` (the nested try-catch merges into
// the outer try-finally).
TEST(PatternStatementTransformTest, NestedTryCatchFinallyIsMerged)
{
    PatternStatementFixture fx;
    auto* outer = new Syntax::TryCatchStatement();
    auto* outerTryBlock = new Syntax::BlockStatement();
    auto* inner = new Syntax::TryCatchStatement();
    auto* work = new Syntax::BlockStatement();
    work->Statements().Add(new Syntax::ReturnStatement());
    inner->TryBlock(work);
    auto* catchClause = new Syntax::CatchClause();
    inner->CatchClauses().Add(catchClause);
    outerTryBlock->Statements().Add(inner);
    outer->TryBlock(outerTryBlock);
    auto* finallyBlock = new Syntax::BlockStatement();
    outer->FinallyBlock(finallyBlock);

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(outer);

    RunTransform(*root, fx);

    ASSERT_EQ(root->Statements().Count(), 1);
    EXPECT_EQ(root->Statements().At(0), outer);
    EXPECT_EQ(outer->TryBlock(), work)
        << "the inner try's block becomes the outer try's block";
    ASSERT_EQ(outer->CatchClauses().Count(), 1);
    EXPECT_EQ(outer->CatchClauses().At(0), catchClause)
        << "the inner catches move to the outer try";
    EXPECT_EQ(outer->FinallyBlock(), finallyBlock);
}

// A nested try without the outer finally does not merge.
TEST(PatternStatementTransformTest, NestedTryCatchWithoutFinallyIsNotMerged)
{
    PatternStatementFixture fx;
    auto* outer = new Syntax::TryCatchStatement();
    auto* outerTryBlock = new Syntax::BlockStatement();
    auto* inner = new Syntax::TryCatchStatement();
    inner->TryBlock(new Syntax::BlockStatement());
    inner->CatchClauses().Add(new Syntax::CatchClause());
    outerTryBlock->Statements().Add(inner);
    outer->TryBlock(outerTryBlock);

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(outer);

    RunTransform(*root, fx);

    EXPECT_EQ(outer->TryBlock(), outerTryBlock)
        << "without a finally the nested structure stays";
    EXPECT_EQ(outer->CatchClauses().Count(), 0);
}

// ---- The enhanced-using statement -------------------------------------------------------

// A `using (var x = e) { }` statement that is the last statement of its block
// becomes the C# 8.0 using-declaration form (`using var x = e;`).
TEST(PatternStatementTransformTest, EnhancedUsingIsIntroduced)
{
    PatternStatementFixture fx;
    auto* usingStatement = new Syntax::UsingStatement();
    auto* declaration = new Syntax::VariableDeclarationStatement();
    usingStatement->ResourceAcquisition(declaration);
    usingStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(usingStatement);

    RunTransform(*root, fx);

    EXPECT_TRUE(usingStatement->IsEnhanced());
}

// A using statement followed by another statement keeps the statement form.
TEST(PatternStatementTransformTest, EnhancedUsingRequiresLastStatement)
{
    PatternStatementFixture fx;
    auto* usingStatement = new Syntax::UsingStatement();
    usingStatement->ResourceAcquisition(new Syntax::VariableDeclarationStatement());
    usingStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(usingStatement);
    root->Statements().Add(new Syntax::ReturnStatement());

    RunTransform(*root, fx);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}

// A using over an expression (not a variable declaration) keeps the form.
TEST(PatternStatementTransformTest, EnhancedUsingRequiresVariableDeclaration)
{
    PatternStatementFixture fx;
    auto* usingStatement = new Syntax::UsingStatement();
    usingStatement->ResourceAcquisition(new Syntax::IdentifierExpression("x"));
    usingStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(usingStatement);

    RunTransform(*root, fx);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}

// ---- The pattern-based fixed statement --------------------------------------------------

// `fixed (var p = &expr.GetPinnableReference()) { }` over a value-type target
// becomes `fixed (var p = expr) { }` (the C# 7.3 pattern-based fixed).
TEST(PatternStatementTransformTest, PatternBasedFixedIsIntroduced)
{
    PatternStatementFixture fx;
    auto* fixedStatement = new Syntax::FixedStatement();
    auto* variable = new Syntax::VariableInitializer();
    auto* target = new Syntax::IdentifierExpression("buffer");
    // The target resolves to a value type (IsReferenceType false).
    target->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        TS::ITypePtr(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32))));
    auto* call = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(target, "GetPinnableReference"));
    variable->Initializer(new Syntax::UnaryOperatorExpression(
        call, Syntax::UnaryOperatorType::AddressOf));
    fixedStatement->Variables().Add(variable);
    fixedStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(fixedStatement);

    RunTransform(*root, fx);

    EXPECT_EQ(variable->Initializer(), target)
        << "the address-of-GetPinnableReference initializer unwraps to the target";
}

// A reference-type target keeps the GetPinnableReference call (the fixed
// semantics only match for value types).
TEST(PatternStatementTransformTest, PatternBasedFixedRequiresValueType)
{
    PatternStatementFixture fx;
    auto* fixedStatement = new Syntax::FixedStatement();
    auto* variable = new Syntax::VariableInitializer();
    auto* target = new Syntax::IdentifierExpression("buffer");
    // A reference type (a class).
    target->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        TS::ITypePtr(std::make_shared<TS::KnownType>(TS::KnownTypeCode::String))));
    auto* call = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(target, "GetPinnableReference"));
    variable->Initializer(new Syntax::UnaryOperatorExpression(
        call, Syntax::UnaryOperatorType::AddressOf));
    fixedStatement->Variables().Add(variable);
    fixedStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(fixedStatement);

    RunTransform(*root, fx);

    auto* initializer =
        dynamic_cast<Syntax::UnaryOperatorExpression*>(variable->Initializer());
    ASSERT_NE(initializer, nullptr)
        << "a reference-type target keeps the address-of call";
}

// A fixed initializer that is not the GetPinnableReference shape keeps its form.
TEST(PatternStatementTransformTest, PatternBasedFixedRequiresTheHelperShape)
{
    PatternStatementFixture fx;
    auto* fixedStatement = new Syntax::FixedStatement();
    auto* variable = new Syntax::VariableInitializer();
    variable->Initializer(new Syntax::IdentifierExpression("ptr"));
    fixedStatement->Variables().Add(variable);
    fixedStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(fixedStatement);

    RunTransform(*root, fx);

    EXPECT_NE(variable->Initializer(), nullptr)
        << "a non-matching initializer is untouched";
}

// ---- The automatic-property reshape ------------------------------------------------------

// A fake IField with settable classified attributes: the backing-field
// checks read [CompilerGenerated] through IEntity::HasAttribute.
class AutoPropertyTestField : public TSImpl::FakeField {
public:
    explicit AutoPropertyTestField(const TS::ICompilation& compilation)
        : TSImpl::FakeField(compilation) {}
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return known_.find(attribute) != known_.end();
    }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        return HasAttribute(attribute) ? &sentinel_ : nullptr;
    }
    void AddKnownAttribute(TS::KnownAttribute attribute) {
        known_.insert(attribute);
    }
    void ClearKnownAttributes() { known_.clear(); }

private:
    struct SentinelAttribute : TS::IAttribute {
        const TS::IType& AttributeType() const override { return type_; }
        const TS::IMethod* Constructor() const override { return nullptr; }
        bool HasDecodeErrors() const override { return false; }
        std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
            return {};
        }
        std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
            return {};
        }
        TS::KnownType type_{TS::KnownTypeCode::Object};
    };
    SentinelAttribute sentinel_;
    std::set<TS::KnownAttribute> known_;
};

// A fake IMethod with settable classified attributes: the accessor
// checks read [CompilerGenerated] through IEntity::HasAttribute.
class AutoPropertyTestMethod : public TSImpl::FakeMethod {
public:
    AutoPropertyTestMethod(const TS::ICompilation& compilation,
                           TS::SymbolKind symbolKind)
        : TSImpl::FakeMethod(compilation, symbolKind) {}
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return known_.find(attribute) != known_.end();
    }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        return HasAttribute(attribute) ? &sentinel_ : nullptr;
    }
    void AddKnownAttribute(TS::KnownAttribute attribute) {
        known_.insert(attribute);
    }

private:
    struct SentinelAttribute : TS::IAttribute {
        const TS::IType& AttributeType() const override { return type_; }
        const TS::IMethod* Constructor() const override { return nullptr; }
        bool HasDecodeErrors() const override { return false; }
        std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
            return {};
        }
        std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
            return {};
        }
        TS::KnownType type_{TS::KnownTypeCode::Object};
    };
    SentinelAttribute sentinel_;
    std::set<TS::KnownAttribute> known_;
};

// The convertible auto-property shape inside a type declaration:
//   int Count { get { return <Count>k__BackingField; }
//              set { <Count>k__BackingField = value; } }
//   private int <Count>k__BackingField;   // removed, its sections move
struct AutoPropertyRig {
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> typeDef;
    std::shared_ptr<TSImpl::FakeProperty> property;
    std::shared_ptr<AutoPropertyTestField> backingField;
    Syntax::PropertyDeclaration* propertyDeclaration = nullptr;
    Syntax::Accessor* getter = nullptr;
    Syntax::Accessor* setter = nullptr;
    Syntax::AttributeSection* movedSection = nullptr;
    Syntax::Attribute* survivingAttribute = nullptr;
    Syntax::FieldDeclaration* fieldDecl = nullptr;
    std::unique_ptr<Syntax::TypeDeclaration> type;
};

// The [Attr] attribute definition stub (the type the syntax attribute's
// resolve result points at).
TS::ITypePtr MakeAttributeType(const PatternStatementFixture& fx,
                               const std::string& ns, const std::string& name) {
    return std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        name, ns, TS::FullTypeName(ns.empty() ? name : ns + "." + name),
        TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, nullptr);
}

// A syntax [Attr] node whose type resolves to the given definition.
Syntax::Attribute* MakeAttribute(TS::ITypePtr attributeType) {
    auto* simpleType = new Syntax::SimpleType(attributeType->Name());
    simpleType->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(attributeType));
    return new Syntax::Attribute(simpleType);
}

AutoPropertyRig MakeAutoProperty(PatternStatementFixture& fx) {
    AutoPropertyRig rig;
    rig.typeDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "MyClass", std::string(), TS::FullTypeName("MyClass"), TS::TypeKind::Class,
        TS::Accessibility::Public, fx.compilation, nullptr);

    // The property and its accessors (fakes; the accessor checks are skipped
    // because the declaring type carries a compiler-generated _Count field).
    rig.property = std::make_shared<TSImpl::FakeProperty>(fx.compilation);
    rig.property->SetName("Count");
    auto getterMethod = std::make_shared<AutoPropertyTestMethod>(
        fx.compilation, TS::SymbolKind::Method);
    getterMethod->SetName("get_Count");
    getterMethod->AddKnownAttribute(TS::KnownAttribute::CompilerGenerated);
    auto setterMethod = std::make_shared<AutoPropertyTestMethod>(
        fx.compilation, TS::SymbolKind::Method);
    setterMethod->SetName("set_Count");
    setterMethod->AddKnownAttribute(TS::KnownAttribute::CompilerGenerated);
    rig.property->SetGetter(static_cast<const TS::IMethod*>(getterMethod.get()));
    rig.property->SetSetter(static_cast<const TS::IMethod*>(setterMethod.get()));
    rig.property->SetDeclaringType(rig.typeDef);

    // The backing field: <Count>k__BackingField, compiler-generated.
    rig.backingField = std::make_shared<AutoPropertyTestField>(fx.compilation);
    rig.backingField->SetName("<Count>k__BackingField");
    rig.backingField->AddKnownAttribute(TS::KnownAttribute::CompilerGenerated);
    rig.backingField->SetDeclaringType(rig.typeDef);
    rig.backingField->SetReturnType(TS::ITypePtr(std::make_shared<TS::KnownType>(
        TS::KnownTypeCode::Int32)));

    // The declaring type's fields: the VB-style _Count (a compiler-generated
    // field makes the accessor compiler-generated checks unnecessary) and
    // the backing field itself.
    auto countField = std::make_shared<AutoPropertyTestField>(fx.compilation);
    countField->SetName("_Count");
    countField->AddKnownAttribute(TS::KnownAttribute::CompilerGenerated);
    countField->SetDeclaringType(rig.typeDef);
    rig.typeDef->SetFields({countField.get(), rig.backingField.get()});
    rig.typeDef->SetProperties(
        {static_cast<const TS::IProperty*>(rig.property.get())});

    // The property declaration with the get/set bodies over the backing field.
    rig.propertyDeclaration = new Syntax::PropertyDeclaration();
    rig.propertyDeclaration->Name("Count");
    rig.propertyDeclaration->ReturnType(new Syntax::SimpleType("Int32"));
    rig.propertyDeclaration->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.property.get())));

    rig.getter = new Syntax::Accessor();
    auto* getterBody = new Syntax::BlockStatement();
    auto* fieldReference = new Syntax::IdentifierExpression("<Count>k__BackingField");
    fieldReference->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.backingField.get())));
    getterBody->Statements().Add(new Syntax::ReturnStatement(fieldReference));
    rig.getter->Body(getterBody);
    // The accessor's [CompilerGenerated] section is stripped.
    auto* getterSection = new Syntax::AttributeSection();
    getterSection->Attributes().Add(MakeAttribute(MakeAttributeType(
        fx, "System.Runtime.CompilerServices", "CompilerGeneratedAttribute")));
    rig.getter->Attributes().Add(getterSection);
    rig.propertyDeclaration->Getter(rig.getter);

    rig.setter = new Syntax::Accessor();
    auto* setterBody = new Syntax::BlockStatement();
    setterBody->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("<Count>k__BackingField"),
            Syntax::AssignmentOperatorType::Assign,
            new Syntax::IdentifierExpression("value"))));
    rig.setter->Body(setterBody);
    rig.propertyDeclaration->Setter(rig.setter);

    // The backing field declaration: a [CompilerGenerated] plus a surviving
    // custom attribute (its section moves onto the property with the
    // "field" target).
    rig.fieldDecl = new Syntax::FieldDeclaration();
    rig.fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.backingField.get())));
    rig.movedSection = new Syntax::AttributeSection();
    rig.movedSection->Attributes().Add(MakeAttribute(MakeAttributeType(
        fx, "System.Runtime.CompilerServices", "CompilerGeneratedAttribute")));
    rig.survivingAttribute =
        MakeAttribute(MakeAttributeType(fx, "MyNamespace", "MyAttribute"));
    rig.movedSection->Attributes().Add(rig.survivingAttribute);
    rig.fieldDecl->Attributes().Add(rig.movedSection);
    auto* variable = new Syntax::VariableInitializer();
    variable->Name("<Count>k__BackingField");
    rig.fieldDecl->Variables().Add(variable);

    rig.type = std::make_unique<Syntax::TypeDeclaration>();
    rig.type->Members().Add(rig.propertyDeclaration);
    rig.type->Members().Add(rig.fieldDecl);
    rig.type->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(rig.typeDef));

    // Keep the fake members alive for the program's lifetime (the resolve
    // results hold raw pointers).
    static std::vector<std::shared_ptr<void>> keepAlive;
    keepAlive.push_back(getterMethod);
    keepAlive.push_back(setterMethod);
    keepAlive.push_back(countField);
    return rig;
}

// The shape above becomes the auto-property `int Count { get; set; }`; the
// backing field declaration disappears and its surviving attribute
// sections move onto the property with the "field" target.
TEST(PatternStatementTransformTest, AutomaticPropertyIsIntroduced)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);

    RunTransform(*rig.type, fx);

    EXPECT_EQ(rig.getter->Body(), nullptr) << "the getter body is cleared";
    EXPECT_EQ(rig.setter->Body(), nullptr) << "the setter body is cleared";
    EXPECT_EQ(rig.getter->Attributes().Count(), 0)
        << "the accessor's [CompilerGenerated] section is stripped";
    ASSERT_EQ(rig.type->Members().Count(), 1)
        << "the backing field declaration is removed";
    ASSERT_EQ(rig.propertyDeclaration->Attributes().Count(), 1)
        << "the surviving field section moves onto the property";
    EXPECT_EQ(rig.propertyDeclaration->Attributes().At(0), rig.movedSection);
    EXPECT_EQ(rig.propertyDeclaration->Attributes().At(0)->AttributeTarget(), "field");
    EXPECT_EQ(rig.movedSection->Attributes().Count(), 1);
    EXPECT_EQ(rig.movedSection->Attributes().At(0), rig.survivingAttribute);
}

// A property whose backing field is not compiler-generated keeps its
// accessor bodies.
TEST(PatternStatementTransformTest, AutomaticPropertyRequiresCompilerGeneratedField)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);
    rig.backingField->ClearKnownAttributes();

    RunTransform(*rig.type, fx);

    EXPECT_NE(rig.getter->Body(), nullptr)
        << "a non-compiler-generated backing field keeps the accessor bodies";
    EXPECT_EQ(rig.type->Members().Count(), 2)
        << "the field declaration stays";
}

// A backing field whose name is not the compiler's pattern keeps the
// accessor bodies.
TEST(PatternStatementTransformTest, AutomaticPropertyRequiresBackingFieldName)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);
    rig.backingField->SetName("someOtherField");

    RunTransform(*rig.type, fx);

    EXPECT_NE(rig.getter->Body(), nullptr)
        << "a non-backing-field name keeps the accessor bodies";
    EXPECT_EQ(rig.type->Members().Count(), 2);
}

// ---- The backing-field identifier rewrite ----------------------------------------------

// A use of the compiler-generated backing field outside the accessors
// becomes a use of the property (the identifier is replaced and the
// parent's resolve result is re-pointed).
TEST(PatternStatementTransformTest, BackingFieldUsageIsReplacedWithProperty)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);

    auto* use = new Syntax::IdentifierExpression("<Count>k__BackingField");
    use->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.backingField.get())));

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("y"),
            Syntax::AssignmentOperatorType::Assign, use)));

    RunTransform(*root, fx);

    EXPECT_EQ(use->IdentifierToken()->Name(), "Count")
        << "the backing field identifier becomes the property name";
    const auto* resolveResult = use->Annotation<Sem::MemberResolveResult>();
    ASSERT_NE(resolveResult, nullptr);
    EXPECT_EQ(resolveResult->Member(),
              static_cast<const TS::IMember*>(
                  static_cast<const TS::IProperty*>(rig.property.get())))
        << "the parent's resolve result now points at the property";
}

// A getter-only property without the GetterOnlyAutomaticProperties
// setting keeps the backing field use.
TEST(PatternStatementTransformTest, BackingFieldUsageRequiresSetterOrSetting)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);
    rig.property->SetSetter(nullptr);

    auto* use = new Syntax::IdentifierExpression("<Count>k__BackingField");
    use->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.backingField.get())));

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("y"),
            Syntax::AssignmentOperatorType::Assign, use)));

    RunTransform(*root, fx, /*forStatementSetting=*/true,
                 /*forEachStatementSetting=*/true, /*currentTypeDefinition=*/nullptr,
                 /*useEnhancedUsingSetting=*/true,
                 /*getterOnlyAutomaticPropertiesSetting=*/false);

    EXPECT_EQ(use->IdentifierToken()->Name(), "<Count>k__BackingField")
        << "a getter-only property keeps the backing field use";
}

// A field whose name is not the backing-field pattern keeps its use.
TEST(PatternStatementTransformTest, BackingFieldUsageRequiresFieldName)
{
    PatternStatementFixture fx;
    auto rig = MakeAutoProperty(fx);
    rig.backingField->SetName("someOtherField");

    auto* use = new Syntax::IdentifierExpression("someOtherField");
    use->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        std::make_shared<Sem::TypeResolveResult>(rig.typeDef),
        static_cast<TSImpl::FakeMember*>(rig.backingField.get())));

    auto root = std::make_unique<Syntax::BlockStatement>();
    root->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            new Syntax::IdentifierExpression("y"),
            Syntax::AssignmentOperatorType::Assign, use)));

    RunTransform(*root, fx);

    EXPECT_EQ(use->IdentifierToken()->Name(), "someOtherField");
}

// A Run entered while another Run is in flight throws (the C# reentrancy
// guard). The step hook re-enters the transform mid-visit.
TEST(PatternStatementTransformTest, RunIsGuardedAgainstReentrancy)
{
    PatternStatementFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>(Id("c1"), Call("A"));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(
        new Syntax::IfElseStatement(Id("c2"), Call("B")));
    outer->FalseStatement(elseBlock);
    auto second = std::make_unique<Syntax::IfElseStatement>(Id("x"), Call("Y"));

    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::PatternStatementTransform transform;
    context.Step = [&transform, &second, &context](const std::string&, const void*) {
        transform.Run(*second, context);
    };

    EXPECT_THROW(transform.Run(*outer, context), std::logic_error);
}

// The transform occupies the head of GetAstTransforms (the C# list position,
// CSharpDecompiler.cs line 239).
TEST(PatternStatementTransformTest, PipelineHeadIsPatternStatementTransform)
{
    auto transforms = CS::CSharpDecompiler::GetAstTransforms();
    ASSERT_FALSE(transforms.empty());
    EXPECT_NE(dynamic_cast<CS::Transforms::PatternStatementTransform*>(
                  transforms[0].get()),
              nullptr)
        << "PatternStatementTransform is the first AST transform";
}
