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

// Tests for the `AsExpression`/`IsExpression` concrete node pair (cpp/.../
// Expressions/AsExpression.hpp + IsExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/{As,Is}Expression.cs) -- the next in-order
// Phase-5 piece per the D243 plan ("the remaining AstType-bearing Expression nodes:
// AsExpression/IsExpression -- both an Expression + an AstType, the CastExpression
// two-required-slot shape"). Both are the `CastExpression` two-required-slot shape but with the
// slot order REVERSED (`Expression` at flattened index 0, `Type` at index 1 -- the source
// declares `Expression` before `Type`, unlike `CastExpression` which declares `Type` first),
// plus an `as`/`is` keyword const string. The two suites are kept in one file (the D234
// "natural pair" framing): they share the recording visitor and the `DoMatch` helper, and each
// suite is independently selectable (`--gtest_filter=CSharp_AsExpression.*` /
// `CSharp_IsExpression.*`).
//
// Each suite exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over two required single slots at const indices 0/1), the
// `Expression`/`Type` accessors (the `Expression` one exercising the `class Expression`
// elaborated-type-specifier name-shadowing crux), the const-keyword string, the generated
// `DoMatch` over TWO NON-nullable recursive children (the direct dispatch via `MatchRequired`,
// with the defensive null guard), the `Clone` deep-copy + re-parent of two children, the
// `AcceptVisitor` dispatch, and the depth-first walk (the `Expression` operand first, then the
// `Type`, in the source-declaration order). There is no scalar enum, so `DoMatch` has no
// `Any`-wildcard term. The required-slot invariant means `CheckInvariant` is only exercised on
// a filled node (an empty node would assert "required slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitAsExpression`/`VisitIsExpression` (and
// `VisitCastExpression` for the cross-sibling test) and the nodes they hold, recording a tag
// and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order (document order). The `Type` slot is exercised with a
// `PrimitiveType` (the simplest leaf `AstType`), so `VisitPrimitiveType` records the keyword.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAsExpression(AsExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-as>");
            return;
        }
        trace.push_back("as");
        VisitChildren(node);
    }
    void VisitIsExpression(IsExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-is>");
            return;
        }
        trace.push_back("is");
        VisitChildren(node);
    }
    void VisitCastExpression(CastExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-cast>");
            return;
        }
        trace.push_back("cast");
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) {
            trace.push_back("<null-pt>");
            return;
        }
        trace.push_back("prim:" + node->Keyword());
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

// =====================================================================
// AsExpression
// =====================================================================

// ---- is-a --------------------------------------------------------------

// `AsExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, not `AstType`).
TEST(CSharp_AsExpression, IsAstNodeAndExpressionNotAstType) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Construction -----------------------------------------------------

// The default ctor: both slots are null (no operand, no type). The two required slots still
// occupy their flattened indices even when empty, so `GetChildCount` is 2.
TEST(CSharp_AsExpression, DefaultCtor) {
    AsExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 2);
}

// The all-params ctor sets both slots, parents each child, and assigns the flat indices 0/1
// directly (the const-index `SetChildNode` path for each single slot). The `Expression`
// operand is at index 0 and the `Type` at index 1 (the source declaration order, the reverse
// of `CastExpression`).
TEST(CSharp_AsExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    NullReferenceExpression* operandPtr = operand.get();
    PrimitiveType* typePtr = type.get();
    AsExpression e(operand.get(), type.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 1);
}

// ---- Const keyword string -------------------------------------------

// The `AsKeyword` const string carries the `as` keyword token the output visitor emits
// (CSharpOutputVisitor.VisitAsExpression). It is a static literal, not instance state, so it
// is not part of the generated `DoMatch`.
TEST(CSharp_AsExpression, AsKeywordConst) {
    EXPECT_STREQ(AsExpression::AsKeyword, "as");
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly.
TEST(CSharp_AsExpression, ExpressionSetterFillsAndParents) {
    AsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(operand->ChildIndex, 0);
}

// The `Type` setter fills the slot, parents the child, and assigns the flat index 1 directly.
TEST(CSharp_AsExpression, TypeSetterFillsAndParents) {
    AsExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 1);
}

// A null `Expression` setter clears the slot and detaches the old child (the slot is
// required, but the setter still tolerates a null to detach -- the invariant is enforced by
// `CheckInvariant`, not by the setter).
TEST(CSharp_AsExpression, NullExpressionSetterClearsSlot) {
    AsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(operand->Parent(), nullptr);
    EXPECT_EQ(operand->ChildIndex, -1);
}

// A null `Type` setter clears the slot and detaches the old child.
TEST(CSharp_AsExpression, NullTypeSetterClearsSlot) {
    AsExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_AsExpression, GetChildCountIsTwo) {
    AsExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise. The
// `Expression` operand is at index 0 and the `Type` at index 1 (the source declaration order).
TEST(CSharp_AsExpression, GetChildReturnsSlots) {
    AsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Expression(operand.get());
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), operand.get());
    EXPECT_EQ(e.GetChild(1), type.get());
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_AsExpression, SetChildRoutesToSlots) {
    AsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    e.SetChild(1, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_AsExpression, GetChildSlotInfoReturnsSlotStatics) {
    AsExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &AsExpression::ExpressionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &AsExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic position
// identity `node.Slot.Kind == Slots.X` relies on). `Expression` reuses `Slots::Expression`
// (ported by `UnaryOperatorExpression`); `Type` reuses `Slots::Type` (ported by `Attribute`).
TEST(CSharp_AsExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    ASSERT_EQ(operand->Slot(), &AsExpression::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(type->Slot(), &AsExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

// The slot statics are typed: the `ExpressionSlot` accepts an `Expression` (a
// `NullReferenceExpression` is, a `PrimitiveType` is not); the `TypeSlot` accepts an `AstType`
// (a `PrimitiveType` is, a `NullReferenceExpression` is not). This cross-checks that the two
// slots' element types are distinct and faithful to the C#.
TEST(CSharp_AsExpression, SlotsAreInstanceOfTheirElementTypes) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(AsExpression::ExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(AsExpression::ExpressionSlot.IsInstanceOfType(&pt));
    EXPECT_TRUE(AsExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(AsExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(AsExpression::ExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(AsExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAsExpression`.
TEST(CSharp_AsExpression, AcceptVisitorDispatchesToVisit) {
    AsExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "as");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_AsExpression, AcceptVisitorIsVirtual) {
    AsExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "as");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "as");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `as` node, then its two children in document order (the
// `Expression` operand first, then the `Type` `PrimitiveType`) -- the source declaration
// order, the reverse of `CastExpression`.
TEST(CSharp_AsExpression, DepthFirstWalksChildrenInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "as");
    EXPECT_EQ(v.trace[1], "nullref");
    EXPECT_EQ(v.trace[2], "prim:int");
}

// A node with no children records just the `as` node.
TEST(CSharp_AsExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    AsExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "as");
}

// ---- DoMatch (the generated non-nullable-recursive match, two terms) --

// Two nodes with the same-type children in both slots match.
TEST(CSharp_AsExpression, DoMatchMatchesSameChildren) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Expression` slot (the first term) rejects.
TEST(CSharp_AsExpression, DoMatchRejectsDifferentExpression) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>(); // different operand type
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Type` slot (the second term) rejects.
TEST(CSharp_AsExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("long")); // different type keyword
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern `Expression` rejects: the port guards the non-nullable recursive dispatch
// against a missing child (the C# would null-dereference `this.Expression`; the port returns
// false instead of crashing).
TEST(CSharp_AsExpression, DoMatchNullPatternExpressionRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(nullptr, aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Expression` rejects (the guard: the candidate's `Expression` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_AsExpression, DoMatchNullCandidateExpressionRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(nullptr, bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// All children absent (null) on both sides reject: the guard treats a missing required child
// as no-match (the C# would null-dereference; the port returns false).
TEST(CSharp_AsExpression, DoMatchAllChildrenNullRejects) {
    AsExpression a(nullptr, nullptr);
    AsExpression b(nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: an `int`
// `Type` vs a `long` `Type` rejects (the `PrimitiveType` child's `MatchString` on `Keyword`
// fails), even when the `Expression` slot would match.
TEST(CSharp_AsExpression, DoMatchDelegatesToChildDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not an `AsExpression`) rejects.
TEST(CSharp_AsExpression, DoMatchRejectsNonAsCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_AsExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// An `AsExpression` and an `IsExpression` are distinct node types: even with the same
// children, the type check rejects (the generator's `other is AsExpression`).
TEST(CSharp_AsExpression, DoMatchRejectsIsSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones both children (distinct objects, not shared), re-parents each to the
// copy, and assigns the flat indices (the `Expression` operand at 0, the `Type` at 1).
TEST(CSharp_AsExpression, CloneDeepClonesAllChildren) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    std::unique_ptr<AsExpression> copy(static_cast<AsExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 1);
    // The cloned Type keeps its keyword (the PrimitiveType child deep-copy).
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

// `Clone` of a node with no children yields a node with both slots null (the `Clone` skips a
// child clone when the child is absent).
TEST(CSharp_AsExpression, CloneEmptyNodeHasNullSlots) {
    AsExpression e;
    std::unique_ptr<AsExpression> copy(static_cast<AsExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_AsExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<AsExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_AsExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    std::unique_ptr<AsExpression> copy(static_cast<AsExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(e.Type(), type.get());
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with both slots filled (the slot-structure
// verifier finds each required slot filled and each child's Parent/ChildIndex consistent).
// An empty node is NOT tested: every slot is required, so `CheckInvariant` would assert
// "required slot must not be empty" on a node with a null child.
TEST(CSharp_AsExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression e(operand.get(), type.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// IsExpression
// =====================================================================

// ---- is-a --------------------------------------------------------------

// `IsExpression` is an `AstNode` and an `Expression`; it is NOT an `AstType`.
TEST(CSharp_IsExpression, IsAstNodeAndExpressionNotAstType) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Construction -----------------------------------------------------

// The default ctor: both slots are null (no operand, no type).
TEST(CSharp_IsExpression, DefaultCtor) {
    IsExpression e;
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 2);
}

// The all-params ctor sets both slots, parents each child, and assigns the flat indices 0/1
// directly (the `Expression` operand at 0, the `Type` at 1 -- the source declaration order).
TEST(CSharp_IsExpression, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    NullReferenceExpression* operandPtr = operand.get();
    PrimitiveType* typePtr = type.get();
    IsExpression e(operand.get(), type.get());
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 1);
}

// ---- Const keyword string -------------------------------------------

// The `IsKeyword` const string carries the `is` keyword token the output visitor emits
// (CSharpOutputVisitor.VisitIsExpression). It is a static literal, not instance state, so it
// is not part of the generated `DoMatch`.
TEST(CSharp_IsExpression, IsKeywordConst) {
    EXPECT_STREQ(IsExpression::IsKeyword, "is");
}

// ---- Accessors + slot storage -----------------------------------------

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 0
// directly.
TEST(CSharp_IsExpression, ExpressionSetterFillsAndParents) {
    IsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(operand->ChildIndex, 0);
}

// The `Type` setter fills the slot, parents the child, and assigns the flat index 1 directly.
TEST(CSharp_IsExpression, TypeSetterFillsAndParents) {
    IsExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 1);
}

// A null `Expression` setter clears the slot and detaches the old child.
TEST(CSharp_IsExpression, NullExpressionSetterClearsSlot) {
    IsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(operand->Parent(), nullptr);
    EXPECT_EQ(operand->ChildIndex, -1);
}

// A null `Type` setter clears the slot and detaches the old child.
TEST(CSharp_IsExpression, NullTypeSetterClearsSlot) {
    IsExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_IsExpression, GetChildCountIsTwo) {
    IsExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise. The
// `Expression` operand is at index 0 and the `Type` at index 1 (the source declaration order).
TEST(CSharp_IsExpression, GetChildReturnsSlots) {
    IsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Expression(operand.get());
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), operand.get());
    EXPECT_EQ(e.GetChild(1), type.get());
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_IsExpression, SetChildRoutesToSlots) {
    IsExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    e.SetChild(1, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_IsExpression, GetChildSlotInfoReturnsSlotStatics) {
    IsExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &IsExpression::ExpressionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &IsExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant. `Expression` reuses
// `Slots::Expression`; `Type` reuses `Slots::Type`.
TEST(CSharp_IsExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    ASSERT_EQ(operand->Slot(), &IsExpression::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(type->Slot(), &IsExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

// The slot statics are typed: the `ExpressionSlot` accepts an `Expression` (a
// `NullReferenceExpression` is, a `PrimitiveType` is not); the `TypeSlot` accepts an `AstType`
// (a `PrimitiveType` is, a `NullReferenceExpression` is not).
TEST(CSharp_IsExpression, SlotsAreInstanceOfTheirElementTypes) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(IsExpression::ExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(IsExpression::ExpressionSlot.IsInstanceOfType(&pt));
    EXPECT_TRUE(IsExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(IsExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(IsExpression::ExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(IsExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitIsExpression`.
TEST(CSharp_IsExpression, AcceptVisitorDispatchesToVisit) {
    IsExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "is");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_IsExpression, AcceptVisitorIsVirtual) {
    IsExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "is");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "is");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `is` node, then its two children in document order (the
// `Expression` operand first, then the `Type` `PrimitiveType`) -- the source declaration order.
TEST(CSharp_IsExpression, DepthFirstWalksChildrenInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "is");
    EXPECT_EQ(v.trace[1], "nullref");
    EXPECT_EQ(v.trace[2], "prim:int");
}

// A node with no children records just the `is` node.
TEST(CSharp_IsExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    IsExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "is");
}

// ---- DoMatch (the generated non-nullable-recursive match, two terms) --

// Two nodes with the same-type children in both slots match.
TEST(CSharp_IsExpression, DoMatchMatchesSameChildren) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Expression` slot (the first term) rejects.
TEST(CSharp_IsExpression, DoMatchRejectsDifferentExpression) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<ThisReferenceExpression>(); // different operand type
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Type` slot (the second term) rejects.
TEST(CSharp_IsExpression, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("long")); // different type keyword
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern `Expression` rejects: the port guards the non-nullable recursive dispatch
// against a missing child (the C# would null-dereference `this.Expression`; the port returns
// false instead of crashing).
TEST(CSharp_IsExpression, DoMatchNullPatternExpressionRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(nullptr, aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Expression` rejects (the guard: the candidate's `Expression` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_IsExpression, DoMatchNullCandidateExpressionRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression b(nullptr, bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// All children absent (null) on both sides reject.
TEST(CSharp_IsExpression, DoMatchAllChildrenNullRejects) {
    IsExpression a(nullptr, nullptr);
    IsExpression b(nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: an `int`
// `Type` vs a `long` `Type` rejects (the `PrimitiveType` child's `MatchString` on `Keyword`
// fails), even when the `Expression` slot would match.
TEST(CSharp_IsExpression, DoMatchDelegatesToChildDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    IsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not an `IsExpression`) rejects.
TEST(CSharp_IsExpression, DoMatchRejectsNonIsCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_IsExpression, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// An `IsExpression` and an `AsExpression` are distinct node types: even with the same
// children, the type check rejects (the generator's `other is IsExpression`).
TEST(CSharp_IsExpression, DoMatchRejectsAsSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression a(aOperand.get(), aType.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(bOperand.get(), bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones both children (distinct objects, not shared), re-parents each to the
// copy, and assigns the flat indices (the `Expression` operand at 0, the `Type` at 1).
TEST(CSharp_IsExpression, CloneDeepClonesAllChildren) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    std::unique_ptr<IsExpression> copy(static_cast<IsExpression*>(e.Clone()));
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 1);
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

// `Clone` of a node with no children yields a node with both slots null.
TEST(CSharp_IsExpression, CloneEmptyNodeHasNullSlots) {
    IsExpression e;
    std::unique_ptr<IsExpression> copy(static_cast<IsExpression*>(e.Clone()));
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_IsExpression, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<IsExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_IsExpression, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    std::unique_ptr<IsExpression> copy(static_cast<IsExpression*>(e.Clone()));
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(e.Type(), type.get());
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with both slots filled. An empty node is
// NOT tested: every slot is required, so `CheckInvariant` would assert "required slot must
// not be empty" on a node with a null child.
TEST(CSharp_IsExpression, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    IsExpression e(operand.get(), type.get());
    e.CheckInvariant();
    SUCCEED();
}
