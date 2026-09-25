// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the concrete pattern nodes (PatternNodes.hpp) and the PatternExtensions
// `Match`/`IsMatch` entry points. The matching engine itself (Match/BacktrackingInfo/
// Pattern.DoMatchCollection) is covered by PatternMatching_Test.cpp using local stubs;
// this suite drives the real nodes against real AST nodes.

#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
namespace PM = ILSpy::Decompiler::CSharp::Syntax::PatternMatching;

// ---- AnyNode ---------------------------------------------------------------

TEST(CSharp_PatternMatching_AnyNode, MatchesNonNullAndCaptures) {
    IdentifierExpression id("x");
    PM::AnyNode any("g");
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(any.DoMatch(&id, m));
    ASSERT_TRUE(m.Has("g"));
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], &id);
}

TEST(CSharp_PatternMatching_AnyNode, DoesNotMatchNull) {
    PM::AnyNode any("g");
    PM::Match m = PM::Match::CreateNew();
    EXPECT_FALSE(any.DoMatch(nullptr, m));
    // `Match.Add` skips a null node, so no capture is recorded.
    EXPECT_FALSE(m.Has("g"));
}

TEST(CSharp_PatternMatching_AnyNode, NullGroupDoesNotCapture) {
    IdentifierExpression id("x");
    PM::AnyNode any;
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(any.DoMatch(&id, m));
    EXPECT_TRUE(m.Get("g").empty());
}

// ---- AnyNodeOrNull ---------------------------------------------------------

TEST(CSharp_PatternMatching_AnyNodeOrNull, MatchesNullWithNullCapture) {
    PM::AnyNodeOrNull any("g");
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(any.DoMatch(nullptr, m));
    ASSERT_TRUE(m.Has("g"));
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], nullptr);
}

TEST(CSharp_PatternMatching_AnyNodeOrNull, MatchesNonNullWithCapture) {
    IdentifierExpression id("x");
    PM::AnyNodeOrNull any("g");
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(any.DoMatch(&id, m));
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], &id);
}

// ---- NamedNode -------------------------------------------------------------

TEST(CSharp_PatternMatching_NamedNode, CapturesAndDelegatesToChild) {
    IdentifierExpression id("x");
    PM::AnyNode any;
    PM::NamedNode named("g", &any);
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(named.DoMatch(&id, m));
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], &id);
}

TEST(CSharp_PatternMatching_NamedNode, FailsWhenChildRejects) {
    // AnyNode rejects null, so the NamedNode must too.
    PM::AnyNode any;
    PM::NamedNode named("g", &any);
    PM::Match m = PM::Match::CreateNew();
    EXPECT_FALSE(named.DoMatch(nullptr, m));
}

TEST(CSharp_PatternMatching_NamedNode, NullChildThrows) {
    EXPECT_THROW(PM::NamedNode("g", nullptr), std::invalid_argument);
}

// ---- OptionalNode ----------------------------------------------------------

TEST(CSharp_PatternMatching_OptionalNode, MatchesNullWithoutDelegating) {
    PM::AnyNode any;
    PM::OptionalNode opt(&any);
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(opt.DoMatch(nullptr, m));
}

TEST(CSharp_PatternMatching_OptionalNode, DelegatesForNonNull) {
    IdentifierExpression id("x");
    PM::AnyNode any;
    PM::OptionalNode opt(&any);
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(opt.DoMatch(&id, m));
}

TEST(CSharp_PatternMatching_OptionalNode, GroupNameCtorCapturesViaNamedNode) {
    IdentifierExpression id("x");
    PM::AnyNode any;
    PM::OptionalNode opt("g", &any);
    PM::Match m = PM::Match::CreateNew();
    EXPECT_TRUE(opt.DoMatch(&id, m));
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], &id);
}

// ---- Repeat ----------------------------------------------------------------

TEST(CSharp_PatternMatching_Repeat, DoMatchNullHonoursMinCount) {
    PM::AnyNode any;
    PM::Repeat repeat(&any); // MinCount 0 default
    EXPECT_TRUE(repeat.DoMatch(nullptr, PM::Match::CreateNew()));

    PM::Repeat required(&any);
    required.MinCount = 1;
    EXPECT_FALSE(required.DoMatch(nullptr, PM::Match::CreateNew()));
}

TEST(CSharp_PatternMatching_Repeat, DoMatchNonNullHonoursMaxCount) {
    IdentifierExpression id("x");
    PM::AnyNode any;
    PM::Repeat repeat(&any);
    EXPECT_TRUE(repeat.DoMatch(&id, PM::Match::CreateNew()));

    PM::Repeat none(&any);
    none.MaxCount = 0;
    EXPECT_FALSE(none.DoMatch(&id, PM::Match::CreateNew()));
}

TEST(CSharp_PatternMatching_Repeat, DoMatchCollectionGreedilyConsumes) {
    // [Repeat(any), any] over [id1, id2]: the repeat consumes both, then the
    // trailing any would have to match an absent candidate; the backtracking
    // stack finds the alternative where the repeat consumes one and the trailing
    // any matches the second.
    IdentifierExpression id1("a"), id2("b");
    PM::AnyNode repeated, tail;
    PM::Repeat repeat(&repeated);
    std::vector<PM::INode*> pattern = { &repeat, &tail };
    std::vector<PM::INode*> other = { &id1, &id2 };
    EXPECT_TRUE(PM::Pattern::DoMatchCollection(pattern, other, PM::Match::CreateNew()));
}

TEST(CSharp_PatternMatching_Repeat, DoMatchCollectionHonoursMinCount) {
    IdentifierExpression id1("a");
    PM::AnyNode repeated;
    PM::Repeat repeat(&repeated);
    repeat.MinCount = 2;
    repeat.MaxCount = 2;
    std::vector<PM::INode*> pattern = { &repeat };
    std::vector<PM::INode*> one = { &id1 };
    std::vector<PM::INode*> two = { &id1, &id1 };
    EXPECT_FALSE(PM::Pattern::DoMatchCollection(pattern, one, PM::Match::CreateNew()));
    EXPECT_TRUE(PM::Pattern::DoMatchCollection(pattern, two, PM::Match::CreateNew()));
}

// ---- Backreference ---------------------------------------------------------

TEST(CSharp_PatternMatching_Backreference, AbsentGroupMatchesOnlyNull) {
    IdentifierExpression id("x");
    PM::Backreference back("g");
    EXPECT_TRUE(back.DoMatch(nullptr, PM::Match::CreateNew()));
    EXPECT_FALSE(back.DoMatch(&id, PM::Match::CreateNew()));
}

TEST(CSharp_PatternMatching_Backreference, MatchesLastCapturedNode) {
    IdentifierExpression id("x"), sameName("x"), otherName("y");
    PM::AnyNode capture("g");
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(capture.DoMatch(&id, m));
    PM::Backreference back("g");
    // The captured node is structurally matched against the candidate.
    EXPECT_TRUE(back.DoMatch(&sameName, m));
    EXPECT_FALSE(back.DoMatch(&otherName, m));
}

TEST(CSharp_PatternMatching_Backreference, UsesLastCapture) {
    IdentifierExpression first("x"), last("y"), candidateX("x"), candidateY("y");
    PM::AnyNode capture("g");
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(capture.DoMatch(&first, m));
    ASSERT_TRUE(capture.DoMatch(&last, m));
    PM::Backreference back("g");
    EXPECT_FALSE(back.DoMatch(&candidateX, m));
    EXPECT_TRUE(back.DoMatch(&candidateY, m));
}

// ---- IdentifierExpressionBackreference -------------------------------------

TEST(CSharp_PatternMatching_IdentifierExpressionBackreference, MatchesSameIdentifier) {
    IdentifierExpression captured("index"), sameName("index"), otherName("other");
    PM::AnyNode capture("indexVariable");
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(capture.DoMatch(&captured, m));
    PM::IdentifierExpressionBackreference back("indexVariable");
    EXPECT_TRUE(back.DoMatch(&sameName, m));
    EXPECT_FALSE(back.DoMatch(&otherName, m));
}

TEST(CSharp_PatternMatching_IdentifierExpressionBackreference, RejectsCandidateWithTypeArguments) {
    IdentifierExpression captured("index");
    IdentifierExpression typed("index");
    SimpleType typeArgument("T");
    typed.TypeArguments().Add(&typeArgument);
    PM::AnyNode capture("indexVariable");
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(capture.DoMatch(&captured, m));
    PM::IdentifierExpressionBackreference back("indexVariable");
    EXPECT_FALSE(back.DoMatch(&typed, m));
}

TEST(CSharp_PatternMatching_IdentifierExpressionBackreference, RejectsAbsentCapture) {
    IdentifierExpression candidate("index");
    PM::IdentifierExpressionBackreference back("indexVariable");
    EXPECT_FALSE(back.DoMatch(&candidate, PM::Match::CreateNew()));
}

TEST(CSharp_PatternMatching_IdentifierExpressionBackreference, RejectsNonIdentifierCandidate) {
    SimpleType type("T");
    PM::IdentifierExpressionBackreference back("indexVariable");
    PM::Match m = PM::Match::CreateNew();
    m.Add("indexVariable", &type);
    // A captured non-IdentifierExpression has no Identifier child; the candidate is
    // not an IdentifierExpression either, so the match fails.
    EXPECT_FALSE(back.DoMatch(&type, m));
}

// ---- Choice ----------------------------------------------------------------

TEST(CSharp_PatternMatching_Choice, FirstMatchingAlternativeWins) {
    IdentifierExpression patternA("a"), candidateA("a");
    PM::AnyNode any;
    PM::Choice choice;
    choice.Add("a", &patternA);
    choice.Add("b", &any);
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(choice.DoMatch(&candidateA, m));
    ASSERT_TRUE(m.Has("a"));
    EXPECT_FALSE(m.Has("b"));
}

TEST(CSharp_PatternMatching_Choice, FallsThroughAndRestoresCheckpoint) {
    IdentifierExpression patternA("a"), candidateB("b");
    PM::AnyNode any;
    PM::Choice choice;
    choice.Add("a", &patternA);
    choice.Add("b", &any);
    PM::Match m = PM::Match::CreateNew();
    ASSERT_TRUE(choice.DoMatch(&candidateB, m));
    // The failed first alternative captured "a"; the checkpoint restore dropped it.
    EXPECT_FALSE(m.Has("a"));
    ASSERT_TRUE(m.Has("b"));
    EXPECT_EQ(m.Get("b")[0], &candidateB);
}

TEST(CSharp_PatternMatching_Choice, AllAlternativesFail) {
    IdentifierExpression patternA("a"), patternB("b"), candidateC("c");
    PM::Choice choice;
    choice.Add(&patternA);
    choice.Add(&patternB);
    EXPECT_FALSE(choice.DoMatch(&candidateC, PM::Match::CreateNew()));
}

// ---- PatternExtensions -----------------------------------------------------

TEST(CSharp_PatternMatching_PatternExtensions, MatchReturnsPopulatedMatchOnSuccess) {
    IdentifierExpression pattern("x"), candidate("x");
    PM::Match m = PM::PatternExtensions::Match(pattern, &candidate);
    EXPECT_TRUE(m.Success());
}

TEST(CSharp_PatternMatching_PatternExtensions, MatchReturnsFailureSentinelOnReject) {
    IdentifierExpression pattern("x"), candidate("y");
    PM::Match m = PM::PatternExtensions::Match(pattern, &candidate);
    EXPECT_FALSE(m.Success());
}

TEST(CSharp_PatternMatching_PatternExtensions, IsMatchMirrorsDoMatch) {
    IdentifierExpression pattern("x"), sameName("x"), otherName("y");
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(pattern, &sameName));
    EXPECT_FALSE(PM::PatternExtensions::IsMatch(pattern, &otherName));
}

TEST(CSharp_PatternMatching_PatternExtensions, StructuralAstNodeMatch) {
    // Two IdentifierExpressions with the same name are structurally equal.
    IdentifierExpression pattern("x"), candidate("x");
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(pattern, &candidate));
}

TEST(CSharp_PatternMatching_PatternExtensions, CaptureThroughRealPatternTree) {
    // A NamedNode wrapping a real AstNode pattern captures the candidate.
    IdentifierExpression pattern("x"), candidate("x");
    PM::NamedNode named("g", &pattern);
    PM::Match m = PM::PatternExtensions::Match(named, &candidate);
    ASSERT_TRUE(m.Success());
    ASSERT_EQ(m.Get("g").size(), 1u);
    EXPECT_EQ(m.Get("g")[0], &candidate);
}
