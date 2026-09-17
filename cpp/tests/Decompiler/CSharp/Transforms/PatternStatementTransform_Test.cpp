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

// Tests for the `PatternStatementTransform` skeleton and the two purely-structural
// sub-transforms landed so far: the conditional-logic reassociation (`a && (b && c)` ->
// `(a && b) && c`) and the negated-equality rewrite (`!(a == b)` -> `a != b`). The
// pattern-based sub-transforms are not ported yet, so this suite drives the structure the
// `VisitChildren` replace-and-revisit walk depends on. The transform rewrites the tree
// structurally, so the tests assert node identity and shape rather than rendered text.

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the NormalizeBlockStatements suite shape).
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

Syntax::Expression* Ref(const char* name) {
    return new Syntax::IdentifierExpression(name);
}

Syntax::BinaryOperatorExpression* Bin(Syntax::Expression* left, Syntax::BinaryOperatorType op,
                                      Syntax::Expression* right) {
    return new Syntax::BinaryOperatorExpression(left, op, right);
}

// Runs the transform over a fresh block holding one expression statement and returns the
// (possibly replaced) statement expression afterward.
Syntax::Expression* RunOnExpression(TransformFixture& fixture, Syntax::Expression* expression) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    auto* statement = new Syntax::ExpressionStatement(expression);
    block->Statements().Add(statement);
    Transforms::PatternStatementTransform transform;
    transform.Run(*block, context);
    return statement->Expression();
}

} // namespace

// `!(a == b)` becomes `a != b`: the unary is replaced by the inner binary with the operator
// flipped to inequality.
TEST(PatternStatementTransformTest, RewritesNegatedEquality)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* equality = Bin(a, Syntax::BinaryOperatorType::Equality, b);
    auto* negated = new Syntax::UnaryOperatorExpression(equality, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary, equality);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::InEquality);
    EXPECT_EQ(binary->Left(), a);
    EXPECT_EQ(binary->Right(), b);
}

// `!(a != b)` is left alone (the inner operator is not equality).
TEST(PatternStatementTransformTest, KeepsNegatedInequality)
{
    TransformFixture fixture;
    auto* inequality = Bin(Ref("a"), Syntax::BinaryOperatorType::InEquality, Ref("b"));
    auto* negated = new Syntax::UnaryOperatorExpression(inequality, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(result);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary, negated);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(unary->Expression(), inequality);
}

// `!(a < b)` is left alone (the inner operator is a relational operator).
TEST(PatternStatementTransformTest, KeepsNegatedRelational)
{
    TransformFixture fixture;
    auto* lessThan = Bin(Ref("a"), Syntax::BinaryOperatorType::LessThan, Ref("b"));
    auto* negated = new Syntax::UnaryOperatorExpression(lessThan, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    ASSERT_NE(dynamic_cast<Syntax::UnaryOperatorExpression*>(result), nullptr);
    EXPECT_EQ(result, negated);
}

// A non-negated equality is not touched.
TEST(PatternStatementTransformTest, KeepsPlainEquality)
{
    TransformFixture fixture;
    auto* equality = Bin(Ref("a"), Syntax::BinaryOperatorType::Equality, Ref("b"));

    Syntax::Expression* result = RunOnExpression(fixture, equality);

    EXPECT_EQ(result, equality);
    EXPECT_EQ(equality->Operator(), Syntax::BinaryOperatorType::Equality);
}

// `a && (b && c)` becomes `(a && b) && c`: the right operand becomes the parent and the
// visited expression its left operand.
TEST(PatternStatementTransformTest, ReassociatesConditionalAnd)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* inner = Bin(b, Syntax::BinaryOperatorType::ConditionalAnd, c);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalAnd, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, inner);
    ASSERT_NE(top->Left(), nullptr);
    ASSERT_NE(top->Right(), nullptr);
    EXPECT_EQ(top->Right(), c);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, outer);
    EXPECT_EQ(left->Left(), a);
    EXPECT_EQ(left->Right(), b);
}

// `a || (b || c)` becomes `(a || b) || c` the same way.
TEST(PatternStatementTransformTest, ReassociatesConditionalOr)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* inner = Bin(b, Syntax::BinaryOperatorType::ConditionalOr, c);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalOr, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, inner);
    EXPECT_EQ(top->Right(), c);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, outer);
    EXPECT_EQ(left->Left(), a);
    EXPECT_EQ(left->Right(), b);
}

// A mixed pair (`a && (b || c)`) is not associative, so it stays as-is.
TEST(PatternStatementTransformTest, KeepsMixedConditionalOperators)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("b"), Syntax::BinaryOperatorType::ConditionalOr, Ref("c"));
    auto* outer = Bin(Ref("a"), Syntax::BinaryOperatorType::ConditionalAnd, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Right(), inner);
}

// A non-conditional binary operator with a nested conditional right operand is not
// reassociated.
TEST(PatternStatementTransformTest, KeepsNonConditionalWithNestedConditional)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("b"), Syntax::BinaryOperatorType::ConditionalAnd, Ref("c"));
    auto* outer = Bin(Ref("a"), Syntax::BinaryOperatorType::LessThan, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Right(), inner);
}

// The `VisitChildren` walk descends: a negated equality nested as the left operand of a
// conditional-and is rewritten while the outer node is left in place.
TEST(PatternStatementTransformTest, RewritesNestedNegatedEquality)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* equality = Bin(a, Syntax::BinaryOperatorType::Equality, b);
    auto* negated = new Syntax::UnaryOperatorExpression(equality, Syntax::UnaryOperatorType::Not);
    auto* outer = Bin(negated, Syntax::BinaryOperatorType::ConditionalAnd, Ref("c"));

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(outer->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, equality);
    EXPECT_EQ(left->Operator(), Syntax::BinaryOperatorType::InEquality);
}

// The `VisitChildren` replace-and-revisit walk keeps revisiting a replaced node until the
// visit returns the same node: `a && (b && (c && d))` is flattened fully left-associative to
// `((a && b) && c) && d`.
TEST(PatternStatementTransformTest, FlattensFullyLeftAssociative)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* d = Ref("d");
    auto* cAndD = Bin(c, Syntax::BinaryOperatorType::ConditionalAnd, d);
    auto* bAndCAndD = Bin(b, Syntax::BinaryOperatorType::ConditionalAnd, cAndD);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalAnd, bAndCAndD);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    // Final shape: ((a && b) && c) && d -- the original nodes are reused at each level.
    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, cAndD);
    EXPECT_EQ(top->Right(), d);
    auto* second = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, bAndCAndD);
    EXPECT_EQ(second->Right(), c);
    auto* third = dynamic_cast<Syntax::BinaryOperatorExpression*>(second->Left());
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third, outer);
    EXPECT_EQ(third->Left(), a);
    EXPECT_EQ(third->Right(), b);
}

// A tree with no matching shape is left structurally untouched (every node identity is
// preserved by the walk).
TEST(PatternStatementTransformTest, LeavesUnrelatedTreeUntouched)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("a"), Syntax::BinaryOperatorType::Add, Ref("b"));
    auto* outer = Bin(inner, Syntax::BinaryOperatorType::Multiply, Ref("c"));

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Left(), inner);
    EXPECT_EQ(inner->Operator(), Syntax::BinaryOperatorType::Add);
}
