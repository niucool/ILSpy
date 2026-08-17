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

// Tests for the `AssignmentExpression` concrete node (cpp/.../Expressions/
// AssignmentExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.cs) -- the second
// slot-bearing C# AST node (sharing the `BinaryOperatorExpression` two-single-slot shape).
// Exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over two single slots), the `Left`/`Right`/`Operator` accessors (incl.
// the `Assign` default and the `(left, right)` convenience ctor), the generated `DoMatch`
// over nullable recursive children (`MatchOptional`) plus the `Any`-wildcard enum term, the
// `Clone` deep-copy + re-parent, the `AcceptVisitor` dispatch, and the depth-first walk.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
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

// A recording depth-first visitor: overrides `VisitAssignmentExpression` (and the leaf
// expressions it holds), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAssignmentExpression(AssignmentExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("assign");
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

// The default ctor: `Left`/`Right` are null, `Operator` is `Assign` (the enum's zero value --
// the C# default for an uninitialized `AssignmentOperatorType` property, NOT `Any`).
TEST(CSharp_AssignmentExpression, DefaultCtor) {
    AssignmentExpression e;
    EXPECT_EQ(e.Left(), nullptr);
    EXPECT_EQ(e.Right(), nullptr);
    EXPECT_EQ(e.Operator(), AssignmentOperatorType::Assign);
}

// The hand-written `(left, right)` convenience ctor (the common `a = b` shape) sets
// `Left`/`Right` and defaults `Operator` to `Assign`, parenting the children.
TEST(CSharp_AssignmentExpression, ConvenienceCtorDefaultsOperatorToAssign) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    NullReferenceExpression* leftPtr = left.get();
    ThisReferenceExpression* rightPtr = right.get();
    AssignmentExpression e(left.get(), right.get());
    EXPECT_EQ(e.Left(), leftPtr);
    EXPECT_EQ(e.Right(), rightPtr);
    EXPECT_EQ(e.Operator(), AssignmentOperatorType::Assign);
    EXPECT_EQ(leftPtr->Parent(), &e);
    EXPECT_EQ(rightPtr->Parent(), &e);
    EXPECT_EQ(leftPtr->ChildIndex, 0);
    EXPECT_EQ(rightPtr->ChildIndex, 1);
}

// The all-params ctor sets `Left`/`Operator`/`Right` and parents the children.
TEST(CSharp_AssignmentExpression, AllParamsCtorSetsAndParents) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    NullReferenceExpression* leftPtr = left.get();
    ThisReferenceExpression* rightPtr = right.get();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, right.get());
    EXPECT_EQ(e.Left(), leftPtr);
    EXPECT_EQ(e.Right(), rightPtr);
    EXPECT_EQ(e.Operator(), AssignmentOperatorType::Add);
    EXPECT_EQ(leftPtr->Parent(), &e);
    EXPECT_EQ(rightPtr->Parent(), &e);
    EXPECT_EQ(leftPtr->ChildIndex, 0);
    EXPECT_EQ(rightPtr->ChildIndex, 1);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Left` setter fills the slot, parents the child, and assigns the flat index 0 directly
// (the const-index `SetChildNode` path).
TEST(CSharp_AssignmentExpression, LeftSetterFillsAndParents) {
    AssignmentExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Left(a.get());
    EXPECT_EQ(e.Left(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(e.ChildIndicesValid());
}

// The `Right` setter fills the slot at flat index 1.
TEST(CSharp_AssignmentExpression, RightSetterFillsAndParents) {
    AssignmentExpression e;
    auto a = std::make_unique<ThisReferenceExpression>();
    e.Right(a.get());
    EXPECT_EQ(e.Right(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_EQ(a->ChildIndex, 1);
    EXPECT_TRUE(e.ChildIndicesValid());
}

// `Left`/`Right` are nullable: a null setter clears the slot and detaches the old child.
TEST(CSharp_AssignmentExpression, NullSetterClearsSlot) {
    AssignmentExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Left(a.get());
    e.Left(nullptr);
    EXPECT_EQ(e.Left(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_AssignmentExpression, GetChildCountIsTwo) {
    AssignmentExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns `Left`/`Right` at indices 0/1 and throws out of range otherwise.
TEST(CSharp_AssignmentExpression, GetChildReturnsSlots) {
    AssignmentExpression e;
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    e.Left(left.get());
    e.Right(right.get());
    EXPECT_EQ(e.GetChild(0), left.get());
    EXPECT_EQ(e.GetChild(1), right.get());
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the `Left`/`Right` slot setters.
TEST(CSharp_AssignmentExpression, SetChildRoutesToSlots) {
    AssignmentExpression e;
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    e.SetChild(0, left.get());
    e.SetChild(1, right.get());
    EXPECT_EQ(e.Left(), left.get());
    EXPECT_EQ(e.Right(), right.get());
    EXPECT_EQ(left->Parent(), &e);
    EXPECT_EQ(right->Parent(), &e);
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node `LeftSlot`/`RightSlot` (pointing at the shared
// `Slots::Left`/`Slots::Right` kind).
TEST(CSharp_AssignmentExpression, GetChildSlotInfoReturnsSlotStatics) {
    AssignmentExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &AssignmentExpression::LeftSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &AssignmentExpression::RightSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic position
// identity `node.Slot.Kind == Slots.Left` relies on) -- the same `Slots::Left`/`Slots::Right`
// constants shared with `BinaryOperatorExpression`.
TEST(CSharp_AssignmentExpression, SlotKindPointsAtSharedSlotsConstant) {
    AssignmentExpression e;
    auto left = std::make_unique<NullReferenceExpression>();
    e.Left(left.get());
    ASSERT_EQ(left->Slot(), &AssignmentExpression::LeftSlot);
    EXPECT_EQ(left->Slot()->Kind(), &Slots::Left);
    auto right = std::make_unique<ThisReferenceExpression>();
    e.Right(right.get());
    ASSERT_EQ(right->Slot(), &AssignmentExpression::RightSlot);
    EXPECT_EQ(right->Slot()->Kind(), &Slots::Right);
}

// The `LeftSlot`/`RightSlot` are `Expression`-typed: an `Expression` is an instance, a null
// pointer is not.
TEST(CSharp_AssignmentExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(AssignmentExpression::LeftSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(AssignmentExpression::LeftSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAssignmentExpression`.
TEST(CSharp_AssignmentExpression, AcceptVisitorDispatchesToVisit) {
    AssignmentExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "assign");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_AssignmentExpression, AcceptVisitorIsVirtual) {
    AssignmentExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "assign");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "assign");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the assignment node, then its `Left` and `Right` operands in
// document order.
TEST(CSharp_AssignmentExpression, DepthFirstWalksOperandsInOrder) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, right.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "assign");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "this");
}

// A node with no operands records just the assignment node (no children to walk).
TEST(CSharp_AssignmentExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    AssignmentExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "assign");
}

// ---- DoMatch (the generated nullable-recursive + Any-wildcard match) --

// Two nodes with the same operator and same-type operands match.
TEST(CSharp_AssignmentExpression, DoMatchMatchesSameOperatorAndOperands) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a1(aLeft.get(), AssignmentOperatorType::Add, aRight.get());

    auto bLeft = std::make_unique<NullReferenceExpression>();
    auto bRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression b1(bLeft.get(), AssignmentOperatorType::Add, bRight.get());
    EXPECT_TRUE(DoMatchAgainst(&a1, &b1));
}

// Different operators reject (the `Operator` term fails).
TEST(CSharp_AssignmentExpression, DoMatchRejectsDifferentOperator) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a(aLeft.get(), AssignmentOperatorType::Add, aRight.get());

    auto bLeft = std::make_unique<NullReferenceExpression>();
    auto bRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression b(bLeft.get(), AssignmentOperatorType::Subtract, bRight.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The `Any` operator wildcard (the pattern's `Operator` is `Any`) matches any candidate
// operator.
TEST(CSharp_AssignmentExpression, DoMatchAnyOperatorWildcardMatchesAny) {
    auto pLeft = std::make_unique<NullReferenceExpression>();
    auto pRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression pattern(pLeft.get(), AssignmentOperatorType::Any, pRight.get());

    auto cLeft = std::make_unique<NullReferenceExpression>();
    auto cRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression candAdd(cLeft.get(), AssignmentOperatorType::Add, cRight.get());
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candAdd));

    auto dLeft = std::make_unique<NullReferenceExpression>();
    auto dRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression candMul(dLeft.get(), AssignmentOperatorType::Multiply, dRight.get());
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candMul));
}

// Both operands absent (null) match: `MatchOptional(null, null)` -> the candidate must be
// null too, which it is.
TEST(CSharp_AssignmentExpression, DoMatchBothOperandsAbsentMatches) {
    AssignmentExpression a(nullptr, AssignmentOperatorType::Add, nullptr);
    AssignmentExpression b(nullptr, AssignmentOperatorType::Add, nullptr);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A null pattern operand vs a present candidate operand rejects: `MatchOptional(null,
// non-null)` -> the candidate is not null, so the match fails.
TEST(CSharp_AssignmentExpression, DoMatchNullOperandRejectsPresentOperand) {
    AssignmentExpression a(nullptr, AssignmentOperatorType::Add, nullptr);
    auto bLeft = std::make_unique<NullReferenceExpression>();
    auto bRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression b(bLeft.get(), AssignmentOperatorType::Add, bRight.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A present pattern operand vs a null candidate operand rejects: the pattern operand's own
// `DoMatch(null)` decides (a `NullReferenceExpression` is not a null candidate), so the match
// fails.
TEST(CSharp_AssignmentExpression, DoMatchPresentOperandRejectsNullOperand) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a(aLeft.get(), AssignmentOperatorType::Add, aRight.get());
    AssignmentExpression b(nullptr, AssignmentOperatorType::Add, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive `MatchOptional` delegates the operand match to the operand's own `DoMatch`:
// a `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// operand's type-only `DoMatch` fails).
TEST(CSharp_AssignmentExpression, DoMatchOperandsDelegateToOperandDoMatch) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a(aLeft.get(), AssignmentOperatorType::Add, aRight.get());

    auto bLeft = std::make_unique<ThisReferenceExpression>();
    auto bRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression b(bLeft.get(), AssignmentOperatorType::Add, bRight.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not an `AssignmentExpression`) rejects.
TEST(CSharp_AssignmentExpression, DoMatchRejectsDifferentType) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a(aLeft.get(), AssignmentOperatorType::Add, aRight.get());

    auto bLeft = std::make_unique<NullReferenceExpression>();
    auto bRight = std::make_unique<ThisReferenceExpression>();
    BinaryOperatorExpression b(bLeft.get(), BinaryOperatorType::Add, bRight.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_AssignmentExpression, DoMatchRejectsNullCandidate) {
    auto aLeft = std::make_unique<NullReferenceExpression>();
    auto aRight = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression a(aLeft.get(), AssignmentOperatorType::Add, aRight.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` copies the scalar `Operator` and deep-clones the children (distinct objects, not
// shared), and re-parents the cloned children to the copy.
TEST(CSharp_AssignmentExpression, CloneCopiesOperatorAndDeepClonesChildren) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, right.get());
    std::unique_ptr<AssignmentExpression> copy(static_cast<AssignmentExpression*>(e.Clone()));
    EXPECT_EQ(copy->Operator(), AssignmentOperatorType::Add);
    ASSERT_NE(copy->Left(), nullptr);
    ASSERT_NE(copy->Right(), nullptr);
    EXPECT_NE(copy->Left(), left.get());
    EXPECT_NE(copy->Right(), right.get());
    EXPECT_EQ(copy->Left()->Parent(), copy.get());
    EXPECT_EQ(copy->Right()->Parent(), copy.get());
    EXPECT_EQ(copy->Left()->ChildIndex, 0);
    EXPECT_EQ(copy->Right()->ChildIndex, 1);
}

// `Clone` of a node with no operands copies the `Operator` and yields a node with null
// operands (the default `Assign` operator for a default-constructed node).
TEST(CSharp_AssignmentExpression, CloneEmptyNodeCopiesOperator) {
    AssignmentExpression e;
    e.Operator(AssignmentOperatorType::Multiply);
    std::unique_ptr<AssignmentExpression> copy(static_cast<AssignmentExpression*>(e.Clone()));
    EXPECT_EQ(copy->Operator(), AssignmentOperatorType::Multiply);
    EXPECT_EQ(copy->Left(), nullptr);
    EXPECT_EQ(copy->Right(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_AssignmentExpression, CloneIsVirtualAndCovariant) {
    auto left = std::make_unique<NullReferenceExpression>();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, nullptr);
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    // The cloned node is still an AssignmentExpression.
    EXPECT_NE(dynamic_cast<AssignmentExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_AssignmentExpression, CloneDoesNotDetachSource) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, right.get());
    std::unique_ptr<AssignmentExpression> copy(static_cast<AssignmentExpression*>(e.Clone()));
    EXPECT_EQ(left->Parent(), &e);
    EXPECT_EQ(right->Parent(), &e);
    EXPECT_EQ(e.Left(), left.get());
    EXPECT_EQ(e.Right(), right.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `AssignmentExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_AssignmentExpression, IsAstNodeAndExpression) {
    AssignmentExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with both operands filled (the
// slot-structure verifier finds every required slot filled and each child's
// Parent/ChildIndex consistent).
TEST(CSharp_AssignmentExpression, CheckInvariantPassesOnFilledNode) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    AssignmentExpression e(left.get(), AssignmentOperatorType::Add, right.get());
    e.CheckInvariant();
    SUCCEED();
}

// The inherited `CheckInvariant` passes on an empty node (no required slots).
TEST(CSharp_AssignmentExpression, CheckInvariantPassesOnEmptyNode) {
    AssignmentExpression e;
    e.CheckInvariant();
    SUCCEED();
}
