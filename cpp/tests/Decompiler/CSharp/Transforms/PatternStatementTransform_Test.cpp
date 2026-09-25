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

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
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
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;

// The fixture: the compilation + the using scope the DecompileRun requires (the
// NormalizeBlockStatements_Test pattern).
struct PatternStatementFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;

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
void RunTransform(Syntax::AstNode& root, const PatternStatementFixture& fx) {
    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
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
