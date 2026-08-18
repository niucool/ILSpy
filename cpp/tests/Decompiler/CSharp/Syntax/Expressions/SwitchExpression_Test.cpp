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

// Tests for the `SwitchExpressionSection` and `SwitchExpression` concrete nodes
// (cpp/.../Syntax/SwitchExpressionSection.hpp and
// cpp/.../Syntax/Expressions/SwitchExpression.hpp, the ports of the two nodes in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/SwitchExpression.cs). The
// `switch_expression_arm ::= pattern '=>' expression` (C# grammar 12.12) arm and the
// `switch_expression ::= expression 'switch' '{' switch_expression_arm* '}'` governing node.
//
// `SwitchExpressionSection` is a sealed direct-`AstNode` node with two REQUIRED single
// `Expression` slots (`Pattern`/`Body`), the `CastExpression` D243 two-required-single-slot shape
// applied to a direct-`AstNode` base. `SwitchExpression` is a sealed `Expression` with one
// REQUIRED `Expression` slot (the governing expression, the `Expression()` accessor shadowing
// the base type -- the `CastExpression` D243 name-shadowing crux) plus a `SwitchSections`
// collection of `SwitchExpressionSection`, the `InvocationExpression` D248 single-`Expression`-
// slot-plus-a-collection shape with the collection element type a concrete node.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchExpressionSection.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the two nodes under test (plus the leaf
// `Expression`s used as operands and the `InvocationExpression`/`WhileStatement` used for the
// cross-type DoMatch rejections), recording a tag and recursing via `VisitChildren` (the
// inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitSwitchExpressionSection(SwitchExpressionSection* node) override {
        if (node == nullptr) { trace.push_back("<null-section>"); return; }
        trace.push_back("section");
        VisitChildren(node);
    }
    void VisitSwitchExpression(SwitchExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-switch>"); return; }
        trace.push_back("switch");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-this>"); return; }
        trace.push_back("this");
        VisitChildren(node);
    }
    void VisitBaseReferenceExpression(BaseReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-base>"); return; }
        trace.push_back("base");
        VisitChildren(node);
    }
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
        VisitChildren(node);
    }
    void VisitInvocationExpression(InvocationExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-invoke>"); return; }
        trace.push_back("invoke");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match()` holds a NULL capture
// vector -- the D283 crux).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `SwitchExpressionSection` and both its `Expression` children alive in the
// test scope (the port's non-owning raw-pointer child slots -- the D223 design).
struct SectionHolder {
    std::unique_ptr<SwitchExpressionSection> section;
    std::unique_ptr<Expression> pattern;
    std::unique_ptr<Expression> body;
    SwitchExpressionSection* get() const { return section.get(); }
    SwitchExpressionSection* operator->() const { return section.get(); }
};

// Build a `SwitchExpressionSection` with a `NullReferenceExpression` `Pattern` and a
// `ThisReferenceExpression` `Body`. The holder keeps every node alive.
SectionHolder make_Section() {
    SectionHolder h;
    h.section = std::make_unique<SwitchExpressionSection>();
    h.pattern = std::make_unique<NullReferenceExpression>();
    h.body = std::make_unique<ThisReferenceExpression>();
    h.section->Pattern(h.pattern.get());
    h.section->Body(h.body.get());
    return h;
}

// A holder keeping a `SwitchExpression` and its governing `Expression` plus its `SwitchSections`
// collection elements alive in the test scope.
struct SwitchHolder {
    std::unique_ptr<SwitchExpression> sw;
    std::unique_ptr<Expression> governing;
    std::unique_ptr<NullReferenceExpression> sec0Pattern;
    std::unique_ptr<ThisReferenceExpression> sec0Body;
    std::unique_ptr<SwitchExpressionSection> section0;
    std::unique_ptr<BaseReferenceExpression> sec1Pattern;
    std::unique_ptr<PrimitiveExpression> sec1Body;
    std::unique_ptr<SwitchExpressionSection> section1;
    SwitchExpression* get() const { return sw.get(); }
    SwitchExpression* operator->() const { return sw.get(); }
};

// Build a `SwitchExpression` with a `ThisReferenceExpression` governing expression and one
// `SwitchExpressionSection` (a `NullReferenceExpression` pattern + a `ThisReferenceExpression`
// body). The holder keeps every node alive.
SwitchHolder make_SwitchOneSection() {
    SwitchHolder h;
    h.sw = std::make_unique<SwitchExpression>();
    h.governing = std::make_unique<ThisReferenceExpression>();
    h.sw->Expression(h.governing.get());

    h.section0 = std::make_unique<SwitchExpressionSection>();
    h.sec0Pattern = std::make_unique<NullReferenceExpression>();
    h.sec0Body = std::make_unique<ThisReferenceExpression>();
    h.section0->Pattern(h.sec0Pattern.get());
    h.section0->Body(h.sec0Body.get());
    h.sw->SwitchSections().Add(h.section0.get());
    return h;
}

// Build a `SwitchExpression` with a `ThisReferenceExpression` governing expression and two
// `SwitchExpressionSection`s. The holder keeps every node alive.
SwitchHolder make_SwitchTwoSections() {
    SwitchHolder h = make_SwitchOneSection();

    h.section1 = std::make_unique<SwitchExpressionSection>();
    h.sec1Pattern = std::make_unique<BaseReferenceExpression>();
    h.sec1Body = std::make_unique<PrimitiveExpression>(int32_t(42));
    h.section1->Pattern(h.sec1Pattern.get());
    h.section1->Body(h.sec1Body.get());
    h.sw->SwitchSections().Add(h.section1.get());
    return h;
}

} // namespace

// =====================================================================
// SwitchExpressionSection
// =====================================================================

// ---- Is-a --------------------------------------------------------------

// `SwitchExpressionSection` is an `AstNode` (the `dynamic_cast` is-a the slot system and the
// annotation channel use); it is NOT an `Expression`/`Statement`/`AstType` (it derives directly
// from `AstNode`, parallel to -- not under -- those hierarchies).
TEST(CSharp_SwitchExpressionSection, IsAstNodeNotExpressionStatementAstType) {
    SwitchExpressionSection ses;
    EXPECT_NE(dynamic_cast<AstNode*>(&ses), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ses), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ses), nullptr);
}

// `SwitchExpressionSection` is a concrete (non-abstract) and `final` class (the C# is `sealed`).
TEST(CSharp_SwitchExpressionSection, IsConcreteAndFinal) {
    auto ses = std::make_unique<SwitchExpressionSection>();
    ASSERT_NE(ses, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ses.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<SwitchExpressionSection>);
}

// ---- Construction -----------------------------------------------------

// The empty ctor yields a node with both `Pattern` and `Body` null (both are required slots, so
// the node is only valid until both are set).
TEST(CSharp_SwitchExpressionSection, EmptyCtorYieldsNullSlots) {
    SwitchExpressionSection ses;
    EXPECT_EQ(ses.Pattern(), nullptr);
    EXPECT_EQ(ses.Body(), nullptr);
}

// The all-params ctor sets `Pattern` then `Body` in declaration order.
TEST(CSharp_SwitchExpressionSection, AllParamsCtorSetsSlots) {
    auto pattern = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ThisReferenceExpression>();
    SwitchExpressionSection ses(pattern.get(), body.get());
    EXPECT_EQ(ses.Pattern(), pattern.get());
    EXPECT_EQ(ses.Body(), body.get());
}

// ---- The Pattern/Body slots --------------------------------------------

// The `Pattern`/`Body` getters return the backing fields; the setters re-parent the children and
// record the flattened index (the const-index `SetChildNode(ref field, value, index)`).
TEST(CSharp_SwitchExpressionSection, SlotAccessorsAndSetters) {
    SwitchExpressionSection ses;
    auto pattern = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ThisReferenceExpression>();
    ses.Pattern(pattern.get());
    ses.Body(body.get());
    EXPECT_EQ(ses.Pattern(), pattern.get());
    EXPECT_EQ(ses.Body(), body.get());
    EXPECT_EQ(pattern->Parent(), &ses);
    EXPECT_EQ(body->Parent(), &ses);
    EXPECT_EQ(pattern->ChildIndex, 0);
    EXPECT_EQ(body->ChildIndex, 1);
}

// Setting a slot to a second child re-parents the new child and clears the old child's parent.
TEST(CSharp_SwitchExpressionSection, SetterReparentsAndClearsOld) {
    SwitchExpressionSection ses;
    auto p1 = std::make_unique<NullReferenceExpression>();
    auto p2 = std::make_unique<ThisReferenceExpression>();
    ses.Pattern(p1.get());
    EXPECT_EQ(p1->Parent(), &ses);
    ses.Pattern(p2.get());
    EXPECT_EQ(ses.Pattern(), p2.get());
    EXPECT_EQ(p2->Parent(), &ses);
    EXPECT_EQ(p1->Parent(), nullptr);
}

// ---- Slot storage -----------------------------------------------------

// `GetChildCount` is the constant 2; `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat two-case
// index switch.
TEST(CSharp_SwitchExpressionSection, SlotStorageContract) {
    auto h = make_Section();
    EXPECT_EQ(h->GetChildCount(), 2);
    EXPECT_EQ(h->GetChild(0), h->Pattern());
    EXPECT_EQ(h->GetChild(1), h->Body());
    EXPECT_EQ(h->GetChildSlotInfo(0), &SwitchExpressionSection::PatternSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &SwitchExpressionSection::BodySlot);
    EXPECT_THROW((void)h->GetChild(2), std::out_of_range);
    EXPECT_THROW((void)h->GetChildSlotInfo(2), std::out_of_range);
}

// `SetChild` replaces each slot in place.
TEST(CSharp_SwitchExpressionSection, SetChildReplacesSlots) {
    auto h = make_Section();
    auto newPattern = std::make_unique<BaseReferenceExpression>();
    auto newBody = std::make_unique<NullReferenceExpression>();
    h->SetChild(0, newPattern.get());
    h->SetChild(1, newBody.get());
    EXPECT_EQ(h->Pattern(), newPattern.get());
    EXPECT_EQ(h->Body(), newBody.get());
}

// ---- Slot statics -----------------------------------------------------

// The per-node slot statics point at the shared `Slots` kinds (`PatternSlot` at `Slots::Pattern`,
// `BodySlot` at `Slots::SwitchExpressionBody`); both are required (non-optional) and
// non-collection.
TEST(CSharp_SwitchExpressionSection, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(SwitchExpressionSection::PatternSlot.Kind(), &Slots::Pattern);
    EXPECT_EQ(SwitchExpressionSection::BodySlot.Kind(), &Slots::SwitchExpressionBody);
    EXPECT_FALSE(SwitchExpressionSection::PatternSlot.IsCollection());
    EXPECT_FALSE(SwitchExpressionSection::PatternSlot.IsOptional());
    EXPECT_FALSE(SwitchExpressionSection::BodySlot.IsCollection());
    EXPECT_FALSE(SwitchExpressionSection::BodySlot.IsOptional());
    // The two slot statics are distinct (same element type -- compare directly).
    EXPECT_NE(&SwitchExpressionSection::PatternSlot, &SwitchExpressionSection::BodySlot);
}

// The `Pattern` and `Body` children's `Slot()` reports the per-node slot static.
TEST(CSharp_SwitchExpressionSection, ChildrenSlotReportsSlotStatic) {
    auto h = make_Section();
    EXPECT_EQ(h->Pattern()->Slot(), &SwitchExpressionSection::PatternSlot);
    EXPECT_EQ(h->Body()->Slot(), &SwitchExpressionSection::BodySlot);
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitSwitchExpressionSection` (through the concrete type and
// through the abstract `AstNode*` base -- the virtual dispatch). Uses a default-constructed
// (empty) node so `VisitChildren` yields just the node tag (an empty node's `Children()` is
// empty -- the D276/D288 dispatch-test precedent; a filled node would recurse into its slots).
TEST(CSharp_SwitchExpressionSection, AcceptVisitorDispatch) {
    SwitchExpressionSection ses;
    RecordingVisitor v;
    ses.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "section");
}

TEST(CSharp_SwitchExpressionSection, AcceptVisitorVirtualThroughAstNode) {
    SwitchExpressionSection ses;
    AstNode* node = &ses;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "section");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `Pattern` then the `Body` in document order.
TEST(CSharp_SwitchExpressionSection, DepthFirstWalkOrder) {
    auto h = make_Section();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // section, nullref (Pattern), this (Body)
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "section");
    EXPECT_EQ(v.trace[1], "nullref");
    EXPECT_EQ(v.trace[2], "this");
}

// ---- DoMatch ----------------------------------------------------------

// Two `SwitchExpressionSection`s with equal `Pattern`/`Body` operands match.
TEST(CSharp_SwitchExpressionSection, DoMatchMatchesSame) {
    auto a = make_Section();
    auto b = make_Section();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Pattern` mismatch rejects.
TEST(CSharp_SwitchExpressionSection, DoMatchRejectsDifferentPattern) {
    auto a = make_Section();  // Pattern = NullReferenceExpression
    SectionHolder b;
    b.section = std::make_unique<SwitchExpressionSection>();
    b.pattern = std::make_unique<ThisReferenceExpression>();  // different pattern type
    b.body = std::make_unique<ThisReferenceExpression>();
    b.section->Pattern(b.pattern.get());
    b.section->Body(b.body.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` mismatch rejects.
TEST(CSharp_SwitchExpressionSection, DoMatchRejectsDifferentBody) {
    auto a = make_Section();  // Body = ThisReferenceExpression
    SectionHolder b;
    b.section = std::make_unique<SwitchExpressionSection>();
    b.pattern = std::make_unique<NullReferenceExpression>();
    b.body = std::make_unique<NullReferenceExpression>();  // different body type
    b.section->Pattern(b.pattern.get());
    b.section->Body(b.body.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`SwitchExpressionSection` candidate rejects (the type-check gate).
TEST(CSharp_SwitchExpressionSection, DoMatchRejectsNonSectionCandidate) {
    auto a = make_Section();
    NullReferenceExpression other;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &other));
}

// A null candidate rejects (the type-check gate, `dynamic_cast` yields null).
TEST(CSharp_SwitchExpressionSection, DoMatchRejectsNullCandidate) {
    auto a = make_Section();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-copies both `Expression` children (distinct re-parented nodes), is virtual through
// `AstNode*`, and returns `SwitchExpressionSection*` covariantly.
TEST(CSharp_SwitchExpressionSection, CloneDeepCopiesChildren) {
    auto h = make_Section();
    std::unique_ptr<SwitchExpressionSection> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->Pattern(), nullptr);
    EXPECT_NE(copy->Body(), nullptr);
    EXPECT_NE(copy->Pattern(), h->Pattern());
    EXPECT_NE(copy->Body(), h->Body());
    // The clones are re-parented to the copy.
    EXPECT_EQ(copy->Pattern()->Parent(), copy.get());
    EXPECT_EQ(copy->Body()->Parent(), copy.get());
    // The source children keep their original parent.
    EXPECT_EQ(h->Pattern()->Parent(), h.get());
    EXPECT_EQ(h->Body()->Parent(), h.get());
}

TEST(CSharp_SwitchExpressionSection, CloneVirtualThroughAstNode) {
    auto h = make_Section();
    AstNode* node = h.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<SwitchExpressionSection*>(copy.get()), nullptr);
}

// ---- CheckInvariant ---------------------------------------------------

// A `SwitchExpressionSection` with both slots filled passes the inherited `CheckInvariant`.
TEST(CSharp_SwitchExpressionSection, CheckInvariantPassesOnFilledNode) {
    auto h = make_Section();
    h->CheckInvariant();
}

// An EMPTY `SwitchExpressionSection` is REJECTED by `CheckInvariant` -- both slots are REQUIRED,
// so the missing required slots trip the inherited slot-structure verifier. `EXPECT_DEATH`
// (a debug-only assertion; a no-op in NDEBUG).
TEST(CSharp_SwitchExpressionSection, CheckInvariantRejectsEmptyNode) {
    SwitchExpressionSection ses;  // both slots empty
    EXPECT_DEATH(ses.CheckInvariant(), ".*");
}

// =====================================================================
// SwitchExpression
// =====================================================================

// ---- Is-a --------------------------------------------------------------

// `SwitchExpression` is an `Expression` and an `AstNode`; it is NOT an `AstType` (it derives from
// `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_SwitchExpression, IsExpressionAndAstNodeNotAstType) {
    SwitchExpression se;
    EXPECT_NE(dynamic_cast<Expression*>(&se), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&se), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&se), nullptr);
}

// `SwitchExpression` is a concrete (non-abstract) and `final` class (the C# is `sealed`).
TEST(CSharp_SwitchExpression, IsConcreteAndFinal) {
    auto se = std::make_unique<SwitchExpression>();
    ASSERT_NE(se, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(se.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<SwitchExpression>);
}

// ---- The const string --------------------------------------------------

// The `SwitchKeyword` const string is "switch" (the `switch` keyword token the output visitor
// emits). It is a static field, not instance state.
TEST(CSharp_SwitchExpression, SwitchKeywordIsSwitch) {
    EXPECT_STREQ(SwitchExpression::SwitchKeyword, "switch");
}

// ---- Construction -----------------------------------------------------

// The empty ctor yields a node with a null `Expression` (required slot) and an empty
// `SwitchSections` collection.
TEST(CSharp_SwitchExpression, EmptyCtorYieldsNullExpressionAndEmptyCollection) {
    SwitchExpression se;
    EXPECT_EQ(se.Expression(), nullptr);
    EXPECT_EQ(se.SwitchSections().Count(), 0);
    EXPECT_EQ(se.GetChildCount(), 1);  // the single Expression slot counts even when null
}

// The explicit `(Expression)` required-prefix ctor sets the governing expression.
TEST(CSharp_SwitchExpression, RequiredPrefixCtorSetsExpression) {
    auto governing = std::make_unique<ThisReferenceExpression>();
    SwitchExpression se(governing.get());
    EXPECT_EQ(se.Expression(), governing.get());
    EXPECT_EQ(governing->Parent(), &se);
}

// ---- The Expression slot -----------------------------------------------

// The `Expression` getter returns the backing field; the setter re-parents the child and records
// the flattened index 0 (the const-index `SetChildNode(ref field, value, 0)`). The
// `Expression()` accessor shadows the `Expression` base type (the `CastExpression` D243
// name-shadowing crux), exercised here through the public API.
TEST(CSharp_SwitchExpression, ExpressionAccessorAndSetter) {
    SwitchExpression se;
    auto governing = std::make_unique<ThisReferenceExpression>();
    se.Expression(governing.get());
    EXPECT_EQ(se.Expression(), governing.get());
    EXPECT_EQ(governing->Parent(), &se);
    EXPECT_EQ(governing->ChildIndex, 0);
}

// ---- The SwitchSections collection ------------------------------------

// `Add` re-parents a section and maintains its `ChildIndex` incrementally (the collection is the
// node's only collection and last slot, so `supportsIncremental` is true -- an element's
// flattened `ChildIndex` is exactly `1 + its local position`).
TEST(CSharp_SwitchExpression, AddReparentsAndIndexesIncrementally) {
    auto h = make_SwitchTwoSections();
    EXPECT_EQ(h->SwitchSections().Count(), 2);
    EXPECT_EQ(h->SwitchSections().At(0), h.section0.get());
    EXPECT_EQ(h->SwitchSections().At(1), h.section1.get());
    EXPECT_EQ(h.section0->Parent(), h.get());
    EXPECT_EQ(h.section1->Parent(), h.get());
    EXPECT_EQ(h.section0->ChildIndex, 1);
    EXPECT_EQ(h.section1->ChildIndex, 2);
}

// `GetCollectionByKind` returns the `SwitchSections` collection for the
// `SwitchExpressionSection` kind.
TEST(CSharp_SwitchExpression, GetCollectionByKindReturnsSwitchSections) {
    auto h = make_SwitchOneSection();
    AstNodeCollection* coll = h->GetCollectionByKind(&Slots::SwitchExpressionSection);
    ASSERT_NE(coll, nullptr);
    EXPECT_EQ(coll->NodeCount(), 1);
    // A different collection kind returns null (the base fallback).
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Argument), nullptr);
}

// ---- Slot storage -----------------------------------------------------

// `GetChildCount` is `1 + Count`; `GetChild`/`GetChildSlotInfo` walk the single slot then the
// collection.
TEST(CSharp_SwitchExpression, SlotStorageContract) {
    auto h = make_SwitchTwoSections();
    EXPECT_EQ(h->GetChildCount(), 3);  // 1 (Expression) + 2 (sections)
    EXPECT_EQ(h->GetChild(0), h->Expression());
    EXPECT_EQ(h->GetChild(1), h.section0.get());
    EXPECT_EQ(h->GetChild(2), h.section1.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &SwitchExpression::ExpressionSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &SwitchExpression::SwitchSectionsSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &SwitchExpression::SwitchSectionsSlot);
    EXPECT_THROW((void)h->GetChild(3), std::out_of_range);
}

// ---- Slot statics -----------------------------------------------------

// The per-node slot statics point at the shared `Slots` kinds (`ExpressionSlot` at
// `Slots::Expression`, `SwitchSectionsSlot` at `Slots::SwitchExpressionSection`).
TEST(CSharp_SwitchExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(SwitchExpression::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_EQ(SwitchExpression::SwitchSectionsSlot.Kind(), &Slots::SwitchExpressionSection);
    EXPECT_FALSE(SwitchExpression::ExpressionSlot.IsCollection());
    EXPECT_FALSE(SwitchExpression::ExpressionSlot.IsOptional());
    EXPECT_TRUE(SwitchExpression::SwitchSectionsSlot.IsCollection());
    // The two slot statics are distinct (distinct element types -- compare through the base).
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&SwitchExpression::ExpressionSlot),
              static_cast<const CSharpSlotInfo*>(&SwitchExpression::SwitchSectionsSlot));
}

// The `SwitchSections` elements' and the `Expression`'s `Slot()` reports the per-node slot static.
TEST(CSharp_SwitchExpression, CollectionAndExpressionSlotReportsSlotStatic) {
    auto h = make_SwitchTwoSections();
    EXPECT_EQ(h->Expression()->Slot(), &SwitchExpression::ExpressionSlot);
    EXPECT_EQ(h->SwitchSections().At(0)->Slot(), &SwitchExpression::SwitchSectionsSlot);
    EXPECT_EQ(h->SwitchSections().At(1)->Slot(), &SwitchExpression::SwitchSectionsSlot);
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitSwitchExpression` (through the concrete type and through
// the abstract `AstNode*`/`Expression*` bases -- the virtual dispatch). Uses a
// default-constructed (empty) node so `VisitChildren` yields just the node tag (an empty node's
// `Children()` is empty -- the D276/D288 dispatch-test precedent; a filled node would recurse
// into its slots).
TEST(CSharp_SwitchExpression, AcceptVisitorDispatch) {
    SwitchExpression se;
    RecordingVisitor v;
    se.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "switch");
}

TEST(CSharp_SwitchExpression, AcceptVisitorVirtualThroughAstNodeAndExpression) {
    SwitchExpression se;
    AstNode* node = &se;
    Expression* expr = &se;
    RecordingVisitor v1;
    node->AcceptVisitor(v1);
    ASSERT_EQ(v1.trace.size(), 1u);
    EXPECT_EQ(v1.trace[0], "switch");
    RecordingVisitor v2;
    expr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "switch");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the governing `Expression` then the `SwitchSections` in document
// order (each section recurses into its `Pattern` then `Body`).
TEST(CSharp_SwitchExpression, DepthFirstWalkOneSection) {
    auto h = make_SwitchOneSection();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // switch, this (governing), section, nullref (Pattern), this (Body)
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "switch");
    EXPECT_EQ(v.trace[1], "this");
    EXPECT_EQ(v.trace[2], "section");
    EXPECT_EQ(v.trace[3], "nullref");
    EXPECT_EQ(v.trace[4], "this");
}

TEST(CSharp_SwitchExpression, DepthFirstWalkTwoSections) {
    auto h = make_SwitchTwoSections();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // switch, this (governing), section0 (nullref, this), section1 (base, prim)
    ASSERT_EQ(v.trace.size(), 8u);
    EXPECT_EQ(v.trace[0], "switch");
    EXPECT_EQ(v.trace[1], "this");
    EXPECT_EQ(v.trace[2], "section");
    EXPECT_EQ(v.trace[3], "nullref");
    EXPECT_EQ(v.trace[4], "this");
    EXPECT_EQ(v.trace[5], "section");
    EXPECT_EQ(v.trace[6], "base");
    EXPECT_EQ(v.trace[7], "prim");
}

// ---- DoMatch ----------------------------------------------------------

// Two `SwitchExpression`s with equal `Expression`/`SwitchSections` match.
TEST(CSharp_SwitchExpression, DoMatchMatchesSame) {
    auto a = make_SwitchTwoSections();
    auto b = make_SwitchTwoSections();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A governing `Expression` mismatch rejects.
TEST(CSharp_SwitchExpression, DoMatchRejectsDifferentExpression) {
    auto a = make_SwitchOneSection();  // governing = ThisReferenceExpression
    SwitchHolder b;
    b.sw = std::make_unique<SwitchExpression>();
    b.governing = std::make_unique<NullReferenceExpression>();  // different governing type
    b.sw->Expression(b.governing.get());
    b.section0 = std::make_unique<SwitchExpressionSection>();
    b.sec0Pattern = std::make_unique<NullReferenceExpression>();
    b.sec0Body = std::make_unique<ThisReferenceExpression>();
    b.section0->Pattern(b.sec0Pattern.get());
    b.section0->Body(b.sec0Body.get());
    b.sw->SwitchSections().Add(b.section0.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `SwitchSections` count mismatch rejects (the collection recursive DoMatch).
TEST(CSharp_SwitchExpression, DoMatchRejectsDifferentSectionCount) {
    auto a = make_SwitchTwoSections();
    auto b = make_SwitchOneSection();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `SwitchSections` element mismatch rejects (the recursive term delegates to the section's
// `DoMatch`).
TEST(CSharp_SwitchExpression, DoMatchRejectsDifferentSectionPattern) {
    auto a = make_SwitchTwoSections();
    SwitchHolder b;
    b.sw = std::make_unique<SwitchExpression>();
    b.governing = std::make_unique<ThisReferenceExpression>();
    b.sw->Expression(b.governing.get());
    b.section0 = std::make_unique<SwitchExpressionSection>();
    b.sec0Pattern = std::make_unique<NullReferenceExpression>();
    b.sec0Body = std::make_unique<ThisReferenceExpression>();
    b.section0->Pattern(b.sec0Pattern.get());
    b.section0->Body(b.sec0Body.get());
    b.sw->SwitchSections().Add(b.section0.get());
    b.section1 = std::make_unique<SwitchExpressionSection>();
    auto diffPattern = std::make_unique<NullReferenceExpression>();  // different from a's BaseReferenceExpression
    b.sec1Body = std::make_unique<PrimitiveExpression>(int32_t(42));
    b.section1->Pattern(diffPattern.get());
    b.section1->Body(b.sec1Body.get());
    b.sw->SwitchSections().Add(b.section1.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`SwitchExpression` candidate rejects (the type-check gate).
TEST(CSharp_SwitchExpression, DoMatchRejectsNonSwitchCandidate) {
    auto a = make_SwitchOneSection();
    InvocationExpression other;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &other));
}

// A null candidate rejects.
TEST(CSharp_SwitchExpression, DoMatchRejectsNullCandidate) {
    auto a = make_SwitchOneSection();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-copies the governing `Expression` and every `SwitchSections` element (distinct
// re-parented nodes), is virtual through `AstNode*`/`Expression*`, and returns `SwitchExpression*`
// covariantly.
TEST(CSharp_SwitchExpression, CloneDeepCopiesAllSlots) {
    auto h = make_SwitchTwoSections();
    std::unique_ptr<SwitchExpression> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), h->Expression());
    EXPECT_EQ(copy->SwitchSections().Count(), 2);
    // The cloned governing expression is re-parented to the copy.
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    // The cloned sections are distinct and re-parented.
    EXPECT_NE(copy->SwitchSections().At(0), h.section0.get());
    EXPECT_NE(copy->SwitchSections().At(1), h.section1.get());
    EXPECT_EQ(copy->SwitchSections().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->SwitchSections().At(1)->Parent(), copy.get());
    // The source children keep their original parent.
    EXPECT_EQ(h->Expression()->Parent(), h.get());
    EXPECT_EQ(h.section0->Parent(), h.get());
    EXPECT_EQ(h.section1->Parent(), h.get());
}

TEST(CSharp_SwitchExpression, CloneVirtualAndCovariant) {
    auto h = make_SwitchOneSection();
    AstNode* node = h.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<SwitchExpression*>(copy.get()), nullptr);
    Expression* exprCopy = dynamic_cast<Expression*>(copy.get());
    ASSERT_NE(exprCopy, nullptr);
    // The covariant Clone through Expression* returns SwitchExpression* (the Expression::Clone
    // pure-virtual).
    std::unique_ptr<SwitchExpression> typed(dynamic_cast<SwitchExpression*>(exprCopy->Clone()));
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->SwitchSections().Count(), 1);
}

// ---- CheckInvariant ---------------------------------------------------

// A `SwitchExpression` with the governing `Expression` set (and any number of sections) passes
// the inherited `CheckInvariant`.
TEST(CSharp_SwitchExpression, CheckInvariantPassesOnFilledNode) {
    auto h = make_SwitchTwoSections();
    h->CheckInvariant();
}

// An EMPTY `SwitchExpression` (no governing `Expression`) is REJECTED by `CheckInvariant` -- the
// `Expression` slot is REQUIRED, so the missing required slot trips the inherited slot-structure
// verifier. `EXPECT_DEATH` (a debug-only assertion; a no-op in NDEBUG).
TEST(CSharp_SwitchExpression, CheckInvariantRejectsEmptyNode) {
    SwitchExpression se;  // no governing Expression
    EXPECT_DEATH(se.CheckInvariant(), ".*");
}
