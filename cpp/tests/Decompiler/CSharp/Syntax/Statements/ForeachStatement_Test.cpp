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

// Tests for the `ForeachStatement` concrete node -- the next in-order Phase-5 piece per the D263
// plan (now unblocked by the `VariableDesignation` hierarchy). `ForeachStatement` is the
// `foreach_statement ::= 'await'? 'foreach' '(' type variable_designation 'in' expression ')'
// statement` node (C# grammar 13.9.5.1): a sealed `Statement` with FOUR single, REQUIRED
// (non-nullable) `[Slot]` children -- a `VariableType` `AstType`, a `VariableDesignation`
// `VariableDesignation`, an `InExpression` `Expression`, and an `EmbeddedStatement` `Statement`
// -- plus an `IsAsync` bool scalar (the leading `await`). It reuses the already-ported
// `Slots::Type`/`Slots::VariableDesignation`/`Slots::Expression`/`Slots::EmbeddedStatement` kinds
// with no new `Slots` constant; the `AwaitKeyword` const aliases
// `UnaryOperatorExpression::AwaitKeyword`. The `VariableDesignation()` slot accessor shadows the
// `VariableDesignation` class (the `Expression()`-of-type-`Expression` D231 crux applied to a
// `VariableDesignation`-typed slot accessor); the other three slot accessors do not shadow their
// element types. The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `VisitForeachStatement` under test (plus the
// child node types it holds and the structural-twin loop statements `WhileStatement`/
// `ForStatement`), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitForeachStatement(ForeachStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("foreach");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
    void VisitForStatement(ForStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("for");
        VisitChildren(node);
    }
    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr) return;
        trace.push_back("continue");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) return;
        trace.push_back("break");
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) return;
        trace.push_back("primitive");
        VisitChildren(node);
    }
    void VisitSingleVariableDesignation(SingleVariableDesignation* node) override {
        if (node == nullptr) return;
        trace.push_back("single");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) return;
        trace.push_back("id");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("null");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("this");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Builds a fully-formed `ForeachStatement` (all four required slots filled) keeping every node
// alive in the holder's `unique_ptr`s (the port's child slots are non-owning raw pointers -- the
// test must keep the children alive in the test scope, the AttributeSection D241 use-after-free
// precedent). `get()` exposes the `ForeachStatement*`.
struct ForeachHolder {
    std::unique_ptr<PrimitiveType> variableType;
    std::unique_ptr<SingleVariableDesignation> designation;
    std::unique_ptr<NullReferenceExpression> inExpression;
    std::unique_ptr<ContinueStatement> embeddedStatement;
    std::unique_ptr<ForeachStatement> node;

    ForeachStatement* get() const { return node.get(); }
};

static ForeachHolder makeForeach(std::string designationName = "x") {
    ForeachHolder h;
    h.variableType = std::make_unique<PrimitiveType>("int");
    h.designation = std::make_unique<SingleVariableDesignation>(std::move(designationName));
    h.inExpression = std::make_unique<NullReferenceExpression>();
    h.embeddedStatement = std::make_unique<ContinueStatement>();
    h.node = std::make_unique<ForeachStatement>(
        h.variableType.get(), h.designation.get(),
        h.inExpression.get(), h.embeddedStatement.get());
    return h;
}

} // namespace

// ==========================================================================
// ForeachStatement (VariableType AstType + VariableDesignation + InExpression
//                   Expression + EmbeddedStatement Statement + IsAsync bool)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ForeachStatement, IsStatementAndAstNodeNotExpression) {
    ForeachStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_ForeachStatement, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<ForeachStatement>);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_ForeachStatement, ForeachKeywordConst) {
    EXPECT_STREQ(ForeachStatement::ForeachKeyword, "foreach");
}

TEST(CSharp_ForeachStatement, InKeywordConst) {
    EXPECT_STREQ(ForeachStatement::InKeyword, "in");
}

TEST(CSharp_ForeachStatement, AwaitKeywordAliasesUnaryOperatorExpression) {
    // The `AwaitKeyword` const aliases `UnaryOperatorExpression::AwaitKeyword` (the canonical
    // `await` literal), faithful to the C# `public const string AwaitKeyword =
    // UnaryOperatorExpression.AwaitKeyword`.
    EXPECT_STREQ(ForeachStatement::AwaitKeyword, "await");
    EXPECT_EQ(ForeachStatement::AwaitKeyword, UnaryOperatorExpression::AwaitKeyword);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_ForeachStatement, DefaultCtor) {
    ForeachStatement s;
    EXPECT_EQ(s.VariableType(), nullptr);
    EXPECT_EQ(s.VariableDesignation(), nullptr);
    EXPECT_EQ(s.InExpression(), nullptr);
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_FALSE(s.IsAsync());
    // Four single slots (no collection), so GetChildCount is the constant 4.
    EXPECT_EQ(s.GetChildCount(), 4);
}

TEST(CSharp_ForeachStatement, AllParamsCtorFillsSlots) {
    auto vt = std::make_unique<PrimitiveType>("int");
    auto desig = std::make_unique<SingleVariableDesignation>("x");
    auto inExpr = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    ForeachStatement s(vt.get(), desig.get(), inExpr.get(), body.get());
    EXPECT_EQ(s.VariableType(), vt.get());
    EXPECT_EQ(s.VariableDesignation(), desig.get());
    EXPECT_EQ(s.InExpression(), inExpr.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(vt->Parent(), &s);
    EXPECT_EQ(desig->Parent(), &s);
    EXPECT_EQ(inExpr->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
}

// ---- IsAsync scalar -----------------------------------------------------

TEST(CSharp_ForeachStatement, IsAsyncDefaultsFalse) {
    ForeachStatement s;
    EXPECT_FALSE(s.IsAsync());
}

TEST(CSharp_ForeachStatement, IsAsyncSetter) {
    ForeachStatement s;
    s.IsAsync(true);
    EXPECT_TRUE(s.IsAsync());
    s.IsAsync(false);
    EXPECT_FALSE(s.IsAsync());
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_ForeachStatement, VariableTypeSetterFillsAndParents) {
    ForeachStatement s;
    auto vt = std::make_unique<PrimitiveType>("int");
    s.VariableType(vt.get());
    EXPECT_EQ(s.VariableType(), vt.get());
    EXPECT_EQ(vt->Parent(), &s);
}

TEST(CSharp_ForeachStatement, VariableDesignationSetterFillsAndParents) {
    ForeachStatement s;
    auto desig = std::make_unique<SingleVariableDesignation>("x");
    s.VariableDesignation(desig.get());
    EXPECT_EQ(s.VariableDesignation(), desig.get());
    EXPECT_EQ(desig->Parent(), &s);
}

TEST(CSharp_ForeachStatement, InExpressionSetterFillsAndParents) {
    ForeachStatement s;
    auto inExpr = std::make_unique<NullReferenceExpression>();
    s.InExpression(inExpr.get());
    EXPECT_EQ(s.InExpression(), inExpr.get());
    EXPECT_EQ(inExpr->Parent(), &s);
}

TEST(CSharp_ForeachStatement, EmbeddedStatementSetterFillsAndParents) {
    ForeachStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
}

TEST(CSharp_ForeachStatement, NullVariableTypeSetterClearsSlot) {
    ForeachStatement s;
    auto vt = std::make_unique<PrimitiveType>("int");
    s.VariableType(vt.get());
    s.VariableType(nullptr);
    EXPECT_EQ(s.VariableType(), nullptr);
    EXPECT_EQ(vt->Parent(), nullptr);
    EXPECT_EQ(vt->ChildIndex, -1);
}

TEST(CSharp_ForeachStatement, RejectsAlreadyParentedChild) {
    ForeachStatement a;
    ForeachStatement b;
    auto body = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(body.get());
    EXPECT_THROW(b.EmbeddedStatement(body.get()), std::invalid_argument);
}

TEST(CSharp_ForeachStatement, GetChildReturnsSlots) {
    auto h = makeForeach();
    ForeachStatement* s = h.get();
    EXPECT_EQ(s->GetChild(0), h.variableType.get());
    EXPECT_EQ(s->GetChild(1), h.designation.get());
    EXPECT_EQ(s->GetChild(2), h.inExpression.get());
    EXPECT_EQ(s->GetChild(3), h.embeddedStatement.get());
    EXPECT_EQ(s->GetChildCount(), 4);
    EXPECT_THROW(s->GetChild(4), std::out_of_range);
}

TEST(CSharp_ForeachStatement, GetChildOnEmptyNodeReturnsNullSingles) {
    ForeachStatement s;
    EXPECT_EQ(s.GetChild(0), nullptr);
    EXPECT_EQ(s.GetChild(1), nullptr);
    EXPECT_EQ(s.GetChild(2), nullptr);
    EXPECT_EQ(s.GetChild(3), nullptr);
    EXPECT_THROW(s.GetChild(4), std::out_of_range);
}

TEST(CSharp_ForeachStatement, SetChildRoutesToSlots) {
    ForeachStatement s;
    auto vt0 = std::make_unique<PrimitiveType>("int");
    auto desig0 = std::make_unique<SingleVariableDesignation>("x");
    auto inExpr0 = std::make_unique<NullReferenceExpression>();
    auto body0 = std::make_unique<ContinueStatement>();
    s.SetChild(0, vt0.get());
    s.SetChild(1, desig0.get());
    s.SetChild(2, inExpr0.get());
    s.SetChild(3, body0.get());
    EXPECT_EQ(s.VariableType(), vt0.get());
    EXPECT_EQ(s.VariableDesignation(), desig0.get());
    EXPECT_EQ(s.InExpression(), inExpr0.get());
    EXPECT_EQ(s.EmbeddedStatement(), body0.get());
    EXPECT_EQ(vt0->Parent(), &s);
    EXPECT_EQ(desig0->Parent(), &s);
    EXPECT_EQ(inExpr0->Parent(), &s);
    EXPECT_EQ(body0->Parent(), &s);
    EXPECT_THROW(s.SetChild(4, nullptr), std::out_of_range);
}

TEST(CSharp_ForeachStatement, GetChildSlotInfoReturnsSlotStatics) {
    auto h = makeForeach();
    ForeachStatement* s = h.get();
    EXPECT_EQ(s->GetChildSlotInfo(0), &ForeachStatement::VariableTypeSlot);
    EXPECT_EQ(s->GetChildSlotInfo(1), &ForeachStatement::VariableDesignationSlot);
    EXPECT_EQ(s->GetChildSlotInfo(2), &ForeachStatement::InExpressionSlot);
    EXPECT_EQ(s->GetChildSlotInfo(3), &ForeachStatement::EmbeddedStatementSlot);
    EXPECT_THROW(s->GetChildSlotInfo(4), std::out_of_range);
}

TEST(CSharp_ForeachStatement, GetCollectionByKindReturnsNullForAllSlots) {
    // All four slots are single slots (no collection), so GetCollectionByKind returns null for
    // every kind (the base fallback). The single-slot kinds themselves return null (they are not
    // collection kinds), and the unrelated collection kinds return null too.
    ForeachStatement s;
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::VariableDesignation), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::EmbeddedStatement), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Statement), nullptr);
    EXPECT_EQ(s.GetCollectionByKind(&Slots::Argument), nullptr);
}

TEST(CSharp_ForeachStatement, SlotKindPointsAtSharedSlotsConstant) {
    auto h = makeForeach();
    ForeachStatement* s = h.get();
    ASSERT_EQ(h.variableType->Slot(), &ForeachStatement::VariableTypeSlot);
    EXPECT_EQ(h.variableType->Slot()->Kind(), &Slots::Type);
    ASSERT_EQ(h.designation->Slot(), &ForeachStatement::VariableDesignationSlot);
    EXPECT_EQ(h.designation->Slot()->Kind(), &Slots::VariableDesignation);
    ASSERT_EQ(h.inExpression->Slot(), &ForeachStatement::InExpressionSlot);
    EXPECT_EQ(h.inExpression->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(h.embeddedStatement->Slot(), &ForeachStatement::EmbeddedStatementSlot);
    EXPECT_EQ(h.embeddedStatement->Slot()->Kind(), &Slots::EmbeddedStatement);
}

TEST(CSharp_ForeachStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(ForeachStatement::VariableTypeSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    PrimitiveType pt("int");
    EXPECT_TRUE(ForeachStatement::VariableTypeSlot.IsInstanceOfType(&pt));
    ContinueStatement cs;
    EXPECT_FALSE(ForeachStatement::VariableTypeSlot.IsInstanceOfType(&cs));
    SingleVariableDesignation svd("x");
    EXPECT_TRUE(ForeachStatement::VariableDesignationSlot.IsInstanceOfType(&svd));
    EXPECT_FALSE(ForeachStatement::VariableDesignationSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(ForeachStatement::VariableDesignationSlot.IsInstanceOfType(&cs));
    NullReferenceExpression nre;
    EXPECT_TRUE(ForeachStatement::InExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(ForeachStatement::InExpressionSlot.IsInstanceOfType(&cs));
    EXPECT_TRUE(ForeachStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(ForeachStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_ForeachStatement, AcceptVisitorDispatchesToVisit) {
    ForeachStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "foreach");
}

TEST(CSharp_ForeachStatement, AcceptVisitorIsVirtual) {
    ForeachStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "foreach");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "foreach");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_ForeachStatement, DepthFirstWalksChildrenInOrder) {
    auto h = makeForeach();
    RecordingVisitor v;
    h.get()->AcceptVisitor(v);
    // Document order: VariableType (PrimitiveType), VariableDesignation (SingleVariableDesignation
    // + its backing IdentifierToken), InExpression (NullReferenceExpression), EmbeddedStatement
    // (ContinueStatement).
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "foreach", "primitive", "single", "id", "null", "continue"}));
}

TEST(CSharp_ForeachStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    ForeachStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "foreach");
}

TEST(CSharp_ForeachStatement, DepthFirstWithoutVariableDesignationSkipsIt) {
    // A half-constructed node (no VariableDesignation): the null single slot is skipped by the
    // walk, which visits only present children via `Children()`.
    ForeachStatement s;
    auto vt = std::make_unique<PrimitiveType>("int");
    auto inExpr = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.VariableType(vt.get());
    s.InExpression(inExpr.get());
    s.EmbeddedStatement(body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"foreach", "primitive", "null", "continue"}));
}

// ---- DoMatch (the generated plain-equality + four MatchRequired match) --

TEST(CSharp_ForeachStatement, DoMatchMatchesSameValues) {
    auto a = makeForeach();
    auto b = makeForeach();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchDifferentIsAsyncRejects) {
    auto a = makeForeach();
    auto b = makeForeach();
    a.get()->IsAsync(true);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchBothIsAsyncTrueMatches) {
    auto a = makeForeach();
    auto b = makeForeach();
    a.get()->IsAsync(true);
    b.get()->IsAsync(true);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchDifferentVariableTypeRejects) {
    auto a = makeForeach();
    auto b = makeForeach();
    b.variableType->Keyword("string");
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchDifferentVariableDesignationRejects) {
    auto a = makeForeach("x");
    auto b = makeForeach("y");
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchDifferentInExpressionRejects) {
    auto a = makeForeach();
    // b has a different InExpression type (ThisReferenceExpression vs NullReferenceExpression);
    // built directly (the holder's typed unique_ptr cannot hold a different concrete type).
    auto bVt = std::make_unique<PrimitiveType>("int");
    auto bDesig = std::make_unique<SingleVariableDesignation>("x");
    auto bInExpr = std::make_unique<ThisReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    ForeachStatement b(bVt.get(), bDesig.get(), bInExpr.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_ForeachStatement, DoMatchDifferentEmbeddedStatementRejects) {
    auto a = makeForeach();
    // b has a different EmbeddedStatement type (BreakStatement vs ContinueStatement); built
    // directly (the holder's typed unique_ptr cannot hold a different concrete type).
    auto bVt = std::make_unique<PrimitiveType>("int");
    auto bDesig = std::make_unique<SingleVariableDesignation>("x");
    auto bInExpr = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<BreakStatement>();
    ForeachStatement b(bVt.get(), bDesig.get(), bInExpr.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_ForeachStatement, DoMatchNullVariableTypePatternRejects) {
    // A half-constructed pattern (no VariableType) rejects without crashing (the MatchRequired
    // null-pattern guard).
    ForeachStatement a;
    auto b = makeForeach();
    EXPECT_FALSE(DoMatchAgainst(&a, b.get()));
}

TEST(CSharp_ForeachStatement, DoMatchNullVariableTypeCandidateRejects) {
    auto a = makeForeach();
    ForeachStatement b;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_ForeachStatement, DoMatchRejectsWhileStatementStructuralTwin) {
    // ForeachStatement and WhileStatement are both sealed Statement nodes but distinct concrete
    // types, so the other-is-ForeachStatement / other-is-WhileStatement type-check gates reject.
    auto a = makeForeach();
    auto wCond = std::make_unique<NullReferenceExpression>();
    auto wBody = std::make_unique<ContinueStatement>();
    WhileStatement b(wCond.get(), wBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_ForeachStatement, DoMatchRejectsForStatementStructuralTwin) {
    auto a = makeForeach();
    ForStatement b;
    auto bBody = std::make_unique<ContinueStatement>();
    b.EmbeddedStatement(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_ForeachStatement, DoMatchRejectsDifferentType) {
    ForeachStatement s;
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&s, &nre));
}

TEST(CSharp_ForeachStatement, DoMatchRejectsNullCandidate) {
    auto a = makeForeach();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_ForeachStatement, CloneDeepCopiesAllSlots) {
    auto h = makeForeach();
    h.get()->IsAsync(true);
    std::unique_ptr<ForeachStatement> copy(h.get()->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), h.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_TRUE(copy->IsAsync());
    // VariableType deep-copied and re-parented.
    ASSERT_NE(copy->VariableType(), nullptr);
    EXPECT_NE(copy->VariableType(), h.variableType.get());
    EXPECT_EQ(copy->VariableType()->Parent(), copy.get());
    // VariableDesignation deep-copied and re-parented.
    ASSERT_NE(copy->VariableDesignation(), nullptr);
    EXPECT_NE(copy->VariableDesignation(), h.designation.get());
    EXPECT_EQ(copy->VariableDesignation()->Parent(), copy.get());
    // InExpression deep-copied and re-parented.
    ASSERT_NE(copy->InExpression(), nullptr);
    EXPECT_NE(copy->InExpression(), h.inExpression.get());
    EXPECT_EQ(copy->InExpression()->Parent(), copy.get());
    // EmbeddedStatement deep-copied and re-parented.
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), h.embeddedStatement.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
}

TEST(CSharp_ForeachStatement, CloneSkipsAbsentChildren) {
    // A clone of a node with only EmbeddedStatement set: the absent slots are skipped (no
    // deep-copy of a null slot).
    ForeachStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    std::unique_ptr<ForeachStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->VariableType(), nullptr);
    EXPECT_EQ(copy->VariableDesignation(), nullptr);
    EXPECT_EQ(copy->InExpression(), nullptr);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
}

TEST(CSharp_ForeachStatement, CloneEmptyNodeHasNoChildren) {
    ForeachStatement s;
    std::unique_ptr<ForeachStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->VariableType(), nullptr);
    EXPECT_EQ(copy->VariableDesignation(), nullptr);
    EXPECT_EQ(copy->InExpression(), nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
    EXPECT_FALSE(copy->IsAsync());
}

TEST(CSharp_ForeachStatement, CloneIsVirtualAndCovariant) {
    ForeachStatement s;
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ForeachStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<ForeachStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_ForeachStatement, CloneDoesNotDetachSource) {
    auto h = makeForeach();
    std::unique_ptr<ForeachStatement> copy(h.get()->Clone());
    EXPECT_EQ(h.variableType->Parent(), h.get());
    EXPECT_EQ(h.designation->Parent(), h.get());
    EXPECT_EQ(h.inExpression->Parent(), h.get());
    EXPECT_EQ(h.embeddedStatement->Parent(), h.get());
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_ForeachStatement, CheckInvariantPassesOnFilledNode) {
    auto h = makeForeach();
    h.get()->CheckInvariant();
    SUCCEED();
}

TEST(CSharp_ForeachStatement, CheckInvariantRejectsEmptyNode) {
    // All four slots are required, so a default-constructed (empty) node violates the
    // required-slot invariant (the assert fires in debug).
    ForeachStatement s;
    EXPECT_DEBUG_DEATH(s.CheckInvariant(), "");
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_ForeachStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* vt = &ForeachStatement::VariableTypeSlot;
    const CSharpSlotInfo* vd = &ForeachStatement::VariableDesignationSlot;
    const CSharpSlotInfo* ie = &ForeachStatement::InExpressionSlot;
    const CSharpSlotInfo* es = &ForeachStatement::EmbeddedStatementSlot;
    EXPECT_NE(vt, vd);
    EXPECT_NE(vt, ie);
    EXPECT_NE(vt, es);
    EXPECT_NE(vd, ie);
    EXPECT_NE(vd, es);
    EXPECT_NE(ie, es);
    EXPECT_EQ(vt->Kind(), &Slots::Type);
    EXPECT_EQ(vd->Kind(), &Slots::VariableDesignation);
    EXPECT_EQ(ie->Kind(), &Slots::Expression);
    EXPECT_EQ(es->Kind(), &Slots::EmbeddedStatement);
    // All four are single (non-collection) required (non-optional) slots.
    EXPECT_FALSE(vt->IsCollection());
    EXPECT_FALSE(vd->IsCollection());
    EXPECT_FALSE(ie->IsCollection());
    EXPECT_FALSE(es->IsCollection());
    EXPECT_FALSE(vt->IsOptional());
    EXPECT_FALSE(vd->IsOptional());
    EXPECT_FALSE(ie->IsOptional());
    EXPECT_FALSE(es->IsOptional());
}

TEST(CSharp_ForeachStatement, VariableTypeKindIsSharedWithAttribute) {
    // ForeachStatement.VariableTypeSlot and Attribute.TypeSlot both point at the shared
    // Slots::Type kind.
    EXPECT_EQ(ForeachStatement::VariableTypeSlot.Kind(), &Slots::Type);
}

TEST(CSharp_ForeachStatement, VariableDesignationKindIsSharedWithParenthesized) {
    // ForeachStatement.VariableDesignationSlot (a single slot) and
    // ParenthesizedVariableDesignation's VariableDesignations collection share the
    // Slots::VariableDesignation kind (the kind-collapsing by [Slot] name design -- one kind per
    // [Slot] name regardless of single-vs-collection).
    EXPECT_EQ(ForeachStatement::VariableDesignationSlot.Kind(), &Slots::VariableDesignation);
}

TEST(CSharp_ForeachStatement, InExpressionKindIsSharedWithUnaryOperator) {
    // ForeachStatement.InExpressionSlot points at Slots::Expression (shared by the
    // unary/assignment expressions' operand positions).
    EXPECT_EQ(ForeachStatement::InExpressionSlot.Kind(), &Slots::Expression);
}

TEST(CSharp_ForeachStatement, EmbeddedStatementKindIsSharedWithWhile) {
    // ForeachStatement and WhileStatement share the Slots::EmbeddedStatement kind.
    EXPECT_EQ(ForeachStatement::EmbeddedStatementSlot.Kind(),
              WhileStatement::EmbeddedStatementSlot.Kind());
}

TEST(CSharp_ForeachStatement, SlotKindsAreDistinctFromUnrelatedKinds) {
    // The four slot kinds are distinct from one another and from the unrelated Slots::Statement
    // and Slots::Argument kinds. Slots::Type/VariableDesignation/Expression/EmbeddedStatement/
    // Statement span four distinct element types (AstType/VariableDesignation/Expression/
    // Statement/Statement), so the distinct-element-type CSharpSlotInfoT<T> instantiations are
    // unrelated pointer types; compare through the common CSharpSlotInfo* base (the D251/D252
    // EXPECT_NE cross-element-type precedent).
    const CSharpSlotInfo* vt = &Slots::Type;
    const CSharpSlotInfo* vd = &Slots::VariableDesignation;
    const CSharpSlotInfo* ie = &Slots::Expression;
    const CSharpSlotInfo* es = &Slots::EmbeddedStatement;
    const CSharpSlotInfo* st = &Slots::Statement;
    const CSharpSlotInfo* ag = &Slots::Argument;
    EXPECT_NE(vt, vd);
    EXPECT_NE(vt, ie);
    EXPECT_NE(vt, es);
    EXPECT_NE(vd, ie);
    EXPECT_NE(vd, es);
    EXPECT_NE(ie, es);
    EXPECT_NE(vt, st);
    EXPECT_NE(vt, ag);
    EXPECT_NE(es, st);
}
