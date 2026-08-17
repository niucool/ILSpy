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

// Tests for the `CheckedExpression`/`UncheckedExpression` concrete node pair (cpp/.../
// Expressions/CheckedExpression.hpp + UncheckedExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/{Checked,Unchecked}Expression.cs) -- the
// next in-order Phase-5 piece per the D233 plan ("a natural pair to port together"). Both
// are the simplest slot-bearing shape (the ParenthesizedExpression shape): a single,
// REQUIRED (non-nullable) `Expression` child and no scalar member, each adding one
// `CheckedKeyword`/`UncheckedKeyword` const string. The two suites are kept in one file (the
// "natural pair" framing): they share the recording visitor and the `DoMatch` helper, and
// each suite is independently selectable (`--gtest_filter=CSharp_CheckedExpression.*` /
// `CSharp_UncheckedExpression.*`).
//
// Each suite exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over one required single slot at const index 0), the `Expression`
// accessor, the const-keyword string, the generated `DoMatch` over ONE NON-nullable recursive
// child (the direct dispatch, with the defensive null guard) -- the simplest slot-bearing
// `DoMatch`, with no `Any`-wildcard term -- the `Clone` deep-copy + re-parent, the
// `AcceptVisitor` dispatch, and the depth-first walk. The per-node slot REUSES the shared
// `Slots::Expression` kind -- no new `Slots.hpp` constant. The required-slot invariant means
// `CheckInvariant` is only exercised on a filled node (an empty node would assert "required
// slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `Visit` methods for the checked/unchecked
// pair and the leaf expressions they hold, recording a tag and recursing via `VisitChildren`
// (the inherited depth-first default). The trace is the visited nodes in pre-order
// (document order).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitCheckedExpression(CheckedExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("checked");
        VisitChildren(node);
    }
    void VisitUncheckedExpression(UncheckedExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("unchecked");
        VisitChildren(node);
    }
    void VisitParenthesizedExpression(ParenthesizedExpression* node) override {
        if (node == nullptr)
            return;
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

// =====================================================================
// CheckedExpression
// =====================================================================

// ---- Construction -----------------------------------------------------

// The default ctor: the `Expression` slot is null (no operand).
TEST(CSharp_CheckedExpression, DefaultCtor) {
    CheckedExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
}

// The all-params ctor sets `Expression`, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path for the single slot).
TEST(CSharp_CheckedExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    CheckedExpression e(operand.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Const keyword string -------------------------------------------

// The `CheckedKeyword` const string carries the `checked` keyword token the output visitor
// emits (CSharpOutputVisitor.VisitCheckedExpression calls WriteKeyword(CheckedExpression.
// CheckedKeyword)). It is a static literal, not instance state, so it is not part of the
// generated `DoMatch`.
TEST(CSharp_CheckedExpression, CheckedKeywordConst) {
    EXPECT_STREQ(CheckedExpression::CheckedKeyword, "checked");
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_CheckedExpression, ExpressionSetterFillsAndParents) {
    CheckedExpression e;
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
TEST(CSharp_CheckedExpression, NullSetterClearsSlot) {
    CheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection).
TEST(CSharp_CheckedExpression, GetChildCountIsOne) {
    CheckedExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns the slot at its flat index and throws out of range otherwise.
TEST(CSharp_CheckedExpression, GetChildReturnsSlot) {
    CheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.GetChild(0), a.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the slot setter.
TEST(CSharp_CheckedExpression, SetChildRoutesToSlot) {
    CheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static.
TEST(CSharp_CheckedExpression, GetChildSlotInfoReturnsSlotStatic) {
    CheckedExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &CheckedExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots::Expression` constant (the
// polymorphic position identity `node.Slot.Kind == Slots.Expression` relies on). This is
// the same kind `UnaryOperatorExpression`/`ParenthesizedExpression` registered, so the node
// types share the `Expression` operand position.
TEST(CSharp_CheckedExpression, SlotKindPointsAtSharedSlotsExpression) {
    CheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    ASSERT_EQ(a->Slot(), &CheckedExpression::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
    // The kind is shared with UnaryOperatorExpression's / ParenthesizedExpression's slot.
    EXPECT_EQ(CheckedExpression::ExpressionSlot.Kind(), &Slots::Expression);
}

// The slot static is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_CheckedExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(CheckedExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(CheckedExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitCheckedExpression`.
TEST(CSharp_CheckedExpression, AcceptVisitorDispatchesToVisit) {
    CheckedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_CheckedExpression, AcceptVisitorIsVirtual) {
    CheckedExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "checked");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the checked node, then its single child.
TEST(CSharp_CheckedExpression, DepthFirstWalksChildInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    CheckedExpression e(operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "checked");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no children records just the checked node.
TEST(CSharp_CheckedExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    CheckedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
}

// ---- DoMatch (the generated non-nullable-recursive match, one term) --

// Two nodes with the same-type child match.
TEST(CSharp_CheckedExpression, DoMatchMatchesSameChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates the child match to the child's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// child's type-only `DoMatch` fails).
TEST(CSharp_CheckedExpression, DoMatchRejectsDifferentChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    CheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Expression`; the port returns false
// instead of crashing).
TEST(CSharp_CheckedExpression, DoMatchNullPatternOperandRejects) {
    CheckedExpression a(nullptr);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects (the guard: the candidate's operand is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_CheckedExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());

    CheckedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as no-match
// (the C# would null-dereference; the port returns false).
TEST(CSharp_CheckedExpression, DoMatchBothOperandsNullRejects) {
    CheckedExpression a(nullptr);
    CheckedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `CheckedExpression`) rejects.
TEST(CSharp_CheckedExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_CheckedExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// A `CheckedExpression` and an `UncheckedExpression` are distinct node types: even with the
// same operand, the type check rejects (the generator's `other is CheckedExpression`).
TEST(CSharp_CheckedExpression, DoMatchRejectsUncheckedSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones the child (a distinct object, not shared), re-parents it to the copy,
// and assigns the flat index.
TEST(CSharp_CheckedExpression, CloneDeepClonesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    CheckedExpression e(operand.get());
    std::unique_ptr<CheckedExpression> copy(
        static_cast<CheckedExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

// `Clone` of a node with no child yields a node with the slot null (the `Clone` skips the
// child clone when the child is absent).
TEST(CSharp_CheckedExpression, CloneEmptyNodeHasNullSlot) {
    CheckedExpression e;
    std::unique_ptr<CheckedExpression> copy(
        static_cast<CheckedExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_CheckedExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    CheckedExpression e(operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<CheckedExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_CheckedExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    CheckedExpression e(operand.get());
    std::unique_ptr<CheckedExpression> copy(
        static_cast<CheckedExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `CheckedExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_CheckedExpression, IsAstNodeAndExpression) {
    CheckedExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the slot filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the slot is required, so `CheckInvariant` would assert "required
// slot must not be empty" on a node with a null child.
TEST(CSharp_CheckedExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    CheckedExpression e(operand.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// UncheckedExpression
// =====================================================================

// ---- Construction -----------------------------------------------------

// The default ctor: the `Expression` slot is null (no operand).
TEST(CSharp_UncheckedExpression, DefaultCtor) {
    UncheckedExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
}

// The all-params ctor sets `Expression`, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path for the single slot).
TEST(CSharp_UncheckedExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    UncheckedExpression e(operand.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Const keyword string -------------------------------------------

// The `UncheckedKeyword` const string carries the `unchecked` keyword token the output
// visitor emits (CSharpOutputVisitor.VisitUncheckedExpression calls WriteKeyword(
// UncheckedExpression.UncheckedKeyword)). It is a static literal, not instance state, so it
// is not part of the generated `DoMatch`.
TEST(CSharp_UncheckedExpression, UncheckedKeywordConst) {
    EXPECT_STREQ(UncheckedExpression::UncheckedKeyword, "unchecked");
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_UncheckedExpression, ExpressionSetterFillsAndParents) {
    UncheckedExpression e;
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
TEST(CSharp_UncheckedExpression, NullSetterClearsSlot) {
    UncheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection).
TEST(CSharp_UncheckedExpression, GetChildCountIsOne) {
    UncheckedExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns the slot at its flat index and throws out of range otherwise.
TEST(CSharp_UncheckedExpression, GetChildReturnsSlot) {
    UncheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.GetChild(0), a.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the slot setter.
TEST(CSharp_UncheckedExpression, SetChildRoutesToSlot) {
    UncheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static.
TEST(CSharp_UncheckedExpression, GetChildSlotInfoReturnsSlotStatic) {
    UncheckedExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &UncheckedExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots::Expression` constant (the
// polymorphic position identity `node.Slot.Kind == Slots.Expression` relies on). This is
// the same kind `UnaryOperatorExpression`/`ParenthesizedExpression`/`CheckedExpression`
// registered, so the node types share the `Expression` operand position.
TEST(CSharp_UncheckedExpression, SlotKindPointsAtSharedSlotsExpression) {
    UncheckedExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    ASSERT_EQ(a->Slot(), &UncheckedExpression::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
    EXPECT_EQ(UncheckedExpression::ExpressionSlot.Kind(), &Slots::Expression);
}

// The slot static is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_UncheckedExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(UncheckedExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(UncheckedExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitUncheckedExpression`.
TEST(CSharp_UncheckedExpression, AcceptVisitorDispatchesToVisit) {
    UncheckedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unchecked");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_UncheckedExpression, AcceptVisitorIsVirtual) {
    UncheckedExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unchecked");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "unchecked");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the unchecked node, then its single child.
TEST(CSharp_UncheckedExpression, DepthFirstWalksChildInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression e(operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "unchecked");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no children records just the unchecked node.
TEST(CSharp_UncheckedExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    UncheckedExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unchecked");
}

// ---- DoMatch (the generated non-nullable-recursive match, one term) --

// Two nodes with the same-type child match.
TEST(CSharp_UncheckedExpression, DoMatchMatchesSameChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates the child match to the child's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// child's type-only `DoMatch` fails).
TEST(CSharp_UncheckedExpression, DoMatchRejectsDifferentChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    UncheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Expression`; the port returns false
// instead of crashing).
TEST(CSharp_UncheckedExpression, DoMatchNullPatternOperandRejects) {
    UncheckedExpression a(nullptr);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects (the guard: the candidate's operand is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_UncheckedExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());

    UncheckedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as no-match
// (the C# would null-dereference; the port returns false).
TEST(CSharp_UncheckedExpression, DoMatchBothOperandsNullRejects) {
    UncheckedExpression a(nullptr);
    UncheckedExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not an `UncheckedExpression`) rejects.
TEST(CSharp_UncheckedExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_UncheckedExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// An `UncheckedExpression` and a `CheckedExpression` are distinct node types: even with the
// same operand, the type check rejects (the generator's `other is UncheckedExpression`).
TEST(CSharp_UncheckedExpression, DoMatchRejectsCheckedSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    CheckedExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones the child (a distinct object, not shared), re-parents it to the copy,
// and assigns the flat index.
TEST(CSharp_UncheckedExpression, CloneDeepClonesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression e(operand.get());
    std::unique_ptr<UncheckedExpression> copy(
        static_cast<UncheckedExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

// `Clone` of a node with no child yields a node with the slot null (the `Clone` skips the
// child clone when the child is absent).
TEST(CSharp_UncheckedExpression, CloneEmptyNodeHasNullSlot) {
    UncheckedExpression e;
    std::unique_ptr<UncheckedExpression> copy(
        static_cast<UncheckedExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_UncheckedExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression e(operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<UncheckedExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_UncheckedExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression e(operand.get());
    std::unique_ptr<UncheckedExpression> copy(
        static_cast<UncheckedExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `UncheckedExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_UncheckedExpression, IsAstNodeAndExpression) {
    UncheckedExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the slot filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the slot is required, so `CheckInvariant` would assert "required
// slot must not be empty" on a node with a null child.
TEST(CSharp_UncheckedExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UncheckedExpression e(operand.get());
    e.CheckInvariant();
    SUCCEED();
}
