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

// Tests for the query-expression family (the first slice of the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs): the `QueryClause`
// abstract base, the `QueryOrdering` node + the `QueryOrderingDirection` enum, the
// `QueryExpression` collection-only container, and the first three concrete clauses
// (`QueryWhereClause`/`QuerySelectClause`/`QueryOrderClause`). `query_expression ::=
// query_clause+` (C# grammar 12.23.1).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrdering.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrderClause.hpp"
#include "Decompiler/CSharp/Syntax/QuerySelectClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the concrete nodes under test (plus the leaf
// `Expression`s used as operands and the `WhileStatement` used for the cross-type DoMatch
// rejection), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitQueryOrdering(QueryOrdering* node) override {
        if (node == nullptr) { trace.push_back("<null-qord>"); return; }
        trace.push_back("qord");
        VisitChildren(node);
    }
    void VisitQueryExpression(QueryExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-qexpr>"); return; }
        trace.push_back("qexpr");
        VisitChildren(node);
    }
    void VisitQueryWhereClause(QueryWhereClause* node) override {
        if (node == nullptr) { trace.push_back("<null-where>"); return; }
        trace.push_back("where");
        VisitChildren(node);
    }
    void VisitQuerySelectClause(QuerySelectClause* node) override {
        if (node == nullptr) { trace.push_back("<null-sel>"); return; }
        trace.push_back("select");
        VisitChildren(node);
    }
    void VisitQueryOrderClause(QueryOrderClause* node) override {
        if (node == nullptr) { trace.push_back("<null-orderby>"); return; }
        trace.push_back("orderby");
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
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
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

} // namespace

// ---------------------------------------------------------------------------
// CSharp_QueryClause -- the abstract base
// ---------------------------------------------------------------------------
TEST(CSharp_QueryClause, IsAbstractAndPolymorphic) {
    EXPECT_TRUE(std::is_abstract_v<QueryClause>);
    EXPECT_TRUE(std::is_polymorphic_v<QueryClause>);
}

TEST(CSharp_QueryClause, QueryClauseIsAstNodeButNotExpressionStatementAstType) {
    QueryWhereClause where;
    EXPECT_NE(dynamic_cast<AstNode*>(&where), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&where), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&where), nullptr);
}

// ---------------------------------------------------------------------------
// CSharp_QueryOrdering -- the ordering node + the QueryOrderingDirection enum
// ---------------------------------------------------------------------------
TEST(CSharp_QueryOrdering, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryOrdering>);
    EXPECT_TRUE(std::is_final_v<QueryOrdering>);
}

TEST(CSharp_QueryOrdering, IsAstNodeButNotQueryClauseExpressionStatementAstType) {
    QueryOrdering ord;
    EXPECT_NE(dynamic_cast<AstNode*>(&ord), nullptr);
    // `QueryOrdering` derives DIRECTLY from `AstNode`, NOT `QueryClause` (it is an element of
    // `QueryOrderClause.Orderings`, not itself a query clause).
    EXPECT_EQ(dynamic_cast<QueryClause*>(&ord), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ord), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ord), nullptr);
}

TEST(CSharp_QueryOrdering, ConstKeywords) {
    EXPECT_STREQ(QueryOrdering::AscendingKeyword, "ascending");
    EXPECT_STREQ(QueryOrdering::DescendingKeyword, "descending");
}

TEST(CSharp_QueryOrdering, EnumValues) {
    EXPECT_EQ(static_cast<int>(QueryOrderingDirection::None), 0);
    EXPECT_EQ(static_cast<int>(QueryOrderingDirection::Ascending), 1);
    EXPECT_EQ(static_cast<int>(QueryOrderingDirection::Descending), 2);
}

TEST(CSharp_QueryOrdering, DefaultCtorDirectionIsNoneAndExpressionIsNull) {
    QueryOrdering ord;
    EXPECT_EQ(ord.Direction(), QueryOrderingDirection::None);
    EXPECT_EQ(ord.Expression(), nullptr);
}

TEST(CSharp_QueryOrdering, DirectionScalarRoundTrip) {
    QueryOrdering ord;
    ord.Direction(QueryOrderingDirection::Descending);
    EXPECT_EQ(ord.Direction(), QueryOrderingDirection::Descending);
    ord.Direction(QueryOrderingDirection::Ascending);
    EXPECT_EQ(ord.Direction(), QueryOrderingDirection::Ascending);
}

TEST(CSharp_QueryOrdering, AllParamsCtorSetsExpressionAndDirection) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::Descending);
    EXPECT_EQ(ord.Expression(), expr.get());
    EXPECT_EQ(ord.Direction(), QueryOrderingDirection::Descending);
}

TEST(CSharp_QueryOrdering, ExpressionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryOrdering ord(a.get(), QueryOrderingDirection::None);
    EXPECT_EQ(ord.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &ord);
    ord.Expression(b.get());
    EXPECT_EQ(ord.Expression(), b.get());
    EXPECT_EQ(b->Parent(), &ord);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryOrdering, SlotStorageContract) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::None);
    EXPECT_EQ(ord.GetChildCount(), 1);
    EXPECT_EQ(ord.GetChild(0), expr.get());
    EXPECT_EQ(ord.GetChildSlotInfo(0), &QueryOrdering::ExpressionSlot);
    EXPECT_THROW(ord.GetChild(1), std::out_of_range);
    EXPECT_THROW(ord.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_QueryOrdering, SlotStaticPointsAtSharedExpressionKind) {
    EXPECT_EQ(QueryOrdering::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(QueryOrdering::ExpressionSlot.IsCollection());
    EXPECT_FALSE(QueryOrdering::ExpressionSlot.IsOptional());
    // The `ExpressionSlot` (a `CSharpSlotInfoT<Expression>`) accepts an `Expression` and
    // rejects a non-`Expression` (the `QueryOrdering` IS-A the `AstNode` but NOT an
    // `Expression`, so it is rejected).
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(QueryOrdering::ExpressionSlot.IsInstanceOfType(expr.get()));
    auto ord = std::make_unique<QueryOrdering>();
    EXPECT_FALSE(QueryOrdering::ExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(ord.get())));
}

TEST(CSharp_QueryOrdering, AcceptVisitorDispatchAndVirtuality) {
    auto expr = std::make_unique<NullReferenceExpression>();
    auto ord = std::make_unique<QueryOrdering>(expr.get(), QueryOrderingDirection::None);
    // Dispatch through `AstNode*` exercises the virtual `AcceptVisitor`.
    AstNode* asNode = ord.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"qord", "nullref"}));
}

TEST(CSharp_QueryOrdering, DepthFirstWalk) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::None);
    RecordingVisitor v;
    ord.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"qord", "nullref"}));
}

TEST(CSharp_QueryOrdering, DoMatchSame) {
    auto a1 = std::make_unique<NullReferenceExpression>();
    auto a2 = std::make_unique<NullReferenceExpression>();
    QueryOrdering p(a1.get(), QueryOrderingDirection::None);
    QueryOrdering c(a2.get(), QueryOrderingDirection::None);
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryOrdering, DoMatchRejectsDirectionMismatch) {
    auto a1 = std::make_unique<NullReferenceExpression>();
    auto a2 = std::make_unique<NullReferenceExpression>();
    QueryOrdering p(a1.get(), QueryOrderingDirection::Ascending);
    QueryOrdering c(a2.get(), QueryOrderingDirection::Descending);
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryOrdering, DoMatchNoneIsNotWildcard) {
    // `QueryOrderingDirection` has NO `Any` member, so the plain-equality term means a `None`
    // pattern matches only a `None` candidate (NOT any direction).
    auto a1 = std::make_unique<NullReferenceExpression>();
    auto a2 = std::make_unique<NullReferenceExpression>();
    QueryOrdering p(a1.get(), QueryOrderingDirection::None);
    QueryOrdering c(a2.get(), QueryOrderingDirection::Ascending);
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryOrdering, DoMatchRejectsDifferentExpression) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryOrdering p(a.get(), QueryOrderingDirection::None);
    QueryOrdering c(b.get(), QueryOrderingDirection::None);
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryOrdering, DoMatchRejectsNonQueryOrderingCandidate) {
    auto a = std::make_unique<NullReferenceExpression>();
    QueryOrdering p(a.get(), QueryOrderingDirection::None);
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(&p, static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryOrdering, DoMatchRejectsNullCandidate) {
    auto a = std::make_unique<NullReferenceExpression>();
    QueryOrdering p(a.get(), QueryOrderingDirection::None);
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryOrdering, CloneDeepCopiesChildAndScalar) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::Descending);
    std::unique_ptr<QueryOrdering> copy(ord.Clone());
    EXPECT_NE(copy.get(), &ord);
    EXPECT_EQ(copy->Direction(), QueryOrderingDirection::Descending);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), expr.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    // The original is not detached (the clone deep-copies, not steals).
    EXPECT_EQ(expr->Parent(), &ord);
}

TEST(CSharp_QueryOrdering, CloneIsVirtualAndCovariant) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::None);
    AstNode* asNode = &ord;
    std::unique_ptr<AstNode> copy(asNode->Clone());
    EXPECT_NE(dynamic_cast<QueryOrdering*>(copy.get()), nullptr);
}

TEST(CSharp_QueryOrdering, CloneSkipsAbsentChild) {
    QueryOrdering ord;
    ord.Direction(QueryOrderingDirection::Ascending);
    std::unique_ptr<QueryOrdering> copy(ord.Clone());
    EXPECT_EQ(copy->Expression(), nullptr);
    EXPECT_EQ(copy->Direction(), QueryOrderingDirection::Ascending);
}

TEST(CSharp_QueryOrdering, CheckInvariantFilledPasses) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryOrdering ord(expr.get(), QueryOrderingDirection::None);
    ord.CheckInvariant();  // should not assert
    SUCCEED();
}

// ---------------------------------------------------------------------------
// CSharp_QueryExpression -- the collection-only container
// ---------------------------------------------------------------------------
TEST(CSharp_QueryExpression, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryExpression>);
    EXPECT_TRUE(std::is_final_v<QueryExpression>);
}

TEST(CSharp_QueryExpression, IsExpressionAndAstNodeButNotQueryClause) {
    QueryExpression q;
    EXPECT_NE(dynamic_cast<Expression*>(&q), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&q), nullptr);
    EXPECT_EQ(dynamic_cast<QueryClause*>(&q), nullptr);
}

TEST(CSharp_QueryExpression, EmptyCtorReportsGetChildCountZero) {
    QueryExpression q;
    EXPECT_EQ(q.GetChildCount(), 0);
    EXPECT_EQ(q.Clauses().Count(), 0);
}

TEST(CSharp_QueryExpression, AddReparentsAndMaintainsIncrementalChildIndex) {
    QueryExpression q;
    auto where = std::make_unique<QueryWhereClause>();
    auto sel = std::make_unique<QuerySelectClause>();
    q.Clauses().Add(where.get());
    q.Clauses().Add(sel.get());
    EXPECT_EQ(q.Clauses().Count(), 2);
    EXPECT_EQ(q.GetChildCount(), 2);
    // Incremental: an element's flattened ChildIndex is exactly its local position (the
    // collection is the node's only slot and last slot).
    EXPECT_EQ(where->ChildIndex, 0);
    EXPECT_EQ(sel->ChildIndex, 1);
    EXPECT_EQ(where->Parent(), &q);
    EXPECT_EQ(sel->Parent(), &q);
}

TEST(CSharp_QueryExpression, GetCollectionByKindReturnsClauses) {
    QueryExpression q;
    EXPECT_EQ(q.GetCollectionByKind(&Slots::Clause), &q.Clauses());
    EXPECT_EQ(q.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(q.GetCollectionByKind(&Slots::Condition), nullptr);
}

TEST(CSharp_QueryExpression, SlotStorageContract) {
    QueryExpression q;
    auto where = std::make_unique<QueryWhereClause>();
    q.Clauses().Add(where.get());
    EXPECT_EQ(q.GetChild(0), where.get());
    EXPECT_EQ(q.GetChildSlotInfo(0), &QueryExpression::ClausesSlot);
    EXPECT_THROW(q.GetChild(1), std::out_of_range);
    EXPECT_THROW(q.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_QueryExpression, SlotStaticPointsAtSharedClauseKind) {
    EXPECT_EQ(QueryExpression::ClausesSlot.Kind(), &Slots::Clause);
    EXPECT_TRUE(QueryExpression::ClausesSlot.IsCollection());
    EXPECT_TRUE(QueryExpression::ClausesSlot.IsOptional());
}

TEST(CSharp_QueryExpression, AcceptVisitorDispatchAndVirtuality) {
    auto q = std::make_unique<QueryExpression>();
    AstNode* asNode = q.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"qexpr"}));
}

TEST(CSharp_QueryExpression, DepthFirstWalkEmpty) {
    QueryExpression q;
    RecordingVisitor v;
    q.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"qexpr"}));
}

TEST(CSharp_QueryExpression, DepthFirstWalkWithClauses) {
    auto q = std::make_unique<QueryExpression>();
    auto where = std::make_unique<QueryWhereClause>();
    auto cond = std::make_unique<NullReferenceExpression>();
    where->Condition(cond.get());
    auto sel = std::make_unique<QuerySelectClause>();
    auto proj = std::make_unique<ThisReferenceExpression>();
    sel->Expression(proj.get());
    q->Clauses().Add(where.get());
    q->Clauses().Add(sel.get());
    RecordingVisitor v;
    q->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"qexpr", "where", "nullref", "select", "this"}));
}

TEST(CSharp_QueryExpression, DoMatchSame) {
    auto q1 = std::make_unique<QueryExpression>();
    auto q2 = std::make_unique<QueryExpression>();
    EXPECT_TRUE(DoMatchAgainst(q1.get(), q2.get()));
}

TEST(CSharp_QueryExpression, DoMatchRejectsDifferentClauseCount) {
    auto q1 = std::make_unique<QueryExpression>();
    auto q2 = std::make_unique<QueryExpression>();
    auto sel = std::make_unique<QuerySelectClause>();
    q2->Clauses().Add(sel.get());
    EXPECT_FALSE(DoMatchAgainst(q1.get(), q2.get()));
}

TEST(CSharp_QueryExpression, DoMatchRejectsDifferentClauseValue) {
    auto q1 = std::make_unique<QueryExpression>();
    auto w1 = std::make_unique<QueryWhereClause>();
    auto c1 = std::make_unique<NullReferenceExpression>();
    w1->Condition(c1.get());
    q1->Clauses().Add(w1.get());
    auto q2 = std::make_unique<QueryExpression>();
    auto s2 = std::make_unique<QuerySelectClause>();
    auto p2 = std::make_unique<NullReferenceExpression>();
    s2->Expression(p2.get());
    q2->Clauses().Add(s2.get());
    EXPECT_FALSE(DoMatchAgainst(q1.get(), q2.get()));
}

TEST(CSharp_QueryExpression, DoMatchRejectsNonQueryExpression) {
    auto q = std::make_unique<QueryExpression>();
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(q.get(), static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryExpression, DoMatchRejectsNullCandidate) {
    auto q = std::make_unique<QueryExpression>();
    EXPECT_FALSE(DoMatchAgainst(q.get(), nullptr));
}

TEST(CSharp_QueryExpression, CloneDeepCopiesClauses) {
    auto q = std::make_unique<QueryExpression>();
    auto sel = std::make_unique<QuerySelectClause>();
    auto proj = std::make_unique<NullReferenceExpression>();
    sel->Expression(proj.get());
    q->Clauses().Add(sel.get());
    std::unique_ptr<QueryExpression> copy(q->Clone());
    EXPECT_NE(copy.get(), q.get());
    ASSERT_EQ(copy->Clauses().Count(), 1);
    auto* clonedClause = copy->Clauses().At(0);
    EXPECT_NE(clonedClause, sel.get());
    EXPECT_EQ(clonedClause->Parent(), copy.get());
    // The cloned clause is a `QuerySelectClause` whose `Expression` is a distinct clone.
    auto* clonedSel = dynamic_cast<QuerySelectClause*>(clonedClause);
    ASSERT_NE(clonedSel, nullptr);
    ASSERT_NE(clonedSel->Expression(), nullptr);
    EXPECT_NE(clonedSel->Expression(), proj.get());
    EXPECT_EQ(clonedSel->Expression()->Parent(), clonedSel);
}

TEST(CSharp_QueryExpression, CloneIsVirtualAndCovariant) {
    auto q = std::make_unique<QueryExpression>();
    AstNode* asNode = q.get();
    std::unique_ptr<AstNode> copy(asNode->Clone());
    EXPECT_NE(dynamic_cast<QueryExpression*>(copy.get()), nullptr);
    Expression* asExpr = q.get();
    std::unique_ptr<Expression> ecopy(asExpr->Clone());
    EXPECT_NE(dynamic_cast<QueryExpression*>(ecopy.get()), nullptr);
}

TEST(CSharp_QueryExpression, CloneEmpty) {
    QueryExpression q;
    std::unique_ptr<QueryExpression> copy(q.Clone());
    EXPECT_EQ(copy->Clauses().Count(), 0);
}

TEST(CSharp_QueryExpression, CheckInvariantEmptyPasses) {
    QueryExpression q;
    q.CheckInvariant();  // empty passes (no required slots)
    SUCCEED();
}

// ---------------------------------------------------------------------------
// CSharp_QueryWhereClause -- the where clause (single required Condition Expression)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryWhereClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryWhereClause>);
    EXPECT_TRUE(std::is_final_v<QueryWhereClause>);
}

TEST(CSharp_QueryWhereClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryWhereClause w;
    EXPECT_NE(dynamic_cast<QueryClause*>(&w), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&w), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&w), nullptr);
}

TEST(CSharp_QueryWhereClause, ConstKeyword) {
    EXPECT_STREQ(QueryWhereClause::WhereKeyword, "where");
}

TEST(CSharp_QueryWhereClause, DefaultCtorConditionIsNull) {
    QueryWhereClause w;
    EXPECT_EQ(w.Condition(), nullptr);
    EXPECT_EQ(w.GetChildCount(), 1);  // the single slot counts even when null
}

TEST(CSharp_QueryWhereClause, RequiredPrefixCtorSetsCondition) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(expr.get());
    EXPECT_EQ(w.Condition(), expr.get());
    EXPECT_EQ(expr->Parent(), &w);
}

TEST(CSharp_QueryWhereClause, ConditionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryWhereClause w(a.get());
    EXPECT_EQ(a->Parent(), &w);
    w.Condition(b.get());
    EXPECT_EQ(w.Condition(), b.get());
    EXPECT_EQ(b->Parent(), &w);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryWhereClause, SlotStorageContract) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(expr.get());
    EXPECT_EQ(w.GetChildCount(), 1);
    EXPECT_EQ(w.GetChild(0), expr.get());
    EXPECT_EQ(w.GetChildSlotInfo(0), &QueryWhereClause::ConditionSlot);
    EXPECT_THROW(w.GetChild(1), std::out_of_range);
}

TEST(CSharp_QueryWhereClause, SlotStaticPointsAtSharedConditionKind) {
    EXPECT_EQ(QueryWhereClause::ConditionSlot.Kind(), &Slots::Condition);
    EXPECT_FALSE(QueryWhereClause::ConditionSlot.IsCollection());
    EXPECT_FALSE(QueryWhereClause::ConditionSlot.IsOptional());
}

TEST(CSharp_QueryWhereClause, AcceptVisitorDispatchAndVirtuality) {
    auto w = std::make_unique<QueryWhereClause>();
    AstNode* asNode = w.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"where"}));
}

TEST(CSharp_QueryWhereClause, DepthFirstWalkWithCondition) {
    auto w = std::make_unique<QueryWhereClause>();
    auto cond = std::make_unique<NullReferenceExpression>();
    w->Condition(cond.get());
    RecordingVisitor v;
    w->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"where", "nullref"}));
}

TEST(CSharp_QueryWhereClause, DoMatchSame) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<NullReferenceExpression>();
    QueryWhereClause p(a.get());
    QueryWhereClause c(b.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryWhereClause, DoMatchRejectsDifferentCondition) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryWhereClause p(a.get());
    QueryWhereClause c(b.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryWhereClause, DoMatchRejectsNonQueryWhereClause) {
    auto a = std::make_unique<NullReferenceExpression>();
    QueryWhereClause p(a.get());
    auto sel = std::make_unique<QuerySelectClause>();
    auto expr = std::make_unique<NullReferenceExpression>();
    sel->Expression(expr.get());
    EXPECT_FALSE(DoMatchAgainst(&p, sel.get()));
}

TEST(CSharp_QueryWhereClause, DoMatchRejectsNullCandidate) {
    auto a = std::make_unique<NullReferenceExpression>();
    QueryWhereClause p(a.get());
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryWhereClause, CloneDeepCopiesCondition) {
    auto cond = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(cond.get());
    std::unique_ptr<QueryWhereClause> copy(w.Clone());
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    EXPECT_EQ(cond->Parent(), &w);  // original not detached
}

TEST(CSharp_QueryWhereClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto cond = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(cond.get());
    QueryClause* asClause = &w;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryWhereClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryWhereClause, CheckInvariantFilledPasses) {
    auto cond = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(cond.get());
    w.CheckInvariant();  // should not assert
    SUCCEED();
}

// ---------------------------------------------------------------------------
// CSharp_QuerySelectClause -- the select clause (single required Expression)
// ---------------------------------------------------------------------------
TEST(CSharp_QuerySelectClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QuerySelectClause>);
    EXPECT_TRUE(std::is_final_v<QuerySelectClause>);
}

TEST(CSharp_QuerySelectClause, IsQueryClauseAndAstNodeButNotExpression) {
    QuerySelectClause s;
    EXPECT_NE(dynamic_cast<QueryClause*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_QuerySelectClause, ConstKeyword) {
    EXPECT_STREQ(QuerySelectClause::SelectKeyword, "select");
}

TEST(CSharp_QuerySelectClause, DefaultCtorExpressionIsNull) {
    QuerySelectClause s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_QuerySelectClause, RequiredPrefixCtorSetsExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QuerySelectClause s(expr.get());
    EXPECT_EQ(s.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &s);
}

TEST(CSharp_QuerySelectClause, ExpressionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QuerySelectClause s(a.get());
    s.Expression(b.get());
    EXPECT_EQ(s.Expression(), b.get());
    EXPECT_EQ(b->Parent(), &s);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QuerySelectClause, SlotStorageContract) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QuerySelectClause s(expr.get());
    EXPECT_EQ(s.GetChildCount(), 1);
    EXPECT_EQ(s.GetChild(0), expr.get());
    EXPECT_EQ(s.GetChildSlotInfo(0), &QuerySelectClause::ExpressionSlot);
}

TEST(CSharp_QuerySelectClause, SlotStaticPointsAtSharedExpressionKind) {
    EXPECT_EQ(QuerySelectClause::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(QuerySelectClause::ExpressionSlot.IsCollection());
    EXPECT_FALSE(QuerySelectClause::ExpressionSlot.IsOptional());
}

TEST(CSharp_QuerySelectClause, AcceptVisitorDispatchAndVirtuality) {
    auto s = std::make_unique<QuerySelectClause>();
    AstNode* asNode = s.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"select"}));
}

TEST(CSharp_QuerySelectClause, DepthFirstWalkWithExpression) {
    auto s = std::make_unique<QuerySelectClause>();
    auto expr = std::make_unique<NullReferenceExpression>();
    s->Expression(expr.get());
    RecordingVisitor v;
    s->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"select", "nullref"}));
}

TEST(CSharp_QuerySelectClause, DoMatchSame) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<NullReferenceExpression>();
    QuerySelectClause p(a.get());
    QuerySelectClause c(b.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QuerySelectClause, DoMatchRejectsDifferentExpression) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QuerySelectClause p(a.get());
    QuerySelectClause c(b.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QuerySelectClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<NullReferenceExpression>();
    QuerySelectClause p(a.get());
    QueryWhereClause c(b.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QuerySelectClause, DoMatchRejectsNullCandidate) {
    auto a = std::make_unique<NullReferenceExpression>();
    QuerySelectClause p(a.get());
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QuerySelectClause, CloneDeepCopiesExpression) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QuerySelectClause s(expr.get());
    std::unique_ptr<QuerySelectClause> copy(s.Clone());
    EXPECT_NE(copy->Expression(), expr.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(expr->Parent(), &s);
}

TEST(CSharp_QuerySelectClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QuerySelectClause s(expr.get());
    QueryClause* asClause = &s;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QuerySelectClause*>(copy.get()), nullptr);
}

TEST(CSharp_QuerySelectClause, CheckInvariantFilledPasses) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QuerySelectClause s(expr.get());
    s.CheckInvariant();  // should not assert
    SUCCEED();
}

// ---------------------------------------------------------------------------
// CSharp_QueryOrderClause -- the orderby clause (collection-only Orderings)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryOrderClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryOrderClause>);
    EXPECT_TRUE(std::is_final_v<QueryOrderClause>);
}

TEST(CSharp_QueryOrderClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryOrderClause o;
    EXPECT_NE(dynamic_cast<QueryClause*>(&o), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&o), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&o), nullptr);
}

TEST(CSharp_QueryOrderClause, ConstKeyword) {
    EXPECT_STREQ(QueryOrderClause::OrderbyKeyword, "orderby");
}

TEST(CSharp_QueryOrderClause, EmptyCtorReportsGetChildCountZero) {
    QueryOrderClause o;
    EXPECT_EQ(o.GetChildCount(), 0);
    EXPECT_EQ(o.Orderings().Count(), 0);
}

TEST(CSharp_QueryOrderClause, AddReparentsAndMaintainsIncrementalChildIndex) {
    QueryOrderClause o;
    auto ord1 = std::make_unique<QueryOrdering>();
    auto ord2 = std::make_unique<QueryOrdering>();
    o.Orderings().Add(ord1.get());
    o.Orderings().Add(ord2.get());
    EXPECT_EQ(o.Orderings().Count(), 2);
    EXPECT_EQ(o.GetChildCount(), 2);
    EXPECT_EQ(ord1->ChildIndex, 0);
    EXPECT_EQ(ord2->ChildIndex, 1);
    EXPECT_EQ(ord1->Parent(), &o);
    EXPECT_EQ(ord2->Parent(), &o);
}

TEST(CSharp_QueryOrderClause, GetCollectionByKindReturnsOrderings) {
    QueryOrderClause o;
    EXPECT_EQ(o.GetCollectionByKind(&Slots::Ordering), &o.Orderings());
    EXPECT_EQ(o.GetCollectionByKind(&Slots::Clause), nullptr);
}

TEST(CSharp_QueryOrderClause, SlotStorageContract) {
    QueryOrderClause o;
    auto ord = std::make_unique<QueryOrdering>();
    o.Orderings().Add(ord.get());
    EXPECT_EQ(o.GetChild(0), ord.get());
    EXPECT_EQ(o.GetChildSlotInfo(0), &QueryOrderClause::OrderingsSlot);
    EXPECT_THROW(o.GetChild(1), std::out_of_range);
}

TEST(CSharp_QueryOrderClause, SlotStaticPointsAtSharedOrderingKind) {
    EXPECT_EQ(QueryOrderClause::OrderingsSlot.Kind(), &Slots::Ordering);
    EXPECT_TRUE(QueryOrderClause::OrderingsSlot.IsCollection());
    EXPECT_TRUE(QueryOrderClause::OrderingsSlot.IsOptional());
}

TEST(CSharp_QueryOrderClause, AcceptVisitorDispatchAndVirtuality) {
    auto o = std::make_unique<QueryOrderClause>();
    AstNode* asNode = o.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"orderby"}));
}

TEST(CSharp_QueryOrderClause, DepthFirstWalkEmpty) {
    QueryOrderClause o;
    RecordingVisitor v;
    o.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"orderby"}));
}

TEST(CSharp_QueryOrderClause, DepthFirstWalkWithOrderings) {
    auto o = std::make_unique<QueryOrderClause>();
    auto ord = std::make_unique<QueryOrdering>();
    auto expr = std::make_unique<NullReferenceExpression>();
    ord->Expression(expr.get());
    o->Orderings().Add(ord.get());
    RecordingVisitor v;
    o->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"orderby", "qord", "nullref"}));
}

TEST(CSharp_QueryOrderClause, DoMatchSame) {
    auto o1 = std::make_unique<QueryOrderClause>();
    auto o2 = std::make_unique<QueryOrderClause>();
    EXPECT_TRUE(DoMatchAgainst(o1.get(), o2.get()));
}

TEST(CSharp_QueryOrderClause, DoMatchRejectsDifferentOrderingCount) {
    auto o1 = std::make_unique<QueryOrderClause>();
    auto o2 = std::make_unique<QueryOrderClause>();
    auto ord = std::make_unique<QueryOrdering>();
    o2->Orderings().Add(ord.get());
    EXPECT_FALSE(DoMatchAgainst(o1.get(), o2.get()));
}

TEST(CSharp_QueryOrderClause, DoMatchRejectsDifferentOrderingValue) {
    auto o1 = std::make_unique<QueryOrderClause>();
    auto ord1 = std::make_unique<QueryOrdering>();
    auto e1 = std::make_unique<NullReferenceExpression>();
    ord1->Expression(e1.get());
    o1->Orderings().Add(ord1.get());
    auto o2 = std::make_unique<QueryOrderClause>();
    auto ord2 = std::make_unique<QueryOrdering>();
    auto e2 = std::make_unique<ThisReferenceExpression>();
    ord2->Expression(e2.get());
    o2->Orderings().Add(ord2.get());
    EXPECT_FALSE(DoMatchAgainst(o1.get(), o2.get()));
}

TEST(CSharp_QueryOrderClause, DoMatchRejectsNonQueryOrderClause) {
    auto o = std::make_unique<QueryOrderClause>();
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(o.get(), static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryOrderClause, DoMatchRejectsNullCandidate) {
    auto o = std::make_unique<QueryOrderClause>();
    EXPECT_FALSE(DoMatchAgainst(o.get(), nullptr));
}

TEST(CSharp_QueryOrderClause, CloneDeepCopiesOrderings) {
    auto o = std::make_unique<QueryOrderClause>();
    auto ord = std::make_unique<QueryOrdering>();
    auto expr = std::make_unique<NullReferenceExpression>();
    ord->Expression(expr.get());
    o->Orderings().Add(ord.get());
    std::unique_ptr<QueryOrderClause> copy(o->Clone());
    ASSERT_EQ(copy->Orderings().Count(), 1);
    auto* clonedOrd = copy->Orderings().At(0);
    EXPECT_NE(clonedOrd, ord.get());
    EXPECT_EQ(clonedOrd->Parent(), copy.get());
    ASSERT_NE(clonedOrd->Expression(), nullptr);
    EXPECT_NE(clonedOrd->Expression(), expr.get());
    EXPECT_EQ(clonedOrd->Expression()->Parent(), clonedOrd);
}

TEST(CSharp_QueryOrderClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto o = std::make_unique<QueryOrderClause>();
    QueryClause* asClause = o.get();
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryOrderClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryOrderClause, CheckInvariantEmptyPasses) {
    QueryOrderClause o;
    o.CheckInvariant();  // empty passes (no required slots)
    SUCCEED();
}
