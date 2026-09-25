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
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
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
                  bool forEachStatementSetting = true) {
    DecompilerSettings settings;
    settings.SetForStatement(forStatementSetting);
    settings.SetForEachStatement(forEachStatementSetting);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
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

// ---- The Run shell ---------------------------------------------------------------------

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
