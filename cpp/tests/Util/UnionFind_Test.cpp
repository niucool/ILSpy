// Tests for UnionFind (the C# Util.UnionFind, the disjoint-set the
// SplitVariables GroupStores pass uses to merge store instructions that
// must stay together): path-compressed Find and rank-ordered Merge.

#include "Decompiler/Util/UnionFind.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using ILSpy::Decompiler::Util::UnionFind;

TEST(Util_UnionFind, ElementsStartInSingletonSets) {
    UnionFind<std::string> uf;
    EXPECT_EQ(uf.Find("a"), "a");
    EXPECT_EQ(uf.Find("b"), "b");
    EXPECT_NE(uf.Find("a"), uf.Find("b"));
}

TEST(Util_UnionFind, MergeJoinsSetsAndFindIsStable) {
    UnionFind<std::string> uf;
    uf.Merge("a", "b");
    EXPECT_EQ(uf.Find("a"), uf.Find("b"));
    uf.Merge("c", "d");
    EXPECT_EQ(uf.Find("c"), uf.Find("d"));
    EXPECT_NE(uf.Find("a"), uf.Find("c"));
    // Transitivity: merging representatives joins the sets.
    uf.Merge("b", "c");
    EXPECT_EQ(uf.Find("a"), uf.Find("d"));
}

TEST(Util_UnionFind, MergeIsIdempotent) {
    UnionFind<std::string> uf;
    uf.Merge("a", "b");
    const std::string root = uf.Find("a");
    uf.Merge("a", "b");
    uf.Merge("b", "a");
    EXPECT_EQ(uf.Find("a"), root);
    EXPECT_EQ(uf.Find("b"), root);
}

TEST(Util_UnionFind, WorksOverPointers) {
    UnionFind<const void*> uf;
    int a = 0, b = 0;
    uf.Merge(&a, &b);
    EXPECT_EQ(uf.Find(&a), uf.Find(&b));
    EXPECT_EQ(uf.Find(&a), uf.Find(&a));
}

} // namespace
