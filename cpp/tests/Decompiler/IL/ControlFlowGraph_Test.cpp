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

// ControlFlowGraph tests: per-container CFG construction (branch edges +
// positional fall-through after conditional finals), dominance over real
// decoded method bodies, and exit marking.

#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::FlowAnalysis::ControlFlowNode;

namespace {

// b0: if (1==1) br b3 ; b1: br b3 ; b2: br b3 ; b3: leave
// (positional fall-through: b0->b1, b1->b2, so the diamond is b0->{b1,b2}->b3)
struct DiamondBlocks {
    std::vector<std::unique_ptr<Block>> blocks;
    std::unique_ptr<BlockContainer> container;

    DiamondBlocks() {
        container = std::make_unique<BlockContainer>();
        for (int i = 0; i < 4; ++i) container->AddBlock(std::make_unique<Block>());
        Block* b1 = container->Blocks[1].get();
        Block* b2 = container->Blocks[2].get();
        Block* b3 = container->Blocks[3].get();
        (void)b1; (void)b2;

        container->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
            std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(1),
                                   ComparisonKind::Equality),
            std::make_unique<Branch>(b3)));
        container->Blocks[1]->SetFinal(std::make_unique<Branch>(b3));
        container->Blocks[2]->SetFinal(std::make_unique<Branch>(b3));
        container->Blocks[3]->SetFinal(std::make_unique<Leave>(container.get()));
    }
};

} // namespace

TEST(ControlFlowGraph, EdgeStructureAndDominators) {
    DiamondBlocks d;
    ControlFlowGraph cfg(d.container.get());
    ASSERT_EQ(cfg.Nodes().size(), 4u);

    auto* n0 = cfg.GetNode(d.container->Blocks[0].get());
    auto* n1 = cfg.GetNode(d.container->Blocks[1].get());
    auto* n2 = cfg.GetNode(d.container->Blocks[2].get());
    auto* n3 = cfg.GetNode(d.container->Blocks[3].get());

    EXPECT_EQ(n0->Successors.size(), 2u) << "if + fall-through";
    EXPECT_EQ(n1->Successors.size(), 1u);
    EXPECT_EQ(n3->Successors.size(), 0u) << "leave has no in-container edge";
    EXPECT_EQ(n3->Predecessors.size(), 3u) << "b1, b2 and the if branch";

    EXPECT_EQ(n0->ImmediateDominator, nullptr) << "entry has no idom";
    EXPECT_EQ(n3->ImmediateDominator, n0) << "diamond join idom is the head";
    EXPECT_TRUE(n0->Dominates(n3));
    EXPECT_FALSE(n3->Dominates(n0));
}

TEST(ControlFlowGraph, LeaveToFunctionBodyIsNotAReachableExit) {
    DiamondBlocks d;
    ControlFlowGraph cfg(d.container.get());
    auto* n3 = cfg.GetNode(d.container->Blocks[3].get());
    // b3's leave targets the *naked* container here (not an ILFunction body),
    // so it counts as an exit from the container.
    EXPECT_TRUE(cfg.HasDirectExitOutOfContainer(n3));
}
