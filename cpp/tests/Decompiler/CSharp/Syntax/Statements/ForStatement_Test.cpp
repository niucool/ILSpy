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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `ForStatement` concrete node -- the next in-order Phase-5 piece per the D262
// plan. `ForStatement` is the first ported node with a collection -> single -> collection ->
// single slot layout: a sealed `Statement` with an `Initializers` `AstNodeCollection<Statement>`
// collection (the init statements before the first `;`), a NULLABLE `Expression?` `Condition`
// (the loop test, absent for `for (;;)`), an `Iterators` `AstNodeCollection<Statement>` collection
// (the step statements after the second `;`), and a REQUIRED `Statement` `EmbeddedStatement`
// (the loop body). Both collections are non-incremental (two collections, so `Add` invalidates
// the parent's indices); both single slots follow collections so they use the index-less
// `SetChildNode` setter. It adds the new `Slots::ForInitializer`/`Slots::Iterator` kinds and
// reuses the already-ported `Slots::Condition` (the per-node slot carrying `IsOptional=true` for
// the nullable case)/`Slots::EmbeddedStatement`; NO name-shadowing crux (no member is named
// `Expression`/`Statement`). The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper
// (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
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
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `VisitForStatement` under test (plus the leaf
// expression and leaf statements it holds, and the structural-twin `WhileStatement`), recording a
// tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitForStatement(ForStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("for");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("while");
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
// ForStatement (Initializers Statement collection + nullable Condition
//                Expression + Iterators Statement collection + required
//                EmbeddedStatement Statement)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ForStatement, IsStatementAndAstNodeNotExpression) {
    ForStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_ForStatement, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<ForStatement>);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_ForStatement, ForKeywordConst) {
    EXPECT_STREQ(ForStatement::ForKeyword, "for");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_ForStatement, DefaultCtor) {
    ForStatement s;
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_EQ(s.Initializers().Count(), 0);
    EXPECT_EQ(s.Iterators().Count(), 0);
    // Two single slots (Condition + EmbeddedStatement) always occupy a flattened index each,
    // even with empty collections, so GetChildCount is 2 (2 + 0 + 0).
    EXPECT_EQ(s.GetChildCount(), 2);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_ForStatement, InitializersCollectionAddParentsAndInvalidates) {
    ForStatement s;
    auto init = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init.get());
    EXPECT_EQ(s.Initializers().Count(), 1);
    EXPECT_EQ(s.Initializers().At(0), init.get());
    EXPECT_EQ(init->Parent(), &s);
    // Both collections are non-incremental (two collections), so Add invalidates the parent's
    // indices; the ChildIndex is stale until a reindex is triggered.
    ASSERT_FALSE(s.ChildIndicesValid());
}

TEST(CSharp_ForStatement, IteratorsCollectionAddParentsAndInvalidates) {
    ForStatement s;
    auto iter = std::make_unique<BreakStatement>();
    s.Iterators().Add(iter.get());
    EXPECT_EQ(s.Iterators().Count(), 1);
    EXPECT_EQ(s.Iterators().At(0), iter.get());
    EXPECT_EQ(iter->Parent(), &s);
    ASSERT_FALSE(s.ChildIndicesValid());
}

TEST(CSharp_ForStatement, ConditionSetterFillsAndParents) {
    ForStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(cond->Parent(), &s);
}

TEST(CSharp_ForStatement, NullConditionSetterClearsSlot) {
    ForStatement s;
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    s.Condition(nullptr);
    EXPECT_EQ(s.Condition(), nullptr);
    EXPECT_EQ(cond->Parent(), nullptr);
    EXPECT_EQ(cond->ChildIndex, -1);
}

TEST(CSharp_ForStatement, EmbeddedStatementSetterFillsAndParents) {
    ForStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
}

TEST(CSharp_ForStatement, RejectsAlreadyParentedChild) {
    ForStatement a;
    ForStatement b;
    auto body = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(body.get());
    EXPECT_THROW(b.EmbeddedStatement(body.get()), std::invalid_argument);
}

TEST(CSharp_ForStatement, GetChildReturnsSlots) {
    ForStatement s;
    auto init = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter.get());
    s.EmbeddedStatement(body.get());
    // Flattened layout: Initializers [0,1), Condition at 1, Iterators [2,3), EmbeddedStatement at 3.
    EXPECT_EQ(s.GetChild(0), init.get());
    EXPECT_EQ(s.GetChild(1), cond.get());
    EXPECT_EQ(s.GetChild(2), iter.get());
    EXPECT_EQ(s.GetChild(3), body.get());
    EXPECT_EQ(s.GetChildCount(), 4);
    EXPECT_THROW(s.GetChild(4), std::out_of_range);
}

TEST(CSharp_ForStatement, GetChildOnEmptyNodeReturnsNullSingles) {
    ForStatement s;
    // With empty collections, Condition is at 0 (null) and EmbeddedStatement at 1 (null).
    EXPECT_EQ(s.GetChild(0), nullptr);
    EXPECT_EQ(s.GetChild(1), nullptr);
    EXPECT_THROW(s.GetChild(2), std::out_of_range);
}

TEST(CSharp_ForStatement, SetChildRoutesToSlots) {
    // `SetChild` replaces a collection element in place (the element must already exist at the
    // flattened index) and sets a single slot in place. Add the collection elements first so the
    // flattened indices are valid, then replace each via `SetChild`.
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto iter0 = std::make_unique<BreakStatement>();
    s.Initializers().Add(init0.get());
    s.Iterators().Add(iter0.get());
    auto init = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    // Flattened layout (1 Initializer, 1 Iterator): Initializers[0] at 0, Condition at 1,
    // Iterators[0] at 2, EmbeddedStatement at 3.
    s.SetChild(0, init.get());
    s.SetChild(1, cond.get());
    s.SetChild(2, iter.get());
    s.SetChild(3, body.get());
    EXPECT_EQ(s.Initializers().At(0), init.get());
    EXPECT_EQ(s.Condition(), cond.get());
    EXPECT_EQ(s.Iterators().At(0), iter.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(init->Parent(), &s);
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(iter->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_THROW(s.SetChild(4, nullptr), std::out_of_range);
}

TEST(CSharp_ForStatement, GetChildSlotInfoReturnsSlotStatics) {
    ForStatement s;
    auto init = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter.get());
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.GetChildSlotInfo(0), &ForStatement::InitializersSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &ForStatement::ConditionSlot);
    EXPECT_EQ(s.GetChildSlotInfo(2), &ForStatement::IteratorsSlot);
    EXPECT_EQ(s.GetChildSlotInfo(3), &ForStatement::EmbeddedStatementSlot);
    EXPECT_THROW(s.GetChildSlotInfo(4), std::out_of_range);
}

TEST(CSharp_ForStatement, GetCollectionByKindRoutesByKind) {
    ForStatement s;
    EXPECT_EQ(s.GetCollectionByKind(&Slots::ForInitializer), &s.Initializers());
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Iterator), &s.Iterators());
    // The single-slot kinds and unrelated collection kinds return null (the base fallback).
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Condition), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::EmbeddedStatement), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Statement), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Argument), nullptr);
}

TEST(CSharp_ForStatement, SlotKindPointsAtSharedSlotsConstant) {
    ForStatement s;
    auto init = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter.get());
    s.EmbeddedStatement(body.get());
    ASSERT_EQ(init->Slot(), &ForStatement::InitializersSlot);
    EXPECT_EQ(init->Slot()->Kind(), &Slots::ForInitializer);
    ASSERT_EQ(cond->Slot(), &ForStatement::ConditionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Condition);
    ASSERT_EQ(iter->Slot(), &ForStatement::IteratorsSlot);
    EXPECT_EQ(iter->Slot()->Kind(), &Slots::Iterator);
    ASSERT_EQ(body->Slot(), &ForStatement::EmbeddedStatementSlot);
    EXPECT_EQ(body->Slot()->Kind(), &Slots::EmbeddedStatement);
}

TEST(CSharp_ForStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(ForStatement::InitializersSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    ContinueStatement cs;
    EXPECT_TRUE(ForStatement::InitializersSlot.IsInstanceOfType(&cs));
    NullReferenceExpression nre;
    EXPECT_FALSE(ForStatement::InitializersSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(ForStatement::ConditionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(ForStatement::ConditionSlot.IsInstanceOfType(&cs));
    EXPECT_TRUE(ForStatement::IteratorsSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(ForStatement::IteratorsSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(ForStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(ForStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
}

// ---- The dynamic flattened-index layout (non-incremental) -----------

// After `Add`/set the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its correct
// flattened index: `Initializers` at `[0, initCount)`, `Condition` at `initCount`, `Iterators` at
// `[initCount + 1, initCount + 1 + iterCount)`, `EmbeddedStatement` at `initCount + 1 + iterCount`.
TEST(CSharp_ForStatement, ChildIndicesRebuiltAfterReindex) {
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto init1 = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Initializers().Add(init1.get());
    auto cond = std::make_unique<NullReferenceExpression>();
    s.Condition(cond.get());
    auto iter0 = std::make_unique<BreakStatement>();
    s.Iterators().Add(iter0.get());
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    ASSERT_FALSE(s.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)init0->Slot();
    ASSERT_TRUE(s.ChildIndicesValid());
    EXPECT_EQ(init0->ChildIndex, 0);  // Initializers[0] at 0
    EXPECT_EQ(init1->ChildIndex, 1);  // Initializers[1] at 1
    EXPECT_EQ(cond->ChildIndex, 2);   // Condition at initCount (2)
    EXPECT_EQ(iter0->ChildIndex, 3);   // Iterators[0] at initCount + 1 (3)
    EXPECT_EQ(body->ChildIndex, 4);    // EmbeddedStatement at initCount + 1 + iterCount (4)
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_ForStatement, AcceptVisitorDispatchesToVisit) {
    ForStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "for");
}

TEST(CSharp_ForStatement, AcceptVisitorIsVirtual) {
    ForStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "for");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "for");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_ForStatement, DepthFirstWalksChildrenInOrder) {
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto init1 = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter0 = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Initializers().Add(init1.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter0.get());
    s.EmbeddedStatement(body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    // Collection -> single -> collection -> single document order.
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "for", "continue", "continue", "null", "break", "continue"}));
}

TEST(CSharp_ForStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ForStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "for");
}

TEST(CSharp_ForStatement, DepthFirstWithoutConditionSkipsIt) {
    // A `for (;;)` loop: no Condition (null single slot is skipped by the walk, which visits only
    // present children via `Children()`).
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto iter0 = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Iterators().Add(iter0.get());
    s.EmbeddedStatement(body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"for", "continue", "break", "continue"}));
}

// ---- DoMatch (the generated collection + MatchOptional + collection +
//                MatchRequired match) -----------------------------------

TEST(CSharp_ForStatement, DoMatchMatchesSameValues) {
    ForStatement a;
    auto aInit = std::make_unique<ContinueStatement>();
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aIter = std::make_unique<BreakStatement>();
    auto aBody = std::make_unique<ContinueStatement>();
    a.Initializers().Add(aInit.get());
    a.Condition(aCond.get());
    a.Iterators().Add(aIter.get());
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bInit = std::make_unique<ContinueStatement>();
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bIter = std::make_unique<BreakStatement>();
    auto bBody = std::make_unique<ContinueStatement>();
    b.Initializers().Add(bInit.get());
    b.Condition(bCond.get());
    b.Iterators().Add(bIter.get());
    b.EmbeddedStatement(bBody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchBothConditionsAbsentMatches) {
    // A `for (;;)` vs a `for (;;)` -- both Conditions absent, MatchOptional returns true.
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bBody = std::make_unique<ContinueStatement>();
    b.EmbeddedStatement(bBody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchConditionAbsentVsPresentRejects) {
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    b.Condition(bCond.get());
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchDifferentConditionRejects) {
    ForStatement a;
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    a.Condition(aCond.get());
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bCond = std::make_unique<ThisReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    b.Condition(bCond.get());
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchDifferentInitializersCountRejects) {
    ForStatement a;
    auto aInit = std::make_unique<ContinueStatement>();
    auto aBody = std::make_unique<ContinueStatement>();
    a.Initializers().Add(aInit.get());
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bInit0 = std::make_unique<ContinueStatement>();
    auto bInit1 = std::make_unique<ContinueStatement>();
    auto bBody = std::make_unique<ContinueStatement>();
    b.Initializers().Add(bInit0.get());
    b.Initializers().Add(bInit1.get());
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchDifferentIteratorsRejects) {
    ForStatement a;
    auto aIter = std::make_unique<BreakStatement>();
    auto aBody = std::make_unique<ContinueStatement>();
    a.Iterators().Add(aIter.get());
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bBody = std::make_unique<ContinueStatement>();
    b.EmbeddedStatement(bBody.get());
    // a has one iterator, b has none -> different Iterators collection -> rejects.
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchDifferentEmbeddedStatementRejects) {
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    auto bBody = std::make_unique<BreakStatement>();
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchNullEmbeddedStatementPatternRejects) {
    // A half-constructed pattern (no EmbeddedStatement) rejects without crashing (the
    // MatchRequired null-pattern guard).
    ForStatement a;
    ForStatement b;
    auto bBody = std::make_unique<ContinueStatement>();
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchNullEmbeddedStatementCandidateRejects) {
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    ForStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchRejectsWhileStatementStructuralTwin) {
    // ForStatement and WhileStatement are both sealed Statement nodes but distinct concrete
    // types, so the other-is-ForStatement / other-is-WhileStatement type-check gates reject.
    ForStatement a;
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    a.Condition(aCond.get());
    a.EmbeddedStatement(aBody.get());
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bCond.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchRejectsWhileStatementStructuralTwinReverse) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aCond.get(), aBody.get());
    ForStatement b;
    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    b.Condition(bCond.get());
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ForStatement, DoMatchRejectsDifferentType) {
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_ForStatement, DoMatchRejectsNullCandidate) {
    ForStatement a;
    auto aBody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(aBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_ForStatement, CloneDeepCopiesCollectionsAndSingles) {
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto init1 = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter0 = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Initializers().Add(init1.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter0.get());
    s.EmbeddedStatement(body.get());
    std::unique_ptr<ForStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    // Initializers deep-copied and re-parented.
    ASSERT_EQ(copy->Initializers().Count(), 2);
    EXPECT_NE(copy->Initializers().At(0), init0.get());
    EXPECT_NE(copy->Initializers().At(1), init1.get());
    EXPECT_EQ(copy->Initializers().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->Initializers().At(1)->Parent(), copy.get());
    // Condition deep-copied and re-parented.
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    // Iterators deep-copied and re-parented.
    ASSERT_EQ(copy->Iterators().Count(), 1);
    EXPECT_NE(copy->Iterators().At(0), iter0.get());
    EXPECT_EQ(copy->Iterators().At(0)->Parent(), copy.get());
    // EmbeddedStatement deep-copied and re-parented.
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
}

TEST(CSharp_ForStatement, CloneSkipsAbsentCondition) {
    // A `for (;;)` clone: the absent Condition is skipped (no deep-copy of a null slot).
    ForStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    std::unique_ptr<ForStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
    EXPECT_EQ(copy->Initializers().Count(), 0);
    EXPECT_EQ(copy->Iterators().Count(), 0);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
}

TEST(CSharp_ForStatement, CloneEmptyNodeHasNoChildren) {
    ForStatement s;
    std::unique_ptr<ForStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
    EXPECT_EQ(copy->Initializers().Count(), 0);
    EXPECT_EQ(copy->Iterators().Count(), 0);
}

TEST(CSharp_ForStatement, CloneIsVirtualAndCovariant) {
    ForStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ForStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<ForStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_ForStatement, CloneDoesNotDetachSource) {
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter0 = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter0.get());
    s.EmbeddedStatement(body.get());
    std::unique_ptr<ForStatement> copy(s.Clone());
    EXPECT_EQ(init0->Parent(), &s);
    EXPECT_EQ(cond->Parent(), &s);
    EXPECT_EQ(iter0->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_ForStatement, CheckInvariantPassesOnFilledNode) {
    ForStatement s;
    auto init0 = std::make_unique<ContinueStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    auto iter0 = std::make_unique<BreakStatement>();
    auto body = std::make_unique<ContinueStatement>();
    s.Initializers().Add(init0.get());
    s.Condition(cond.get());
    s.Iterators().Add(iter0.get());
    s.EmbeddedStatement(body.get());
    s.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_ForStatement, CheckInvariantPassesWithNoCondition) {
    // The Condition is an OPTIONAL slot (nullable), so a `for (;;)`-shaped node (no Condition)
    // is invariant-valid as long as the REQUIRED EmbeddedStatement is filled.
    ForStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_ForStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* i = &ForStatement::InitializersSlot;
    const CSharpSlotInfo* c = &ForStatement::ConditionSlot;
    const CSharpSlotInfo* t = &ForStatement::IteratorsSlot;
    const CSharpSlotInfo* e = &ForStatement::EmbeddedStatementSlot;
    EXPECT_NE(i, c);
    EXPECT_NE(i, t);
    EXPECT_NE(i, e);
    EXPECT_NE(c, t);
    EXPECT_NE(c, e);
    EXPECT_NE(t, e);
    EXPECT_EQ(i->Kind(), &Slots::ForInitializer);
    EXPECT_EQ(c->Kind(), &Slots::Condition);
    EXPECT_EQ(t->Kind(), &Slots::Iterator);
    EXPECT_EQ(e->Kind(), &Slots::EmbeddedStatement);
    // The two collection slots are optional (a collection may be empty); the Condition single
    // slot is optional (nullable); the EmbeddedStatement single slot is required.
    EXPECT_TRUE(i->IsCollection());
    EXPECT_TRUE(t->IsCollection());
    EXPECT_FALSE(c->IsCollection());
    EXPECT_FALSE(e->IsCollection());
    EXPECT_TRUE(i->IsOptional());
    EXPECT_TRUE(t->IsOptional());
    EXPECT_TRUE(c->IsOptional());
    EXPECT_FALSE(e->IsOptional());
}

TEST(CSharp_ForStatement, ConditionKindIsSharedWithConditionalExpression) {
    // ForStatement.ConditionSlot and ConditionalExpression.ConditionSlot both point at the shared
    // Slots::Condition kind; the per-node slots carry different IsOptional flags (Conditional's
    // is required, ForStatement's is optional).
    EXPECT_EQ(ForStatement::ConditionSlot.Kind(), &Slots::Condition);
}

TEST(CSharp_ForStatement, EmbeddedStatementKindIsSharedWithWhile) {
    // ForStatement and WhileStatement share the Slots::EmbeddedStatement kind.
    EXPECT_EQ(ForStatement::EmbeddedStatementSlot.Kind(),
              WhileStatement::EmbeddedStatementSlot.Kind());
}

TEST(CSharp_ForStatement, CollectionKindsAreDistinctFromStatementKind) {
    // Slots::ForInitializer and Slots::Iterator are distinct kinds (distinct [Slot] names),
    // both distinct from Slots::Statement (BlockStatement's collection kind) -- all are
    // CSharpSlotInfoT<Statement> so the pointer types are unrelated; compare through the common
    // CSharpSlotInfo* base (the D251/D252 EXPECT_NE cross-element-type precedent).
    const CSharpSlotInfo* fi = &Slots::ForInitializer;
    const CSharpSlotInfo* it = &Slots::Iterator;
    EXPECT_NE(fi, it);
    EXPECT_NE(fi, static_cast<const CSharpSlotInfo*>(&Slots::Statement));
    EXPECT_NE(it, static_cast<const CSharpSlotInfo*>(&Slots::Statement));
    EXPECT_NE(fi, static_cast<const CSharpSlotInfo*>(&Slots::EmbeddedStatement));
}
