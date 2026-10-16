// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

// Tests for the `LockStatement` concrete node -- the next in-order Phase-5 piece per the D260
// plan. `LockStatement` is the `WhileStatement` D258 two-required-single-slot shape (a REQUIRED
// `Expression` lock-object at index 0 + a REQUIRED `Statement` `EmbeddedStatement` body at
// index 1) with the loop test slot renamed `Expression`. It reuses the already-ported
// `Slots::Expression` (by `UnaryOperatorExpression`) and `Slots::EmbeddedStatement` (by
// `WhileStatement`) kinds with no new `Slots` constant; the `Expression()` accessor shadows
// the `Expression` base type (the `ExpressionStatement` D255 name-shadowing crux), so the
// header uses the elaborated `class Expression` specifier. The suite shares a
// `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `VisitLockStatement` under test (plus the leaf
// expression and leaf statements it holds, and the structural-twin `WhileStatement`), recording
// a tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitLockStatement(LockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("lock");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("null");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("this");
        VisitChildren(node);
    }
    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr) return;
        trace.push_back("continue");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) return;
        trace.push_back("break");
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
// LockStatement (Expression + EmbeddedStatement, both required)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_LockStatement, IsStatementAndAstNodeNotExpression) {
    LockStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keyword -------------------------------------------------------

TEST(CSharp_LockStatement, LockKeywordConst) {
    EXPECT_STREQ(LockStatement::LockKeyword, "lock");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_LockStatement, DefaultCtor) {
    LockStatement s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 2);
}

TEST(CSharp_LockStatement, AllParamsCtorSetsAndParents) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    EXPECT_EQ(s.Expression(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_LockStatement, ExpressionSetterFillsAndParents) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Expression(cond.get());
    EXPECT_EQ(s.Expression(), cond.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
}

TEST(CSharp_LockStatement, EmbeddedStatementSetterFillsAndParents) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Expression(cond.get());
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

TEST(CSharp_LockStatement, NullExpressionSetterClearsSlot) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Expression(cond.get());
    s.Expression(nullptr);
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(cond->Parent(), nullptr);
    EXPECT_EQ(cond->ChildIndex, -1);
}

TEST(CSharp_LockStatement, GetChildReturnsSlots) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.Expression(cond.get());
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.GetChild(0), cond.get());
    EXPECT_EQ(s.GetChild(1), body.get());
    EXPECT_THROW(s.GetChild(2), std::out_of_range);
}

TEST(CSharp_LockStatement, SetChildRoutesToSlots) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.SetChild(0, cond.get());
    s.SetChild(1, body.get());
    EXPECT_EQ(s.Expression(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_THROW(s.SetChild(2, nullptr), std::out_of_range);
}

TEST(CSharp_LockStatement, GetChildSlotInfoReturnsSlotStatics) {
    LockStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &LockStatement::ExpressionSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &LockStatement::EmbeddedStatementSlot);
    EXPECT_THROW(s.GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_LockStatement, SlotKindPointsAtSharedSlotsConstant) {
    LockStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.Expression(cond.get());
    s.EmbeddedStatement(body.get());
    ASSERT_EQ(cond->Slot(), &LockStatement::ExpressionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(body->Slot(), &LockStatement::EmbeddedStatementSlot);
    EXPECT_EQ(body->Slot()->Kind(), &Slots::EmbeddedStatement);
}

TEST(CSharp_LockStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(LockStatement::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(LockStatement::ExpressionSlot.IsInstanceOfType(&nre));
    ContinueStatement cs;
    EXPECT_FALSE(LockStatement::ExpressionSlot.IsInstanceOfType(&cs));
    EXPECT_TRUE(LockStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(LockStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_LockStatement, AcceptVisitorDispatchesToVisit) {
    LockStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lock");
}

TEST(CSharp_LockStatement, AcceptVisitorIsVirtual) {
    LockStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lock");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "lock");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_LockStatement, DepthFirstWalksChildrenInOrder) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "lock");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "continue");
}

TEST(CSharp_LockStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    LockStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lock");
}

// ---- DoMatch (the generated MatchRequired x2 match) ---------------------

TEST(CSharp_LockStatement, DoMatchMatchesSameValues) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    LockStatement b(bCond.get(), bBody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchDifferentExpressionRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<ThisReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    LockStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchDifferentEmbeddedStatementRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<BreakStatement>();
    LockStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchNullExpressionRejects) {
    LockStatement a(nullptr, nullptr);
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    LockStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchRejectsWhileStatementStructuralTwin) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchRejectsWhileStatementStructuralTwinReverse) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    LockStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LockStatement, DoMatchRejectsDifferentType) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_LockStatement, DoMatchRejectsNullCandidate) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    LockStatement a(aCond.get(), aBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_LockStatement, CloneDeepCopiesChildren) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    std::unique_ptr<LockStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), cond.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->EmbeddedStatement()->ChildIndex, 1);
}

TEST(CSharp_LockStatement, CloneEmptyNodeHasNoChildren) {
    LockStatement s;
    std::unique_ptr<LockStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
}

TEST(CSharp_LockStatement, CloneIsVirtualAndCovariant) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<LockStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<LockStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_LockStatement, CloneDoesNotDetachSource) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    std::unique_ptr<LockStatement> copy(s.Clone());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_LockStatement, CheckInvariantPassesOnFilledNode) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    LockStatement s(cond.get(), body.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_LockStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* c = &LockStatement::ExpressionSlot;
    const CSharpSlotInfo* e = &LockStatement::EmbeddedStatementSlot;
    EXPECT_NE(c, e);
    EXPECT_EQ(c->Kind(), &Slots::Expression);
    EXPECT_EQ(e->Kind(), &Slots::EmbeddedStatement);
    EXPECT_FALSE(c->IsOptional());
    EXPECT_FALSE(e->IsOptional());
}

TEST(CSharp_LockStatement, EmbeddedStatementKindIsSharedWithWhile) {
    // `LockStatement` and `WhileStatement` share the `Slots::EmbeddedStatement` kind.
    EXPECT_EQ(LockStatement::EmbeddedStatementSlot.Kind(),
              WhileStatement::EmbeddedStatementSlot.Kind());
}

TEST(CSharp_LockStatement, ExpressionKindIsSharedWithUnaryOperatorExpression) {
    // `LockStatement.Expression` and `UnaryOperatorExpression.Expression` share the
    // `Slots::Expression` kind (the operand position).
    EXPECT_EQ(LockStatement::ExpressionSlot.Kind(),
              &Slots::Expression);
}
