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

#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/FlowAnalysis/Dominance.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <functional>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkBlocks(ILInstruction* inst, const std::function<bool(ILInstruction*)>& enter) {
    if (!inst || !enter(inst)) return;
    for (int i = 0; i < inst->ChildCount(); ++i) WalkBlocks(inst->GetChild(i), enter);
}

} // namespace

ControlFlowGraph::ControlFlowGraph(BlockContainer* container)
    : container_(container) {
    auto& blocks = container->Blocks;
    cfg_.reserve(blocks.size());
    nodeHasDirectExitOut_ = std::vector<bool>(blocks.size(), false);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        auto node = std::make_unique<FlowAnalysis::ControlFlowNode>();
        node->UserIndex = static_cast<int>(i);
        node->UserData = blocks[i].get();
        dict_.emplace(blocks[i].get(), node.get());
        cfg_.push_back(std::move(node));
    }
    CreateEdges();
    if (!cfg_.empty())
        FlowAnalysis::Dominance::ComputeDominance(cfg_[0].get());
    nodeHasReachableExit_ = FlowAnalysis::Dominance::MarkNodesWithReachableExits(
        [&] {
            std::vector<FlowAnalysis::ControlFlowNode*> raw;
            raw.reserve(cfg_.size());
            for (auto& n : cfg_) raw.push_back(n.get());
            return raw;
        }());
    auto exits = FindNodesWithExitsOutOfContainer();
    for (std::size_t i = 0; i < exits.size(); ++i)
        if (exits[i]) nodeHasReachableExit_[i] = true;
}

void ControlFlowGraph::CreateEdges() {
    auto& blocks = container_->Blocks;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        Block* block = blocks[i].get();
        auto* sourceNode = cfg_[i].get();
        // Branch descendants (including the block's final, switch sections' bodies,
        // if-branches) create CFG edges; leaves out of the container don't.
        WalkBlocks(block, [&](ILInstruction* inst) {
            if (auto* br = dynamic_cast<Branch*>(inst)) {
                Block* tgt = br->TargetBlock;
                if (tgt && tgt->Parent == container_) {
                    sourceNode->AddEdgeTo(dict_[tgt]);
                } else if (tgt && tgt->IsDescendantOf(container_)) {
                    // Internal control flow within a nested container; not ours.
                } else {
                    // Branch out of this container into a parent container.
                    nodeHasDirectExitOut_[i] = true;
                }
            } else if (auto* leave = dynamic_cast<Leave*>(inst)) {
                // Exits out of the whole function (a return) are not exits for
                // HasReachableExit purposes; exits leaving this container are.
                bool leavingFunction = leave->TargetContainer != nullptr &&
                    leave->TargetContainer->Parent != nullptr &&
                    leave->TargetContainer->Parent->IsRoot();
                if (!leavingFunction &&
                    (!leave->TargetContainer || !leave->TargetContainer->IsDescendantOf(block))) {
                    nodeHasDirectExitOut_[i] = true;
                }
            }
            return true;
        });
        // Our reader materializes branch finals only for explicit jumps: a
        // block whose final is a conditional If falls through to the next
        // block in the container (the C# reader emits an explicit fall-through
        // Branch instead; the edge is the same either way).
        ILInstruction* fin = block->FinalInstruction.get();
        if (fin && !HasFlag(fin->Flags(), InstructionFlags::EndPointUnreachable)) {
            if (i + 1 < blocks.size() && blocks[i + 1]->Parent == container_)
                sourceNode->AddEdgeTo(cfg_[i + 1].get());
        }
    }
}

std::vector<bool> ControlFlowGraph::FindNodesWithExitsOutOfContainer() const {
    // Marks nodes that exit the container entirely (with or without returning):
    // invariant: leaving[n] implies leaving[n.idom].
    std::vector<bool> leaving(cfg_.size(), false);
    for (const auto& node : cfg_) {
        int idx = node->UserIndex;
        if (idx < 0 || static_cast<std::size_t>(idx) >= cfg_.size()) continue;
        if (leaving[idx]) continue;
        if (nodeHasDirectExitOut_[idx]) {
            for (auto* p = node.get(); p != nullptr; p = p->ImmediateDominator) {
                if (p->UserIndex < 0) break;
                if (leaving[p->UserIndex]) break;  // stop at already-marked
                leaving[p->UserIndex] = true;
            }
        }
    }
    return leaving;
}

} // namespace ILSpy::Decompiler::IL
