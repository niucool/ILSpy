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

// Tests for the goto family of C# AST statement nodes -- `GotoStatement` (a single NULLABLE
// `string?` `Label` string-name `[Slot("Identifier")]` over a backing `LabelToken` `Identifier`
// slot, the `SimpleType` D237 string-name-`[Slot]` shape MINUS the `TypeArguments` collection),
// `GotoCaseStatement` (a single REQUIRED `Expression` `LabelExpression` slot, the
// `ExpressionStatement` D255 shape applied to the goto family), and `GotoDefaultStatement` (the
// cleanest leaf -- no `[Slot]` children, no match members) -- the next in-order Phase-5 piece
// per the D256 plan ("the EmptyStatement/GotoStatement/LabelStatement leaves"). All three carry
// `goto`/`case`/`default` keyword const strings; `GotoStatement`/`GotoCaseStatement` reuse the
// already-ported `Slots::Identifier`/`Slots::Expression` kinds with no new `Slots` constant.
// The three suites share a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// multi-suite pattern); each is independently `--gtest_filter`-selectable.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three goto-family `Visit` methods under test
// (plus the `Identifier` token and the leaf expressions the goto family holds), recording a tag
// and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitGotoStatement(GotoStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("goto:" + node->Label().value_or(""));
        VisitChildren(node);
    }
    void VisitGotoCaseStatement(GotoCaseStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("gotocase");
        VisitChildren(node);
    }
    void VisitGotoDefaultStatement(GotoDefaultStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("gotodefault");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
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

} // namespace

// ==========================================================================
// GotoStatement (nullable string-name [Slot] Label/LabelToken + GotoKeyword)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_GotoStatement, IsStatementAndAstNodeNotExpression) {
    GotoStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keyword -------------------------------------------------------

TEST(CSharp_GotoStatement, GotoKeywordConst) {
    EXPECT_STREQ(GotoStatement::GotoKeyword, "goto");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_GotoStatement, DefaultCtor) {
    GotoStatement s;
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_FALSE(s.Label().has_value());
    EXPECT_EQ(s.GetChildCount(), 1);  // the slot counts even when the token is absent
}

TEST(CSharp_GotoStatement, StringCtorCreatesTokenViaCreateIfNotEmpty) {
    GotoStatement s(std::string("target"));
    ASSERT_NE(s.LabelToken(), nullptr);
    ASSERT_TRUE(s.Label().has_value());
    EXPECT_EQ(*s.Label(), "target");
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
    EXPECT_EQ(s.LabelToken()->ChildIndex, 0);
}

TEST(CSharp_GotoStatement, EmptyStringCtorClearsToken) {
    // CreateIfNotEmpty on an empty name yields a null token (the faithful `string?`
    // optionality -- an empty label is an absent label). Braced-init avoids the most-vexing-parse
    // (`GotoStatement s(std::string())` would declare `s` as a function, the D236 precedent).
    GotoStatement s{std::string()};
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_FALSE(s.Label().has_value());
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_GotoStatement, LabelSetterCreatesToken) {
    GotoStatement s;
    s.Label(std::string("loop"));
    ASSERT_NE(s.LabelToken(), nullptr);
    ASSERT_TRUE(s.Label().has_value());
    EXPECT_EQ(*s.Label(), "loop");
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
    EXPECT_EQ(s.LabelToken()->ChildIndex, 0);
}

TEST(CSharp_GotoStatement, EmptyLabelClearsToken) {
    GotoStatement s;
    s.Label(std::string("loop"));
    s.Label(std::string_view());  // empty -> CreateIfNotEmpty -> null
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_FALSE(s.Label().has_value());
}

TEST(CSharp_GotoStatement, LabelTokenSetterFillsAndParents) {
    GotoStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("end"));
    Identifier* tokenPtr = token.get();
    s.LabelToken(token.get());
    EXPECT_EQ(s.LabelToken(), tokenPtr);
    EXPECT_EQ(tokenPtr->Parent(), &s);
    EXPECT_EQ(tokenPtr->ChildIndex, 0);
}

TEST(CSharp_GotoStatement, NullLabelTokenSetterClearsSlot) {
    GotoStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("end"));
    s.LabelToken(token.get());
    s.LabelToken(nullptr);
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_EQ(token->Parent(), nullptr);
    EXPECT_EQ(token->ChildIndex, -1);
}

TEST(CSharp_GotoStatement, GetChildReturnsSlot) {
    GotoStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.LabelToken(token.get());
    EXPECT_EQ(s.GetChild(0), token.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_GotoStatement, SetChildRoutesToSlot) {
    GotoStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.SetChild(0, token.get());
    EXPECT_EQ(s.LabelToken(), token.get());
    EXPECT_EQ(token->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_GotoStatement, GetChildSlotInfoReturnsSlotStatic) {
    GotoStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &GotoStatement::LabelTokenSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_GotoStatement, SlotKindPointsAtSharedSlotsConstant) {
    GotoStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.LabelToken(token.get());
    ASSERT_EQ(token->Slot(), &GotoStatement::LabelTokenSlot);
    EXPECT_EQ(token->Slot()->Kind(), &Slots::Identifier);
}

TEST(CSharp_GotoStatement, SlotIsInstanceOfIdentifier) {
    EXPECT_FALSE(GotoStatement::LabelTokenSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    auto id = std::unique_ptr<Identifier>(Identifier::Create("x"));
    EXPECT_TRUE(GotoStatement::LabelTokenSlot.IsInstanceOfType(id.get()));
    NullReferenceExpression nre;
    EXPECT_FALSE(GotoStatement::LabelTokenSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_GotoStatement, AcceptVisitorDispatchesToVisit) {
    GotoStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "goto:");
}

TEST(CSharp_GotoStatement, AcceptVisitorIsVirtual) {
    GotoStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "goto:");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "goto:");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_GotoStatement, DepthFirstWalksLabelTokenInOrder) {
    GotoStatement s(std::string("target"));
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "goto:target");
    EXPECT_EQ(v.trace[1], "id:target");
}

TEST(CSharp_GotoStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    GotoStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "goto:");
}

// ---- DoMatch (the generated string MatchString match) ------------------

TEST(CSharp_GotoStatement, DoMatchMatchesSameLabel) {
    GotoStatement a(std::string("loop"));
    GotoStatement b(std::string("loop"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchDifferentLabelRejects) {
    GotoStatement a(std::string("loop"));
    GotoStatement b(std::string("end"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchBothAbsentMatches) {
    // Both labels absent (null tokens) match -- two label-less gotos match via MatchString's
    // null==null path.
    GotoStatement a;
    GotoStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchPatternPresentCandidateAbsentRejects) {
    GotoStatement a(std::string("loop"));
    GotoStatement b;  // no label
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchPatternAbsentCandidatePresentRejects) {
    GotoStatement a;  // no label
    GotoStatement b(std::string("end"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchAnyStringWildcardMatchesAny) {
    // A pattern whose `Label` is the `$any$` sentinel matches any candidate label via
    // `Pattern::MatchString`'s wildcard path (`pattern == AnyString` -> true). Build the pattern
    // directly by setting its `LabelToken` `Name` to `$any$` (the `Identifier::Create` factory).
    GotoStatement pat;
    pat.LabelToken(Identifier::Create("$any$"));
    GotoStatement candA(std::string("loop"));
    GotoStatement candB(std::string("end"));
    EXPECT_TRUE(DoMatchAgainst(&pat, &candA));
    EXPECT_TRUE(DoMatchAgainst(&pat, &candB));
}

TEST(CSharp_GotoStatement, DoMatchRejectsGotoCaseStatementSibling) {
    GotoStatement a(std::string("loop"));
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement b(operand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchRejectsGotoDefaultStatementSibling) {
    GotoStatement a(std::string("loop"));
    GotoDefaultStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoStatement, DoMatchRejectsDifferentType) {
    GotoStatement a(std::string("loop"));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_GotoStatement, DoMatchRejectsNullCandidate) {
    GotoStatement a(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_GotoStatement, CloneDeepCopiesToken) {
    GotoStatement s(std::string("target"));
    std::unique_ptr<GotoStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->LabelToken(), nullptr);
    EXPECT_NE(copy->LabelToken(), s.LabelToken());
    ASSERT_TRUE(copy->Label().has_value());
    EXPECT_EQ(*copy->Label(), "target");
    EXPECT_EQ(copy->LabelToken()->Parent(), copy.get());
    EXPECT_EQ(copy->LabelToken()->ChildIndex, 0);
}

TEST(CSharp_GotoStatement, CloneEmptyNodeHasNoToken) {
    GotoStatement s;
    std::unique_ptr<GotoStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->LabelToken(), nullptr);
    EXPECT_FALSE(copy->Label().has_value());
}

TEST(CSharp_GotoStatement, CloneIsVirtualAndCovariant) {
    GotoStatement s(std::string("target"));
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<GotoStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<GotoStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_GotoStatement, CloneDoesNotDetachSource) {
    GotoStatement s(std::string("target"));
    std::unique_ptr<GotoStatement> copy(s.Clone());
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_GotoStatement, CheckInvariantPassesOnFilledNode) {
    GotoStatement s(std::string("target"));
    s.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_GotoStatement, CheckInvariantPassesOnEmptyNode) {
    // The slot is nullable, so an empty node is invariant-valid (unlike a required-slot node).
    GotoStatement s;
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// GotoCaseStatement (required Expression LabelExpression slot + goto/case consts)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_GotoCaseStatement, IsStatementAndAstNodeNotExpression) {
    GotoCaseStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_GotoCaseStatement, GotoAndCaseKeywordConsts) {
    EXPECT_STREQ(GotoCaseStatement::GotoKeyword, "goto");
    EXPECT_STREQ(GotoCaseStatement::CaseKeyword, "case");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_GotoCaseStatement, DefaultCtor) {
    GotoCaseStatement s;
    EXPECT_EQ(s.LabelExpression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_GotoCaseStatement, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    GotoCaseStatement s(operand.get());
    EXPECT_EQ(s.LabelExpression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &s);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_GotoCaseStatement, LabelExpressionSetterFillsAndParents) {
    GotoCaseStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.LabelExpression(a.get());
    EXPECT_EQ(s.LabelExpression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(s.ChildIndicesValid());
}

TEST(CSharp_GotoCaseStatement, NullSetterClearsSlot) {
    GotoCaseStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.LabelExpression(a.get());
    s.LabelExpression(nullptr);
    EXPECT_EQ(s.LabelExpression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

TEST(CSharp_GotoCaseStatement, GetChildReturnsSlot) {
    GotoCaseStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.LabelExpression(operand.get());
    EXPECT_EQ(s.GetChild(0), operand.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_GotoCaseStatement, SetChildRoutesToSlot) {
    GotoCaseStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, operand.get());
    EXPECT_EQ(s.LabelExpression(), operand.get());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_GotoCaseStatement, GetChildSlotInfoReturnsSlotStatic) {
    GotoCaseStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &GotoCaseStatement::LabelExpressionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_GotoCaseStatement, SlotKindPointsAtSharedSlotsConstant) {
    GotoCaseStatement s;
    auto operand = std::make_unique<NullReferenceExpression>();
    s.LabelExpression(operand.get());
    ASSERT_EQ(operand->Slot(), &GotoCaseStatement::LabelExpressionSlot);
    EXPECT_EQ(operand->Slot()->Kind(), &Slots::Expression);
}

TEST(CSharp_GotoCaseStatement, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(GotoCaseStatement::LabelExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    NullReferenceExpression nre;
    EXPECT_TRUE(GotoCaseStatement::LabelExpressionSlot.IsInstanceOfType(&nre));
    auto id = std::unique_ptr<Identifier>(Identifier::Create("x"));
    EXPECT_FALSE(GotoCaseStatement::LabelExpressionSlot.IsInstanceOfType(id.get()));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_GotoCaseStatement, AcceptVisitorDispatchesToVisit) {
    GotoCaseStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotocase");
}

TEST(CSharp_GotoCaseStatement, AcceptVisitorIsVirtual) {
    GotoCaseStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotocase");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "gotocase");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_GotoCaseStatement, DepthFirstWalksOperandInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement s(operand.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "gotocase");
    EXPECT_EQ(v.trace[1], "null");
}

TEST(CSharp_GotoCaseStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    GotoCaseStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotocase");
}

// ---- DoMatch (the generated non-nullable-recursive MatchRequired match) -

TEST(CSharp_GotoCaseStatement, DoMatchMatchesSameValue) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    auto bOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement b(bOperand.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchNullPatternOperandRejects) {
    GotoCaseStatement a(nullptr);
    auto bOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchNullCandidateOperandRejects) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    GotoCaseStatement b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchBothOperandsNullRejects) {
    GotoCaseStatement a(nullptr);
    GotoCaseStatement b(nullptr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchOperandDelegatesToOperandDoMatch) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    auto bOperand = std::make_unique<ThisReferenceExpression>();
    GotoCaseStatement b(bOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchRejectsGotoStatementSibling) {
    GotoStatement a(std::string("loop"));
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement b(operand.get());
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

TEST(CSharp_GotoCaseStatement, DoMatchRejectsGotoDefaultStatementSibling) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    GotoDefaultStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoCaseStatement, DoMatchRejectsDifferentType) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_GotoCaseStatement, DoMatchRejectsNullCandidate) {
    auto aOperand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement a(aOperand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_GotoCaseStatement, CloneDeepCopiesChild) {
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement s(operand.get());
    std::unique_ptr<GotoCaseStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->LabelExpression(), nullptr);
    EXPECT_NE(copy->LabelExpression(), operand.get());
    EXPECT_EQ(copy->LabelExpression()->Parent(), copy.get());
    EXPECT_EQ(copy->LabelExpression()->ChildIndex, 0);
}

TEST(CSharp_GotoCaseStatement, CloneEmptyNodeHasNoChild) {
    GotoCaseStatement s;
    std::unique_ptr<GotoCaseStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->LabelExpression(), nullptr);
}

TEST(CSharp_GotoCaseStatement, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement s(operand.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<GotoCaseStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<GotoCaseStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_GotoCaseStatement, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement s(operand.get());
    std::unique_ptr<GotoCaseStatement> copy(s.Clone());
    EXPECT_EQ(operand->Parent(), &s);
    EXPECT_EQ(s.LabelExpression(), operand.get());
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_GotoCaseStatement, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement s(operand.get());
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// GotoDefaultStatement (the cleanest leaf -- no slots, goto/default consts)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_GotoDefaultStatement, IsStatementAndAstNodeNotExpression) {
    GotoDefaultStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_GotoDefaultStatement, GotoAndDefaultKeywordConsts) {
    EXPECT_STREQ(GotoDefaultStatement::GotoKeyword, "goto");
    EXPECT_STREQ(GotoDefaultStatement::DefaultKeyword, "default");
}

// ---- Construction + slot storage ----------------------------------------

TEST(CSharp_GotoDefaultStatement, DefaultCtorHasNoSlots) {
    GotoDefaultStatement s;
    EXPECT_EQ(s.GetChildCount(), 0);
    EXPECT_THROW(s.GetChild(0), std::out_of_range);
    EXPECT_THROW(s.GetChildSlotInfo(0), std::out_of_range);
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_GotoDefaultStatement, AcceptVisitorDispatchesToVisit) {
    GotoDefaultStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotodefault");
}

TEST(CSharp_GotoDefaultStatement, AcceptVisitorIsVirtual) {
    GotoDefaultStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotodefault");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "gotodefault");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_GotoDefaultStatement, DepthFirstRecordsJustSelf) {
    GotoDefaultStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "gotodefault");
}

// ---- DoMatch (the generated type-only match) ---------------------------

TEST(CSharp_GotoDefaultStatement, DoMatchMatchesSameType) {
    GotoDefaultStatement a;
    GotoDefaultStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoDefaultStatement, DoMatchRejectsGotoStatementSibling) {
    GotoDefaultStatement a;
    GotoStatement b(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoDefaultStatement, DoMatchRejectsGotoCaseStatementSibling) {
    GotoDefaultStatement a;
    auto operand = std::make_unique<NullReferenceExpression>();
    GotoCaseStatement b(operand.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_GotoDefaultStatement, DoMatchRejectsDifferentType) {
    GotoDefaultStatement a;
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_GotoDefaultStatement, DoMatchRejectsNullCandidate) {
    GotoDefaultStatement a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_GotoDefaultStatement, CloneProducesFreshCopy) {
    GotoDefaultStatement s;
    std::unique_ptr<GotoDefaultStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
}

TEST(CSharp_GotoDefaultStatement, CloneIsVirtualAndCovariant) {
    GotoDefaultStatement s;
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<GotoDefaultStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<GotoDefaultStatement*>(stmtCopy.get()), nullptr);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_GotoDefaultStatement, CheckInvariantPasses) {
    GotoDefaultStatement s;
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_GotoStatement, SlotStaticsAreDistinctAcrossFamily) {
    // The two slot-bearing goto-family nodes (`GotoStatement`/`GotoCaseStatement`) each declare
    // their own per-node slot static pointing at a shared `Slots` kind (`GotoStatement` ->
    // `Slots::Identifier`, `GotoCaseStatement` -> `Slots::Expression`); the slot statics are
    // distinct objects (distinct addresses) and the kinds are distinct. Compare through the
    // common `CSharpSlotInfo*` base.
    const CSharpSlotInfo* gt = &GotoStatement::LabelTokenSlot;
    const CSharpSlotInfo* gc = &GotoCaseStatement::LabelExpressionSlot;
    EXPECT_NE(gt, gc);
    EXPECT_EQ(gt->Kind(), &Slots::Identifier);
    EXPECT_EQ(gc->Kind(), &Slots::Expression);
}
