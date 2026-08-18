// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
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

// Tests for the `ErrorExpression` concrete node (cpp/.../Syntax/Expressions/ErrorExpression.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ErrorExpression.cs) -- the
// unparseable-input placeholder leaf: a sealed `Expression` with NO `[Slot]` children, its own
// `Location` field (a zero-width point span -- `StartLocation` == `EndLocation` == `Location`,
// distinct from `EmptyStatement` D259 whose `EndLocation` is `Location + 1`), and a hand-written
// `(string error)` ctor attaching the error text as a trailing multi-line `Comment`. Exercises
// the `Location` accessor, the derived `EndLocation` (zero-width), the `(string)` ctor's
// trailing-`Comment` attachment (verified via `TrailingTrivia()`), the inherited zero-child slot
// defaults, the `AcceptVisitor` dispatch, the type-only `DoMatch`, the per-concrete-node `Clone`
// (which deep-copies the trailing `Comment`), and the inherited `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete types, recursing via the inherited `VisitChildren` (the document-order walk of the
// slot-storage children -- trivia is NOT in the slot storage, so the default walk does not visit
// the trailing `Comment`; it is reached via `TrailingTrivia()`).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitErrorExpression(ErrorExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("error");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nre>"); return; }
        trace.push_back("null");
        VisitChildren(node);
    }
    void VisitComment(Comment* node) override {
        if (node == nullptr) { trace.push_back("<null-comment>"); return; }
        trace.push_back("comment:" + node->Content());
        VisitChildren(node);
    }
};

bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ===========================================================================
// is-a
// ===========================================================================

TEST(CSharp_ErrorExpression, IsExpressionAndAstNodeNotStatementOrAstType) {
    ErrorExpression e;
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

TEST(CSharp_ErrorExpression, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<ErrorExpression>);
    EXPECT_TRUE(std::is_final_v<ErrorExpression>);
}

// ===========================================================================
// Construction
// ===========================================================================

TEST(CSharp_ErrorExpression, DefaultCtorLocationIsEmpty) {
    ErrorExpression e;
    EXPECT_EQ(e.Location(), TextLocation::Empty);
    EXPECT_EQ(e.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(e.EndLocation(), TextLocation::Empty);
}

TEST(CSharp_ErrorExpression, DefaultCtorHasNoTrailingTrivia) {
    ErrorExpression e;
    EXPECT_TRUE(e.TrailingTrivia().empty());
    EXPECT_TRUE(e.LeadingTrivia().empty());
}

TEST(CSharp_ErrorExpression, StringCtorAttachesTrailingMultiLineComment) {
    ErrorExpression e(std::string("missing ';'"));
    // The error text is attached as a trailing multi-line Comment (the hand-written ctor).
    auto trailing = e.TrailingTrivia();
    ASSERT_EQ(trailing.size(), 1u);
    EXPECT_TRUE(e.LeadingTrivia().empty());
    auto* c = dynamic_cast<Comment*>(trailing[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->Content(), "missing ';'");
    EXPECT_EQ(c->CommentType(), CommentType::MultiLine);
    EXPECT_EQ(c->Parent(), &e);
}

// ===========================================================================
// Location / StartLocation / EndLocation (zero-width point span)
// ===========================================================================

TEST(CSharp_ErrorExpression, LocationSetGetRoundTrip) {
    ErrorExpression e;
    e.Location(TextLocation(3, 7));
    EXPECT_EQ(e.Location(), TextLocation(3, 7));
    EXPECT_EQ(e.StartLocation(), TextLocation(3, 7));
}

TEST(CSharp_ErrorExpression, EndLocationEqualsLocationZeroWidthPoint) {
    // `StartLocation` == `EndLocation` == `Location` (a zero-width point, distinct from
    // `EmptyStatement` D259 whose `EndLocation` is `Location + 1`).
    ErrorExpression e;
    e.Location(TextLocation(3, 7));
    EXPECT_EQ(e.EndLocation(), TextLocation(3, 7));
    EXPECT_EQ(e.EndLocation(), e.StartLocation());
}

TEST(CSharp_ErrorExpression, DefaultEndLocationIsEmptyNotOneColumnPast) {
    // A default-constructed node reports `EndLocation` == `Empty` (NOT `Empty + 1`).
    ErrorExpression e;
    EXPECT_EQ(e.EndLocation(), TextLocation::Empty);
    EXPECT_NE(e.EndLocation(), TextLocation(TextLocation::Empty.Line, TextLocation::Empty.Column + 1));
}

// ===========================================================================
// Slot storage (inherited zero-child defaults)
// ===========================================================================

TEST(CSharp_ErrorExpression, HasNoSlotChildren) {
    ErrorExpression e;
    EXPECT_EQ(e.GetChildCount(), 0);
    EXPECT_EQ(e.FirstChild(), nullptr);
    EXPECT_FALSE(e.HasChildren());
}

TEST(CSharp_ErrorExpression, GetChildThrowsOutOfRange) {
    ErrorExpression e;
    EXPECT_THROW(static_cast<void>(e.GetChild(0)), std::out_of_range);
}

TEST(CSharp_ErrorExpression, GetCollectionByKindReturnsNullForEveryKind) {
    ErrorExpression e;
    EXPECT_EQ(e.GetCollectionByKind(nullptr), nullptr);
}

// ===========================================================================
// AcceptVisitor dispatch
// ===========================================================================

TEST(CSharp_ErrorExpression, AcceptVisitorDispatchesToVisitErrorExpression) {
    ErrorExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "error");
}

TEST(CSharp_ErrorExpression, AcceptVisitorIsVirtualThroughAstNode) {
    ErrorExpression e;
    AstNode* asNode = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "error");
}

TEST(CSharp_ErrorExpression, AcceptVisitorIsVirtualThroughExpression) {
    ErrorExpression e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asExpr->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "error");
}

// ===========================================================================
// Depth-first walk
// ===========================================================================

TEST(CSharp_ErrorExpression, DepthFirstRecordsJustSelfNoSlotChildren) {
    // No `[Slot]` children, so the default depth-first walk records just the node. The trailing
    // `Comment` (when present) is reached via `TrailingTrivia()`, NOT the slot-storage walk.
    ErrorExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "error");
}

TEST(CSharp_ErrorExpression, DepthFirstIgnoresTrailingComment) {
    // Even with a trailing `Comment` (the hand-written ctor), the default depth-first walk
    // records just the node: trivia is NOT in the slot storage, so `VisitChildren` skips it.
    ErrorExpression e(std::string("err"));
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "error");
    // The trailing Comment IS reachable via TrailingTrivia.
    ASSERT_EQ(e.TrailingTrivia().size(), 1u);
}

// ===========================================================================
// DoMatch (type-only)
// ===========================================================================

TEST(CSharp_ErrorExpression, DoMatchMatchesSameType) {
    ErrorExpression a;
    ErrorExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ErrorExpression, DoMatchRejectsDifferentType) {
    ErrorExpression a;
    NullReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ErrorExpression, DoMatchRejectsNullCandidate) {
    ErrorExpression a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ===========================================================================
// Clone
// ===========================================================================

TEST(CSharp_ErrorExpression, CloneCopiesLocation) {
    // `ErrorExpression` derives `EndLocation`, so its `Clone` copies `StartLocation` (the
    // Identifier/PrimitiveType/EmptyStatement derives-EndLocation-copies-StartLocation
    // precedent). The derived `EndLocation` is the stored `StartLocation` (zero-width point).
    ErrorExpression e;
    e.Location(TextLocation(5, 12));
    std::unique_ptr<ErrorExpression> copy(e.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &e);
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Location(), TextLocation(5, 12));
    EXPECT_EQ(copy->StartLocation(), TextLocation(5, 12));
    EXPECT_EQ(copy->EndLocation(), TextLocation(5, 12));
}

TEST(CSharp_ErrorExpression, CloneIsVirtualAndCovariant) {
    ErrorExpression e;
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<ErrorExpression*>(nodeCopy.get()), nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(dynamic_cast<ErrorExpression*>(exprCopy.get()), nullptr);
}

TEST(CSharp_ErrorExpression, CloneDeepCopiesTrailingComment) {
    // The hand-written ctor's trailing `Comment` is deep-copied by `Clone` (the `CloneAnnotationsFrom`
    // + `ReparentTrivia` combo deep-copies the `NodeTrivia` holder's trivia lists). The clone's
    // trailing `Comment` is a DISTINCT node (not the source's), re-parented to the clone.
    ErrorExpression e(std::string("boom"));
    ASSERT_EQ(e.TrailingTrivia().size(), 1u);
    auto* srcComment = e.TrailingTrivia()[0];

    std::unique_ptr<ErrorExpression> copy(e.Clone());
    ASSERT_NE(copy, nullptr);
    auto trailing = copy->TrailingTrivia();
    ASSERT_EQ(trailing.size(), 1u);
    auto* c = dynamic_cast<Comment*>(trailing[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_NE(c, srcComment);           // a distinct deep copy, not the source's trivia
    EXPECT_EQ(c->Content(), "boom");
    EXPECT_EQ(c->CommentType(), CommentType::MultiLine);
    EXPECT_EQ(c->Parent(), copy.get()); // re-parented to the clone
}

// ===========================================================================
// CheckInvariant
// ===========================================================================

TEST(CSharp_ErrorExpression, CheckInvariantPasses) {
    ErrorExpression e;
    e.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_ErrorExpression, CheckInvariantPassesOnStringCtorNode) {
    ErrorExpression e(std::string("err"));
    e.CheckInvariant();
    SUCCEED();
}
