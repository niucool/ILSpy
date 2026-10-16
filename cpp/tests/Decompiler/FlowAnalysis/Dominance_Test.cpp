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

// Dominance tests: the Cooper-Harvey-Kennedy iterative dominator computation,
// post-order numbering, Dominates(), and the reachable-exit marker used by the
// loop/transform analyses.

#include "Decompiler/FlowAnalysis/ControlFlowNode.hpp"
#include "Decompiler/FlowAnalysis/Dominance.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using namespace ILSpy::Decompiler::FlowAnalysis;

namespace {

// entry -> B -> C -> D (diamond) -> E ; B->D ; C->D ; D->E ; E loops to C
//   plus U unreachable: U -> D
struct SmallGraph {
    ControlFlowNode Uentry, B, C, D, E, U;
    SmallGraph() {
        Uentry.AddEdgeTo(&B);
        B.AddEdgeTo(&C);
        C.AddEdgeTo(&D);
        D.AddEdgeTo(&E);
        B.AddEdgeTo(&D);
        E.AddEdgeTo(&C);
        U.AddEdgeTo(&D);
    }
    std::vector<ControlFlowNode*> All() {
        return { &Uentry, &B, &C, &D, &E, &U };
    }
};

} // namespace

TEST(Dominance, ComputesImmediateDominators) {
    SmallGraph g;
    Dominance::ComputeDominance(&g.Uentry);

    EXPECT_EQ(g.Uentry.ImmediateDominator, nullptr) << "the entry has no idom";
    EXPECT_EQ(g.B.ImmediateDominator, &g.Uentry);
    // E and C form a loop; both are reached from B:
    EXPECT_EQ(g.C.ImmediateDominator, &g.B);
    // Every path from the entry to D passes through B (B dominates D even
    // though D has three predecessors).
    EXPECT_EQ(g.D.ImmediateDominator, &g.B);
    EXPECT_EQ(g.E.ImmediateDominator, &g.D);
    EXPECT_TRUE(g.Uentry.Dominates(&g.E));
    EXPECT_TRUE(g.D.Dominates(&g.E));
    EXPECT_FALSE(g.E.Dominates(&g.D));
    EXPECT_TRUE(g.C.Dominates(&g.C)) << "non-strict dominance includes self";
}

TEST(Dominance, UnreachableNodeHasNoDominatorOrChildren) {
    SmallGraph g;
    Dominance::ComputeDominance(&g.Uentry);

    EXPECT_TRUE(g.Uentry.IsReachable());
    EXPECT_TRUE(g.E.IsReachable());
    EXPECT_FALSE(g.U.IsReachable()) << "U is not reachable from the entry";
    EXPECT_EQ(g.U.ImmediateDominator, nullptr);
}

TEST(Dominance, PostOrderNumbersAreAssignedAndDistinct) {
    SmallGraph g;
    Dominance::ComputeDominance(&g.Uentry);

    // 5 reachable nodes get 0..4; the entry is last (highest post-order).
    std::vector<int> numbers;
    for (ControlFlowNode* n : { &g.Uentry, &g.B, &g.C, &g.D, &g.E }) {
        EXPECT_GE(n->PostOrderNumber, 0);
        numbers.push_back(n->PostOrderNumber);
    }
    std::sort(numbers.begin(), numbers.end());
    for (int i = 0; i < 5; ++i) EXPECT_EQ(numbers[i], i);
    EXPECT_EQ(g.Uentry.PostOrderNumber, 4);
    EXPECT_EQ(g.U.PostOrderNumber, -1) << "unreachable nodes keep no number";
}

TEST(Dominance, MarkNodesWithReachableExitsMarksDiamondHead) {
    SmallGraph g;
    Dominance::ComputeDominance(&g.Uentry);
    std::vector<ControlFlowNode*> cfg = g.All();
    for (std::size_t i = 0; i < cfg.size(); ++i) cfg[i]->UserIndex = static_cast<int>(i);
    auto nonEmpty = Dominance::MarkNodesWithReachableExits(std::move(cfg));

    // Two joins: D (preds C/B/E, idom B) and C (preds B/E, idom B). The idom
    // walks from each join's predecessors mark the runners between the
    // predecessor and the join's idom: C (path to D), E and D (from E's path
    // to C), and U (D's unreachable predecessor -- reachability of the
    // *predecessor* is not consulted, only of the join).
    EXPECT_FALSE(nonEmpty[0]) << "entry is on no join path";
    EXPECT_FALSE(nonEmpty[1]) << "B dominates both joins";
    EXPECT_TRUE(nonEmpty[2]);
    EXPECT_TRUE(nonEmpty[3]);
    EXPECT_TRUE(nonEmpty[4]);
    EXPECT_TRUE(nonEmpty[5]) << "join predecessors are marked regardless of their reachability";
}
