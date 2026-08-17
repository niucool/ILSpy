// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to do in the Software
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

// Tests for the `DirectionExpression`/`ThrowExpression` concrete node pair (cpp/.../
// Expressions/DirectionExpression.hpp + ThrowExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/{Direction,Throw}Expression.cs) -- the
// next in-order Phase-5 piece per the D234 plan ("the other one-required-Expression-slot
// nodes still unported ... if they share the shape"). Both are the one-required-Expression-
// slot shape that reuse `Slots::Expression`: `DirectionExpression` adds a `FieldDirection`
// scalar enum (the first ported scalar enum with NO `Any` member, so the `DoMatch` term is a
// plain equality) plus `ref`/`out`/`in` keyword const strings; `ThrowExpression` adds only a
// `throw` keyword const string. The two suites are kept in one file (they share the one-
// required-Expression-slot shape and clear out the remaining non-`AstType` single-Expression-
// slot nodes): they share the recording visitor and the `DoMatch` helper, and each suite is
// independently selectable (`--gtest_filter=CSharp_DirectionExpression.*` /
// `CSharp_ThrowExpression.*`).
//
// Each suite exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over one required single slot at const index 0), the `Expression`
// accessor, the const-keyword string(s), the generated `DoMatch` over ONE NON-nullable recursive
// child (the direct dispatch, with the defensive null guard), the `Clone` deep-copy +
// re-parent, the `AcceptVisitor` dispatch, and the depth-first walk. The per-node slot REUSES
// the shared `Slots::Expression` kind -- no new `Slots.hpp` constant. The required-slot
// invariant means `CheckInvariant` is only exercised on a filled node (an empty node would
// assert "required slot must not be empty"). The `DirectionExpression` suite additionally
// exercises the `FieldDirection` scalar get/set and the plain-equality `DoMatch` term (no
// `Any`-wildcard).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `Visit` methods for the direction/throw pair
// and the leaf expressions they hold, recording a tag and recursing via `VisitChildren` (the
// inherited depth-first default). The trace is the visited nodes in pre-order
// (document order).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitDirectionExpression(DirectionExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("direction");
        VisitChildren(node);
    }
    void VisitThrowExpression(ThrowExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("throw");
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
// DirectionExpression
// =====================================================================

// ---- Construction -----------------------------------------------------

// The default ctor: the `Expression` slot is null and `FieldDirection` is `None` (the enum's
// zero value, the C# default).
TEST(CSharp_DirectionExpression, DefaultCtor) {
    DirectionExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.FieldDirection(), FieldDirection::None);
}

// The all-params ctor sets `FieldDirection` and `Expression`, parents the child, and assigns
// the flat index 0 directly (the const-index `SetChildNode` path for the single slot).
TEST(CSharp_DirectionExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    DirectionExpression e(FieldDirection::Ref, operand.get());
    EXPECT_EQ(e.FieldDirection(), FieldDirection::Ref);
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Const keyword strings -------------------------------------------

// The `RefKeyword`/`OutKeyword`/`InKeyword` const strings carry the `ref`/`out`/`in` keyword
// tokens the output visitor emits (CSharpOutputVisitor.VisitDirectionExpression). They are
// static literals, not instance state, so they are not part of the generated `DoMatch`.
TEST(CSharp_DirectionExpression, KeywordConsts) {
    EXPECT_STREQ(DirectionExpression::RefKeyword, "ref");
    EXPECT_STREQ(DirectionExpression::OutKeyword, "out");
    EXPECT_STREQ(DirectionExpression::InKeyword, "in");
}

// ---- Scalar enum accessor --------------------------------------------

// The `FieldDirection` setter updates the scalar (it shadows the enum, so the elaborated
// `enum FieldDirection` specifier resolves to the enum).
TEST(CSharp_DirectionExpression, FieldDirectionScalar) {
    DirectionExpression e;
    EXPECT_EQ(e.FieldDirection(), FieldDirection::None);
    e.FieldDirection(FieldDirection::Out);
    EXPECT_EQ(e.FieldDirection(), FieldDirection::Out);
    e.FieldDirection(FieldDirection::In);
    EXPECT_EQ(e.FieldDirection(), FieldDirection::In);
    e.FieldDirection(FieldDirection::None);
    EXPECT_EQ(e.FieldDirection(), FieldDirection::None);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_DirectionExpression, ExpressionSetterFillsAndParents) {
    DirectionExpression e;
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
TEST(CSharp_DirectionExpression, NullSetterClearsSlot) {
    DirectionExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection -- the `FieldDirection`
// scalar is NOT a slot).
TEST(CSharp_DirectionExpression, GetChildCountIsOne) {
    DirectionExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns the slot at its flat index and throws out of range otherwise.
TEST(CSharp_DirectionExpression, GetChildReturnsSlot) {
    DirectionExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.GetChild(0), a.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the slot setter.
TEST(CSharp_DirectionExpression, SetChildRoutesToSlot) {
    DirectionExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static.
TEST(CSharp_DirectionExpression, GetChildSlotInfoReturnsSlotStatic) {
    DirectionExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &DirectionExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots::Expression` constant (the
// polymorphic position identity `node.Slot.Kind == Slots.Expression` relies on). This is
// the same kind `UnaryOperatorExpression`/`ParenthesizedExpression`/`CheckedExpression`/
// `UncheckedExpression`/`ThrowExpression` registered, so the node types share the `Expression`
// operand position.
TEST(CSharp_DirectionExpression, SlotKindPointsAtSharedSlotsExpression) {
    DirectionExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    ASSERT_EQ(a->Slot(), &DirectionExpression::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
    EXPECT_EQ(DirectionExpression::ExpressionSlot.Kind(), &Slots::Expression);
}

// The slot static is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_DirectionExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(DirectionExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(DirectionExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitDirectionExpression`.
TEST(CSharp_DirectionExpression, AcceptVisitorDispatchesToVisit) {
    DirectionExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "direction");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_DirectionExpression, AcceptVisitorIsVirtual) {
    DirectionExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "direction");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "direction");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the direction node, then its single child.
TEST(CSharp_DirectionExpression, DepthFirstWalksChildInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    DirectionExpression e(FieldDirection::Ref, operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "direction");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no children records just the direction node.
TEST(CSharp_DirectionExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    DirectionExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "direction");
}

// ---- DoMatch (the generated match: non-nullable-recursive + plain enum) --

// Two nodes with the same-type child AND the same `FieldDirection` match.
TEST(CSharp_DirectionExpression, DoMatchMatchesSameChildAndDirection) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression b(FieldDirection::Ref, bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `FieldDirection` rejects on the plain equality term (no `Any`-wildcard: the
// `FieldDirection` enum has no `Any` member, so a `Ref` pattern does NOT match an `Out`
// candidate -- unlike the `Operator`-bearing nodes where an `Any` operator is the wildcard).
TEST(CSharp_DirectionExpression, DoMatchRejectsDifferentDirection) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression b(FieldDirection::Out, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `None` (the default) pattern matches only a `None` candidate -- the plain equality term,
// so the zero value is a real direction, NOT a wildcard.
TEST(CSharp_DirectionExpression, DoMatchNoneIsNotWildcard) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::None, aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression b(FieldDirection::Ref, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    auto cOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression c(FieldDirection::None, cOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &c));
}

// The recursive dispatch delegates the child match to the child's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// child's type-only `DoMatch` fails), even with the same direction.
TEST(CSharp_DirectionExpression, DoMatchRejectsDifferentChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    DirectionExpression b(FieldDirection::Ref, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Expression`; the port returns false
// instead of crashing).
TEST(CSharp_DirectionExpression, DoMatchNullPatternOperandRejects) {
    DirectionExpression a(FieldDirection::Ref, nullptr);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression b(FieldDirection::Ref, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects (the guard: the candidate's operand is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_DirectionExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());

    DirectionExpression b(FieldDirection::Ref, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as no-match
// (the C# would null-dereference; the port returns false).
TEST(CSharp_DirectionExpression, DoMatchBothOperandsNullRejects) {
    DirectionExpression a(FieldDirection::Ref, nullptr);
    DirectionExpression b(FieldDirection::Ref, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `DirectionExpression`) rejects.
TEST(CSharp_DirectionExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_DirectionExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// A `DirectionExpression` and a `ThrowExpression` are distinct node types: even with the
// same operand, the type check rejects (the generator's `other is DirectionExpression`).
TEST(CSharp_DirectionExpression, DoMatchRejectsThrowSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression a(FieldDirection::Ref, aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones the child (a distinct object, not shared), copies the scalar
// `FieldDirection`, re-parents the child to the copy, and assigns the flat index.
TEST(CSharp_DirectionExpression, CloneDeepClonesChildAndCopiesScalar) {
    auto operand = std::make_unique<NullReferenceExpression>();
    DirectionExpression e(FieldDirection::Out, operand.get());
    std::unique_ptr<DirectionExpression> copy(
        static_cast<DirectionExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
    EXPECT_EQ(copy->FieldDirection(), FieldDirection::Out);
}

// `Clone` of a node with no child yields a node with the slot null (the `Clone` skips the
// child clone when the child is absent), but copies the scalar.
TEST(CSharp_DirectionExpression, CloneEmptyNodeHasNullSlot) {
    DirectionExpression e;
    e.FieldDirection(FieldDirection::In);
    std::unique_ptr<DirectionExpression> copy(
        static_cast<DirectionExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->FieldDirection(), FieldDirection::In);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_DirectionExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    DirectionExpression e(FieldDirection::Ref, operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<DirectionExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_DirectionExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    DirectionExpression e(FieldDirection::Ref, operand.get());
    std::unique_ptr<DirectionExpression> copy(
        static_cast<DirectionExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(e.FieldDirection(), FieldDirection::Ref);
}

// ---- is-a / CheckInvariant -------------------------------------------

// `DirectionExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_DirectionExpression, IsAstNodeAndExpression) {
    DirectionExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the slot filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the slot is required, so `CheckInvariant` would assert "required
// slot must not be empty" on a node with a null child.
TEST(CSharp_DirectionExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    DirectionExpression e(FieldDirection::Ref, operand.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// ThrowExpression
// =====================================================================

// ---- Construction -----------------------------------------------------

// The default ctor: the `Expression` slot is null (no operand).
TEST(CSharp_ThrowExpression, DefaultCtor) {
    ThrowExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
}

// The all-params ctor sets `Expression`, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path for the single slot).
TEST(CSharp_ThrowExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    ThrowExpression e(operand.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Const keyword string -------------------------------------------

// The `ThrowKeyword` const string carries the `throw` keyword token the output visitor emits
// (CSharpOutputVisitor.VisitThrowExpression calls WriteKeyword(ThrowExpression.ThrowKeyword)).
// The C# references `ThrowStatement.ThrowKeyword` (a not-yet-ported `Statement` node); the
// value is the literal "throw", carried as a `static constexpr const char*` now. It is a
// static literal, not instance state, so it is not part of the generated `DoMatch`.
TEST(CSharp_ThrowExpression, ThrowKeywordConst) {
    EXPECT_STREQ(ThrowExpression::ThrowKeyword, "throw");
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly (the const-index `SetChildNode` path).
TEST(CSharp_ThrowExpression, ExpressionSetterFillsAndParents) {
    ThrowExpression e;
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
TEST(CSharp_ThrowExpression, NullSetterClearsSlot) {
    ThrowExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

// `GetChildCount` is the constant 1 (one single slot, no collection).
TEST(CSharp_ThrowExpression, GetChildCountIsOne) {
    ThrowExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

// `GetChild` returns the slot at its flat index and throws out of range otherwise.
TEST(CSharp_ThrowExpression, GetChildReturnsSlot) {
    ThrowExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    EXPECT_EQ(e.GetChild(0), a.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the slot setter.
TEST(CSharp_ThrowExpression, SetChildRoutesToSlot) {
    ThrowExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, a.get());
    EXPECT_EQ(e.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static.
TEST(CSharp_ThrowExpression, GetChildSlotInfoReturnsSlotStatic) {
    ThrowExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &ThrowExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

// The per-node slot's `Kind` points at the shared `Slots::Expression` constant (the
// polymorphic position identity `node.Slot.Kind == Slots.Expression` relies on). This is
// the same kind `UnaryOperatorExpression`/`ParenthesizedExpression`/`CheckedExpression`/
// `UncheckedExpression`/`DirectionExpression` registered, so the node types share the
// `Expression` operand position.
TEST(CSharp_ThrowExpression, SlotKindPointsAtSharedSlotsExpression) {
    ThrowExpression e;
    auto a = std::make_unique<NullReferenceExpression>();
    e.Expression(a.get());
    ASSERT_EQ(a->Slot(), &ThrowExpression::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
    EXPECT_EQ(ThrowExpression::ExpressionSlot.Kind(), &Slots::Expression);
}

// The slot static is `Expression`-typed: an `Expression` is an instance, a null is not.
TEST(CSharp_ThrowExpression, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(ThrowExpression::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(ThrowExpression::ExpressionSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitThrowExpression`.
TEST(CSharp_ThrowExpression, AcceptVisitorDispatchesToVisit) {
    ThrowExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_ThrowExpression, AcceptVisitorIsVirtual) {
    ThrowExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "throw");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the throw node, then its single child.
TEST(CSharp_ThrowExpression, DepthFirstWalksChildInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowExpression e(operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "throw");
    EXPECT_EQ(v.trace[1], "null");
}

// A node with no children records just the throw node.
TEST(CSharp_ThrowExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ThrowExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "throw");
}

// ---- DoMatch (the generated non-nullable-recursive match, one term) --

// Two nodes with the same-type child match.
TEST(CSharp_ThrowExpression, DoMatchMatchesSameChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates the child match to the child's own `DoMatch`: a
// `NullReferenceExpression` operand vs a `ThisReferenceExpression` operand rejects (the
// child's type-only `DoMatch` fails).
TEST(CSharp_ThrowExpression, DoMatchRejectsDifferentChild) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>();
    ThrowExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern operand rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Expression`; the port returns false
// instead of crashing).
TEST(CSharp_ThrowExpression, DoMatchNullPatternOperandRejects) {
    ThrowExpression a(nullptr);

    auto bOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate operand rejects (the guard: the candidate's operand is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_ThrowExpression, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());

    ThrowExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Both operands absent (null) reject: the guard treats a missing required child as no-match
// (the C# would null-dereference; the port returns false).
TEST(CSharp_ThrowExpression, DoMatchBothOperandsNullRejects) {
    ThrowExpression a(nullptr);
    ThrowExpression b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `ThrowExpression`) rejects.
TEST(CSharp_ThrowExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_ThrowExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// A `ThrowExpression` and a `DirectionExpression` are distinct node types: even with the
// same operand, the type check rejects (the generator's `other is ThrowExpression`).
TEST(CSharp_ThrowExpression, DoMatchRejectsDirectionSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    ThrowExpression a(aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    DirectionExpression b(FieldDirection::Ref, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones the child (a distinct object, not shared), re-parents it to the copy,
// and assigns the flat index.
TEST(CSharp_ThrowExpression, CloneDeepClonesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowExpression e(operand.get());
    std::unique_ptr<ThrowExpression> copy(
        static_cast<ThrowExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

// `Clone` of a node with no child yields a node with the slot null (the `Clone` skips the
// child clone when the child is absent).
TEST(CSharp_ThrowExpression, CloneEmptyNodeHasNullSlot) {
    ThrowExpression e;
    std::unique_ptr<ThrowExpression> copy(
        static_cast<ThrowExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_ThrowExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowExpression e(operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ThrowExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's child is distinct and re-parented to the
// copy, not stolen from the source).
TEST(CSharp_ThrowExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowExpression e(operand.get());
    std::unique_ptr<ThrowExpression> copy(
        static_cast<ThrowExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- is-a / CheckInvariant -------------------------------------------

// `ThrowExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_ThrowExpression, IsAstNodeAndExpression) {
    ThrowExpression e;
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// The inherited `CheckInvariant` passes on a node with the slot filled (the slot-structure
// verifier finds the required slot filled and the child's Parent/ChildIndex consistent). An
// empty node is NOT tested: the slot is required, so `CheckInvariant` would assert "required
// slot must not be empty" on a node with a null child.
TEST(CSharp_ThrowExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    ThrowExpression e(operand.get());
    e.CheckInvariant();
    SUCCEED();
}
