// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the operator-kind mapping layer between the C# AST operator enums and the
// BCL `System.Linq.Expressions.ExpressionType` (cpp/.../TypeSystem/ExpressionType.hpp):
// `UnaryOperatorExpression::GetLinqNodeType`, `BinaryOperatorExpression::GetLinqNodeType`,
// `AssignmentExpression::GetLinqNodeType`, `AssignmentExpression::GetCorrespondingBinaryOperator`,
// and `AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType` -- the hand-written
// statics on the operator AST nodes (UnaryOperatorExpression.cs line 114,
// BinaryOperatorExpression.cs line 126, AssignmentExpression.cs lines 107/140/172) that the
// resolver's `ResolveUnaryOperator` / `ResolveBinaryOperator` / `ResolveAssignment` regions
// consume (CSharpResolver.cs lines 417/493/530/647/922/956/2943-2944) and the future
// ExpressionBuilder re-consumes. Each mapping pins: the checked/unchecked pairs (the only
// operators with a `checkForOverflow`-sensitive result), the `Extension` fallbacks (the
// operators with no LINQ expression-tree node), the closed-throw `default` arms (the
// pattern wildcards and the pattern-only operators), and the two nullable-returning
// assignment mappings (the compound-assignment filter and the reverse mapping's
// `default: return null` -- NOT a throw).

#include <gtest/gtest.h>

#include <optional>

#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using ILSpy::Decompiler::TypeSystem::ExpressionType;

namespace {

// The set of UnaryOperatorType values that hit the C# `default` arm (throw): the
// pattern-matching wildcard and the three no-syntax wrapper kinds.
constexpr UnaryOperatorType kThrowingUnaryOperators[] = {
    UnaryOperatorType::Any,
    UnaryOperatorType::NullConditional,
    UnaryOperatorType::NullConditionalRewrap,
    UnaryOperatorType::IsTrue,
};

// The set of UnaryOperatorType values that map to ExpressionType::Extension (the
// pointer, await, and pattern operators with no LINQ expression-tree node).
constexpr UnaryOperatorType kExtensionUnaryOperators[] = {
    UnaryOperatorType::Dereference,
    UnaryOperatorType::AddressOf,
    UnaryOperatorType::Await,
    UnaryOperatorType::SuppressNullableWarning,
    UnaryOperatorType::IndexFromEnd,
    UnaryOperatorType::PatternNot,
    UnaryOperatorType::PatternRelationalLessThan,
    UnaryOperatorType::PatternRelationalLessThanOrEqual,
    UnaryOperatorType::PatternRelationalGreaterThan,
    UnaryOperatorType::PatternRelationalGreaterThanOrEqual,
};

} // namespace

// ---- UnaryOperatorExpression::GetLinqNodeType ---------------------------------------------

TEST(UnaryOperatorLinqNodeTypeTest, LogicalNotMapsToNot)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Not, false),
              ExpressionType::Not);
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Not, true),
              ExpressionType::Not);
}

TEST(UnaryOperatorLinqNodeTypeTest, BitwiseNotMapsToOnesComplement)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::BitNot, false),
              ExpressionType::OnesComplement);
}

TEST(UnaryOperatorLinqNodeTypeTest, MinusUncheckedMapsToNegate)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Minus, false),
              ExpressionType::Negate);
}

TEST(UnaryOperatorLinqNodeTypeTest, MinusCheckedMapsToNegateChecked)
{
    // The ONLY checkForOverflow-sensitive unary mapping (the C# `-a` is `Negate` unchecked
    // and `NegateChecked` in a checked context).
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Minus, true),
              ExpressionType::NegateChecked);
}

TEST(UnaryOperatorLinqNodeTypeTest, PlusMapsToUnaryPlus)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Plus, true),
              ExpressionType::UnaryPlus);
}

TEST(UnaryOperatorLinqNodeTypeTest, PreIncrementMapsToPreIncrementAssign)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Increment, false),
              ExpressionType::PreIncrementAssign);
}

TEST(UnaryOperatorLinqNodeTypeTest, PreDecrementMapsToPreDecrementAssign)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::Decrement, false),
              ExpressionType::PreDecrementAssign);
}

TEST(UnaryOperatorLinqNodeTypeTest, PostIncrementAndPostDecrementMapToPostAssigns)
{
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::PostIncrement, false),
              ExpressionType::PostIncrementAssign);
    EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(UnaryOperatorType::PostDecrement, false),
              ExpressionType::PostDecrementAssign);
}

TEST(UnaryOperatorLinqNodeTypeTest, PointerAwaitAndPatternOperatorsMapToExtension)
{
    for (UnaryOperatorType op : kExtensionUnaryOperators) {
        EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(op, false),
                  ExpressionType::Extension)
            << "operator index " << static_cast<int>(op);
        EXPECT_EQ(UnaryOperatorExpression::GetLinqNodeType(op, true),
                  ExpressionType::Extension)
            << "operator index " << static_cast<int>(op);
    }
}

TEST(UnaryOperatorLinqNodeTypeTest, PatternWildcardAndWrapperKindsThrow)
{
    for (UnaryOperatorType op : kThrowingUnaryOperators) {
        EXPECT_THROW(UnaryOperatorExpression::GetLinqNodeType(op, false), std::out_of_range)
            << "operator index " << static_cast<int>(op);
    }
}

// ---- BinaryOperatorExpression::GetLinqNodeType --------------------------------------------

TEST(BinaryOperatorLinqNodeTypeTest, BitwiseAndOrMapToAndOr)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::BitwiseAnd, false),
              ExpressionType::And);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::BitwiseOr, false),
              ExpressionType::Or);
}

TEST(BinaryOperatorLinqNodeTypeTest, ConditionalAndOrMapToAndAlsoOrElse)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::ConditionalAnd, false),
              ExpressionType::AndAlso);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::ConditionalOr, false),
              ExpressionType::OrElse);
}

TEST(BinaryOperatorLinqNodeTypeTest, ExclusiveOrMapsToExclusiveOr)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::ExclusiveOr, false),
              ExpressionType::ExclusiveOr);
}

TEST(BinaryOperatorLinqNodeTypeTest, ComparisonOperatorsMapToComparisonNodes)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::GreaterThan, false),
              ExpressionType::GreaterThan);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::GreaterThanOrEqual, false),
              ExpressionType::GreaterThanOrEqual);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Equality, false),
              ExpressionType::Equal);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::InEquality, false),
              ExpressionType::NotEqual);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::LessThan, false),
              ExpressionType::LessThan);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::LessThanOrEqual, false),
              ExpressionType::LessThanOrEqual);
}

TEST(BinaryOperatorLinqNodeTypeTest, AddUncheckedAndChecked)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Add, false),
              ExpressionType::Add);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Add, true),
              ExpressionType::AddChecked);
}

TEST(BinaryOperatorLinqNodeTypeTest, SubtractUncheckedAndChecked)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Subtract, false),
              ExpressionType::Subtract);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Subtract, true),
              ExpressionType::SubtractChecked);
}

TEST(BinaryOperatorLinqNodeTypeTest, MultiplyUncheckedAndChecked)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Multiply, false),
              ExpressionType::Multiply);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Multiply, true),
              ExpressionType::MultiplyChecked);
}

TEST(BinaryOperatorLinqNodeTypeTest, DivideAndModulusMapToDivideAndModulo)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Divide, true),
              ExpressionType::Divide);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Modulus, true),
              ExpressionType::Modulo);
}

TEST(BinaryOperatorLinqNodeTypeTest, ShiftsMapToLeftShiftAndRightShift)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::ShiftLeft, false),
              ExpressionType::LeftShift);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::ShiftRight, false),
              ExpressionType::RightShift);
}

TEST(BinaryOperatorLinqNodeTypeTest, NullCoalescingMapsToCoalesce)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::NullCoalescing, false),
              ExpressionType::Coalesce);
}

TEST(BinaryOperatorLinqNodeTypeTest, RangeAndUnsignedShiftRightMapToExtension)
{
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Range, false),
              ExpressionType::Extension);
    EXPECT_EQ(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::UnsignedShiftRight, false),
              ExpressionType::Extension);
}

TEST(BinaryOperatorLinqNodeTypeTest, PatternWildcardThrows)
{
    EXPECT_THROW(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::Any, false),
                 std::out_of_range);
}

TEST(BinaryOperatorLinqNodeTypeTest, IsPatternThrows)
{
    // An `is` pattern is NOT a binary operation and hits the C# `default` arm -- the
    // non-obvious closed-throw member (GetOperatorToken returns the `is` keyword for it,
    // but the LINQ mapping rejects it).
    EXPECT_THROW(BinaryOperatorExpression::GetLinqNodeType(BinaryOperatorType::IsPattern, false),
                 std::out_of_range);
}

// ---- AssignmentExpression::GetLinqNodeType ------------------------------------------------

TEST(AssignmentOperatorMappingTest, AssignMapsToAssign)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Assign, true),
              ExpressionType::Assign);
}

TEST(AssignmentOperatorMappingTest, AddUncheckedAndChecked)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Add, false),
              ExpressionType::AddAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Add, true),
              ExpressionType::AddAssignChecked);
}

TEST(AssignmentOperatorMappingTest, SubtractUncheckedAndChecked)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Subtract, false),
              ExpressionType::SubtractAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Subtract, true),
              ExpressionType::SubtractAssignChecked);
}

TEST(AssignmentOperatorMappingTest, MultiplyUncheckedAndChecked)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Multiply, false),
              ExpressionType::MultiplyAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Multiply, true),
              ExpressionType::MultiplyAssignChecked);
}

TEST(AssignmentOperatorMappingTest, DivideAndModulusMapToDivideAssignAndModuloAssign)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Divide, true),
              ExpressionType::DivideAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Modulus, true),
              ExpressionType::ModuloAssign);
}

TEST(AssignmentOperatorMappingTest, ShiftsMapToLeftShiftAssignAndRightShiftAssign)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::ShiftLeft, false),
              ExpressionType::LeftShiftAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::ShiftRight, false),
              ExpressionType::RightShiftAssign);
}

TEST(AssignmentOperatorMappingTest, UnsignedShiftRightMapsToExtension)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::UnsignedShiftRight, false),
              ExpressionType::Extension);
}

TEST(AssignmentOperatorMappingTest, LogicalCompoundsMapToAndOrExclusiveOrAssign)
{
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::BitwiseAnd, false),
              ExpressionType::AndAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::BitwiseOr, false),
              ExpressionType::OrAssign);
    EXPECT_EQ(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::ExclusiveOr, false),
              ExpressionType::ExclusiveOrAssign);
}

TEST(AssignmentOperatorMappingTest, PatternWildcardThrows)
{
    EXPECT_THROW(AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Any, false),
                 std::out_of_range);
}

// ---- AssignmentExpression::GetCorrespondingBinaryOperator ----------------------------------

TEST(AssignmentOperatorMappingTest, PlainAssignIsNotCompound)
{
    // The C# `GetCorrespondingBinaryOperator` returns null for the plain `=` (there is no
    // underlying binary operation); ports to std::nullopt.
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Assign),
              std::nullopt);
}

TEST(AssignmentOperatorMappingTest, ArithmeticCompoundsMapToArithmeticBinaryOperators)
{
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Add),
              BinaryOperatorType::Add);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Subtract),
              BinaryOperatorType::Subtract);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Multiply),
              BinaryOperatorType::Multiply);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Divide),
              BinaryOperatorType::Divide);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Modulus),
              BinaryOperatorType::Modulus);
}

TEST(AssignmentOperatorMappingTest, ShiftCompoundsMapToShiftBinaryOperators)
{
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::ShiftLeft),
              BinaryOperatorType::ShiftLeft);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::ShiftRight),
              BinaryOperatorType::ShiftRight);
    EXPECT_EQ(
        AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::UnsignedShiftRight),
        BinaryOperatorType::UnsignedShiftRight);
}

TEST(AssignmentOperatorMappingTest, LogicalCompoundsMapToLogicalBinaryOperators)
{
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::BitwiseAnd),
              BinaryOperatorType::BitwiseAnd);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::BitwiseOr),
              BinaryOperatorType::BitwiseOr);
    EXPECT_EQ(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::ExclusiveOr),
              BinaryOperatorType::ExclusiveOr);
}

TEST(AssignmentOperatorMappingTest, CorrespondingBinaryOperatorWildcardThrows)
{
    EXPECT_THROW(AssignmentExpression::GetCorrespondingBinaryOperator(AssignmentOperatorType::Any),
                 std::out_of_range);
}

// ---- AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType ---------------------

TEST(AssignmentOperatorMappingTest, ReverseCheckedVariantsFoldOntoSameOperator)
{
    // The checked LINQ variants fold back onto the SAME assignment operator as their
    // unchecked twins (the checked context is the resolver's CheckForOverflow flag, not
    // the operator kind).
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::AddAssign),
              AssignmentOperatorType::Add);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::AddAssignChecked),
              AssignmentOperatorType::Add);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::SubtractAssign),
              AssignmentOperatorType::Subtract);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::SubtractAssignChecked),
              AssignmentOperatorType::Subtract);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::MultiplyAssign),
              AssignmentOperatorType::Multiply);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::MultiplyAssignChecked),
              AssignmentOperatorType::Multiply);
}

TEST(AssignmentOperatorMappingTest, ReverseArithmeticAssignments)
{
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::DivideAssign),
              AssignmentOperatorType::Divide);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::ModuloAssign),
              AssignmentOperatorType::Modulus);
}

TEST(AssignmentOperatorMappingTest, ReverseShiftAssignments)
{
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::LeftShiftAssign),
              AssignmentOperatorType::ShiftLeft);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::RightShiftAssign),
              AssignmentOperatorType::ShiftRight);
}

TEST(AssignmentOperatorMappingTest, ReverseLogicalAssignments)
{
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::AndAssign),
              AssignmentOperatorType::BitwiseAnd);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::OrAssign),
              AssignmentOperatorType::BitwiseOr);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::ExclusiveOrAssign),
              AssignmentOperatorType::ExclusiveOr);
}

TEST(AssignmentOperatorMappingTest, ReverseNonAssignmentKindsReturnNullopt)
{
    // The `default: return null` (NOT a throw): any non-compound-assignment node kind maps
    // to null -- including ExpressionType.Assign itself (the plain assignment has no case
    // in the switch, the load-bearing crux: GetLinqNodeType(Assign) is NOT invertible).
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(ExpressionType::Add),
              std::nullopt);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::Assign),
              std::nullopt);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::Negate),
              std::nullopt);
    EXPECT_EQ(AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(
                  ExpressionType::Extension),
              std::nullopt);
}

TEST(AssignmentOperatorMappingTest, LinqRoundTripOverCompoundAssignments)
{
    // The forward mapping (unchecked) then the reverse mapping recovers the operator for
    // every compound assignment -- the composition the resolver's ResolveAssignment region
    // and the ExpressionBuilder rely on.
    const AssignmentOperatorType compounds[] = {
        AssignmentOperatorType::Add,     AssignmentOperatorType::Subtract,
        AssignmentOperatorType::Multiply, AssignmentOperatorType::Divide,
        AssignmentOperatorType::Modulus,  AssignmentOperatorType::ShiftLeft,
        AssignmentOperatorType::ShiftRight, AssignmentOperatorType::BitwiseAnd,
        AssignmentOperatorType::BitwiseOr, AssignmentOperatorType::ExclusiveOr,
    };
    for (AssignmentOperatorType op : compounds) {
        auto linq = AssignmentExpression::GetLinqNodeType(op, false);
        auto roundTripped = AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(linq);
        ASSERT_TRUE(roundTripped.has_value());
        EXPECT_EQ(*roundTripped, op);
    }
    // The checked variants round-trip too (they fold onto the same operator).
    auto linq = AssignmentExpression::GetLinqNodeType(AssignmentOperatorType::Add, true);
    auto roundTripped = AssignmentExpression::GetAssignmentOperatorTypeFromExpressionType(linq);
    ASSERT_TRUE(roundTripped.has_value());
    EXPECT_EQ(*roundTripped, AssignmentOperatorType::Add);
}
