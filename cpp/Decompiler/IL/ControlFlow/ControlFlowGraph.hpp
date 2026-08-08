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

// Port of ICSharpCode.Decompiler/IL/ControlFlow/ControlFlowGraph.cs: a control
// flow graph over one BlockContainer, with dominance precomputed. Leaves out
// of the container are not modeled as edges (but are recorded for
// HasReachableExit queries).

#pragma once

#include "Decompiler/FlowAnalysis/ControlFlowNode.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;

class ControlFlowGraph {
public:
    explicit ControlFlowGraph(BlockContainer* container);

    BlockContainer* Container() const { return container_; }

    // Nodes in original block order; Nodes()[i]->UserData == Blocks()[i].
    const std::vector<std::unique_ptr<FlowAnalysis::ControlFlowNode>>& Nodes() const { return cfg_; }

    // The node for a block that belonged to the container at construction.
    // Requires the block to have been a member then.
    FlowAnalysis::ControlFlowNode* GetNode(Block* block) const {
        auto it = dict_.find(block);
        return it == dict_.end() ? nullptr : it->second;
    }

    // True iff control can leave the node's dominated region via a branch or
    // leave (i.e. without executing a return/throw).
    bool HasReachableExit(const FlowAnalysis::ControlFlowNode* node) const {
        return node->UserIndex >= 0 &&
               static_cast<std::size_t>(node->UserIndex) < nodeHasReachableExit_.size() &&
               nodeHasReachableExit_[node->UserIndex];
    }

    // Whether the node directly contains a branch/leave exiting the container.
    bool HasDirectExitOutOfContainer(const FlowAnalysis::ControlFlowNode* node) const {
        return node->UserIndex >= 0 &&
               static_cast<std::size_t>(node->UserIndex) < nodeHasDirectExitOut_.size() &&
               nodeHasDirectExitOut_[node->UserIndex];
    }

private:
    BlockContainer* container_;
    std::vector<std::unique_ptr<FlowAnalysis::ControlFlowNode>> cfg_;
    std::unordered_map<Block*, FlowAnalysis::ControlFlowNode*> dict_;
    std::vector<bool> nodeHasDirectExitOut_;
    std::vector<bool> nodeHasReachableExit_;

    void CreateEdges();
    std::vector<bool> FindNodesWithExitsOutOfContainer() const;
};

} // namespace ILSpy::Decompiler::IL
