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

// Tests for the `CastExpression` concrete node (cpp/.../Expressions/CastExpression.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/Expressions/CastExpression.cs) -- the first
// AstType-bearing `Expression` node (a sealed `Expression` with a required `AstType` `Type`
// slot at index 0 + a required `Expression` `Expression` slot at index 1, reusing the
// already-ported `Slots::Type`/`Slots::Expression` kinds). Exercises the slot-storage contract
// (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` over two required single slots at
// const indices), the `Type`/`Expression` accessors (the latter exercising the `class
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
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
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

// A recording depth-first visitor: overrides `VisitCastExpression` (and the nodes it holds),
// recording a tag and recursing via `VisitChildren` (the inherited depth-first default). The
// trace is the visited nodes in pre-order (document order). The `Type` slot is exercised with
// a `PrimitiveType` (the simplest leaf `AstType`), so `VisitPrimitiveType` records the keyword.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitCastExpression(CastExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
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

// ---- is-a --------------------------------------------------------------

// `CastExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, not `AstType`).
TEST(CSharp_CastExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Construction -----------------------------------------------------

// The default ctor: both slots are null (no type, no operand). The two required slots still
// occupy their flattened indices even when empty, so `GetChildCount` is 2.
TEST(CSharp_CastExpression, DefaultCtor) {
    CastExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 2);
}

// The all-params ctor sets both slots, parents each child, and assigns the flat indices 0/1
// directly (the const-index `SetChildNode` path for each single slot).
TEST(CSharp_CastExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    PrimitiveType* typePtr = type.get();
    NullReferenceExpression* operandPtr = operand.get();
    CastExpression e(type.get(), operand.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(e.Expression(), operandPtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
    EXPECT_EQ(operandPtr->Parent(), &e);
    EXPECT_EQ(operandPtr->ChildIndex, 1);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Type` setter fills the slot, parents the child, and assigns the flat index 0 directly.
TEST(CSharp_CastExpression, TypeSetterFillsAndParents) {
    CastExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

// The `Expression` setter fills the slot, parents the child, and assigns the flat index 1
// directly.
TEST(CSharp_CastExpression, ExpressionSetterFillsAndParents) {
    CastExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_EQ(operand->Parent(), &e);
    EXPECT_EQ(operand->ChildIndex, 1);
}

// A null `Type` setter clears the slot and detaches the old child (the slot is required, but
// the setter still tolerates a null to detach -- the invariant is enforced by
// `CheckInvariant`, not by the setter).
TEST(CSharp_CastExpression, NullTypeSetterClearsSlot) {
    CastExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

// A null `Expression` setter clears the slot and detaches the old child.
TEST(CSharp_CastExpression, NullExpressionSetterClearsSlot) {
    CastExpression e;
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Expression(operand.get());
    e.Expression(nullptr);
    EXPECT_EQ(e.Expression(), nullptr);
    EXPECT_EQ(operand->Parent(), nullptr);
    EXPECT_EQ(operand->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_CastExpression, GetChildCountIsTwo) {
    CastExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise.
TEST(CSharp_CastExpression, GetChildReturnsSlots) {
    CastExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    e.Type(type.get());
    e.Expression(operand.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_EQ(e.GetChild(1), operand.get());
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_CastExpression, SetChildRoutesToSlots) {
    CastExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    e.SetChild(1, operand.get());
    EXPECT_EQ(e.Expression(), operand.get());
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_CastExpression, GetChildSlotInfoReturnsSlotStatics) {
    CastExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &CastExpression::TypeSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &CastExpression::ExpressionSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic
// position identity `node.Slot.Kind == Slots.X` relies on). `Type` reuses `Slots::Type`
// (ported by `Attribute`); `Expression` reuses `Slots::Expression` (ported by
// `UnaryOperatorExpression`).
TEST(CSharp_CastExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    ASSERT_EQ(type->Slot(), &CastExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
    ASSERT_EQ(operand->Slot(), &CastExpression::ExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

// The slot statics are typed: the `TypeSlot` accepts an `AstType` (a `PrimitiveType` is, a
// `NullReferenceExpression` is not); the `ExpressionSlot` accepts an `Expression` (a
// `NullReferenceExpression` is, a `PrimitiveType` is not). This cross-checks that the two
// slots' element types are distinct and faithful to the C#.
TEST(CSharp_CastExpression, SlotsAreInstanceOfTheirElementTypes) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(CastExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(CastExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(CastExpression::ExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(CastExpression::ExpressionSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(CastExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(CastExpression::ExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitCastExpression`.
TEST(CSharp_CastExpression, AcceptVisitorDispatchesToVisit) {
    CastExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cast");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_CastExpression, AcceptVisitorIsVirtual) {
    CastExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cast");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "cast");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the cast node, then its two children in document order (the
// `Type` `PrimitiveType`, then the `Expression` operand).
TEST(CSharp_CastExpression, DepthFirstWalksChildrenInOrder) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "cast");
    EXPECT_EQ(v.trace[1], "prim:int");
    EXPECT_EQ(v.trace[2], "nullref");
}

// A node with no children records just the cast node.
TEST(CSharp_CastExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    CastExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "cast");
}

// ---- DoMatch (the generated non-nullable-recursive match, two terms) --

// Two nodes with the same-type children in both slots match.
TEST(CSharp_CastExpression, DoMatchMatchesSameChildren) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    auto bOperand = std::make_unique<NullReferenceExpression>();
    CastExpression b(bType.get(), bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Type` slot (the first term) rejects.
TEST(CSharp_CastExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("long")); // different type keyword
    auto bOperand = std::make_unique<NullReferenceExpression>();
    CastExpression b(bType.get(), bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Expression` slot (the second term) rejects.
TEST(CSharp_CastExpression, DoMatchRejectsDifferentExpression) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    auto bOperand = std::make_unique<ThisReferenceExpression>(); // different operand type
    CastExpression b(bType.get(), bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null pattern `Type` rejects: the port guards the non-nullable recursive dispatch against
// a missing child (the C# would null-dereference `this.Type`; the port returns false instead
// of crashing).
TEST(CSharp_CastExpression, DoMatchNullPatternTypeRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(nullptr, aOperand.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    auto bOperand = std::make_unique<NullReferenceExpression>();
    CastExpression b(bType.get(), bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Type` rejects (the guard: the candidate's `Type` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_CastExpression, DoMatchNullCandidateTypeRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());

    auto bOperand = std::make_unique<NullReferenceExpression>();
    CastExpression b(nullptr, bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// All children absent (null) on both sides reject: the guard treats a missing required child
// as no-match (the C# would null-dereference; the port returns false).
TEST(CSharp_CastExpression, DoMatchAllChildrenNullRejects) {
    CastExpression a(nullptr, nullptr);
    CastExpression b(nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: an `int`
// `Type` vs a `long` `Type` rejects (the `PrimitiveType` child's `MatchString` on `Keyword`
// fails), even when the `Expression` slot would match.
TEST(CSharp_CastExpression, DoMatchDelegatesToChildDoMatch) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());

    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    auto bOperand = std::make_unique<NullReferenceExpression>();
    CastExpression b(bType.get(), bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `CastExpression`) rejects.
TEST(CSharp_CastExpression, DoMatchRejectsNonCastCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_CastExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    auto aOperand = std::make_unique<NullReferenceExpression>();
    CastExpression a(aType.get(), aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones both children (distinct objects, not shared), re-parents each to the
// copy, and assigns the flat indices.
TEST(CSharp_CastExpression, CloneDeepClonesAllChildren) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    std::unique_ptr<CastExpression> copy(static_cast<CastExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), operand.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 1);
    // The cloned Type keeps its keyword (the PrimitiveType child deep-copy).
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

// `Clone` of a node with no children yields a node with both slots null (the `Clone` skips a
// child clone when the child is absent).
TEST(CSharp_CastExpression, CloneEmptyNodeHasNullSlots) {
    CastExpression e;
    std::unique_ptr<CastExpression> copy(static_cast<CastExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_CastExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<CastExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_CastExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    std::unique_ptr<CastExpression> copy(static_cast<CastExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(e.Expression(), operand.get());
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with both slots filled (the slot-structure
// verifier finds each required slot filled and each child's Parent/ChildIndex consistent).
// An empty node is NOT tested: every slot is required, so `CheckInvariant` would assert
// "required slot must not be empty" on a node with a null child.
TEST(CSharp_CastExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto operand = std::make_unique<NullReferenceExpression>();
    CastExpression e(type.get(), operand.get());
    e.CheckInvariant();
    SUCCEED();
}
