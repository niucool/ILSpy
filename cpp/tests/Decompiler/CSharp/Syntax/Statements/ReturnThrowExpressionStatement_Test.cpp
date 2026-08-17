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

// Tests for the first slot-bearing C# AST statement nodes -- the `ReturnStatement`/`ThrowStatement`
// nullable-`Expression?` sibling pair (the simplest nullable-single-slot shape, a `MatchOptional`
// `DoMatch`) plus the `ExpressionStatement` required-`Expression` counterpart (the
// `UnaryOperatorExpression` D231 shape applied to a statement, a `MatchRequired` `DoMatch`) -- the
// next in-order Phase-5 piece per the D254 plan. All three carry a single `[Slot("Expression")]`
// `Expression` child at flattened index 0 (reusing the already-ported `Slots::Expression` kind);
// `ReturnStatement`/`ThrowStatement` add a `ReturnKeyword`/`ThrowKeyword` const string, and the
// slot is nullable (the returned value / thrown exception, absent for a bare `return;`/rethrow
// `throw;`); `ExpressionStatement` has no const string and a REQUIRED `Expression` (the
// statement-expression). The three suites share a `RecordingVisitor` and a `DoMatchAgainst`
// helper (the D234 multi-suite pattern); each is independently `--gtest_filter`-selectable.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three statement `Visit` methods under test (and
// the leaf expressions they hold), recording a tag and recursing via `VisitChildren` (the
// inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("return");
        VisitChildren(node);
    }
    void VisitThrowStatement(ThrowStatement* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("throw");
        VisitChildren(node);
    }
    void VisitExpressionStatement(ExpressionStatement* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("exprstmt");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("null");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("this");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ==========================================================================
// ReturnStatement (nullable Expression slot + ReturnKeyword const)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ReturnStatement, IsStatementAndAstNodeNotExpression) {
    ReturnStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keyword -------------------------------------------------------

TEST(CSharp_ReturnStatement, ReturnKeywordConst) {
    EXPECT_STREQ(ReturnStatement::ReturnKeyword, "return");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_ReturnStatement, DefaultCtor) {
    ReturnStatement s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);  // the slot counts even when empty
}

TEST(CSharp_ReturnStatement, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    ReturnStatement s(operand.get());
    EXPECT_EQ(s.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &s);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_ReturnStatement, ExpressionSetterFillsAndParents) {
    ReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    EXPECT_EQ(s.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(s.ChildIndicesValid());
}

TEST(CSharp_ReturnStatement, NullSetterClearsSlot) {
    ReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    s.Expression(nullptr);
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

TEST(CSharp_ReturnStatement, GetChildReturnsSlot) {
    ReturnStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    EXPECT_EQ(s.GetChild(0), operand.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
    EXPECT_THROW(s.GetChild(-1), std::out_of_range);
}

TEST(CSharp_ReturnStatement, SetChildRoutesToSlot) {
    ReturnStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, operand.get());
    EXPECT_EQ(s.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_ReturnStatement, GetChildSlotInfoReturnsSlotStatic) {
    ReturnStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &ReturnStatement::ExpressionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_ReturnStatement, SlotKindPointsAtSharedSlotsConstant) {
    ReturnStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    ASSERT_EQ(operand->Slot(), &ReturnStatement::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

TEST(CSharp_ReturnStatement, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(ReturnStatement::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ReturnStatement::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_ReturnStatement, AcceptVisitorDispatchesToVisit) {
    ReturnStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "return");
}

TEST(CSharp_ReturnStatement, AcceptVisitorIsVirtual) {
    ReturnStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "return");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "return");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_ReturnStatement, DepthFirstWalksOperandInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ReturnStatement s(operand.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "return");
    EXPECT_EQ(v.trace[1], "null");
}

TEST(CSharp_ReturnStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ReturnStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "return");
}

// ---- DoMatch (the generated nullable-recursive MatchOptional match) -----

TEST(CSharp_ReturnStatement, DoMatchMatchesSameValue) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchBothAbsentMatches) {
    // Both operands absent (null) match -- the MatchOptional nullable-recursive path.
    ReturnStatement a;
    ReturnStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchPatternPresentCandidateAbsentRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());
    ReturnStatement b;  // no operand
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchPatternAbsentCandidatePresentRejects) {
    ReturnStatement a;  // no operand
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchOperandDelegatesToOperandDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    ReturnStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchRejectsThrowStatementSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ReturnStatement, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_ReturnStatement, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_ReturnStatement, CloneDeepCopiesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ReturnStatement s(operand.get());
    std::unique_ptr<ReturnStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

TEST(CSharp_ReturnStatement, CloneEmptyNodeHasNoChild) {
    ReturnStatement s;
    std::unique_ptr<ReturnStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
}

TEST(CSharp_ReturnStatement, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ReturnStatement s(operand.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ReturnStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<ReturnStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_ReturnStatement, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ReturnStatement s(operand.get());
    std::unique_ptr<ReturnStatement> copy(s.Clone());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_EQ(s.Expression(), operand.get());
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_ReturnStatement, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ReturnStatement s(operand.get());
    s.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_ReturnStatement, CheckInvariantPassesOnEmptyNode) {
    // The slot is nullable, so an empty node is invariant-valid (unlike a required-slot node).
    ReturnStatement s;
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// ThrowStatement (nullable Expression slot + ThrowKeyword const)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ThrowStatement, IsStatementAndAstNodeNotExpression) {
    ThrowStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keyword -------------------------------------------------------

TEST(CSharp_ThrowStatement, ThrowKeywordConst) {
    EXPECT_STREQ(ThrowStatement::ThrowKeyword, "throw");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_ThrowStatement, DefaultCtor) {
    ThrowStatement s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_ThrowStatement, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    ThrowStatement s(operand.get());
    EXPECT_EQ(s.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &s);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_ThrowStatement, ExpressionSetterFillsAndParents) {
    ThrowStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    EXPECT_EQ(s.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_EQ(a->ChildIndex, 0);
}

TEST(CSharp_ThrowStatement, NullSetterClearsSlot) {
    ThrowStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    s.Expression(nullptr);
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

TEST(CSharp_ThrowStatement, GetChildReturnsSlot) {
    ThrowStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    EXPECT_EQ(s.GetChild(0), operand.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_ThrowStatement, SetChildRoutesToSlot) {
    ThrowStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, operand.get());
    EXPECT_EQ(s.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_ThrowStatement, GetChildSlotInfoReturnsSlotStatic) {
    ThrowStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &ThrowStatement::ExpressionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_ThrowStatement, SlotKindPointsAtSharedSlotsConstant) {
    ThrowStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    ASSERT_EQ(operand->Slot(), &ThrowStatement::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

TEST(CSharp_ThrowStatement, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(ThrowStatement::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ThrowStatement::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_ThrowStatement, AcceptVisitorDispatchesToVisit) {
    ThrowStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
}

TEST(CSharp_ThrowStatement, AcceptVisitorIsVirtual) {
    ThrowStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "throw");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_ThrowStatement, DepthFirstWalksOperandInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowStatement s(operand.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "throw");
    EXPECT_EQ(v.trace[1], "null");
}

TEST(CSharp_ThrowStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ThrowStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
}

// ---- DoMatch (the generated nullable-recursive MatchOptional match) -----

TEST(CSharp_ThrowStatement, DoMatchMatchesSameValue) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchBothAbsentMatches) {
    ThrowStatement a;
    ThrowStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchPatternPresentCandidateAbsentRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    ThrowStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchPatternAbsentCandidatePresentRejects) {
    ThrowStatement a;
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchOperandDelegatesToOperandDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    auto bOperand = std::make_unique<ThisReferenceExpression>();
    ThrowStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchRejectsReturnStatementSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ThrowStatement, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_ThrowStatement, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowStatement a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_ThrowStatement, CloneDeepCopiesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowStatement s(operand.get());
    std::unique_ptr<ThrowStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

TEST(CSharp_ThrowStatement, CloneEmptyNodeHasNoChild) {
    ThrowStatement s;
    std::unique_ptr<ThrowStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
}

TEST(CSharp_ThrowStatement, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowStatement s(operand.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ThrowStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<ThrowStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_ThrowStatement, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowStatement s(operand.get());
    std::unique_ptr<ThrowStatement> copy(s.Clone());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_EQ(s.Expression(), operand.get());
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_ThrowStatement, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowStatement s(operand.get());
    s.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_ThrowStatement, CheckInvariantPassesOnEmptyNode) {
    // The slot is nullable, so an empty node is invariant-valid.
    ThrowStatement s;
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// ExpressionStatement (required Expression slot, no const)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ExpressionStatement, IsStatementAndAstNodeNotExpression) {
    ExpressionStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_ExpressionStatement, DefaultCtor) {
    ExpressionStatement s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_ExpressionStatement, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    ExpressionStatement s(operand.get());
    EXPECT_EQ(s.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &s);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_ExpressionStatement, ExpressionSetterFillsAndParents) {
    ExpressionStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    EXPECT_EQ(s.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(s.ChildIndicesValid());
}

TEST(CSharp_ExpressionStatement, NullSetterClearsSlot) {
    // The slot is required, but the setter still tolerates a null to detach -- the invariant is
    // enforced by CheckInvariant, not by the setter (the UnaryOperatorExpression D231 precedent).
    ExpressionStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    s.Expression(nullptr);
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

TEST(CSharp_ExpressionStatement, GetChildReturnsSlot) {
    ExpressionStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    EXPECT_EQ(s.GetChild(0), operand.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_ExpressionStatement, SetChildRoutesToSlot) {
    ExpressionStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, operand.get());
    EXPECT_EQ(s.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_ExpressionStatement, GetChildSlotInfoReturnsSlotStatic) {
    ExpressionStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &ExpressionStatement::ExpressionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_ExpressionStatement, SlotKindPointsAtSharedSlotsConstant) {
    ExpressionStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.Expression(operand.get());
    ASSERT_EQ(operand->Slot(), &ExpressionStatement::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

TEST(CSharp_ExpressionStatement, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(ExpressionStatement::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ExpressionStatement::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_ExpressionStatement, AcceptVisitorDispatchesToVisit) {
    ExpressionStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "exprstmt");
}

TEST(CSharp_ExpressionStatement, AcceptVisitorIsVirtual) {
    ExpressionStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "exprstmt");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "exprstmt");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_ExpressionStatement, DepthFirstWalksOperandInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement s(operand.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "exprstmt");
    EXPECT_EQ(v.trace[1], "null");
}

TEST(CSharp_ExpressionStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ExpressionStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "exprstmt");
}

// ---- DoMatch (the generated non-nullable-recursive MatchRequired match) -

TEST(CSharp_ExpressionStatement, DoMatchMatchesSameValue) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchNullPatternOperandRejects) {
    // A null pattern operand rejects: MatchRequired guards the missing required child (the C#
    // would null-dereference this.Expression; the port returns false instead of crashing).
    ExpressionStatement a(nullptr);
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    ExpressionStatement b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchBothOperandsNullRejects) {
    ExpressionStatement a(nullptr);
    ExpressionStatement b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchOperandDelegatesToOperandDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    auto bOperand = std::make_unique<ThisReferenceExpression>();
    ExpressionStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchRejectsReturnStatementSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    ReturnStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ExpressionStatement, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_ExpressionStatement, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_ExpressionStatement, CloneDeepCopiesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement s(operand.get());
    std::unique_ptr<ExpressionStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

TEST(CSharp_ExpressionStatement, CloneEmptyNodeHasNoChild) {
    ExpressionStatement s;
    std::unique_ptr<ExpressionStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
}

TEST(CSharp_ExpressionStatement, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement s(operand.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ExpressionStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<ExpressionStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_ExpressionStatement, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement s(operand.get());
    std::unique_ptr<ExpressionStatement> copy(s.Clone());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_EQ(s.Expression(), operand.get());
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_ExpressionStatement, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ExpressionStatement s(operand.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_ReturnStatement, SlotStaticsAreDistinctAcrossSiblings) {
    // The three sibling statement nodes each declare their own per-node ExpressionSlot pointing
    // at the shared Slots::Expression kind; the slot statics are distinct objects (distinct
    // addresses) but share the kind. Compare through the common CSharpSlotInfo* base.
    const CSharpSlotInfo* ret = &ReturnStatement::ExpressionSlot;
    const CSharpSlotInfo* thr = &ThrowStatement::ExpressionSlot;
    const CSharpSlotInfo* exp = &ExpressionStatement::ExpressionSlot;
    EXPECT_NE(ret, thr);
    EXPECT_NE(ret, exp);
    EXPECT_NE(thr, exp);
    EXPECT_EQ(ret->Kind(), &Slots::Expression);
    EXPECT_EQ(thr->Kind(), &Slots::Expression);
    EXPECT_EQ(exp->Kind(), &Slots::Expression);
}
