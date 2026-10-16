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

// Tests for the `ParenthesizedExpression` concrete node (cpp/.../Expressions/
// ParenthesizedExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.cs) -- the
// simplest slot-bearing shape: a single, REQUIRED (non-nullable) `Expression` child and no
// scalar member. Exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over one required single slot at const index 0), the `Expression`
// accessor, the generated `DoMatch` over ONE NON-nullable recursive child (the direct
// dispatch, with the defensive null guard) -- the simplest slot-bearing `DoMatch`, with no
// `Any`-wildcard term -- the `Clone` deep-copy + re-parent, the `AcceptVisitor` dispatch, and
// the depth-first walk. The per-node slot REUSES the shared `Slots::Expression` kind (the
// same kind `UnaryOperatorExpression` registered) -- no new `Slots.hpp` constant. The
// required-slot invariant means `CheckInvariant` is only exercised on a filled node (an empty
// node would assert "required slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitParenthesizedExpression` (and the leaf
// expressions it holds), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order (document order).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitParenthesizedExpression(ParenthesizedExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("paren");
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

// The default ctor: the `Expression` slot is null (no operand).
TEST(CSharp_ParenthesizedExpression, DefaultCtor) {
    ParenthesizedExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
}

// The all-params ctor sets `Expression`, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path for the single slot).
TEST(CSharp_ParenthesizedExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    ParenthesizedExpression e(operand.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_ParenthesizedExpression, ExpressionSetterFillsAndParents) {
    ParenthesizedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(e.ChildIndicesValid());
}

// A null setter clears the slot and detaches the old child (the slot is required, but the
// setter still tolerates a null to detach -- the invariant is enforced by `CheckInvariant`,
// not by the setter).
TEST(CSharp_ParenthesizedExpression, NullSetterClearsSlot) {
    ParenthesizedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection).
TEST(CSharp_ParenthesizedExpression, GetChildCountIsOne) {
    ParenthesizedExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns the slot at its flat index and throws out of range otherwise.
TEST(CSharp_ParenthesizedExpression, GetChildReturnsSlot) {
    ParenthesizedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.GetChild(0), a.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the slot setter.
TEST(CSharp_ParenthesizedExpression, SetChildRoutesToSlot) {
    ParenthesizedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static.
TEST(CSharp_ParenthesizedExpression, GetChildSlotInfoReturnsSlotStatic) {
    ParenthesizedExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &ParenthesizedExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots::Expression` constant (the
// polymorphic position identity `node.Slot.Kind == Slots.Expression` relies on). This is the
// same kind `UnaryOperatorExpression` registered, so the two node types share the
// `Expression` operand position.
TEST(CSharp_ParenthesizedExpression, SlotKindPointsAtSharedSlotsExpression) {
    ParenthesizedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    ASSERT_EQ(a->Slot(), &ParenthesizedExpression::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
    // The kind is shared with UnaryOperatorExpression's Expression slot.
    EXPECT_EQ(ParenthesizedExpression::ExpressionSlot.Kind(),
              &Slots::Expression);
}

// The slot static is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_ParenthesizedExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(ParenthesizedExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ParenthesizedExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitParenthesizedExpression`.
TEST(CSharp_ParenthesizedExpression, AcceptVisitorDispatchesToVisit) {
    ParenthesizedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "paren");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_ParenthesizedExpression, AcceptVisitorIsVirtual) {
    ParenthesizedExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "paren");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "paren");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the parenthesized node, then its single child.
TEST(CSharp_ParenthesizedExpression, DepthFirstWalksChildInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression e(operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "paren");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no children records just the parenthesized node.
TEST(CSharp_ParenthesizedExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ParenthesizedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "paren");
}

// ---- DoMatch (the generated non-nullable-recursive match, one term) --

// Two nodes with the same-type child match.
TEST(CSharp_ParenthesizedExpression, DoMatchMatchesSameChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates the child match to the child's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// child's type-only `DoMatch` fails).
TEST(CSharp_ParenthesizedExpression, DoMatchRejectsDifferentChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression a(aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    ParenthesizedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Expression`; the port returns false
// instead of crashing).
TEST(CSharp_ParenthesizedExpression, DoMatchNullPatternOperandRejects) {
    ParenthesizedExpression a(nullptr);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects (the guard: the candidate's operand is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_ParenthesizedExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression a(aOperand.get());

    ParenthesizedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as no-match
// (the C# would null-dereference; the port returns false).
TEST(CSharp_ParenthesizedExpression, DoMatchBothOperandsNullRejects) {
    ParenthesizedExpression a(nullptr);
    ParenthesizedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `ParenthesizedExpression`) rejects.
TEST(CSharp_ParenthesizedExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_ParenthesizedExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones the child (a distinct object, not shared), re-parents it to the copy,
// and assigns the flat index.
TEST(CSharp_ParenthesizedExpression, CloneDeepClonesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression e(operand.get());
    std::unique_ptr<ParenthesizedExpression> copy(
        static_cast<ParenthesizedExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

// `Clone` of a node with no child yields a node with the slot null (the `Clone` skips the
// child clone when the child is absent).
TEST(CSharp_ParenthesizedExpression, CloneEmptyNodeHasNullSlot) {
    ParenthesizedExpression e;
    std::unique_ptr<ParenthesizedExpression> copy(
        static_cast<ParenthesizedExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_ParenthesizedExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression e(operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ParenthesizedExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_ParenthesizedExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression e(operand.get());
    std::unique_ptr<ParenthesizedExpression> copy(
        static_cast<ParenthesizedExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `ParenthesizedExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the
// slot system and the annotation channel use).
TEST(CSharp_ParenthesizedExpression, IsAstNodeAndExpression) {
    ParenthesizedExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the slot filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the slot is required, so `CheckInvariant` would assert "required
// slot must not be empty" on a node with a null child.
TEST(CSharp_ParenthesizedExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ParenthesizedExpression e(operand.get());
    e.CheckInvariant();
    SUCCEED();
}
