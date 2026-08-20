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

// Tests for `GenericGrammarAmbiguityVisitor` (OutputVisitor/GenericGrammarAmbiguityVisitor.hpp,
// the port of ICSharpCode.Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.cs) --
// the "F(G<A,B>(7));" grammar-ambiguity resolver. `CausesAmbiguityWithGenerics` probes a single
// `LessThan` binary; `ResolveAmbiguities` walks a tree and wraps each ambiguous binary in a
// `ParenthesizedExpression`. The tests build small expression trees covering the operator,
// nesting, and per-node-Visit dispatch branches, then assert the boolean probe or the
// post-walk wrap state.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using ILSpy::Decompiler::CSharp::OutputVisitor::GenericGrammarAmbiguityVisitor;

namespace {

// Builds an `IdentifierExpression` holding the given name (the operand nodes the tests use).
std::unique_ptr<IdentifierExpression> MakeId(const std::string& name) {
    return std::make_unique<IdentifierExpression>(name);
}

// Builds a `PrimitiveExpression` holding an `int32` literal (an unhandled operand the walk
// stops on, and the operand inside a `ParenthesizedExpression`).
std::unique_ptr<PrimitiveExpression> MakeInt(int value) {
    return std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(value)));
}

// Builds a `ParenthesizedExpression` wrapping the given operand (the `(...)` the resolver
// detects after a matching `>`).
std::unique_ptr<ParenthesizedExpression> MakeParen(Expression* operand) {
    return std::make_unique<ParenthesizedExpression>(operand);
}

// True when `expr` is a `ParenthesizedExpression`.
bool IsParen(Expression* expr) {
    return dynamic_cast<ParenthesizedExpression*>(expr) != nullptr;
}

} // namespace

// A `GreaterThan` binary (not `LessThan`) is never ambiguous: the probe returns false before
// driving any walk (the `if (Operator != LessThan) return false` early-out).
TEST(CSharp_GenericGrammarAmbiguityVisitor, NotLessThanOperatorReturnsFalse) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    BinaryOperatorExpression greater(a.get(), BinaryOperatorType::GreaterThan, b.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&greater));
}

// A standalone `a < b` (no node following in document order): the walk reaches `b`, keeps
// visiting (identifier), then runs out of nodes -- no matching `>`, no ambiguity.
TEST(CSharp_GenericGrammarAmbiguityVisitor, LessThanWithNoFollowingGreaterReturnsFalse) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, b.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < 5`: the right operand is an unhandled `PrimitiveExpression`, so `VisitChildren` stops the
// walk (an unhandled node is not valid in a type-argument list) -- no ambiguity.
TEST(CSharp_GenericGrammarAmbiguityVisitor, LessThanFollowedByUnhandledPrimitiveReturnsFalse) {
    auto a = MakeId("a");
    auto five = MakeInt(5);
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, five.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < (b > (c))`: the walk descends into the `>` binary, the `>` closes the nesting (1 -> 0),
// and its right operand is a `ParenthesizedExpression` -- the `G<...>(...)` shape, so the probe
// returns true (the basic positive case).
TEST(CSharp_GenericGrammarAmbiguityVisitor, GreaterThanFollowedByParenReturnsTrue) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto paren = MakeParen(c.get());
    auto greater = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::GreaterThan, paren.get());
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, greater.get());

    EXPECT_TRUE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < (b > c)`: the `>` closes the nesting but its right operand is an identifier (not a
// `ParenthesizedExpression`), so there is no `(...)` to signal a generic call -- no ambiguity.
TEST(CSharp_GenericGrammarAmbiguityVisitor, GreaterThanFollowedByIdentifierReturnsFalse) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeId("c");
    auto greater = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::GreaterThan, c.get());
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, greater.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < (b + (c))`: the `+` is not `<`/`>`/`>>`/`>>>`, so the `default` case stops the walk
// (no ambiguity) -- the `(...)` after the `+` does not matter, only `>`-family operators close
// the generic nesting.
TEST(CSharp_GenericGrammarAmbiguityVisitor, DefaultOperatorStopsWalk) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto paren = MakeParen(c.get());
    auto add = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::Add, paren.get());
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, add.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < (T > (c))` where the `<`'s right operand is a `TypeReferenceExpression`: the visitor keeps
// visiting (returns false), so the walk continues past it to the `>` binary and detects the
// `(...)` -- the probe returns true. Pins `VisitTypeReferenceExpression` returns false.
TEST(CSharp_GenericGrammarAmbiguityVisitor, TypeReferenceExpressionKeepsVisiting) {
    auto a = MakeId("a");
    auto type = std::make_unique<SimpleType>("T");
    auto typeRef = std::make_unique<TypeReferenceExpression>(type.get());
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto paren = MakeParen(c.get());
    auto greater = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::GreaterThan, paren.get());
    auto less = std::make_unique<BinaryOperatorExpression>(
        a.get(), BinaryOperatorType::LessThan, typeRef.get());
    BinaryOperatorExpression outer(less.get(), BinaryOperatorType::Add, greater.get());

    EXPECT_TRUE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(less.get()));
}

// `a < (x.y > (c))` where the `<`'s right operand is a `MemberReferenceExpression`: the visitor
// recurses into the `Target` (the `x` identifier) and keeps visiting, so the walk continues to
// the `>` binary and detects the `(...)` -- the probe returns true. Pins
// `VisitMemberReferenceExpression` returns `Target.AcceptVisitor(this)`.
TEST(CSharp_GenericGrammarAmbiguityVisitor, MemberReferenceExpressionRecursesIntoTarget) {
    auto a = MakeId("a");
    auto x = MakeId("x");
    auto mre = std::make_unique<MemberReferenceExpression>(x.get(), "y");
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto paren = MakeParen(c.get());
    auto greater = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::GreaterThan, paren.get());
    auto less = std::make_unique<BinaryOperatorExpression>(
        a.get(), BinaryOperatorType::LessThan, mre.get());
    BinaryOperatorExpression outer(less.get(), BinaryOperatorType::Add, greater.get());

    EXPECT_TRUE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(less.get()));
}

// `a < (c < (e >> (f)))`: two `<` raise the nesting to 2, then `>>` (`ShiftRight`) subtracts 2
// back to 0, and its right operand is a `ParenthesizedExpression` -- the probe returns true.
// Pins the `ShiftRight when genericNestingLevel >= 2` guard and the `-= 2` adjustment.
TEST(CSharp_GenericGrammarAmbiguityVisitor, ShiftRightWithSufficientNestingReturnsTrue) {
    auto a = MakeId("a");
    auto c = MakeId("c");
    auto e = MakeId("e");
    auto f = MakeInt(1);
    auto parenF = MakeParen(f.get());
    auto shiftRight = std::make_unique<BinaryOperatorExpression>(
        e.get(), BinaryOperatorType::ShiftRight, parenF.get());
    auto less2 = std::make_unique<BinaryOperatorExpression>(
        c.get(), BinaryOperatorType::LessThan, shiftRight.get());
    BinaryOperatorExpression less1(a.get(), BinaryOperatorType::LessThan, less2.get());

    EXPECT_TRUE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less1));
}

// `a < (b >> (c))`: only one `<` (nesting 1) precedes the `>>`, so the `ShiftRight when
// genericNestingLevel >= 2` guard fails and the walk stops (no ambiguity) -- the probe returns
// false. Pins a failed `when` guard falls through to the stop (no `-= 2`).
TEST(CSharp_GenericGrammarAmbiguityVisitor, ShiftRightWithInsufficientNestingReturnsFalse) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto parenC = MakeParen(c.get());
    auto shiftRight = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::ShiftRight, parenC.get());
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, shiftRight.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `a < (b < (c < (d >>> (e))))`: three `<` raise the nesting to 3, then `>>>`
// (`UnsignedShiftRight`) subtracts 3 back to 0, and its right operand is a
// `ParenthesizedExpression` -- the probe returns true. Pins the
// `UnsignedShiftRight when genericNestingLevel >= 3` guard and the `-= 3` adjustment.
TEST(CSharp_GenericGrammarAmbiguityVisitor, UnsignedShiftRightWithSufficientNestingReturnsTrue) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeId("c");
    auto d = MakeId("d");
    auto e = MakeInt(1);
    auto parenE = MakeParen(e.get());
    auto unsignedShiftRight = std::make_unique<BinaryOperatorExpression>(
        d.get(), BinaryOperatorType::UnsignedShiftRight, parenE.get());
    auto less3 = std::make_unique<BinaryOperatorExpression>(
        c.get(), BinaryOperatorType::LessThan, unsignedShiftRight.get());
    auto less2 = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::LessThan, less3.get());
    BinaryOperatorExpression less1(a.get(), BinaryOperatorType::LessThan, less2.get());

    EXPECT_TRUE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less1));
}

// `a < (b >>> (c))`: only one `<` (nesting 1) precedes the `>>>`, so the `UnsignedShiftRight
// when genericNestingLevel >= 3` guard fails and the walk stops (no ambiguity) -- the probe
// returns false. Pins a failed `when` guard falls through to the stop (no `-= 3`).
TEST(CSharp_GenericGrammarAmbiguityVisitor, UnsignedShiftRightWithInsufficientNestingReturnsFalse) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeInt(1);
    auto parenC = MakeParen(c.get());
    auto unsignedShiftRight = std::make_unique<BinaryOperatorExpression>(
        b.get(), BinaryOperatorType::UnsignedShiftRight, parenC.get());
    BinaryOperatorExpression less(a.get(), BinaryOperatorType::LessThan, unsignedShiftRight.get());

    EXPECT_FALSE(GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(&less));
}

// `ResolveAmbiguities` wraps the ambiguous `a < b` (its right operand, walked forward, is
// closed by `c > (d)` whose right operand is a `ParenthesizedExpression`) in a
// `ParenthesizedExpression`, leaving the `>` binary untouched.
TEST(CSharp_GenericGrammarAmbiguityVisitor, ResolveAmbiguitiesWrapsAmbiguousLessThan) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeId("c");
    auto d = MakeInt(1);
    auto parenD = MakeParen(d.get());
    auto less = std::make_unique<BinaryOperatorExpression>(
        a.get(), BinaryOperatorType::LessThan, b.get());
    auto greater = std::make_unique<BinaryOperatorExpression>(
        c.get(), BinaryOperatorType::GreaterThan, parenD.get());
    BinaryOperatorExpression outer(less.get(), BinaryOperatorType::Add, greater.get());

    GenericGrammarAmbiguityVisitor::ResolveAmbiguities(&outer);

    ASSERT_NE(outer.Left(), nullptr);
    EXPECT_TRUE(IsParen(outer.Left()));
    auto* wrap = dynamic_cast<ParenthesizedExpression*>(outer.Left());
    ASSERT_NE(wrap, nullptr);
    EXPECT_EQ(wrap->Expression(), less.get());
    // The `>` binary (the outer's right operand) is not a `<` and is left untouched.
    EXPECT_FALSE(IsParen(outer.Right()));
    EXPECT_EQ(dynamic_cast<BinaryOperatorExpression*>(outer.Right()), greater.get());
}

// `ResolveAmbiguities` does NOT wrap a `a < b` whose forward walk is closed by `c > d` where
// `d` is an identifier (no `(...)` after the `>`) -- the `>` binary's right operand is not a
// `ParenthesizedExpression`, so there is no generic-call shape to disambiguate.
TEST(CSharp_GenericGrammarAmbiguityVisitor, ResolveAmbiguitiesDoesNotWrapNonAmbiguous) {
    auto a = MakeId("a");
    auto b = MakeId("b");
    auto c = MakeId("c");
    auto d = MakeId("d");
    auto less = std::make_unique<BinaryOperatorExpression>(
        a.get(), BinaryOperatorType::LessThan, b.get());
    auto greater = std::make_unique<BinaryOperatorExpression>(
        c.get(), BinaryOperatorType::GreaterThan, d.get());
    BinaryOperatorExpression outer(less.get(), BinaryOperatorType::Add, greater.get());

    GenericGrammarAmbiguityVisitor::ResolveAmbiguities(&outer);

    ASSERT_NE(outer.Left(), nullptr);
    EXPECT_FALSE(IsParen(outer.Left()));
    EXPECT_EQ(dynamic_cast<BinaryOperatorExpression*>(outer.Left()), less.get());
}
