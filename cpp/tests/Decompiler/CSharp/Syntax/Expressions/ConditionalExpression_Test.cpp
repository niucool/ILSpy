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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `ConditionalExpression` concrete node (cpp/.../Expressions/
// ConditionalExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.cs) -- the first
// ported node with MORE THAN ONE required (non-nullable) child slot. Exercises the
// slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` over three
// required single slots at const indices), the `Condition`/`TrueExpression`/`FalseExpression`
// accessors, the generated `DoMatch` over THREE NON-nullable recursive children (the direct
// dispatch, with the defensive null guard) -- the first node to exercise it three times in
// one node -- the `Clone` deep-copy + re-parent of three children, the `AcceptVisitor`
// dispatch, and the depth-first walk. There is no scalar enum, so `DoMatch` has no
// `Any`-wildcard term. The required-slot invariant means `CheckInvariant` is only exercised
// on a filled node (an empty node would assert "required slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitConditionalExpression` (and the leaf
// expressions it holds), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order (document order).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitConditionalExpression(ConditionalExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("cond");
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
    void VisitBaseReferenceExpression(BaseReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("base");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the
// D220 pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ---- Construction -----------------------------------------------------

// The default ctor: all three slots are null (no operands).
TEST(CSharp_ConditionalExpression, DefaultCtor) {
    ConditionalExpression e;
    EXPECT_EQ(e.Condition(), nullptr);
    EXPECT_EQ(e.TrueExpression(), nullptr);
    EXPECT_EQ(e.FalseExpression(), nullptr);
}

// The all-params ctor sets all three slots, parents each child, and assigns the flat indices
// 0/1/2 directly (the const-index `SetChildNode` path for each single slot).
TEST(CSharp_ConditionalExpression, AllParamsCtorSetsAndParents) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    NullReferenceExpression* condPtr = cond.get();
    ThisReferenceExpression* truePtr = trueExpr.get();
    BaseReferenceExpression* falsePtr = falseExpr.get();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    EXPECT_EQ(e.Condition(), condPtr);
    EXPECT_EQ(e.TrueExpression(), truePtr);
    EXPECT_EQ(e.FalseExpression(), falsePtr);
    EXPECT_EQ(condPtr->Parent(), &e);
    EXPECT_EQ(condPtr->ChildIndex, 0);
    EXPECT_EQ(truePtr->Parent(), &e);
    EXPECT_EQ(truePtr->ChildIndex, 1);
    EXPECT_EQ(falsePtr->Parent(), &e);
    EXPECT_EQ(falsePtr->ChildIndex, 2);
}

// ---- Accessors + slot storage -----------------------------------------

// Each setter fills its slot, parents the child, and assigns the flat index directly (the
// const-index `SetChildNode` path).
TEST(CSharp_ConditionalExpression, SettersFillAndParent) {
    ConditionalExpression e;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    e.Condition(cond.get());
    EXPECT_EQ(cond->Parent(), &e);
    EXPECT_EQ(cond->ChildIndex, 0);
    e.TrueExpression(trueExpr.get());
    EXPECT_EQ(trueExpr->Parent(), &e);
    EXPECT_EQ(trueExpr->ChildIndex, 1);
    e.FalseExpression(falseExpr.get());
    EXPECT_EQ(falseExpr->Parent(), &e);
    EXPECT_EQ(falseExpr->ChildIndex, 2);
    EXPECT_TRUE(e.ChildIndicesValid());
}

// A null setter clears a slot and detaches the old child (the slot is required, but the
// setter still tolerates a null to detach -- the invariant is enforced by `CheckInvariant`,
// not by the setter).
TEST(CSharp_ConditionalExpression, NullSetterClearsSlot) {
    ConditionalExpression e;
    auto cond = std::make_unique<NullReferenceExpression>();
    e.Condition(cond.get());
    e.Condition(nullptr);
    EXPECT_EQ(e.Condition(), nullptr);
    EXPECT_EQ(cond->Parent(), nullptr);
    EXPECT_EQ(cond->ChildIndex, -1);
}

// `GetChildCount` is the constant 3 (three single slots, no collection).
TEST(CSharp_ConditionalExpression, GetChildCountIsThree) {
    ConditionalExpression e;
    EXPECT_EQ(e.GetChildCount(), 3);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise.
TEST(CSharp_ConditionalExpression, GetChildReturnsSlots) {
    ConditionalExpression e;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    e.Condition(cond.get());
    e.TrueExpression(trueExpr.get());
    e.FalseExpression(falseExpr.get());
    EXPECT_EQ(e.GetChild(0), cond.get());
    EXPECT_EQ(e.GetChild(1), trueExpr.get());
    EXPECT_EQ(e.GetChild(2), falseExpr.get());
    EXPECT_THROW(e.GetChild(3), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_ConditionalExpression, SetChildRoutesToSlots) {
    ConditionalExpression e;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    e.SetChild(0, cond.get());
    EXPECT_EQ(e.Condition(), cond.get());
    EXPECT_EQ(cond->Parent(), &e);
    e.SetChild(1, trueExpr.get());
    EXPECT_EQ(e.TrueExpression(), trueExpr.get());
    e.SetChild(2, falseExpr.get());
    EXPECT_EQ(e.FalseExpression(), falseExpr.get());
    EXPECT_THROW(e.SetChild(3, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_ConditionalExpression, GetChildSlotInfoReturnsSlotStatics) {
    ConditionalExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &ConditionalExpression::ConditionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &ConditionalExpression::TrueExpressionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(2), &ConditionalExpression::FalseExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(3), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic
// position identity `node.Slot.Kind == Slots.X` relies on). `Condition` is shared with the
// statement nodes; `True`/`False` are unique to `ConditionalExpression`.
TEST(CSharp_ConditionalExpression, SlotKindsPointAtSharedSlotsConstants) {
    ConditionalExpression e;
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    e.Condition(cond.get());
    e.TrueExpression(trueExpr.get());
    e.FalseExpression(falseExpr.get());
    ASSERT_EQ(cond->Slot(), &ConditionalExpression::ConditionSlot);
    EXPECT_EQ(cond->Slot()->Kind(), &Slots::Condition);
    ASSERT_EQ(trueExpr->Slot(), &ConditionalExpression::TrueExpressionSlot);
    EXPECT_EQ(trueExpr->Slot()->Kind(), &Slots::True);
    ASSERT_EQ(falseExpr->Slot(), &ConditionalExpression::FalseExpressionSlot);
    EXPECT_EQ(falseExpr->Slot()->Kind(), &Slots::False);
}

// The slot statics are `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_ConditionalExpression, SlotsAreInstanceOfExpression) {
    EXPECT_FALSE(ConditionalExpression::ConditionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(ConditionalExpression::TrueExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(ConditionalExpression::FalseExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ConditionalExpression::ConditionSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(ConditionalExpression::TrueExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(ConditionalExpression::FalseExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitConditionalExpression`.
TEST(CSharp_ConditionalExpression, AcceptVisitorDispatchesToVisit) {
    ConditionalExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cond");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_ConditionalExpression, AcceptVisitorIsVirtual) {
    ConditionalExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cond");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "cond");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the conditional node, then its three children in document
// order (Condition, TrueExpression, FalseExpression).
TEST(CSharp_ConditionalExpression, DepthFirstWalksChildrenInOrder) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 4u);
    EXPECT_EQ(v.trace[0], "cond");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "this");
    EXPECT_EQ(v.trace[3], "base");
}

// A node with no children records just the conditional node.
TEST(CSharp_ConditionalExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ConditionalExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cond");
}

// ---- DoMatch (the generated non-nullable-recursive match, three terms) --

// Two nodes with the same-type children in all three slots match.
TEST(CSharp_ConditionalExpression, DoMatchMatchesSameChildren) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Condition` slot (the first term) rejects.
TEST(CSharp_ConditionalExpression, DoMatchRejectsDifferentCondition) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<ThisReferenceExpression>(); // different condition type
    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `TrueExpression` slot (the second term) rejects.
TEST(CSharp_ConditionalExpression, DoMatchRejectsDifferentTrueExpression) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bTrue = std::make_unique<BaseReferenceExpression>(); // different true arm type
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `FalseExpression` slot (the third term) rejects.
TEST(CSharp_ConditionalExpression, DoMatchRejectsDifferentFalseExpression) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<ThisReferenceExpression>(); // different false arm type
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern `Condition` rejects: the port guards the non-nullable recursive dispatch
// against a missing child (the C# would null-dereference `this.Condition`; the port returns
// false instead of crashing).
TEST(CSharp_ConditionalExpression, DoMatchNullPatternConditionRejects) {
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(nullptr, aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<NullReferenceExpression>();
    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Condition` rejects (the guard: the candidate's `Condition` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_ConditionalExpression, DoMatchNullCandidateConditionRejects) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(nullptr, bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// All children absent (null) on both sides reject: the guard treats a missing required
// child as no-match (the C# would null-dereference; the port returns false).
TEST(CSharp_ConditionalExpression, DoMatchAllChildrenNullRejects) {
    ConditionalExpression a(nullptr, nullptr, nullptr);
    ConditionalExpression b(nullptr, nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: a
// `NullReferenceExpression` condition vs a `ThisReferenceExpression` condition rejects (the
// child's type-only `DoMatch` fails), even when the other two slots match.
TEST(CSharp_ConditionalExpression, DoMatchDelegatesToChildDoMatch) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());

    auto bCond = std::make_unique<ThisReferenceExpression>(); // child type mismatch
    auto bTrue = std::make_unique<ThisReferenceExpression>();
    auto bFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression b(bCond.get(), bTrue.get(), bFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `ConditionalExpression`) rejects.
TEST(CSharp_ConditionalExpression, DoMatchRejectsDifferentType) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_ConditionalExpression, DoMatchRejectsNullCandidate) {
    auto aCond = std::make_unique<NullReferenceExpression>();
    auto aTrue = std::make_unique<ThisReferenceExpression>();
    auto aFalse = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression a(aCond.get(), aTrue.get(), aFalse.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones all three children (distinct objects, not shared), re-parents each to
// the copy, and assigns the flat indices.
TEST(CSharp_ConditionalExpression, CloneDeepClonesAllChildren) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    std::unique_ptr<ConditionalExpression> copy(static_cast<ConditionalExpression*>(e.Clone()));
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    EXPECT_EQ(copy->Condition()->ChildIndex, 0);
    ASSERT_NE(copy->TrueExpression(), nullptr);
    EXPECT_NE(copy->TrueExpression(), trueExpr.get());
    EXPECT_EQ(copy->TrueExpression()->Parent(), copy.get());
    EXPECT_EQ(copy->TrueExpression()->ChildIndex, 1);
    ASSERT_NE(copy->FalseExpression(), nullptr);
    EXPECT_NE(copy->FalseExpression(), falseExpr.get());
    EXPECT_EQ(copy->FalseExpression()->Parent(), copy.get());
    EXPECT_EQ(copy->FalseExpression()->ChildIndex, 2);
}

// `Clone` of a node with no children yields a node with all slots null (the `Clone` skips a
// child clone when the child is absent).
TEST(CSharp_ConditionalExpression, CloneEmptyNodeHasNullSlots) {
    ConditionalExpression e;
    std::unique_ptr<ConditionalExpression> copy(static_cast<ConditionalExpression*>(e.Clone()));
    EXPECT_EQ(copy->Condition(), nullptr);
    EXPECT_EQ(copy->TrueExpression(), nullptr);
    EXPECT_EQ(copy->FalseExpression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_ConditionalExpression, CloneIsVirtualAndCovariant) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ConditionalExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_ConditionalExpression, CloneDoesNotDetachSource) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    std::unique_ptr<ConditionalExpression> copy(static_cast<ConditionalExpression*>(e.Clone()));
    EXPECT_EQ(cond->Parent(), &e);
    EXPECT_EQ(e.Condition(), cond.get());
    EXPECT_EQ(e.TrueExpression(), trueExpr.get());
    EXPECT_EQ(e.FalseExpression(), falseExpr.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `ConditionalExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the
// slot system and the annotation channel use).
TEST(CSharp_ConditionalExpression, IsAstNodeAndExpression) {
    ConditionalExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with all three slots filled (the
// slot-structure verifier finds each required slot filled and each child's Parent/ChildIndex
// consistent). An empty node is NOT tested: every slot is required, so `CheckInvariant` would
// assert "required slot must not be empty" on a node with a null child.
TEST(CSharp_ConditionalExpression, CheckInvariantPassesOnFilledNode) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto trueExpr = std::make_unique<ThisReferenceExpression>();
    auto falseExpr = std::make_unique<BaseReferenceExpression>();
    ConditionalExpression e(cond.get(), trueExpr.get(), falseExpr.get());
    e.CheckInvariant();
    SUCCEED();
}
