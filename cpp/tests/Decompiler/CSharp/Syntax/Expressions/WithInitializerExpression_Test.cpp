// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

// Tests for the `WithInitializerExpression` concrete node (cpp/.../Expressions/
// WithInitializerExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.cs) -- the
// `with_expression ::= expression 'with' array_initializer` shape (C# grammar 12.10): a sealed
// `Expression` with a required `Expression` slot at index 0 + a required
// `ArrayInitializerExpression` `Initializer` slot at index 1, reusing the already-ported
// `Slots::Expression`/`Slots::Initializer` kinds. Exercises the slot-storage contract
// (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` over two required single slots at
// const indices), the `Expression`/`Initializer` accessors (the former exercising the `class
// Expression` elaborated-type-specifier name-shadowing crux), the generated `DoMatch` over
// TWO NON-nullable recursive children (the direct dispatch via `MatchRequired`, with the
// defensive null guard), the `Clone` deep-copy + re-parent of two children, the
// `AcceptVisitor` dispatch, and the depth-first walk. There is no scalar enum, so `DoMatch`
// has no `Any`-wildcard term. The required-slot invariant means `CheckInvariant` is only
// exercised on a filled node (an empty node would assert "required slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitWithInitializerExpression` (and the nodes
// it holds), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order (document order). The `Expression`
// slot is exercised with a `NullReferenceExpression`/`ThisReferenceExpression` (the simplest
// leaf `Expression`s), and the `Initializer` slot with an `ArrayInitializerExpression` (the
// `Elements` collection of which is empty in the walk tests, so `VisitArrayInitializerExpression`
// records just the `arrinit` tag).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitWithInitializerExpression(WithInitializerExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-with>");
            return;
        }
        trace.push_back("with");
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-arrinit>");
            return;
        }
        trace.push_back("arrinit");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-nre>");
            return;
        }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-tre>");
            return;
        }
        trace.push_back("thisref");
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

// ---- is-a --------------------------------------------------------------

// `WithInitializerExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the
// slot system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, not `AstType`).
TEST(CSharp_WithInitializerExpression, IsAstNodeAndExpressionNotAstType) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Construction -----------------------------------------------------

// The default ctor: both slots are null (no receiver, no initializer). The two required slots
// still occupy their flattened indices even when empty, so `GetChildCount` is 2.
TEST(CSharp_WithInitializerExpression, DefaultCtor) {
    WithInitializerExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.Initializer(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 2);
}

// The all-params ctor sets both slots, parents each child, and assigns the flat indices 0/1
// directly (the const-index `SetChildNode` path for each single slot).
TEST(CSharp_WithInitializerExpression, AllParamsCtorSetsAndParents) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    NullReferenceExpression* receiverPtr = receiver.get();
    ArrayInitializerExpression* initPtr = init.get();
    WithInitializerExpression e(receiver.get(), init.get());
    EXPECT_EQ(e.Expression(), receiverPtr);
    EXPECT_EQ(e.Initializer(), initPtr);
    EXPECT_EQ(receiverPtr->Parent(), &e);
    EXPECT_EQ(receiverPtr->ChildIndex, 0);
    EXPECT_EQ(initPtr->Parent(), &e);
    EXPECT_EQ(initPtr->ChildIndex, 1);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly.
TEST(CSharp_WithInitializerExpression, ExpressionSetterFillsAndParents) {
    WithInitializerExpression e;
    auto receiver = std::make_unique<NullReferenceExpression>();
    e.Expression(receiver.get());
    EXPECT_EQ(e.Expression(), receiver.get());
    EXPECT_EQ(receiver->Parent(), &e);
    EXPECT_EQ(receiver->ChildIndex, 0);
}

// The `Initializer` setter fills the slot, parents the child, and assigns the flat index 1
// directly.
TEST(CSharp_WithInitializerExpression, InitializerSetterFillsAndParents) {
    WithInitializerExpression e;
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Initializer(init.get());
    EXPECT_EQ(e.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &e);
    EXPECT_EQ(init->ChildIndex, 1);
}

// A null `Expression` setter clears the slot and detaches the old child (the slot is
// required, but the setter still tolerates a null to detach -- the invariant is enforced by
// `CheckInvariant`, not by the setter).
TEST(CSharp_WithInitializerExpression, NullExpressionSetterClearsSlot) {
    WithInitializerExpression e;
    auto receiver = std::make_unique<NullReferenceExpression>();
    e.Expression(receiver.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(receiver->Parent(), nullptr);
    EXPECT_EQ(receiver->ChildIndex, -1);
}

// A null `Initializer` setter clears the slot and detaches the old child.
TEST(CSharp_WithInitializerExpression, NullInitializerSetterClearsSlot) {
    WithInitializerExpression e;
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Initializer(init.get());
    e.Initializer(nullptr);
    EXPECT_EQ(e.Initializer(), nullptr);
    EXPECT_EQ(init->Parent(), nullptr);
    EXPECT_EQ(init->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_WithInitializerExpression, GetChildCountIsTwo) {
    WithInitializerExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise.
TEST(CSharp_WithInitializerExpression, GetChildReturnsSlots) {
    WithInitializerExpression e;
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Expression(receiver.get());
    e.Initializer(init.get());
    EXPECT_EQ(e.GetChild(0), receiver.get());
    EXPECT_EQ(e.GetChild(1), init.get());
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_WithInitializerExpression, SetChildRoutesToSlots) {
    WithInitializerExpression e;
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.SetChild(0, receiver.get());
    EXPECT_EQ(e.Expression(), receiver.get());
    EXPECT_EQ(receiver->Parent(), &e);
    e.SetChild(1, init.get());
    EXPECT_EQ(e.Initializer(), init.get());
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_WithInitializerExpression, GetChildSlotInfoReturnsSlotStatics) {
    WithInitializerExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &WithInitializerExpression::ExpressionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &WithInitializerExpression::InitializerSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic position
// identity `node.Slot.Kind == Slots.X` relies on). `Expression` reuses `Slots::Expression`
// (ported by `UnaryOperatorExpression`); `Initializer` reuses `Slots::Initializer` (ported by
// `ObjectCreateExpression`/`ArrayCreateExpression` as the cycle-broken kind in
// `ArrayInitializerExpression.hpp`).
TEST(CSharp_WithInitializerExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    ASSERT_EQ(receiver->Slot(), &WithInitializerExpression::ExpressionSlot);
    EXPECT_EQ(receiver->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(init->Slot(), &WithInitializerExpression::InitializerSlot);
    EXPECT_EQ(init->Slot()->Kind(), &Slots::Initializer);
}

// The slot statics are typed: the `ExpressionSlot` accepts an `Expression` (a
// `NullReferenceExpression` is, an `ArrayInitializerExpression` is -- it derives from
// `Expression` -- but an `AstType`-typed `PrimitiveType` is not); the `InitializerSlot`
// accepts an `ArrayInitializerExpression` (a `NullReferenceExpression` is not, even though
// both are `Expression`s). This cross-checks that the two slots' element types are distinct
// and faithful to the C#.
TEST(CSharp_WithInitializerExpression, SlotsAreInstanceOfTheirElementTypes) {
    NullReferenceExpression nre;
    ArrayInitializerExpression aie;
    EXPECT_TRUE(WithInitializerExpression::ExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(WithInitializerExpression::ExpressionSlot.IsInstanceOfType(&aie));
    EXPECT_TRUE(WithInitializerExpression::InitializerSlot.IsInstanceOfType(&aie));
    EXPECT_FALSE(WithInitializerExpression::InitializerSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(WithInitializerExpression::ExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(WithInitializerExpression::InitializerSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitWithInitializerExpression`.
TEST(CSharp_WithInitializerExpression, AcceptVisitorDispatchesToVisit) {
    WithInitializerExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "with");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_WithInitializerExpression, AcceptVisitorIsVirtual) {
    WithInitializerExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "with");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "with");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `with` node, then its two children in document order (the
// `Expression` receiver, then the `Initializer` `ArrayInitializerExpression`).
TEST(CSharp_WithInitializerExpression, DepthFirstWalksChildrenInOrder) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "with");
    EXPECT_EQ(v.trace[1], "nullref");
    EXPECT_EQ(v.trace[2], "arrinit");
}

// A node with no children records just the `with` node.
TEST(CSharp_WithInitializerExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    WithInitializerExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "with");
}

// ---- DoMatch (the generated non-nullable-recursive match, two terms) --

// Two nodes with the same-type children in both slots match (both `Initializer`s empty -- the
// collection-DoMatch on `Elements` matches two empty collections).
TEST(CSharp_WithInitializerExpression, DoMatchMatchesSameChildren) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());

    auto bReceiver = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression b(bReceiver.get(), bInit.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Expression` slot (the first term) rejects (a `NullReferenceExpression`
// pattern vs a `ThisReferenceExpression` candidate rejects on the child's type-only `DoMatch`).
TEST(CSharp_WithInitializerExpression, DoMatchRejectsDifferentExpression) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());

    auto bReceiver = std::make_unique<ThisReferenceExpression>(); // different receiver type
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression b(bReceiver.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Initializer` slot (the second term) rejects: the `Initializer`'s
// `DoMatch` is a collection-DoMatch on `Elements`, so an empty `Initializer` vs one with a
// single element rejects on the count mismatch.
TEST(CSharp_WithInitializerExpression, DoMatchRejectsDifferentInitializer) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());

    auto bReceiver = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    auto bElem = std::make_unique<NullReferenceExpression>();
    bInit->Elements().Add(bElem.get()); // one element -- different from the empty pattern
    WithInitializerExpression b(bReceiver.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern `Expression` rejects: the port guards the non-nullable recursive dispatch
// against a missing child (the C# would null-dereference `this.Expression`; the port returns
// false instead of crashing).
TEST(CSharp_WithInitializerExpression, DoMatchNullPatternExpressionRejects) {
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(nullptr, aInit.get());

    auto bReceiver = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression b(bReceiver.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Expression` rejects (the guard: the candidate's `Expression` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_WithInitializerExpression, DoMatchNullCandidateExpressionRejects) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());

    auto bInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression b(nullptr, bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// All children absent (null) on both sides reject: the guard treats a missing required child
// as no-match (the C# would null-dereference; the port returns false).
TEST(CSharp_WithInitializerExpression, DoMatchAllChildrenNullRejects) {
    WithInitializerExpression a(nullptr, nullptr);
    WithInitializerExpression b(nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: a
// `NullReferenceExpression` `Expression` vs a `ThisReferenceExpression` `Expression` rejects
// (the receiver child's type-only `DoMatch` fails), even when the `Initializer` slot would
// match.
TEST(CSharp_WithInitializerExpression, DoMatchDelegatesToChildDoMatch) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());

    auto bReceiver = std::make_unique<ThisReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression b(bReceiver.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `WithInitializerExpression`) rejects.
TEST(CSharp_WithInitializerExpression, DoMatchRejectsNonWithInitializerCandidate) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_WithInitializerExpression, DoMatchRejectsNullCandidate) {
    auto aReceiver = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression a(aReceiver.get(), aInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones both children (distinct objects, not shared), re-parents each to the
// copy, and assigns the flat indices.
TEST(CSharp_WithInitializerExpression, CloneDeepClonesAllChildren) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    std::unique_ptr<WithInitializerExpression> copy(static_cast<WithInitializerExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), receiver.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
    ASSERT_NE(copy->Initializer(), nullptr);
    EXPECT_NE(copy->Initializer(), init.get());
    EXPECT_EQ(copy->Initializer()->Parent(), copy.get());
    EXPECT_EQ(copy->Initializer()->ChildIndex, 1);
}

// `Clone` of a node with no children yields a node with both slots null (the `Clone` skips a
// child clone when the child is absent).
TEST(CSharp_WithInitializerExpression, CloneEmptyNodeHasNullSlots) {
    WithInitializerExpression e;
    std::unique_ptr<WithInitializerExpression> copy(static_cast<WithInitializerExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_WithInitializerExpression, CloneIsVirtualAndCovariant) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<WithInitializerExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_WithInitializerExpression, CloneDoesNotDetachSource) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    std::unique_ptr<WithInitializerExpression> copy(static_cast<WithInitializerExpression*>(e.Clone()));
    EXPECT_EQ(receiver->Parent(), &e);
    EXPECT_EQ(e.Expression(), receiver.get());
    EXPECT_EQ(e.Initializer(), init.get());
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with both slots filled (the slot-structure
// verifier finds each required slot filled and each child's Parent/ChildIndex consistent).
// An empty node is NOT tested here: every slot is required, so `CheckInvariant` would assert
// "required slot must not be empty" on a node with a null child (exercised below under
// `#ifndef NDEBUG`).
TEST(CSharp_WithInitializerExpression, CheckInvariantPassesOnFilledNode) {
    auto receiver = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    WithInitializerExpression e(receiver.get(), init.get());
    e.CheckInvariant();
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_WithInitializerExpression, CheckInvariantRejectsEmptyNode) {
    // Both slots are required (non-nullable), so an empty node violates the required-slot
    // invariant (the `UnaryOperatorExpression` D231 required-slot behavior -- the assert
    // fires in debug).
    WithInitializerExpression e;
    EXPECT_DEATH(e.CheckInvariant(), "");
}
#endif
