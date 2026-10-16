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

// Tests for `InsertParenthesesVisitor` (OutputVisitor/InsertParenthesesVisitor.hpp, the port
// of ICSharpCode.Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.cs) -- the AST
// transform that wraps sub-expressions in `ParenthesizedExpression` nodes wherever the C#
// language spec (or, with `InsertParenthesesForReadability`, readability) requires it. Each
// test builds a small expression tree, runs the relevant `Visit` method, and asserts whether a
// specific child slot was parenthesized (replaced with a `ParenthesizedExpression`), pinning
// the operator-precedence / associativity rules the visitor encodes.

#include <gtest/gtest.h>

#include <memory>

#include "Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using ILSpy::Decompiler::CSharp::OutputVisitor::InsertParenthesesVisitor;

namespace {

// Builds a `PrimitiveExpression` holding an `int32` literal (the operand nodes the tests use).
std::unique_ptr<PrimitiveExpression> MakeInt(int value) {
    return std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(value)));
}

// True when `expr` is a `ParenthesizedExpression` (the slot was parenthesized by the visitor).
bool IsParen(Expression* expr) {
    return dynamic_cast<ParenthesizedExpression*>(expr) != nullptr;
}

} // namespace

// `2 * (1 + 1)`: a lower-precedence `Add` as the right operand of a `Multiply` must be
// parenthesized (left-associative; the right operand needs precedence >= multiplicative + 1).
TEST(CSharp_InsertParenthesesVisitor, AddInsideMulIsParenthesized) {
    auto two = MakeInt(2);
    auto oneA = MakeInt(1);
    auto oneB = MakeInt(1);
    auto add = std::make_unique<BinaryOperatorExpression>(oneA.get(), BinaryOperatorType::Add, oneB.get());
    BinaryOperatorExpression outer(two.get(), BinaryOperatorType::Multiply, add.get());

    InsertParenthesesVisitor v;
    v.VisitBinaryOperatorExpression(&outer);

    ASSERT_NE(outer.Right(), nullptr);
    EXPECT_TRUE(IsParen(outer.Right()));
    ASSERT_NE(dynamic_cast<ParenthesizedExpression*>(outer.Right()), nullptr);
    EXPECT_EQ(dynamic_cast<ParenthesizedExpression*>(outer.Right())->Expression(), add.get());
}

// `1 + 2 * 3`: a higher-precedence `Multiply` as the right operand of an `Add` needs NO
// parentheses (multiplicative >= additive + 1).
TEST(CSharp_InsertParenthesesVisitor, MulInsideAddIsNotParenthesized) {
    auto one = MakeInt(1);
    auto two = MakeInt(2);
    auto three = MakeInt(3);
    auto mul = std::make_unique<BinaryOperatorExpression>(two.get(), BinaryOperatorType::Multiply, three.get());
    BinaryOperatorExpression outer(one.get(), BinaryOperatorType::Add, mul.get());

    InsertParenthesesVisitor v;
    v.VisitBinaryOperatorExpression(&outer);

    ASSERT_NE(outer.Right(), nullptr);
    EXPECT_FALSE(IsParen(outer.Right()));
    EXPECT_NE(dynamic_cast<BinaryOperatorExpression*>(outer.Right()), nullptr);
}

// `1 + (2 + 3)`: a same-precedence `Add` as the right operand of an `Add` must be parenthesized
// (left-associative; the right operand of equal precedence needs parens to preserve grouping).
TEST(CSharp_InsertParenthesesVisitor, SamePrecedenceRightIsParenthesized) {
    auto one = MakeInt(1);
    auto two = MakeInt(2);
    auto three = MakeInt(3);
    auto add = std::make_unique<BinaryOperatorExpression>(two.get(), BinaryOperatorType::Add, three.get());
    BinaryOperatorExpression outer(one.get(), BinaryOperatorType::Add, add.get());

    InsertParenthesesVisitor v;
    v.VisitBinaryOperatorExpression(&outer);

    ASSERT_NE(outer.Right(), nullptr);
    EXPECT_TRUE(IsParen(outer.Right()));
}

// `(1 + 2) + 3`: a same-precedence `Add` as the LEFT operand of an `Add` needs NO parentheses
// (left-associative; the left operand of equal precedence keeps the natural grouping).
TEST(CSharp_InsertParenthesesVisitor, SamePrecedenceLeftIsNotParenthesized) {
    auto one = MakeInt(1);
    auto two = MakeInt(2);
    auto three = MakeInt(3);
    auto add = std::make_unique<BinaryOperatorExpression>(one.get(), BinaryOperatorType::Add, two.get());
    BinaryOperatorExpression outer(add.get(), BinaryOperatorType::Add, three.get());

    InsertParenthesesVisitor v;
    v.VisitBinaryOperatorExpression(&outer);

    ASSERT_NE(outer.Left(), nullptr);
    EXPECT_FALSE(IsParen(outer.Left()));
    EXPECT_NE(dynamic_cast<BinaryOperatorExpression*>(outer.Left()), nullptr);
}

// `(a ? b : c) ? d : e`: a `?:` as the CONDITION of an outer `?:` must be parenthesized. Without
// the parens the inner-outer `?:` would re-associate to the right (`a ? b : (c ? d : e)`).
TEST(CSharp_InsertParenthesesVisitor, ConditionalAsConditionIsParenthesized) {
    auto a = MakeInt(1);
    auto b = MakeInt(2);
    auto c = MakeInt(3);
    auto d = MakeInt(4);
    auto e = MakeInt(5);
    auto inner = std::make_unique<ConditionalExpression>(a.get(), b.get(), c.get());
    ConditionalExpression outer(inner.get(), d.get(), e.get());

    InsertParenthesesVisitor v;
    v.VisitConditionalExpression(&outer);

    ASSERT_NE(outer.Condition(), nullptr);
    EXPECT_TRUE(IsParen(outer.Condition()));
    ASSERT_NE(dynamic_cast<ParenthesizedExpression*>(outer.Condition()), nullptr);
    EXPECT_EQ(dynamic_cast<ParenthesizedExpression*>(outer.Condition())->Expression(), inner.get());
}

// A nested unary `-(-a)`: only parenthesized in readability mode (the C# `if (child != null &&
// InsertParenthesesForReadability) Parenthesize(child)`).
TEST(CSharp_InsertParenthesesVisitor, NestedUnaryParenthesizedOnlyInReadabilityMode) {
    auto one = MakeInt(1);
    auto inner = std::make_unique<UnaryOperatorExpression>(one.get(), UnaryOperatorType::Minus);
    UnaryOperatorExpression outer(inner.get(), UnaryOperatorType::Minus);

    InsertParenthesesVisitor readability;
    readability.InsertParenthesesForReadability = true;
    readability.VisitUnaryOperatorExpression(&outer);
    ASSERT_NE(outer.Expression(), nullptr);
    EXPECT_TRUE(IsParen(outer.Expression()));

    // Rebuild an identical tree; with readability off the nested unary is left alone.
    auto one2 = MakeInt(1);
    auto inner2 = std::make_unique<UnaryOperatorExpression>(one2.get(), UnaryOperatorType::Minus);
    UnaryOperatorExpression outer2(inner2.get(), UnaryOperatorType::Minus);
    InsertParenthesesVisitor strict;
    strict.VisitUnaryOperatorExpression(&outer2);
    ASSERT_NE(outer2.Expression(), nullptr);
    EXPECT_FALSE(IsParen(outer2.Expression()));
}

// `(int)(-1)`: a cast with a `SimpleType` type and a unary-minus operand must parenthesize the
// operand (the C# grammar ambiguity -- `(A)-b` is not a cast when `A` could be an expression).
TEST(CSharp_InsertParenthesesVisitor, CastOfUnaryMinusWithSimpleTypeIsParenthesized) {
    auto one = MakeInt(1);
    auto unary = std::make_unique<UnaryOperatorExpression>(one.get(), UnaryOperatorType::Minus);
    auto type = std::make_unique<SimpleType>("int");
    CastExpression cast(type.get(), unary.get());

    InsertParenthesesVisitor v;
    v.VisitCastExpression(&cast);

    ASSERT_NE(cast.Expression(), nullptr);
    EXPECT_TRUE(IsParen(cast.Expression()));
    ASSERT_NE(dynamic_cast<ParenthesizedExpression*>(cast.Expression()), nullptr);
    EXPECT_EQ(dynamic_cast<ParenthesizedExpression*>(cast.Expression())->Expression(), unary.get());
}

// An interpolation whose expression contains a `global::` (`MemberType { IsDoubleColon: true }`)
// must have its expression parenthesized (the C# `InterpolationNeedsParenthesis` recursion).
TEST(CSharp_InsertParenthesesVisitor, InterpolationWithGlobalDoubleColonIsParenthesized) {
    auto global = std::make_unique<SimpleType>("global");
    auto memberType = std::make_unique<MemberType>(global.get(), "Foo");
    memberType->IsDoubleColon(true);
    auto typeRef = std::make_unique<TypeReferenceExpression>(memberType.get());
    Interpolation interp(typeRef.get());

    InsertParenthesesVisitor v;
    v.VisitInterpolation(&interp);

    ASSERT_NE(interp.Expression(), nullptr);
    EXPECT_TRUE(IsParen(interp.Expression()));
    ASSERT_NE(dynamic_cast<ParenthesizedExpression*>(interp.Expression()), nullptr);
    EXPECT_EQ(dynamic_cast<ParenthesizedExpression*>(interp.Expression())->Expression(), typeRef.get());
}
