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

// Tests for the switch family of C# AST nodes -- `CaseLabel` (a sealed `AstNode` deriving directly
// from the `AstNode` root, a leaf of a `SwitchSection`'s `CaseLabels` collection, with a single
// NULLABLE `Expression?` `Expression` slot -- the `ReturnStatement` D255 nullable-`Expression?`-
// slot shape applied to a direct-`AstNode`-derived node, plus `CaseKeyword`/`DefaultKeyword` const
// strings; the `Expression()` accessor shadows the `Expression` base type, the D231 crux),
// `SwitchSection` (a non-sealed `AstNode` with TWO collections -- a `CaseLabels
// AstNodeCollection<CaseLabel>` + a `Statements AstNodeCollection<Statement>` reusing the
// `WhileStatement` D258 `Slots::EmbeddedStatement` kind as a collection; both non-incremental, the
// `ComposedType` D242 two-collection shape with NO single slot between; plus the NEW cycle-broken
// `Slots::CaseLabel` kind), and `SwitchStatement` (a sealed `Statement` structurally the
// `InvocationExpression` D248 shape -- a single REQUIRED `Expression` child at index 0 + a
// `SwitchSections AstNodeCollection<SwitchSection>` collection at index 1, incremental -- with the
// `SwitchKeyword` const string; the `Expression()` accessor shadows the `Expression` base type;
// plus the NEW cycle-broken `Slots::SwitchSection` kind) -- the next in-order Phase-5 piece per
// the D267 plan ("SwitchStatement, TryCatchStatement, LocalFunctionDeclarationStatement,
// VariableDeclarationStatement ..."). The three suites share a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 multi-suite pattern); each is independently
// `--gtest_filter`-selectable.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the three switch-family `Visit` methods under test
// (plus the leaf statements the sections hold and the leaf expressions the labels/statements
// hold), recording a tag and recursing via `VisitChildren` (the inherited depth-first default).
// The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitCaseLabel(CaseLabel* node) override {
        if (node == nullptr) { trace.push_back("<null-case>"); return; }
        trace.push_back(node->Expression() != nullptr ? "case" : "default");
        VisitChildren(node);
    }
    void VisitSwitchSection(SwitchSection* node) override {
        if (node == nullptr) { trace.push_back("<null-section>"); return; }
        trace.push_back("section");
        VisitChildren(node);
    }
    void VisitSwitchStatement(SwitchStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-switch>"); return; }
        trace.push_back("switch");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-break>"); return; }
        trace.push_back("break");
        VisitChildren(node);
    }
    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-continue>"); return; }
        trace.push_back("continue");
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
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ==========================================================================
// CaseLabel (the case/default label leaf)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `CaseLabel` derives directly from the `AstNode` root (not `Expression`/`Statement`); it is a
// leaf of a `SwitchSection`'s `CaseLabels` collection.
TEST(CSharp_CaseLabel, IsAstNodeNotExpressionNotStatement) {
    CaseLabel cl;
    EXPECT_NE(dynamic_cast<AstNode*>(&cl), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&cl), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&cl), nullptr);
}

// `CaseLabel` is `final` (the C# `sealed`).
TEST(CSharp_CaseLabel, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<CaseLabel>);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_CaseLabel, ConstKeywords) {
    EXPECT_STREQ(CaseLabel::CaseKeyword, "case");
    EXPECT_STREQ(CaseLabel::DefaultKeyword, "default");
}

// The empty ctor leaves `Expression` null (the `default:` label form). `GetChildCount` is 1 (the
// one single slot, empty).
TEST(CSharp_CaseLabel, EmptyCtorHasNullExpression) {
    CaseLabel cl;
    EXPECT_EQ(cl.Expression(), nullptr);
    EXPECT_EQ(cl.GetChildCount(), 1);
    EXPECT_EQ(cl.StartLocation(), TextLocation::Empty);
}

// The `(Expression*)` ctor (the generated all-params ctor) sets the `Expression` and parents it
// at index 0.
TEST(CSharp_CaseLabel, CtorSetsExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    EXPECT_EQ(cl.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &cl);
    EXPECT_EQ(expr->ChildIndex, 0);
}

// ---- The `Expression` slot -----------------------------------------------

// The `Expression` setter re-parents the new expression and detaches the old one.
TEST(CSharp_CaseLabel, ExpressionSetterReparentsAndDetaches) {
    CaseLabel cl;
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    cl.Expression(a.get());
    EXPECT_EQ(a->Parent(), &cl);
    EXPECT_EQ(cl.Expression(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    cl.Expression(b.get());
    EXPECT_EQ(b->Parent(), &cl);
    EXPECT_EQ(cl.Expression(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Expression` setter clears and detaches the old expression (back to the `default:` form).
TEST(CSharp_CaseLabel, ExpressionSetterClearsWithNull) {
    CaseLabel cl;
    auto a = std::make_unique<NullReferenceExpression>();
    cl.Expression(a.get());
    cl.Expression(nullptr);
    EXPECT_EQ(cl.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot storage --------------------------------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat single-case switch over the one slot.
TEST(CSharp_CaseLabel, SlotStorageFlatSwitch) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    EXPECT_EQ(cl.GetChild(0), expr.get());
    EXPECT_EQ(cl.GetChildSlotInfo(0), &CaseLabel::ExpressionSlot);
    auto b = std::make_unique<ThisReferenceExpression>();
    cl.SetChild(0, b.get());
    EXPECT_EQ(cl.Expression(), b.get());
    EXPECT_EQ(b->Parent(), &cl);
    EXPECT_EQ(expr->Parent(), nullptr);  // the old expression was detached by SetChildNode
}

// `GetChild`/`SetChild`/`GetChildSlotInfo` throw on an out-of-range index.
TEST(CSharp_CaseLabel, SlotStorageThrowsOutOfRange) {
    CaseLabel cl;
    EXPECT_THROW(cl.GetChild(1), std::out_of_range);
    EXPECT_THROW(cl.SetChild(1, nullptr), std::out_of_range);
    EXPECT_THROW(cl.GetChildSlotInfo(1), std::out_of_range);
}

// The `ExpressionSlot` points at the shared `Slots::Expression` kind (already ported by
// `UnaryOperatorExpression`); no new `Slots` constant for `CaseLabel`'s `Expression` slot.
TEST(CSharp_CaseLabel, ExpressionSlotPointsAtSlotsExpression) {
    EXPECT_EQ(CaseLabel::ExpressionSlot.Kind(), &Slots::Expression);
}

// `IsInstanceOfType` (the slot's is-a test) accepts an `Expression` and rejects a `Statement`
// (the `ExpressionSlot` is an `Expression`-typed slot).
TEST(CSharp_CaseLabel, ExpressionSlotIsInstanceOfTypeCrossCheck) {
    NullReferenceExpression expr;
    BreakStatement stmt;
    EXPECT_TRUE(CaseLabel::ExpressionSlot.IsInstanceOfType(&expr));
    EXPECT_FALSE(CaseLabel::ExpressionSlot.IsInstanceOfType(&stmt));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitCaseLabel`; the depth-first walk recurses into the
// `Expression` child. A `case expr:` label records "case" then the expression; a `default:` label
// (null `Expression`) records just "default".
TEST(CSharp_CaseLabel, AcceptVisitorDispatchesAndWalks) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    RecordingVisitor v;
    cl.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"case", "nullref"}));
}

// A `default:` label (null `Expression`) records just "default" (no child to recurse into).
TEST(CSharp_CaseLabel, AcceptVisitorDefaultLabelRecordsNoChildren) {
    CaseLabel cl;
    RecordingVisitor v;
    cl.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"default"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor
// relies on).
TEST(CSharp_CaseLabel, AcceptVisitorIsVirtualThroughAstNode) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    AstNode* asAst = &cl;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"case", "nullref"}));
}

// ---- DoMatch (the generated nullable-recursive match) --------------------

// Two `default:` labels (both null `Expression`) match (the `MatchOptional` both-absent path).
TEST(CSharp_CaseLabel, DoMatchMatchesTwoDefaults) {
    CaseLabel a;
    CaseLabel b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `case expr:` labels with the same expression value match (the `MatchOptional` both-present
// path delegates to the expression's `DoMatch`).
TEST(CSharp_CaseLabel, DoMatchMatchesTwoCasesWithSameExpression) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    CaseLabel a(ea.get());
    CaseLabel b(eb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `case expr:` label vs a `default:` label (one present, one absent) rejects (the
// `MatchOptional` asymmetry path).
TEST(CSharp_CaseLabel, DoMatchRejectsCaseVsDefault) {
    auto ea = std::make_unique<NullReferenceExpression>();
    CaseLabel a(ea.get());  // case expr:
    CaseLabel b;            // default:
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `case expr:` labels with different expression values reject (the recursive `DoMatch` on the
// expressions fails).
TEST(CSharp_CaseLabel, DoMatchRejectsDifferentExpressions) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<ThisReferenceExpression>();
    CaseLabel a(ea.get());
    CaseLabel b(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A non-`CaseLabel` candidate rejects on the type-check gate.
TEST(CSharp_CaseLabel, DoMatchRejectsNonCaseLabelCandidate) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel a(expr.get());
    NullReferenceExpression other;
    EXPECT_FALSE(DoMatchAgainst(&a, &other));
}

// A null candidate rejects.
TEST(CSharp_CaseLabel, DoMatchRejectsNullCandidate) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel a(expr.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-copies the `Expression` (distinct from the source) and re-parents it.
TEST(CSharp_CaseLabel, CloneDeepCopiesExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    std::unique_ptr<CaseLabel> copy(cl.Clone());
    EXPECT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), expr.get());       // distinct (deep-copied)
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(expr->Parent(), &cl);                  // source untouched
}

// `Clone` of a `default:` label (null `Expression`) keeps a null `Expression`.
TEST(CSharp_CaseLabel, CloneDefaultLabelStaysNull) {
    CaseLabel cl;
    std::unique_ptr<CaseLabel> copy(cl.Clone());
    EXPECT_EQ(copy->Expression(), nullptr);
}

// `Clone` is virtual through an `AstNode*` and covariant through `CaseLabel*`.
TEST(CSharp_CaseLabel, CloneIsVirtualAndCovariant) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    AstNode* asAst = &cl;
    std::unique_ptr<AstNode> copy(asAst->Clone());
    EXPECT_NE(dynamic_cast<CaseLabel*>(copy.get()), nullptr);
    CaseLabel* typed = cl.Clone();
    EXPECT_NE(typed, nullptr);
    delete typed;
}

// `Clone` does not detach the source's children.
TEST(CSharp_CaseLabel, CloneDoesNotDetachSource) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    std::unique_ptr<CaseLabel> copy(cl.Clone());
    EXPECT_EQ(cl.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &cl);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A default-constructed (empty) `CaseLabel` passes the inherited `CheckInvariant` (the `Expression`
// slot is nullable, so its absence is invariant-valid -- the `ReturnStatement` D255 nullable-slot
// behavior). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_CaseLabel, CheckInvariantPassesOnEmptyNode) {
    CaseLabel cl;
    cl.CheckInvariant();
}

// A filled `CaseLabel` passes the inherited `CheckInvariant`.
TEST(CSharp_CaseLabel, CheckInvariantPassesOnFilledNode) {
    auto expr = std::make_unique<NullReferenceExpression>();
    CaseLabel cl(expr.get());
    cl.CheckInvariant();
}

// ---- Slot identity -----------------------------------------------------

// The `ExpressionSlot` is distinct from unrelated `Slots` kinds (compared by address through the
// common `CSharpSlotInfo*` base -- the cross-element-type `EXPECT_NE` precedent, since
// `CSharpSlotInfoT<Expression>` and `CSharpSlotInfoT<Statement>` are unrelated pointer types).
TEST(CSharp_CaseLabel, SlotStaticDistinctFromUnrelatedKinds) {
    EXPECT_NE((const CSharpSlotInfo*)&CaseLabel::ExpressionSlot,
              (const CSharpSlotInfo*)&Slots::EmbeddedStatement);
    EXPECT_NE((const CSharpSlotInfo*)&CaseLabel::ExpressionSlot,
              (const CSharpSlotInfo*)&Slots::CaseLabel);
}

// ==========================================================================
// SwitchSection (the two-collection section)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `SwitchSection` derives directly from the `AstNode` root (not `Expression`/`Statement`); it is an
// element of a `SwitchStatement`'s `SwitchSections` collection.
TEST(CSharp_SwitchSection, IsAstNodeNotExpressionNotStatement) {
    SwitchSection ss;
    EXPECT_NE(dynamic_cast<AstNode*>(&ss), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ss), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ss), nullptr);
}

// `SwitchSection` is NOT `final` (the C# is not `sealed` -- the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a `PatternPlaceholder` subclass;
// deferred but the class stays non-`final`).
TEST(CSharp_SwitchSection, IsConcreteAndNotFinal) {
    EXPECT_FALSE(std::is_final_v<SwitchSection>);
    EXPECT_FALSE(std::is_abstract_v<SwitchSection>);
}

// ---- Construction --------------------------------------------------------

// The empty ctor has no case labels and no statements. `GetChildCount` is 0 (both collections
// empty).
TEST(CSharp_SwitchSection, EmptyCtorHasNoCaseLabelsOrStatements) {
    SwitchSection ss;
    EXPECT_EQ(ss.CaseLabels().Count(), 0);
    EXPECT_EQ(ss.Statements().Count(), 0);
    EXPECT_EQ(ss.GetChildCount(), 0);
    EXPECT_EQ(ss.StartLocation(), TextLocation::Empty);
}

// ---- The `CaseLabels` collection slot ------------------------------------

// `Add` appends a case label, re-parents it, and invalidates the parent's indices (the collection
// is non-incremental -- two collections). The reindex is triggered by `Slot()` (which calls
// `EnsureChildIndices` on the parent).
TEST(CSharp_SwitchSection, CaseLabelsAddAppendsParentsAndInvalidates) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();  // default: label
    ss.CaseLabels().Add(def.get());
    EXPECT_EQ(ss.CaseLabels().Count(), 1);
    EXPECT_EQ(def->Parent(), &ss);
    EXPECT_EQ(ss.GetChildCount(), 1);
    ASSERT_FALSE(ss.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)def->Slot();
    ASSERT_TRUE(ss.ChildIndicesValid());
    EXPECT_EQ(def->ChildIndex, 0);
}

// ---- The `Statements` collection slot ------------------------------------

// `Add` appends a statement, re-parents it, and invalidates the parent's indices. The reindex is
// triggered by `Slot()`.
TEST(CSharp_SwitchSection, StatementsAddAppendsParentsAndInvalidates) {
    SwitchSection ss;
    auto brk = std::make_unique<BreakStatement>();
    ss.Statements().Add(brk.get());
    EXPECT_EQ(ss.Statements().Count(), 1);
    EXPECT_EQ(brk->Parent(), &ss);
    EXPECT_EQ(ss.GetChildCount(), 1);
    ASSERT_FALSE(ss.ChildIndicesValid());
    (void)brk->Slot();
    ASSERT_TRUE(ss.ChildIndicesValid());
    EXPECT_EQ(brk->ChildIndex, 0);  // CaseLabels (slot 0) is empty, so Statements[0] is at 0
}

// ---- GetCollectionByKind -------------------------------------------------

// `GetCollectionByKind` returns the `CaseLabels` collection for the `CaseLabel` kind.
TEST(CSharp_SwitchSection, GetCollectionByKindReturnsCaseLabelsForCaseLabelKind) {
    SwitchSection ss;
    EXPECT_EQ(ss.GetCollectionByKind(&Slots::CaseLabel), &ss.CaseLabels());
    EXPECT_EQ(ss.GetCollectionByKind(&Slots::EmbeddedStatement), &ss.Statements());  // the other collection
    EXPECT_EQ(ss.GetCollectionByKind(&Slots::Expression), nullptr);  // an unrelated single-slot kind
    EXPECT_EQ(ss.GetCollectionByKind(nullptr), nullptr);
}

// `GetCollectionByKind` returns the `Statements` collection for the `EmbeddedStatement` kind (the
// `Statements` collection reuses the `WhileStatement` D258 single-`Statement` kind as a collection
// -- the kind-collapsing-by-name design).
TEST(CSharp_SwitchSection, StatementsSlotReusesSlotsEmbeddedStatementKind) {
    SwitchSection ss;
    EXPECT_EQ(SwitchSection::StatementsSlot.Kind(), &Slots::EmbeddedStatement);
    EXPECT_EQ(ss.GetCollectionByKind(&Slots::EmbeddedStatement), &ss.Statements());
}

// ---- Slot storage (collection -> collection) ------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the two collections in declaration order. A
// section with one case label (default:) and two statements (break, continue) reports
// `GetChildCount` 3, with `GetChildSlotInfo` routing each child to its slot.
TEST(CSharp_SwitchSection, CollectionAwareSlotStorage) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();
    auto brk = std::make_unique<BreakStatement>();
    auto cont = std::make_unique<ContinueStatement>();
    ss.CaseLabels().Add(def.get());
    ss.Statements().Add(brk.get());
    ss.Statements().Add(cont.get());
    EXPECT_EQ(ss.GetChildCount(), 3);
    EXPECT_EQ(ss.GetChild(0), def.get());
    EXPECT_EQ(ss.GetChild(1), brk.get());
    EXPECT_EQ(ss.GetChild(2), cont.get());
    EXPECT_EQ(ss.GetChildSlotInfo(0), &SwitchSection::CaseLabelsSlot);
    EXPECT_EQ(ss.GetChildSlotInfo(1), &SwitchSection::StatementsSlot);
    EXPECT_EQ(ss.GetChildSlotInfo(2), &SwitchSection::StatementsSlot);
    EXPECT_THROW(ss.GetChild(3), std::out_of_range);
    EXPECT_THROW(ss.GetChildSlotInfo(3), std::out_of_range);
}

// `SetChild` replaces a collection element in place (the element must already exist at the
// flattened index).
TEST(CSharp_SwitchSection, SetChildReplacesCollectionElement) {
    SwitchSection ss;
    auto brk = std::make_unique<BreakStatement>();
    auto cont = std::make_unique<ContinueStatement>();
    ss.Statements().Add(brk.get());
    ss.SetChild(0, cont.get());
    EXPECT_EQ(ss.GetChild(0), cont.get());
    EXPECT_EQ(cont->Parent(), &ss);
    EXPECT_EQ(brk->Parent(), nullptr);  // detached by SetAt
}

// The dynamic flattened-index layout reindex: `CaseLabels [0, caseCount)` then `Statements
// [caseCount, caseCount + stmtCount)`. With both collections non-incremental, `Add` leaves the
// indices invalid until a reindex is triggered (here by `Slot()`), after which each child's
// `ChildIndex` is its correct flattened index.
TEST(CSharp_SwitchSection, DynamicFlattenedIndexReindex) {
    SwitchSection ss;
    auto def0 = std::make_unique<CaseLabel>();
    auto def1 = std::make_unique<CaseLabel>();
    auto brk = std::make_unique<BreakStatement>();
    auto cont = std::make_unique<ContinueStatement>();
    ss.CaseLabels().Add(def0.get());
    ss.CaseLabels().Add(def1.get());
    ss.Statements().Add(brk.get());
    ss.Statements().Add(cont.get());
    EXPECT_EQ(ss.GetChildCount(), 4);
    ASSERT_FALSE(ss.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)def0->Slot();
    ASSERT_TRUE(ss.ChildIndicesValid());
    EXPECT_EQ(def0->ChildIndex, 0);  // CaseLabels[0]
    EXPECT_EQ(def1->ChildIndex, 1);  // CaseLabels[1]
    EXPECT_EQ(brk->ChildIndex, 2);   // Statements[0] at caseCount (2)
    EXPECT_EQ(cont->ChildIndex, 3);  // Statements[1] at caseCount + 1 (3)
}

// The `CaseLabelsSlot` points at the NEW cycle-broken `Slots::CaseLabel` kind.
TEST(CSharp_SwitchSection, CaseLabelsSlotPointsAtSlotsCaseLabel) {
    EXPECT_EQ(SwitchSection::CaseLabelsSlot.Kind(), &Slots::CaseLabel);
}

// `IsInstanceOfType` (the slot's is-a test) accepts the right element type and rejects the wrong
// one for each collection.
TEST(CSharp_SwitchSection, SlotIsInstanceOfTypeCrossCheck) {
    CaseLabel cl;
    BreakStatement stmt;
    EXPECT_TRUE(SwitchSection::CaseLabelsSlot.IsInstanceOfType(&cl));
    EXPECT_FALSE(SwitchSection::CaseLabelsSlot.IsInstanceOfType(&stmt));
    EXPECT_TRUE(SwitchSection::StatementsSlot.IsInstanceOfType(&stmt));
    EXPECT_FALSE(SwitchSection::StatementsSlot.IsInstanceOfType(&cl));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitSwitchSection`; the depth-first walk visits the `CaseLabels`
// collection (each `CaseLabel` recursing into its `Expression`) then the `Statements` collection.
TEST(CSharp_SwitchSection, AcceptVisitorDispatchesAndWalksTwoCollections) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();  // default:
    auto exprCase = std::make_unique<CaseLabel>();
    auto caseExpr = std::make_unique<NullReferenceExpression>();
    exprCase->Expression(caseExpr.get());  // case null:
    auto brk = std::make_unique<BreakStatement>();
    ss.CaseLabels().Add(def.get());
    ss.CaseLabels().Add(exprCase.get());
    ss.Statements().Add(brk.get());
    RecordingVisitor v;
    ss.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "section",
        "default",                 // the first CaseLabels element (default:, no children)
        "case", "nullref",         // the second CaseLabels element (case null:, recurses into expr)
        "break"}));                 // the Statements collection
}

// `AcceptVisitor` is virtual through an `AstNode*`.
TEST(CSharp_SwitchSection, AcceptVisitorIsVirtualThroughAstNode) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();
    ss.CaseLabels().Add(def.get());
    AstNode* asAst = &ss;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"section", "default"}));
}

// ---- DoMatch (the generated two-collection match) -----------------------

// Two empty `SwitchSection`s match (both collections empty == empty).
TEST(CSharp_SwitchSection, DoMatchMatchesTwoEmpty) {
    SwitchSection a;
    SwitchSection b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `SwitchSection`s with the same `CaseLabels` and `Statements` match.
TEST(CSharp_SwitchSection, DoMatchMatchesSameFilled) {
    SwitchSection a;
    SwitchSection b;
    auto ad = std::make_unique<CaseLabel>();
    auto bd = std::make_unique<CaseLabel>();
    auto ab = std::make_unique<BreakStatement>();
    auto bb = std::make_unique<BreakStatement>();
    a.CaseLabels().Add(ad.get());
    a.Statements().Add(ab.get());
    b.CaseLabels().Add(bd.get());
    b.Statements().Add(bb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Different `CaseLabels` counts reject (the collection match fails when the counts differ).
TEST(CSharp_SwitchSection, DoMatchRejectsDifferentCaseLabelsCount) {
    SwitchSection a;
    SwitchSection b;
    auto ad = std::make_unique<CaseLabel>();
    a.CaseLabels().Add(ad.get());
    // b has no case labels
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `Statements` counts reject.
TEST(CSharp_SwitchSection, DoMatchRejectsDifferentStatementsCount) {
    SwitchSection a;
    SwitchSection b;
    auto ab = std::make_unique<BreakStatement>();
    a.Statements().Add(ab.get());
    // b has no statements
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `CaseLabel` values reject (a `case:` vs a `default:` label).
TEST(CSharp_SwitchSection, DoMatchRejectsDifferentCaseLabelValue) {
    SwitchSection a;
    SwitchSection b;
    auto aexpr = std::make_unique<NullReferenceExpression>();
    auto acase = std::make_unique<CaseLabel>(aexpr.get());  // case null:
    auto bdef = std::make_unique<CaseLabel>();              // default:
    a.CaseLabels().Add(acase.get());
    b.CaseLabels().Add(bdef.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A non-`SwitchSection` candidate rejects on the type-check gate.
TEST(CSharp_SwitchSection, DoMatchRejectsNonSwitchSectionCandidate) {
    SwitchSection a;
    CaseLabel other;  // a different direct-AstNode-derived node
    EXPECT_FALSE(DoMatchAgainst(&a, &other));
}

// A null candidate rejects.
TEST(CSharp_SwitchSection, DoMatchRejectsNullCandidate) {
    SwitchSection a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-copies both collections (distinct from the source) and re-parents every element.
TEST(CSharp_SwitchSection, CloneDeepCopiesBothCollections) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();
    auto brk = std::make_unique<BreakStatement>();
    ss.CaseLabels().Add(def.get());
    ss.Statements().Add(brk.get());
    std::unique_ptr<SwitchSection> copy(ss.Clone());
    EXPECT_EQ(copy->CaseLabels().Count(), 1);
    EXPECT_EQ(copy->Statements().Count(), 1);
    EXPECT_NE(copy->CaseLabels().At(0), def.get());  // distinct (deep-copied)
    EXPECT_NE(copy->Statements().At(0), brk.get());  // distinct (deep-copied)
    EXPECT_EQ(copy->CaseLabels().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->Statements().At(0)->Parent(), copy.get());
}

// `Clone` of an empty section stays empty.
TEST(CSharp_SwitchSection, CloneEmptyStaysEmpty) {
    SwitchSection ss;
    std::unique_ptr<SwitchSection> copy(ss.Clone());
    EXPECT_EQ(copy->CaseLabels().Count(), 0);
    EXPECT_EQ(copy->Statements().Count(), 0);
}

// `Clone` is virtual through an `AstNode*` and covariant through `SwitchSection*`.
TEST(CSharp_SwitchSection, CloneIsVirtualAndCovariant) {
    SwitchSection ss;
    AstNode* asAst = &ss;
    std::unique_ptr<AstNode> copy(asAst->Clone());
    EXPECT_NE(dynamic_cast<SwitchSection*>(copy.get()), nullptr);
    SwitchSection* typed = ss.Clone();
    EXPECT_NE(typed, nullptr);
    delete typed;
}

// `Clone` does not detach the source's children.
TEST(CSharp_SwitchSection, CloneDoesNotDetachSource) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();
    auto brk = std::make_unique<BreakStatement>();
    ss.CaseLabels().Add(def.get());
    ss.Statements().Add(brk.get());
    std::unique_ptr<SwitchSection> copy(ss.Clone());
    EXPECT_EQ(ss.CaseLabels().Count(), 1);
    EXPECT_EQ(ss.Statements().Count(), 1);
    EXPECT_EQ(def->Parent(), &ss);
    EXPECT_EQ(brk->Parent(), &ss);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A default-constructed (empty) `SwitchSection` passes the inherited `CheckInvariant` (there are
// no required single slots -- both slots are collections, and a collection is never a required
// slot). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_SwitchSection, CheckInvariantPassesOnEmptyNode) {
    SwitchSection ss;
    ss.CheckInvariant();
}

// A filled `SwitchSection` passes the inherited `CheckInvariant`.
TEST(CSharp_SwitchSection, CheckInvariantPassesOnFilledNode) {
    SwitchSection ss;
    auto def = std::make_unique<CaseLabel>();
    auto brk = std::make_unique<BreakStatement>();
    ss.CaseLabels().Add(def.get());
    ss.Statements().Add(brk.get());
    ss.CheckInvariant();
}

// ---- Slot identity -----------------------------------------------------

// The `CaseLabelsSlot` and `StatementsSlot` are distinct slot statics (compared by address through
// the common `CSharpSlotInfo*` base -- the cross-element-type `EXPECT_NE` precedent, since
// `CSharpSlotInfoT<CaseLabel>` and `CSharpSlotInfoT<Statement>` are unrelated pointer types).
TEST(CSharp_SwitchSection, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&SwitchSection::CaseLabelsSlot,
              (const CSharpSlotInfo*)&SwitchSection::StatementsSlot);
}

// ==========================================================================
// SwitchStatement (the switch statement)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `SwitchStatement` derives from `Statement`; it is disjoint from `Expression` (the D254
// disjoint-hierarchy discriminator).
TEST(CSharp_SwitchStatement, IsStatementAndAstNodeNotExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    EXPECT_NE(dynamic_cast<Statement*>(&sw), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&sw), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&sw), nullptr);
}

// `SwitchStatement` is `final` (the C# `sealed`).
TEST(CSharp_SwitchStatement, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<SwitchStatement>);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_SwitchStatement, SwitchKeywordConst) {
    EXPECT_STREQ(SwitchStatement::SwitchKeyword, "switch");
}

// The empty ctor has no expression and no switch sections. `GetChildCount` is 1 (the one empty
// single slot `Expression`) + 0 sections.
TEST(CSharp_SwitchStatement, EmptyCtorHasNoExpressionOrSwitchSections) {
    SwitchStatement sw;
    EXPECT_EQ(sw.Expression(), nullptr);
    EXPECT_EQ(sw.SwitchSections().Count(), 0);
    EXPECT_EQ(sw.GetChildCount(), 1);
    EXPECT_EQ(sw.StartLocation(), TextLocation::Empty);
}

// The `(Expression*)` ctor (the generated required-prefix ctor) sets the `Expression` and parents
// it at index 0.
TEST(CSharp_SwitchStatement, CtorSetsExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    EXPECT_EQ(sw.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &sw);
    EXPECT_EQ(expr->ChildIndex, 0);
}

// ---- The `Expression` slot -----------------------------------------------

// The `Expression` setter re-parents the new expression and detaches the old one.
TEST(CSharp_SwitchStatement, ExpressionSetterReparentsAndDetaches) {
    SwitchStatement sw;
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    sw.Expression(a.get());
    EXPECT_EQ(a->Parent(), &sw);
    EXPECT_EQ(sw.Expression(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    sw.Expression(b.get());
    EXPECT_EQ(b->Parent(), &sw);
    EXPECT_EQ(sw.Expression(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Expression` setter clears and detaches the old expression.
TEST(CSharp_SwitchStatement, ExpressionSetterClearsWithNull) {
    SwitchStatement sw;
    auto a = std::make_unique<NullReferenceExpression>();
    sw.Expression(a.get());
    sw.Expression(nullptr);
    EXPECT_EQ(sw.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
}

// ---- The `SwitchSections` collection slot --------------------------------

// `Add` appends a switch section, re-parents it, and maintains its flattened `ChildIndex`
// incrementally (the collection is the node's only collection and last slot, so
// `supportsIncremental` is true -- an element's `ChildIndex` is `1 + its local position`).
TEST(CSharp_SwitchStatement, SwitchSectionsAddAppendsParentsAndMaintainsChildIndex) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto s0 = std::make_unique<SwitchSection>();
    auto s1 = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(s0.get());
    sw.SwitchSections().Add(s1.get());
    EXPECT_EQ(sw.SwitchSections().Count(), 2);
    EXPECT_EQ(s0->Parent(), &sw);
    EXPECT_EQ(s1->Parent(), &sw);
    EXPECT_EQ(s0->ChildIndex, 1);  // incremental: 1 + 0
    EXPECT_EQ(s1->ChildIndex, 2);  // incremental: 1 + 1
    EXPECT_EQ(sw.GetChildCount(), 3);  // 1 Expression + 2 sections
}

// `GetCollectionByKind` returns the `SwitchSections` collection for the `SwitchSection` kind.
TEST(CSharp_SwitchStatement, GetCollectionByKindReturnsSwitchSectionsForSwitchSectionKind) {
    SwitchStatement sw;
    EXPECT_EQ(sw.GetCollectionByKind(&Slots::SwitchSection), &sw.SwitchSections());
    EXPECT_EQ(sw.GetCollectionByKind(&Slots::Expression), nullptr);  // a single slot
    EXPECT_EQ(sw.GetCollectionByKind(&Slots::CaseLabel), nullptr);   // an unrelated collection kind
    EXPECT_EQ(sw.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot storage (single -> collection) --------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single slot then the collection. A switch
// with an expression and two sections reports `GetChildCount` 3.
TEST(CSharp_SwitchStatement, CollectionAwareSlotStorage) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto s0 = std::make_unique<SwitchSection>();
    auto s1 = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(s0.get());
    sw.SwitchSections().Add(s1.get());
    EXPECT_EQ(sw.GetChildCount(), 3);
    EXPECT_EQ(sw.GetChild(0), expr.get());
    EXPECT_EQ(sw.GetChild(1), s0.get());
    EXPECT_EQ(sw.GetChild(2), s1.get());
    EXPECT_EQ(sw.GetChildSlotInfo(0), &SwitchStatement::ExpressionSlot);
    EXPECT_EQ(sw.GetChildSlotInfo(1), &SwitchStatement::SwitchSectionsSlot);
    EXPECT_EQ(sw.GetChildSlotInfo(2), &SwitchStatement::SwitchSectionsSlot);
    EXPECT_THROW(sw.GetChild(3), std::out_of_range);
}

// `SetChild` replaces the `Expression` (index 0) or a collection element (index >= 1).
TEST(CSharp_SwitchStatement, SetChildReplacesExpressionAndSection) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto s0 = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(s0.get());
    auto newExpr = std::make_unique<ThisReferenceExpression>();
    sw.SetChild(0, newExpr.get());
    EXPECT_EQ(sw.Expression(), newExpr.get());
    EXPECT_EQ(newExpr->Parent(), &sw);
    EXPECT_EQ(expr->Parent(), nullptr);  // detached by SetChildNode
    auto newSec = std::make_unique<SwitchSection>();
    sw.SetChild(1, newSec.get());
    EXPECT_EQ(sw.SwitchSections().At(0), newSec.get());
    EXPECT_EQ(newSec->Parent(), &sw);
    EXPECT_EQ(s0->Parent(), nullptr);  // detached by SetAt
}

// The `ExpressionSlot` points at the shared `Slots::Expression` kind (already ported by
// `UnaryOperatorExpression`).
TEST(CSharp_SwitchStatement, ExpressionSlotPointsAtSlotsExpression) {
    EXPECT_EQ(SwitchStatement::ExpressionSlot.Kind(), &Slots::Expression);
}

// The `SwitchSectionsSlot` points at the NEW cycle-broken `Slots::SwitchSection` kind.
TEST(CSharp_SwitchStatement, SwitchSectionsSlotPointsAtSlotsSwitchSection) {
    EXPECT_EQ(SwitchStatement::SwitchSectionsSlot.Kind(), &Slots::SwitchSection);
}

// `IsInstanceOfType` (the slot's is-a test) accepts the right element type and rejects the wrong
// one for each slot.
TEST(CSharp_SwitchStatement, SlotIsInstanceOfTypeCrossCheck) {
    NullReferenceExpression expr;
    SwitchSection sec;
    BreakStatement stmt;
    EXPECT_TRUE(SwitchStatement::ExpressionSlot.IsInstanceOfType(&expr));
    EXPECT_FALSE(SwitchStatement::ExpressionSlot.IsInstanceOfType(&sec));
    EXPECT_TRUE(SwitchStatement::SwitchSectionsSlot.IsInstanceOfType(&sec));
    EXPECT_FALSE(SwitchStatement::SwitchSectionsSlot.IsInstanceOfType(&stmt));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitSwitchStatement`; the depth-first walk visits the
// `Expression` (single slot) then the `SwitchSections` (each `SwitchSection` recursing into its
// `CaseLabels` and `Statements`).
TEST(CSharp_SwitchStatement, AcceptVisitorDispatchesAndWalks) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto sec = std::make_unique<SwitchSection>();
    auto def = std::make_unique<CaseLabel>();  // default:
    auto brk = std::make_unique<BreakStatement>();
    sec->CaseLabels().Add(def.get());
    sec->Statements().Add(brk.get());
    sw.SwitchSections().Add(sec.get());
    RecordingVisitor v;
    sw.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "switch",
        "nullref",                 // the Expression single slot
        "section", "default", "break"}));  // the SwitchSections collection (section -> labels -> statements)
}

// `AcceptVisitor` is virtual through an `AstNode*`.
TEST(CSharp_SwitchStatement, AcceptVisitorIsVirtualThroughAstNode) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    AstNode* asAst = &sw;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"switch", "nullref"}));
}

// `AcceptVisitor` is virtual through a `Statement*` (the covariant base).
TEST(CSharp_SwitchStatement, AcceptVisitorIsVirtualThroughStatement) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    Statement* asStmt = &sw;
    RecordingVisitor v;
    asStmt->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"switch", "nullref"}));
}

// ---- DoMatch (the generated required-recursive + collection match) -------

// Two `SwitchStatement`s with the same `Expression` and empty `SwitchSections` match.
TEST(CSharp_SwitchStatement, DoMatchMatchesSame) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    SwitchStatement a(ea.get());
    SwitchStatement b(eb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Expression` (the non-nullable recursive `MatchRequired` term) rejects.
TEST(CSharp_SwitchStatement, DoMatchRejectsDifferentExpression) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<ThisReferenceExpression>();
    SwitchStatement a(ea.get());
    SwitchStatement b(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `SwitchSections` counts reject (the collection match fails when the counts differ).
TEST(CSharp_SwitchStatement, DoMatchRejectsDifferentSwitchSectionsCount) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    SwitchStatement a(ea.get());
    SwitchStatement b(eb.get());
    auto sa = std::make_unique<SwitchSection>();
    a.SwitchSections().Add(sa.get());
    // b has no sections
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null-pattern `Expression` (a half-constructed pattern) rejects without crashing (the
// `MatchRequired` null-pattern guard).
TEST(CSharp_SwitchStatement, DoMatchRejectsNullPatternExpressionWithoutCrash) {
    SwitchStatement a;  // no expression
    auto eb = std::make_unique<NullReferenceExpression>();
    SwitchStatement b(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A non-`SwitchStatement` candidate rejects on the type-check gate.
TEST(CSharp_SwitchStatement, DoMatchRejectsNonSwitchStatementCandidate) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement a(expr.get());
    NullReferenceExpression other;  // a different node
    EXPECT_FALSE(DoMatchAgainst(&a, &other));
}

// A null candidate rejects.
TEST(CSharp_SwitchStatement, DoMatchRejectsNullCandidate) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement a(expr.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-copies the `Expression` and the `SwitchSections` (distinct from the source) and
// re-parents them.
TEST(CSharp_SwitchStatement, CloneDeepCopiesExpressionAndSwitchSections) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto sec = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(sec.get());
    std::unique_ptr<SwitchStatement> copy(sw.Clone());
    EXPECT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), expr.get());  // distinct (deep-copied)
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->SwitchSections().Count(), 1);
    EXPECT_NE(copy->SwitchSections().At(0), sec.get());  // distinct (deep-copied)
    EXPECT_EQ(copy->SwitchSections().At(0)->Parent(), copy.get());
}

// `Clone` of a switch with a null `Expression` keeps a null `Expression` (and copies the sections).
TEST(CSharp_SwitchStatement, CloneNullExpressionStaysNull) {
    SwitchStatement sw;  // no expression
    auto sec = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(sec.get());
    std::unique_ptr<SwitchStatement> copy(sw.Clone());
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->SwitchSections().Count(), 1);
}

// `Clone` is virtual through an `AstNode*` and covariant through `Statement*`/`SwitchStatement*`.
TEST(CSharp_SwitchStatement, CloneIsVirtualAndCovariant) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    AstNode* asAst = &sw;
    std::unique_ptr<AstNode> copy(asAst->Clone());
    EXPECT_NE(dynamic_cast<SwitchStatement*>(copy.get()), nullptr);
    Statement* asStmt = &sw;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<SwitchStatement*>(stmtCopy.get()), nullptr);
    SwitchStatement* typed = sw.Clone();
    EXPECT_NE(typed, nullptr);
    delete typed;
}

// `Clone` does not detach the source's children.
TEST(CSharp_SwitchStatement, CloneDoesNotDetachSource) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    auto sec = std::make_unique<SwitchSection>();
    sw.SwitchSections().Add(sec.get());
    std::unique_ptr<SwitchStatement> copy(sw.Clone());
    EXPECT_EQ(sw.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &sw);
    EXPECT_EQ(sw.SwitchSections().Count(), 1);
    EXPECT_EQ(sec->Parent(), &sw);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A default-constructed (empty) `SwitchStatement` violates the required-`Expression` invariant
// (the assert fires in debug -- the `LabelStatement` D259 / `ForeachStatement` D265 precedent).
TEST(CSharp_SwitchStatement, CheckInvariantRejectsEmptyNode) {
    SwitchStatement sw;
    EXPECT_DEBUG_DEATH(sw.CheckInvariant(), "");
}

// A `SwitchStatement` with the expression set (the sections may be empty -- a collection is never
// a required slot) passes the inherited `CheckInvariant`. Runs in debug builds (a no-op in
// NDEBUG)
TEST(CSharp_SwitchStatement, CheckInvariantPassesOnFilledNode) {
    auto expr = std::make_unique<NullReferenceExpression>();
    SwitchStatement sw(expr.get());
    sw.CheckInvariant();  // no sections -- still valid (a collection is never required)
}

// ---- Slot identity -----------------------------------------------------

// The `ExpressionSlot` and `SwitchSectionsSlot` are distinct slot statics (compared by address
// through the common `CSharpSlotInfo*` base -- the cross-element-type `EXPECT_NE` precedent, since
// `CSharpSlotInfoT<Expression>` and `CSharpSlotInfoT<SwitchSection>` are unrelated pointer types).
TEST(CSharp_SwitchStatement, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&SwitchStatement::ExpressionSlot,
              (const CSharpSlotInfo*)&SwitchStatement::SwitchSectionsSlot);
    EXPECT_NE((const CSharpSlotInfo*)&SwitchStatement::ExpressionSlot,
              (const CSharpSlotInfo*)&Slots::SwitchSection);
    EXPECT_NE((const CSharpSlotInfo*)&SwitchStatement::SwitchSectionsSlot,
              (const CSharpSlotInfo*)&Slots::Expression);
}
