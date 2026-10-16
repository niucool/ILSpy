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

#include "Decompiler/Util/BitSet.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using BitSet = ILSpy::Decompiler::Util::BitSet;

// ---- Construction & capacity ---------------------------------------------

TEST(Util_BitSet, CapacityRoundsUpToWholeWord) {
	// max(1, ceil(capacity / 64)) words, each 64 bits.
	EXPECT_EQ(BitSet(0).Capacity(), 64);
	EXPECT_EQ(BitSet(1).Capacity(), 64);
	EXPECT_EQ(BitSet(63).Capacity(), 64);
	EXPECT_EQ(BitSet(64).Capacity(), 64);
	EXPECT_EQ(BitSet(65).Capacity(), 128);
	EXPECT_EQ(BitSet(128).Capacity(), 128);
	EXPECT_EQ(BitSet(129).Capacity(), 192);
	EXPECT_EQ(BitSet(0).Any(), false);
	// A negative capacity degrades to the minimum single word.
	EXPECT_EQ(BitSet(-5).Capacity(), 64);
}

TEST(Util_BitSet, DefaultConstructorIsOneAllClearWord) {
	BitSet b;
	EXPECT_EQ(b.Capacity(), 64);
	EXPECT_FALSE(b.Any());
	EXPECT_FALSE(b[0]);
}

// ---- Set / Clear / indexer ------------------------------------------------

TEST(Util_BitSet, SetAndReadBitsWithinFirstWord) {
	BitSet b(64);
	EXPECT_FALSE(b[0]);
	b.Set(0);
	b.Set(5);
	b.Set(63);
	EXPECT_TRUE(b[0]);
	EXPECT_TRUE(b[5]);
	EXPECT_TRUE(b[63]);
	EXPECT_FALSE(b[1]);
	EXPECT_TRUE(b.Any());
	b.Clear(0);
	EXPECT_FALSE(b[0]);
	EXPECT_TRUE(b[5]);
}

TEST(Util_BitSet, SetAndReadBitsAcrossWordBoundary) {
	BitSet b(192);  // 3 words
	b.Set(63);
	b.Set(64);
	b.Set(127);
	b.Set(128);
	b.Set(191);
	EXPECT_TRUE(b[63]);
	EXPECT_TRUE(b[64]);
	EXPECT_TRUE(b[127]);
	EXPECT_TRUE(b[128]);
	EXPECT_TRUE(b[191]);
	EXPECT_FALSE(b[0]);
	EXPECT_FALSE(b[65]);
	b.Clear(64);
	EXPECT_FALSE(b[64]);
	EXPECT_TRUE(b[63]);
}

TEST(Util_BitSet, ClearAll) {
	BitSet b(130);
	for (int i : {0, 1, 64, 65, 128, 129})
		b.Set(i);
	EXPECT_TRUE(b.Any());
	b.ClearAll();
	EXPECT_FALSE(b.Any());
	EXPECT_FALSE(b[0]);
	EXPECT_FALSE(b[129]);
}

// ---- Set(start, end) / Clear(start, end) ----------------------------------

TEST(Util_BitSet, SetRangeWithinOneWord) {
	BitSet b(64);
	b.Set(5, 10);  // bits 5..9
	for (int i = 0; i < 64; i++)
		EXPECT_EQ(b[i], i >= 5 && i <= 9) << "bit " << i;
}

TEST(Util_BitSet, SetRangeAcrossWordBoundary) {
	BitSet b(192);
	b.Set(60, 68);  // bits 60..67, spanning words 0 and 1
	EXPECT_FALSE(b[59]);
	for (int i = 60; i <= 67; i++)
		EXPECT_TRUE(b[i]) << "bit " << i;
	EXPECT_FALSE(b[68]);
}

TEST(Util_BitSet, SetRangeWholeWords) {
	BitSet b(192);
	b.Set(64, 128);  // all of word 1
	EXPECT_FALSE(b[63]);
	for (int i = 64; i < 128; i++)
		EXPECT_TRUE(b[i]) << "bit " << i;
	EXPECT_FALSE(b[128]);
}

TEST(Util_BitSet, SetEmptyRangeIsNoOp) {
	BitSet b(64);
	b.Set(3, 3);
	EXPECT_FALSE(b.Any());
	b.Set(0, 0);  // empty range at the boundary
	EXPECT_FALSE(b.Any());
}

TEST(Util_BitSet, ClearRangeWithinOneWord) {
	BitSet b(64);
	b.Set(0, 64);
	b.Clear(5, 10);
	for (int i = 0; i < 64; i++)
		EXPECT_EQ(b[i], !(i >= 5 && i <= 9)) << "bit " << i;
}

TEST(Util_BitSet, ClearRangeAcrossWordBoundary) {
	BitSet b(192);
	b.Set(0, 192);
	b.Clear(60, 68);
	for (int i = 0; i < 192; i++)
		EXPECT_EQ(b[i], !(i >= 60 && i <= 67)) << "bit " << i;
}

// ---- Any / All ------------------------------------------------------------

TEST(Util_BitSet, AnyIsFalseForEmptyAndTrueAfterSet) {
	BitSet b(64);
	EXPECT_FALSE(b.Any());
	b.Set(10);
	EXPECT_TRUE(b.Any());
	b.Clear(10);
	EXPECT_FALSE(b.Any());
}

TEST(Util_BitSet, AllSingleWordRange) {
	BitSet b(64);
	EXPECT_FALSE(b.All(0, 64));
	b.Set(0, 64);
	EXPECT_TRUE(b.All(0, 64));
	EXPECT_TRUE(b.All(0, 64));  // full word
	b.Clear(5);
	EXPECT_FALSE(b.All(0, 64));
	EXPECT_TRUE(b.All(0, 5));   // sub-range still all-set
	EXPECT_TRUE(b.All(6, 64));
}

TEST(Util_BitSet, AllEmptyRangeIsVacuouslyTrue) {
	BitSet b(64);
	EXPECT_TRUE(b.All(3, 3));
	EXPECT_TRUE(b.All(0, 0));  // empty range at the start
}

TEST(Util_BitSet, AllAcrossWordBoundary) {
	BitSet b(192);
	b.Set(60, 68);
	EXPECT_TRUE(b.All(60, 68));
	EXPECT_FALSE(b.All(59, 68));
	EXPECT_FALSE(b.All(60, 69));
	b.Set(0, 192);
	EXPECT_TRUE(b.All(0, 192));
	EXPECT_TRUE(b.All(0, 128));
	EXPECT_TRUE(b.All(64, 128));  // whole word 1
}

TEST(Util_BitSet, AllAtWordAlignedEnd) {
	// endIndex a multiple of 64 selects the whole end word.
	BitSet b(192);
	b.Set(0, 64);
	EXPECT_TRUE(b.All(0, 64));
	b.Set(64, 128);
	EXPECT_TRUE(b.All(0, 128));
	EXPECT_FALSE(b.All(0, 129));
}

// ---- Set relation predicates ---------------------------------------------

TEST(Util_BitSet, SetEqualsSubsetSuperset) {
	BitSet a(64), b(64), c(64);
	a.Set(1);
	a.Set(3);
	b.Set(1);
	b.Set(3);
	c.Set(1);
	EXPECT_TRUE(a.SetEquals(b));
	EXPECT_FALSE(a.SetEquals(c));
	EXPECT_TRUE(c.IsSubsetOf(a));
	EXPECT_TRUE(a.IsSupersetOf(c));
	EXPECT_TRUE(c.IsProperSubsetOf(a));
	EXPECT_TRUE(a.IsProperSupersetOf(c));
	EXPECT_FALSE(a.IsProperSubsetOf(b));  // equal -> not proper
}

TEST(Util_BitSet, SetRelationsAcrossWordBoundary) {
	BitSet a(192), b(192);
	a.Set(5);
	a.Set(70);
	a.Set(150);
	b.Set(5);
	b.Set(70);
	EXPECT_TRUE(b.IsSubsetOf(a));
	EXPECT_FALSE(a.IsSubsetOf(b));
	EXPECT_TRUE(a.Overlaps(b));
	BitSet empty(192);
	EXPECT_FALSE(a.Overlaps(empty));
}

TEST(Util_BitSet, Overlaps) {
	BitSet a(64), b(64);
	a.Set(3);
	b.Set(4);
	EXPECT_FALSE(a.Overlaps(b));
	b.Set(3);
	EXPECT_TRUE(a.Overlaps(b));
}

// ---- Union / Intersect ----------------------------------------------------

TEST(Util_BitSet, UnionWith) {
	BitSet a(192), b(192);
	a.Set(1);
	a.Set(70);
	b.Set(70);
	b.Set(140);
	a.UnionWith(b);
	EXPECT_TRUE(a[1]);
	EXPECT_TRUE(a[70]);
	EXPECT_TRUE(a[140]);
	EXPECT_FALSE(a[2]);
}

TEST(Util_BitSet, IntersectWith) {
	BitSet a(192), b(192);
	a.Set(1);
	a.Set(70);
	a.Set(140);
	b.Set(70);
	b.Set(141);
	a.IntersectWith(b);
	EXPECT_FALSE(a[1]);
	EXPECT_TRUE(a[70]);
	EXPECT_FALSE(a[140]);
}

// ---- NextSetBit / SetBits -------------------------------------------------

TEST(Util_BitSet, NextSetBitSingleWord) {
	BitSet b(64);
	b.Set(3);
	b.Set(40);
	EXPECT_EQ(b.NextSetBit(0, 64), 3);
	EXPECT_EQ(b.NextSetBit(4, 64), 40);
	EXPECT_EQ(b.NextSetBit(41, 64), -1);
	EXPECT_EQ(b.NextSetBit(0, 10), 3);
}

TEST(Util_BitSet, NextSetBitAcrossWordBoundary) {
	BitSet b(192);
	b.Set(3);
	b.Set(64);
	b.Set(130);
	EXPECT_EQ(b.NextSetBit(0, 192), 3);
	EXPECT_EQ(b.NextSetBit(4, 192), 64);
	EXPECT_EQ(b.NextSetBit(65, 192), 130);
	EXPECT_EQ(b.NextSetBit(131, 192), -1);
}

TEST(Util_BitSet, NextSetBitEmptyRange) {
	BitSet b(64);
	b.Set(5);
	EXPECT_EQ(b.NextSetBit(0, 0), -1);
	EXPECT_EQ(b.NextSetBit(6, 6), -1);
}

TEST(Util_BitSet, SetBitsEnumeratesAscending) {
	BitSet b(192);
	for (int i : {2, 64, 65, 130})
		b.Set(i);
	std::vector<int> bits = b.SetBits(0, 192);
	EXPECT_EQ(bits, (std::vector<int>{2, 64, 65, 130}));
	EXPECT_EQ(b.SetBits(3, 66), (std::vector<int>{64, 65}));
	EXPECT_TRUE(b.SetBits(131, 192).empty());
}

// ---- ReplaceWith / Clone --------------------------------------------------

TEST(Util_BitSet, ReplaceWith) {
	BitSet a(64), b(64);
	a.Set(3);
	a.Set(7);
	b.ReplaceWith(a);
	EXPECT_TRUE(b[3]);
	EXPECT_TRUE(b[7]);
	EXPECT_FALSE(b[0]);
	a.Clear(3);
	EXPECT_TRUE(b[3]);  // b is a copy, not aliased
	EXPECT_TRUE(b[7]);
}

TEST(Util_BitSet, CloneIsIndependent) {
	BitSet a(192);
	a.Set(70);
	BitSet c = a.Clone();
	EXPECT_TRUE(c[70]);
	a.Clear(70);
	EXPECT_TRUE(c[70]);  // clone is independent
	EXPECT_FALSE(a[70]);
}

// ---- ToString ------------------------------------------------------------

TEST(Util_BitSet, ToString) {
	BitSet b(192);
	EXPECT_EQ(b.ToString(), "{}");
	b.Set(0);
	b.Set(3);
	b.Set(64);
	EXPECT_EQ(b.ToString(), "{0, 3, 64}");
}

// ---- The DoLift relevance contract ---------------------------------------
// `DoLift` / `DoLiftBinary` return a `BitSet(nullableVars.Count)` whose bit i
// is set iff nullable var i was "relevant" to the lifted expression; the
// `bits.All(0, nullableVars.Count)` gate then decides whether the lift is
// allowed (every nullable var must contribute).

TEST(Util_BitSet, RelevanceGateAllIsTrueWhenEveryVarRelevant) {
	// 3 nullable vars, all relevant -> lift allowed.
	BitSet bits(3);
	bits.Set(0);
	bits.Set(1);
	bits.Set(2);
	EXPECT_TRUE(bits.All(0, 3));
}

TEST(Util_BitSet, RelevanceGateAllIsFalseWhenAnyVarIrrelevant) {
	// 3 nullable vars, var 1 not relevant -> lift blocked.
	BitSet bits(3);
	bits.Set(0);
	bits.Set(2);
	EXPECT_FALSE(bits.All(0, 3));
}

TEST(Util_BitSet, RelevanceGateSingleVar) {
	BitSet bits(1);
	EXPECT_FALSE(bits.All(0, 1));
	bits.Set(0);
	EXPECT_TRUE(bits.All(0, 1));
}

TEST(Util_BitSet, RelevanceGateFoundIndicesAny) {
	// `DoLift`'s GetValueOrDefault branch builds a fresh BitSet and sets the
	// matching var's bit; `Any()` confirms at least one var matched.
	BitSet found(4);
	EXPECT_FALSE(found.Any());
	found.Set(2);
	EXPECT_TRUE(found.Any());
}
