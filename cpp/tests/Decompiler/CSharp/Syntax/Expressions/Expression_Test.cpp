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

// Tests for the first slice of the generated C# AST node hierarchy -- the `Expression`
// abstract base plus the three concrete leaf expressions (`NullReferenceExpression`,
// `ThisReferenceExpression`, `BaseReferenceExpression`) that plug into the
// `IAstVisitor`/`AcceptVisitor` dispatch (the next in-order Phase-5 piece per the D225
// plan). The concrete nodes derive from `Expression` (which derives from `AstNode`),
// override `AcceptVisitor` to call `visitor.Visit<NodeName>(this)`, supply the generated
// type-only `DoMatch`, and override `Clone` (no `MemberwiseClone` in C++).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides only the three `Visit` methods under test,
// recording the node type (the role the generated per-node `Visit` overrides play). Each
// recurses via `VisitChildren` (the inherited depth-first default), so the recorded trace is
// the pre-order walk; the leaves have no children, so the trace is just the visited nodes.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("null");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("this");
        VisitChildren(node);
    }
    void VisitBaseReferenceExpression(BaseReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("base");
        VisitChildren(node);
    }
};

// A container stub (one collection slot of `AstNode` children) used to exercise the
// depth-first walk across a tree of the new concrete expressions (the D220 stub precedent).
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
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

} // namespace

// ---- Expression abstract base -------------------------------------------

// `Expression` is abstract: it cannot be instantiated directly (the covariant `Clone` is
// pure-virtual), matching the C# `public abstract class Expression`. A concrete expression
// is NOT abstract (it overrides `Clone`). The `is_abstract` trait pins this at compile time.
TEST(CSharp_Expression, IsAbstractViaPureVirtualClone) {
    static_assert(std::is_abstract_v<Expression>);
    static_assert(!std::is_abstract_v<NullReferenceExpression>);
    static_assert(!std::is_abstract_v<ThisReferenceExpression>);
    static_assert(!std::is_abstract_v<BaseReferenceExpression>);
    // The only way to get an `Expression` is via a concrete subclass; a concrete node
    // constructs and upcasts.
    auto nre = std::make_unique<NullReferenceExpression>();
    Expression* expr = nre.get();
    EXPECT_NE(expr, nullptr);
}

// A concrete expression IS an `Expression` and an `AstNode` (the `dynamic_cast` is-a the
// slot system and the annotation channel use).
TEST(CSharp_Expression, ConcreteExpressionIsExpressionAndAstNode) {
    auto nre = std::make_unique<NullReferenceExpression>();
    EXPECT_NE(dynamic_cast<Expression*>(nre.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(nre.get()), nullptr);
    auto tre = std::make_unique<ThisReferenceExpression>();
    EXPECT_NE(dynamic_cast<Expression*>(tre.get()), nullptr);
    auto bre = std::make_unique<BaseReferenceExpression>();
    EXPECT_NE(dynamic_cast<Expression*>(bre.get()), nullptr);
}

// ---- EndLocation (the keyword span) ------------------------------------

// `NullReferenceExpression` derives `EndLocation` from `StartLocation` spanning `"null"` (4).
TEST(CSharp_Expression, NullReferenceExpressionEndLocationSpansNull) {
    NullReferenceExpression nre(TextLocation(3, 5));
    EXPECT_EQ(nre.StartLocation(), TextLocation(3, 5));
    EXPECT_EQ(nre.EndLocation(), TextLocation(3, 9));
}

// `ThisReferenceExpression` spans `"this"` (4).
TEST(CSharp_Expression, ThisReferenceExpressionEndLocationSpansThis) {
    ThisReferenceExpression tre(TextLocation(1, 10));
    EXPECT_EQ(tre.StartLocation(), TextLocation(1, 10));
    EXPECT_EQ(tre.EndLocation(), TextLocation(1, 14));
}

// `BaseReferenceExpression` spans `"base"` (4).
TEST(CSharp_Expression, BaseReferenceExpressionEndLocationSpansBase) {
    BaseReferenceExpression bre(TextLocation(2, 7));
    EXPECT_EQ(bre.StartLocation(), TextLocation(2, 7));
    EXPECT_EQ(bre.EndLocation(), TextLocation(2, 11));
}

// ---- AcceptVisitor dispatch ---------------------------------------------

// A concrete node's `AcceptVisitor` dispatches to the matching `Visit` method on the
// visitor (the visitor-pattern round-trip: the node routes back to the visitor).
TEST(CSharp_Expression, AcceptVisitorDispatchesToMatchingVisit) {
    NullReferenceExpression nre;
    ThisReferenceExpression tre;
    BaseReferenceExpression bre;
    RecordingVisitor v;
    nre.AcceptVisitor(v);
    tre.AcceptVisitor(v);
    bre.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"null", "this", "base"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` (or an `Expression*`)
// dispatches to the concrete override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_Expression, AcceptVisitorIsVirtualThroughAstNodeAndExpression) {
    NullReferenceExpression nre;
    AstNode* asAst = &nre;
    Expression* asExpr = &nre;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    asExpr->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"null", "null"}));
}

// `DepthFirstAstVisitor::VisitChildren` walks the node's children in document order,
// dispatching each child's `AcceptVisitor` back to the matching `Visit` -- the depth-first
// traversal across a tree of the new concrete expressions.
TEST(CSharp_Expression, DepthFirstWalkVisitsTreeInDocumentOrder) {
    Container root;
    NullReferenceExpression nre;
    ThisReferenceExpression tre;
    BaseReferenceExpression bre;
    root.Add(&nre);
    root.Add(&tre);
    root.Add(&bre);

    RecordingVisitor v;
    root.AcceptVisitor(v);  // Container's AcceptVisitor is a no-op; visit the children directly
    ASSERT_TRUE(v.trace.empty());

    // Visit the children explicitly (the container does not dispatch itself).
    RecordingVisitor w;
    for (AstNode* child : root.Children())
        child->AcceptVisitor(w);
    EXPECT_EQ(w.trace, (std::vector<std::string>{"null", "this", "base"}));
}

// ---- DoMatch (the generated type-only match) ----------------------------

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation
// (the protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A `NullReferenceExpression` matches another `NullReferenceExpression` (the generated
// `return other is NullReferenceExpression`).
TEST(CSharp_Expression, DoMatchMatchesSameType) {
    NullReferenceExpression a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `NullReferenceExpression` does not match a `ThisReferenceExpression` (the type-only
// match rejects a different concrete type).
TEST(CSharp_Expression, DoMatchRejectsDifferentType) {
    NullReferenceExpression a;
    ThisReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `BaseReferenceExpression` does not match a `NullReferenceExpression`.
TEST(CSharp_Expression, DoMatchRejectsBaseVersusNull) {
    NullReferenceExpression nre;
    BaseReferenceExpression bre;
    EXPECT_FALSE(DoMatchAgainst(&bre, &nre));
    EXPECT_TRUE(DoMatchAgainst(&bre, &bre));
}

// A null candidate: the `INode.DoMatch` delegation admits a null (a missing child) and
// the typed `DoMatch` rejects it (`dynamic_cast` of null is null). The null case is reached
// when a pattern matches against an absent optional child.
TEST(CSharp_Expression, DoMatchRejectsNullCandidate) {
    NullReferenceExpression a;
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` returns a fresh, detached deep copy with the same print-time `StartLocation`.
TEST(CSharp_Expression, CloneCopiesStartLocationAndDetaches) {
    NullReferenceExpression original(TextLocation(5, 12));
    std::unique_ptr<NullReferenceExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);  // a fresh node, not the same
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_EQ(copy->StartLocation(), TextLocation(5, 12));
    EXPECT_EQ(copy->EndLocation(), TextLocation(5, 16));
}

// `Clone` is virtual through `Expression*` (covariant return): a call through an
// `Expression*` returns an `Expression*` (the typed return the C# `new Expression Clone()`
// gives), and the result is the concrete type.
TEST(CSharp_Expression, CloneIsCovariantThroughExpression) {
    ThisReferenceExpression original(TextLocation(2, 3));
    Expression* expr = &original;
    std::unique_ptr<Expression> copy(expr->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<ThisReferenceExpression*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(2, 3));
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`
// (the static return type), dispatched to the concrete override.
TEST(CSharp_Expression, CloneIsVirtualThroughAstNode) {
    BaseReferenceExpression original(TextLocation(4, 1));
    AstNode* node = &original;
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<BaseReferenceExpression*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(4, 1));
}

// ---- CheckInvariant (inherited from AstNode) ----------------------------

// A leaf expression with no children passes the inherited `CheckInvariant` (the no-child
// case the base handles). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_Expression, CheckInvariantPassesOnLeaf) {
    NullReferenceExpression nre;
    nre.CheckInvariant();
    ThisReferenceExpression tre;
    tre.CheckInvariant();
    BaseReferenceExpression bre;
    bre.CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -------------------

// A leaf expression reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_Expression, LeafHasNoChildren) {
    NullReferenceExpression nre;
    EXPECT_EQ(nre.GetChildCount(), 0);
    EXPECT_FALSE(nre.HasChildren());
    EXPECT_EQ(nre.FirstChild(), nullptr);
    EXPECT_EQ(nre.LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : nre.Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}
