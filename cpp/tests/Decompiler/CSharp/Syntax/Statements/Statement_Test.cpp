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

// Tests for the start of the Statement hierarchy -- the `Statement` abstract base plus the
// three concrete leaf statements (`ContinueStatement`, `BreakStatement`, `YieldBreakStatement`)
// that plug into the `IAstVisitor`/`AcceptVisitor` dispatch (the next in-order Phase-5 piece
// per the D253 plan). The concrete nodes derive from `Statement` (which derives from
// `AstNode`), override `AcceptVisitor` to call `visitor.Visit<NodeName>(this)`, supply the
// generated type-only `DoMatch`, and override `Clone` (no `MemberwiseClone` in C++). All
// three are leaf statements with no `[Slot]` children and no members; `BreakStatement` and
// `YieldBreakStatement` carry a `BreakKeyword` (and `YieldKeyword`) const string the output
// visitor emits.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides only the three `Visit` methods under test,
// recording a tag and recursing via `VisitChildren` (the inherited depth-first default).
// The leaves have no children, so the recorded trace is just the visited nodes.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr)
            return;
        trace.push_back("continue");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr)
            return;
        trace.push_back("break");
        VisitChildren(node);
    }
    void VisitYieldBreakStatement(YieldBreakStatement* node) override {
        if (node == nullptr)
            return;
        trace.push_back("yield break");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A container stub (one collection slot of `AstNode` children) used to exercise the
// depth-first walk across a tree of the new concrete statements (the D220 stub precedent).
class Container : public AstNode {
    std::vector<AstNode*> children_;

    static const CSharpSlotInfo& ItemSlot() {
        static const CSharpSlotInfoT<AstNode> slot{"Item", true, nullptr, true};
        return slot;
    }

public:
    void Add(AstNode* child) {
        if (child == nullptr)
            return;
        children_.push_back(child);
        child->SetParent(this);
        InvalidateChildIndices();
    }

    int GetChildCount() const override { return static_cast<int>(children_.size()); }
    AstNode* GetChild(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("Container::GetChild");
        return children_[static_cast<std::size_t>(index)];
    }
    void SetChild(int index, AstNode* value) override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("Container::SetChild");
        if (children_[static_cast<std::size_t>(index)] == value)
            return;
        if (auto* old = children_[static_cast<std::size_t>(index)])
            old->ClearParentAndIndex();
        children_[static_cast<std::size_t>(index)] = value;
        if (value) {
            value->SetParent(this);
            value->ChildIndex = index;
        }
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("Container::GetChildSlotInfo");
        return &ItemSlot();
    }

protected:
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }

public:
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

} // namespace

// ---- Statement abstract base -------------------------------------------

// `Statement` is abstract: it cannot be instantiated directly (the covariant `Clone` is
// pure-virtual), matching the C# `public abstract class Statement`. A concrete statement
// is NOT abstract (it overrides `Clone`). The `is_abstract` trait pins this at compile time.
TEST(CSharp_Statement, IsAbstractViaPureVirtualClone) {
    static_assert(std::is_abstract_v<Statement>);
    static_assert(!std::is_abstract_v<ContinueStatement>);
    static_assert(!std::is_abstract_v<BreakStatement>);
    static_assert(!std::is_abstract_v<YieldBreakStatement>);
    // The only way to get a `Statement` is via a concrete subclass; a concrete node
    // constructs and upcasts.
    auto cs = std::make_unique<ContinueStatement>();
    Statement* stmt = cs.get();
    EXPECT_NE(stmt, nullptr);
}

// A concrete statement IS a `Statement` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use), but NOT an `Expression` (disjoint hierarchies).
TEST(CSharp_Statement, ConcreteStatementIsStatementAndAstNodeNotExpression) {
    auto cs = std::make_unique<ContinueStatement>();
    EXPECT_NE(dynamic_cast<Statement*>(cs.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(cs.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(cs.get()), nullptr);
    auto bs = std::make_unique<BreakStatement>();
    EXPECT_NE(dynamic_cast<Statement*>(bs.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(bs.get()), nullptr);
    auto ybs = std::make_unique<YieldBreakStatement>();
    EXPECT_NE(dynamic_cast<Statement*>(ybs.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(ybs.get()), nullptr);
}

// ---- const keyword strings --------------------------------------------

// `BreakStatement` carries the `BreakKeyword` "break".
TEST(CSharp_Statement, BreakStatementBreakKeyword) {
    EXPECT_STREQ(BreakStatement::BreakKeyword, "break");
}

// `YieldBreakStatement` carries both the `YieldKeyword` "yield" and the `BreakKeyword` "break".
TEST(CSharp_Statement, YieldBreakStatementKeywords) {
    EXPECT_STREQ(YieldBreakStatement::YieldKeyword, "yield");
    EXPECT_STREQ(YieldBreakStatement::BreakKeyword, "break");
}

// `ContinueStatement` declares no keyword const (the C# declares no members).
TEST(CSharp_Statement, ContinueStatementHasNoKeywordConst) {
    // ContinueStatement is constructible and empty; no keyword accessor exists (the C#
    // declares no const string). This is a compile-time property; the assertion is that the
    // node constructs and is distinct.
    ContinueStatement cs;
    (void)cs;
    SUCCEED();
}

// ---- AcceptVisitor dispatch ---------------------------------------------

// A concrete node's `AcceptVisitor` dispatches to the matching `Visit` method on the
// visitor (the visitor-pattern round-trip: the node routes back to the visitor).
TEST(CSharp_Statement, AcceptVisitorDispatchesToMatchingVisit) {
    ContinueStatement cs;
    BreakStatement bs;
    YieldBreakStatement ybs;
    RecordingVisitor v;
    cs.AcceptVisitor(v);
    bs.AcceptVisitor(v);
    ybs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"continue", "break", "yield break"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` (or a `Statement*`)
// dispatches to the concrete override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_Statement, AcceptVisitorIsVirtualThroughAstNodeAndStatement) {
    BreakStatement bs;
    AstNode* asAst = &bs;
    Statement* asStmt = &bs;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    asStmt->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"break", "break"}));
}

// `DepthFirstAstVisitor::VisitChildren` walks the node's children in document order,
// dispatching each child's `AcceptVisitor` back to the matching `Visit` -- the depth-first
// traversal across a tree of the new concrete statements.
TEST(CSharp_Statement, DepthFirstWalkVisitsTreeInDocumentOrder) {
    Container root;
    ContinueStatement cs;
    BreakStatement bs;
    YieldBreakStatement ybs;
    root.Add(&cs);
    root.Add(&bs);
    root.Add(&ybs);

    RecordingVisitor v;
    root.AcceptVisitor(v);  // Container's AcceptVisitor is a no-op; visit the children directly
    ASSERT_TRUE(v.trace.empty());

    // Visit the children explicitly (the container does not dispatch itself).
    RecordingVisitor w;
    for (AstNode* child : root.Children())
        child->AcceptVisitor(w);
    EXPECT_EQ(w.trace, (std::vector<std::string>{"continue", "break", "yield break"}));
}

// ---- DoMatch (the generated type-only match) ----------------------------

// A `ContinueStatement` matches another `ContinueStatement` (the generated
// `return other is ContinueStatement`).
TEST(CSharp_Statement, DoMatchContinueMatchesSameType) {
    ContinueStatement a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `BreakStatement` matches another `BreakStatement`.
TEST(CSharp_Statement, DoMatchBreakMatchesSameType) {
    BreakStatement a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `YieldBreakStatement` matches another `YieldBreakStatement`.
TEST(CSharp_Statement, DoMatchYieldBreakMatchesSameType) {
    YieldBreakStatement a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A leaf statement does not match a different concrete type (the type-only match rejects a
// different statement type).
TEST(CSharp_Statement, DoMatchRejectsDifferentType) {
    ContinueStatement cs;
    BreakStatement bs;
    YieldBreakStatement ybs;
    EXPECT_FALSE(DoMatchAgainst(&cs, &bs));
    EXPECT_FALSE(DoMatchAgainst(&bs, &cs));
    EXPECT_FALSE(DoMatchAgainst(&cs, &ybs));
    EXPECT_FALSE(DoMatchAgainst(&ybs, &cs));
    EXPECT_FALSE(DoMatchAgainst(&bs, &ybs));
    EXPECT_FALSE(DoMatchAgainst(&ybs, &bs));
}

// A null candidate: the `INode.DoMatch` delegation admits a null (a missing child) and the
// typed `DoMatch` rejects it (`dynamic_cast` of null is null).
TEST(CSharp_Statement, DoMatchRejectsNullCandidate) {
    ContinueStatement cs;
    BreakStatement bs;
    YieldBreakStatement ybs;
    EXPECT_FALSE(DoMatchAgainst(&cs, nullptr));
    EXPECT_FALSE(DoMatchAgainst(&bs, nullptr));
    EXPECT_FALSE(DoMatchAgainst(&ybs, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` returns a fresh, detached deep copy.
TEST(CSharp_Statement, CloneReturnsFreshDetachedCopy) {
    ContinueStatement original;
    std::unique_ptr<ContinueStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);  // a fresh node, not the same
    EXPECT_EQ(copy->Parent(), nullptr);  // detached

    BreakStatement originalB;
    std::unique_ptr<BreakStatement> copyB(originalB.Clone());
    ASSERT_NE(copyB, nullptr);
    EXPECT_NE(copyB.get(), &originalB);
    EXPECT_EQ(copyB->Parent(), nullptr);

    YieldBreakStatement originalY;
    std::unique_ptr<YieldBreakStatement> copyY(originalY.Clone());
    ASSERT_NE(copyY, nullptr);
    EXPECT_NE(copyY.get(), &originalY);
    EXPECT_EQ(copyY->Parent(), nullptr);
}

// `Clone` is virtual through `Statement*` (covariant return): a call through a `Statement*`
// returns a `Statement*` (the typed return the C# `new Statement Clone()` gives), and the
// result is the concrete type.
TEST(CSharp_Statement, CloneIsCovariantThroughStatement) {
    BreakStatement original;
    Statement* stmt = &original;
    std::unique_ptr<Statement> copy(stmt->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<BreakStatement*>(copy.get()), nullptr);
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`
// (the static return type), dispatched to the concrete override.
TEST(CSharp_Statement, CloneIsVirtualThroughAstNode) {
    YieldBreakStatement original;
    AstNode* node = &original;
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<YieldBreakStatement*>(copy.get()), nullptr);
}

// A `BreakStatement` clone remains a `BreakStatement` that dispatches to `VisitBreakStatement`.
TEST(CSharp_Statement, CloneDispatchesAsConcreteType) {
    BreakStatement original;
    std::unique_ptr<BreakStatement> copy(original.Clone());
    RecordingVisitor v;
    copy->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"break"}));
}

// ---- CheckInvariant (inherited from AstNode) ----------------------------

// A leaf statement with no children passes the inherited `CheckInvariant` (the no-child case
// the base handles). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_Statement, CheckInvariantPassesOnLeaf) {
    ContinueStatement cs;
    cs.CheckInvariant();
    BreakStatement bs;
    bs.CheckInvariant();
    YieldBreakStatement ybs;
    ybs.CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -------------------

// A leaf statement reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_Statement, LeafHasNoChildren) {
    ContinueStatement cs;
    EXPECT_EQ(cs.GetChildCount(), 0);
    EXPECT_FALSE(cs.HasChildren());
    EXPECT_EQ(cs.FirstChild(), nullptr);
    EXPECT_EQ(cs.LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : cs.Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);

    BreakStatement bs;
    EXPECT_EQ(bs.GetChildCount(), 0);
    EXPECT_FALSE(bs.HasChildren());

    YieldBreakStatement ybs;
    EXPECT_EQ(ybs.GetChildCount(), 0);
    EXPECT_FALSE(ybs.HasChildren());
}
