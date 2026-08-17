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

// Tests for the `UsingStatement` concrete node -- the next in-order Phase-5 piece per the D261
// plan. `UsingStatement` is the `WhileStatement` D258 two-required-single-slot shape (a REQUIRED
// `AstNode` `ResourceAcquisition` at index 0 -- typed the abstract `AstNode` base because the
// `using (...)` production takes EITHER a local-variable-declaration OR an expression -- plus a
// REQUIRED `Statement` `EmbeddedStatement` body at index 1) with two bool scalars
// (`IsAsync`/`IsEnhanced`) and the `UsingKeyword`/`AwaitKeyword` const strings. It adds the new
// `Slots::ResourceAcquisition` kind (a `CSharpSlotInfoT<AstNode>`, the first ported slot kind
// whose element type is the abstract `AstNode` base) and reuses the already-ported
// `Slots::EmbeddedStatement` (by `WhileStatement`); NO name-shadowing crux (no member is named
// `AstNode`/`Statement`). The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper
// (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `VisitUsingStatement` under test (plus the leaf
// expression and leaf statements it holds, and the structural-twin `WhileStatement`), recording a
// tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitUsingStatement(UsingStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("using");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("while");
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
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ==========================================================================
// UsingStatement (ResourceAcquisition AstNode + EmbeddedStatement Statement,
//                  both required; IsAsync/IsEnhanced bool scalars)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_UsingStatement, IsStatementAndAstNodeNotExpression) {
    UsingStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_UsingStatement, UsingKeywordConst) {
    EXPECT_STREQ(UsingStatement::UsingKeyword, "using");
}

TEST(CSharp_UsingStatement, AwaitKeywordConst) {
    // `UsingStatement.AwaitKeyword` aliases `UnaryOperatorExpression.AwaitKeyword` (the canonical
    // `await` literal); both are the same compile-time constant "await".
    EXPECT_STREQ(UsingStatement::AwaitKeyword, "await");
    EXPECT_STREQ(UsingStatement::AwaitKeyword, UnaryOperatorExpression::AwaitKeyword);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_UsingStatement, DefaultCtor) {
    UsingStatement s;
    EXPECT_EQ(s.ResourceAcquisition(), nullptr);
    EXPECT_EQ(s.EmbeddedStatement(), nullptr);
    EXPECT_FALSE(s.IsAsync());
    EXPECT_FALSE(s.IsEnhanced());
    EXPECT_EQ(s.GetChildCount(), 2);
}

TEST(CSharp_UsingStatement, AllParamsCtorSetsAndParents) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    EXPECT_EQ(s.ResourceAcquisition(), resource.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(resource->Parent(), &s);
    EXPECT_EQ(resource->ChildIndex, 0);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

// ---- bool scalars --------------------------------------------------------

TEST(CSharp_UsingStatement, IsAsyncAccessor) {
    UsingStatement s;
    EXPECT_FALSE(s.IsAsync());
    s.IsAsync(true);
    EXPECT_TRUE(s.IsAsync());
    s.IsAsync(false);
    EXPECT_FALSE(s.IsAsync());
}

TEST(CSharp_UsingStatement, IsEnhancedAccessor) {
    UsingStatement s;
    EXPECT_FALSE(s.IsEnhanced());
    s.IsEnhanced(true);
    EXPECT_TRUE(s.IsEnhanced());
    s.IsEnhanced(false);
    EXPECT_FALSE(s.IsEnhanced());
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_UsingStatement, ResourceAcquisitionSetterFillsAndParents) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    s.ResourceAcquisition(resource.get());
    EXPECT_EQ(s.ResourceAcquisition(), resource.get());
    EXPECT_EQ(resource->Parent(), &s);
    EXPECT_EQ(resource->ChildIndex, 0);
}

TEST(CSharp_UsingStatement, EmbeddedStatementSetterFillsAndParents) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    s.ResourceAcquisition(resource.get());
    auto body = std::make_unique<ContinueStatement>();
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_EQ(body->ChildIndex, 1);
}

TEST(CSharp_UsingStatement, NullResourceAcquisitionSetterClearsSlot) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    s.ResourceAcquisition(resource.get());
    s.ResourceAcquisition(nullptr);
    EXPECT_EQ(s.ResourceAcquisition(), nullptr);
    EXPECT_EQ(resource->Parent(), nullptr);
    EXPECT_EQ(resource->ChildIndex, -1);
}

TEST(CSharp_UsingStatement, GetChildReturnsSlots) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.ResourceAcquisition(resource.get());
    s.EmbeddedStatement(body.get());
    EXPECT_EQ(s.GetChild(0), resource.get());
    EXPECT_EQ(s.GetChild(1), body.get());
    EXPECT_THROW(s.GetChild(2), std::out_of_range);
}

TEST(CSharp_UsingStatement, SetChildRoutesToSlots) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.SetChild(0, resource.get());
    s.SetChild(1, body.get());
    EXPECT_EQ(s.ResourceAcquisition(), resource.get());
    EXPECT_EQ(s.EmbeddedStatement(), body.get());
    EXPECT_EQ(resource->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
    EXPECT_THROW(s.SetChild(2, nullptr), std::out_of_range);
}

TEST(CSharp_UsingStatement, GetChildSlotInfoReturnsSlotStatics) {
    UsingStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &UsingStatement::ResourceAcquisitionSlot);
    EXPECT_EQ(s.GetChildSlotInfo(1), &UsingStatement::EmbeddedStatementSlot);
    EXPECT_THROW(s.GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_UsingStatement, SlotKindPointsAtSharedSlotsConstant) {
    UsingStatement s;
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    s.ResourceAcquisition(resource.get());
    s.EmbeddedStatement(body.get());
    ASSERT_EQ(resource->Slot(), &UsingStatement::ResourceAcquisitionSlot);
    EXPECT_EQ(resource->Slot()->Kind(), &Slots::ResourceAcquisition);
    ASSERT_EQ(body->Slot(), &UsingStatement::EmbeddedStatementSlot);
    EXPECT_EQ(body->Slot()->Kind(), &Slots::EmbeddedStatement);
}

TEST(CSharp_UsingStatement, SlotIsInstanceOfCorrectType) {
    EXPECT_FALSE(UsingStatement::ResourceAcquisitionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    // `ResourceAcquisition` is typed the abstract `AstNode` base, so `IsInstanceOfType` (a
    // `dynamic_cast<const AstNode*>`) accepts ANY `AstNode`-derived node -- both an `Expression`
    // (the expression form of the production) and a `Statement` (the
    // local-variable-declaration form, once that node lands).
    NullReferenceExpression nre;
    EXPECT_TRUE(UsingStatement::ResourceAcquisitionSlot.IsInstanceOfType(&nre));
    ContinueStatement cs;
    EXPECT_TRUE(UsingStatement::ResourceAcquisitionSlot.IsInstanceOfType(&cs));
    EXPECT_FALSE(UsingStatement::EmbeddedStatementSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(UsingStatement::EmbeddedStatementSlot.IsInstanceOfType(&cs));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_UsingStatement, AcceptVisitorDispatchesToVisit) {
    UsingStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "using");
}

TEST(CSharp_UsingStatement, AcceptVisitorIsVirtual) {
    UsingStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "using");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "using");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_UsingStatement, DepthFirstWalksChildrenInOrder) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "using");
    EXPECT_EQ(v.trace[1], "null");
    EXPECT_EQ(v.trace[2], "continue");
}

TEST(CSharp_UsingStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    UsingStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "using");
}

// ---- DoMatch (the generated bool x2 + MatchRequired x2 match) -----------

TEST(CSharp_UsingStatement, DoMatchMatchesSameValues) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchDifferentIsAsyncRejects) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    a.IsAsync(true);
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchDifferentIsEnhancedRejects) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    a.IsEnhanced(true);
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchIsAsyncWildcardMatchesWhenBothSet) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    a.IsAsync(true);
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    b.IsAsync(true);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchDifferentResourceAcquisitionRejects) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    auto bRes = std::make_unique<ThisReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchDifferentEmbeddedStatementRejects) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<BreakStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchNullResourceAcquisitionRejects) {
    UsingStatement a(nullptr, nullptr);
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchRejectsWhileStatementStructuralTwin) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    WhileStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchRejectsWhileStatementStructuralTwinReverse) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    WhileStatement a(aRes.get(), aBody.get());
    auto bRes = std::make_unique<NullReferenceExpression>();
    auto bBody = std::make_unique<ContinueStatement>();
    UsingStatement b(bRes.get(), bBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_UsingStatement, DoMatchRejectsDifferentType) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_UsingStatement, DoMatchRejectsNullCandidate) {
    auto aRes = std::make_unique<NullReferenceExpression>();
    auto aBody = std::make_unique<ContinueStatement>();
    UsingStatement a(aRes.get(), aBody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_UsingStatement, CloneDeepCopiesChildrenAndScalars) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    s.IsAsync(true);
    s.IsEnhanced(true);
    std::unique_ptr<UsingStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_TRUE(copy->IsAsync());
    EXPECT_TRUE(copy->IsEnhanced());
    ASSERT_NE(copy->ResourceAcquisition(), nullptr);
    EXPECT_NE(copy->ResourceAcquisition(), resource.get());
    EXPECT_EQ(copy->ResourceAcquisition()->Parent(), copy.get());
    EXPECT_EQ(copy->ResourceAcquisition()->ChildIndex, 0);
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
    EXPECT_EQ(copy->EmbeddedStatement()->ChildIndex, 1);
}

TEST(CSharp_UsingStatement, CloneEmptyNodeHasNoChildren) {
    UsingStatement s;
    std::unique_ptr<UsingStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->ResourceAcquisition(), nullptr);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
    EXPECT_FALSE(copy->IsAsync());
    EXPECT_FALSE(copy->IsEnhanced());
}

TEST(CSharp_UsingStatement, CloneIsVirtualAndCovariant) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<UsingStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<UsingStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_UsingStatement, CloneDoesNotDetachSource) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    std::unique_ptr<UsingStatement> copy(s.Clone());
    EXPECT_EQ(resource->Parent(), &s);
    EXPECT_EQ(body->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_UsingStatement, CheckInvariantPassesOnFilledNode) {
    auto resource = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<ContinueStatement>();
    UsingStatement s(resource.get(), body.get());
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_UsingStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* r = &UsingStatement::ResourceAcquisitionSlot;
    const CSharpSlotInfo* e = &UsingStatement::EmbeddedStatementSlot;
    EXPECT_NE(r, e);
    EXPECT_EQ(r->Kind(), &Slots::ResourceAcquisition);
    EXPECT_EQ(e->Kind(), &Slots::EmbeddedStatement);
    EXPECT_FALSE(r->IsOptional());
    EXPECT_FALSE(e->IsOptional());
}

TEST(CSharp_UsingStatement, EmbeddedStatementKindIsSharedWithWhile) {
    // `UsingStatement` and `WhileStatement` share the `Slots::EmbeddedStatement` kind.
    EXPECT_EQ(UsingStatement::EmbeddedStatementSlot.Kind(),
              WhileStatement::EmbeddedStatementSlot.Kind());
}

TEST(CSharp_UsingStatement, ResourceAcquisitionKindIsDistinct) {
    // `Slots::ResourceAcquisition` is a distinct kind (the first ported slot kind whose element
    // type is the abstract `AstNode` base), distinct from every other ported kind. The `Slots`
    // constants are unrelated `CSharpSlotInfoT<T>` pointer types (distinct element types), so
    // compare through the common `CSharpSlotInfo*` base (the D251/D252 precedent).
    const CSharpSlotInfo* r = &Slots::ResourceAcquisition;
    EXPECT_NE(r, static_cast<const CSharpSlotInfo*>(&Slots::EmbeddedStatement));
    EXPECT_NE(r, static_cast<const CSharpSlotInfo*>(&Slots::Expression));
    EXPECT_NE(r, static_cast<const CSharpSlotInfo*>(&Slots::Statement));
}
