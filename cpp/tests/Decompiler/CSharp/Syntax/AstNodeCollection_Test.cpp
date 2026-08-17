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

// Tests for the `AstNodeCollection` / `AstNodeCollectionT<T>` collection-slot system
// (cpp/Decompiler/CSharp/Syntax/AstNodeCollection.hpp, mirroring
// ICSharpCode.Decompiler/CSharp/Syntax/AstNodeCollection.cs). Exercises the core
// collection API (`Add`/`Insert`/`InsertBefore`/`InsertAfter`/`Remove`/`IndexOf`/
// `Contains`/`Clear`/`Count`/the indexer get+set) and the four `AstNodeCollection`
// mutation virtuals, on both the incremental and the invalidate fast-paths, plus the
// integration with the parent's flattened child-index space (`GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo`/`GetCollectionByKind`/`Slot`/sibling navigation).

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;

namespace {

// A leaf node (no children): the element type of the test collections, and a child/
// candidate in the integration tests. Its `DoMatch` is unused here.
class StubExpr : public AstNode {
public:
    bool DoMatch(AstNode*, Match) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A container whose only slot is a collection of `StubExpr`. Derives from `StubExpr` so
// the self-add guard (`child == parent`) can be exercised (a `StubCollectionContainer` is
// itself a `StubExpr`, the `BlockStatement : Statement` shape). The slot-storage contract
// delegates into the owned `AstNodeCollectionT<StubExpr>`: `GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo` map the flattened index into the collection, and
// `GetCollectionByKind` returns the collection for its kind. Mirrors the generated node
// for a node whose only slot is a collection (the common `supportsIncremental = true,
// baseIndex = 0` case).
class StubCollectionContainer : public StubExpr {
    AstNodeCollectionT<StubExpr> items_;
public:
    static inline const CSharpSlotInfoT<StubExpr> ItemSlot{"Item", true, nullptr, true};

    explicit StubCollectionContainer(bool supportsIncremental = true)
        : items_(this, &ItemSlot, 0, supportsIncremental) {}

    AstNodeCollectionT<StubExpr>& Items() { return items_; }

    int GetChildCount() const override { return items_.Count(); }
    AstNode* GetChild(int index) const override { return items_.At(index); }
    void SetChild(int index, AstNode* value) override {
        items_.SetAt(index, static_cast<StubExpr*>(value));
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("StubCollectionContainer::GetChildSlotInfo");
        return &ItemSlot;
    }
    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        return kind == &ItemSlot ? &items_ : nullptr;
    }
};

} // namespace

// ---- Construction ----------------------------------------------------------

TEST(CSharp_AstNodeCollection, EmptyCollectionHasZeroCount) {
    StubCollectionContainer c;
    EXPECT_EQ(c.Items().Count(), 0);
}

TEST(CSharp_AstNodeCollection, NullParentRejected) {
    EXPECT_THROW(AstNodeCollectionT<StubExpr>(nullptr, &StubCollectionContainer::ItemSlot),
                 std::invalid_argument);
}

// ---- Add (incremental fast-path) -------------------------------------------

TEST(CSharp_AstNodeCollection, AddAppendsAndParentsIncremental) {
    StubCollectionContainer c(true);
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);

    EXPECT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(a.Parent(), &c);
    EXPECT_EQ(b.Parent(), &c);
    // The incremental fast-path (supportsIncremental + parent valid) assigns the new
    // element's ChildIndex directly and keeps the parent valid by construction.
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(b.ChildIndex, 1);
    EXPECT_TRUE(c.ChildIndicesValid());
}

TEST(CSharp_AstNodeCollection, AddNullIsNoOp) {
    StubCollectionContainer c;
    c.Items().Add(nullptr);
    EXPECT_EQ(c.Items().Count(), 0);
}

TEST(CSharp_AstNodeCollection, AddRejectsSelfAsChild) {
    // A container that is itself an element of its own collection (the BlockStatement :
    // Statement shape) cannot be added to itself.
    StubCollectionContainer c;
    EXPECT_THROW(c.Items().Add(&c), std::invalid_argument);
    EXPECT_EQ(c.Items().Count(), 0);
}

TEST(CSharp_AstNodeCollection, AddRejectsAlreadyParented) {
    StubCollectionContainer c1, c2;
    StubExpr a;
    c1.Items().Add(&a);
    // `a` is already parented to `c1`; adding it to `c2` is rejected.
    EXPECT_THROW(c2.Items().Add(&a), std::invalid_argument);
    EXPECT_EQ(c2.Items().Count(), 0);
    // `a` is still in `c1`.
    EXPECT_EQ(a.Parent(), &c1);
    EXPECT_EQ(c1.Items().Count(), 1);
}

// ---- Add (invalidate fast-path) --------------------------------------------

TEST(CSharp_AstNodeCollection, AddNonIncrementalInvalidatesAndLazyReindexes) {
    StubCollectionContainer c(false);
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);

    // The non-incremental path invalidates the parent's indices and does NOT assign the
    // new element's ChildIndex (it stays -1 until the lazy reindex).
    EXPECT_FALSE(c.ChildIndicesValid());
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(b.ChildIndex, -1);

    // The first Slot/sibling read triggers EnsureChildIndices, which reindexes.
    ASSERT_EQ(a.Slot(), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(b.ChildIndex, 1);
    EXPECT_TRUE(c.ChildIndicesValid());
}

// ---- Indexer get / set -----------------------------------------------------

TEST(CSharp_AstNodeCollection, IndexerGetReturnsElement) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(c.Items().At(1), &b);
    EXPECT_THROW(c.Items().At(2), std::out_of_range);
    EXPECT_THROW(c.Items().At(-1), std::out_of_range);
}

TEST(CSharp_AstNodeCollection, IndexerSetReplacesInPlace) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);

    StubExpr x;
    c.Items().SetAt(0, &x);

    // The old element is detached; the new one takes its place and its flattened index.
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(x.Parent(), &c);
    EXPECT_EQ(x.ChildIndex, 0);
    EXPECT_EQ(c.Items()[0], &x);
    EXPECT_EQ(c.Items()[1], &b);  // untouched
}

TEST(CSharp_AstNodeCollection, IndexerSetSameValueIsNoOp) {
    StubCollectionContainer c;
    StubExpr a;
    c.Items().Add(&a);
    c.Items().SetAt(0, &a);  // same value -> no detach, no rethrow
    EXPECT_EQ(a.Parent(), &c);
    EXPECT_EQ(c.Items()[0], &a);
}

TEST(CSharp_AstNodeCollection, IndexerSetRejectsAlreadyParented) {
    StubCollectionContainer c1, c2;
    StubExpr a, b;
    c1.Items().Add(&a);
    c2.Items().Add(&b);
    EXPECT_THROW(c2.Items().SetAt(0, &a), std::invalid_argument);
}

TEST(CSharp_AstNodeCollection, IndexerSetRejectsNull) {
    StubCollectionContainer c;
    StubExpr a;
    c.Items().Add(&a);
    EXPECT_THROW(c.Items().SetAt(0, static_cast<StubExpr*>(nullptr)), std::invalid_argument);
}

// ---- IndexOf / Contains ----------------------------------------------------

TEST(CSharp_AstNodeCollection, IndexOfFindsByPosition) {
    StubCollectionContainer c;
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);

    EXPECT_EQ(c.Items().IndexOf(&a), 0);
    EXPECT_EQ(c.Items().IndexOf(&b), 1);
    // An unparented node is not in the collection.
    EXPECT_EQ(c.Items().IndexOf(&x), -1);
}

TEST(CSharp_AstNodeCollection, IndexOfIncrementalFastPath) {
    // On the incremental fast-path with valid indices, IndexOf reads ChildIndex - base in
    // O(1) (the element's local position is its flattened index minus the base).
    StubCollectionContainer c(true);
    StubExpr a, b, d;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().Add(&d);
    ASSERT_TRUE(c.ChildIndicesValid());
    EXPECT_EQ(c.Items().IndexOf(&b), 1);
    EXPECT_EQ(c.Items().IndexOf(&d), 2);
}

TEST(CSharp_AstNodeCollection, ContainsRespectsParentAndPresence) {
    StubCollectionContainer c1, c2;
    StubExpr a;
    c1.Items().Add(&a);
    EXPECT_TRUE(c1.Items().Contains(&a));
    // `a` is parented to `c1`, so `c2` does not contain it (even though it is the same
    // object).
    EXPECT_FALSE(c2.Items().Contains(&a));
    StubExpr x;
    EXPECT_FALSE(c1.Items().Contains(&x));
    EXPECT_FALSE(c1.Items().Contains(nullptr));
}

// ---- Insert / InsertBefore / InsertAfter -----------------------------------

TEST(CSharp_AstNodeCollection, InsertBeforeInsertsAtPosition) {
    StubCollectionContainer c;
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().InsertBefore(&b, &x);

    ASSERT_EQ(c.Items().Count(), 3);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(c.Items()[1], &x);  // inserted before `b`
    EXPECT_EQ(c.Items()[2], &b);
    EXPECT_EQ(x.Parent(), &c);
}

TEST(CSharp_AstNodeCollection, InsertBeforeAbsentSiblingAppends) {
    // A null/absent existingItem yields IndexOf == -1, so the new item is appended.
    StubCollectionContainer c;
    StubExpr a, x;
    c.Items().Add(&a);
    c.Items().InsertBefore(nullptr, &x);
    ASSERT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(c.Items()[1], &x);  // appended
}

TEST(CSharp_AstNodeCollection, InsertAfterInsertsAfterPosition) {
    StubCollectionContainer c;
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().InsertAfter(&a, &x);

    ASSERT_EQ(c.Items().Count(), 3);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(c.Items()[1], &x);  // inserted after `a`
    EXPECT_EQ(c.Items()[2], &b);
}

TEST(CSharp_AstNodeCollection, InsertAfterAbsentSiblingInsertsAtFront) {
    // A null/absent existingItem yields IndexOf == -1, so Insert(-1 + 1 = 0) inserts at
    // the front.
    StubCollectionContainer c;
    StubExpr a, x;
    c.Items().Add(&a);
    c.Items().InsertAfter(nullptr, &x);
    ASSERT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(c.Items()[0], &x);  // front
    EXPECT_EQ(c.Items()[1], &a);
}

TEST(CSharp_AstNodeCollection, InsertReindexesIncremental) {
    StubCollectionContainer c(true);
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    ASSERT_TRUE(c.ChildIndicesValid());
    c.Items().InsertBefore(&b, &x);  // insert at index 1

    // The incremental path reindexes from the insertion point; the parent stays valid.
    EXPECT_TRUE(c.ChildIndicesValid());
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(x.ChildIndex, 1);
    EXPECT_EQ(b.ChildIndex, 2);
}

TEST(CSharp_AstNodeCollection, InsertInvalidatesNonIncremental) {
    StubCollectionContainer c(false);
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().InsertBefore(&b, &x);

    // The non-incremental path invalidates; the indices are reassigned lazily on read.
    EXPECT_FALSE(c.ChildIndicesValid());
    ASSERT_EQ(x.Slot(), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(x.ChildIndex, 1);
    EXPECT_EQ(b.ChildIndex, 2);
}

// ---- Remove ----------------------------------------------------------------

TEST(CSharp_AstNodeCollection, RemoveDetachesAndReindexes) {
    StubCollectionContainer c(true);
    StubExpr a, b, d;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().Add(&d);
    ASSERT_TRUE(c.ChildIndicesValid());

    EXPECT_TRUE(c.Items().Remove(&b));

    // The removed element is detached; the elements after it shift down and are reindexed.
    EXPECT_EQ(b.Parent(), nullptr);
    EXPECT_EQ(b.ChildIndex, -1);
    ASSERT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(c.Items()[1], &d);
    EXPECT_TRUE(c.ChildIndicesValid());
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(d.ChildIndex, 1);
}

TEST(CSharp_AstNodeCollection, RemoveAbsentReturnsFalse) {
    StubCollectionContainer c;
    StubExpr a, x;
    c.Items().Add(&a);
    EXPECT_FALSE(c.Items().Remove(&x));
    EXPECT_EQ(c.Items().Count(), 1);
}

TEST(CSharp_AstNodeCollection, RemoveInvalidatesNonIncremental) {
    StubCollectionContainer c(false);
    StubExpr a, b, d;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().Add(&d);

    EXPECT_TRUE(c.Items().Remove(&b));
    // The non-incremental path invalidates; reindex on read.
    EXPECT_FALSE(c.ChildIndicesValid());
    ASSERT_EQ(a.Slot(), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(d.ChildIndex, 1);
}

// ---- Clear -----------------------------------------------------------------

TEST(CSharp_AstNodeCollection, ClearDetachesAllAndInvalidates) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);

    c.Items().Clear();

    EXPECT_EQ(c.Items().Count(), 0);
    EXPECT_EQ(a.Parent(), nullptr);
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(b.Parent(), nullptr);
    EXPECT_EQ(b.ChildIndex, -1);
    EXPECT_FALSE(c.ChildIndicesValid());
}

TEST(CSharp_AstNodeCollection, ClearEmptyIsNoOp) {
    StubCollectionContainer c;
    c.Items().Clear();  // no throw, no invalidate of an already-valid empty parent
    EXPECT_EQ(c.Items().Count(), 0);
    EXPECT_TRUE(c.ChildIndicesValid());
}

// ---- AstNodeCollection mutation virtuals -----------------------------------

TEST(CSharp_AstNodeCollection, AddNodeRoutesToAdd) {
    StubCollectionContainer c;
    StubExpr a;
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    coll->AddNode(&a);
    EXPECT_EQ(c.Items().Count(), 1);
    EXPECT_EQ(c.Items()[0], &a);
    EXPECT_EQ(a.Parent(), &c);
}

TEST(CSharp_AstNodeCollection, InsertNodeBeforeRoutesToInsertBefore) {
    StubCollectionContainer c;
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    coll->InsertNodeBefore(&b, &x);
    ASSERT_EQ(c.Items().Count(), 3);
    EXPECT_EQ(c.Items()[1], &x);
}

TEST(CSharp_AstNodeCollection, InsertNodeBeforeAbsentExistingAppends) {
    // `existing` not in this collection -> the new node is appended.
    StubCollectionContainer c;
    StubExpr a, x;
    c.Items().Add(&a);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    coll->InsertNodeBefore(nullptr, &x);  // null existing -> append
    ASSERT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(c.Items()[1], &x);
}

TEST(CSharp_AstNodeCollection, InsertNodeAfterRoutesToInsertAfter) {
    StubCollectionContainer c;
    StubExpr a, b, x;
    c.Items().Add(&a);
    c.Items().Add(&b);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    coll->InsertNodeAfter(&a, &x);
    ASSERT_EQ(c.Items().Count(), 3);
    EXPECT_EQ(c.Items()[1], &x);
}

TEST(CSharp_AstNodeCollection, InsertNodeAfterAbsentExistingInsertsAtFront) {
    StubCollectionContainer c;
    StubExpr a, x;
    c.Items().Add(&a);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    coll->InsertNodeAfter(nullptr, &x);  // null existing -> front
    ASSERT_EQ(c.Items().Count(), 2);
    EXPECT_EQ(c.Items()[0], &x);
}

TEST(CSharp_AstNodeCollection, RemoveNodeRoutesToRemove) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    EXPECT_TRUE(coll->RemoveNode(&a));
    ASSERT_EQ(c.Items().Count(), 1);
    EXPECT_EQ(c.Items()[0], &b);
    EXPECT_EQ(a.Parent(), nullptr);
}

TEST(CSharp_AstNodeCollection, RemoveNodeWrongTypeReturnsFalse) {
    // A node that is not a `StubExpr` is not in the collection (the dynamic_cast fails).
    StubCollectionContainer c;
    StubExpr a;
    c.Items().Add(&a);
    AstNodeCollection* coll = c.GetCollectionByKind(&StubCollectionContainer::ItemSlot);
    ASSERT_NE(coll, nullptr);
    // A non-StubExpr AstNode: use a pure AstNode-derived stub the collection does not hold.
    class NotAnExpr : public AstNode {
    public:
        bool DoMatch(AstNode*, Match) override { return false; }
        void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    };
    NotAnExpr n;
    EXPECT_FALSE(coll->RemoveNode(&n));
    EXPECT_EQ(c.Items().Count(), 1);
}

// ---- Integration with the parent's flattened child-index space ---------------

TEST(CSharp_AstNodeCollection, IntegratesWithParentChildIndexSpace) {
    StubCollectionContainer c(true);
    StubExpr a, b, d;
    c.Items().Add(&a);
    c.Items().Add(&b);
    c.Items().Add(&d);

    // The collection's elements occupy the parent's flattened child-index space.
    EXPECT_EQ(c.GetChildCount(), 3);
    EXPECT_EQ(c.GetChild(0), &a);
    EXPECT_EQ(c.GetChild(1), &b);
    EXPECT_EQ(c.GetChild(2), &d);
    EXPECT_EQ(c.GetChildSlotInfo(0), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(c.GetChildSlotInfo(2), &StubCollectionContainer::ItemSlot);
    EXPECT_THROW(c.GetChildSlotInfo(3), std::out_of_range);

    // Slot()/sibling navigation work over the collection's elements.
    EXPECT_EQ(a.Slot(), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(b.Slot(), &StubCollectionContainer::ItemSlot);
    EXPECT_EQ(a.NextSibling(), &b);
    EXPECT_EQ(b.PrevSibling(), &a);
    EXPECT_EQ(d.NextSibling(), nullptr);
    EXPECT_EQ(a.PrevSibling(), nullptr);

    // The Children view walks the collection's elements in document order.
    std::vector<AstNode*> visited;
    for (AstNode* child : c.Children())
        visited.push_back(child);
    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], &a);
    EXPECT_EQ(visited[1], &b);
    EXPECT_EQ(visited[2], &d);
}

TEST(CSharp_AstNodeCollection, GetCollectionByKindReturnsNullForUnknownKind) {
    StubCollectionContainer c;
    // A distinct slot (its own address) is not this node's collection kind.
    const CSharpSlotInfoT<StubExpr> otherSlot{"Other", true, nullptr, true};
    EXPECT_EQ(c.GetCollectionByKind(&otherSlot), nullptr);
    EXPECT_NE(c.GetCollectionByKind(&StubCollectionContainer::ItemSlot), nullptr);
}

TEST(CSharp_AstNodeCollection, DescendantsWalksCollectionElements) {
    // A collection element that is itself a container (StubCollectionContainer : StubExpr)
    // is visited by Descendants, and its own collection's elements are descended into.
    StubCollectionContainer outer(true);
    StubCollectionContainer inner(true);
    StubExpr leaf;
    outer.Items().Add(&inner);
    inner.Items().Add(&leaf);

    auto desc = outer.Descendants();
    // Pre-order: inner, then inner's leaf.
    ASSERT_EQ(desc.size(), 2u);
    EXPECT_EQ(desc[0], &inner);
    EXPECT_EQ(desc[1], &leaf);
}

// ---- Pattern-matcher surface (NodeCount/NodeAt/AsNodeList/DoMatch) ------
// The pattern matcher consumes a collection as a node-list view (`AsNodeList`) and matches
// it against another collection by index with backtracking (`DoMatch` ->
// `Pattern.DoMatchCollection`). A leaf whose `DoMatch` returns true (matches any candidate)
// lets the positive path be exercised; the existing `StubExpr` (`DoMatch` returns false)
// exercises the negative path.

class StubMatchNode : public AstNode {
public:
    // A concrete node's `DoMatch` is `other is ConcreteNode && ...`: a type gate that rejects
    // a null candidate (and a non-`StubMatchNode`). This stub matches any `StubMatchNode`
    // candidate (so same-count collections of stubs match) but rejects null -- the behaviour a
    // real node's type gate gives (so a 2-vs-1 collection match fails, the 2nd pattern element
    // matching the null past-the-end).
    bool DoMatch(AstNode* other, Match) override {
        return dynamic_cast<StubMatchNode*>(other) != nullptr;
    }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

class StubMatchContainer : public StubMatchNode {
    AstNodeCollectionT<StubMatchNode> items_;
public:
    static inline const CSharpSlotInfoT<StubMatchNode> ItemSlot{"Item", true, nullptr, true};

    StubMatchContainer() : items_(this, &ItemSlot, 0, true) {}
    AstNodeCollectionT<StubMatchNode>& Items() { return items_; }
};

// `NodeCount` is the element count (0 until the first `Add`).
TEST(CSharp_AstNodeCollection, NodeCountIsZeroOnEmptyCollection) {
    StubCollectionContainer c;
    EXPECT_EQ(c.Items().NodeCount(), 0);
}

TEST(CSharp_AstNodeCollection, NodeCountIsElementCount) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);
    EXPECT_EQ(c.Items().NodeCount(), 2);
}

// `NodeAt` returns the element at the local position as an `INode*` (the upcast `T*` ->
// `INode*`); past the end throws.
TEST(CSharp_AstNodeCollection, NodeAtReturnsElementAsINode) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);
    EXPECT_EQ(c.Items().NodeAt(0), &a);
    EXPECT_EQ(c.Items().NodeAt(1), &b);
    EXPECT_THROW(c.Items().NodeAt(2), std::out_of_range);
}

// `AsNodeList` builds a node-list view of the current elements (a snapshot `vector<INode*>`).
TEST(CSharp_AstNodeCollection, AsNodeListBuildsCurrentElements) {
    StubCollectionContainer c;
    StubExpr a, b;
    c.Items().Add(&a);
    c.Items().Add(&b);
    auto view = c.Items().AsNodeList();
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0], &a);
    EXPECT_EQ(view[1], &b);
}

TEST(CSharp_AstNodeCollection, AsNodeListEmptyOnEmptyCollection) {
    StubCollectionContainer c;
    EXPECT_TRUE(c.Items().AsNodeList().empty());
}

// `DoMatch` of two EMPTY collections succeeds (no elements to mismatch).
TEST(CSharp_AstNodeCollection, DoMatchEmptyCollectionsMatch) {
    StubMatchContainer a, b;
    Match m = Match::CreateNew();
    EXPECT_TRUE(a.Items().DoMatch(b.Items(), m));
}

// `DoMatch` of two same-count collections whose elements all match (a `StubMatchNode` matches
// any candidate) succeeds.
TEST(CSharp_AstNodeCollection, DoMatchSameCountMatchingElementsMatch) {
    StubMatchContainer a, b;
    StubMatchNode a1, a2, b1, b2;
    a.Items().Add(&a1);
    a.Items().Add(&a2);
    b.Items().Add(&b1);
    b.Items().Add(&b2);
    Match m = Match::CreateNew();
    EXPECT_TRUE(a.Items().DoMatch(b.Items(), m));
}

// `DoMatch` of different-count collections fails (the pattern runs out or the other runs out).
TEST(CSharp_AstNodeCollection, DoMatchDifferentCountFails) {
    StubMatchContainer a, b;
    StubMatchNode a1, b1, b2;
    a.Items().Add(&a1);
    b.Items().Add(&b1);
    b.Items().Add(&b2);
    Match m = Match::CreateNew();
    EXPECT_FALSE(a.Items().DoMatch(b.Items(), m));  // 1 vs 2
    EXPECT_FALSE(b.Items().DoMatch(a.Items(), m));  // 2 vs 1
}

// `DoMatch` of same-count collections whose elements do NOT match (a `StubExpr`'s `DoMatch`
// returns false) fails.
TEST(CSharp_AstNodeCollection, DoMatchSameCountMismatchingElementsFails) {
    StubCollectionContainer a, b;
    StubExpr a1, b1;
    a.Items().Add(&a1);
    b.Items().Add(&b1);
    Match m = Match::CreateNew();
    EXPECT_FALSE(a.Items().DoMatch(b.Items(), m));
}
