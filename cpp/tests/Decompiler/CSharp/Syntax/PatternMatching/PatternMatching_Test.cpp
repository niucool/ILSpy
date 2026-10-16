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

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/BacktrackingInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching;

namespace {
// A minimal candidate node: an INode that is matched against but does not itself
// drive matching. Its DoMatch/DoMatchCollection are never exercised by these
// tests (the pattern side drives), but INode is abstract so they must be defined.
class StubNode : public INode {
public:
    bool DoMatch(INode* /*other*/, Match /*match*/) override { return false; }
    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match, BacktrackingInfo& /*bi*/) override {
        return DoMatch(pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)] : nullptr, match);
    }
};

// Faithful to AnyNode: matches any non-null node, optionally capturing it under a
// group name (a null group does not capture).
class TestAnyNode : public Pattern {
    std::optional<std::string_view> group_;
public:
    TestAnyNode() = default;
    explicit TestAnyNode(std::string_view group) : group_(group) {}
    bool DoMatch(INode* other, Match match) override {
        match.Add(group_, other);
        return other != nullptr;
    }
};

// Faithful to OptionalNode: pushes the "absent" alternative (resume the next
// pattern node at the same position), then tries to consume the candidate here.
class TestOptionalNode : public Pattern {
    INode* child_;
public:
    explicit TestOptionalNode(INode* child) : child_(child) {}
    bool DoMatch(INode* other, Match match) override {
        return other == nullptr || child_->DoMatch(other, match);
    }
    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match, BacktrackingInfo& bi) override {
        bi.BacktrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        return child_->DoMatch(pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)] : nullptr, match);
    }
};
} // namespace

// ---- Match: success / failure sentinel -------------------------------------

TEST(CSharp_PatternMatching_Match, DefaultMatchIsFailure) {
    Match m;
    EXPECT_FALSE(m.Success());
}

TEST(CSharp_PatternMatching_Match, CreateNewIsSuccess) {
    Match m = Match::CreateNew();
    EXPECT_TRUE(m.Success());
}

// ---- Match: Add / Has / Get -------------------------------------------------

TEST(CSharp_PatternMatching_Match, AddCapturesUnderGroup) {
    Match m = Match::CreateNew();
    StubNode n;
    m.Add("g", &n);
    EXPECT_TRUE(m.Has("g"));
    auto got = m.Get("g");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], &n);
}

TEST(CSharp_PatternMatching_Match, AddWithNullGroupDoesNotCapture) {
    Match m = Match::CreateNew();
    StubNode n;
    m.Add(std::nullopt, &n);
    // Nothing was captured, so every group is absent.
    EXPECT_FALSE(m.Has("g"));
    EXPECT_TRUE(m.Get("g").empty());
}

TEST(CSharp_PatternMatching_Match, AddNullRecordsNullCapture) {
    Match m = Match::CreateNew();
    m.AddNull("g");
    EXPECT_TRUE(m.Has("g"));
    auto got = m.Get("g");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], nullptr);
}

TEST(CSharp_PatternMatching_Match, HasIsFalseForUnknownGroup) {
    Match m = Match::CreateNew();
    EXPECT_FALSE(m.Has("nope"));
}

TEST(CSharp_PatternMatching_Match, HasIsFalseOnFailureSentinel) {
    Match m; // failure sentinel
    EXPECT_FALSE(m.Has("g"));
}

TEST(CSharp_PatternMatching_Match, GetReturnsAllCapturesForGroupInOrder) {
    Match m = Match::CreateNew();
    StubNode a, b;
    m.Add("g", &a);
    m.Add("other", &b);
    m.Add("g", &b);
    auto got = m.Get("g");
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], &a);
    EXPECT_EQ(got[1], &b);
}

TEST(CSharp_PatternMatching_Match, GetTypedDownCastsCapture) {
    Match m = Match::CreateNew();
    TestAnyNode n;
    m.Add("g", &n);
    auto got = m.Get<TestAnyNode>("g");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], &n);
}

TEST(CSharp_PatternMatching_Match, GetTypedReturnsNullForWrongType) {
    Match m = Match::CreateNew();
    StubNode n; // a StubNode is not a TestAnyNode
    m.Add("g", &n);
    auto got = m.Get<TestAnyNode>("g");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], nullptr); // dynamic_cast fails -> nullptr
}

// ---- Match: CheckPoint / RestoreCheckPoint --------------------------------

TEST(CSharp_PatternMatching_Match, CheckPointAndRestoreTruncates) {
    Match m = Match::CreateNew();
    StubNode a, b, c;
    m.Add("g", &a);
    m.Add("g", &b);
    int cp = m.CheckPoint();
    EXPECT_EQ(cp, 2);
    m.Add("g", &c);
    EXPECT_EQ(m.CheckPoint(), 3);
    m.RestoreCheckPoint(cp);
    EXPECT_EQ(m.Get("g").size(), 2u);
}

// ---- Match: copy shares the capture vector ---------------------------------

TEST(CSharp_PatternMatching_Match, CopySharesCaptures) {
    Match m = Match::CreateNew();
    StubNode a;
    Match copy = m; // shared_ptr copy: both alias the one capture vector
    copy.Add("g", &a);
    EXPECT_TRUE(m.Has("g")); // the original sees the capture added via the copy
    EXPECT_EQ(m.Get("g").size(), 1u);
}

// ---- BacktrackingInfo / PossibleMatch --------------------------------------

TEST(CSharp_PatternMatching_BacktrackingInfo, StackPushPopTop) {
    BacktrackingInfo bi;
    EXPECT_TRUE(bi.BacktrackingStack.empty());
    bi.BacktrackingStack.push(PossibleMatch(3, 10));
    bi.BacktrackingStack.push(PossibleMatch(5, 20));
    EXPECT_EQ(bi.BacktrackingStack.size(), 2u);
    EXPECT_EQ(bi.BacktrackingStack.top().NextOtherIndex, 5);
    EXPECT_EQ(bi.BacktrackingStack.top().Checkpoint, 20);
    bi.BacktrackingStack.pop();
    EXPECT_EQ(bi.BacktrackingStack.top().NextOtherIndex, 3);
    EXPECT_EQ(bi.BacktrackingStack.top().Checkpoint, 10);
}

TEST(CSharp_PatternMatching_BacktrackingInfo, PossibleMatchRecordsFields) {
    PossibleMatch pm(7, 42);
    EXPECT_EQ(pm.NextOtherIndex, 7);
    EXPECT_EQ(pm.Checkpoint, 42);
}

// ---- Pattern.MatchString ---------------------------------------------------

TEST(CSharp_PatternMatching_Pattern, AnyStringMatchesAnything) {
    EXPECT_TRUE(Pattern::MatchString(Pattern::AnyString, std::string_view("x")));
    EXPECT_TRUE(Pattern::MatchString(Pattern::AnyString, std::string_view("")));
}

TEST(CSharp_PatternMatching_Pattern, AnyStringMatchesNull) {
    EXPECT_TRUE(Pattern::MatchString(Pattern::AnyString, std::nullopt));
}

TEST(CSharp_PatternMatching_Pattern, EqualStringsMatch) {
    EXPECT_TRUE(Pattern::MatchString(std::string_view("x"), std::string_view("x")));
}

TEST(CSharp_PatternMatching_Pattern, DifferentStringsDoNotMatch) {
    EXPECT_FALSE(Pattern::MatchString(std::string_view("x"), std::string_view("y")));
}

TEST(CSharp_PatternMatching_Pattern, NullPatternMatchesOnlyNull) {
    EXPECT_TRUE(Pattern::MatchString(std::nullopt, std::nullopt));
    EXPECT_FALSE(Pattern::MatchString(std::nullopt, std::string_view("x")));
}

TEST(CSharp_PatternMatching_Pattern, NonNullPatternDoesNotMatchNull) {
    EXPECT_FALSE(Pattern::MatchString(std::string_view("x"), std::nullopt));
}

// ---- Pattern.DoMatchCollection (deterministic) -----------------------------

TEST(CSharp_PatternMatching_DoMatchCollection, EmptyPatternMatchesEmptyOther) {
    std::vector<INode*> pattern;
    std::vector<INode*> other;
    EXPECT_TRUE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, SingleAnyMatchesSingleCandidate) {
    TestAnyNode any;
    StubNode x;
    std::vector<INode*> pattern = { &any };
    std::vector<INode*> other = { &x };
    EXPECT_TRUE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, SingleAnyDoesNotMatchTwoCandidates) {
    TestAnyNode any;
    StubNode x, y;
    std::vector<INode*> pattern = { &any };
    std::vector<INode*> other = { &x, &y };
    EXPECT_FALSE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, TwoAnysDoNotMatchSingleCandidate) {
    // The second Any would have to match an absent (null) candidate; AnyNode
    // rejects null, so this fails (the backtracking stack empties).
    TestAnyNode a, b;
    StubNode x;
    std::vector<INode*> pattern = { &a, &b };
    std::vector<INode*> other = { &x };
    EXPECT_FALSE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, TwoAnysMatchTwoCandidates) {
    TestAnyNode a, b;
    StubNode x, y;
    std::vector<INode*> pattern = { &a, &b };
    std::vector<INode*> other = { &x, &y };
    EXPECT_TRUE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

// ---- Pattern.DoMatchCollection (backtracking via OptionalNode) ------------

TEST(CSharp_PatternMatching_DoMatchCollection, OptionalThenAnyMatchesSingleViaSkip) {
    // Optional(any) may consume the element (then Any has nothing and fails) OR
    // skip (then Any matches the element). The skip alternative is pushed onto
    // the backtracking stack; the consume path fails, the loop pops the skip and
    // retries Any at the same position, which succeeds.
    TestAnyNode any;
    TestOptionalNode opt(&any);
    TestAnyNode tail;
    StubNode x;
    std::vector<INode*> pattern = { &opt, &tail };
    std::vector<INode*> other = { &x };
    EXPECT_TRUE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, OptionalThenAnyDoesNotMatchEmpty) {
    // Both paths fail: Optional consuming an absent element -> any.DoMatch(null)
    // = false; Optional skipping -> Any matches null = false. So empty does not
    // match [Optional(any), Any].
    TestAnyNode any;
    TestOptionalNode opt(&any);
    TestAnyNode tail;
    std::vector<INode*> pattern = { &opt, &tail };
    std::vector<INode*> other;
    EXPECT_FALSE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, OptionalThenAnyMatchesTwoViaConsume) {
    // Optional consumes the first (any matches it), Any matches the second: the
    // consume path succeeds on the first try, no backtracking needed.
    TestAnyNode any;
    TestOptionalNode opt(&any);
    TestAnyNode tail;
    StubNode x, y;
    std::vector<INode*> pattern = { &opt, &tail };
    std::vector<INode*> other = { &x, &y };
    EXPECT_TRUE(Pattern::DoMatchCollection(pattern, other, Match::CreateNew()));
}

TEST(CSharp_PatternMatching_DoMatchCollection, CapturesPropagateToCallerMatch) {
    // A capture recorded by a pattern node deep in the recursion reaches the
    // caller's Match (the shared capture vector): the AnyNode named "first"
    // captures the single candidate, and the caller's match records it.
    TestAnyNode first("first");
    StubNode x;
    std::vector<INode*> pattern = { &first };
    std::vector<INode*> other = { &x };
    Match m = Match::CreateNew();
    ASSERT_TRUE(Pattern::DoMatchCollection(pattern, other, m));
    EXPECT_TRUE(m.Has("first"));
    ASSERT_EQ(m.Get("first").size(), 1u);
    EXPECT_EQ(m.Get("first")[0], &x);
}
