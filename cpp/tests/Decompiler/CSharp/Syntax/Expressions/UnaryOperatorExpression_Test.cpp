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

// Tests for the `UnaryOperatorExpression` concrete node (cpp/.../Expressions/
// UnaryOperatorExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.cs) -- the
// first ported node with a REQUIRED (non-nullable) child slot. Exercises the slot-storage
// contract (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` over a single required
// slot), the `Expression`/`Operator` accessors, the generated `DoMatch` over a NON-nullable
// recursive child (the direct dispatch, with the defensive null guard) plus the
// `Any`-wildcard enum term, the `Clone` deep-copy + re-parent, the `AcceptVisitor`
// dispatch, and the depth-first walk. The required-slot invariant means `CheckInvariant` is
// only exercised on a filled node (an empty node would assert "required slot must not be
// empty").

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
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitUnaryOperatorExpression` (and the leaf
// expressions it holds), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitUnaryOperatorExpression(UnaryOperatorExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("unary");
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
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the
// D220 pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ---- Construction -----------------------------------------------------

// The default ctor: `Expression` is null, `Operator` is `Any` (the enum's zero value, the
// C# default -- unlike `AssignmentOperatorType` whose zero is `Assign`).
TEST(CSharp_UnaryOperatorExpression, DefaultCtor) {
    UnaryOperatorExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.Operator(), UnaryOperatorType::Any);
}

// The all-params ctor sets `Expression`/`Operator` and parents the child.
TEST(CSharp_UnaryOperatorExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(e.Operator(), UnaryOperatorType::Not);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_UnaryOperatorExpression, ExpressionSetterFillsAndParents) {
    UnaryOperatorExpression e;
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
TEST(CSharp_UnaryOperatorExpression, NullSetterClearsSlot) {
    UnaryOperatorExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection).
TEST(CSharp_UnaryOperatorExpression, GetChildCountIsOne) {
    UnaryOperatorExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns `Expression` at index 0 and throws out of range otherwise.
TEST(CSharp_UnaryOperatorExpression, GetChildReturnsSlot) {
    UnaryOperatorExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    EXPECT_EQ(e.GetChild(0), operand.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the `Expression` slot setter.
TEST(CSharp_UnaryOperatorExpression, SetChildRoutesToSlot) {
    UnaryOperatorExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node `ExpressionSlot` (pointing at the shared
// `Slots::Expression` kind).
TEST(CSharp_UnaryOperatorExpression, GetChildSlotInfoReturnsSlotStatic) {
    UnaryOperatorExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &UnaryOperatorExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic
// position identity `node.Slot.Kind == Slots.Expression` relies on).
TEST(CSharp_UnaryOperatorExpression, SlotKindPointsAtSharedSlotsConstant) {
    UnaryOperatorExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    ASSERT_EQ(operand->Slot(), &UnaryOperatorExpression::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

// The `ExpressionSlot` is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_UnaryOperatorExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(UnaryOperatorExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(UnaryOperatorExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitUnaryOperatorExpression`.
TEST(CSharp_UnaryOperatorExpression, AcceptVisitorDispatchesToVisit) {
    UnaryOperatorExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unary");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to
// the concrete override.
TEST(CSharp_UnaryOperatorExpression, AcceptVisitorIsVirtual) {
    UnaryOperatorExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unary");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "unary");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the unary node, then its `Expression` operand.
TEST(CSharp_UnaryOperatorExpression, DepthFirstWalksOperandInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "unary");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no operand records just the unary node (no children to walk).
TEST(CSharp_UnaryOperatorExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    UnaryOperatorExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unary");
}

// ---- DoMatch (the generated non-nullable-recursive + Any-wildcard match) --

// Two nodes with the same operator and same-type operand match.
TEST(CSharp_UnaryOperatorExpression, DoMatchMatchesSameOperatorAndOperand) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression b(bOperand.get(), UnaryOperatorType::Not);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Different operators reject (the `Operator` term fails).
TEST(CSharp_UnaryOperatorExpression, DoMatchRejectsDifferentOperator) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression b(bOperand.get(), UnaryOperatorType::BitNot);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The `Any` operator wildcard (the pattern's `Operator` is `Any`) matches any candidate
// operator. (The default-constructed node's `Operator` IS `Any`, so it is the wildcard
// pattern -- unlike `AssignmentExpression` whose default `Operator` is `Assign`.)
TEST(CSharp_UnaryOperatorExpression, DoMatchAnyOperatorWildcardMatchesAny) {
    auto pOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression pattern(pOperand.get(), UnaryOperatorType::Any);

    auto cOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression candNot(cOperand.get(), UnaryOperatorType::Not);
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candNot));

    auto dOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression candMinus(dOperand.get(), UnaryOperatorType::Minus);
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candMinus));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch
// against a missing operand (the C# would null-dereference `this.Expression`; the port
// returns false instead of crashing).
TEST(CSharp_UnaryOperatorExpression, DoMatchNullPatternOperandRejects) {
    UnaryOperatorExpression a(nullptr, UnaryOperatorType::Not);
    auto bOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression b(bOperand.get(), UnaryOperatorType::Not);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects: the guard (the candidate's `Expression` is null).
TEST(CSharp_UnaryOperatorExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);
    UnaryOperatorExpression b(nullptr, UnaryOperatorType::Not);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as
// no-match (the C# would null-dereference; the port returns false for both sides null).
TEST(CSharp_UnaryOperatorExpression, DoMatchBothOperandsNullRejects) {
    UnaryOperatorExpression a(nullptr, UnaryOperatorType::Not);
    UnaryOperatorExpression b(nullptr, UnaryOperatorType::Not);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates the operand match to the operand's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// operand's type-only `DoMatch` fails).
TEST(CSharp_UnaryOperatorExpression, DoMatchOperandDelegatesToOperandDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    UnaryOperatorExpression b(bOperand.get(), UnaryOperatorType::Not);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `UnaryOperatorExpression`) rejects.
TEST(CSharp_UnaryOperatorExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_UnaryOperatorExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression a(aOperand.get(), UnaryOperatorType::Not);
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` copies the scalar `Operator` and deep-clones the child (a distinct object, not
// shared), and re-parents the cloned child to the copy.
TEST(CSharp_UnaryOperatorExpression, CloneCopiesOperatorAndDeepClonesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    std::unique_ptr<UnaryOperatorExpression> copy(static_cast<UnaryOperatorExpression*>(e.Clone()));
    EXPECT_EQ(copy->Operator(), UnaryOperatorType::Not);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

// `Clone` of a node with no operand copies the `Operator` and yields a node with a null
// operand (the `Clone` skips the child clone when the operand is absent).
TEST(CSharp_UnaryOperatorExpression, CloneEmptyNodeCopiesOperator) {
    UnaryOperatorExpression e;
    e.Operator(UnaryOperatorType::Minus);
    std::unique_ptr<UnaryOperatorExpression> copy(static_cast<UnaryOperatorExpression*>(e.Clone()));
    EXPECT_EQ(copy->Operator(), UnaryOperatorType::Minus);
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_UnaryOperatorExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<UnaryOperatorExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_UnaryOperatorExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    std::unique_ptr<UnaryOperatorExpression> copy(static_cast<UnaryOperatorExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `UnaryOperatorExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the
// slot system and the annotation channel use).
TEST(CSharp_UnaryOperatorExpression, IsAstNodeAndExpression) {
    UnaryOperatorExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the operand filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the `Expression` slot is required, so `CheckInvariant` would
// assert "required slot must not be empty" on a node with a null operand.
TEST(CSharp_UnaryOperatorExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    UnaryOperatorExpression e(operand.get(), UnaryOperatorType::Not);
    e.CheckInvariant();
    SUCCEED();
}
