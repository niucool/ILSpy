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

// Tests for `PrettifyAssignments` -- the compound-assignment rewrite slice (`x = x op y`
// becomes `x op= y`). The transform rewrites the tree structurally, so the tests assert
// operator enum values and node identity rather than rendered text. The
// operator-mapping table is pinned directly, and the side-effect-free left-hand-side
// classification is exercised through member accesses, indexers, and pointer dereferences.

#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the PatternStatementTransform suite shape).
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

Syntax::IdentifierExpression* Ref(const char* name) {
    return new Syntax::IdentifierExpression(name);
}

Syntax::BinaryOperatorExpression* Bin(Syntax::Expression* left, Syntax::BinaryOperatorType op,
                                      Syntax::Expression* right) {
    return new Syntax::BinaryOperatorExpression(left, op, right);
}

Syntax::AssignmentExpression* Assign(Syntax::Expression* left, Syntax::Expression* right) {
    return new Syntax::AssignmentExpression(left, right);
}

// Runs the transform over a fresh block holding one expression statement wrapping the
// assignment, and returns the assignment afterward (the transform never replaces it).
Syntax::AssignmentExpression* RunOnAssignment(TransformFixture& fixture,
                                              Syntax::AssignmentExpression* assignment) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(assignment));
    Transforms::PrettifyAssignments transform;
    transform.Run(*block, context);
    return assignment;
}

// `x = x + y` becomes `x += y`: the operator is set to the compound form and the binary's
// right operand is hoisted onto the assignment (the binary node is emptied of it).
Syntax::AssignmentExpression* RewriteSimple(Syntax::BinaryOperatorType op,
                                            Syntax::AssignmentOperatorType expected) {
    TransformFixture fixture;
    auto* x = Ref("x");
    auto* x2 = Ref("x");
    auto* y = Ref("y");
    auto* binary = Bin(x2, op, y);
    auto* assignment = Assign(x, binary);
    RunOnAssignment(fixture, assignment);
    EXPECT_EQ(assignment->Operator(), expected);
    EXPECT_EQ(assignment->Left(), x);
    EXPECT_EQ(assignment->Right(), y);
    EXPECT_EQ(binary->Left(), x2);
    EXPECT_EQ(binary->Right(), nullptr);
    return assignment;
}

} // namespace

// The binary-operator to compound-assignment-operator mapping (the public static table).
TEST(PrettifyAssignmentsTest, MapsEveryCompoundBinaryOperator)
{
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Add),
              Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Subtract),
              Syntax::AssignmentOperatorType::Subtract);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Multiply),
              Syntax::AssignmentOperatorType::Multiply);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Divide),
              Syntax::AssignmentOperatorType::Divide);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Modulus),
              Syntax::AssignmentOperatorType::Modulus);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::ShiftLeft),
              Syntax::AssignmentOperatorType::ShiftLeft);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::ShiftRight),
              Syntax::AssignmentOperatorType::ShiftRight);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::UnsignedShiftRight),
              Syntax::AssignmentOperatorType::UnsignedShiftRight);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::BitwiseAnd),
              Syntax::AssignmentOperatorType::BitwiseAnd);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::BitwiseOr),
              Syntax::AssignmentOperatorType::BitwiseOr);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::ExclusiveOr),
              Syntax::AssignmentOperatorType::ExclusiveOr);
}

// Non-compound binary operators map to the plain `Assign` sentinel (the `default` arm).
TEST(PrettifyAssignmentsTest, MapsNonCompoundOperatorsToAssign)
{
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Equality),
              Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::ConditionalAnd),
              Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::NullCoalescing),
              Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(Transforms::PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                  Syntax::BinaryOperatorType::Any),
              Syntax::AssignmentOperatorType::Assign);
}

// `x = x + y` -> `x += y`.
TEST(PrettifyAssignmentsTest, RewritesAdd)
{
    RewriteSimple(Syntax::BinaryOperatorType::Add, Syntax::AssignmentOperatorType::Add);
}

// `x = x - y` -> `x -= y`.
TEST(PrettifyAssignmentsTest, RewritesSubtract)
{
    RewriteSimple(Syntax::BinaryOperatorType::Subtract, Syntax::AssignmentOperatorType::Subtract);
}

// `x = x * y` -> `x *= y`.
TEST(PrettifyAssignmentsTest, RewritesMultiply)
{
    RewriteSimple(Syntax::BinaryOperatorType::Multiply, Syntax::AssignmentOperatorType::Multiply);
}

// `x = x & y` -> `x &= y` (a bitwise compound form).
TEST(PrettifyAssignmentsTest, RewritesBitwiseAnd)
{
    RewriteSimple(Syntax::BinaryOperatorType::BitwiseAnd, Syntax::AssignmentOperatorType::BitwiseAnd);
}

// `this.x = this.x + y` -> `this.x += y` (a member access with a side-effect-free target).
TEST(PrettifyAssignmentsTest, RewritesMemberAccessWithSideEffectFreeTarget)
{
    TransformFixture fixture;
    auto* target = new Syntax::ThisReferenceExpression();
    auto* target2 = new Syntax::ThisReferenceExpression();
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::MemberReferenceExpression(target2, "x"),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::MemberReferenceExpression(target, "x");
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Left(), left);
    EXPECT_EQ(assignment->Right(), y);
}

// `f().x = f().x + y` is left alone: the member-access target has side effects, so reusing
// it in a compound assignment would evaluate the call twice.
TEST(PrettifyAssignmentsTest, KeepsMemberAccessWithSideEffectTarget)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::MemberReferenceExpression(
                           new Syntax::InvocationExpression(Ref("f")), "x"),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::MemberReferenceExpression(
        new Syntax::InvocationExpression(Ref("f")), "x");
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// `base.x = base.x + y` -> `base.x += y` (a base reference is side-effect free).
TEST(PrettifyAssignmentsTest, RewritesBaseReferenceTarget)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::MemberReferenceExpression(
                           new Syntax::BaseReferenceExpression(), "x"),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::MemberReferenceExpression(
        new Syntax::BaseReferenceExpression(), "x");
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Right(), y);
}

// `T.x = T.x + y` -> `T.x += y` (a type reference is side-effect free).
TEST(PrettifyAssignmentsTest, RewritesTypeReferenceTarget)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::MemberReferenceExpression(
                           new Syntax::TypeReferenceExpression(new Syntax::PrimitiveType("int")),
                           "x"),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::MemberReferenceExpression(
        new Syntax::TypeReferenceExpression(new Syntax::PrimitiveType("int")), "x");
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Right(), y);
}

// `a[i] = a[i] + y` -> `a[i] += y` (an indexer with side-effect-free target and argument).
TEST(PrettifyAssignmentsTest, RewritesIndexerWithSideEffectFreeParts)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binaryTarget = Ref("a");
    auto* binary = new Syntax::BinaryOperatorExpression();
    auto* binaryIndexer = new Syntax::IndexerExpression(binaryTarget);
    binaryIndexer->Arguments().Add(Ref("i"));
    binary->Left(binaryIndexer);
    binary->Operator(Syntax::BinaryOperatorType::Add);
    binary->Right(y);
    auto* leftIndexer = new Syntax::IndexerExpression(Ref("a"));
    leftIndexer->Arguments().Add(Ref("i"));
    auto* assignment = Assign(leftIndexer, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Left(), leftIndexer);
    EXPECT_EQ(assignment->Right(), y);
}

// `a[f()] = a[f()] + y` is left alone: the indexer's argument has side effects.
TEST(PrettifyAssignmentsTest, KeepsIndexerWithSideEffectArgument)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binaryIndexer = new Syntax::IndexerExpression(Ref("a"));
    binaryIndexer->Arguments().Add(new Syntax::InvocationExpression(Ref("f")));
    auto* binary = Bin(binaryIndexer, Syntax::BinaryOperatorType::Add, y);
    auto* leftIndexer = new Syntax::IndexerExpression(Ref("a"));
    leftIndexer->Arguments().Add(new Syntax::InvocationExpression(Ref("f")));
    auto* assignment = Assign(leftIndexer, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// `*p = *p + y` -> `*p += y` (a pointer dereference of a side-effect-free operand).
TEST(PrettifyAssignmentsTest, RewritesDereferenceWithSideEffectFreeOperand)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::UnaryOperatorExpression(
                           Ref("p"), Syntax::UnaryOperatorType::Dereference),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::UnaryOperatorExpression(
        Ref("p"), Syntax::UnaryOperatorType::Dereference);
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Right(), y);
}

// `*f() = *f() + y` is left alone: the dereference operand has side effects.
TEST(PrettifyAssignmentsTest, KeepsDereferenceWithSideEffectOperand)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* binary = Bin(new Syntax::UnaryOperatorExpression(
                           new Syntax::InvocationExpression(Ref("f")),
                           Syntax::UnaryOperatorType::Dereference),
                       Syntax::BinaryOperatorType::Add, y);
    auto* left = new Syntax::UnaryOperatorExpression(
        new Syntax::InvocationExpression(Ref("f")), Syntax::UnaryOperatorType::Dereference);
    auto* assignment = Assign(left, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// `x += x + y` is left alone: the assignment is already compound, so the rewrite only runs
// for the plain `=` operator.
TEST(PrettifyAssignmentsTest, KeepsCompoundAssignment)
{
    TransformFixture fixture;
    auto* binary = Bin(Ref("x"), Syntax::BinaryOperatorType::Add, Ref("y"));
    auto* assignment = new Syntax::AssignmentExpression(
        Ref("x"), Syntax::AssignmentOperatorType::Add, binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(assignment->Right(), binary);
}

// `x = y` is left alone: the right-hand side is not a binary operator expression.
TEST(PrettifyAssignmentsTest, KeepsNonBinaryRightHandSide)
{
    TransformFixture fixture;
    auto* y = Ref("y");
    auto* assignment = Assign(Ref("x"), y);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), y);
}

// `x = z + y` is left alone: the left-hand side does not match the binary's left operand.
TEST(PrettifyAssignmentsTest, KeepsWhenLeftDoesNotMatchBinaryLeft)
{
    TransformFixture fixture;
    auto* binary = Bin(Ref("z"), Syntax::BinaryOperatorType::Add, Ref("y"));
    auto* assignment = Assign(Ref("x"), binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// `x = x == y` is left alone: equality has no compound-assignment form.
TEST(PrettifyAssignmentsTest, KeepsEqualityRightHandSide)
{
    TransformFixture fixture;
    auto* binary = Bin(Ref("x"), Syntax::BinaryOperatorType::Equality, Ref("y"));
    auto* assignment = Assign(Ref("x"), binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// `x = x + null` is left alone: there is no right operand to hoist.
TEST(PrettifyAssignmentsTest, KeepsBinaryWithNullRight)
{
    TransformFixture fixture;
    auto* binary = Bin(Ref("x"), Syntax::BinaryOperatorType::Add, nullptr);
    auto* assignment = Assign(Ref("x"), binary);

    RunOnAssignment(fixture, assignment);

    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    EXPECT_EQ(assignment->Right(), binary);
}

// A block with two statements: both compound rewrites run during the same walk (proving the
// visitor reaches every expression statement, not just the first).
TEST(PrettifyAssignmentsTest, RewritesEveryStatementInBlock)
{
    TransformFixture fixture;
    auto* first = Assign(Ref("x"), Bin(Ref("x"), Syntax::BinaryOperatorType::Add, Ref("y")));
    auto* second = Assign(Ref("p"), Bin(Ref("p"), Syntax::BinaryOperatorType::Multiply, Ref("q")));
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(first));
    block->Statements().Add(new Syntax::ExpressionStatement(second));

    Transforms::PrettifyAssignments transform;
    transform.Run(*block, context);

    EXPECT_EQ(first->Operator(), Syntax::AssignmentOperatorType::Add);
    EXPECT_EQ(second->Operator(), Syntax::AssignmentOperatorType::Multiply);
}
