// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for PrettifyAssignments (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/PrettifyAssignments.cs): "x = x +
// y" becomes "x += y" when the two operands match and the operator has a
// compound form; "x = x + 1" becomes "x++" (post-increment under an
// expression statement); a non-matching left side and a non-constant right
// side are left alone.

#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The fixture: the compilation + the using scope the DecompileRun ctor
// requires (the CallBuilderDelegateReference fixture chain).
struct PrettifyFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;
    // The compilation-driven Int32 (the minimal KnownType(Int32) has a null
    // GetDefinition, so the primitive-integer checks fail on it).
    TS::ITypePtr int32Type;

    PrettifyFixture()
        : scopelessContext(std::make_shared<CSharpTS::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharpTS::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          int32Type(TS::ITypePtr(
              std::shared_ptr<TS::IType>(),
              const_cast<TS::IType*>(
                  &compilation.FindType(TS::KnownTypeCode::Int32)))) {
    }
};

// Build `x = x + <constant>` (the annotated shape the builder produces: each
// identifier carries a TypeResolveResult; the right side a constant resolve
// result over Int32).
std::unique_ptr<Syntax::ExpressionStatement> MakeIncrementStatement(
    PrettifyFixture& fx, const std::string& name) {
    auto x = new Syntax::IdentifierExpression(name);
    x->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(fx.int32Type));
    auto lhs = new Syntax::IdentifierExpression(name);
    lhs->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(fx.int32Type));
    auto rhs = new Syntax::PrimitiveExpression(std::int32_t(1));
    // The C# `rr.ConstantValue` reads the right side's annotation; a constant
    // Int32 1.
    rhs->AddAnnotation(
        std::make_shared<Sem::ConstantResolveResult>(fx.int32Type, std::any(1)));
    auto binary = new Syntax::BinaryOperatorExpression(
        lhs, Syntax::BinaryOperatorType::Add, rhs);
    auto assignment = new Syntax::AssignmentExpression(
        x, Syntax::AssignmentOperatorType::Assign, binary);
    return std::make_unique<Syntax::ExpressionStatement>(assignment);
}

} // namespace

// The full compound fold: `x = x + 1` becomes `x++` under an expression
// statement (the compound step then the increment step both apply; the
// observable result is the unary form).
TEST(PrettifyAssignmentsTest, AddBecomesCompoundThenIncrement)
{
    PrettifyFixture fx;
    auto statement = MakeIncrementStatement(fx, "x");
    DecompilerSettings settings;
    CS::Transforms::TransformContext context;
    DecompileRun runStorage(&settings, fx.usingScope);
    context.DecompileRun = &runStorage;
    int steps = 0;
    context.Step = [&steps](const std::string&, const void*) { steps++; };
    CS::Transforms::PrettifyAssignments transform;
    transform.Run(*statement, context);

    // The statement's expression is now a post-increment unary operator.
    const Syntax::AstNode* expr = statement->Expression();
    const auto* unary = dynamic_cast<const Syntax::UnaryOperatorExpression*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::PostIncrement);
    EXPECT_GE(steps, 1);
}

// A non-constant right side aborts the increment fold (the assignment stays a
// compound assignment or plain assignment).
TEST(PrettifyAssignmentsTest, NonConstantRightSideIsLeftAlone)
{
    PrettifyFixture fx;
    auto statement = MakeIncrementStatement(fx, "x");
    // Replace the constant right side with an unannotated identifier (no
    // constant resolve result).
    Syntax::AssignmentExpression* assignment =
        static_cast<Syntax::AssignmentExpression*>(statement->Expression());
    Syntax::BinaryOperatorExpression* binary =
        static_cast<Syntax::BinaryOperatorExpression*>(assignment->Right());
    auto other = new Syntax::IdentifierExpression("y");
    binary->Right(other);

    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::PrettifyAssignments transform;
    transform.Run(*statement, context);

    // The compound fold may still have applied (x on both sides matches), but
    // the increment fold has not: the statement still holds an assignment
    // expression, not a unary increment.
    const Syntax::AstNode* expr = statement->Expression();
    EXPECT_EQ(dynamic_cast<const Syntax::UnaryOperatorExpression*>(expr), nullptr);
}

// The operator mapping: every arithmetic binary operator maps to its
// assignment twin (the C# public static).
TEST(PrettifyAssignmentsTest, OperatorMappingCoversTheArithmeticFamily)
{
    using BO = Syntax::BinaryOperatorType;
    using AO = Syntax::AssignmentOperatorType;
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::Add),
              AO::Add);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::Subtract),
              AO::Subtract);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::Multiply),
              AO::Multiply);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::Divide),
              AO::Divide);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::Modulus),
              AO::Modulus);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::ShiftLeft),
              AO::ShiftLeft);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::ShiftRight),
              AO::ShiftRight);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::UnsignedShiftRight),
              AO::UnsignedShiftRight);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::BitwiseAnd),
              AO::BitwiseAnd);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::BitwiseOr),
              AO::BitwiseOr);
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::ExclusiveOr),
              AO::ExclusiveOr);
    // The relational operators have no compound form (the C# default arm).
    EXPECT_EQ(CS::Transforms::PrettifyAssignments::
                  GetAssignmentOperatorForBinaryOperator(BO::LessThan),
              AO::Assign);
}