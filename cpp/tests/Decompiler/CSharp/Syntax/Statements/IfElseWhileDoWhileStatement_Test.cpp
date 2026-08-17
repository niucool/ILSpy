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

// Tests for the Condition+embedded-statement loop/control statements -- `IfElseStatement`
// (a REQUIRED `Expression` `Condition` + a REQUIRED `Statement` `TrueStatement` + a NULLABLE
// `Statement?` `FalseStatement`), `WhileStatement` (a REQUIRED `Expression` `Condition` + a
// REQUIRED `Statement` `EmbeddedStatement`), and `DoWhileStatement` (the `WhileStatement` shape
// with the slot order reversed -- `EmbeddedStatement`-0/`Condition`-1 -- plus a hand-written
// `(Expression, Statement)` convenience ctor) -- the next in-order Phase-5 piece per the D257
// plan. All three reuse the already-ported `Slots::Condition` kind; `IfElseStatement` adds the
// new `Slots::TrueStatement`/`Slots::FalseStatement` kinds, `WhileStatement` adds the new
// `Slots::EmbeddedStatement` kind, and `DoWhileStatement` reuses both. The three suites share a
// `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 multi-suite pattern); each is
// independently `--gtest_filter`-selectable.

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
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three loop/control `Visit` methods under test
// (plus the leaf expression and leaf statements they hold), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitIfElseStatement(IfElseStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("if");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
    void VisitDoWhileStatement(DoWhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("dowhile");
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
// IfElseStatement (Condition + TrueStatement + nullable FalseStatement)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_IfElseStatement, IsStatementAndAstNodeNotExpression) {
    IfElseStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_IfElseStatement, IfAndElseKeywordConsts) {
    EXPECT_STREQ(IfElseStatement::IfKeyword, "if");
    EXPECT_STREQ(IfElseStatement::ElseKeyword, "else");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_IfElseStatement, DefaultCtor) {
    IfElseStatement s;
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(s.TrueStatement(), nullptr);
    EXPECT_EQ(s.FalseStatement(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 3);
}

TEST(CSharp_IfElseStatement, RequiredPrefixCtorSetsAndParents) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    NullReferenceExpression* condPtr = cond.get();
    ContinueStatement* thenPtr = then.get();
    IfElseStatement s(cond.get(), then.get());
    EXPECT_EQ(s.Condition(), condPtr);
    EXPECT_EQ(s.TrueStatement(), thenPtr);
    EXPECT_EQ(s.FalseStatement(), nullptr);
    EXPECT_EQ(condPtr->Parent(), &s);
    EXPECT_EQ(condPtr->ChildIndex, 0);
    EXPECT_EQ(thenPtr->Parent(), &s);
    EXPECT_EQ(thenPtr->ChildIndex, 1);
}

TEST(CSharp_IfElseStatement, AllParamsCtorSetsAndParents) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    IfElseStatement s(cond.get(), then.get(), els.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.TrueStatement(), then.get());
    EXPECT_EQ(s.FalseStatement(), els.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
    EXPECT_EQ(then->Parent(), &s);
    EXPECT_EQ(then->ChildIndex, 1);
    EXPECT_EQ(els->Parent(), &s);
    EXPECT_EQ(els->ChildIndex, 2);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_IfElseStatement, ConditionSetterFillsAndParents) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
}

TEST(CSharp_IfElseStatement, TrueStatementSetterFillsAndParents) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    auto then = std::make_unique<ContinueStatement>();
    s.TrueStatement(then.get());
    EXPECT_EQ(s.TrueStatement(), then.get());
    EXPECT_EQ(then->Parent(), &s);
    EXPECT_EQ(then->ChildIndex, 1);
}

TEST(CSharp_IfElseStatement, FalseStatementSetterFillsAndParents) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    s.Condition(cond.get());
    s.TrueStatement(then.get());
    auto els = std::make_unique<BreakStatement>();
    s.FalseStatement(els.get());
    EXPECT_EQ(s.FalseStatement(), els.get());
    EXPECT_EQ(els->Parent(), &s);
    EXPECT_EQ(els->ChildIndex, 2);
}

TEST(CSharp_IfElseStatement, NullFalseStatementSetterClearsSlot) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    s.Condition(cond.get());
    s.TrueStatement(then.get());
    s.FalseStatement(els.get());
    s.FalseStatement(nullptr);
    EXPECT_EQ(s.FalseStatement(), nullptr);
    EXPECT_EQ(els->Parent(), nullptr);
    EXPECT_EQ(els->ChildIndex, -1);
}

TEST(CSharp_IfElseStatement, GetChildReturnsSlots) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    s.Condition(cond.get());
    s.TrueStatement(then.get());
    s.FalseStatement(els.get());
    EXPECT_EQ(s.GetChild(0), cond.get());
    EXPECT_EQ(s.GetChild(1), then.get());
    EXPECT_EQ(s.GetChild(2), els.get());
    EXPECT_THROW(s.GetChild(3), std::out_of_range);
}

TEST(CSharp_IfElseStatement, SetChildRoutesToSlots) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    s.SetChild(0, cond.get());
    s.SetChild(1, then.get());
    s.SetChild(2, els.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.TrueStatement(), then.get());
    EXPECT_EQ(s.FalseStatement(), els.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(then->Parent(), &s);
    EXPECT_EQ(els->Parent(), &s);
    EXPECT_THROW(s.SetChild(3, nullptr), std::out_of_range);
}

TEST(CSharp_IfElseStatement, GetChildSlotInfoReturnsSlotStatics) {
    IfElseStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &IfElseStatement::ConditionSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &IfElseStatement::TrueStatementSlot);
    EXPECT_EQ(s.GetChildSlotInfo(2), &IfElseStatement::FalseStatementSlot);
    EXPECT_THROW(s.GetChildSlotInfo(3), std::out_of_range);
}

TEST(CSharp_IfElseStatement, SlotKindPointsAtSharedSlotsConstant) {
    IfElseStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    s.Condition(cond.get());
    s.TrueStatement(then.get());
    s.FalseStatement(els.get());
    ASSERT_EQ(cond->Slot(), &IfElseStatement::ConditionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Condition);
    ASSERT_EQ(then->Slot(), &IfElseStatement::TrueStatementSlot);
    EXPECT_EQ(then->Slot()->Kind(), &Slots::TrueStatement);
    ASSERT_EQ(els->Slot(), &IfElseStatement::FalseStatementSlot);
    EXPECT_EQ(els->Slot()->Kind(), &Slots::FalseStatement);
}

TEST(CSharp_IfElseStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(IfElseStatement::ConditionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(IfElseStatement::ConditionSlot.IsInstanceOfType(&nre));
    ContinueStatement cs;
    EXPECT_FALSE(IfElseStatement::ConditionSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(IfElseStatement::TrueStatementSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(IfElseStatement::TrueStatementSlot.IsInstanceOfType(&cs));
    EXPECT_TRUE(IfElseStatement::FalseStatementSlot.IsInstanceOfType(&cs));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_IfElseStatement, AcceptVisitorDispatchesToVisit) {
    IfElseStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "if");
}

TEST(CSharp_IfElseStatement, AcceptVisitorIsVirtual) {
    IfElseStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "if");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "if");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_IfElseStatement, DepthFirstWalksChildrenInOrder) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    IfElseStatement s(cond.get(), then.get(), els.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 4u);
    EXPECT_EQ(v.trace[0], "if");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "continue");
    EXPECT_EQ(v.trace[3], "break");
}

TEST(CSharp_IfElseStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    IfElseStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "if");
}

// ---- DoMatch (the generated MatchRequired + MatchOptional match) --------

TEST(CSharp_IfElseStatement, DoMatchMatchesSameValues) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    auto aEls = std::make_unique<BreakStatement>();
    IfElseStatement a(aCond.get(), aThen.get(), aEls.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    auto bEls = std::make_unique<BreakStatement>();
    IfElseStatement b(bCond.get(), bThen.get(), bEls.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchDifferentConditionRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    auto bCond = std::make_unique<ThisReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchDifferentTrueStatementRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<BreakStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchDifferentFalseStatementRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    auto aEls = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get(), aEls.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    auto bEls = std::make_unique<BreakStatement>();
    IfElseStatement b(bCond.get(), bThen.get(), bEls.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchBothFalseStatementsAbsentMatches) {
    // The nullable `FalseStatement` slot: both absent match (an `if` without `else`).
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchPatternFalsePresentCandidateAbsentRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    auto aEls = std::make_unique<BreakStatement>();
    IfElseStatement a(aCond.get(), aThen.get(), aEls.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());  // no FalseStatement
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchPatternFalseAbsentCandidatePresentRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());  // no FalseStatement
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    auto bEls = std::make_unique<BreakStatement>();
    IfElseStatement b(bCond.get(), bThen.get(), bEls.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchNullConditionRejects) {
    IfElseStatement a(nullptr, nullptr);  // missing required Condition
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchRejectsWhileStatementSibling) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchRejectsDoWhileStatementSibling) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    auto bBody = std::make_unique<ContinueStatement>();
    auto bCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement b(bBody.get(), bCond.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_IfElseStatement, DoMatchRejectsDifferentType) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_IfElseStatement, DoMatchRejectsNullCandidate) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aThen = std::make_unique<ContinueStatement>();
    IfElseStatement a(aCond.get(), aThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_IfElseStatement, CloneDeepCopiesChildren) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    IfElseStatement s(cond.get(), then.get(), els.get());
    std::unique_ptr<IfElseStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    EXPECT_EQ(copy->Condition()->ChildIndex, 0);
    ASSERT_NE(copy->TrueStatement(), nullptr);
    EXPECT_NE(copy->TrueStatement(), then.get());
    EXPECT_EQ(copy->TrueStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->TrueStatement()->ChildIndex, 1);
    ASSERT_NE(copy->FalseStatement(), nullptr);
    EXPECT_NE(copy->FalseStatement(), els.get());
    EXPECT_EQ(copy->FalseStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->FalseStatement()->ChildIndex, 2);
}

TEST(CSharp_IfElseStatement, CloneEmptyNodeHasNoChildren) {
    IfElseStatement s;
    std::unique_ptr<IfElseStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
    EXPECT_EQ(copy->TrueStatement(), nullptr);
    EXPECT_EQ(copy->FalseStatement(), nullptr);
}

TEST(CSharp_IfElseStatement, CloneSkipsAbsentFalseStatement) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    IfElseStatement s(cond.get(), then.get());  // no FalseStatement
    std::unique_ptr<IfElseStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->Condition(), nullptr);
    ASSERT_NE(copy->TrueStatement(), nullptr);
    EXPECT_EQ(copy->FalseStatement(), nullptr);
}

TEST(CSharp_IfElseStatement, CloneIsVirtualAndCovariant) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    IfElseStatement s(cond.get(), then.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<IfElseStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<IfElseStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_IfElseStatement, CloneDoesNotDetachSource) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    IfElseStatement s(cond.get(), then.get(), els.get());
    std::unique_ptr<IfElseStatement> copy(s.Clone());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(then->Parent(), &s);
    EXPECT_EQ(els->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_IfElseStatement, CheckInvariantPassesOnFilledNode) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    auto els = std::make_unique<BreakStatement>();
    IfElseStatement s(cond.get(), then.get(), els.get());
    s.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_IfElseStatement, CheckInvariantPassesWithoutFalseStatement) {
    // The `FalseStatement` slot is nullable, so an `if` without `else` is invariant-valid.
    auto cond = std::make_unique<NullReferenceExpression>();
    auto then = std::make_unique<ContinueStatement>();
    IfElseStatement s(cond.get(), then.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_IfElseStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* c = &IfElseStatement::ConditionSlot;
    const CSharpSlotInfo* t = &IfElseStatement::TrueStatementSlot;
    const CSharpSlotInfo* f = &IfElseStatement::FalseStatementSlot;
    EXPECT_NE(c, t);
    EXPECT_NE(c, f);
    EXPECT_NE(t, f);
    EXPECT_EQ(c->Kind(), &Slots::Condition);
    EXPECT_EQ(t->Kind(), &Slots::TrueStatement);
    EXPECT_EQ(f->Kind(), &Slots::FalseStatement);
    EXPECT_TRUE(IfElseStatement::FalseStatementSlot.IsOptional());
    EXPECT_FALSE(IfElseStatement::ConditionSlot.IsOptional());
    EXPECT_FALSE(IfElseStatement::TrueStatementSlot.IsOptional());
}

// ==========================================================================
// WhileStatement (Condition + EmbeddedStatement, both required)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_WhileStatement, IsStatementAndAstNodeNotExpression) {
    WhileStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keyword -------------------------------------------------------

TEST(CSharp_WhileStatement, WhileKeywordConst) {
    EXPECT_STREQ(WhileStatement::WhileKeyword, "while");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_WhileStatement, DefaultCtor) {
    WhileStatement s;
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 2);
}

TEST(CSharp_WhileStatement, AllParamsCtorSetsAndParents) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_WhileStatement, ConditionSetterFillsAndParents) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 0);
}

TEST(CSharp_WhileStatement, EmbeddedStatementSetterFillsAndParents) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

TEST(CSharp_WhileStatement, NullConditionSetterClearsSlot) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    s.Condition(nullptr);
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(cond->Parent(), nullptr);
    EXPECT_EQ(cond->ChildIndex, -1);
}

TEST(CSharp_WhileStatement, GetChildReturnsSlots) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.Condition(cond.get());
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.GetChild(0), cond.get());
    EXPECT_EQ(s.GetChild(1), body.get());
    EXPECT_THROW(s.GetChild(2), std::out_of_range);
}

TEST(CSharp_WhileStatement, SetChildRoutesToSlots) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.SetChild(0, cond.get());
    s.SetChild(1, body.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_THROW(s.SetChild(2, nullptr), std::out_of_range);
}

TEST(CSharp_WhileStatement, GetChildSlotInfoReturnsSlotStatics) {
    WhileStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &WhileStatement::ConditionSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &WhileStatement::EmbeddedStatementSlot);
    EXPECT_THROW(s.GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_WhileStatement, SlotKindPointsAtSharedSlotsConstant) {
    WhileStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.Condition(cond.get());
    s.EmbeddedStatement(body.get());
    ASSERT_EQ(cond->Slot(), &WhileStatement::ConditionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Condition);
    ASSERT_EQ(body->Slot(), &WhileStatement::EmbeddedStatementSlot);
    EXPECT_EQ(body->Slot()->Kind(), &Slots::EmbeddedStatement);
}

TEST(CSharp_WhileStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(WhileStatement::ConditionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(WhileStatement::ConditionSlot.IsInstanceOfType(&nre));
    ContinueStatement cs;
    EXPECT_FALSE(WhileStatement::ConditionSlot.IsInstanceOfType(&cs));
    EXPECT_TRUE(WhileStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(WhileStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_WhileStatement, AcceptVisitorDispatchesToVisit) {
    WhileStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "while");
}

TEST(CSharp_WhileStatement, AcceptVisitorIsVirtual) {
    WhileStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "while");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "while");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_WhileStatement, DepthFirstWalksChildrenInOrder) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "while");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "continue");
}

TEST(CSharp_WhileStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    WhileStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "while");
}

// ---- DoMatch (the generated MatchRequired x2 match) ---------------------

TEST(CSharp_WhileStatement, DoMatchMatchesSameValues) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchDifferentConditionRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<ThisReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchDifferentEmbeddedStatementRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<BreakStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchNullConditionRejects) {
    WhileStatement a(nullptr, nullptr);
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchRejectsIfElseStatementSibling) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchRejectsDoWhileStatementSibling) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    auto bBody = std::make_unique<ContinueStatement>();
    auto bCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement b(bBody.get(), bCond.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_WhileStatement, DoMatchRejectsDifferentType) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_WhileStatement, DoMatchRejectsNullCandidate) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_WhileStatement, CloneDeepCopiesChildren) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    std::unique_ptr<WhileStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    EXPECT_EQ(copy->Condition()->ChildIndex, 0);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->EmbeddedStatement()->ChildIndex, 1);
}

TEST(CSharp_WhileStatement, CloneEmptyNodeHasNoChildren) {
    WhileStatement s;
    std::unique_ptr<WhileStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
}

TEST(CSharp_WhileStatement, CloneIsVirtualAndCovariant) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<WhileStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<WhileStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_WhileStatement, CloneDoesNotDetachSource) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    std::unique_ptr<WhileStatement> copy(s.Clone());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_WhileStatement, CheckInvariantPassesOnFilledNode) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    WhileStatement s(cond.get(), body.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_WhileStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* c = &WhileStatement::ConditionSlot;
    const CSharpSlotInfo* e = &WhileStatement::EmbeddedStatementSlot;
    EXPECT_NE(c, e);
    EXPECT_EQ(c->Kind(), &Slots::Condition);
    EXPECT_EQ(e->Kind(), &Slots::EmbeddedStatement);
    EXPECT_FALSE(c->IsOptional());
    EXPECT_FALSE(e->IsOptional());
}

TEST(CSharp_WhileStatement, EmbeddedStatementKindIsSharedWithDoWhile) {
    // `WhileStatement` and `DoWhileStatement` share the `Slots::EmbeddedStatement` kind.
    EXPECT_EQ(WhileStatement::EmbeddedStatementSlot.Kind(),
              DoWhileStatement::EmbeddedStatementSlot.Kind());
}

// ==========================================================================
// DoWhileStatement (EmbeddedStatement-0/Condition-1, slot order reversed)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_DoWhileStatement, IsStatementAndAstNodeNotExpression) {
    DoWhileStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_DoWhileStatement, DoAndWhileKeywordConsts) {
    EXPECT_STREQ(DoWhileStatement::DoKeyword, "do");
    EXPECT_STREQ(DoWhileStatement::WhileKeyword, "while");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_DoWhileStatement, DefaultCtor) {
    DoWhileStatement s;
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 2);
}

TEST(CSharp_DoWhileStatement, GeneratedCtorSetsAndParentsInDeclarationOrder) {
    // The generated all-params ctor takes (Statement embeddedStatement, Expression condition)
    // -- the source declaration order (EmbeddedStatement first).
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 0);
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 1);
}

TEST(CSharp_DoWhileStatement, HandWrittenCtorSetsAndParentsInReversedOrder) {
    // The hand-written convenience ctor takes (Expression condition, Statement embeddedStatement)
    // -- the param order reversed relative to the generated ctor. The slots land at the same
    // flattened indices regardless of the assignment order (the const-index SetChildNode).
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    DoWhileStatement s(cond.get(), body.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 1);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_DoWhileStatement, EmbeddedStatementSetterFillsAndParents) {
    DoWhileStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 0);
}

TEST(CSharp_DoWhileStatement, ConditionSetterFillsAndParents) {
    DoWhileStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(cond->ChildIndex, 1);
}

TEST(CSharp_DoWhileStatement, GetChildReturnsSlotsInDeclarationOrder) {
    DoWhileStatement s;
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    s.EmbeddedStatement(body.get());
    s.Condition(cond.get());
    EXPECT_EQ(s.GetChild(0), body.get());  // EmbeddedStatement at index 0
    EXPECT_EQ(s.GetChild(1), cond.get());  // Condition at index 1
    EXPECT_THROW(s.GetChild(2), std::out_of_range);
}

TEST(CSharp_DoWhileStatement, SetChildRoutesToSlots) {
    DoWhileStatement s;
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, body.get());
    s.SetChild(1, cond.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_THROW(s.SetChild(2, nullptr), std::out_of_range);
}

TEST(CSharp_DoWhileStatement, GetChildSlotInfoReturnsSlotStatics) {
    DoWhileStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &DoWhileStatement::EmbeddedStatementSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &DoWhileStatement::ConditionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_DoWhileStatement, SlotKindPointsAtSharedSlotsConstant) {
    DoWhileStatement s;
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    s.EmbeddedStatement(body.get());
    s.Condition(cond.get());
    ASSERT_EQ(body->Slot(), &DoWhileStatement::EmbeddedStatementSlot);
    EXPECT_EQ(body->Slot()->Kind(), &Slots::EmbeddedStatement);
    ASSERT_EQ(cond->Slot(), &DoWhileStatement::ConditionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Condition);
}

TEST(CSharp_DoWhileStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(DoWhileStatement::EmbeddedStatementSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    ContinueStatement cs;
    EXPECT_TRUE(DoWhileStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoWhileStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(DoWhileStatement::ConditionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(DoWhileStatement::ConditionSlot.IsInstanceOfType(&cs));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_DoWhileStatement, AcceptVisitorDispatchesToVisit) {
    DoWhileStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "dowhile");
}

TEST(CSharp_DoWhileStatement, AcceptVisitorIsVirtual) {
    DoWhileStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "dowhile");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "dowhile");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_DoWhileStatement, DepthFirstWalksChildrenInDeclarationOrder) {
    // The slot order is EmbeddedStatement-0/Condition-1, so the depth-first walk visits the body
    // before the condition (the reverse of WhileStatement's Condition-then-body).
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "dowhile");
    EXPECT_EQ(v.trace[1], "continue");
    EXPECT_EQ(v.trace[2], "null");
}

TEST(CSharp_DoWhileStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    DoWhileStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "dowhile");
}

// ---- DoMatch (the generated MatchRequired x2 match, declaration order) --

TEST(CSharp_DoWhileStatement, DoMatchMatchesSameValues) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    auto bBody = std::make_unique<ContinueStatement>();
    auto bCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement b(bBody.get(), bCond.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DoWhileStatement, DoMatchDifferentEmbeddedStatementRejects) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    auto bBody = std::make_unique<BreakStatement>();
    auto bCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement b(bBody.get(), bCond.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DoWhileStatement, DoMatchDifferentConditionRejects) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    auto bBody = std::make_unique<ContinueStatement>();
    auto bCond = std::make_unique<ThisReferenceExpression>();
    DoWhileStatement b(bBody.get(), bCond.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DoWhileStatement, DoMatchRejectsWhileStatementSibling) {
    // A `DoWhileStatement` and a `WhileStatement` are disjoint concrete types even though they
    // share the same two slot kinds (`Condition`/`EmbeddedStatement`); the `other is
    // DoWhileStatement` type-check gate rejects.
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DoWhileStatement, DoMatchRejectsIfElseStatementSibling) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bThen = std::make_unique<ContinueStatement>();
    IfElseStatement b(bCond.get(), bThen.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DoWhileStatement, DoMatchRejectsDifferentType) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_DoWhileStatement, DoMatchRejectsNullCandidate) {
    auto aBody = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement a(aBody.get(), aCond.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_DoWhileStatement, CloneDeepCopiesChildren) {
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    std::unique_ptr<DoWhileStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->EmbeddedStatement()->ChildIndex, 0);
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    EXPECT_EQ(copy->Condition()->ChildIndex, 1);
}

TEST(CSharp_DoWhileStatement, CloneEmptyNodeHasNoChildren) {
    DoWhileStatement s;
    std::unique_ptr<DoWhileStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
}

TEST(CSharp_DoWhileStatement, CloneIsVirtualAndCovariant) {
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<DoWhileStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<DoWhileStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_DoWhileStatement, CloneDoesNotDetachSource) {
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    std::unique_ptr<DoWhileStatement> copy(s.Clone());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(cond->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_DoWhileStatement, CheckInvariantPassesOnFilledNode) {
    auto body = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    DoWhileStatement s(body.get(), cond.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_DoWhileStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* e = &DoWhileStatement::EmbeddedStatementSlot;
    const CSharpSlotInfo* c = &DoWhileStatement::ConditionSlot;
    EXPECT_NE(e, c);
    EXPECT_EQ(e->Kind(), &Slots::EmbeddedStatement);
    EXPECT_EQ(c->Kind(), &Slots::Condition);
    EXPECT_FALSE(e->IsOptional());
    EXPECT_FALSE(c->IsOptional());
}

TEST(CSharp_DoWhileStatement, SharesEmbeddedStatementKindWithWhileStatement) {
    EXPECT_EQ(DoWhileStatement::EmbeddedStatementSlot.Kind(),
              WhileStatement::EmbeddedStatementSlot.Kind());
}
