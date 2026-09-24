// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software") to deal in the Software
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

// Tests for the concrete pattern nodes (PatternNodes.hpp): AnyNode, AnyNodeOrNull,
// NamedNode, Choice, OptionalNode, Repeat, Backreference, and the
// IdentifierExpressionBackreference -- plus the PatternExtensions free functions
// (MatchNode / IsMatchPattern).

#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::AnyNode;
using PatternMatching::AnyNodeOrNull;
using PatternMatching::Backreference;
using PatternMatching::Choice;
using PatternMatching::IdentifierExpressionBackreference;
using PatternMatching::NamedNode;
using PatternMatching::OptionalNode;
using PatternMatching::Repeat;

namespace {

// A candidate side node: matches nothing itself; used as the matched-against
// value (the C# tests use real AST nodes here -- the real IdentifierExpression
// and MemberReferenceExpression nodes stand in for that role below).
class StubNode : public PatternMatching::INode {
public:
    bool DoMatch(PatternMatching::INode* /*other*/, PatternMatching::Match /*match*/) override {
        return false;
    }
    bool DoMatchCollection(const std::vector<PatternMatching::INode*>& other, int pos,
                           PatternMatching::Match match,
                           PatternMatching::BacktrackingInfo& /*bi*/) override {
        return DoMatch(pos < static_cast<int>(other.size())
                           ? other[static_cast<std::size_t>(pos)]
                           : nullptr,
                       match);
    }
};

} // namespace

// ---- AnyNode ---------------------------------------------------------------------------

TEST(PatternNodesTest, AnyNodeMatchesNonNullAndCaptures)
{
    AnyNode any("collection");
    IdentifierExpression candidate(std::string("c"));
    PatternMatching::Match m = MatchNode(any, &candidate);
    ASSERT_TRUE(m.Success());
    EXPECT_TRUE(m.Has("collection"));
    ASSERT_EQ(m.Get<IdentifierExpression>("collection").size(), 1u);
    EXPECT_EQ(m.Get<IdentifierExpression>("collection")[0], &candidate);
}

TEST(PatternNodesTest, AnyNodeDoesNotMatchNull)
{
    AnyNode any;
    EXPECT_FALSE(IsMatchPattern(any, nullptr));
}

TEST(PatternNodesTest, AnyNodeOrNullMatchesNullAndRecordsNullCapture)
{
    AnyNodeOrNull anyOrNull("group");
    PatternMatching::Match m = MatchNode(anyOrNull, nullptr);
    ASSERT_TRUE(m.Success());
    EXPECT_TRUE(m.Has("group"));
    // The null capture: Get records the entry with a null value.
    ASSERT_EQ(m.Get("group").size(), 1u);
    EXPECT_EQ(m.Get("group")[0], nullptr);
}

// ---- NamedNode ---------------------------------------------------------------------------

TEST(PatternNodesTest, NamedNodeCapturesAndDelegates)
{
    AnyNode child;
    NamedNode named("enumerator", child);
    IdentifierExpression candidate(std::string("e"));
    PatternMatching::Match m = MatchNode(named, &candidate);
    ASSERT_TRUE(m.Success());
    ASSERT_EQ(m.Get<IdentifierExpression>("enumerator").size(), 1u);
}

TEST(PatternNodesTest, NamedNodeFailsWhenChildFails)
{
    // A NamedNode over a Backreference to a group that was never captured
    // fails (the child's DoMatch decides).
    Backreference missing("missing");
    NamedNode named("x", missing);
    StubNode candidate;
    EXPECT_FALSE(IsMatchPattern(named, &candidate));
}

// ---- Choice --------------------------------------------------------------------------------

TEST(PatternNodesTest, ChoiceMatchesFirstAlternative)
{
    // The named Add wraps the group-less AnyNode (the C# named overload wraps a
    // NamedNode); the first alternative matches and captures once.
    AnyNode any;
    Choice choice;
    choice.Add("collection", any);
    StubNode loser;
    choice.Add(loser);
    IdentifierExpression candidate(std::string("c"));
    PatternMatching::Match m = MatchNode(choice, &candidate);
    ASSERT_TRUE(m.Success());
    ASSERT_EQ(m.Get<IdentifierExpression>("collection").size(), 1u);
    EXPECT_EQ(m.Get<IdentifierExpression>("collection")[0], &candidate);
}

TEST(PatternNodesTest, ChoiceRestoresCheckpointBetweenAlternatives)
{
    // The first alternative captures under a group then fails on a backreference
    // guard; the second alternative succeeds -- the failed alternative's capture
    // must be rolled back (the C# `match.RestoreCheckPoint`).
    IdentifierExpression ident(std::string("c"));
    AnyNode any("maybe");
    Backreference guard("maybe");
    // Alternative 1: capture under "maybe" then require it to match a StubNode
    // (impossible -- a capture vs a StubNode fails on the type check).
    NamedNode first("maybe", any);
    Choice choice;
    // A failed alternative: NamedNode("maybe", ...) captures, then the
    // Backreference("self") re-match against a non-AstNode stub fails.
    StubNode stub;
    Backreference nothing("nothing");
    choice.Add(first);
    choice.Add(any);
    choice.Add(nothing);
    PatternMatching::Match m = MatchNode(choice, &ident);
    // The AnyNode alternative succeeds.
    ASSERT_TRUE(m.Success());
    EXPECT_TRUE(m.Has("maybe"));
}

// ---- OptionalNode --------------------------------------------------------------------------

TEST(PatternNodesTest, OptionalNodeMatchesNullCandidate)
{
    AnyNode any;
    OptionalNode optional(any);
    EXPECT_TRUE(IsMatchPattern(optional, nullptr));
}

TEST(PatternNodesTest, OptionalNodeMatchesNonNullViaChild)
{
    AnyNode any;
    OptionalNode optional(any);
    IdentifierExpression candidate(std::string("x"));
    EXPECT_TRUE(IsMatchPattern(optional, &candidate));
}

TEST(PatternNodesTest, OptionalNodeOverCollectionMatchesAbsentElement)
{
    // A pattern of [OptionalNode, AnyNode] against a single-element candidate
    // list: the optional treats the element as absent and the AnyNode consumes
    // it (the backtracking path through DoMatchCollection).
    AnyNode any("second");
    OptionalNode optional(any);
    Repeat tail(any);
    std::vector<PatternMatching::INode*> pattern = {&optional, &tail};
    StubNode x;
    std::vector<PatternMatching::INode*> other = {&x, &x};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_TRUE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

// ---- Repeat ----------------------------------------------------------------------------------

TEST(PatternNodesTest, RepeatMatchesExactCountOverCollection)
{
    AnyNode any;
    Repeat repeat(any);
    repeat.MinCount(2);
    repeat.MaxCount(2);
    std::vector<PatternMatching::INode*> pattern = {&repeat};
    StubNode x;
    std::vector<PatternMatching::INode*> other = {&x, &x};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_TRUE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

TEST(PatternNodesTest, RepeatRejectsTooFewCandidates)
{
    AnyNode any;
    Repeat repeat(any);
    repeat.MinCount(2);
    std::vector<PatternMatching::INode*> pattern = {&repeat};
    StubNode x;
    std::vector<PatternMatching::INode*> other = {&x};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_FALSE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

// ---- Backreference ------------------------------------------------------------------------------

TEST(PatternNodesTest, BackreferenceReMatchesEarlierCapture)
{
    // Capture an IdentifierExpression under "enumerator", then require the next
    // pattern node to re-match the SAME capture: a second IdentifierExpression
    // with the same identifier matches it.
    IdentifierExpression first(std::string("e"));
    IdentifierExpression second(std::string("e"));
    NamedNode capture("enumerator", first);
    Backreference back("enumerator");
    std::vector<PatternMatching::INode*> pattern = {&capture, &back};
    std::vector<PatternMatching::INode*> other = {&first, &second};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_TRUE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

TEST(PatternNodesTest, BackreferenceFailsOnDifferentCapture)
{
    IdentifierExpression first(std::string("e1"));
    IdentifierExpression second(std::string("e2"));
    NamedNode capture("enumerator", first);
    Backreference back("enumerator");
    std::vector<PatternMatching::INode*> pattern = {&capture, &back};
    std::vector<PatternMatching::INode*> other = {&first, &second};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_FALSE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

// ---- IdentifierExpressionBackreference ------------------------------------------------------------

TEST(PatternNodesTest, IdentifierExpressionBackreferenceMatchesSameIdentifier)
{
    // The capture and the re-match run within ONE match (the C# usage): the
    // pattern [NamedNode("enumerator", AnyNode), IdentifierExpressionBackreference]
    // matches two IdentifierExpression candidates with the same identifier.
    AnyNode any("enumerator");
    NamedNode capture("enumerator", any);
    IdentifierExpressionBackreference back("enumerator");
    std::vector<PatternMatching::INode*> pattern = {&capture, &back};
    IdentifierExpression first(std::string("list"));
    IdentifierExpression second(std::string("list"));
    std::vector<PatternMatching::INode*> other = {&first, &second};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_TRUE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

TEST(PatternNodesTest, IdentifierExpressionBackreferenceRejectsDifferentIdentifier)
{
    AnyNode any("enumerator");
    NamedNode capture("enumerator", any);
    IdentifierExpressionBackreference back("enumerator");
    std::vector<PatternMatching::INode*> pattern = {&capture, &back};
    IdentifierExpression first(std::string("list"));
    IdentifierExpression second(std::string("other"));
    std::vector<PatternMatching::INode*> other = {&first, &second};
    PatternMatching::Match m = PatternMatching::Match::CreateNew();
    EXPECT_FALSE(PatternMatching::Pattern::DoMatchCollection(pattern, other, m));
}

TEST(PatternNodesTest, IdentifierExpressionBackreferenceRejectsNonIdentifier)
{
    IdentifierExpressionBackreference back("enumerator");
    // A candidate that is not an IdentifierExpression fails (the C# `as` + null
    // check).
    StubNode candidate;
    EXPECT_FALSE(IsMatchPattern(back, &candidate));
}

// ---- The real getEnumerator shape ------------------------------------------------------------------

// The C# `getEnumeratorPattern`: an InvocationExpression whose target is a
// Choice over `collection.GetEnumerator` / `collection.GetAsyncEnumerator`
// MemberReferenceExpressions. Matching a real MemberReferenceExpression
// candidate exercises the full pattern stack end to end.
TEST(PatternNodesTest, GetEnumeratorPatternShapeMatchesMemberReference)
{
    // The C# `new MemberReferenceExpression(new AnyNode("collection").ToExpression(),
    // "GetEnumerator")`: the pattern's target is the placeholder-wrapped AnyNode
    // (the ToExpression free function standing in for the C# implicit conversion),
    // so the pattern matches ANY target while capturing it.
    AnyNode anyCollection("collection");
    Expression* patternTarget = Expression::ToExpression(anyCollection);
    MemberReferenceExpression patternMre(patternTarget, "GetEnumerator");
    MemberReferenceExpression candidate(new IdentifierExpression(std::string("xs")),
                                        "GetEnumerator");
    PatternMatching::Match m = MatchNode(patternMre, &candidate);
    ASSERT_TRUE(m.Success());
    // The "collection" group captures the candidate's Target child.
    ASSERT_EQ(m.Get<IdentifierExpression>("collection").size(), 1u);
    EXPECT_EQ(m.Get<IdentifierExpression>("collection")[0]->Identifier(), "xs");
}

TEST(PatternNodesTest, GetEnumeratorPatternShapeRejectsWrongMemberName)
{
    IdentifierExpression collection(std::string("list"));
    MemberReferenceExpression patternMre(&collection, "GetEnumerator");
    MemberReferenceExpression candidate(new IdentifierExpression(std::string("xs")),
                                        "Dispose");
    PatternMatching::Match m = MatchNode(patternMre, &candidate);
    EXPECT_FALSE(m.Success());
}