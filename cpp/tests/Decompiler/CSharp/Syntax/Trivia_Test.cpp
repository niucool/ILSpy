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

// Tests for the trivia half of the annotation channel (cpp/Decompiler/CSharp/Syntax/
// Trivia.hpp + the trivia mutation path on AstNode.{hpp,cpp}, mirroring the `#region Trivia`
// of ICSharpCode.Decompiler/CSharp/Syntax/AstNode.cs): `AddLeadingTrivia`/`PrependLeadingTrivia`/
// `AddTrailingTrivia`, `LeadingTrivia`/`TrailingTrivia`, `CopyTriviaFrom`, `ReparentTrivia`
// (via `Clone`), `NodeTrivia.Clone`, and the `CheckInvariant`/`CheckTriviaInvariant` debug
// machinery. The `TriviaHolder` stub's `Clone` calls `CloneAnnotationsFrom` + `ReparentTrivia`
// (the concrete-node `Clone` shape) so the annotation+trivia cloning is exercised.

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;

namespace {

// A concrete trivia (the `Comment` shape -- a `Trivia` carrying a `Content` string). The
// port takes ownership on attach, so `Clone()` returns a fresh detached copy (`new`'d).
class TestComment : public Trivia {
public:
    std::string Content;
    explicit TestComment(std::string c) : Content(std::move(c)) {}
    TestComment(std::string c, TextLocation start, TextLocation end)
        : Trivia(start, end), Content(std::move(c)) {}

    AstNode* Clone() const override { return new TestComment(Content, StartLocation(), EndLocation()); }
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

// A leaf owning node (no slots) whose `Clone` follows the port's concrete-node shape: copy
// the (absent here) scalar fields, deep-clone the (absent) children, then
// `CloneAnnotationsFrom(*this)` + `ReparentTrivia()` to copy the annotation channel. This
// exercises the trivia-cloning path the real concrete nodes will use.
class TriviaHolder : public AstNode {
public:
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    AstNode* Clone() const override {
        auto* copy = new TriviaHolder();
        copy->CloneAnnotationsFrom(*this);
        copy->ReparentTrivia();
        return copy;
    }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

// A single-slot node (one optional single slot, known-index set) for the `CheckInvariant`
// slot-space + trivia test. The slot's declared child type is `TestLeaf`; a `TestLeaf` child
// is valid in the slot.
class TestLeaf : public AstNode {
public:
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    AstNode* Clone() const override { return new TestLeaf(); }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

class StubSingleSlot : public AstNode {
    TestLeaf* child_ = nullptr;
public:
    static inline const CSharpSlotInfoT<TestLeaf> ChildSlotKind{"Child", false, nullptr, false};
    static inline const CSharpSlotInfoT<TestLeaf> ChildSlot{"Child", false, &ChildSlotKind, true};

    int GetChildCount() const override { return 1; }
    AstNode* GetChild(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingleSlot::GetChild");
        return child_;
    }
    void SetChild(int index, AstNode* value) override {
        if (index != 0) throw std::out_of_range("StubSingleSlot::SetChild");
        SetChildNode(child_, static_cast<TestLeaf*>(value), 0);
    }
    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        if (index != 0) throw std::out_of_range("StubSingleSlot::GetChildSlotInfo");
        return &ChildSlot;
    }
    bool DoMatch(AstNode* /*other*/, Match /*match*/) override { return false; }
    AstNode* Clone() const override {
        auto* copy = new StubSingleSlot();
        if (child_ != nullptr)
            copy->SetChild(0, static_cast<TestLeaf*>(child_->Clone()));
        copy->CloneAnnotationsFrom(*this);
        copy->ReparentTrivia();
        return copy;
    }
    void AcceptVisitor(IAstVisitor& /*visitor*/) override {}
    bool AcceptVisitorBool(IAstVisitorBool& /*visitor*/) override { return false; }
};

} // namespace

// ---- AddLeadingTrivia / AddTrailingTrivia / LeadingTrivia / TrailingTrivia --

TEST(CSharp_Trivia, AddLeadingTriviaAttachesAndLeadingReturnsIt) {
    TriviaHolder node;
    auto* c = new TestComment("hello");
    node.AddLeadingTrivia(c);
    auto leading = node.LeadingTrivia();
    ASSERT_EQ(leading.size(), 1u);
    EXPECT_EQ(leading[0], c);
    EXPECT_TRUE(node.TrailingTrivia().empty());
    // The trivia is parented to the node, at index 0, with the holder's list as siblings.
    EXPECT_EQ(c->Parent(), &node);
    EXPECT_EQ(c->ChildIndex, 0);
    EXPECT_EQ(c->TriviaSiblings(), &node.Annotation<NodeTrivia>()->Leading);
}

TEST(CSharp_Trivia, AddTrailingTriviaAttachesAndTrailingReturnsIt) {
    TriviaHolder node;
    auto* c = new TestComment("trail");
    node.AddTrailingTrivia(c);
    auto trailing = node.TrailingTrivia();
    ASSERT_EQ(trailing.size(), 1u);
    EXPECT_EQ(trailing[0], c);
    EXPECT_TRUE(node.LeadingTrivia().empty());
    EXPECT_EQ(c->Parent(), &node);
    EXPECT_EQ(c->ChildIndex, 0);
    EXPECT_EQ(c->TriviaSiblings(), &node.Annotation<NodeTrivia>()->Trailing);
}

TEST(CSharp_Trivia, AddLeadingTriviaInInsertionOrder) {
    TriviaHolder node;
    auto* a = new TestComment("a");
    auto* b = new TestComment("b");
    auto* c = new TestComment("c");
    node.AddLeadingTrivia(a);
    node.AddLeadingTrivia(b);
    node.AddLeadingTrivia(c);
    auto leading = node.LeadingTrivia();
    ASSERT_EQ(leading.size(), 3u);
    EXPECT_EQ(leading[0], a);
    EXPECT_EQ(leading[1], b);
    EXPECT_EQ(leading[2], c);
    EXPECT_EQ(a->ChildIndex, 0);
    EXPECT_EQ(b->ChildIndex, 1);
    EXPECT_EQ(c->ChildIndex, 2);
}

TEST(CSharp_Trivia, PrependLeadingTriviaInsertsAtFront) {
    TriviaHolder node;
    auto* a = new TestComment("a");
    auto* b = new TestComment("b");
    node.AddLeadingTrivia(a);  // [a]
    node.PrependLeadingTrivia(b);  // [b, a]
    auto leading = node.LeadingTrivia();
    ASSERT_EQ(leading.size(), 2u);
    EXPECT_EQ(leading[0], b);   // prepended before a
    EXPECT_EQ(leading[1], a);
    EXPECT_EQ(b->ChildIndex, 0);
    EXPECT_EQ(a->ChildIndex, 1);   // reindexed after the insert
}

TEST(CSharp_Trivia, AddLeadingTriviaNullThrows) {
    TriviaHolder node;
    EXPECT_THROW(node.AddLeadingTrivia(nullptr), std::invalid_argument);
    EXPECT_TRUE(node.LeadingTrivia().empty());
}

TEST(CSharp_Trivia, AddLeadingTriviaSelfReferenceThrows) {
    TriviaHolder node;
    EXPECT_THROW(node.AddLeadingTrivia(reinterpret_cast<Trivia*>(&node)), std::invalid_argument);
}

TEST(CSharp_Trivia, AddLeadingTriviaAlreadyParentedThrows) {
    TriviaHolder a, b;
    auto* c = new TestComment("x");
    a.AddLeadingTrivia(c);
    // c is already parented to a; attaching it to b is rejected.
    EXPECT_THROW(b.AddLeadingTrivia(c), std::invalid_argument);
    EXPECT_EQ(c->Parent(), &a);   // unchanged
    EXPECT_TRUE(b.LeadingTrivia().empty());
}

TEST(CSharp_Trivia, LeadingTrailingEmptyWhenNone) {
    TriviaHolder node;
    EXPECT_TRUE(node.LeadingTrivia().empty());
    EXPECT_TRUE(node.TrailingTrivia().empty());
    // No NodeTrivia annotation is created until the first trivia is attached.
    EXPECT_EQ(node.Annotation<NodeTrivia>(), nullptr);
}

// ---- Annotations() integration -------------------------------------------

TEST(CSharp_Trivia, NodeTriviaAppearsInAnnotations) {
    TriviaHolder node;
    auto* c = new TestComment("c");
    node.AddLeadingTrivia(c);
    auto all = node.Annotations();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0], node.Annotation<NodeTrivia>());
}

// ---- CopyTriviaFrom ------------------------------------------------------

TEST(CSharp_Trivia, CopyTriviaFromDeepCopiesAndAppends) {
    TriviaHolder src;
    src.AddLeadingTrivia(new TestComment("a"));
    src.AddLeadingTrivia(new TestComment("b"));
    src.AddTrailingTrivia(new TestComment("z"));

    TriviaHolder dst;
    dst.AddLeadingTrivia(new TestComment("pre"));  // existing leading
    dst.CopyTriviaFrom(src);

    auto dstLeading = dst.LeadingTrivia();
    ASSERT_EQ(dstLeading.size(), 3u);   // the existing "pre" + the copied "a", "b"
    EXPECT_EQ(static_cast<TestComment*>(dstLeading[0])->Content, "pre");
    EXPECT_EQ(static_cast<TestComment*>(dstLeading[1])->Content, "a");
    EXPECT_EQ(static_cast<TestComment*>(dstLeading[2])->Content, "b");
    auto dstTrailing = dst.TrailingTrivia();
    ASSERT_EQ(dstTrailing.size(), 1u);
    EXPECT_EQ(static_cast<TestComment*>(dstTrailing[0])->Content, "z");

    // The copies are DISTINCT objects (deep copy), parented to dst (not src).
    auto srcLeading = src.LeadingTrivia();
    EXPECT_NE(dstLeading[1], srcLeading[0]);
    EXPECT_NE(dstLeading[2], srcLeading[1]);
    EXPECT_EQ(dstLeading[1]->Parent(), &dst);
    EXPECT_EQ(srcLeading[0]->Parent(), &src);   // src untouched
}

TEST(CSharp_Trivia, CopyTriviaFromNoOpWhenSourceHasNone) {
    TriviaHolder src;
    TriviaHolder dst;
    dst.AddLeadingTrivia(new TestComment("x"));
    dst.CopyTriviaFrom(src);   // src has no trivia; dst is unchanged
    ASSERT_EQ(dst.LeadingTrivia().size(), 1u);
    EXPECT_EQ(static_cast<TestComment*>(dst.LeadingTrivia()[0])->Content, "x");
}

// ---- Clone (CloneAnnotationsFrom + ReparentTrivia) ------------------------

TEST(CSharp_Trivia, CloneDeepCopiesAndReparentsTrivia) {
    TriviaHolder src;
    src.AddLeadingTrivia(new TestComment("a"));
    src.AddLeadingTrivia(new TestComment("b"));
    src.AddTrailingTrivia(new TestComment("z"));

    AstNode* cloneRaw = src.Clone();
    ASSERT_NE(cloneRaw, nullptr);
    auto* clone = static_cast<TriviaHolder*>(cloneRaw);
    ASSERT_NE(clone, &src);

    auto srcLeading = src.LeadingTrivia();
    auto cloneLeading = clone->LeadingTrivia();
    ASSERT_EQ(cloneLeading.size(), 2u);
    // Deep copies: distinct objects with the same content.
    EXPECT_NE(cloneLeading[0], srcLeading[0]);
    EXPECT_NE(cloneLeading[1], srcLeading[1]);
    EXPECT_EQ(static_cast<TestComment*>(cloneLeading[0])->Content, "a");
    EXPECT_EQ(static_cast<TestComment*>(cloneLeading[1])->Content, "b");
    auto cloneTrailing = clone->TrailingTrivia();
    ASSERT_EQ(cloneTrailing.size(), 1u);
    EXPECT_EQ(static_cast<TestComment*>(cloneTrailing[0])->Content, "z");

    // ReparentTrivia: the cloned trivia point at the clone, not the source.
    EXPECT_EQ(cloneLeading[0]->Parent(), clone);
    EXPECT_EQ(cloneLeading[1]->Parent(), clone);
    EXPECT_EQ(cloneTrailing[0]->Parent(), clone);
    EXPECT_EQ(cloneLeading[0]->ChildIndex, 0);
    EXPECT_EQ(cloneLeading[1]->ChildIndex, 1);
    EXPECT_EQ(cloneTrailing[0]->ChildIndex, 0);
    EXPECT_EQ(cloneLeading[0]->TriviaSiblings(), &clone->Annotation<NodeTrivia>()->Leading);

    // The source is untouched.
    EXPECT_EQ(srcLeading[0]->Parent(), &src);
    EXPECT_EQ(srcLeading[1]->Parent(), &src);
    auto srcTrailing = src.TrailingTrivia();
    EXPECT_EQ(srcTrailing[0]->Parent(), &src);

    delete clone;
}

// ---- CheckInvariant (debug machinery, pass cases) -------------------------

TEST(CSharp_Trivia, CheckInvariantPassesOnLeafWithoutTrivia) {
    TriviaHolder node;
    EXPECT_NO_FATAL_FAILURE(node.CheckInvariant());   // no slots, no trivia -> consistent
}

TEST(CSharp_Trivia, CheckInvariantPassesOnLeafWithTrivia) {
    TriviaHolder node;
    node.AddLeadingTrivia(new TestComment("a"));
    node.AddTrailingTrivia(new TestComment("z"));
    EXPECT_NO_FATAL_FAILURE(node.CheckInvariant());   // trivia consistent (parent/siblings/index)
}

TEST(CSharp_Trivia, CheckInvariantPassesOnFilledSlotWithTrivia) {
    StubSingleSlot node;
    TestLeaf child;
    node.SetChild(0, &child);
    node.AddLeadingTrivia(new TestComment("c"));
    EXPECT_NO_FATAL_FAILURE(node.CheckInvariant());   // slot filled + trivia consistent
}

TEST(CSharp_Trivia, CheckInvariantPassesOnClonedTree) {
    TriviaHolder src;
    src.AddLeadingTrivia(new TestComment("a"));
    src.AddTrailingTrivia(new TestComment("z"));
    EXPECT_NO_FATAL_FAILURE(src.CheckInvariant());
    AstNode* clone = src.Clone();
    EXPECT_NO_FATAL_FAILURE(clone->CheckInvariant());   // clone's trivia re-parented -> consistent
    delete clone;
}

// ---- Trivia source location ----------------------------------------------

TEST(CSharp_Trivia, TriviaCarriesItsOwnLocation) {
    TestComment c("x", TextLocation(2, 5), TextLocation(2, 12));
    EXPECT_EQ(c.StartLocation(), TextLocation(2, 5));
    EXPECT_EQ(c.EndLocation(), TextLocation(2, 12));
    c.SetStartLocation(TextLocation(3, 1));
    c.SetEndLocation(TextLocation(3, 9));
    EXPECT_EQ(c.StartLocation(), TextLocation(3, 1));
    EXPECT_EQ(c.EndLocation(), TextLocation(3, 9));
}
