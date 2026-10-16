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

// Tests for the embedded-`BlockStatement` keyword-statement trio -- `CheckedStatement`
// (the `checked_statement ::= 'checked' block` production), `UncheckedStatement` (the
// `unchecked_statement ::= 'unchecked' block` production), and `UnsafeStatement` (the
// `unsafe_statement ::= 'unsafe' block` production) -- the next in-order Phase-5 piece per
// the D259 plan ("the remaining concrete statements: ... CheckedStatement, UncheckedStatement,
// UnsafeStatement ..."). Each is a sealed `Statement` carrying a single REQUIRED `BlockStatement`
// `Body` child plus a const keyword string, sharing the new `Slots::Body` kind (cycle-broken into
// `BlockStatement.hpp`). The three suites share a `RecordingVisitor` and a `DoMatchAgainst`
// helper (the D234 multi-suite pattern); each is independently `--gtest_filter`-selectable.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UncheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UnsafeStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three new `Visit` methods under test (plus the
// `BlockStatement` body and the leaf statements a body holds), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitCheckedStatement(CheckedStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("checked");
        VisitChildren(node);
    }
    void VisitUncheckedStatement(UncheckedStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("unchecked");
        VisitChildren(node);
    }
    void VisitUnsafeStatement(UnsafeStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("unsafe");
        VisitChildren(node);
    }
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitEmptyStatement(EmptyStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-empty>"); return; }
        trace.push_back("empty");
        VisitChildren(node);
    }
    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-return>"); return; }
        trace.push_back("return");
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

// Builds a body block holding one `EmptyStatement`, for the depth-first-walk tests.
static std::unique_ptr<BlockStatement> MakeBodyWithEmpty() {
    auto block = std::make_unique<BlockStatement>();
    block->Statements().Add(std::make_unique<EmptyStatement>().release());
    return block;
}

} // namespace

// ==========================================================================
// CheckedStatement (required BlockStatement Body slot + checked const)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_CheckedStatement, IsStatementAndAstNodeNotExpression) {
    CheckedStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    // The Statement and Expression hierarchies are disjoint (both derive directly from AstNode):
    // a concrete statement is NOT an Expression.
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_CheckedStatement, IsFinal) {
    EXPECT_TRUE(std::is_final_v<CheckedStatement>);
}

// ---- const keyword ------------------------------------------------------

TEST(CSharp_CheckedStatement, CheckedKeyword) {
    EXPECT_STREQ(CheckedStatement::CheckedKeyword, "checked");
}

// ---- construction --------------------------------------------------------

TEST(CSharp_CheckedStatement, DefaultCtorHasNullBody) {
    CheckedStatement s;
    EXPECT_EQ(s.Body(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_CheckedStatement, AllParamsCtorSetsBody) {
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    CheckedStatement s(bodyPtr);
    EXPECT_EQ(s.Body(), bodyPtr);
    // The ctor transferred ownership of the child to the parent.
    EXPECT_EQ(body->Parent(), &s);
    body.release();  // The parent now owns it; release the unique_ptr without deleting.
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

// ---- Body slot accessors -------------------------------------------------

TEST(CSharp_CheckedStatement, BodySetterReparentsAndIndexes) {
    CheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();
    EXPECT_EQ(s.Body(), bodyPtr);
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

TEST(CSharp_CheckedStatement, BodySetterRejectsAlreadyParented) {
    CheckedStatement a;
    CheckedStatement b;
    auto body = std::make_unique<BlockStatement>();
    a.Body(body.get());
    EXPECT_THROW({ b.Body(body.get()); }, std::logic_error);
}

// ---- slot storage --------------------------------------------------------

TEST(CSharp_CheckedStatement, SlotStorageContract) {
    CheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    EXPECT_EQ(s.GetChildCount(), 1);
    EXPECT_EQ(s.GetChild(0), bodyPtr);
    EXPECT_EQ(s.GetChildSlotInfo(0), &CheckedStatement::BodySlot);
    EXPECT_THROW({ (void)s.GetChild(1); }, std::out_of_range);
    EXPECT_THROW({ (void)s.GetChildSlotInfo(1); }, std::out_of_range);
}

TEST(CSharp_CheckedStatement, SetChildAtSlotIndexZero) {
    CheckedStatement s;
    auto body1 = std::make_unique<BlockStatement>();
    auto body2 = std::make_unique<BlockStatement>();
    BlockStatement* b1 = body1.get();
    BlockStatement* b2 = body2.get();
    s.Body(b1);
    body1.release();
    s.SetChild(0, b2);
    body2.release();
    EXPECT_EQ(s.Body(), b2);
    EXPECT_EQ(b2->Parent(), &s);
    EXPECT_EQ(b2->ChildIndex, 0);
}

// ---- slot kind identity --------------------------------------------------

TEST(CSharp_CheckedStatement, BodySlotPointsAtSharedSlotsBody) {
    EXPECT_EQ(CheckedStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(CheckedStatement::BodySlot.IsCollection(), false);
    EXPECT_EQ(CheckedStatement::BodySlot.IsOptional(), false);
}

// ---- IsInstanceOfType is-a (the slot's dynamic_cast checker) -------------

TEST(CSharp_CheckedStatement, BodySlotAcceptsBlockStatement) {
    BlockStatement block;
    EXPECT_TRUE(CheckedStatement::BodySlot.IsInstanceOfType(&block));
}

TEST(CSharp_CheckedStatement, BodySlotRejectsNonBlockStatement) {
    CheckedStatement s;  // a Statement but NOT a BlockStatement
    EXPECT_FALSE(CheckedStatement::BodySlot.IsInstanceOfType(&s));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

TEST(CSharp_CheckedStatement, AcceptVisitorDispatches) {
    CheckedStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
}

TEST(CSharp_CheckedStatement, AcceptVisitorVirtualThroughAstNode) {
    CheckedStatement s;
    AstNode* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
}

TEST(CSharp_CheckedStatement, AcceptVisitorVirtualThroughStatement) {
    CheckedStatement s;
    Statement* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "checked");
}

// ---- depth-first walk ----------------------------------------------------

TEST(CSharp_CheckedStatement, DepthFirstWalkRecurseIntoBodyBlock) {
    CheckedStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    RecordingVisitor v;
    s.AcceptVisitor(v);
    // checked -> block -> empty (the body block holds one EmptyStatement).
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "checked");
    EXPECT_EQ(v.trace[1], "block");
    EXPECT_EQ(v.trace[2], "empty");
}

// ---- DoMatch -------------------------------------------------------------

TEST(CSharp_CheckedStatement, DoMatchSameBody) {
    auto p = std::make_unique<CheckedStatement>();
    auto c = std::make_unique<CheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_TRUE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchDifferentBody) {
    auto p = std::make_unique<CheckedStatement>();
    auto c = std::make_unique<CheckedStatement>();
    // Two blocks with DIFFERENT statement counts -> the collection DoMatch rejects.
    auto pb = std::make_unique<BlockStatement>();
    pb->Statements().Add(std::make_unique<EmptyStatement>().release());
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchNullPatternBodyDoesNotMatch) {
    auto p = std::make_unique<CheckedStatement>();  // null Body (missing required pattern child)
    auto c = std::make_unique<CheckedStatement>();
    auto cb = std::make_unique<BlockStatement>();
    c->Body(cb.get());
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchNullCandidateBodyDoesNotMatch) {
    auto p = std::make_unique<CheckedStatement>();
    auto c = std::make_unique<CheckedStatement>();  // null Body (missing required candidate child)
    auto pb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    pb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchRejectsUncheckedCandidate) {
    auto p = std::make_unique<CheckedStatement>();
    auto c = std::make_unique<UncheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchRejectsUnsafeCandidate) {
    auto p = std::make_unique<CheckedStatement>();
    auto c = std::make_unique<UnsafeStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_CheckedStatement, DoMatchRejectsNull) {
    auto p = std::make_unique<CheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    pb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), nullptr));
}

TEST(CSharp_CheckedStatement, DoMatchRejectsNonStatementCandidate) {
    auto p = std::make_unique<CheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    pb.release();
    auto c = std::make_unique<NullReferenceExpression>();  // an Expression, not a Statement
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

// ---- Clone ---------------------------------------------------------------

TEST(CSharp_CheckedStatement, CloneDeepCopiesBody) {
    CheckedStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    std::unique_ptr<CheckedStatement> clone(s.Clone());
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), bodyPtr);  // deep copy, not the same node
    EXPECT_EQ(clone->Body()->Parent(), clone.get());
    EXPECT_EQ(clone->Body()->ChildIndex, 0);
    // The cloned body has the same structure (one EmptyStatement child).
    EXPECT_EQ(clone->Body()->GetChildCount(), 1);
}

TEST(CSharp_CheckedStatement, CloneNullBody) {
    CheckedStatement s;  // no Body
    std::unique_ptr<CheckedStatement> clone(s.Clone());
    EXPECT_EQ(clone->Body(), nullptr);
}

TEST(CSharp_CheckedStatement, CloneVirtualThroughAstNode) {
    CheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    s.Body(body.get());
    body.release();
    AstNode* node = &s;
    std::unique_ptr<AstNode> clone(node->Clone());
    EXPECT_NE(dynamic_cast<CheckedStatement*>(clone.get()), nullptr);
}

TEST(CSharp_CheckedStatement, CloneCovariantThroughStatement) {
    CheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    s.Body(body.get());
    body.release();
    Statement* node = &s;
    std::unique_ptr<Statement> clone(node->Clone());
    EXPECT_NE(dynamic_cast<CheckedStatement*>(clone.get()), nullptr);
}

// ---- CheckInvariant ------------------------------------------------------

TEST(CSharp_CheckedStatement, CheckInvariantPassesOnFilledNode) {
    CheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    s.Body(body.get());
    body.release();
#ifndef NDEBUG
    EXPECT_NO_FATAL_FAILURE(s.CheckInvariant());
#endif
}

// ==========================================================================
// UncheckedStatement (required BlockStatement Body slot + unchecked const)
// ==========================================================================

TEST(CSharp_UncheckedStatement, IsStatementAndAstNodeNotExpression) {
    UncheckedStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_UncheckedStatement, IsFinal) {
    EXPECT_TRUE(std::is_final_v<UncheckedStatement>);
}

TEST(CSharp_UncheckedStatement, UncheckedKeyword) {
    EXPECT_STREQ(UncheckedStatement::UncheckedKeyword, "unchecked");
}

TEST(CSharp_UncheckedStatement, DefaultCtorHasNullBody) {
    UncheckedStatement s;
    EXPECT_EQ(s.Body(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_UncheckedStatement, AllParamsCtorSetsBody) {
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    UncheckedStatement s(bodyPtr);
    EXPECT_EQ(s.Body(), bodyPtr);
    EXPECT_EQ(body->Parent(), &s);
    body.release();
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

TEST(CSharp_UncheckedStatement, BodySetterReparentsAndIndexes) {
    UncheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();
    EXPECT_EQ(s.Body(), bodyPtr);
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

TEST(CSharp_UncheckedStatement, SlotStorageContract) {
    UncheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    EXPECT_EQ(s.GetChildCount(), 1);
    EXPECT_EQ(s.GetChild(0), bodyPtr);
    EXPECT_EQ(s.GetChildSlotInfo(0), &UncheckedStatement::BodySlot);
    EXPECT_THROW({ (void)s.GetChild(1); }, std::out_of_range);
}

TEST(CSharp_UncheckedStatement, BodySlotPointsAtSharedSlotsBody) {
    EXPECT_EQ(UncheckedStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(UncheckedStatement::BodySlot.IsCollection(), false);
    EXPECT_EQ(UncheckedStatement::BodySlot.IsOptional(), false);
}

TEST(CSharp_UncheckedStatement, BodySlotAcceptsBlockStatement) {
    BlockStatement block;
    EXPECT_TRUE(UncheckedStatement::BodySlot.IsInstanceOfType(&block));
}

TEST(CSharp_UncheckedStatement, AcceptVisitorDispatches) {
    UncheckedStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unchecked");
}

TEST(CSharp_UncheckedStatement, AcceptVisitorVirtualThroughStatement) {
    UncheckedStatement s;
    Statement* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unchecked");
}

TEST(CSharp_UncheckedStatement, DepthFirstWalkRecurseIntoBodyBlock) {
    UncheckedStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "unchecked");
    EXPECT_EQ(v.trace[1], "block");
    EXPECT_EQ(v.trace[2], "empty");
}

TEST(CSharp_UncheckedStatement, DoMatchSameBody) {
    auto p = std::make_unique<UncheckedStatement>();
    auto c = std::make_unique<UncheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_TRUE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UncheckedStatement, DoMatchDifferentBody) {
    auto p = std::make_unique<UncheckedStatement>();
    auto c = std::make_unique<UncheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    pb->Statements().Add(std::make_unique<EmptyStatement>().release());
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UncheckedStatement, DoMatchRejectsCheckedCandidate) {
    auto p = std::make_unique<UncheckedStatement>();
    auto c = std::make_unique<CheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UncheckedStatement, DoMatchRejectsUnsafeCandidate) {
    auto p = std::make_unique<UncheckedStatement>();
    auto c = std::make_unique<UnsafeStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UncheckedStatement, DoMatchRejectsNull) {
    auto p = std::make_unique<UncheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    pb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), nullptr));
}

TEST(CSharp_UncheckedStatement, CloneDeepCopiesBody) {
    UncheckedStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    std::unique_ptr<UncheckedStatement> clone(s.Clone());
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), bodyPtr);
    EXPECT_EQ(clone->Body()->Parent(), clone.get());
    EXPECT_EQ(clone->Body()->ChildIndex, 0);
    EXPECT_EQ(clone->Body()->GetChildCount(), 1);
}

TEST(CSharp_UncheckedStatement, CloneNullBody) {
    UncheckedStatement s;
    std::unique_ptr<UncheckedStatement> clone(s.Clone());
    EXPECT_EQ(clone->Body(), nullptr);
}

TEST(CSharp_UncheckedStatement, CheckInvariantPassesOnFilledNode) {
    UncheckedStatement s;
    auto body = std::make_unique<BlockStatement>();
    s.Body(body.get());
    body.release();
#ifndef NDEBUG
    EXPECT_NO_FATAL_FAILURE(s.CheckInvariant());
#endif
}

// ==========================================================================
// UnsafeStatement (required BlockStatement Body slot + unsafe const)
// ==========================================================================

TEST(CSharp_UnsafeStatement, IsStatementAndAstNodeNotExpression) {
    UnsafeStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_UnsafeStatement, IsFinal) {
    EXPECT_TRUE(std::is_final_v<UnsafeStatement>);
}

TEST(CSharp_UnsafeStatement, UnsafeKeyword) {
    EXPECT_STREQ(UnsafeStatement::UnsafeKeyword, "unsafe");
}

TEST(CSharp_UnsafeStatement, DefaultCtorHasNullBody) {
    UnsafeStatement s;
    EXPECT_EQ(s.Body(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_UnsafeStatement, AllParamsCtorSetsBody) {
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    UnsafeStatement s(bodyPtr);
    EXPECT_EQ(s.Body(), bodyPtr);
    EXPECT_EQ(body->Parent(), &s);
    body.release();
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

TEST(CSharp_UnsafeStatement, BodySetterReparentsAndIndexes) {
    UnsafeStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();
    EXPECT_EQ(s.Body(), bodyPtr);
    EXPECT_EQ(s.Body()->Parent(), &s);
    EXPECT_EQ(s.Body()->ChildIndex, 0);
}

TEST(CSharp_UnsafeStatement, SlotStorageContract) {
    UnsafeStatement s;
    auto body = std::make_unique<BlockStatement>();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    EXPECT_EQ(s.GetChildCount(), 1);
    EXPECT_EQ(s.GetChild(0), bodyPtr);
    EXPECT_EQ(s.GetChildSlotInfo(0), &UnsafeStatement::BodySlot);
    EXPECT_THROW({ (void)s.GetChild(1); }, std::out_of_range);
}

TEST(CSharp_UnsafeStatement, BodySlotPointsAtSharedSlotsBody) {
    EXPECT_EQ(UnsafeStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(UnsafeStatement::BodySlot.IsCollection(), false);
    EXPECT_EQ(UnsafeStatement::BodySlot.IsOptional(), false);
}

TEST(CSharp_UnsafeStatement, BodySlotAcceptsBlockStatement) {
    BlockStatement block;
    EXPECT_TRUE(UnsafeStatement::BodySlot.IsInstanceOfType(&block));
}

TEST(CSharp_UnsafeStatement, AcceptVisitorDispatches) {
    UnsafeStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unsafe");
}

TEST(CSharp_UnsafeStatement, AcceptVisitorVirtualThroughStatement) {
    UnsafeStatement s;
    Statement* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "unsafe");
}

TEST(CSharp_UnsafeStatement, DepthFirstWalkRecurseIntoBodyBlock) {
    UnsafeStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "unsafe");
    EXPECT_EQ(v.trace[1], "block");
    EXPECT_EQ(v.trace[2], "empty");
}

TEST(CSharp_UnsafeStatement, DoMatchSameBody) {
    auto p = std::make_unique<UnsafeStatement>();
    auto c = std::make_unique<UnsafeStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_TRUE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UnsafeStatement, DoMatchDifferentBody) {
    auto p = std::make_unique<UnsafeStatement>();
    auto c = std::make_unique<UnsafeStatement>();
    auto pb = std::make_unique<BlockStatement>();
    pb->Statements().Add(std::make_unique<EmptyStatement>().release());
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UnsafeStatement, DoMatchRejectsCheckedCandidate) {
    auto p = std::make_unique<UnsafeStatement>();
    auto c = std::make_unique<CheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UnsafeStatement, DoMatchRejectsUncheckedCandidate) {
    auto p = std::make_unique<UnsafeStatement>();
    auto c = std::make_unique<UncheckedStatement>();
    auto pb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    c->Body(cb.get());
    pb.release();
    cb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_UnsafeStatement, DoMatchRejectsNull) {
    auto p = std::make_unique<UnsafeStatement>();
    auto pb = std::make_unique<BlockStatement>();
    p->Body(pb.get());
    pb.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), nullptr));
}

TEST(CSharp_UnsafeStatement, CloneDeepCopiesBody) {
    UnsafeStatement s;
    auto body = MakeBodyWithEmpty();
    BlockStatement* bodyPtr = body.get();
    s.Body(bodyPtr);
    body.release();

    std::unique_ptr<UnsafeStatement> clone(s.Clone());
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), bodyPtr);
    EXPECT_EQ(clone->Body()->Parent(), clone.get());
    EXPECT_EQ(clone->Body()->ChildIndex, 0);
    EXPECT_EQ(clone->Body()->GetChildCount(), 1);
}

TEST(CSharp_UnsafeStatement, CloneNullBody) {
    UnsafeStatement s;
    std::unique_ptr<UnsafeStatement> clone(s.Clone());
    EXPECT_EQ(clone->Body(), nullptr);
}

TEST(CSharp_UnsafeStatement, CheckInvariantPassesOnFilledNode) {
    UnsafeStatement s;
    auto body = std::make_unique<BlockStatement>();
    s.Body(body.get());
    body.release();
#ifndef NDEBUG
    EXPECT_NO_FATAL_FAILURE(s.CheckInvariant());
#endif
}

// ==========================================================================
// Cross-trio: shared Slots::Body kind and slot-static distinctness
// ==========================================================================

TEST(CSharp_CheckedUncheckedUnsafeStatement, SharedSlotsBodyKind) {
    // All three per-node BodySlots point at the SAME shared Slots::Body kind (the kind-collapsing
    // by [Slot] name): a [Slot("Body")] BlockStatement on every node collapses to one kind.
    EXPECT_EQ(CheckedStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(UncheckedStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(UnsafeStatement::BodySlot.Kind(), &Slots::Body);
    EXPECT_EQ(CheckedStatement::BodySlot.Kind(), UncheckedStatement::BodySlot.Kind());
    EXPECT_EQ(UncheckedStatement::BodySlot.Kind(), UnsafeStatement::BodySlot.Kind());
}

TEST(CSharp_CheckedUncheckedUnsafeStatement, PerNodeSlotStaticsAreDistinctInstances) {
    // The three per-node BodySlots are distinct static instances (distinct addresses), each
    // carrying its own node-local per-position flags, even though they share the kind.
    EXPECT_NE(&CheckedStatement::BodySlot, &UncheckedStatement::BodySlot);
    EXPECT_NE(&UncheckedStatement::BodySlot, &UnsafeStatement::BodySlot);
    EXPECT_NE(&CheckedStatement::BodySlot, &UnsafeStatement::BodySlot);
}
