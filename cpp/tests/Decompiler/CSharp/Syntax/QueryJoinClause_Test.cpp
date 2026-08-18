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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `QueryJoinClause` -- the complex remaining query clause (the third and final slice
// of the port of ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs).
// `join_clause ::=
//       'join' type? identifier 'in' expression 'on' expression 'equals' expression
//     | 'join' type? identifier 'in' expression 'on' expression 'equals' expression 'into' identifier`
// (C# grammar 12.23.1). The first slice (QueryClause/QueryOrdering/QueryExpression +
// QueryWhereClause/QuerySelectClause/QueryOrderClause) is in QueryExpression_Test.cpp (D310);
// the second slice (QueryLetClause/QueryGroupClause/QueryFromClause/QueryContinuationClause) is
// in QueryClauses_Test.cpp (D311).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
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
#include "Decompiler/CSharp/Syntax/QueryJoinClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the concrete node under test (plus the leaf
// `Expression`s, the `Identifier` token, the `PrimitiveType` used as an `AstType` operand, the
// `QueryWhereClause` used for the cross-sibling DoMatch rejection, and the `WhileStatement`
// used for the cross-type DoMatch rejection), recording a tag and recursing via `VisitChildren`
// (the inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitQueryJoinClause(QueryJoinClause* node) override {
        if (node == nullptr) { trace.push_back("<null-join>"); return; }
        trace.push_back("join");
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

// A holder keeping all the slot children alive in the test scope. The port's non-owning
// raw-pointer child model means a child's raw pointer dangles if the owning `unique_ptr` dies,
// so a multi-level tree must keep every node alive in the test scope (the D241 holder pattern).
// `JoinIdentifier`/`IntoIdentifier` are set via the string setters (which create their own
// tokens), so they need no holder member.
struct JoinHolder {
    std::unique_ptr<PrimitiveType> type;
    std::unique_ptr<Expression> inExpression;
    std::unique_ptr<Expression> onExpression;
    std::unique_ptr<Expression> equalsExpression;
    std::unique_ptr<QueryJoinClause> node;
    QueryJoinClause* operator->() { return node.get(); }
};

// Builds a join with all slots filled: Type=PrimitiveType("int"), JoinIdentifier="x",
// InExpression=NullReferenceExpression, OnExpression=ThisReferenceExpression,
// EqualsExpression=PrimitiveExpression(0), IntoIdentifier="g".
JoinHolder makeJoinWithAllSlots() {
    JoinHolder h;
    h.type = std::make_unique<PrimitiveType>(std::string("int"));
    h.inExpression = std::make_unique<NullReferenceExpression>();
    h.onExpression = std::make_unique<ThisReferenceExpression>();
    h.equalsExpression = std::make_unique<PrimitiveExpression>(int32_t(0));
    h.node = std::make_unique<QueryJoinClause>(
        h.type.get(), std::string("x"), h.inExpression.get(),
        h.onExpression.get(), h.equalsExpression.get(), std::string("g"));
    return h;
}

// Builds a join with the minimal required slots and both nullable slots absent: null Type,
// JoinIdentifier="x", the three required Expressions, empty IntoIdentifier (null token).
JoinHolder makeJoinMinimal() {
    JoinHolder h;
    h.inExpression = std::make_unique<NullReferenceExpression>();
    h.onExpression = std::make_unique<ThisReferenceExpression>();
    h.equalsExpression = std::make_unique<PrimitiveExpression>(int32_t(0));
    h.node = std::make_unique<QueryJoinClause>(
        nullptr, std::string("x"), h.inExpression.get(),
        h.onExpression.get(), h.equalsExpression.get(), std::string(""));
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// CSharp_QueryJoinClause -- the join clause
// ---------------------------------------------------------------------------
TEST(CSharp_QueryJoinClause, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<QueryJoinClause>);
    EXPECT_TRUE(std::is_final_v<QueryJoinClause>);
}

TEST(CSharp_QueryJoinClause, IsQueryClauseAndAstNodeButNotExpression) {
    QueryJoinClause j;
    EXPECT_NE(dynamic_cast<QueryClause*>(&j), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&j), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&j), nullptr);
}

TEST(CSharp_QueryJoinClause, ConstKeywords) {
    EXPECT_STREQ(QueryJoinClause::JoinKeyword, "join");
    EXPECT_STREQ(QueryJoinClause::InKeyword, "in");
    EXPECT_STREQ(QueryJoinClause::OnKeyword, "on");
    EXPECT_STREQ(QueryJoinClause::EqualsKeyword, "equals");
    EXPECT_STREQ(QueryJoinClause::IntoKeyword, "into");
}

TEST(CSharp_QueryJoinClause, DefaultCtorSlotsAreNull) {
    QueryJoinClause j;
    EXPECT_EQ(j.Type(), nullptr);
    EXPECT_EQ(j.JoinIdentifierToken(), nullptr);
    EXPECT_EQ(j.InExpression(), nullptr);
    EXPECT_EQ(j.OnExpression(), nullptr);
    EXPECT_EQ(j.EqualsExpression(), nullptr);
    EXPECT_EQ(j.IntoIdentifierToken(), nullptr);
    EXPECT_EQ(j.GetChildCount(), 6);
    EXPECT_FALSE(j.IsGroupJoin());  // no IntoIdentifier -> not a group join
}

TEST(CSharp_QueryJoinClause, IsGroupJoinFalseByDefaultAndOnEmptyIntoIdentifier) {
    JoinHolder h = makeJoinMinimal();  // empty IntoIdentifier -> null token
    EXPECT_FALSE(h->IsGroupJoin());
    EXPECT_FALSE(h->IntoIdentifier().has_value());  // no token
}

TEST(CSharp_QueryJoinClause, IsGroupJoinTrueWhenIntoIdentifierNonEmpty) {
    JoinHolder h = makeJoinWithAllSlots();  // IntoIdentifier="g" -> group join
    EXPECT_TRUE(h->IsGroupJoin());
    ASSERT_TRUE(h->IntoIdentifier().has_value());
    EXPECT_EQ(*h->IntoIdentifier(), "g");
}

TEST(CSharp_QueryJoinClause, AllParamsCtorWithNullTypeAndNullIntoSetsSlots) {
    JoinHolder h = makeJoinMinimal();
    EXPECT_EQ(h->Type(), nullptr);
    EXPECT_EQ(h->JoinIdentifier(), "x");
    EXPECT_NE(h->JoinIdentifierToken(), nullptr);
    EXPECT_EQ(h->InExpression(), h.inExpression.get());
    EXPECT_EQ(h->OnExpression(), h.onExpression.get());
    EXPECT_EQ(h->EqualsExpression(), h.equalsExpression.get());
    EXPECT_EQ(h->IntoIdentifierToken(), nullptr);  // empty IntoIdentifier -> null token
    EXPECT_EQ(h.inExpression->Parent(), h.node.get());
    EXPECT_EQ(h.onExpression->Parent(), h.node.get());
    EXPECT_EQ(h.equalsExpression->Parent(), h.node.get());
    EXPECT_EQ(h->JoinIdentifierToken()->Parent(), h.node.get());
    EXPECT_FALSE(h->IsGroupJoin());
}

TEST(CSharp_QueryJoinClause, AllParamsCtorWithAllSlots) {
    JoinHolder h = makeJoinWithAllSlots();
    EXPECT_EQ(h->Type(), h.type.get());
    EXPECT_EQ(h->JoinIdentifier(), "x");
    EXPECT_EQ(h->InExpression(), h.inExpression.get());
    EXPECT_EQ(h->OnExpression(), h.onExpression.get());
    EXPECT_EQ(h->EqualsExpression(), h.equalsExpression.get());
    ASSERT_TRUE(h->IntoIdentifier().has_value());
    EXPECT_EQ(*h->IntoIdentifier(), "g");
    EXPECT_EQ(h.type->Parent(), h.node.get());
    EXPECT_TRUE(h->IsGroupJoin());
}

TEST(CSharp_QueryJoinClause, TypeSetterReparentsAndDetaches) {
    auto a = std::make_unique<PrimitiveType>(std::string("int"));
    auto b = std::make_unique<PrimitiveType>(std::string("long"));
    JoinHolder h = makeJoinMinimal();
    h->Type(a.get());
    EXPECT_EQ(h->Type(), a.get());
    EXPECT_EQ(a->Parent(), h.node.get());
    h->Type(b.get());
    EXPECT_EQ(h->Type(), b.get());
    EXPECT_EQ(b->Parent(), h.node.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

TEST(CSharp_QueryJoinClause, JoinIdentifierSetterCreatesTokenForEmpty) {
    QueryJoinClause j;
    j.JoinIdentifier(std::string(""));
    EXPECT_NE(j.JoinIdentifierToken(), nullptr);  // empty name yields a token with empty Name
    EXPECT_EQ(j.JoinIdentifier(), "");
    EXPECT_EQ(j.JoinIdentifierToken()->Parent(), &j);
}

TEST(CSharp_QueryJoinClause, InExpressionSetterReparentsAndDetaches) {
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ThisReferenceExpression>();
    JoinHolder h = makeJoinMinimal();
    h->InExpression(b.get());
    EXPECT_EQ(h->InExpression(), b.get());
    EXPECT_EQ(b->Parent(), h.node.get());
    EXPECT_EQ(a->Parent(), nullptr);  // the original inExpression detached
}

TEST(CSharp_QueryJoinClause, IntoIdentifierSetterCreatesAndClearsToken) {
    QueryJoinClause j;
    EXPECT_EQ(j.IntoIdentifierToken(), nullptr);
    EXPECT_FALSE(j.IntoIdentifier().has_value());
    // A non-empty name creates a token (the nullable string? -- CreateIfNotEmpty).
    j.IntoIdentifier(std::string("g"));
    EXPECT_NE(j.IntoIdentifierToken(), nullptr);
    ASSERT_TRUE(j.IntoIdentifier().has_value());
    EXPECT_EQ(*j.IntoIdentifier(), "g");
    EXPECT_TRUE(j.IsGroupJoin());
    // An empty name clears the token (faithful to the string? optionality).
    j.IntoIdentifier(std::string(""));
    EXPECT_EQ(j.IntoIdentifierToken(), nullptr);
    EXPECT_FALSE(j.IntoIdentifier().has_value());
    EXPECT_FALSE(j.IsGroupJoin());
}

TEST(CSharp_QueryJoinClause, IntoIdentifierTokenSetterReparents) {
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("g")));
    QueryJoinClause j;
    j.IntoIdentifierToken(tok.get());
    EXPECT_EQ(j.IntoIdentifierToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &j);
}

TEST(CSharp_QueryJoinClause, SlotStorageContract) {
    JoinHolder h = makeJoinWithAllSlots();
    EXPECT_EQ(h->GetChildCount(), 6);
    EXPECT_EQ(h->GetChild(0), h.type.get());
    EXPECT_EQ(h->GetChild(1), h->JoinIdentifierToken());
    EXPECT_EQ(h->GetChild(2), h.inExpression.get());
    EXPECT_EQ(h->GetChild(3), h.onExpression.get());
    EXPECT_EQ(h->GetChild(4), h.equalsExpression.get());
    EXPECT_EQ(h->GetChild(5), h->IntoIdentifierToken());
    EXPECT_EQ(h->GetChildSlotInfo(0), &QueryJoinClause::TypeSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &QueryJoinClause::JoinIdentifierTokenSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &QueryJoinClause::InExpressionSlot);
    EXPECT_EQ(h->GetChildSlotInfo(3), &QueryJoinClause::OnExpressionSlot);
    EXPECT_EQ(h->GetChildSlotInfo(4), &QueryJoinClause::EqualsExpressionSlot);
    EXPECT_EQ(h->GetChildSlotInfo(5), &QueryJoinClause::IntoIdentifierTokenSlot);
    EXPECT_THROW(h->GetChild(6), std::out_of_range);
    EXPECT_THROW(h->GetChildSlotInfo(6), std::out_of_range);
}

TEST(CSharp_QueryJoinClause, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(QueryJoinClause::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(QueryJoinClause::JoinIdentifierTokenSlot.Kind(), &Slots::JoinIdentifier);
    EXPECT_EQ(QueryJoinClause::InExpressionSlot.Kind(), &Slots::InExpression);
    EXPECT_EQ(QueryJoinClause::OnExpressionSlot.Kind(), &Slots::OnExpression);
    EXPECT_EQ(QueryJoinClause::EqualsExpressionSlot.Kind(), &Slots::EqualsExpression);
    EXPECT_EQ(QueryJoinClause::IntoIdentifierTokenSlot.Kind(), &Slots::IntoIdentifier);
    EXPECT_TRUE(QueryJoinClause::TypeSlot.IsOptional());             // Type is nullable
    EXPECT_FALSE(QueryJoinClause::JoinIdentifierTokenSlot.IsOptional());   // join name required
    EXPECT_FALSE(QueryJoinClause::InExpressionSlot.IsOptional());         // required
    EXPECT_FALSE(QueryJoinClause::OnExpressionSlot.IsOptional());         // required
    EXPECT_FALSE(QueryJoinClause::EqualsExpressionSlot.IsOptional());    // required
    EXPECT_TRUE(QueryJoinClause::IntoIdentifierTokenSlot.IsOptional());  // into name nullable
}

TEST(CSharp_QueryJoinClause, SlotStaticsAreDistinct) {
    // The five new Expression/Identifier kinds are distinct from each other and from the
    // reused Slots::Type/Slots::Expression. Comparing distinct-element-type CSharpSlotInfoT<T>
    // instantiations directly fails (gtest CmpHelperNE needs comparable pointer types), so cast
    // to the common const CSharpSlotInfo* base (the D222 precedent).
    const CSharpSlotInfo* type = &Slots::Type;
    const CSharpSlotInfo* jid = &Slots::JoinIdentifier;
    const CSharpSlotInfo* inExpr = &Slots::InExpression;
    const CSharpSlotInfo* onExpr = &Slots::OnExpression;
    const CSharpSlotInfo* eqExpr = &Slots::EqualsExpression;
    const CSharpSlotInfo* intoId = &Slots::IntoIdentifier;
    const CSharpSlotInfo* expr = &Slots::Expression;
    EXPECT_NE(jid, inExpr);
    EXPECT_NE(inExpr, onExpr);
    EXPECT_NE(onExpr, eqExpr);
    EXPECT_NE(eqExpr, intoId);
    EXPECT_NE(jid, intoId);
    EXPECT_NE(inExpr, expr);   // distinct from the reused Slots::Expression kind
    EXPECT_NE(jid, type);      // distinct from the reused Slots::Type kind
}

TEST(CSharp_QueryJoinClause, IsInstanceOfTypeAcceptsRightChildren) {
    // The slot-kind is-a test (the dynamic_cast): TypeSlot/IntoIdentifierTokenSlot accept an
    // Identifier/AstType; the Expression slots accept an Expression; an Identifier is NOT an
    // Expression and an Expression is NOT an Identifier.
    auto id = std::make_unique<Identifier>();
    auto expr = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    EXPECT_TRUE(Slots::JoinIdentifier.IsInstanceOfType(id.get()));
    EXPECT_TRUE(Slots::IntoIdentifier.IsInstanceOfType(id.get()));
    EXPECT_TRUE(Slots::InExpression.IsInstanceOfType(expr.get()));
    EXPECT_TRUE(Slots::Type.IsInstanceOfType(type.get()));
    EXPECT_FALSE(Slots::JoinIdentifier.IsInstanceOfType(expr.get()));   // Expression not Identifier
    EXPECT_FALSE(Slots::InExpression.IsInstanceOfType(id.get()));       // Identifier not Expression
    EXPECT_FALSE(Slots::JoinIdentifier.IsInstanceOfType(nullptr));     // null rejects
}

TEST(CSharp_QueryJoinClause, AcceptVisitorDispatchAndVirtuality) {
    auto bare = std::make_unique<QueryJoinClause>();
    AstNode* asNode = bare.get();
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"join"}));
}

TEST(CSharp_QueryJoinClause, AcceptVisitorVirtualThroughQueryClause) {
    auto bare = std::make_unique<QueryJoinClause>();
    QueryClause* asClause = bare.get();
    RecordingVisitor v;
    asClause->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"join"}));
}

TEST(CSharp_QueryJoinClause, DepthFirstWalkWithAllSlots) {
    JoinHolder h = makeJoinWithAllSlots();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // join, Type (primtype leaf), the backing JoinIdentifierToken, InExpression, OnExpression,
    // EqualsExpression, the backing IntoIdentifierToken -- in flattened-index order.
    EXPECT_EQ(v.trace, std::vector<std::string>(
        {"join", "primtype", "id:x", "nullref", "this", "prim", "id:g"}));
}

TEST(CSharp_QueryJoinClause, DepthFirstWalkWithNullTypeAndNullInto) {
    JoinHolder h = makeJoinMinimal();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // Null Type and null IntoIdentifierToken skipped; the backing JoinIdentifierToken then the
    // three required Expressions.
    EXPECT_EQ(v.trace, std::vector<std::string>(
        {"join", "id:x", "nullref", "this", "prim"}));
}

TEST(CSharp_QueryJoinClause, DoMatchSameWithNullTypeAndNullInto) {
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    EXPECT_TRUE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchSameWithAllSlots) {
    JoinHolder p = makeJoinWithAllSlots();
    JoinHolder c = makeJoinWithAllSlots();
    EXPECT_TRUE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsTypeAsymmetry) {
    JoinHolder p = makeJoinWithAllSlots();  // has Type
    JoinHolder c = makeJoinMinimal();       // no Type
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
    EXPECT_FALSE(DoMatchAgainst(c.node.get(), p.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsDifferentJoinIdentifier) {
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    c->JoinIdentifier(std::string("y"));  // different name
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsDifferentInExpression) {
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    auto diff = std::make_unique<ThisReferenceExpression>();  // p's InExpression is NullRef
    c->InExpression(diff.get());
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsDifferentOnExpression) {
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    auto diff = std::make_unique<NullReferenceExpression>();  // p's OnExpression is ThisRef
    c->OnExpression(diff.get());
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsDifferentEqualsExpression) {
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    auto diff = std::make_unique<NullReferenceExpression>();  // p's EqualsExpression is Primitive
    c->EqualsExpression(diff.get());
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsIntoIdentifierAsymmetry) {
    JoinHolder p = makeJoinWithAllSlots();  // has IntoIdentifier="g"
    JoinHolder c = makeJoinMinimal();        // no IntoIdentifier
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
    EXPECT_FALSE(DoMatchAgainst(c.node.get(), p.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsDifferentIntoIdentifier) {
    JoinHolder p = makeJoinWithAllSlots();  // IntoIdentifier="g"
    JoinHolder c = makeJoinWithAllSlots();
    c->IntoIdentifier(std::string("h"));  // different group name
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchAcceptsAnyStringWildcardOnJoinIdentifier) {
    JoinHolder p = makeJoinWithAllSlots();
    JoinHolder c = makeJoinWithAllSlots();
    p->JoinIdentifier(std::string(PatternMatching::Pattern::AnyString));  // $any$ wildcard
    c->JoinIdentifier(std::string("anything"));
    EXPECT_TRUE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchBothAbsentIntoIdentifierMatch) {
    // Two joins with no IntoIdentifier both pass the nullable MatchString(nullopt, nullopt).
    JoinHolder p = makeJoinMinimal();
    JoinHolder c = makeJoinMinimal();
    // Set a non-empty IntoIdentifier on p, then clear it -> both nullopt.
    p->IntoIdentifier(std::string("g"));
    p->IntoIdentifier(std::string(""));  // clears the token
    EXPECT_FALSE(p->IntoIdentifier().has_value());
    EXPECT_TRUE(DoMatchAgainst(p.node.get(), c.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsCrossSiblingQueryWhereClause) {
    auto whereExpr = std::make_unique<NullReferenceExpression>();
    QueryWhereClause w(whereExpr.get());
    JoinHolder p = makeJoinMinimal();
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), &w));
    EXPECT_FALSE(DoMatchAgainst(&w, p.node.get()));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsNonQueryJoinClause) {
    JoinHolder p = makeJoinMinimal();
    auto whileStmt = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), static_cast<AstNode*>(whileStmt.get())));
}

TEST(CSharp_QueryJoinClause, DoMatchRejectsNullCandidate) {
    JoinHolder p = makeJoinMinimal();
    EXPECT_FALSE(DoMatchAgainst(p.node.get(), nullptr));
}

TEST(CSharp_QueryJoinClause, CloneDeepCopiesSlots) {
    JoinHolder h = makeJoinWithAllSlots();
    std::unique_ptr<QueryJoinClause> copy(h->Clone());
    EXPECT_EQ(copy->JoinIdentifier(), "x");
    EXPECT_NE(copy->JoinIdentifierToken(), h->JoinIdentifierToken());
    EXPECT_EQ(copy->JoinIdentifierToken()->Parent(), copy.get());
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), h.type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_NE(copy->InExpression(), h.inExpression.get());
    EXPECT_EQ(copy->InExpression()->Parent(), copy.get());
    EXPECT_NE(copy->OnExpression(), h.onExpression.get());
    EXPECT_EQ(copy->OnExpression()->Parent(), copy.get());
    EXPECT_NE(copy->EqualsExpression(), h.equalsExpression.get());
    EXPECT_EQ(copy->EqualsExpression()->Parent(), copy.get());
    ASSERT_TRUE(copy->IntoIdentifier().has_value());
    EXPECT_EQ(*copy->IntoIdentifier(), "g");
    EXPECT_NE(copy->IntoIdentifierToken(), h->IntoIdentifierToken());
    EXPECT_EQ(copy->IntoIdentifierToken()->Parent(), copy.get());
    // The originals are still parented to the source node.
    EXPECT_EQ(h.type->Parent(), h.node.get());
    EXPECT_EQ(h.inExpression->Parent(), h.node.get());
    EXPECT_TRUE(copy->IsGroupJoin());
}

TEST(CSharp_QueryJoinClause, CloneSkipsNullTypeAndNullIntoIdentifier) {
    JoinHolder h = makeJoinMinimal();
    std::unique_ptr<QueryJoinClause> copy(h->Clone());
    EXPECT_EQ(copy->Type(), nullptr);  // null Type skipped
    EXPECT_EQ(copy->IntoIdentifierToken(), nullptr);  // null IntoIdentifier skipped
    EXPECT_FALSE(copy->IntoIdentifier().has_value());
    EXPECT_FALSE(copy->IsGroupJoin());
    EXPECT_EQ(copy->JoinIdentifier(), "x");
    EXPECT_NE(copy->InExpression(), nullptr);
    EXPECT_NE(copy->OnExpression(), nullptr);
    EXPECT_NE(copy->EqualsExpression(), nullptr);
}

TEST(CSharp_QueryJoinClause, CloneIsVirtualAndCovariantThroughQueryClause) {
    JoinHolder h = makeJoinMinimal();
    QueryClause* asClause = h.node.get();
    std::unique_ptr<QueryClause> copy(asClause->Clone());
    EXPECT_NE(dynamic_cast<QueryJoinClause*>(copy.get()), nullptr);
}

TEST(CSharp_QueryJoinClause, CheckInvariantFilledPassesWithNullTypeAndNullInto) {
    JoinHolder h = makeJoinMinimal();  // Type and IntoIdentifier absent; required slots filled
    h->CheckInvariant();  // both nullable slots absent is invariant-valid
    SUCCEED();
}

TEST(CSharp_QueryJoinClause, CheckInvariantFullPasses) {
    JoinHolder h = makeJoinWithAllSlots();
    h->CheckInvariant();
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_QueryJoinClause, CheckInvariantEmptyRejects) {
    QueryJoinClause j;
    EXPECT_DEATH(j.CheckInvariant(), "");  // required JoinIdentifier/In/On/Equals missing
}

TEST(CSharp_QueryJoinClause, CheckInvariantRejectsMissingInExpression) {
    JoinHolder h = makeJoinMinimal();
    h->InExpression(nullptr);  // clear a required slot
    EXPECT_DEATH(h->CheckInvariant(), "");
}
#endif
