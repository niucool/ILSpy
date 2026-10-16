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

// Tests for the remaining query clauses (the second slice of the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs): `QueryLetClause`
// (let_clause), `QueryGroupClause` (group_clause), `QueryFromClause` (from_clause), and
// `QueryContinuationClause` (query_continuation). `query_expression ::= query_clause+`
// (C# grammar 12.23.1). The first slice (QueryClause/QueryOrdering/QueryExpression +
// QueryWhereClause/QuerySelectClause/QueryOrderClause) is in QueryExpression_Test.cpp (D310);
// the complex `QueryJoinClause` (6 slots + 5 new Slots kinds) lands next.

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
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryContinuationClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryFromClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryGroupClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryLetClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the concrete nodes under test (plus the leaf
// `Expression`s, the `Identifier` token, the `PrimitiveType` used as an `AstType` operand, the
// `QueryExpression` used as a `PrecedingQuery` child, the `QueryWhereClause` used for the
// cross-sibling DoMatch rejection, and the `WhileStatement` used for the cross-type DoMatch
// rejection), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitQueryLetClause(QueryLetClause* node) override {
        if (node == nullptr) { trace.push_back("<null-let>"); return; }
        trace.push_back("let");
        VisitChildren(node);
    }
    void VisitQueryGroupClause(QueryGroupClause* node) override {
        if (node == nullptr) { trace.push_back("<null-group>"); return; }
        trace.push_back("group");
        VisitChildren(node);
    }
    void VisitQueryFromClause(QueryFromClause* node) override {
        if (node == nullptr) { trace.push_back("<null-from>"); return; }
        trace.push_back("from");
        VisitChildren(node);
    }
    void VisitQueryContinuationClause(QueryContinuationClause* node) override {
        if (node == nullptr) { trace.push_back("<null-cont>"); return; }
        trace.push_back("cont");
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
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
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
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-primtype>"); return; }
        trace.push_back("primtype");
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
// CSharp_QueryLetClause -- the let clause (required Identifier string + required Expression)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryLetClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryLetClause>);
    EXPECT_TRUE(std::is_final_v<QueryLetClause>);
}

TEST(CSharp_QueryLetClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryLetClause l;
    EXPECT_NE(dynamic_cast<QueryClause*>(&l), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&l), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&l), nullptr);
}

TEST(CSharp_QueryLetClause, ConstKeyword) {
    EXPECT_STREQ(QueryLetClause::LetKeyword, "let");
}

TEST(CSharp_QueryLetClause, DefaultCtorSlotsAreNull) {
    QueryLetClause l;
    EXPECT_EQ(l.IdentifierToken(), nullptr);
    EXPECT_EQ(l.Expression(), nullptr);
    EXPECT_EQ(l.GetChildCount(), 2);
}

TEST(CSharp_QueryLetClause, AllParamsCtorSetsSlots) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryLetClause l(std::string("x"), expr.get());
    EXPECT_EQ(l.Identifier(), "x");
    EXPECT_NE(l.IdentifierToken(), nullptr);
    EXPECT_EQ(l.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &l);
    EXPECT_EQ(l.IdentifierToken()->Parent(), &l);
}

TEST(CSharp_QueryLetClause, IdentifierSetterCreatesTokenForEmpty) {
    QueryLetClause l;
    l.Identifier(std::string(""));
    EXPECT_NE(l.IdentifierToken(), nullptr);  // empty name yields a token with empty Name
    EXPECT_EQ(l.Identifier(), "");
    EXPECT_EQ(l.IdentifierToken()->Parent(), &l);
}

TEST(CSharp_QueryLetClause, ExpressionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryLetClause l(std::string("x"), a.get());
    l.Expression(b.get());
    EXPECT_EQ(l.Expression(), b.get());
    EXPECT_EQ(b->Parent(), &l);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryLetClause, IdentifierTokenSetterReparents) {
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("y")));
    QueryLetClause l;
    l.IdentifierToken(tok.get());
    EXPECT_EQ(l.IdentifierToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &l);
}

TEST(CSharp_QueryLetClause, SlotStorageContract) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryLetClause l(std::string("x"), expr.get());
    EXPECT_EQ(l.GetChildCount(), 2);
    EXPECT_EQ(l.GetChild(0), l.IdentifierToken());
    EXPECT_EQ(l.GetChild(1), expr.get());
    EXPECT_EQ(l.GetChildSlotInfo(0), &QueryLetClause::IdentifierTokenSlot);
    EXPECT_EQ(l.GetChildSlotInfo(1), &QueryLetClause::ExpressionSlot);
}

TEST(CSharp_QueryLetClause, SlotStaticPointsAtSharedKinds) {
    EXPECT_EQ(QueryLetClause::IdentifierTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(QueryLetClause::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(QueryLetClause::IdentifierTokenSlot.IsCollection());
    EXPECT_FALSE(QueryLetClause::IdentifierTokenSlot.IsOptional());
    EXPECT_FALSE(QueryLetClause::ExpressionSlot.IsCollection());
    EXPECT_FALSE(QueryLetClause::ExpressionSlot.IsOptional());
}

TEST(CSharp_QueryLetClause, AcceptVisitorDispatchAndVirtuality) {
    auto l = std::make_unique<QueryLetClause>(std::string("x"),
        std::make_unique<NullReferenceExpression>().get());
    // Use a bare node (no children) so VisitChildren yields just the node tag.
    auto bare = std::make_unique<QueryLetClause>();
    AstNode* asNode = bare.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"let"}));
}

TEST(CSharp_QueryLetClause, DepthFirstWalkVisitsTokenThenExpression) {
    auto l = std::make_unique<QueryLetClause>();
    auto expr = std::make_unique<NullReferenceExpression>();
    l->Expression(expr.get());
    l->Identifier(std::string("x"));
    RecordingVisitor v;
    l->AcceptVisitor(v);
    // The backing IdentifierToken (index 0) is a visited child, then the Expression (index 1).
    EXPECT_EQ(v.trace, std::vector<std::string>({"let", "id:x", "nullref"}));
}

TEST(CSharp_QueryLetClause, DoMatchSame) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    QueryLetClause c(std::string("x"), eb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryLetClause, DoMatchRejectsDifferentIdentifier) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    QueryLetClause c(std::string("y"), eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryLetClause, DoMatchRejectsDifferentExpression) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<ThisReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    QueryLetClause c(std::string("x"), eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryLetClause, DoMatchAcceptsAnyStringWildcardOnIdentifier) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string(PatternMatching::Pattern::AnyString), ea.get());
    QueryLetClause c(std::string("anything"), eb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryLetClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    QueryWhereClause c(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QueryLetClause, DoMatchRejectsNonQueryLetClause) {
    auto ea = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(&p, static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryLetClause, DoMatchRejectsNullCandidate) {
    auto ea = std::make_unique<NullReferenceExpression>();
    QueryLetClause p(std::string("x"), ea.get());
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryLetClause, CloneDeepCopiesSlots) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryLetClause l(std::string("x"), expr.get());
    std::unique_ptr<QueryLetClause> copy(l.Clone());
    EXPECT_EQ(copy->Identifier(), "x");
    EXPECT_NE(copy->IdentifierToken(), l.IdentifierToken());
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());
    EXPECT_NE(copy->Expression(), expr.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(expr->Parent(), &l);  // original not detached
}

TEST(CSharp_QueryLetClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryLetClause l(std::string("x"), expr.get());
    QueryClause* asClause = &l;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryLetClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryLetClause, CheckInvariantFilledPasses) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryLetClause l(std::string("x"), expr.get());
    l.CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_QueryLetClause, CheckInvariantEmptyRejects) {
    QueryLetClause l;
    EXPECT_DEATH(l.CheckInvariant(), "");
}
#endif

// ---------------------------------------------------------------------------
// CSharp_QueryGroupClause -- the group clause (two required Expression slots)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryGroupClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryGroupClause>);
    EXPECT_TRUE(std::is_final_v<QueryGroupClause>);
}

TEST(CSharp_QueryGroupClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryGroupClause g;
    EXPECT_NE(dynamic_cast<QueryClause*>(&g), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&g), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&g), nullptr);
}

TEST(CSharp_QueryGroupClause, ConstKeywords) {
    EXPECT_STREQ(QueryGroupClause::GroupKeyword, "group");
    EXPECT_STREQ(QueryGroupClause::ByKeyword, "by");
}

TEST(CSharp_QueryGroupClause, DefaultCtorSlotsAreNull) {
    QueryGroupClause g;
    EXPECT_EQ(g.Projection(), nullptr);
    EXPECT_EQ(g.Key(), nullptr);
    EXPECT_EQ(g.GetChildCount(), 2);
}

TEST(CSharp_QueryGroupClause, AllParamsCtorSetsSlots) {
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(proj.get(), key.get());
    EXPECT_EQ(g.Projection(), proj.get());
    EXPECT_EQ(g.Key(), key.get());
    EXPECT_EQ(proj->Parent(), &g);
    EXPECT_EQ(key->Parent(), &g);
}

TEST(CSharp_QueryGroupClause, ProjectionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(a.get(), std::make_unique<NullReferenceExpression>().get());
    g.Projection(b.get());
    EXPECT_EQ(g.Projection(), b.get());
    EXPECT_EQ(b->Parent(), &g);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryGroupClause, KeySetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(std::make_unique<NullReferenceExpression>().get(), a.get());
    g.Key(b.get());
    EXPECT_EQ(g.Key(), b.get());
    EXPECT_EQ(b->Parent(), &g);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryGroupClause, SlotStorageContract) {
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(proj.get(), key.get());
    EXPECT_EQ(g.GetChildCount(), 2);
    EXPECT_EQ(g.GetChild(0), proj.get());
    EXPECT_EQ(g.GetChild(1), key.get());
    EXPECT_EQ(g.GetChildSlotInfo(0), &QueryGroupClause::ProjectionSlot);
    EXPECT_EQ(g.GetChildSlotInfo(1), &QueryGroupClause::KeySlot);
}

TEST(CSharp_QueryGroupClause, SlotStaticsPointAtNewKinds) {
    EXPECT_EQ(QueryGroupClause::ProjectionSlot.Kind(), &Slots::Projection);
    EXPECT_EQ(QueryGroupClause::KeySlot.Kind(), &Slots::Key);
    EXPECT_FALSE(QueryGroupClause::ProjectionSlot.IsCollection());
    EXPECT_FALSE(QueryGroupClause::ProjectionSlot.IsOptional());
    EXPECT_FALSE(QueryGroupClause::KeySlot.IsCollection());
    EXPECT_FALSE(QueryGroupClause::KeySlot.IsOptional());
}

TEST(CSharp_QueryGroupClause, NewSlotsKindsAreDistinct) {
    EXPECT_NE(&Slots::Projection, &Slots::Key);
    EXPECT_NE(&Slots::Projection, &Slots::Expression);
    EXPECT_NE(&Slots::Projection, &Slots::Condition);
    EXPECT_NE(&Slots::Key, &Slots::Expression);
    EXPECT_NE(&Slots::Key, &Slots::Condition);
}

TEST(CSharp_QueryGroupClause, AcceptVisitorDispatchAndVirtuality) {
    auto bare = std::make_unique<QueryGroupClause>();
    AstNode* asNode = bare.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"group"}));
}

TEST(CSharp_QueryGroupClause, DepthFirstWalkVisitsProjectionThenKey) {
    auto g = std::make_unique<QueryGroupClause>();
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    g->Projection(proj.get());
    g->Key(key.get());
    RecordingVisitor v;
    g->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"group", "nullref", "this"}));
}

TEST(CSharp_QueryGroupClause, DoMatchSame) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<ThisReferenceExpression>();
    auto pb = std::make_unique<NullReferenceExpression>();
    auto kb = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    QueryGroupClause c(pb.get(), kb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryGroupClause, DoMatchRejectsDifferentProjection) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<ThisReferenceExpression>();
    auto pb = std::make_unique<ThisReferenceExpression>();
    auto kb = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    QueryGroupClause c(pb.get(), kb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryGroupClause, DoMatchRejectsDifferentKey) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<ThisReferenceExpression>();
    auto pb = std::make_unique<NullReferenceExpression>();
    auto kb = std::make_unique<NullReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    QueryGroupClause c(pb.get(), kb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryGroupClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    QueryWhereClause c(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QueryGroupClause, DoMatchRejectsNonQueryGroupClause) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<NullReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(&p, static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryGroupClause, DoMatchRejectsNullCandidate) {
    auto pa = std::make_unique<NullReferenceExpression>();
    auto ka = std::make_unique<NullReferenceExpression>();
    QueryGroupClause p(pa.get(), ka.get());
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryGroupClause, CloneDeepCopiesBothSlots) {
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(proj.get(), key.get());
    std::unique_ptr<QueryGroupClause> copy(g.Clone());
    EXPECT_NE(copy->Projection(), proj.get());
    EXPECT_EQ(copy->Projection()->Parent(), copy.get());
    EXPECT_NE(copy->Key(), key.get());
    EXPECT_EQ(copy->Key()->Parent(), copy.get());
    EXPECT_EQ(proj->Parent(), &g);
    EXPECT_EQ(key->Parent(), &g);
}

TEST(CSharp_QueryGroupClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(proj.get(), key.get());
    QueryClause* asClause = &g;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryGroupClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryGroupClause, CheckInvariantFilledPasses) {
    auto proj = std::make_unique<NullReferenceExpression>();
    auto key = std::make_unique<ThisReferenceExpression>();
    QueryGroupClause g(proj.get(), key.get());
    g.CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_QueryGroupClause, CheckInvariantEmptyRejects) {
    QueryGroupClause g;
    EXPECT_DEATH(g.CheckInvariant(), "");
}
#endif

// ---------------------------------------------------------------------------
// CSharp_QueryFromClause -- the from clause (nullable Type + required Identifier + required Expression)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryFromClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryFromClause>);
    EXPECT_TRUE(std::is_final_v<QueryFromClause>);
}

TEST(CSharp_QueryFromClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryFromClause f;
    EXPECT_NE(dynamic_cast<QueryClause*>(&f), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&f), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&f), nullptr);
}

TEST(CSharp_QueryFromClause, ConstKeywords) {
    EXPECT_STREQ(QueryFromClause::FromKeyword, "from");
    EXPECT_STREQ(QueryFromClause::InKeyword, "in");
}

TEST(CSharp_QueryFromClause, DefaultCtorSlotsAreNull) {
    QueryFromClause f;
    EXPECT_EQ(f.Type(), nullptr);
    EXPECT_EQ(f.IdentifierToken(), nullptr);
    EXPECT_EQ(f.Expression(), nullptr);
    EXPECT_EQ(f.GetChildCount(), 3);
}

TEST(CSharp_QueryFromClause, AllParamsCtorWithNullTypeSetsSlots) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(nullptr, std::string("x"), expr.get());
    EXPECT_EQ(f.Type(), nullptr);
    EXPECT_EQ(f.Identifier(), "x");
    EXPECT_NE(f.IdentifierToken(), nullptr);
    EXPECT_EQ(f.Expression(), expr.get());
    EXPECT_EQ(expr->Parent(), &f);
    EXPECT_EQ(f.IdentifierToken()->Parent(), &f);
}

TEST(CSharp_QueryFromClause, AllParamsCtorWithTypeSetsSlots) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(type.get(), std::string("x"), expr.get());
    EXPECT_EQ(f.Type(), type.get());
    EXPECT_EQ(f.Identifier(), "x");
    EXPECT_EQ(f.Expression(), expr.get());
    EXPECT_EQ(type->Parent(), &f);
}

TEST(CSharp_QueryFromClause, TypeSetterReparentsAndDetaches) {
    auto a = std::make_unique<PrimitiveType>(std::string("int"));
    auto b = std::make_unique<PrimitiveType>(std::string("long"));
    QueryFromClause f(a.get(), std::string("x"), std::make_unique<NullReferenceExpression>().get());
    f.Type(b.get());
    EXPECT_EQ(f.Type(), b.get());
    EXPECT_EQ(b->Parent(), &f);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryFromClause, IdentifierSetterCreatesTokenForEmpty) {
    QueryFromClause f;
    f.Identifier(std::string(""));
    EXPECT_NE(f.IdentifierToken(), nullptr);
    EXPECT_EQ(f.Identifier(), "");
    EXPECT_EQ(f.IdentifierToken()->Parent(), &f);
}

TEST(CSharp_QueryFromClause, ExpressionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    QueryFromClause f(nullptr, std::string("x"), a.get());
    f.Expression(b.get());
    EXPECT_EQ(f.Expression(), b.get());
    EXPECT_EQ(b->Parent(), &f);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryFromClause, SlotStorageContract) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(type.get(), std::string("x"), expr.get());
    EXPECT_EQ(f.GetChildCount(), 3);
    EXPECT_EQ(f.GetChild(0), type.get());
    EXPECT_EQ(f.GetChild(1), f.IdentifierToken());
    EXPECT_EQ(f.GetChild(2), expr.get());
    EXPECT_EQ(f.GetChildSlotInfo(0), &QueryFromClause::TypeSlot);
    EXPECT_EQ(f.GetChildSlotInfo(1), &QueryFromClause::IdentifierTokenSlot);
    EXPECT_EQ(f.GetChildSlotInfo(2), &QueryFromClause::ExpressionSlot);
}

TEST(CSharp_QueryFromClause, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(QueryFromClause::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(QueryFromClause::IdentifierTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(QueryFromClause::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_TRUE(QueryFromClause::TypeSlot.IsOptional());   // Type is nullable
    EXPECT_FALSE(QueryFromClause::IdentifierTokenSlot.IsOptional());
    EXPECT_FALSE(QueryFromClause::ExpressionSlot.IsOptional());
}

TEST(CSharp_QueryFromClause, AcceptVisitorDispatchAndVirtuality) {
    auto bare = std::make_unique<QueryFromClause>();
    AstNode* asNode = bare.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"from"}));
}

TEST(CSharp_QueryFromClause, DepthFirstWalkWithTypeAndNameAndExpression) {
    auto f = std::make_unique<QueryFromClause>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto expr = std::make_unique<NullReferenceExpression>();
    f->Type(type.get());
    f->Identifier(std::string("x"));
    f->Expression(expr.get());
    RecordingVisitor v;
    f->AcceptVisitor(v);
    // Type (index 0, a PrimitiveType leaf), then the backing IdentifierToken (index 1), then
    // the Expression (index 2).
    EXPECT_EQ(v.trace, std::vector<std::string>({"from", "primtype", "id:x", "nullref"}));
}

TEST(CSharp_QueryFromClause, DepthFirstWalkWithNullType) {
    auto f = std::make_unique<QueryFromClause>();
    auto expr = std::make_unique<NullReferenceExpression>();
    f->Identifier(std::string("x"));
    f->Expression(expr.get());
    RecordingVisitor v;
    f->AcceptVisitor(v);
    // Null Type skipped; the backing IdentifierToken then the Expression.
    EXPECT_EQ(v.trace, std::vector<std::string>({"from", "id:x", "nullref"}));
}

TEST(CSharp_QueryFromClause, DoMatchSameWithNullType) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    QueryFromClause c(nullptr, std::string("x"), eb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryFromClause, DoMatchSameWithType) {
    auto ta = std::make_unique<PrimitiveType>(std::string("int"));
    auto tb = std::make_unique<PrimitiveType>(std::string("int"));
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(ta.get(), std::string("x"), ea.get());
    QueryFromClause c(tb.get(), std::string("x"), eb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsTypeAsymmetry) {
    auto ta = std::make_unique<PrimitiveType>(std::string("int"));
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(ta.get(), std::string("x"), ea.get());
    QueryFromClause c(nullptr, std::string("x"), eb.get());  // no Type
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsDifferentIdentifier) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    QueryFromClause c(nullptr, std::string("y"), eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsDifferentExpression) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<ThisReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    QueryFromClause c(nullptr, std::string("x"), eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryFromClause, DoMatchAcceptsAnyStringWildcardOnIdentifier) {
    auto ta = std::make_unique<PrimitiveType>(std::string("int"));
    auto tb = std::make_unique<PrimitiveType>(std::string("int"));
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(ta.get(), std::string(PatternMatching::Pattern::AnyString), ea.get());
    QueryFromClause c(tb.get(), std::string("anything"), eb.get());
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto ea = std::make_unique<NullReferenceExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    QueryWhereClause c(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsNonQueryFromClause) {
    auto ea = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(&p, static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryFromClause, DoMatchRejectsNullCandidate) {
    auto ea = std::make_unique<NullReferenceExpression>();
    QueryFromClause p(nullptr, std::string("x"), ea.get());
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryFromClause, CloneDeepCopiesSlots) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(type.get(), std::string("x"), expr.get());
    std::unique_ptr<QueryFromClause> copy(f.Clone());
    EXPECT_EQ(copy->Identifier(), "x");
    EXPECT_NE(copy->IdentifierToken(), f.IdentifierToken());
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_NE(copy->Expression(), expr.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(expr->Parent(), &f);
}

TEST(CSharp_QueryFromClause, CloneSkipsNullType) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(nullptr, std::string("x"), expr.get());
    std::unique_ptr<QueryFromClause> copy(f.Clone());
    EXPECT_EQ(copy->Type(), nullptr);  // null Type skipped
    EXPECT_EQ(copy->Identifier(), "x");
}

TEST(CSharp_QueryFromClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(nullptr, std::string("x"), expr.get());
    QueryClause* asClause = &f;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryFromClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryFromClause, CheckInvariantFilledPassesWithNullType) {
    auto expr = std::make_unique<NullReferenceExpression>();
    QueryFromClause f(nullptr, std::string("x"), expr.get());
    f.CheckInvariant();  // Type is nullable so a null Type is invariant-valid
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_QueryFromClause, CheckInvariantEmptyRejects) {
    QueryFromClause f;
    EXPECT_DEATH(f.CheckInvariant(), "");
}
#endif

// ---------------------------------------------------------------------------
// CSharp_QueryContinuationClause -- the into continuation (required QueryExpression + required Identifier)
// ---------------------------------------------------------------------------
TEST(CSharp_QueryContinuationClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryContinuationClause>);
    EXPECT_TRUE(std::is_final_v<QueryContinuationClause>);
}

TEST(CSharp_QueryContinuationClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryContinuationClause c;
    EXPECT_NE(dynamic_cast<QueryClause*>(&c), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&c), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&c), nullptr);
}

TEST(CSharp_QueryContinuationClause, ConstKeyword) {
    EXPECT_STREQ(QueryContinuationClause::IntoKeyword, "into");
}

TEST(CSharp_QueryContinuationClause, DefaultCtorSlotsAreNull) {
    QueryContinuationClause c;
    EXPECT_EQ(c.PrecedingQuery(), nullptr);
    EXPECT_EQ(c.IdentifierToken(), nullptr);
    EXPECT_EQ(c.GetChildCount(), 2);
}

TEST(CSharp_QueryContinuationClause, AllParamsCtorSetsSlots) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    EXPECT_EQ(c.PrecedingQuery(), qexpr.get());
    EXPECT_EQ(c.Identifier(), "x");
    EXPECT_NE(c.IdentifierToken(), nullptr);
    EXPECT_EQ(qexpr->Parent(), &c);
    EXPECT_EQ(c.IdentifierToken()->Parent(), &c);
}

TEST(CSharp_QueryContinuationClause, PrecedingQuerySetterReparentsAndDetaches) {
    auto a = std::make_unique<QueryExpression>();
    auto b = std::make_unique<QueryExpression>();
    QueryContinuationClause c(a.get(), std::string("x"));
    c.PrecedingQuery(b.get());
    EXPECT_EQ(c.PrecedingQuery(), b.get());
    EXPECT_EQ(b->Parent(), &c);
    EXPECT_EQ(a->Parent(), nullptr);
}

TEST(CSharp_QueryContinuationClause, IdentifierSetterCreatesTokenForEmpty) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    c.Identifier(std::string(""));
    EXPECT_NE(c.IdentifierToken(), nullptr);
    EXPECT_EQ(c.Identifier(), "");
}

TEST(CSharp_QueryContinuationClause, SlotStorageContract) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    EXPECT_EQ(c.GetChildCount(), 2);
    EXPECT_EQ(c.GetChild(0), qexpr.get());
    EXPECT_EQ(c.GetChild(1), c.IdentifierToken());
    EXPECT_EQ(c.GetChildSlotInfo(0), &QueryContinuationClause::PrecedingQuerySlot);
    EXPECT_EQ(c.GetChildSlotInfo(1), &QueryContinuationClause::IdentifierTokenSlot);
}

TEST(CSharp_QueryContinuationClause, SlotStaticsPointAtKinds) {
    EXPECT_EQ(QueryContinuationClause::PrecedingQuerySlot.Kind(), &Slots::PrecedingQuery);
    EXPECT_EQ(QueryContinuationClause::IdentifierTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_FALSE(QueryContinuationClause::PrecedingQuerySlot.IsCollection());
    EXPECT_FALSE(QueryContinuationClause::PrecedingQuerySlot.IsOptional());
    EXPECT_FALSE(QueryContinuationClause::IdentifierTokenSlot.IsOptional());
}

TEST(CSharp_QueryContinuationClause, PrecedingQueryKindIsCycleBroken) {
    // Slots::PrecedingQuery is cycle-broken into QueryExpression.hpp (not in Slots.hpp); verify
    // its address is stable and distinct from the Identifier kind.
    EXPECT_NE(&Slots::PrecedingQuery, nullptr);
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::PrecedingQuery),
              static_cast<const CSharpSlotInfo*>(&Slots::Identifier));
}

TEST(CSharp_QueryContinuationClause, AcceptVisitorDispatchAndVirtuality) {
    auto bare = std::make_unique<QueryContinuationClause>();
    AstNode* asNode = bare.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"cont"}));
}

TEST(CSharp_QueryContinuationClause, DepthFirstWalkVisitsPrecedingQueryThenToken) {
    auto c = std::make_unique<QueryContinuationClause>();
    auto qexpr = std::make_unique<QueryExpression>();
    c->PrecedingQuery(qexpr.get());
    c->Identifier(std::string("x"));
    RecordingVisitor v;
    c->AcceptVisitor(v);
    // PrecedingQuery (index 0, an empty QueryExpression -> just "qexpr"), then the backing
    // IdentifierToken (index 1).
    EXPECT_EQ(v.trace, std::vector<std::string>({"cont", "qexpr", "id:x"}));
}

TEST(CSharp_QueryContinuationClause, DoMatchSame) {
    auto qa = std::make_unique<QueryExpression>();
    auto qb = std::make_unique<QueryExpression>();
    QueryContinuationClause p(qa.get(), std::string("x"));
    QueryContinuationClause c(qb.get(), std::string("x"));
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryContinuationClause, DoMatchRejectsDifferentPrecedingQueryClausesCount) {
    auto qa = std::make_unique<QueryExpression>();
    auto qb = std::make_unique<QueryExpression>();
    // Give the candidate's PrecedingQuery a clause so the collection-DoMatch rejects.
    auto clause = std::make_unique<QueryWhereClause>(
        std::make_unique<NullReferenceExpression>().get());
    qb->Clauses().Add(clause.get());
    QueryContinuationClause p(qa.get(), std::string("x"));
    QueryContinuationClause c(qb.get(), std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryContinuationClause, DoMatchRejectsDifferentIdentifier) {
    auto qa = std::make_unique<QueryExpression>();
    auto qb = std::make_unique<QueryExpression>();
    QueryContinuationClause p(qa.get(), std::string("x"));
    QueryContinuationClause c(qb.get(), std::string("y"));
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryContinuationClause, DoMatchAcceptsAnyStringWildcardOnIdentifier) {
    auto qa = std::make_unique<QueryExpression>();
    auto qb = std::make_unique<QueryExpression>();
    QueryContinuationClause p(qa.get(), std::string(PatternMatching::Pattern::AnyString));
    QueryContinuationClause c(qb.get(), std::string("anything"));
    EXPECT_TRUE(DoMatchAgainst(&p, &c));
}

TEST(CSharp_QueryContinuationClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto qa = std::make_unique<QueryExpression>();
    auto eb = std::make_unique<NullReferenceExpression>();
    QueryContinuationClause p(qa.get(), std::string("x"));
    QueryWhereClause c(eb.get());
    EXPECT_FALSE(DoMatchAgainst(&p, &c));
    EXPECT_FALSE(DoMatchAgainst(&c, &p));
}

TEST(CSharp_QueryContinuationClause, DoMatchRejectsNonQueryContinuationClause) {
    auto qa = std::make_unique<QueryExpression>();
    QueryContinuationClause p(qa.get(), std::string("x"));
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(&p, static_cast<AstNode*>(w.get())));
}

TEST(CSharp_QueryContinuationClause, DoMatchRejectsNullCandidate) {
    auto qa = std::make_unique<QueryExpression>();
    QueryContinuationClause p(qa.get(), std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&p, nullptr));
}

TEST(CSharp_QueryContinuationClause, CloneDeepCopiesSlots) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    std::unique_ptr<QueryContinuationClause> copy(c.Clone());
    EXPECT_EQ(copy->Identifier(), "x");
    EXPECT_NE(copy->IdentifierToken(), c.IdentifierToken());
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());
    ASSERT_NE(copy->PrecedingQuery(), nullptr);
    EXPECT_NE(copy->PrecedingQuery(), qexpr.get());
    EXPECT_EQ(copy->PrecedingQuery()->Parent(), copy.get());
    EXPECT_EQ(qexpr->Parent(), &c);  // original not detached
}

TEST(CSharp_QueryContinuationClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    QueryClause* asClause = &c;
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryContinuationClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryContinuationClause, CheckInvariantFilledPasses) {
    auto qexpr = std::make_unique<QueryExpression>();
    QueryContinuationClause c(qexpr.get(), std::string("x"));
    c.CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_QueryContinuationClause, CheckInvariantEmptyRejects) {
    QueryContinuationClause c;
    EXPECT_DEATH(c.CheckInvariant(), "");
}
#endif
