// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// The ExpressionBuilder VisitNumericCompoundAssign suite: the operator dispatch
// matrix (eight arithmetic/bitwise operators through HandleCompoundAssignment, the
// two shifts through HandleCompoundShift with the ShiftRight sign/small-integer
// gates), the EvaluatesToOldValue postfix render, the Address/LdObj target kind,
// the pointer-offset value detection (both the fold and the failure-comment
// shapes), the checked/unchecked annotation tail, and the resolver-driven shift
// resolve result -- all over the MinimalCorlib fixture shape of the sibling
// suites.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/PointerTypeReference.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;

// The default settings bag (the C# `new DecompilerSettings()`).
DecompilerSettings DefaultSettings()
{
    return DecompilerSettings{};
}

// The BuilderFixture (the ExpressionBuilderSkeleton_Test shape).
struct BuilderFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    BuilderFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(DefaultSettings()), run(&settings, usingScope)
    {
    }

    // The root using scope over the compilation's global namespace.
    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    ExpressionBuilder MakeBuilder()
    {
        return ExpressionBuilder(nullptr, compilation, FixtureContext(), &function_,
                                 &settings, &run);
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local,
            std::const_pointer_cast<TS::IType>(
                compilation.FindType(code).shared_from_this()));
        local->Name = name;
        return local;
    }

    // A local over an arbitrary type pointer (the pointer-typed fixture arm).
    std::shared_ptr<IL::ILVariable> MakeLocalOverType(TS::ITypePtr type, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                      std::move(type));
        local->Name = name;
        return local;
    }

private:
    IL::ILFunction function_;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;

    const CSharp::TypeSystem::CSharpTypeResolveContext& FixtureContext()
    {
        if (!context_)
        {
            context_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
                compilation.MainModule(), usingScope);
        }
        return *context_;
    }
};

// The resolved NumericCompoundAssign node over the fixture's local (the target) and
// the given value: the store type, sign, input stack types, and underlying result
// type come from the compilation's own types.
IL::NumericCompoundAssign MakeCompoundAssign(
    BuilderFixture& fixture, TS::KnownTypeCode targetTypeCode, const char* name,
    IL::BinaryNumericOperator op, TS::Sign sign, std::unique_ptr<IL::ILInstruction> value,
    IL::CompoundEvalMode evalMode = IL::CompoundEvalMode::EvaluatesToNewValue,
    IL::CompoundTargetKind targetKind = IL::CompoundTargetKind::Property,
    IL::StackType rightInputType = IL::StackType::I4)
{
    auto type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(targetTypeCode).shared_from_this());
    auto local = fixture.MakeLocal(targetTypeCode, name);
    std::unique_ptr<IL::ILInstruction> target;
    if (targetKind == IL::CompoundTargetKind::Address)
        target = std::make_unique<IL::LdLoca>(local);
    else
        target = std::make_unique<IL::LdLoc>(local);
    // The C# ctor copies the BNI's fields: LeftInputType is the loaded value's
    // stack type (the store type), and UnderlyingResultType is the value
    // computation's stack type -- neither is the target instruction's own result
    // type (a Ref for the Address target kind).
    IL::StackType leftStackType = TS::GetStackType(*type);
    IL::StackType underlying = value->ResultType();
    return IL::NumericCompoundAssign(op, false, sign, leftStackType, rightInputType,
                                     underlying, false, std::move(type), evalMode,
                                     std::move(target), targetKind, std::move(value));
}

} // namespace

// ---------------------------------------------------------------------------
// The HandleCompoundAssignment arms

TEST(NumericCompoundAssignTest, AddOverIntLocalRendersAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::Add,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "i");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 1);

    // The resolve result: the two-operand predefined OperatorResolveResult over the
    // store type with the unchecked AddAssign LINQ kind.
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::AddAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->UserDefinedOperatorMethod(), nullptr);
    EXPECT_FALSE(operatorRR->IsLiftedOperator());
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));

    // The assignment carries the NumericCompoundAssign IL annotation.
    const auto ilInstructions = expr.ILInstructions();
    ASSERT_EQ(ilInstructions.size(), std::size_t{1});
    EXPECT_EQ(ilInstructions[0], &node);
    // Add over an integer stack type always carries the overflow annotation; a
    // non-checked compound assign carries the unchecked one.
    EXPECT_TRUE(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>()
                != nullptr);
}

TEST(NumericCompoundAssignTest, CheckedAddRendersCheckedAnnotationAndCheckedKind)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::Add,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(1));
    node.CheckForOverflow = true;
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    // The checked context both selects the Checked LINQ kind on the resolve result
    // and the checked(...) wrapper annotation.
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::AddAssignChecked);
    EXPECT_TRUE(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>()
                != nullptr);
    EXPECT_TRUE(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>()
                    ->IsChecked);
}

TEST(NumericCompoundAssignTest, ArithmeticOperatorMatrix)
{
    BuilderFixture fixture;
    struct Row {
        IL::BinaryNumericOperator ilOp;
        Syntax::AssignmentOperatorType assignOp;
        TS::ExpressionType linqKind;
    };
    Row rows[] = {
        {IL::BinaryNumericOperator::Sub, Syntax::AssignmentOperatorType::Subtract,
         TS::ExpressionType::SubtractAssign},
        {IL::BinaryNumericOperator::Mul, Syntax::AssignmentOperatorType::Multiply,
         TS::ExpressionType::MultiplyAssign},
        {IL::BinaryNumericOperator::Div, Syntax::AssignmentOperatorType::Divide,
         TS::ExpressionType::DivideAssign},
        {IL::BinaryNumericOperator::Rem, Syntax::AssignmentOperatorType::Modulus,
         TS::ExpressionType::ModuloAssign},
    };
    for (const Row& row : rows)
    {
        BuilderFixture rowFixture;
        auto builder = rowFixture.MakeBuilder();
        IL::NumericCompoundAssign node = MakeCompoundAssign(
            rowFixture, TS::KnownTypeCode::Int32, "i", row.ilOp, TS::Sign::Signed,
            std::make_unique<IL::LdcI4>(1));
        auto expr = builder.Translate(&node);
        auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
        ASSERT_TRUE(assignment != nullptr) << "operator " << static_cast<int>(row.ilOp);
        EXPECT_EQ(assignment->Operator(), row.assignOp);
        const auto* operatorRR =
            dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
        ASSERT_TRUE(operatorRR != nullptr);
        EXPECT_EQ(operatorRR->OperatorType(), row.linqKind);
                EXPECT_TRUE(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>()
                    != nullptr);
    }
}

TEST(NumericCompoundAssignTest, BitwiseOperatorsCarryNoOverflowAnnotation)
{
    BuilderFixture fixture;
    struct Row {
        IL::BinaryNumericOperator ilOp;
        Syntax::AssignmentOperatorType assignOp;
        TS::ExpressionType linqKind;
    };
    Row rows[] = {
        {IL::BinaryNumericOperator::BitAnd, Syntax::AssignmentOperatorType::BitwiseAnd,
         TS::ExpressionType::AndAssign},
        {IL::BinaryNumericOperator::BitOr, Syntax::AssignmentOperatorType::BitwiseOr,
         TS::ExpressionType::OrAssign},
        {IL::BinaryNumericOperator::BitXor, Syntax::AssignmentOperatorType::ExclusiveOr,
         TS::ExpressionType::ExclusiveOrAssign},
    };
    for (const Row& row : rows)
    {
        BuilderFixture rowFixture;
        auto builder = rowFixture.MakeBuilder();
        IL::NumericCompoundAssign node = MakeCompoundAssign(
            rowFixture, TS::KnownTypeCode::Int32, "i", row.ilOp, TS::Sign::Signed,
            std::make_unique<IL::LdcI4>(3));
        auto expr = builder.Translate(&node);
        auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
        ASSERT_TRUE(assignment != nullptr) << "operator " << static_cast<int>(row.ilOp);
        EXPECT_EQ(assignment->Operator(), row.assignOp);
        const auto* operatorRR =
            dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
        ASSERT_TRUE(operatorRR != nullptr);
        EXPECT_EQ(operatorRR->OperatorType(), row.linqKind);
        // The bitwise operators can never overflow: no checked/unchecked annotation.
        EXPECT_EQ(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>(),
                  nullptr);
    }
}

TEST(NumericCompoundAssignTest, EvaluatesToOldValueRendersPostIncrement)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "num", IL::BinaryNumericOperator::Add,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(1),
        IL::CompoundEvalMode::EvaluatesToOldValue, IL::CompoundTargetKind::Address);
    auto expr = builder.Translate(&node);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::PostIncrement);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(identifier->Identifier(), "num");

    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::PostIncrementAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->UserDefinedOperatorMethod(), nullptr);
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(1));

    const auto ilInstructions = expr.ILInstructions();
    ASSERT_EQ(ilInstructions.size(), std::size_t{1});
    EXPECT_EQ(ilInstructions[0], &node);
}

TEST(NumericCompoundAssignTest, EvaluatesToOldValueFloatConstantRendersPostDecrement)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The float post-decrement shape: the value is the float constant 1.0 (the
    // DEBUG assert's MatchLdcF8 arm).
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Double, "d", IL::BinaryNumericOperator::Sub,
        TS::Sign::Signed, std::make_unique<IL::LdcF8>(1.0),
        IL::CompoundEvalMode::EvaluatesToOldValue, IL::CompoundTargetKind::Address);
    auto expr = builder.Translate(&node);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::PostDecrement);
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::PostDecrementAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Double");
}

TEST(NumericCompoundAssignTest, AddressTargetKindDereferencesThroughLdObj)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The Address target kind loads through the LdObj helper: the ldloca target
    // renders as the variable identifier (the DirectionExpression unwrap).
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::Mul,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(2),
        IL::CompoundEvalMode::EvaluatesToNewValue, IL::CompoundTargetKind::Address);
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Multiply);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "i");
}

TEST(NumericCompoundAssignTest, PointerTargetFoldsByteOffsetToElementCount)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // ptr += 40 over an int32 element type folds the byte offset to the element
    // count (40 / 4): `p += 10`.
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto pointerType = std::make_shared<TS::PointerType>(
        const_cast<TS::IType&>(intType).shared_from_this());
    auto pointerLocal = fixture.MakeLocalOverType(pointerType, "p");
    IL::NumericCompoundAssign node(
        IL::BinaryNumericOperator::Add, false, TS::Sign::Unsigned, IL::StackType::I,
        IL::StackType::I4, IL::StackType::I, false, pointerType,
        IL::CompoundEvalMode::EvaluatesToNewValue, std::make_unique<IL::LdLoc>(pointerLocal),
        IL::CompoundTargetKind::Address, std::make_unique<IL::LdcI4>(40));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* offset = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(offset != nullptr);
    EXPECT_EQ(*offset, 10);
}

TEST(NumericCompoundAssignTest, PointerTargetWithoutDetectableOffsetAddsErrorComment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A value whose offset cannot be detected keeps the raw value and records the
    // multi-line error comment as trailing trivia.
    auto intLocal = fixture.MakeLocal(TS::KnownTypeCode::Int32, "i");
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto pointerType = std::make_shared<TS::PointerType>(
        const_cast<TS::IType&>(intType).shared_from_this());
    auto pointerLocal = fixture.MakeLocalOverType(pointerType, "p");
    IL::NumericCompoundAssign node(
        IL::BinaryNumericOperator::Add, false, TS::Sign::Unsigned, IL::StackType::I,
        IL::StackType::I4, IL::StackType::I, false, pointerType,
        IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoc>(pointerLocal), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdLoc>(intLocal));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    auto* right = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    EXPECT_EQ(right->Identifier(), "i");
    ASSERT_EQ(right->TrailingTrivia().size(), std::size_t{1});
    auto* comment = dynamic_cast<Syntax::Comment*>(right->TrailingTrivia()[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(), "ILSpy Error: GetPointerArithmeticOffset() failed");
    EXPECT_EQ(comment->CommentType(), Syntax::CommentType::MultiLine);
}

// ---------------------------------------------------------------------------
// The HandleCompoundShift arms

TEST(NumericCompoundAssignTest, ShiftLeftConvertsValueAndResolvesThroughTheResolver)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::ShiftLeft,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::ShiftLeft);
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 1);

    // The resolve result comes from the resolver's ResolveAssignment over the
    // shifted operands (LeftShiftAssign over the store type).
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::LeftShiftAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));
    // The shift operators can never overflow: no checked/unchecked annotation.
    EXPECT_EQ(assignment->Annotation<Transforms::CheckedUncheckedAnnotation>(), nullptr);
}

TEST(NumericCompoundAssignTest, ShiftRightSignedStaysSigned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::ShiftRight,
        TS::Sign::Signed, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::ShiftRight);
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::RightShiftAssign);
}

TEST(NumericCompoundAssignTest, ShiftRightUnsignedOverSignedTypeUsesUnsignedOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The first gate: an unsigned sign over a type whose own sign is Signed uses
    // the C# 11 >>> spelling (the UnsignedRightShift setting is on by default).
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i", IL::BinaryNumericOperator::ShiftRight,
        TS::Sign::Unsigned, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::UnsignedShiftRight);
    // The >>> form has no LINQ expression-tree node: the resolve result is the
    // plain two-operand form over the store type (the Extension LINQ kind).
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::Extension);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));
}

TEST(NumericCompoundAssignTest, ShiftRightUnsignedOverSmallUnsignedTypeUsesUnsignedOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The second gate: an unsigned sign over a small unsigned type (promoted to
    // signed int by the compiler, so the sign bit is zero) preserves the IL's
    // spelling when the setting allows.
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int16, "s", IL::BinaryNumericOperator::ShiftRight,
        TS::Sign::Unsigned, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::UnsignedShiftRight);
}

TEST(NumericCompoundAssignTest, ShiftRightUnsignedOverLargeUnsignedTypeStaysSigned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // UInt64 is neither signed nor a small integer type: the plain >> spelling.
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::UInt64, "l", IL::BinaryNumericOperator::ShiftRight,
        TS::Sign::Unsigned, std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::ShiftRight);
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::RightShiftAssign);
}

TEST(NumericCompoundAssignTest, DefaultOperatorThrowsArgumentOutOfRange)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // An out-of-range BinaryNumericOperator value reaches the default arm.
    IL::NumericCompoundAssign node = MakeCompoundAssign(
        fixture, TS::KnownTypeCode::Int32, "i",
        static_cast<IL::BinaryNumericOperator>(99), TS::Sign::Signed,
        std::make_unique<IL::LdcI4>(1));
    EXPECT_THROW(
        {
            try
            {
                builder.Translate(&node);
            }
            catch (const std::out_of_range& e)
            {
                EXPECT_STREQ(e.what(),
                             "Exception of type 'System.ArgumentOutOfRangeException' was "
                             "thrown.");
                throw;
            }
        },
        std::out_of_range);
}

} // namespace ILSpy::Tests
