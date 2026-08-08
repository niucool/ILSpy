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

// Port of ICSharpCode.Decompiler/FlowAnalysis/ControlFlowNode.cs: a node in a
// per-container control-flow graph, with the dominator-tree bookkeeping used
// by Dominance and the loop/condition/exit-point analyses.

#pragma once

#include <functional>
#include <optional>
#include <vector>

namespace ILSpy::Decompiler::FlowAnalysis {

class ControlFlowNode {
public:
    // User index, can be used to look up additional information in an array.
    int UserIndex = -1;
    // User data (the IL Block this node was built for).
    void* UserData = nullptr;
    // Visited flag, used by the traversals and dominance computation.
    bool Visited = false;
    // Post-order index, computed by dominance analysis.
    int PostOrderNumber = -1;

    // The immediate dominator (parent in the dominator tree).
    // Null for the entry point and for unreachable nodes.
    ControlFlowNode* ImmediateDominator = nullptr;
    // Children in the dominator tree; engaged (empty ok) iff the node is
    // reachable once dominance was computed.
    std::optional<std::vector<ControlFlowNode*>> DominatorTreeChildren;

    std::vector<ControlFlowNode*> Predecessors;
    std::vector<ControlFlowNode*> Successors;

    bool IsReachable() const { return DominatorTreeChildren.has_value(); }

    void AddEdgeTo(ControlFlowNode* target) {
        Successors.push_back(target);
        target->Predecessors.push_back(this);
    }

    // Depth-first post-order traversal from this node following `children`;
    // uses the Visited flags. Mirrors TraversePostOrder over GraphTraversal's
    // iterative depth-first search.
    void TraversePostOrder(
            const std::function<std::vector<ControlFlowNode*>(ControlFlowNode*)>& children,
            const std::function<void(ControlFlowNode*)>& postOrderAction) {
        struct Work { ControlFlowNode* node; bool isPostOrderContinuation; };
        std::vector<Work> worklist;
        worklist.push_back({ this, false });
        while (!worklist.empty()) {
            Work w = worklist.back();
            if (w.isPostOrderContinuation) {
                postOrderAction(w.node);
                worklist.pop_back();
                continue;
            }
            if (w.node->Visited) {
                worklist.pop_back();
                continue;
            }
            w.node->Visited = true;
            worklist.back().isPostOrderContinuation = true;
            auto kids = children(w.node);
            // Push successors reversed so they pop in order.
            for (auto it = kids.rbegin(); it != kids.rend(); ++it)
                worklist.push_back({ *it, false });
        }
    }

    // Whether `this` dominates `node` (non-strict).
    bool Dominates(const ControlFlowNode* node) const {
        for (const ControlFlowNode* tmp = node; tmp != nullptr; tmp = tmp->ImmediateDominator)
            if (tmp == this) return true;
        return false;
    }
};

} // namespace ILSpy::Decompiler::FlowAnalysis
