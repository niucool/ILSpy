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

// Tests for the `AstNode` mutation API (cpp/Decompiler/CSharp/Syntax/AstNode.hpp +
// .cpp, mirroring ICSharpCode.Decompiler/CSharp/Syntax/AstNode.cs): `AddChild`/
// `AddChildUnsafe`, `InsertChildBefore`/`InsertChildBeforeUnsafe`, `InsertChildAfter`,
// `Remove`, `ReplaceWith` (both overloads), `Clone`, `SetChildByKindUntyped`, and the
// `SetChildNode`/`ValidateNewSingleChild` helpers. The stubs model the slot/kind
// relationship faithfully: each per-node slot points (via `Kind`) at a canonical `Slots`
// constant, so `Remove()` (which reads `GetChildSlotInfo(childIndex).Kind`) and
// `SetChildByKindUntyped` route to the right collection/slot.

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <stdexcept>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;

namespace {

// The common base of the test nodes. The collection slot's element type is `StubNode`, so
// a `StubBlock` can hold any `StubNode` subtype (leaf or container), exercising the
// lift-out-of-subtree `ReplaceWith` shape.
class StubNode : public AstNode {
public:
    bool DoMatch(AstNode*, Match) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A leaf node (no children) carrying a scalar `Tag` so `Clone`'s scalar copy can be
// verified. The element type of the test collections and a child/candidate in the
// single-slot tests.
class StubLeaf : public StubNode {
public:
    int Tag = 0;
    explicit StubLeaf(int tag = 0) : Tag(tag) {}
    AstNode* Clone() const override { return new StubLeaf(Tag); }
};

// A single-slot node using the KNOWN-INDEX `SetChildNode(field, value, index)` in its
// `SetChild` (the generated setter shape when no collection precedes the slot). The slot is
// optional (a single nullable child), so `Remove()` on the child clears the slot.
class StubSingle : public StubNode {
    StubNode* child_ = nullptr;
public:
    static inline const CSharpSlotInfoT<StubNode> ChildSlotKind{"Child", false, nullptr, false};
    static inline const CSharpSlotInfoT<StubNode> ChildSlot{"Child", false, &ChildSlotKind, true};

    int GetChildCount() const override { return 1; }
    AstNode* GetChild(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingle::GetChild");
        return child_;
    }
    void SetChild(int index, AstNode* value) override {
        if (index != 0) throw std::out_of_range("StubSingle::SetChild");
        SetChildNode(child_, static_cast<StubNode*>(value), 0);
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingle::GetChildSlotInfo");
        return &ChildSlot;
    }
    AstNode* Clone() const override {
        auto* copy = new StubSingle();
        if (child_ != nullptr)
            copy->SetChild(0, child_->Clone());
        return copy;
    }
};

// A single-slot node using the UNKNOWN-INDEX `SetChildNode(field, value)` (no `index`) in
// its `SetChild` (the shape when a collection precedes the slot, so the index is not
// statically known). An in-place replace carries the old child's index; a set or clear
// invalidates the parent's indices for a lazy rebuild.
class StubSingleUnknown : public StubNode {
    StubNode* child_ = nullptr;
public:
    static inline const CSharpSlotInfoT<StubNode> ChildSlotKind{"ChildU", false, nullptr, false};
    static inline const CSharpSlotInfoT<StubNode> ChildSlot{"ChildU", false, &ChildSlotKind, true};

    int GetChildCount() const override { return 1; }
    AstNode* GetChild(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingleUnknown::GetChild");
        return child_;
    }
    void SetChild(int index, AstNode* value) override {
        if (index != 0) throw std::out_of_range("StubSingleUnknown::SetChild");
        SetChildNode(child_, static_cast<StubNode*>(value));
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingleUnknown::GetChildSlotInfo");
        return &ChildSlot;
    }
    AstNode* Clone() const override {
        auto* copy = new StubSingleUnknown();
        if (child_ != nullptr)
            copy->SetChild(0, child_->Clone());
        return copy;
    }
};

// A collection node (one collection slot) whose per-node slot points at a canonical `Slots`
// constant (`ItemSlotKind`), so `Remove()` (which reads
// `GetChildSlotInfo(childIndex).Kind`) and `AddChild`/`InsertChild*` (which pass the kind)
// route to the collection. `GetCollectionByKind` matches the kind, not the per-node slot.
class StubBlock : public StubNode {
    AstNodeCollectionT<StubNode> items_;
public:
    static inline const CSharpSlotInfoT<StubNode> ItemSlotKind{"Item", true, nullptr, true};
    static inline const CSharpSlotInfoT<StubNode> ItemSlot{"Item", true, &ItemSlotKind, true};

    StubBlock() : items_(this, &ItemSlot, 0, true) {}
    AstNodeCollectionT<StubNode>& Items() { return items_; }

    int GetChildCount() const override { return items_.Count(); }
    AstNode* GetChild(int index) const override { return items_.At(index); }
    void SetChild(int index, AstNode* value) override {
        items_.SetAt(index, static_cast<StubNode*>(value));
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("StubBlock::GetChildSlotInfo");
        return &ItemSlot;
    }
    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        return kind == &ItemSlotKind ? &items_ : nullptr;
    }
    AstNode* Clone() const override {
        auto* copy = new StubBlock();
        for (int i = 0; i < items_.Count(); i++)
            copy->Items().Add(static_cast<StubNode*>(items_.At(i)->Clone()));
        return copy;
    }
};

} // namespace

// ---- SetChildNode (known-index) ---------------------------------------------

TEST(CSharp_AstNodeMutation, SetChildNodeKnownIndexFillsAndParents) {
    StubSingle s;
    StubLeaf a;
    s.SetChild(0, &a);
    EXPECT_EQ(a.Parent(), &s);
    EXPECT_EQ(a.ChildIndex, 0);          // known-index assigns directly
    EXPECT_TRUE(s.ChildIndicesValid());   // no invalidate on the known-index path
    EXPECT_EQ(s.GetChild(0), &a);
}

TEST(CSharp_AstNodeMutation, SetChildNodeKnownIndexReplaceCarriesIndex) {
    StubSingle s;
    StubLeaf a, b;
    s.SetChild(0, &a);
    s.SetChild(0, &b);
    EXPECT_EQ(a.Parent(), nullptr);       // old detached
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(b.Parent(), &s);
    EXPECT_EQ(b.ChildIndex, 0);           // index reused
    EXPECT_EQ(s.GetChild(0), &b);
}

TEST(CSharp_AstNodeMutation, SetChildNodeKnownIndexClearDetaches) {
    StubSingle s;
    StubLeaf a;
    s.SetChild(0, &a);
    s.SetChild(0, nullptr);
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(s.GetChild(0), nullptr);
}

// ---- SetChildNode (unknown-index) -------------------------------------------

TEST(CSharp_AstNodeMutation, SetChildNodeUnknownIndexInvalidatesAndLazyReindexes) {
    StubSingleUnknown s;
    StubLeaf a;
    s.SetChild(0, &a);
    EXPECT_EQ(a.Parent(), &s);
    EXPECT_EQ(a.ChildIndex, -1);          // not assigned on the unknown-index set
    EXPECT_FALSE(s.ChildIndicesValid());  // invalidate on set
    ASSERT_EQ(a.Slot(), &StubSingleUnknown::ChildSlot);
    EXPECT_EQ(a.ChildIndex, 0);           // lazy reindex on first Slot read
    EXPECT_TRUE(s.ChildIndicesValid());
}

TEST(CSharp_AstNodeMutation, SetChildNodeUnknownIndexReplaceCarriesOldIndex) {
    StubSingleUnknown s;
    StubLeaf a, b;
    s.SetChild(0, &a);
    (void)a.Slot();                       // ensure indices (a.ChildIndex = 0)
    ASSERT_EQ(a.ChildIndex, 0);
    s.SetChild(0, &b);
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(b.Parent(), &s);
    EXPECT_EQ(b.ChildIndex, 0);           // in-place replace carries the old index
    EXPECT_TRUE(s.ChildIndicesValid());   // no invalidate on in-place replace
}

// ---- ValidateNewSingleChild (via SetChildNode) ------------------------------

TEST(CSharp_AstNodeMutation, ValidateNewSingleChildRejectsSelfReference) {
    StubSingle s;
    EXPECT_THROW(s.SetChild(0, &s), std::invalid_argument);  // cannot add self as child
    EXPECT_EQ(s.GetChild(0), nullptr);
}

TEST(CSharp_AstNodeMutation, ValidateNewSingleChildRejectsAlreadyParented) {
    StubSingle s1, s2;
    StubLeaf a;
    s1.SetChild(0, &a);
    // `a` is already parented to `s1`; adding it to `s2` (whose old child is null, so the
    // lift-out guard's `Ancestors.Contains(oldChild)` is false) is rejected.
    EXPECT_THROW(s2.SetChild(0, &a), std::invalid_argument);
    EXPECT_EQ(a.Parent(), &s1);           // unchanged
    EXPECT_EQ(s2.GetChild(0), nullptr);
}

TEST(CSharp_AstNodeMutation, ValidateNewSingleChildLiftsOutOfReplacedSubtree) {
    // `outer`'s child is `inner` (a container), and `inner`'s child is `deep`. Replacing
    // `outer`'s child with `deep` lifts `deep` out of `inner` (the old child is an ancestor
    // of the new value -- the C# `value.Ancestors.Contains(oldChild)` lift-out guard),
    // then places `deep` at `inner`'s old slot. `inner` is detached.
    StubSingle outer;
    StubSingle inner;
    StubLeaf deep;
    outer.SetChild(0, &inner);
    inner.SetChild(0, &deep);
    ASSERT_EQ(deep.Parent(), &inner);

    outer.SetChild(0, &deep);  // SetChildNode -> ValidateNewSingleChild lifts deep out

    EXPECT_EQ(outer.GetChild(0), &deep);     // deep now at inner's old slot
    EXPECT_EQ(deep.Parent(), &outer);
    EXPECT_EQ(deep.ChildIndex, 0);
    EXPECT_EQ(inner.Parent(), nullptr);      // inner detached (replaced)
    EXPECT_EQ(inner.ChildIndex, -1);
    EXPECT_EQ(inner.GetChild(0), nullptr);    // deep lifted out of inner
}

// ---- AddChild / AddChildUnsafe ----------------------------------------------

TEST(CSharp_AstNodeMutation, AddChildAppendsToCollection) {
    StubBlock b;
    StubLeaf a, c;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    ASSERT_EQ(b.Items().Count(), 2);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(b.Items()[1], &c);
    EXPECT_EQ(a.Parent(), &b);
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(c.ChildIndex, 1);
}

TEST(CSharp_AstNodeMutation, AddChildNullIsNoOp) {
    StubBlock b;
    b.AddChild<StubNode>(nullptr, &StubBlock::ItemSlotKind);
    EXPECT_EQ(b.Items().Count(), 0);
}

TEST(CSharp_AstNodeMutation, AddChildFillsSingleSlot) {
    StubSingle s;
    StubLeaf a;
    s.AddChild(&a, &StubSingle::ChildSlotKind);
    EXPECT_EQ(s.GetChild(0), &a);
    EXPECT_EQ(a.Parent(), &s);
    EXPECT_EQ(a.ChildIndex, 0);
}

TEST(CSharp_AstNodeMutation, AddChildRejectsUnknownKindOnSingleSlot) {
    StubSingle s;
    StubLeaf a;
    // The block's ItemSlotKind is not a kind this single-slot node declares.
    EXPECT_THROW(s.AddChild(&a, &StubBlock::ItemSlotKind), std::logic_error);
    EXPECT_EQ(s.GetChild(0), nullptr);
}

// ---- InsertChildBefore / InsertChildBeforeUnsafe ----------------------------

TEST(CSharp_AstNodeMutation, InsertChildBeforeInsertsIntoCollection) {
    StubBlock b;
    StubLeaf a, c, x;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    b.InsertChildBefore(&c, &x, &StubBlock::ItemSlotKind);
    ASSERT_EQ(b.Items().Count(), 3);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(b.Items()[1], &x);   // inserted before c
    EXPECT_EQ(b.Items()[2], &c);
    EXPECT_EQ(x.Parent(), &b);
}

TEST(CSharp_AstNodeMutation, InsertChildBeforeUnsafeInsertsIntoCollection) {
    StubBlock b;
    StubLeaf a, c, x;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    b.InsertChildBeforeUnsafe(&c, &x, &StubBlock::ItemSlotKind);
    ASSERT_EQ(b.Items().Count(), 3);
    EXPECT_EQ(b.Items()[1], &x);
}

// ---- InsertChildAfter ------------------------------------------------------

TEST(CSharp_AstNodeMutation, InsertChildAfterInsertsIntoCollection) {
    StubBlock b;
    StubLeaf a, c, x;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    b.InsertChildAfter(&a, &x, &StubBlock::ItemSlotKind);
    ASSERT_EQ(b.Items().Count(), 3);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(b.Items()[1], &x);   // inserted after a
    EXPECT_EQ(b.Items()[2], &c);
    EXPECT_EQ(x.Parent(), &b);
}

// ---- Remove ----------------------------------------------------------------

TEST(CSharp_AstNodeMutation, RemoveFromCollectionDetaches) {
    StubBlock b;
    StubLeaf a, c;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    a.Remove();
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    ASSERT_EQ(b.Items().Count(), 1);
    EXPECT_EQ(b.Items()[0], &c);   // c shifts to index 0
    EXPECT_EQ(c.ChildIndex, 0);
}

TEST(CSharp_AstNodeMutation, RemoveFromSingleSlotClears) {
    StubSingle s;
    StubLeaf a;
    s.SetChild(0, &a);
    a.Remove();
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(s.GetChild(0), nullptr);
}

TEST(CSharp_AstNodeMutation, RemoveUnparentedIsNoOp) {
    StubLeaf a;
    a.Remove();  // no throw, no change
    EXPECT_EQ(a.Parent(), nullptr);
}

// ---- ReplaceWith(AstNode*) -------------------------------------------------

TEST(CSharp_AstNodeMutation, ReplaceWithReplacesInCollection) {
    StubBlock b;
    StubLeaf a, c, x;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    c.ReplaceWith(&x);
    ASSERT_EQ(b.Items().Count(), 2);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(b.Items()[1], &x);
    EXPECT_EQ(c.Parent(), nullptr);   // old detached
    EXPECT_EQ(x.Parent(), &b);
    EXPECT_EQ(x.ChildIndex, 1);
}

TEST(CSharp_AstNodeMutation, ReplaceWithNullIsRemove) {
    StubBlock b;
    StubLeaf a, c;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    c.ReplaceWith(nullptr);
    ASSERT_EQ(b.Items().Count(), 1);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(c.Parent(), nullptr);
}

TEST(CSharp_AstNodeMutation, ReplaceWithSelfIsNoOp) {
    StubBlock b;
    StubLeaf a;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    a.ReplaceWith(&a);
    EXPECT_EQ(a.Parent(), &b);        // unchanged
    EXPECT_EQ(b.Items().Count(), 1);
}

TEST(CSharp_AstNodeMutation, ReplaceWithRootThrows) {
    StubLeaf root;                     // unparented (a root)
    StubLeaf x;
    EXPECT_THROW(root.ReplaceWith(&x), std::logic_error);
}

TEST(CSharp_AstNodeMutation, ReplaceWithLiftsOutOfReplacedSubtree) {
    // `block` holds [a, outer] where outer's child is `inner`. Replacing outer with inner
    // lifts inner out of outer (it is inside the subtree being replaced) and places it at
    // outer's position -- the C# `parenthesizedExpr.ReplaceWith(parenthesizedExpr.Expression)`.
    StubBlock block;
    StubLeaf a, inner;
    StubSingle outer;
    block.AddChild(&a, &StubBlock::ItemSlotKind);
    block.AddChild(&outer, &StubBlock::ItemSlotKind);
    outer.SetChild(0, &inner);
    ASSERT_EQ(inner.Parent(), &outer);

    outer.ReplaceWith(&inner);

    ASSERT_EQ(block.Items().Count(), 2);
    EXPECT_EQ(block.Items()[0], &a);
    EXPECT_EQ(block.Items()[1], &inner);   // inner now at outer's old position
    EXPECT_EQ(inner.Parent(), &block);
    EXPECT_EQ(inner.ChildIndex, 1);
    EXPECT_EQ(outer.Parent(), nullptr);    // outer detached
    EXPECT_EQ(outer.GetChild(0), nullptr);  // inner lifted out of outer
}

TEST(CSharp_AstNodeMutation, ReplaceWithRejectsAlreadyParentedOutside) {
    // `inner` is parented to `other` (not an ancestor of the node being replaced), so the
    // lift-out guard does not fire and the already-used-in-another-tree guard rejects.
    StubBlock block, other;
    StubLeaf a, inner;
    block.AddChild(&a, &StubBlock::ItemSlotKind);
    other.AddChild(&inner, &StubBlock::ItemSlotKind);
    EXPECT_THROW(a.ReplaceWith(&inner), std::invalid_argument);
    EXPECT_EQ(inner.Parent(), &other);    // unchanged
}

TEST(CSharp_AstNodeMutation, ReplaceWithRejectsTypeMismatch) {
    // The ItemSlot's declared child type is StubNode; a node that is not a StubNode is
    // rejected by the slot's IsInstanceOfType test. Use a bare AstNode-derived node that is
    // NOT a StubNode (a leaf of a different branch).
    StubBlock b;
    StubLeaf a;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    class NotAStubNode : public AstNode {
    public:
        bool DoMatch(AstNode*, Match) override { return false; }
        void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    };
    NotAStubNode x;
    EXPECT_THROW(a.ReplaceWith(&x), std::invalid_argument);
    EXPECT_EQ(a.Parent(), &b);   // unchanged
}

// ---- ReplaceWith(Func) -----------------------------------------------------

TEST(CSharp_AstNodeMutation, ReplaceWithFunctionInsertsResult) {
    StubBlock b;
    StubLeaf a, c;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    StubLeaf* replacement = new StubLeaf(99);
    AstNode* removed = c.ReplaceWith(
        [replacement, &c](AstNode* node) -> AstNode* {
            EXPECT_EQ(node, &c);          // the function receives the removed node
            return replacement;
        });
    EXPECT_EQ(removed, replacement);
    ASSERT_EQ(b.Items().Count(), 2);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(b.Items()[1], replacement);
    EXPECT_EQ(replacement->Parent(), &b);
    EXPECT_EQ(c.Parent(), nullptr);       // old detached
}

TEST(CSharp_AstNodeMutation, ReplaceWithFunctionNullResultIsRemove) {
    StubBlock b;
    StubLeaf a, c;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    AstNode* removed = c.ReplaceWith([](AstNode*) -> AstNode* { return nullptr; });
    EXPECT_EQ(removed, nullptr);
    ASSERT_EQ(b.Items().Count(), 1);
    EXPECT_EQ(b.Items()[0], &a);
    EXPECT_EQ(c.Parent(), nullptr);
}

TEST(CSharp_AstNodeMutation, ReplaceWithFunctionNullFunctionThrows) {
    StubBlock b;
    StubLeaf a;
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    std::function<AstNode*(AstNode*)> empty;
    EXPECT_THROW(a.ReplaceWith(empty), std::invalid_argument);
}

TEST(CSharp_AstNodeMutation, ReplaceWithFunctionRootThrows) {
    StubLeaf root;
    EXPECT_THROW(root.ReplaceWith([](AstNode*) -> AstNode* { return nullptr; }),
                 std::logic_error);
}

// ---- Clone ----------------------------------------------------------------

TEST(CSharp_AstNodeMutation, CloneLeafDeepCopiesScalar) {
    StubLeaf a(42);
    AstNode* copy = a.Clone();
    ASSERT_NE(copy, &a);
    auto* leafCopy = dynamic_cast<StubLeaf*>(copy);
    ASSERT_NE(leafCopy, nullptr);
    EXPECT_EQ(leafCopy->Tag, 42);
    EXPECT_EQ(leafCopy->Parent(), nullptr);   // clone is detached (a root)
    EXPECT_EQ(leafCopy->ChildIndex, -1);
}

TEST(CSharp_AstNodeMutation, CloneSingleDeepCopiesChild) {
    StubSingle s;
    StubLeaf a(7);
    s.SetChild(0, &a);
    AstNode* copy = s.Clone();
    ASSERT_NE(copy, &s);
    auto* singleCopy = dynamic_cast<StubSingle*>(copy);
    ASSERT_NE(singleCopy, nullptr);
    ASSERT_NE(singleCopy->GetChild(0), &a);   // deep copy, not the same object
    auto* childCopy = dynamic_cast<StubLeaf*>(singleCopy->GetChild(0));
    ASSERT_NE(childCopy, nullptr);
    EXPECT_EQ(childCopy->Tag, 7);
    EXPECT_EQ(childCopy->Parent(), singleCopy);   // re-parented to the clone
    EXPECT_EQ(childCopy->ChildIndex, 0);
    // The original is untouched.
    EXPECT_EQ(a.Parent(), &s);
}

TEST(CSharp_AstNodeMutation, CloneBlockDeepCopiesCollection) {
    StubBlock b;
    StubLeaf a(1), c(2);
    b.AddChild(&a, &StubBlock::ItemSlotKind);
    b.AddChild(&c, &StubBlock::ItemSlotKind);
    AstNode* copy = b.Clone();
    ASSERT_NE(copy, &b);
    auto* blockCopy = dynamic_cast<StubBlock*>(copy);
    ASSERT_NE(blockCopy, nullptr);
    ASSERT_EQ(blockCopy->Items().Count(), 2);
    EXPECT_NE(blockCopy->Items()[0], &a);   // deep copies, not the same objects
    EXPECT_NE(blockCopy->Items()[1], &c);
    auto* aCopy = dynamic_cast<StubLeaf*>(blockCopy->Items()[0]);
    auto* cCopy = dynamic_cast<StubLeaf*>(blockCopy->Items()[1]);
    ASSERT_NE(aCopy, nullptr);
    ASSERT_NE(cCopy, nullptr);
    EXPECT_EQ(aCopy->Tag, 1);
    EXPECT_EQ(cCopy->Tag, 2);
    EXPECT_EQ(aCopy->Parent(), blockCopy);   // re-parented to the clone
    EXPECT_EQ(cCopy->Parent(), blockCopy);
    EXPECT_EQ(aCopy->ChildIndex, 0);
    EXPECT_EQ(cCopy->ChildIndex, 1);
    // The original is untouched.
    EXPECT_EQ(a.Parent(), &b);
    EXPECT_EQ(c.Parent(), &b);
}

TEST(CSharp_AstNodeMutation, CloneBaseThrowsForUnoverriddenNode) {
    // A concrete node that does NOT override Clone: the base throws (no MemberwiseClone in
    // C++; a concrete node must override).
    class NoClone : public StubNode {
    public:
        AstNode* Clone() const override { return AstNode::Clone(); }  // call base explicitly
    };
    NoClone n;
    EXPECT_THROW(n.Clone(), std::logic_error);
}

// ---- SetChildByKindUntyped ------------------------------------------------

TEST(CSharp_AstNodeMutation, SetChildByKindUntypedFillsMatchingSlot) {
    StubSingle s;
    StubLeaf a;
    s.SetChildByKindUntyped(&StubSingle::ChildSlotKind, &a);
    EXPECT_EQ(s.GetChild(0), &a);
    EXPECT_EQ(a.Parent(), &s);
}

TEST(CSharp_AstNodeMutation, SetChildByKindUntypedRejectsUnknownKind) {
    StubSingle s;
    StubLeaf a;
    EXPECT_THROW(s.SetChildByKindUntyped(&StubBlock::ItemSlotKind, &a), std::logic_error);
    EXPECT_EQ(s.GetChild(0), nullptr);
}
