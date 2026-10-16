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

// Tests for the next simple C# AST statement trio -- `YieldReturnStatement` (the
// `ExpressionStatement` D255 single-REQUIRED-`Expression`-slot shape applied to the yield
// family, plus `YieldKeyword`/`ReturnKeyword` const strings), `EmptyStatement` (the first
// ported statement leaf with its OWN `Location` field and `StartLocation`/`EndLocation`
// overrides -- a one-column span, no `[Slot]` children, a type-only `DoMatch`), and
// `LabelStatement` (the `GotoStatement` D257 string-name-`[Slot]` shape MINUS the nullable
// optionality and MINUS a const keyword -- a single REQUIRED `string` `Label` over a backing
// `LabelToken`) -- the next in-order Phase-5 piece per the D258 plan ("the remaining concrete
// statements: YieldReturnStatement ... EmptyStatement/LabelStatement leaves"). `YieldReturnStatement`
// reuses `Slots::Expression`; `LabelStatement` reuses `Slots::Identifier`; `EmptyStatement` adds
// no `Slots` kind (no slots). The three suites share a `RecordingVisitor` and a `DoMatchAgainst`
// helper (the D234 multi-suite pattern); each is independently `--gtest_filter`-selectable.

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
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three new `Visit` methods under test (plus the
// `Identifier` token and the leaf expressions the statements hold), recording a tag and
// recursing via `VisitChildren` (the inherited depth-first default). The trace is the visited
// nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitYieldReturnStatement(YieldReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("yieldreturn");
        VisitChildren(node);
    }
    void VisitEmptyStatement(EmptyStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("empty");
        VisitChildren(node);
    }
    void VisitLabelStatement(LabelStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("label:" + node->Label());
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
// YieldReturnStatement (required Expression slot + yield/return consts)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_YieldReturnStatement, IsStatementAndAstNodeNotExpression) {
    YieldReturnStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- const keywords ------------------------------------------------------

TEST(CSharp_YieldReturnStatement, YieldAndReturnKeywordConsts) {
    EXPECT_STREQ(YieldReturnStatement::YieldKeyword, "yield");
    EXPECT_STREQ(YieldReturnStatement::ReturnKeyword, "return");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_YieldReturnStatement, DefaultCtor) {
    YieldReturnStatement s;
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);  // the slot counts even when the operand is absent
}

TEST(CSharp_YieldReturnStatement, AllParamsCtorSetsAndParents) {
    auto operand = std::make_unique<NullReferenceExpression>();
    NullReferenceExpression* operandPtr = operand.get();
    YieldReturnStatement s(operand.get());
    EXPECT_EQ(s.Expression(), operandPtr);
    EXPECT_EQ(operandPtr->Parent(), &s);
    EXPECT_EQ(operandPtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_YieldReturnStatement, ExpressionSetterFillsAndParents) {
    YieldReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    EXPECT_EQ(s.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_TRUE(s.ChildIndicesValid());
}

TEST(CSharp_YieldReturnStatement, NullExpressionSetterClearsSlot) {
    YieldReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    s.Expression(nullptr);
    EXPECT_EQ(s.Expression(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(a->ChildIndex, -1);
}

TEST(CSharp_YieldReturnStatement, GetChildReturnsSlot) {
    YieldReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    EXPECT_EQ(s.GetChild(0), a.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_YieldReturnStatement, SetChildRoutesToSlot) {
    YieldReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.SetChild(0, a.get());
    EXPECT_EQ(s.Expression(), a.get());
    EXPECT_EQ(a->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_YieldReturnStatement, GetChildSlotInfoReturnsSlotStatic) {
    YieldReturnStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &YieldReturnStatement::ExpressionSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_YieldReturnStatement, SlotKindPointsAtSharedSlotsConstant) {
    YieldReturnStatement s;
    auto a = std::make_unique<NullReferenceExpression>();
    s.Expression(a.get());
    ASSERT_EQ(a->Slot(), &YieldReturnStatement::ExpressionSlot);
    EXPECT_EQ(a->Slot()->Kind(), &Slots::Expression);
}

TEST(CSharp_YieldReturnStatement, SlotIsInstanceOfExpression) {
    EXPECT_FALSE(YieldReturnStatement::ExpressionSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    auto nre = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(YieldReturnStatement::ExpressionSlot.IsInstanceOfType(nre.get()));
    auto id = std::unique_ptr<Identifier>(Identifier::Create("x"));
    EXPECT_FALSE(YieldReturnStatement::ExpressionSlot.IsInstanceOfType(id.get()));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_YieldReturnStatement, AcceptVisitorDispatchesToVisit) {
    YieldReturnStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "yieldreturn");
}

TEST(CSharp_YieldReturnStatement, AcceptVisitorIsVirtual) {
    YieldReturnStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "yieldreturn");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "yieldreturn");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_YieldReturnStatement, DepthFirstWalksExpressionInOrder) {
    auto operand = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement s(operand.get());
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "yieldreturn");
    EXPECT_EQ(v.trace[1], "null");
}

TEST(CSharp_YieldReturnStatement, DepthFirstOnEmptyNodeRecordsJustSelf) {
    YieldReturnStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "yieldreturn");
}

// ---- DoMatch (the generated non-nullable recursive match) ----------------

TEST(CSharp_YieldReturnStatement, DoMatchMatchesSameOperand) {
    auto a1 = std::make_unique<NullReferenceExpression>();
    auto b1 = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement a(a1.get());
    YieldReturnStatement b(b1.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchDifferentOperandRejects) {
    auto a1 = std::make_unique<NullReferenceExpression>();
    auto b1 = std::make_unique<ThisReferenceExpression>();
    YieldReturnStatement a(a1.get());
    YieldReturnStatement b(b1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchNullPatternOperandRejects) {
    // A null pattern operand (an empty pattern) does not match -- the C# would null-deref, the
    // port's `MatchRequired` guards it defensively.
    YieldReturnStatement a;  // no operand
    auto b1 = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement b(b1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchRejectsReturnStatementStructuralTwin) {
    // `YieldReturnStatement` and `ReturnStatement` are structural twins (both single-`Expression`-
    // slot statements); the pattern matcher's type-check gate distinguishes them.
    auto a1 = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement a(a1.get());
    auto b1 = std::make_unique<NullReferenceExpression>();
    ReturnStatement b(b1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchRejectsEmptyStatementSibling) {
    YieldReturnStatement a;
    EmptyStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchRejectsLabelStatementSibling) {
    YieldReturnStatement a;
    LabelStatement b(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_YieldReturnStatement, DoMatchRejectsDifferentType) {
    YieldReturnStatement a;
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_YieldReturnStatement, DoMatchRejectsNullCandidate) {
    YieldReturnStatement a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_YieldReturnStatement, CloneDeepCopiesOperand) {
    auto operand = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement s(operand.get());
    std::unique_ptr<YieldReturnStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), s.Expression());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(copy->Expression()->ChildIndex, 0);
}

TEST(CSharp_YieldReturnStatement, CloneEmptyNodeHasNoOperand) {
    YieldReturnStatement s;
    std::unique_ptr<YieldReturnStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Expression(), nullptr);
}

TEST(CSharp_YieldReturnStatement, CloneIsVirtualAndCovariant) {
    auto operand = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement s(operand.get());
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<YieldReturnStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<YieldReturnStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_YieldReturnStatement, CloneDoesNotDetachSource) {
    auto operand = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement s(operand.get());
    std::unique_ptr<YieldReturnStatement> copy(s.Clone());
    EXPECT_EQ(s.Expression()->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_YieldReturnStatement, CheckInvariantPassesOnFilledNode) {
    auto operand = std::make_unique<NullReferenceExpression>();
    YieldReturnStatement s(operand.get());
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// EmptyStatement (leaf with Location + StartLocation/EndLocation overrides)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_EmptyStatement, IsStatementAndAstNodeNotExpression) {
    EmptyStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- Construction + Location --------------------------------------------

TEST(CSharp_EmptyStatement, DefaultCtorLocationIsEmpty) {
    EmptyStatement s;
    EXPECT_EQ(s.Location(), TextLocation::Empty);
    EXPECT_EQ(s.StartLocation(), TextLocation::Empty);
}

TEST(CSharp_EmptyStatement, DefaultCtorHasNoSlots) {
    EmptyStatement s;
    EXPECT_EQ(s.GetChildCount(), 0);
    EXPECT_THROW(s.GetChild(0), std::out_of_range);
    EXPECT_THROW(s.GetChildSlotInfo(0), std::out_of_range);
}

TEST(CSharp_EmptyStatement, LocationSetGetRoundTrip) {
    EmptyStatement s;
    s.Location(TextLocation(3, 7));
    EXPECT_EQ(s.Location(), TextLocation(3, 7));
    EXPECT_EQ(s.StartLocation(), TextLocation(3, 7));
}

TEST(CSharp_EmptyStatement, EndLocationIsOneColumnPastLocation) {
    // The `;` is a single column: `EndLocation` is one column past `Location`.
    EmptyStatement s;
    s.Location(TextLocation(3, 7));
    EXPECT_EQ(s.EndLocation(), TextLocation(3, 8));
}

TEST(CSharp_EmptyStatement, DefaultEndLocationIsOneColumnPastEmpty) {
    EmptyStatement s;
    EXPECT_EQ(s.EndLocation(), TextLocation(TextLocation::Empty.Line, TextLocation::Empty.Column + 1));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_EmptyStatement, AcceptVisitorDispatchesToVisit) {
    EmptyStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "empty");
}

TEST(CSharp_EmptyStatement, AcceptVisitorIsVirtual) {
    EmptyStatement s;
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "empty");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "empty");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_EmptyStatement, DepthFirstRecordsJustSelf) {
    EmptyStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "empty");
}

// ---- DoMatch (the generated type-only match) ---------------------------

TEST(CSharp_EmptyStatement, DoMatchMatchesSameType) {
    EmptyStatement a;
    EmptyStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_EmptyStatement, DoMatchRejectsYieldReturnStatementSibling) {
    EmptyStatement a;
    YieldReturnStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_EmptyStatement, DoMatchRejectsLabelStatementSibling) {
    EmptyStatement a;
    LabelStatement b(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_EmptyStatement, DoMatchRejectsDifferentType) {
    EmptyStatement a;
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_EmptyStatement, DoMatchRejectsNullCandidate) {
    EmptyStatement a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_EmptyStatement, CloneCopiesLocation) {
    // `EmptyStatement` derives `EndLocation`, so its `Clone` copies `StartLocation` (the
    // Identifier/PrimitiveType derives-EndLocation-copies-StartLocation precedent).
    EmptyStatement s;
    s.Location(TextLocation(5, 12));
    std::unique_ptr<EmptyStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Location(), TextLocation(5, 12));
    EXPECT_EQ(copy->StartLocation(), TextLocation(5, 12));
    EXPECT_EQ(copy->EndLocation(), TextLocation(5, 13));
}

TEST(CSharp_EmptyStatement, CloneIsVirtualAndCovariant) {
    EmptyStatement s;
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<EmptyStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<EmptyStatement*>(stmtCopy.get()), nullptr);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_EmptyStatement, CheckInvariantPasses) {
    EmptyStatement s;
    s.CheckInvariant();
    SUCCEED();
}

// ==========================================================================
// LabelStatement (required string-name [Slot] Label/LabelToken)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_LabelStatement, IsStatementAndAstNodeNotExpression) {
    LabelStatement s(std::string("loop"));
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_LabelStatement, DefaultCtor) {
    LabelStatement s;
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);  // the slot counts even when the token is absent
}

TEST(CSharp_LabelStatement, StringCtorCreatesTokenViaCreate) {
    LabelStatement s(std::string("loop"));
    ASSERT_NE(s.LabelToken(), nullptr);
    EXPECT_EQ(s.Label(), "loop");
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
    EXPECT_EQ(s.LabelToken()->ChildIndex, 0);
}

TEST(CSharp_LabelStatement, EmptyStringCtorCreatesTokenWithEmptyName) {
    // A non-nullable `Label` uses `Identifier::Create` (NOT `CreateIfNotEmpty`): an empty name
    // yields a token with an empty `Name`, NOT a null token (the `MemberType.MemberName` D238 /
    // `IdentifierExpression.Identifier` D246 non-nullable precedent). Braced-init avoids the
    // most-vexing-parse (`LabelStatement s(std::string())` would declare `s` as a function, the
    // D236/D257 precedent).
    LabelStatement s{std::string()};
    ASSERT_NE(s.LabelToken(), nullptr);
    EXPECT_EQ(s.Label(), "");
}

// ---- Accessors + slot storage -------------------------------------------

TEST(CSharp_LabelStatement, LabelSetterCreatesToken) {
    LabelStatement s;
    s.Label(std::string("end"));
    ASSERT_NE(s.LabelToken(), nullptr);
    EXPECT_EQ(s.Label(), "end");
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
    EXPECT_EQ(s.LabelToken()->ChildIndex, 0);
}

TEST(CSharp_LabelStatement, EmptyLabelSetterCreatesEmptyNameToken) {
    // An empty label setter creates a token with an empty `Name` (NOT a null token) -- the
    // non-nullable behaviour, distinct from `GotoStatement` whose nullable `Label` clears the
    // token on empty via `CreateIfNotEmpty`.
    LabelStatement s;
    s.Label(std::string("loop"));
    s.Label(std::string_view());  // empty -> Create -> token with empty Name, NOT null
    ASSERT_NE(s.LabelToken(), nullptr);
    EXPECT_EQ(s.Label(), "");
}

TEST(CSharp_LabelStatement, LabelTokenSetterFillsAndParents) {
    LabelStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("end"));
    Identifier* tokenPtr = token.get();
    s.LabelToken(token.get());
    EXPECT_EQ(s.LabelToken(), tokenPtr);
    EXPECT_EQ(tokenPtr->Parent(), &s);
    EXPECT_EQ(tokenPtr->ChildIndex, 0);
}

TEST(CSharp_LabelStatement, NullLabelTokenSetterClearsSlot) {
    LabelStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("end"));
    s.LabelToken(token.get());
    s.LabelToken(nullptr);
    EXPECT_EQ(s.LabelToken(), nullptr);
    EXPECT_EQ(token->Parent(), nullptr);
    EXPECT_EQ(token->ChildIndex, -1);
}

TEST(CSharp_LabelStatement, GetChildReturnsSlot) {
    LabelStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.LabelToken(token.get());
    EXPECT_EQ(s.GetChild(0), token.get());
    EXPECT_THROW(s.GetChild(1), std::out_of_range);
}

TEST(CSharp_LabelStatement, SetChildRoutesToSlot) {
    LabelStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.SetChild(0, token.get());
    EXPECT_EQ(s.LabelToken(), token.get());
    EXPECT_EQ(token->Parent(), &s);
    EXPECT_THROW(s.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_LabelStatement, GetChildSlotInfoReturnsSlotStatic) {
    LabelStatement s;
    EXPECT_EQ(s.GetChildSlotInfo(0), &LabelStatement::LabelTokenSlot);
    EXPECT_THROW(s.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_LabelStatement, SlotKindPointsAtSharedSlotsConstant) {
    LabelStatement s;
    auto token = std::unique_ptr<Identifier>(Identifier::Create("x"));
    s.LabelToken(token.get());
    ASSERT_EQ(token->Slot(), &LabelStatement::LabelTokenSlot);
    EXPECT_EQ(token->Slot()->Kind(), &Slots::Identifier);
}

TEST(CSharp_LabelStatement, SlotIsInstanceOfIdentifier) {
    EXPECT_FALSE(LabelStatement::LabelTokenSlot.IsInstanceOfType(
        static_cast<AstNode*>(nullptr)));
    auto id = std::unique_ptr<Identifier>(Identifier::Create("x"));
    EXPECT_TRUE(LabelStatement::LabelTokenSlot.IsInstanceOfType(id.get()));
    NullReferenceExpression nre;
    EXPECT_FALSE(LabelStatement::LabelTokenSlot.IsInstanceOfType(&nre));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_LabelStatement, AcceptVisitorDispatchesToVisit) {
    // A filled `LabelStatement` (the token is required, so the node always carries one) recurses
    // into its `LabelToken` `Identifier` child during the depth-first walk, giving a 2-entry
    // trace (the label tag then the id token). The dispatch is verified by `trace[0]`.
    LabelStatement s(std::string("loop"));
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "label:loop");
    EXPECT_EQ(v.trace[1], "id:loop");
}

TEST(CSharp_LabelStatement, AcceptVisitorIsVirtual) {
    LabelStatement s(std::string("loop"));
    AstNode* asNode = &s;
    Statement* asStmt = &s;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "label:loop");
    RecordingVisitor v2;
    asStmt->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 2u);
    EXPECT_EQ(v2.trace[0], "label:loop");
}

// ---- Depth-first walk ---------------------------------------------------

TEST(CSharp_LabelStatement, DepthFirstWalksLabelTokenInOrder) {
    LabelStatement s(std::string("loop"));
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "label:loop");
    EXPECT_EQ(v.trace[1], "id:loop");
}

// ---- DoMatch (the generated string MatchString match) ------------------

TEST(CSharp_LabelStatement, DoMatchMatchesSameLabel) {
    LabelStatement a(std::string("loop"));
    LabelStatement b(std::string("loop"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchDifferentLabelRejects) {
    LabelStatement a(std::string("loop"));
    LabelStatement b(std::string("end"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchBothEmptyNamesMatch) {
    // Two empty-name labels match (empty == empty) -- a non-nullable empty name is a real ""
    // match, not a null-match (the non-nullable behaviour, distinct from `GotoStatement` whose
    // nullable empty labels are absent tokens that match via null == null).
    LabelStatement a{std::string()};
    LabelStatement b{std::string()};
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchEmptyPatternDoesNotMatchNonEmptyCandidate) {
    LabelStatement a{std::string()};
    LabelStatement b(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchNonEmptyPatternDoesNotMatchEmptyCandidate) {
    LabelStatement a(std::string("loop"));
    LabelStatement b{std::string()};
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchAnyStringWildcardMatchesAny) {
    // A pattern whose `Label` is the `$any$` sentinel matches any candidate label via
    // `Pattern::MatchString`'s wildcard path (`pattern == AnyString` -> true). Build the pattern
    // directly by setting its `LabelToken` `Name` to `$any$` (the `Identifier::Create` factory).
    LabelStatement pat;
    pat.LabelToken(Identifier::Create("$any$"));
    LabelStatement candA(std::string("loop"));
    LabelStatement candB(std::string("end"));
    LabelStatement candC{std::string()};  // empty name is still a real label, matched by $any$
    EXPECT_TRUE(DoMatchAgainst(&pat, &candA));
    EXPECT_TRUE(DoMatchAgainst(&pat, &candB));
    EXPECT_TRUE(DoMatchAgainst(&pat, &candC));
}

TEST(CSharp_LabelStatement, DoMatchRejectsGotoStatementSibling) {
    LabelStatement a(std::string("loop"));
    GotoStatement b(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchRejectsYieldReturnStatementSibling) {
    LabelStatement a(std::string("loop"));
    YieldReturnStatement b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_LabelStatement, DoMatchRejectsDifferentType) {
    LabelStatement a(std::string("loop"));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_LabelStatement, DoMatchRejectsNullCandidate) {
    LabelStatement a(std::string("loop"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

TEST(CSharp_LabelStatement, CloneDeepCopiesToken) {
    LabelStatement s(std::string("loop"));
    std::unique_ptr<LabelStatement> copy(s.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &s);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->LabelToken(), nullptr);
    EXPECT_NE(copy->LabelToken(), s.LabelToken());
    EXPECT_EQ(copy->Label(), "loop");
    EXPECT_EQ(copy->LabelToken()->Parent(), copy.get());
    EXPECT_EQ(copy->LabelToken()->ChildIndex, 0);
}

TEST(CSharp_LabelStatement, CloneIsVirtualAndCovariant) {
    LabelStatement s(std::string("loop"));
    AstNode* asNode = &s;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<LabelStatement*>(nodeCopy.get()), nullptr);
    Statement* asStmt = &s;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<LabelStatement*>(stmtCopy.get()), nullptr);
}

TEST(CSharp_LabelStatement, CloneDoesNotDetachSource) {
    LabelStatement s(std::string("loop"));
    std::unique_ptr<LabelStatement> copy(s.Clone());
    EXPECT_EQ(s.LabelToken()->Parent(), &s);
}

// ---- CheckInvariant -----------------------------------------------------

TEST(CSharp_LabelStatement, CheckInvariantPassesOnFilledNode) {
    LabelStatement s(std::string("loop"));
    s.CheckInvariant();
    SUCCEED();
}

// ---- slot-static distinctness (shared-kind identity) --------------------

TEST(CSharp_YieldReturnLabelStatement, SlotStaticsAreDistinctAcrossTrio) {
    // The two slot-bearing trio nodes (`YieldReturnStatement`/`LabelStatement`) each declare
    // their own per-node slot static pointing at a shared `Slots` kind (`YieldReturnStatement`
    // -> `Slots::Expression`, `LabelStatement` -> `Slots::Identifier`); the slot statics are
    // distinct objects (distinct addresses) and the kinds are distinct. Compare through the
    // common `CSharpSlotInfo*` base. (`EmptyStatement` has no slot static -- no slots.)
    const CSharpSlotInfo* yr = &YieldReturnStatement::ExpressionSlot;
    const CSharpSlotInfo* ls = &LabelStatement::LabelTokenSlot;
    EXPECT_NE(yr, ls);
    EXPECT_EQ(yr->Kind(), &Slots::Expression);
    EXPECT_EQ(ls->Kind(), &Slots::Identifier);
}
