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

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/BacktrackingInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;
using PatternMatching::INode;
using PatternMatching::BacktrackingInfo;

namespace {
// A leaf node (no children): exercises the zero-child slot-storage defaults and serves
// as a child / candidate in the navigation tests (its DoMatch is unused there).
class StubExpr : public AstNode {
public:
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A leaf node that tracks DoMatch calls and records the candidate under a group, so
// the INode delegation can be observed: a call counter distinguishes "delegated to the
// typed DoMatch" from "failed at the gate" (a non-AstNode candidate), and the capture
// (recorded only for a non-null candidate, since Match.Add no-ops on a null node)
// confirms the candidate reached the typed DoMatch.
class TrackingExpr : public AstNode {
public:
    int DoMatchCalls = 0;
    AstNode* LastOther = nullptr;
    bool DoMatch(AstNode* other, Match match) override {
        ++DoMatchCalls;
        LastOther = other;
        match.Add("captured", other);
        return other != nullptr;
    }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A leaf node that overrides the source-location getters (the single-token leaf case),
// so the virtual override (not the stored Empty) is returned.
class StubLeaf : public AstNode {
public:
    TextLocation StartLocation() const override { return TextLocation(7, 9); }
    TextLocation EndLocation() const override { return TextLocation(7, 12); }
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A two-single-slot container (Left/Right) using the known-index set path: SetChild
// assigns the child's ChildIndex directly and keeps the parent's indices valid by
// construction (no invalidate), mirroring the generated SetChildNode(index) helper.
class StubBinary : public AstNode {
    AstNode* left_ = nullptr;
    AstNode* right_ = nullptr;
public:
    static inline const CSharpSlotInfoT<StubExpr> LeftSlot{"Left", false, nullptr, false};
    static inline const CSharpSlotInfoT<StubExpr> RightSlot{"Right", false, nullptr, false};

    int GetChildCount() const override { return 2; }
    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return left_;
            case 1: return right_;
            default: throw std::out_of_range("StubBinary::GetChild");
        }
    }
    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0:
                if (left_ == value) return;
                if (left_) left_->ClearParentAndIndex();
                left_ = value;
                if (value) { value->SetParent(this); value->ChildIndex = 0; }
                break;
            case 1:
                if (right_ == value) return;
                if (right_) right_->ClearParentAndIndex();
                right_ = value;
                if (value) { value->SetParent(this); value->ChildIndex = 1; }
                break;
            default: throw std::out_of_range("StubBinary::SetChild");
        }
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LeftSlot;
            case 1: return &RightSlot;
            default: throw std::out_of_range("StubBinary::GetChildSlotInfo");
        }
    }
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// A collection-style container using the invalidate path: Append stores the child and
// invalidates the parent's indices WITHOUT assigning ChildIndex, so the lazy
// EnsureChildIndices reindex (triggered by the first Slot/sibling read) is exercised.
class StubList : public AstNode {
    std::vector<AstNode*> items_;
public:
    static inline const CSharpSlotInfoT<StubExpr> ItemSlot{"Item", true, nullptr, true};

    void Append(AstNode* value) {
        if (value) value->SetParent(this);
        items_.push_back(value);
        InvalidateChildIndices();
    }
    int GetChildCount() const override { return static_cast<int>(items_.size()); }
    AstNode* GetChild(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("StubList::GetChild");
        return items_[static_cast<std::size_t>(index)];
    }
    void SetChild(int index, AstNode* value) override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("StubList::SetChild");
        if (value) value->SetParent(this);
        items_[static_cast<std::size_t>(index)] = value;
        InvalidateChildIndices();
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index < 0 || index >= GetChildCount())
            throw std::out_of_range("StubList::GetChildSlotInfo");
        return &ItemSlot;
    }
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
};

// An INode that is NOT an AstNode (a pattern node), for the INode.DoMatch "non-AstNode
// candidate fails" branch.
class PurePatternNode : public INode {
public:
    bool DoMatch(INode* /*other*/, Match /*match*/) override { return false; }
    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match, BacktrackingInfo& /*bi*/) override {
        return DoMatch(pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)] : nullptr, match);
    }
};
} // namespace

// ---- Source location -------------------------------------------------------

TEST(CSharp_AstNode, DefaultLocationIsEmpty) {
    StubExpr e;
    EXPECT_EQ(e.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(e.EndLocation(), TextLocation::Empty);
}

TEST(CSharp_AstNode, StorePrintStartEndRecordsLocation) {
    StubExpr e;
    e.StorePrintStart(TextLocation(3, 5));
    e.StorePrintEnd(TextLocation(3, 10));
    EXPECT_EQ(e.StartLocation(), TextLocation(3, 5));
    EXPECT_EQ(e.EndLocation(), TextLocation(3, 10));
}

TEST(CSharp_AstNode, LeafOverridesLocation) {
    StubLeaf l;
    EXPECT_EQ(l.StartLocation(), TextLocation(7, 9));
    EXPECT_EQ(l.EndLocation(), TextLocation(7, 12));
}

// ---- Parent / child index --------------------------------------------------

TEST(CSharp_AstNode, UnparentedDefaults) {
    StubExpr e;
    EXPECT_EQ(e.Parent(), nullptr);
    EXPECT_EQ(e.ChildIndex, -1);
    EXPECT_TRUE(e.ChildIndicesValid());
}

TEST(CSharp_AstNode, SetParentAndClearParentAndIndex) {
    StubExpr e;
    StubExpr p;
    e.SetParent(&p);
    EXPECT_EQ(e.Parent(), &p);
    e.ClearParentAndIndex();
    EXPECT_EQ(e.Parent(), nullptr);
    EXPECT_EQ(e.ChildIndex, -1);
}

TEST(CSharp_AstNode, InvalidateAndRestoreChildIndices) {
    StubExpr e;
    EXPECT_TRUE(e.ChildIndicesValid());
    e.InvalidateChildIndices();
    EXPECT_FALSE(e.ChildIndicesValid());
    // Slot() on an unparented node returns null without touching indices; trigger
    // EnsureChildIndices indirectly is covered by the StubList tests below. Here a
    // childless node re-validates with no children to reindex.
    // (EnsureChildIndices is exercised via Slot/sibling reads on StubList.)
    (void)e;
}

// ---- Slot storage defaults (childless node) --------------------------------

TEST(CSharp_AstNode, ChildlessNodeDefaults) {
    StubExpr e;
    EXPECT_EQ(e.GetChildCount(), 0);
    EXPECT_EQ(e.FirstChild(), nullptr);
    EXPECT_EQ(e.LastChild(), nullptr);
    EXPECT_FALSE(e.HasChildren());
    EXPECT_THROW(e.GetChild(0), std::out_of_range);
    EXPECT_THROW(e.SetChild(0, nullptr), std::out_of_range);
    EXPECT_THROW(e.GetChildSlotInfo(0), std::out_of_range);
    EXPECT_EQ(e.GetCollectionByKind(nullptr), nullptr);
    // CloneChildrenInto default is a no-op.
    e.CloneChildrenInto(nullptr);
}

// ---- Slot / navigation: known-index set path (StubBinary) -----------------

TEST(CSharp_AstNode, UnparentedSlotIsNull) {
    StubExpr e;
    EXPECT_EQ(e.Slot(), nullptr);
}

TEST(CSharp_AstNode, KnownIndexSetAssignsChildIndexAndSlot) {
    StubBinary b;
    StubExpr left;
    StubExpr right;
    b.SetChild(0, &left);
    b.SetChild(1, &right);

    // The known-index path assigns ChildIndex directly and keeps the parent valid.
    EXPECT_EQ(left.ChildIndex, 0);
    EXPECT_EQ(right.ChildIndex, 1);
    EXPECT_EQ(left.Parent(), &b);
    EXPECT_EQ(right.Parent(), &b);
    EXPECT_TRUE(b.ChildIndicesValid());

    EXPECT_EQ(left.Slot(), &StubBinary::LeftSlot);
    EXPECT_EQ(right.Slot(), &StubBinary::RightSlot);
}

TEST(CSharp_AstNode, SiblingNavigationKnownIndex) {
    StubBinary b;
    StubExpr left;
    StubExpr right;
    b.SetChild(0, &left);
    b.SetChild(1, &right);

    EXPECT_EQ(b.GetChildCount(), 2);
    EXPECT_TRUE(b.HasChildren());
    EXPECT_EQ(b.FirstChild(), &left);
    EXPECT_EQ(b.LastChild(), &right);

    EXPECT_EQ(left.NextSibling(), &right);
    EXPECT_EQ(left.PrevSibling(), nullptr);
    EXPECT_EQ(right.PrevSibling(), &left);
    EXPECT_EQ(right.NextSibling(), nullptr);
}

TEST(CSharp_AstNode, EmptySlotsAreSkippedInNavigation) {
    // A single slot left empty (null) is skipped by sibling/child navigation: with only
    // the Right slot filled, FirstChild crosses the empty Left slot to Right, and
    // Right has no previous sibling.
    StubBinary b;
    StubExpr right;
    b.SetChild(1, &right);  // index 0 (Left) left empty

    EXPECT_EQ(b.FirstChild(), &right);  // empty slot 0 skipped -> index 1
    EXPECT_EQ(b.LastChild(), &right);
    EXPECT_EQ(right.PrevSibling(), nullptr);  // the empty slot 0 is skipped
    EXPECT_EQ(right.NextSibling(), nullptr);
}

TEST(CSharp_AstNode, SetChildClearsOldChild) {
    // Replacing a slot detaches the old child (ClearParentAndIndex) and parents the
    // new one in its place, reusing the slot's flattened index.
    StubBinary b;
    StubExpr old0;
    StubExpr new0;
    b.SetChild(0, &old0);
    EXPECT_EQ(old0.Parent(), &b);
    b.SetChild(0, &new0);
    EXPECT_EQ(old0.Parent(), nullptr);  // detached
    EXPECT_EQ(old0.ChildIndex, -1);
    EXPECT_EQ(new0.Parent(), &b);
    EXPECT_EQ(new0.ChildIndex, 0);      // index reused
    EXPECT_EQ(new0.Slot(), &StubBinary::LeftSlot);
}

// ---- Slot / navigation: lazy reindex path (StubList) ----------------------

TEST(CSharp_AstNode, LazyReindexAssignsChildIndexOnFirstRead) {
    StubList l;
    StubExpr a;
    StubExpr c;
    l.Append(&a);
    l.Append(&c);

    // Append invalidates and does NOT assign ChildIndex; the indices are stale.
    EXPECT_FALSE(l.ChildIndicesValid());
    EXPECT_EQ(a.ChildIndex, -1);
    EXPECT_EQ(c.ChildIndex, -1);

    // The first Slot/sibling read triggers EnsureChildIndices, which assigns the
    // flattened indices and re-validates the parent.
    ASSERT_EQ(a.Slot(), &StubList::ItemSlot);
    EXPECT_EQ(a.ChildIndex, 0);
    EXPECT_EQ(c.ChildIndex, 1);
    EXPECT_TRUE(l.ChildIndicesValid());
}

TEST(CSharp_AstNode, SiblingNavigationLazyReindex) {
    StubList l;
    StubExpr a;
    StubExpr c;
    l.Append(&a);
    l.Append(&c);

    // Trigger the lazy reindex, then exercise navigation.
    (void)a.Slot();
    EXPECT_EQ(l.GetChildCount(), 2);
    EXPECT_EQ(l.FirstChild(), &a);
    EXPECT_EQ(l.LastChild(), &c);
    EXPECT_EQ(a.NextSibling(), &c);
    EXPECT_EQ(c.PrevSibling(), &a);
    EXPECT_EQ(a.PrevSibling(), nullptr);
    EXPECT_EQ(c.NextSibling(), nullptr);
}

TEST(CSharp_AstNode, EmptyListHasNoChildren) {
    StubList l;
    EXPECT_EQ(l.GetChildCount(), 0);
    EXPECT_FALSE(l.HasChildren());
    EXPECT_EQ(l.FirstChild(), nullptr);
    EXPECT_EQ(l.LastChild(), nullptr);
}

// ---- Pattern matching (INode delegation) -----------------------------------

TEST(CSharp_AstNode, DoMatchDelegatesToTypedDoMatchOnAstNodeCandidate) {
    TrackingExpr pattern;
    TrackingExpr candidate;
    Match m = Match::CreateNew();
    // Calling through the INode interface exercises the explicit-interface delegation
    // (the C# `((INode)pattern).DoMatch`): a non-null AstNode candidate downcasts and
    // delegates to the typed DoMatch. Calling via INode* also dodges the C++
    // name-hiding of the base DoMatch(INode*, Match) by the derived DoMatch(AstNode*).
    INode* patternNode = &pattern;
    bool r = patternNode->DoMatch(static_cast<INode*>(&candidate), m);
    EXPECT_TRUE(r);
    EXPECT_EQ(pattern.DoMatchCalls, 1);
    EXPECT_EQ(pattern.LastOther, &candidate);
    EXPECT_TRUE(m.Has("captured"));
    auto captured = m.Get("captured");
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], static_cast<INode*>(&candidate));
}

TEST(CSharp_AstNode, DoMatchNullCandidateDelegatesAndFails) {
    TrackingExpr pattern;
    Match m = Match::CreateNew();
    // A null candidate: the delegation accepts it (an absent child may match) and
    // still calls the typed DoMatch, which decides (this stub returns false for
    // null). Match.Add no-ops on a null node, so no capture is recorded.
    INode* patternNode = &pattern;
    bool r = patternNode->DoMatch(static_cast<INode*>(nullptr), m);
    EXPECT_FALSE(r);
    EXPECT_EQ(pattern.DoMatchCalls, 1);   // the typed DoMatch WAS called
    EXPECT_EQ(pattern.LastOther, nullptr);
    EXPECT_FALSE(m.Has("captured"));      // null candidate is not captured
}

TEST(CSharp_AstNode, DoMatchNonAstNodeCandidateFailsWithoutDelegating) {
    TrackingExpr pattern;
    PurePatternNode pp;
    Match m = Match::CreateNew();
    // A non-null candidate that is not an AstNode fails at the delegation gate; the
    // typed DoMatch is NOT called (the call counter stays 0), so no capture is
    // recorded.
    INode* patternNode = &pattern;
    bool r = patternNode->DoMatch(static_cast<INode*>(&pp), m);
    EXPECT_FALSE(r);
    EXPECT_EQ(pattern.DoMatchCalls, 0);
    EXPECT_FALSE(m.Has("captured"));
}

TEST(CSharp_AstNode, DoMatchCollectionDelegatesAtPosition) {
    TrackingExpr pattern;
    TrackingExpr c0;
    std::vector<INode*> v{static_cast<INode*>(&c0)};
    BacktrackingInfo bi;
    Match m = Match::CreateNew();
    INode* patternNode = &pattern;
    EXPECT_TRUE(patternNode->DoMatchCollection(v, 0, m, bi));
    EXPECT_EQ(pattern.DoMatchCalls, 1);
    EXPECT_EQ(pattern.LastOther, &c0);
    EXPECT_TRUE(m.Has("captured"));

    // Past the end: the candidate is absent (null); the delegation still calls the
    // typed DoMatch, which returns false for null (and Match.Add no-ops on null).
    Match m2 = Match::CreateNew();
    EXPECT_FALSE(patternNode->DoMatchCollection(v, 1, m2, bi));
    EXPECT_FALSE(m2.Has("captured"));

    // Empty list at pos 0: absent candidate -> false.
    std::vector<INode*> empty;
    Match m3 = Match::CreateNew();
    EXPECT_FALSE(patternNode->DoMatchCollection(empty, 0, m3, bi));
}

// ---- Children (ChildrenCollection + ChildEnumerator) ----------------------

TEST(CSharp_AstNode, ChildrenCountAndIndex) {
    StubBinary b;
    StubExpr left;
    StubExpr right;
    b.SetChild(0, &left);
    b.SetChild(1, &right);

    ChildrenCollection children = b.Children();
    EXPECT_EQ(children.Count(), 2);
    EXPECT_EQ(children[0], &left);
    EXPECT_EQ(children[1], &right);
}

TEST(CSharp_AstNode, ChildrenRangeForOrder) {
    StubBinary b;
    StubExpr left;
    StubExpr right;
    b.SetChild(0, &left);
    b.SetChild(1, &right);

    std::vector<AstNode*> visited;
    for (AstNode* child : b.Children())
        visited.push_back(child);
    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], &left);
    EXPECT_EQ(visited[1], &right);
}

TEST(CSharp_AstNode, ChildrenEmptyNode) {
    StubExpr e;
    ChildrenCollection children = e.Children();
    EXPECT_EQ(children.Count(), 0);
    EXPECT_THROW(children[0], std::out_of_range);
    // range-for over an empty collection does not execute the body.
    int count = 0;
    for (AstNode* child : e.Children())
        count++;
    EXPECT_EQ(count, 0);
}

TEST(CSharp_AstNode, ChildrenEmptySlotsAreSkipped) {
    // A single slot left empty is skipped: only the Right child is present.
    StubBinary b;
    StubExpr right;
    b.SetChild(1, &right);  // index 0 (Left) empty

    ChildrenCollection children = b.Children();
    EXPECT_EQ(children.Count(), 1);
    EXPECT_EQ(children[0], &right);
}

TEST(CSharp_AstNode, ChildrenEnumeratorMoveNextCurrentReset) {
    StubList l;
    StubExpr a;
    StubExpr c;
    l.Append(&a);
    l.Append(&c);

    ChildEnumerator e = l.Children().GetEnumerator();
    EXPECT_EQ(e.Current(), nullptr);  // before MoveNext
    EXPECT_TRUE(e.MoveNext());
    EXPECT_EQ(e.Current(), &a);
    EXPECT_TRUE(e.MoveNext());
    EXPECT_EQ(e.Current(), &c);
    EXPECT_FALSE(e.MoveNext());
    EXPECT_EQ(e.Current(), nullptr);

    e.Reset();
    EXPECT_TRUE(e.MoveNext());
    EXPECT_EQ(e.Current(), &a);
}

TEST(CSharp_AstNode, ChildrenEnumeratorSkipsReplacementMidLoop) {
    // The enumerator captures each child's successor before yielding it, so replacing the
    // current child mid-loop advances to the captured successor (the replacement is not
    // re-visited) -- the hand-over-hand guarantee the transforms rely on.
    StubList l;
    StubExpr a, b, c, x;
    l.Append(&a);
    l.Append(&b);
    l.Append(&c);

    std::vector<AstNode*> visited;
    ChildEnumerator e = l.Children().GetEnumerator();
    while (e.MoveNext()) {
        AstNode* cur = e.Current();
        visited.push_back(cur);
        if (cur == &a)
            l.SetChild(0, &x);  // replace `a` (index 0) with `x`
    }
    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], &a);
    EXPECT_EQ(visited[1], &b);   // the captured successor, not the replacement `x`
    EXPECT_EQ(visited[2], &c);
}

// ---- Ancestors / AncestorsAndSelf ----------------------------------------

TEST(CSharp_AstNode, AncestorsWalksParentChain) {
    // Tree: outer[bin, leaf] where bin[L1, L2].
    StubList outer;
    StubBinary bin;
    StubExpr leaf, L1, L2;
    outer.Append(&bin);
    outer.Append(&leaf);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    auto ancestors = L1.Ancestors();
    ASSERT_EQ(ancestors.size(), 2u);
    EXPECT_EQ(ancestors[0], &bin);
    EXPECT_EQ(ancestors[1], &outer);

    auto andSelf = L1.AncestorsAndSelf();
    ASSERT_EQ(andSelf.size(), 3u);
    EXPECT_EQ(andSelf[0], &L1);
    EXPECT_EQ(andSelf[1], &bin);
    EXPECT_EQ(andSelf[2], &outer);
}

TEST(CSharp_AstNode, AncestorsOfRootIsEmpty) {
    StubExpr root;
    EXPECT_TRUE(root.Ancestors().empty());
    ASSERT_EQ(root.AncestorsAndSelf().size(), 1u);
    EXPECT_EQ(root.AncestorsAndSelf()[0], &root);
}

// ---- Descendants / DescendantsAndSelf / DescendantNodes -----------------

TEST(CSharp_AstNode, DescendantsPreOrder) {
    // Tree: outer[bin, leaf] where bin[L1, L2]. Pre-order: bin, L1, L2, leaf.
    StubList outer;
    StubBinary bin;
    StubExpr leaf, L1, L2;
    outer.Append(&bin);
    outer.Append(&leaf);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    auto desc = outer.Descendants();
    ASSERT_EQ(desc.size(), 4u);
    EXPECT_EQ(desc[0], &bin);
    EXPECT_EQ(desc[1], &L1);
    EXPECT_EQ(desc[2], &L2);
    EXPECT_EQ(desc[3], &leaf);

    auto descSelf = outer.DescendantsAndSelf();
    ASSERT_EQ(descSelf.size(), 5u);
    EXPECT_EQ(descSelf[0], &outer);
    EXPECT_EQ(descSelf[1], &bin);
    EXPECT_EQ(descSelf[2], &L1);
    EXPECT_EQ(descSelf[3], &L2);
    EXPECT_EQ(descSelf[4], &leaf);
}

TEST(CSharp_AstNode, DescendantsOfLeafIsEmpty) {
    StubExpr e;
    EXPECT_TRUE(e.Descendants().empty());
    ASSERT_EQ(e.DescendantsAndSelf().size(), 1u);
    EXPECT_EQ(e.DescendantsAndSelf()[0], &e);
}

TEST(CSharp_AstNode, DescendantNodesDescendIntoChildrenPredicate) {
    // Tree: outer[bin, leaf] where bin[L1, L2]. With a predicate that vetoes descending
    // into `bin`, the walk yields bin then skips its children, resuming at the stacked
    // sibling `leaf`.
    StubList outer;
    StubBinary bin;
    StubExpr leaf, L1, L2;
    outer.Append(&bin);
    outer.Append(&leaf);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    auto desc = outer.DescendantNodes([&](AstNode* n) { return n != &bin; });
    ASSERT_EQ(desc.size(), 2u);
    EXPECT_EQ(desc[0], &bin);
    EXPECT_EQ(desc[1], &leaf);  // bin's children skipped, resume at the stacked sibling

    // DescendantNodesAndSelf includes `outer` first, then respects the predicate.
    auto descSelf = outer.DescendantNodesAndSelf([&](AstNode* n) { return n != &bin; });
    ASSERT_EQ(descSelf.size(), 3u);
    EXPECT_EQ(descSelf[0], &outer);
    EXPECT_EQ(descSelf[1], &bin);
    EXPECT_EQ(descSelf[2], &leaf);
}

// ---- GetParent ----------------------------------------------------------

TEST(CSharp_AstNode, GetParentTypedReturnsFirstAncestorOfType) {
    StubList outer;
    StubBinary bin;
    StubExpr L1, L2;
    outer.Append(&bin);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    EXPECT_EQ(L1.GetParent<StubBinary>(), &bin);
    EXPECT_EQ(L1.GetParent<StubList>(), &outer);
    // No StubExpr ancestor (L1's ancestors are bin and outer, neither is a StubExpr).
    EXPECT_EQ(L1.GetParent<StubExpr>(), nullptr);
}

TEST(CSharp_AstNode, GetParentPredicate) {
    StubList outer;
    StubBinary bin;
    StubExpr L1, L2;
    outer.Append(&bin);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    // Empty predicate: the first ancestor (bin).
    EXPECT_EQ(L1.GetParent({}), &bin);
    // Predicate matching outer: returns outer.
    EXPECT_EQ(L1.GetParent([&](AstNode* n) { return n == &outer; }), &outer);
    // Predicate matching nothing: returns nullptr.
    EXPECT_EQ(L1.GetParent([](AstNode* /*n*/) { return false; }), nullptr);
}

// ---- GetNextNode / GetPrevNode (document order) --------------------------

TEST(CSharp_AstNode, GetNextNodeDocumentOrder) {
    // Tree: outer[bin, leaf] where bin[L1, L2]. Document order: bin, L1, L2, leaf.
    StubList outer;
    StubBinary bin;
    StubExpr leaf, L1, L2;
    outer.Append(&bin);
    outer.Append(&leaf);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    EXPECT_EQ(L1.GetNextNode(), &L2);   // sibling
    EXPECT_EQ(L2.GetNextNode(), &leaf); // parent bin's next sibling
    EXPECT_EQ(bin.GetNextNode(), &leaf);
    EXPECT_EQ(leaf.GetNextNode(), nullptr);  // root's next is null
}

TEST(CSharp_AstNode, GetPrevNodeDocumentOrder) {
    // Tree: outer[bin, leaf] where bin[L1, L2].
    StubList outer;
    StubBinary bin;
    StubExpr leaf, L1, L2;
    outer.Append(&bin);
    outer.Append(&leaf);
    bin.SetChild(0, &L1);
    bin.SetChild(1, &L2);

    EXPECT_EQ(L2.GetPrevNode(), &L1);   // sibling
    EXPECT_EQ(leaf.GetPrevNode(), &bin);// PrevSibling(leaf) = bin
    EXPECT_EQ(L1.GetPrevNode(), nullptr);  // first child; bin's prev is null (root)
    EXPECT_EQ(bin.GetPrevNode(), nullptr);  // first child; outer is root
}

// ---- Contains / IsInside (location queries) ------------------------------

TEST(CSharp_AstNode, ContainsAndIsInside) {
    StubExpr e;
    e.StorePrintStart(TextLocation(1, 1));
    e.StorePrintEnd(TextLocation(1, 5));

    // Contains: half-open [start, end).
    EXPECT_TRUE(e.Contains(1, 1));   // start
    EXPECT_TRUE(e.Contains(1, 3));  // interior
    EXPECT_FALSE(e.Contains(1, 5)); // end (excluded)
    EXPECT_FALSE(e.Contains(2, 1));  // past end (next line)
    EXPECT_FALSE(e.Contains(1, 0)); // before start

    // IsInside: closed [start, end].
    EXPECT_TRUE(e.IsInside(1, 1));
    EXPECT_TRUE(e.IsInside(1, 5));  // end (included)
    EXPECT_FALSE(e.IsInside(1, 6)); // past end
    EXPECT_FALSE(e.IsInside(2, 1));

    // TextLocation overloads agree with the (line, column) overloads.
    EXPECT_TRUE(e.Contains(TextLocation(1, 3)));
    EXPECT_TRUE(e.IsInside(TextLocation(1, 5)));
}
