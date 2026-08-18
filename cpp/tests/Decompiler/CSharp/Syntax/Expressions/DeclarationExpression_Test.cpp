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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `DeclarationExpression` concrete node (cpp/.../Expressions/
// DeclarationExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.cs) -- the
// `declaration_expression ::= type variable_designation` (C# grammar 12.20): a sealed
// `Expression` with a required `AstType` `Type` slot at index 0 + a required
// `VariableDesignation` `Designation` slot at index 1, reusing the already-ported
// `Slots::Type`/`Slots::VariableDesignation` kinds. The `CastExpression` D243 two-required-
// single-slot shape with the second operand a `VariableDesignation` instead of an
// `Expression`, and NO name-shadowing crux (the `Designation()` accessor does not collide
// with the `VariableDesignation` class since the property is `Designation`, not
// `VariableDesignation`). Exercises the slot-storage contract over two required single slots
// at const indices, the `Type`/`Designation` accessors, the generated `DoMatch` over TWO
// NON-nullable recursive children (the direct dispatch via `MatchRequired`, with the
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
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitDeclarationExpression` (and the nodes it
// holds), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order (document order). The `Type` slot is
// exercised with a `PrimitiveType` (the simplest leaf `AstType`, no `[Slot]` children), and
// the `Designation` slot with a `SingleVariableDesignation` (whose backing `IdentifierToken`
// is a visited child, so the walk recurses into an `Identifier`).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitDeclarationExpression(DeclarationExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-decl>");
            return;
        }
        trace.push_back("decl");
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) {
            trace.push_back("<null-prim>");
            return;
        }
        trace.push_back("prim");
        VisitChildren(node);
    }
    void VisitSingleVariableDesignation(SingleVariableDesignation* node) override {
        if (node == nullptr) {
            trace.push_back("<null-single>");
            return;
        }
        trace.push_back("single");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) {
            trace.push_back("<null-id>");
            return;
        }
        trace.push_back("id");
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

// Builds a fully-formed `DeclarationExpression` (both required slots filled) keeping every
// node alive in the holder's `unique_ptr`s (the port's child slots are non-owning raw pointers
// -- the test must keep the children alive in the test scope, the AttributeSection D241
// use-after-free precedent). `get()` exposes the `DeclarationExpression*`.
struct DeclHolder {
    std::unique_ptr<PrimitiveType> type;
    std::unique_ptr<SingleVariableDesignation> designation;
    std::unique_ptr<DeclarationExpression> node;

    DeclarationExpression* get() const { return node.get(); }
};

static DeclHolder makeDecl(std::string typeName = "int", std::string designationName = "x") {
    DeclHolder h;
    h.type = std::make_unique<PrimitiveType>(std::move(typeName));
    h.designation = std::make_unique<SingleVariableDesignation>(std::move(designationName));
    h.node = std::make_unique<DeclarationExpression>(h.type.get(), h.designation.get());
    return h;
}

} // namespace

// ---- is-a --------------------------------------------------------------

// `DeclarationExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the
// slot system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, not `AstType`).
TEST(CSharp_DeclarationExpression, IsAstNodeAndExpressionNotAstType) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    EXPECT_NE(dynamic_cast<AstNode*>(e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(e), nullptr);
}

// `DeclarationExpression` is `final` (the C# `sealed`).
TEST(CSharp_DeclarationExpression, IsFinal) {
    EXPECT_TRUE(std::is_final_v<DeclarationExpression>);
}

// ---- Construction -----------------------------------------------------

// The default ctor: both slots are null (no type, no designation). The two required slots
// still occupy their flattened indices even when empty, so `GetChildCount` is 2.
TEST(CSharp_DeclarationExpression, DefaultCtor) {
    DeclarationExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.Designation(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 2);
}

// The all-params ctor sets both slots, parents each child, and assigns the flat indices 0/1
// directly (the const-index `SetChildNode` path for each single slot).
TEST(CSharp_DeclarationExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>("int");
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    PrimitiveType* typePtr = type.get();
    SingleVariableDesignation* designationPtr = designation.get();
    DeclarationExpression e(type.get(), designation.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(e.Designation(), designationPtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
    EXPECT_EQ(designationPtr->Parent(), &e);
    EXPECT_EQ(designationPtr->ChildIndex, 1);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Type` setter fills the slot, parents the child, and assigns the flat index 0 directly.
TEST(CSharp_DeclarationExpression, TypeSetterFillsAndParents) {
    DeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>("int");
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

// The `Designation` setter fills the slot, parents the child, and assigns the flat index 1
// directly.
TEST(CSharp_DeclarationExpression, DesignationSetterFillsAndParents) {
    DeclarationExpression e;
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    e.Designation(designation.get());
    EXPECT_EQ(e.Designation(), designation.get());
    EXPECT_EQ(designation->Parent(), &e);
    EXPECT_EQ(designation->ChildIndex, 1);
}

// A null `Type` setter clears the slot and detaches the old child (the slot is required, but
// the setter still tolerates a null to detach -- the invariant is enforced by `CheckInvariant`,
// not by the setter).
TEST(CSharp_DeclarationExpression, NullTypeSetterClearsSlot) {
    DeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>("int");
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

// A null `Designation` setter clears the slot and detaches the old child.
TEST(CSharp_DeclarationExpression, NullDesignationSetterClearsSlot) {
    DeclarationExpression e;
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    e.Designation(designation.get());
    e.Designation(nullptr);
    EXPECT_EQ(e.Designation(), nullptr);
    EXPECT_EQ(designation->Parent(), nullptr);
    EXPECT_EQ(designation->ChildIndex, -1);
}

// `GetChildCount` is the constant 2 (two single slots, no collection).
TEST(CSharp_DeclarationExpression, GetChildCountIsTwo) {
    DeclarationExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise.
TEST(CSharp_DeclarationExpression, GetChildReturnsSlots) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    EXPECT_EQ(e->GetChild(0), h.type.get());
    EXPECT_EQ(e->GetChild(1), h.designation.get());
    EXPECT_THROW(e->GetChild(2), std::out_of_range);
    EXPECT_THROW(e->GetChild(-1), std::out_of_range);
}

// `GetChild` on an empty node returns null for each single slot (the slots occupy their flat
// indices even when empty).
TEST(CSharp_DeclarationExpression, GetChildOnEmptyNodeReturnsNullSingles) {
    DeclarationExpression e;
    EXPECT_EQ(e.GetChild(0), nullptr);
    EXPECT_EQ(e.GetChild(1), nullptr);
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_DeclarationExpression, SetChildRoutesToSlots) {
    DeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>("int");
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    e.SetChild(1, designation.get());
    EXPECT_EQ(e.Designation(), designation.get());
    EXPECT_EQ(designation->Parent(), &e);
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_DeclarationExpression, GetChildSlotInfoReturnsSlotStatics) {
    DeclarationExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &DeclarationExpression::TypeSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &DeclarationExpression::DesignationSlot);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic position
// identity `node.Slot.Kind == Slots.X` relies on). `Type` reuses `Slots::Type` (ported by
// `Attribute`); `Designation` reuses `Slots::VariableDesignation` (ported by
// `ParenthesizedVariableDesignation` as a collection kind, reused as a single by
// `ForeachStatement`).
TEST(CSharp_DeclarationExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    ASSERT_EQ(h.type->Slot(), &DeclarationExpression::TypeSlot);
    EXPECT_EQ(h.type->Slot()->Kind(), &Slots::Type);
    ASSERT_EQ(h.designation->Slot(), &DeclarationExpression::DesignationSlot);
    EXPECT_EQ(h.designation->Slot()->Kind(), &Slots::VariableDesignation);
}

// The slot statics are typed: the `TypeSlot` accepts an `AstType` (a `PrimitiveType` is, a
// `SingleVariableDesignation` is not -- it derives from `VariableDesignation`, not `AstType`);
// the `DesignationSlot` accepts a `VariableDesignation` (a `SingleVariableDesignation` is, a
// `PrimitiveType` is not). A null is rejected by both. This cross-checks that the two slots'
// element types are distinct and faithful to the C#.
TEST(CSharp_DeclarationExpression, SlotsAreInstanceOfTheirElementTypes) {
    PrimitiveType prim;
    SingleVariableDesignation svd("x");
    EXPECT_TRUE(DeclarationExpression::TypeSlot.IsInstanceOfType(&prim));
    EXPECT_FALSE(DeclarationExpression::TypeSlot.IsInstanceOfType(&svd));
    EXPECT_TRUE(DeclarationExpression::DesignationSlot.IsInstanceOfType(&svd));
    EXPECT_FALSE(DeclarationExpression::DesignationSlot.IsInstanceOfType(&prim));
    EXPECT_FALSE(DeclarationExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(DeclarationExpression::DesignationSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitDeclarationExpression`.
TEST(CSharp_DeclarationExpression, AcceptVisitorDispatchesToVisit) {
    DeclarationExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "decl");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_DeclarationExpression, AcceptVisitorIsVirtual) {
    DeclarationExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "decl");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "decl");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `decl` node, then its two children in document order (the
// `Type` `PrimitiveType`, then the `Designation` `SingleVariableDesignation` -- which recurses
// into its backing `IdentifierToken`).
TEST(CSharp_DeclarationExpression, DepthFirstWalksChildrenInOrder) {
    auto h = makeDecl();
    RecordingVisitor v;
    h.get()->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "decl", "prim", "single", "id"}));
}

// A node with no children records just the `decl` node.
TEST(CSharp_DeclarationExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    DeclarationExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "decl");
}

// ---- DoMatch (the generated non-nullable-recursive match, two terms) --

// Two nodes with the same-type children in both slots match.
TEST(CSharp_DeclarationExpression, DoMatchMatchesSameChildren) {
    auto a = makeDecl("int", "x");
    auto b = makeDecl("int", "x");
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A mismatch in the `Type` slot (the first term) rejects (a `PrimitiveType("int")` pattern vs
// a `PrimitiveType("string")` candidate rejects on the `PrimitiveType`'s `MatchString` DoMatch).
TEST(CSharp_DeclarationExpression, DoMatchRejectsDifferentType) {
    auto a = makeDecl("int", "x");
    auto b = makeDecl("string", "x");
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A mismatch in the `Designation` slot (the second term) rejects (a `SingleVariableDesignation("x")`
// pattern vs a `SingleVariableDesignation("y")` candidate rejects on the
// `SingleVariableDesignation`'s `MatchString` DoMatch).
TEST(CSharp_DeclarationExpression, DoMatchRejectsDifferentDesignation) {
    auto a = makeDecl("int", "x");
    auto b = makeDecl("int", "y");
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null pattern `Type` rejects: the port guards the non-nullable recursive dispatch against a
// missing child (the C# would null-dereference `this.Type`; the port returns false instead of
// crashing).
TEST(CSharp_DeclarationExpression, DoMatchNullPatternTypeRejects) {
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    DeclarationExpression a(nullptr, designation.get());

    auto b = makeDecl();
    EXPECT_FALSE(DoMatchAgainst(&a, b.get()));
}

// A null candidate `Type` rejects (the guard: the candidate's `Type` is null, so
// `MatchRequired` flows through the child's `DoMatch(nullptr)` which returns false).
TEST(CSharp_DeclarationExpression, DoMatchNullCandidateTypeRejects) {
    auto a = makeDecl();
    auto designation = std::make_unique<SingleVariableDesignation>("x");
    DeclarationExpression b(nullptr, designation.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

// A null pattern `Designation` rejects (the guard, second term).
TEST(CSharp_DeclarationExpression, DoMatchNullPatternDesignationRejects) {
    auto type = std::make_unique<PrimitiveType>("int");
    DeclarationExpression a(type.get(), nullptr);

    auto b = makeDecl();
    EXPECT_FALSE(DoMatchAgainst(&a, b.get()));
}

// A null candidate `Designation` rejects (the guard, second term).
TEST(CSharp_DeclarationExpression, DoMatchNullCandidateDesignationRejects) {
    auto a = makeDecl();
    auto type = std::make_unique<PrimitiveType>("int");
    DeclarationExpression b(type.get(), nullptr);
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

// All children absent (null) on both sides reject: the guard treats a missing required child
// as no-match (the C# would null-dereference; the port returns false).
TEST(CSharp_DeclarationExpression, DoMatchAllChildrenNullRejects) {
    DeclarationExpression a(nullptr, nullptr);
    DeclarationExpression b(nullptr, nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The recursive dispatch delegates each child match to the child's own `DoMatch`: a
// `PrimitiveType("int")` `Type` vs a `PrimitiveType("string")` `Type` rejects (the `Type` child's
// `MatchString` DoMatch fails), even when the `Designation` slot would match.
TEST(CSharp_DeclarationExpression, DoMatchDelegatesToChildDoMatch) {
    auto a = makeDecl("int", "x");
    auto b = makeDecl("string", "x");
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A different-type candidate (not a `DeclarationExpression`) rejects. `CastExpression` is the
// structural twin (the same two-required-single-slot shape), so this cross-structural-twin
// rejection confirms the two near-identical node types remain distinct in the pattern matcher
// via the `other is DeclarationExpression` type-check gate (the D249 cross-structural-twin
// rejection precedent).
TEST(CSharp_DeclarationExpression, DoMatchRejectsCastExpressionStructuralTwin) {
    auto a = makeDecl();
    auto castType = std::make_unique<PrimitiveType>("int");
    auto castOperand = std::make_unique<NullReferenceExpression>();
    CastExpression c(castType.get(), castOperand.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &c));
    EXPECT_FALSE(DoMatchAgainst(&c, a.get()));
}

// A different-type candidate (not a `DeclarationExpression`) rejects (a plain `PrimitiveType`).
TEST(CSharp_DeclarationExpression, DoMatchRejectsNonDeclarationCandidate) {
    auto a = makeDecl();
    PrimitiveType prim("int");
    EXPECT_FALSE(DoMatchAgainst(a.get(), &prim));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_DeclarationExpression, DoMatchRejectsNullCandidate) {
    auto a = makeDecl();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones both children (distinct objects, not shared), re-parents each to the
// copy, and assigns the flat indices.
TEST(CSharp_DeclarationExpression, CloneDeepClonesAllChildren) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    std::unique_ptr<DeclarationExpression> copy(static_cast<DeclarationExpression*>(e->Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), h.type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    ASSERT_NE(copy->Designation(), nullptr);
    EXPECT_NE(copy->Designation(), h.designation.get());
    EXPECT_EQ(copy->Designation()->Parent(), copy.get());
    EXPECT_EQ(copy->Designation()->ChildIndex, 1);
}

// `Clone` of a node with no children yields a node with both slots null (the `Clone` skips a
// child clone when the child is absent).
TEST(CSharp_DeclarationExpression, CloneEmptyNodeHasNullSlots) {
    DeclarationExpression e;
    std::unique_ptr<DeclarationExpression> copy(static_cast<DeclarationExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Designation(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_DeclarationExpression, CloneIsVirtualAndCovariant) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    AstNode* asNode = e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<DeclarationExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_DeclarationExpression, CloneDoesNotDetachSource) {
    auto h = makeDecl();
    DeclarationExpression* e = h.get();
    std::unique_ptr<DeclarationExpression> copy(static_cast<DeclarationExpression*>(e->Clone()));
    EXPECT_EQ(h.type->Parent(), e);
    EXPECT_EQ(e->Type(), h.type.get());
    EXPECT_EQ(e->Designation(), h.designation.get());
}

// The deep-cloned `Type` carries the same keyword: downcast to `PrimitiveType` to read it (the
// recurring abstract-base-slot-accessor downcast gotcha -- `Type()` returns the abstract
// `AstType*`, so a derived-only member needs a `dynamic_cast`).
TEST(CSharp_DeclarationExpression, CloneCopiesTypeName) {
    auto h = makeDecl("int", "x");
    DeclarationExpression* e = h.get();
    std::unique_ptr<DeclarationExpression> copy(static_cast<DeclarationExpression*>(e->Clone()));
    auto* prim = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(prim, nullptr);
    EXPECT_EQ(prim->Keyword(), std::string("int"));
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with both slots filled (the slot-structure
// verifier finds each required slot filled and each child's Parent/ChildIndex consistent).
// An empty node is NOT tested here: every slot is required, so `CheckInvariant` would assert
// "required slot must not be empty" on a node with a null child (exercised below under
// `#ifndef NDEBUG`).
TEST(CSharp_DeclarationExpression, CheckInvariantPassesOnFilledNode) {
    auto h = makeDecl();
    h.get()->CheckInvariant();
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_DeclarationExpression, CheckInvariantRejectsEmptyNode) {
    // Both slots are required (non-nullable), so an empty node violates the required-slot
    // invariant (the `UnaryOperatorExpression` D231 required-slot behavior -- the assert
    // fires in debug).
    DeclarationExpression e;
    EXPECT_DEATH(e.CheckInvariant(), "");
}
#endif
