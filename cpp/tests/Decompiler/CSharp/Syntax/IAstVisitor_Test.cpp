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

// Tests for the `IAstVisitor` / `AcceptVisitor` dispatch (IAstVisitor.hpp +
// DepthFirstAstVisitor.hpp + the abstract `AstNode::AcceptVisitor`) -- the dispatch half
// of the visitor pattern, the eighth in-order Phase-5 piece. The concrete C# AST node
// hierarchy is not yet ported (it lands next, per PORT_PLAN.md section 5.2 / decision D1),
// so the per-node `Visit<NodeName>` methods do not yet exist on `IAstVisitor`; the dispatch
// mechanism is exercised here with stub concrete nodes (the D220-D224 stub precedent) and a
// recording visitor whose test-local `Visit` methods play the role the generated per-node
// `Visit` overrides will play once the concrete nodes land (each calls `VisitChildren`,
// the depth-first default). The stub's `AcceptVisitor` downcasts the `IAstVisitor&` to the
// recording visitor via `dynamic_cast` (the stubs are test-local, so their `Visit` methods
// are not on `IAstVisitor`); the production concrete nodes will call
// `visitor.Visit<NodeName>(this)` directly (the `Visit` IS on `IAstVisitor`), the same
// dispatch shape.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;

namespace {

// Forward declarations: the stub concrete nodes are defined after the recording visitor
// (their `AcceptVisitor` downcasts to it), and the recording visitor's `Visit` methods
// take the stubs (forward-declared pointers suffice for the declarations).
class VisitorLeaf;
class VisitorParent;

// A recording depth-first visitor (the role the generated `DepthFirstAstVisitor` per-node
// `Visit` overrides play). Each `Visit` method records the node and recurses via
// `VisitChildren` (the protected depth-first default inherited from `DepthFirstAstVisitor`),
// so the recorded trace is the pre-order depth-first walk of the tree. The `Visit` methods
// are test-local (NOT on `IAstVisitor` -- the stub concrete nodes are test-local, so their
// `Visit` methods are not added to the interface); the production `Visit<NodeName>` methods
// will be added to `IAstVisitor` as the concrete node hierarchy lands.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitLeaf(VisitorLeaf* node);
    void VisitParent(VisitorParent* node);
};

// A leaf stub (no children). Its `AcceptVisitor` dispatches to the recording visitor's
// `VisitLeaf` -- the same shape the production concrete node's `AcceptVisitor` uses
// (`visitor.Visit<NodeName>(this)`), downcast to the test-local visitor since the stub is
// not in `IAstVisitor`.
class VisitorLeaf : public AstNode {
public:
    std::string Name;
    explicit VisitorLeaf(std::string n) : Name(std::move(n)) {}

    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }

    void AcceptVisitor(IAstVisitor& visitor) override {
        if (auto* rv = dynamic_cast<RecordingVisitor*>(&visitor))
            rv->VisitLeaf(this);
    }

    // The `<bool>`-variant dispatch entry (the new pure virtual on `AstNode`): this stub
    // is used only with the void visitor above, so the bool variant is a no-op returning
    // `false` (default(bool)); it is never exercised by this test.
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

// A container stub (one collection slot of `AstNode` children) used to exercise the
// depth-first walk. The slot-storage contract delegates into the owned `std::vector`, so
// `Children()` walks the children in document order (the order they were added).
class VisitorParent : public AstNode {
    std::vector<AstNode*> children_;

    static const CSharpSlotInfo& ItemSlot() {
        static const CSharpSlotInfoT<AstNode> slot{"Item", true, nullptr, true};
        return slot;
    }

public:
    std::string Name;
    explicit VisitorParent(std::string n) : Name(std::move(n)) {}

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
            throw std::out_of_range("VisitorParent::GetChild");
        return children_[static_cast<std::size_t>(index)];
    }
    void SetChild(int index, AstNode* value) override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("VisitorParent::SetChild");
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
            throw std::out_of_range("VisitorParent::GetChildSlotInfo");
        return &ItemSlot();
    }

    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }

    void AcceptVisitor(IAstVisitor& visitor) override {
        if (auto* rv = dynamic_cast<RecordingVisitor*>(&visitor))
            rv->VisitParent(this);
    }

    // The `<bool>`-variant dispatch entry (the new pure virtual on `AstNode`): this stub
    // is used only with the void visitor above, so the bool variant is a no-op returning
    // `false` (default(bool)); it is never exercised by this test.
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

// The `Visit` methods (defined after the stubs are complete): record the node, then recurse
// via `VisitChildren` (the protected depth-first default). A leaf has no children, so
// `VisitChildren` is a no-op for `VisitorLeaf`; a parent's `VisitChildren` walks its
// children in document order, calling `child->AcceptVisitor(*this)` on each, which
// dispatches back to the matching `Visit` method -- the depth-first traversal.
void RecordingVisitor::VisitLeaf(VisitorLeaf* node) {
    if (node == nullptr)
        return;
    trace.push_back("leaf:" + node->Name);
    VisitChildren(node);
}

void RecordingVisitor::VisitParent(VisitorParent* node) {
    if (node == nullptr)
        return;
    trace.push_back("parent:" + node->Name);
    VisitChildren(node);
}

} // namespace

// A concrete node's `AcceptVisitor` dispatches to the matching `Visit` method on the
// visitor (the visitor-pattern round-trip: the node routes back to the visitor).
TEST(CSharp_AstVisitor, AcceptVisitorDispatchesToVisitMethod) {
    VisitorLeaf leaf("a");
    RecordingVisitor v;
    leaf.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "leaf:a");
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on -- it holds `AstNode*` and
// calls `node->AcceptVisitor(*this)`).
TEST(CSharp_AstVisitor, AcceptVisitorIsVirtual) {
    VisitorLeaf leaf("a");
    AstNode* node = &leaf;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "leaf:a");
}

// `DepthFirstAstVisitor::VisitChildren` walks the node's children in document order,
// calling `AcceptVisitor` on each (which dispatches back to the matching `Visit` method),
// recursing depth-first: the recorded trace is the pre-order walk of the tree.
TEST(CSharp_AstVisitor, VisitChildrenWalksDepthFirstInDocumentOrder) {
    VisitorParent root("root");
    VisitorLeaf a("a"), b("b");
    VisitorParent mid("mid");
    VisitorLeaf c("c");
    // root -> [a, mid, b]; mid -> [c]
    root.Add(&a);
    root.Add(&mid);
    root.Add(&b);
    mid.Add(&c);

    RecordingVisitor v;
    root.AcceptVisitor(v);

    // Pre-order depth-first: root, a, mid, c, b.
    ASSERT_EQ(v.trace, (std::vector<std::string>{
        "parent:root", "leaf:a", "parent:mid", "leaf:c", "leaf:b"}));
}

// A leaf visited via `VisitChildren` (a parent with only leaves) records each child in
// the order it was added.
TEST(CSharp_AstVisitor, VisitChildrenWalksLeavesInOrder) {
    VisitorParent root("root");
    VisitorLeaf a("a"), b("b"), c("c");
    root.Add(&a);
    root.Add(&b);
    root.Add(&c);

    RecordingVisitor v;
    root.AcceptVisitor(v);

    ASSERT_EQ(v.trace, (std::vector<std::string>{
        "parent:root", "leaf:a", "leaf:b", "leaf:c"}));
}

// `VisitChildren` on a leaf (no children) records nothing beyond the leaf itself -- the
// `Children()` walk of a childless node is empty.
TEST(CSharp_AstVisitor, VisitChildrenOnLeafRecordsNoChildren) {
    VisitorLeaf leaf("solo");
    RecordingVisitor v;
    leaf.AcceptVisitor(v);
    ASSERT_EQ(v.trace, (std::vector<std::string>{"leaf:solo"}));
}

// `VisitChildren`'s null guard is a no-op: a `Visit` method called with a null node (the
// defensive case) records nothing and does not crash. The guard lives in `VisitChildren`
// (protected) and the test-local `Visit` helpers; the production `Visit<NodeName>` methods
// always receive a non-null concrete node, so this is a defensive path only.
TEST(CSharp_AstVisitor, VisitChildrenNullGuardIsNoOp) {
    RecordingVisitor v;
    v.VisitLeaf(nullptr);
    v.VisitParent(nullptr);
    EXPECT_TRUE(v.trace.empty());
}
