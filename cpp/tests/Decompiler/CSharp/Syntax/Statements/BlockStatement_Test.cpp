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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for the `BlockStatement` concrete node (cpp/.../Syntax/Statements/BlockStatement.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/Statements/BlockStatement.cs) -- the first
// collection-bearing C# AST statement node: a non-sealed `Statement` whose sole child slot is
// the `Statements` `AstNodeCollection<Statement>` collection (the `block ::= '{' statement* '}'`
// production, the statement list inside the braces). It is the collection-only
// `ArrayInitializerExpression` D250 shape applied to the Statement hierarchy (the only node with
// a collection slot and NO single child slot, in the Statement hierarchy) and the second
// non-sealed concrete node (its `PatternPlaceholder` subclass is deferred). Exercises the
// `Statements` collection, the collection-only slot-storage contract (an empty node reports
// `GetChildCount` 0), the `AcceptVisitor` dispatch, the generated `DoMatch` (a single collection
// `DoMatch` term), the per-concrete-node `Clone`, and the inherited `CheckInvariant` (which
// passes on the empty node -- there are no required single slots).

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete statement types, recursing via the inherited `VisitChildren` (the document-order
// walk). `VisitBlockStatement`/`VisitContinueStatement`/`VisitBreakStatement`/`VisitReturnStatement`/
// `VisitExpressionStatement`/`VisitNullReferenceExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-continue>"); return; }
        trace.push_back("continue");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-break>"); return; }
        trace.push_back("break");
        VisitChildren(node);
    }
    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-return>"); return; }
        trace.push_back("return");
        VisitChildren(node);
    }
    void VisitExpressionStatement(ExpressionStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-exprstmt>"); return; }
        trace.push_back("exprstmt");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-null>"); return; }
        trace.push_back("null");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `BlockStatement` is a `Statement` and an `AstNode` (the `dynamic_cast` is-a the slot system and
// the annotation channel use); it is NOT an `Expression` (the `Statement` and `Expression`
// hierarchies are disjoint, both deriving directly from `AstNode` -- the D254 disjoint-hierarchy
// discriminator applied to a collection-bearing statement).
TEST(CSharp_BlockStatement, IsStatementAndAstNodeNotExpression) {
    BlockStatement bs;
    EXPECT_NE(dynamic_cast<Statement*>(&bs), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&bs), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&bs), nullptr);
}

// `BlockStatement` is a concrete (non-abstract) class but NOT `final` (the C# is not sealed -- the
// generated `PatternPlaceholder` derives from it; the placeholder is deferred, but the class stays
// open for derivation). It is constructible directly.
TEST(CSharp_BlockStatement, IsConcreteAndNotFinal) {
    auto bs = std::make_unique<BlockStatement>();
    ASSERT_NE(bs, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(bs.get()), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no statements. `GetChildCount` is 0 (the node has no single slots -- the only
// slot is the `Statements` collection, which is empty -- the collection-only shape, like
// `ArrayInitializerExpression`).
TEST(CSharp_BlockStatement, EmptyCtorHasNoStatements) {
    BlockStatement bs;
    EXPECT_EQ(bs.Statements().Count(), 0);
    EXPECT_EQ(bs.GetChildCount(), 0);  // no single slots + 0 statements
    EXPECT_EQ(bs.StartLocation(), TextLocation::Empty);
}

// ---- The `Statements` collection --------------------------------------

// The collection starts empty (0 count); the node's child count is 0 (no single slots).
TEST(CSharp_BlockStatement, StatementsEmptyByDefault) {
    BlockStatement bs;
    EXPECT_EQ(bs.Statements().Count(), 0);
    EXPECT_EQ(bs.GetChildCount(), 0);
}

// `Add` appends a statement, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last/only slot, so an element's index is
// its local position -- baseIndex 0).
TEST(CSharp_BlockStatement, StatementsAddAppendsAndParentsIncremental) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    auto b = std::make_unique<BreakStatement>();
    bs.Statements().Add(a.get());
    bs.Statements().Add(b.get());
    EXPECT_EQ(bs.Statements().Count(), 2);
    EXPECT_EQ(a->Parent(), &bs);
    EXPECT_EQ(b->Parent(), &bs);
    EXPECT_EQ(a->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(b->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(bs.GetChildCount(), 2);  // 2 statements, no single slots
    EXPECT_TRUE(bs.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Statements` collection for the `Statement` kind (the new
// shared `Slots::Statement`) and null for any other kind.
TEST(CSharp_BlockStatement, GetCollectionByKindReturnsStatementsForStatementKind) {
    BlockStatement bs;
    EXPECT_NE(bs.GetCollectionByKind(&Slots::Statement), nullptr);
    EXPECT_EQ(bs.GetCollectionByKind(&Slots::Statement), &bs.Statements());
    EXPECT_EQ(bs.GetCollectionByKind(&Slots::Expression), nullptr);  // a different kind
    EXPECT_EQ(bs.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-only dispatch) -----------

// `GetChild` returns the statements at index 0..Count; the collection occupies the contiguous
// range [0, Count). An empty node throws for any index (there are no slots at all).
TEST(CSharp_BlockStatement, GetChildDispatchesStatements) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    auto b = std::make_unique<BreakStatement>();
    bs.Statements().Add(a.get());
    bs.Statements().Add(b.get());
    EXPECT_EQ(bs.GetChild(0), a.get());
    EXPECT_EQ(bs.GetChild(1), b.get());
    EXPECT_THROW(bs.GetChild(2), std::out_of_range);
    EXPECT_THROW(bs.GetChild(-1), std::out_of_range);
}

// An empty node throws for every index (no single slots, no statements).
TEST(CSharp_BlockStatement, GetChildThrowsOnEmptyNode) {
    BlockStatement bs;
    EXPECT_THROW(bs.GetChild(0), std::out_of_range);
}

// `GetChildSlotInfo` returns the `StatementsSlot` at index 0..Count (the slot identity the slot
// system compares by address).
TEST(CSharp_BlockStatement, GetChildSlotInfoDispatchesStatements) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    EXPECT_EQ(bs.GetChildSlotInfo(0), &BlockStatement::StatementsSlot);
    EXPECT_THROW(bs.GetChildSlotInfo(1), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Statement` KIND (the new shared `Slots.Statement`).
TEST(CSharp_BlockStatement, StatementsSlotPointsAtStatementKind) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    EXPECT_EQ(bs.GetChildSlotInfo(0)->Kind(), &Slots::Statement);
}

// `SetChild` replaces a statement in place at index 0..Count (the collection's `SetAt` re-parents
// and carries the old index).
TEST(CSharp_BlockStatement, SetChildDispatchesStatements) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    // Replace the statement at index 0.
    auto b = std::make_unique<BreakStatement>();
    bs.SetChild(0, b.get());
    EXPECT_EQ(bs.Statements().At(0), b.get());
    EXPECT_EQ(b->Parent(), &bs);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetAt
    EXPECT_THROW(bs.SetChild(1, nullptr), std::out_of_range);  // no statement at index 1
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitBlockStatement` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Statements` children (a `VisitContinueStatement`/
// `VisitBreakStatement` per element).
TEST(CSharp_BlockStatement, AcceptVisitorDispatchesToVisitBlockStatement) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    RecordingVisitor v;
    bs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"block", "continue"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / a `Statement*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_BlockStatement, AcceptVisitorIsVirtualThroughBases) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    AstNode* asAst = &bs;
    Statement* asStmt = &bs;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asStmt->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"block", "continue"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"block", "continue"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits each `Statements` child in document order. A block with a
// nested `ReturnStatement` (itself holding an `Expression`) recurses into the operand.
TEST(CSharp_BlockStatement, DepthFirstWalkVisitsStatementsInOrder) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    auto operand = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<ReturnStatement>();
    b->Expression(operand.get());  // return null;
    bs.Statements().Add(a.get());
    bs.Statements().Add(b.get());
    RecordingVisitor v;
    bs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "block", "continue", "return", "null"}));
}

// The depth-first walk of an empty block records just the node itself (no children).
TEST(CSharp_BlockStatement, DepthFirstWalkOfEmptyRecordsJustNode) {
    BlockStatement bs;
    RecordingVisitor v;
    bs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"block"}));
}

// ---- DoMatch (the generated collection match) ------------------------

// Two `BlockStatement`s with the same (single) statement match.
TEST(CSharp_BlockStatement, DoMatchMatchesSameStatements) {
    BlockStatement a;
    BlockStatement b;
    auto ax = std::make_unique<ContinueStatement>();
    auto bx = std::make_unique<ContinueStatement>();
    a.Statements().Add(ax.get());
    b.Statements().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two empty `BlockStatement`s match (the collection match succeeds when both counts are 0).
TEST(CSharp_BlockStatement, DoMatchMatchesTwoEmpties) {
    BlockStatement a;
    BlockStatement b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `BlockStatement`s with different statement COUNTS do not match (the collection match fails
// when the counts differ).
TEST(CSharp_BlockStatement, DoMatchRejectsDifferentStatementCount) {
    BlockStatement a;
    BlockStatement b;
    auto ax = std::make_unique<ContinueStatement>();
    a.Statements().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 statement, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `BlockStatement`s with the same count but different statement VALUES do not match (the
// element `DoMatch` -- a type-only match on the leaf statement -- rejects: a `ContinueStatement`
// does not match a `BreakStatement`).
TEST(CSharp_BlockStatement, DoMatchRejectsDifferentStatementValue) {
    BlockStatement a;
    BlockStatement b;
    auto ax = std::make_unique<ContinueStatement>();
    auto bx = std::make_unique<BreakStatement>();
    a.Statements().Add(ax.get());
    b.Statements().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `BlockStatement` does not match a different concrete type (the `other is BlockStatement`
// gate). An `ArrayInitializerExpression` is also a collection-only node but in the disjoint
// `Expression` hierarchy, so the type-check gate rejects -- the cross-hierarchy structural-twin
// rejection (the D237 disjoint-hierarchy discriminator applied to two collection-only nodes).
TEST(CSharp_BlockStatement, DoMatchRejectsArrayInitializerExpressionCandidate) {
    BlockStatement bs;
    ArrayInitializerExpression aie;
    EXPECT_FALSE(DoMatchAgainst(&bs, &aie));
    EXPECT_FALSE(DoMatchAgainst(&aie, &bs));
}

// A `BlockStatement` does not match a `ReturnStatement` candidate (the type-check gate rejects).
TEST(CSharp_BlockStatement, DoMatchRejectsReturnStatementCandidate) {
    BlockStatement bs;
    ReturnStatement rs;
    EXPECT_FALSE(DoMatchAgainst(&bs, &rs));
    EXPECT_FALSE(DoMatchAgainst(&rs, &bs));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_BlockStatement, DoMatchRejectsNullCandidate) {
    BlockStatement bs;
    EXPECT_FALSE(DoMatchAgainst(&bs, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the statements, re-parents the clones, and detaches from the source.
TEST(CSharp_BlockStatement, CloneDeepCopiesStatements) {
    BlockStatement original;
    auto a = std::make_unique<ContinueStatement>();
    original.Statements().Add(a.get());

    std::unique_ptr<BlockStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The statement is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Statements().Count(), 1);
    ASSERT_NE(copy->Statements().At(0), nullptr);
    EXPECT_NE(copy->Statements().At(0), a.get());
    EXPECT_NE(dynamic_cast<ContinueStatement*>(copy->Statements().At(0)), nullptr);
    EXPECT_EQ(copy->Statements().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Statements().Count(), 1);
    EXPECT_EQ(a->Parent(), &original);
}

// `Clone` recurses into nested statements: a `ReturnStatement` operand is deep-cloned too.
TEST(CSharp_BlockStatement, CloneDeepCopiesNestedStatements) {
    BlockStatement original;
    auto operand = std::make_unique<NullReferenceExpression>();
    auto ret = std::make_unique<ReturnStatement>();
    ret->Expression(operand.get());
    original.Statements().Add(ret.get());

    std::unique_ptr<BlockStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Statements().Count(), 1);
    auto* retCopy = dynamic_cast<ReturnStatement*>(copy->Statements().At(0));
    ASSERT_NE(retCopy, nullptr);
    ASSERT_NE(retCopy->Expression(), nullptr);
    EXPECT_NE(retCopy->Expression(), operand.get());
    EXPECT_NE(dynamic_cast<NullReferenceExpression*>(retCopy->Expression()), nullptr);
    EXPECT_EQ(retCopy->Expression()->Parent(), retCopy);
    // The original is unchanged.
    EXPECT_EQ(ret->Expression(), operand.get());
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through a `Statement*` (returns a `Statement*`).
TEST(CSharp_BlockStatement, CloneIsVirtualAndCovariant) {
    BlockStatement original;
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<BlockStatement*>(astCopy.get()), nullptr);
    Statement* asStmt = &original;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    ASSERT_NE(stmtCopy, nullptr);
    EXPECT_NE(dynamic_cast<BlockStatement*>(stmtCopy.get()), nullptr);
}

// `Clone` of an empty `BlockStatement` yields an empty clone.
TEST(CSharp_BlockStatement, CloneOfEmptyIsEmpty) {
    BlockStatement original;  // no statements
    std::unique_ptr<BlockStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Statements().Count(), 0);
    EXPECT_EQ(copy->GetChildCount(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `BlockStatement` with statements passes the inherited `CheckInvariant` (the slot-structure
// verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in debug builds (a no-op
// in NDEBUG).
TEST(CSharp_BlockStatement, CheckInvariantPassesOnFilledNode) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    auto b = std::make_unique<BreakStatement>();
    bs.Statements().Add(a.get());
    bs.Statements().Add(b.get());
    bs.CheckInvariant();
}

// An EMPTY `BlockStatement` passes `CheckInvariant` (the node has no required single slots -- the
// only slot is the `Statements` collection, which has no required-slot invariant), the
// collection-only shape where the empty node is invariant-valid (the `ArrayInitializerExpression`
// D250 precedent applied to the Statement hierarchy).
TEST(CSharp_BlockStatement, CheckInvariantPassesOnEmptyNode) {
    BlockStatement bs;
    bs.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `StatementsSlot` is the node's only slot static; the node's `Slot()` reports it for each
// child. The slot's `Kind` is the new shared `Slots::Statement`.
TEST(CSharp_BlockStatement, StatementsSlotIsStatementKind) {
    BlockStatement bs;
    auto a = std::make_unique<ContinueStatement>();
    bs.Statements().Add(a.get());
    EXPECT_EQ(bs.Statements().At(0)->Slot(), &BlockStatement::StatementsSlot);
    EXPECT_EQ(bs.Statements().At(0)->Slot()->Kind(), &Slots::Statement);
    EXPECT_TRUE(bs.Statements().At(0)->Slot()->IsCollection());
}
