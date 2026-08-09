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

#include "Decompiler/Util/Interval.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using LongInterval = ILSpy::Decompiler::Util::LongInterval;
using LongSet = ILSpy::Decompiler::Util::LongSet;

namespace {
const long long kMin = std::numeric_limits<long long>::min();
const long long kMax = std::numeric_limits<long long>::max();
}

// ---- LongInterval --------------------------------------------------------

TEST(Util_LongInterval, DefaultIsEmpty) {
    LongInterval i;
    EXPECT_TRUE(i.IsEmpty());
    EXPECT_EQ(i.Start, 0);
    EXPECT_EQ(i.End, 0);
    EXPECT_EQ(i.InclusiveEnd(), -1);
}

TEST(Util_LongInterval, HalfClosedInterval) {
    LongInterval i(3, 7);  // [3..7) = {3,4,5,6}
    EXPECT_FALSE(i.IsEmpty());
    EXPECT_EQ(i.Start, 3);
    EXPECT_EQ(i.End, 7);
    EXPECT_EQ(i.InclusiveEnd(), 6);
    EXPECT_TRUE(i.Contains(3));
    EXPECT_TRUE(i.Contains(6));
    EXPECT_FALSE(i.Contains(2));
    EXPECT_FALSE(i.Contains(7));
}

TEST(Util_LongInterval, InclusiveCtor) {
    LongInterval i = LongInterval::Inclusive(3, 6);  // {3,4,5,6}
    EXPECT_EQ(i.Start, 3);
    EXPECT_EQ(i.End, 7);
    EXPECT_EQ(i.InclusiveEnd(), 6);
    EXPECT_TRUE(i.Contains(3));
    EXPECT_TRUE(i.Contains(6));
    EXPECT_FALSE(i.Contains(7));
}

TEST(Util_LongInterval, FullRangeSpecialCase) {
    // Start == End == Min is the full [Min..Max] interval, NOT empty.
    LongInterval full(kMin, kMin);
    EXPECT_FALSE(full.IsEmpty());
    EXPECT_EQ(full.InclusiveEnd(), kMax);
    EXPECT_TRUE(full.Contains(kMin));
    EXPECT_TRUE(full.Contains(kMax));
    EXPECT_TRUE(full.Contains(0));
}

TEST(Util_LongInterval, Intersect) {
    LongInterval a = LongInterval::Inclusive(0, 10);   // {0..10}
    LongInterval b = LongInterval::Inclusive(5, 15);   // {5..15}
    LongInterval c = a.Intersect(b);
    EXPECT_EQ(c.Start, 5);
    EXPECT_EQ(c.InclusiveEnd(), 10);
    LongInterval d = LongInterval::Inclusive(20, 30).Intersect(LongInterval::Inclusive(0, 10));
    EXPECT_TRUE(d.IsEmpty());
}

TEST(Util_LongInterval, Range) {
    auto r = LongInterval::Inclusive(3, 5).Range();
    std::vector<long long> expected = {3, 4, 5};
    EXPECT_EQ(r, expected);
}

TEST(Util_LongInterval, Equality) {
    EXPECT_EQ(LongInterval(3, 7), LongInterval(3, 7));
    EXPECT_NE(LongInterval(3, 7), LongInterval(3, 8));
    EXPECT_NE(LongInterval(3, 7), LongInterval(4, 7));
}

// ---- LongSet: construction ------------------------------------------------

TEST(Util_LongSet, EmptyAndSingleValue) {
    LongSet e;
    EXPECT_TRUE(e.IsEmpty());
    EXPECT_EQ(e.Count(), 0u);
    EXPECT_EQ(e.Intervals().size(), 0u);

    LongSet s(5);
    EXPECT_FALSE(s.IsEmpty());
    EXPECT_EQ(s.Count(), 1u);
    ASSERT_EQ(s.Intervals().size(), 1u);
    EXPECT_EQ(s.Intervals()[0], LongInterval::Inclusive(5, 5));
    EXPECT_TRUE(s.Contains(5));
    EXPECT_FALSE(s.Contains(4));
    EXPECT_FALSE(s.Contains(6));
}

TEST(Util_LongSet, UniverseContainsAll) {
    LongSet u = LongSet::Universe();
    EXPECT_FALSE(u.IsEmpty());
    EXPECT_EQ(u.Count(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(u.Contains(kMin));
    EXPECT_TRUE(u.Contains(kMax));
    EXPECT_TRUE(u.Contains(0));
    EXPECT_TRUE(u.Contains(42));
    ASSERT_EQ(u.Intervals().size(), 1u);
    EXPECT_EQ(u.Intervals()[0], LongInterval(kMin, kMin));
}

TEST(Util_LongSet, ConstructorMergesOverlappingAndTouching) {
    // Overlapping [0..3) and [2..5) -> [0..5); touching [5..7) merges too.
    std::vector<LongInterval> ivs = {
        LongInterval(0, 3),    // {0,1,2}
        LongInterval(2, 5),    // {2,3,4}
        LongInterval(5, 7),    // {5,6}  (touches [0..5) at 5)
    };
    LongSet s(ivs);
    ASSERT_EQ(s.Intervals().size(), 1u);
    EXPECT_EQ(s.Intervals()[0], LongInterval(0, 7));  // {0..6}
    EXPECT_EQ(s.Count(), 7u);
}

TEST(Util_LongSet, ConstructorFiltersEmptyAndSorts) {
    std::vector<LongInterval> ivs = {
        LongInterval(),               // empty -> dropped
        LongInterval(10, 12),         // {10,11}
        LongInterval(0, 3),           // {0,1,2}
        LongInterval::Inclusive(3, 3),// {3}
    };
    LongSet s(ivs);
    // {0,1,2} and {3} touch (3 == 3) -> merge to {0..3}; {10,11} separate.
    ASSERT_EQ(s.Intervals().size(), 2u);
    EXPECT_EQ(s.Intervals()[0], LongInterval(0, 4));   // {0,1,2,3}
    EXPECT_EQ(s.Intervals()[1], LongInterval(10, 12));
}

// ---- LongSet: set operations ---------------------------------------------

TEST(Util_LongSet, UnionWith) {
    LongSet a(3);
    LongSet b(5);
    LongSet u = a.UnionWith(b);
    EXPECT_EQ(u.Count(), 2u);
    EXPECT_TRUE(u.Contains(3));
    EXPECT_TRUE(u.Contains(5));
    EXPECT_FALSE(u.Contains(4));
    // Union with self is idempotent.
    EXPECT_TRUE(u.UnionWith(a).SetEquals(u));
}

TEST(Util_LongSet, IntersectWith) {
    LongSet a(std::vector<LongInterval>{LongInterval(0, 5), LongInterval(10, 15)});  // {0..4, 10..14}
    LongSet b(std::vector<LongInterval>{LongInterval(3, 12)});                       // {3..11}
    LongSet i = a.IntersectWith(b);
    ASSERT_EQ(i.Intervals().size(), 2u);
    EXPECT_EQ(i.Intervals()[0], LongInterval(3, 5));    // {3,4}
    EXPECT_EQ(i.Intervals()[1], LongInterval(10, 12)); // {10,11}
}

TEST(Util_LongSet, Overlaps) {
    LongSet a(std::vector<LongInterval>{LongInterval(0, 5)});
    LongSet b(std::vector<LongInterval>{LongInterval(3, 8)});
    LongSet c(std::vector<LongInterval>{LongInterval(10, 15)});
    EXPECT_TRUE(a.Overlaps(b));
    EXPECT_FALSE(a.Overlaps(c));
    EXPECT_FALSE(a.Overlaps(LongSet()));
}

TEST(Util_LongSet, ExceptWith) {
    LongSet a(std::vector<LongInterval>{LongInterval(0, 10)});  // {0..9}
    LongSet b(3);
    LongSet r = a.ExceptWith(b);
    EXPECT_EQ(r.Count(), 9u);
    EXPECT_FALSE(r.Contains(3));
    EXPECT_TRUE(r.Contains(2));
    EXPECT_TRUE(r.Contains(4));
}

TEST(Util_LongSet, InvertOfFiniteIsInfinite) {
    // The defining property the switch family needs: the complement of a
    // finite set is infinite and cannot be held in std::set<int64_t>.
    LongSet s(5);
    LongSet inv = s.Invert();
    EXPECT_FALSE(inv.IsEmpty());
    EXPECT_EQ(inv.Count(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(inv.Contains(5));
    EXPECT_TRUE(inv.Contains(4));
    EXPECT_TRUE(inv.Contains(6));
    EXPECT_TRUE(inv.Contains(kMin));
    EXPECT_TRUE(inv.Contains(kMax));
    // Invert is self-inverse up to the original: invert(invert(s)) == s.
    EXPECT_TRUE(inv.Invert().SetEquals(s));
}

TEST(Util_LongSet, InvertOfEmptyIsUniverse) {
    LongSet e;
    EXPECT_TRUE(e.Invert().SetEquals(LongSet::Universe()));
}

TEST(Util_LongSet, InvertOfUniverseIsEmpty) {
    EXPECT_TRUE(LongSet::Universe().Invert().IsEmpty());
}

TEST(Util_LongSet, AddOffset) {
    LongSet s(5);
    LongSet shifted = s.AddOffset(10);
    EXPECT_TRUE(shifted.Contains(15));
    EXPECT_FALSE(shifted.Contains(5));
    EXPECT_EQ(shifted.Count(), 1u);
    // AddOffset(0) is identity.
    EXPECT_TRUE(s.AddOffset(0).SetEquals(s));
}

TEST(Util_LongSet, AddOffsetWrapsAround) {
    // An interval near Max shifted past Max wraps and splits into two.
    LongSet s(std::vector<LongInterval>{LongInterval::Inclusive(kMax - 1, kMax)});  // {Max-1, Max}
    LongSet shifted = s.AddOffset(2);
    // {Max+1, Max+2} wraps to {Min, Min+1}.
    EXPECT_EQ(shifted.Count(), 2u);
    EXPECT_TRUE(shifted.Contains(kMin));
    EXPECT_TRUE(shifted.Contains(kMin + 1));
    EXPECT_FALSE(shifted.Contains(kMax - 1));
}

// ---- LongSet: subset / superset ------------------------------------------

TEST(Util_LongSet, SubsetSuperset) {
    LongSet big(std::vector<LongInterval>{LongInterval(0, 10)});    // {0..9}
    LongSet small(std::vector<LongInterval>{LongInterval(2, 5)});  // {2,3,4}
    EXPECT_TRUE(small.IsSubsetOf(big));
    EXPECT_TRUE(big.IsSupersetOf(small));
    EXPECT_TRUE(small.IsProperSubsetOf(big));
    EXPECT_TRUE(big.IsProperSupersetOf(small));
    EXPECT_FALSE(big.IsSubsetOf(small));
    EXPECT_FALSE(small.SetEquals(big));
    // A set is a (non-proper) subset of itself.
    EXPECT_TRUE(big.IsSubsetOf(big));
    EXPECT_FALSE(big.IsProperSubsetOf(big));
}

// ---- LongSet: containment / values ----------------------------------------

TEST(Util_LongSet, ContainingInterval) {
    LongSet s(std::vector<LongInterval>{LongInterval(0, 3), LongInterval(10, 15)});
    LongInterval c = s.ContainingInterval();
    EXPECT_EQ(c.Start, 0);
    EXPECT_EQ(c.End, 15);
    EXPECT_TRUE(s.ContainingInterval().IsEmpty() == false);
    EXPECT_TRUE(LongSet().ContainingInterval().IsEmpty());
}

TEST(Util_LongSet, Values) {
    LongSet s(std::vector<LongInterval>{LongInterval(0, 3), LongInterval(10, 12)});  // {0,1,2,10,11}
    std::vector<long long> v = s.Values();
    std::vector<long long> expected = {0, 1, 2, 10, 11};
    EXPECT_EQ(v, expected);
}

TEST(Util_LongSet, SetEquals) {
    LongSet a(5);
    LongSet b(5);
    EXPECT_TRUE(a.SetEquals(b));
    EXPECT_FALSE(a.SetEquals(LongSet(6)));
    // Different interval decompositions of the same value set are equal.
    LongSet c(std::vector<LongInterval>{LongInterval(0, 5)});                 // {0..4}
    LongSet d(std::vector<LongInterval>{LongInterval(0, 2), LongInterval(2, 5)}); // {0,1} + {2,3,4} touch -> {0..4}
    EXPECT_TRUE(c.SetEquals(d));
}

TEST(Util_LongSet, ToStringRoundTrip) {
    LongSet s(std::vector<LongInterval>{LongInterval(0, 3), LongInterval(10, 12)});
    EXPECT_EQ(s.ToString(), "[0..3),[10..12)");
    EXPECT_EQ(LongSet().ToString(), "");
}

// ---- Switch-family contract: AnalyzeCondition value sets ------------------
// These mirror SwitchAnalysis.MakeSetWhereComparisonIsTrue (signed), locking
// in the exact LongSet shapes the switch-detection transforms depend on.

TEST(Util_LongSet_SwitchContract, EqualityIsSingleton) {
    LongSet s(5);
    EXPECT_EQ(s.Count(), 1u);
    EXPECT_TRUE(s.Contains(5));
}

TEST(Util_LongSet_SwitchContract, InequalityIsComplement) {
    LongSet s = LongSet(5).Invert();
    EXPECT_FALSE(s.Contains(5));
    EXPECT_TRUE(s.Contains(4));
    EXPECT_TRUE(s.Contains(6));
}

TEST(Util_LongSet_SwitchContract, LessThanSigned) {
    // x < 5 (signed) == {Min..4} == complement of {5..Max}
    LongSet ge5 = LongSet(std::vector<LongInterval>{LongInterval::Inclusive(5, kMax)});
    LongSet lt5 = ge5.Invert();
    EXPECT_FALSE(lt5.Contains(5));
    EXPECT_FALSE(lt5.Contains(6));
    EXPECT_TRUE(lt5.Contains(4));
    EXPECT_TRUE(lt5.Contains(kMin));
}

TEST(Util_LongSet_SwitchContract, LessThanOrEqualSigned) {
    // x <= 5 (signed) == {Min..5}
    LongSet le5 = LongSet(std::vector<LongInterval>{LongInterval::Inclusive(kMin, 5)});
    EXPECT_TRUE(le5.Contains(5));
    EXPECT_TRUE(le5.Contains(kMin));
    EXPECT_FALSE(le5.Contains(6));
}

TEST(Util_LongSet_SwitchContract, GreaterThanSigned) {
    // x > 5 (signed) == {6..Max} == complement of {Min..5}
    LongSet le5 = LongSet(std::vector<LongInterval>{LongInterval::Inclusive(kMin, 5)});
    LongSet gt5 = le5.Invert();
    EXPECT_FALSE(gt5.Contains(5));
    EXPECT_TRUE(gt5.Contains(6));
    EXPECT_TRUE(gt5.Contains(kMax));
}

TEST(Util_LongSet_SwitchContract, GreaterThanOrEqualSigned) {
    // x >= 5 (signed) == {5..Max}
    LongSet ge5 = LongSet(std::vector<LongInterval>{LongInterval::Inclusive(5, kMax)});
    EXPECT_TRUE(ge5.Contains(5));
    EXPECT_TRUE(ge5.Contains(kMax));
    EXPECT_FALSE(ge5.Contains(4));
}

TEST(Util_LongSet_SwitchContract, AddOffsetMovesLabels) {
    // SwitchAnalysis.AnalyzeSwitch handles `switch(V - sub)` by offsetting labels.
    // A section labelled {0,1,2} over switch(V-1) is really {1,2,3} over switch(V).
    LongSet labels(std::vector<LongInterval>{LongInterval(0, 3)});  // {0,1,2}
    LongSet moved = labels.AddOffset(1);
    EXPECT_TRUE(moved.Contains(1));
    EXPECT_TRUE(moved.Contains(2));
    EXPECT_TRUE(moved.Contains(3));
    EXPECT_FALSE(moved.Contains(0));
    EXPECT_EQ(moved.Count(), 3u);
}
