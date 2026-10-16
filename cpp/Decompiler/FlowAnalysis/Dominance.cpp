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

#include "Decompiler/FlowAnalysis/Dominance.hpp"
#include "Decompiler/FlowAnalysis/ControlFlowNode.hpp"

#include <cassert>
#include <vector>

namespace ILSpy::Decompiler::FlowAnalysis {
namespace Dominance {

void ComputeDominance(ControlFlowNode* entryPoint) {
    std::vector<ControlFlowNode*> nodes;
    entryPoint->TraversePostOrder(
        [](ControlFlowNode* n) { return n->Successors; },
        [&](ControlFlowNode* n) { nodes.push_back(n); });
    assert(nodes.back() == entryPoint);
    for (std::size_t i = 0; i < nodes.size(); ++i)
        nodes[i]->PostOrderNumber = static_cast<int>(i);

    // For the purpose of the algorithm the entry dominates itself; reset at end.
    entryPoint->ImmediateDominator = entryPoint;
    bool changed;
    do {
        changed = false;
        // All nodes except the entry, in reverse post-order:
        for (std::size_t i = nodes.size() - 1; i-- > 0; ) {
            ControlFlowNode* b = nodes[i];
            ControlFlowNode* newIdom = nullptr;
            for (ControlFlowNode* p : b->Predecessors) {
                // Ignore predecessors that were not processed yet.
                if (p->ImmediateDominator == nullptr) continue;
                if (newIdom == nullptr) newIdom = p;
                else newIdom = FindCommonDominator(p, newIdom);
            }
            // Reverse post-order ensures at least one predecessor was processed.
            assert(newIdom != nullptr);
            if (newIdom != b->ImmediateDominator) {
                b->ImmediateDominator = newIdom;
                changed = true;
            }
        }
    } while (changed);

    for (ControlFlowNode* node : nodes) {
        if (node->ImmediateDominator != nullptr)
            node->DominatorTreeChildren = std::vector<ControlFlowNode*>();
    }
    entryPoint->ImmediateDominator = nullptr;
    for (ControlFlowNode* node : nodes) {
        if (node->ImmediateDominator != nullptr)
            node->ImmediateDominator->DominatorTreeChildren->push_back(node);
        node->Visited = false;
    }
}

ControlFlowNode* FindCommonDominator(ControlFlowNode* a, ControlFlowNode* b) {
    while (a != b) {
        while (a->PostOrderNumber < b->PostOrderNumber) a = a->ImmediateDominator;
        while (b->PostOrderNumber < a->PostOrderNumber) b = b->ImmediateDominator;
    }
    return a;
}

std::vector<bool> MarkNodesWithReachableExits(const std::vector<ControlFlowNode*>& cfg) {
#ifndef NDEBUG
    for (std::size_t i = 0; i < cfg.size(); ++i) assert(cfg[i]->UserIndex == (int)i);
#endif
    std::vector<bool> nonEmpty(cfg.size(), false);
    for (ControlFlowNode* j : cfg) {
        // If j is a join-point (more than one incoming edge); the root
        // (reachable with no immediate dominator) counts as an extra edge.
        if (j->IsReachable() && (j->Predecessors.size() >= 2 ||
                                 (j->Predecessors.size() >= 1 && j->ImmediateDominator == nullptr))) {
            for (ControlFlowNode* p : j->Predecessors) {
                for (ControlFlowNode* runner = p;
                     runner != j->ImmediateDominator && runner != j && runner != nullptr;
                     runner = runner->ImmediateDominator) {
                    nonEmpty[runner->UserIndex] = true;
                }
            }
        }
    }
    return nonEmpty;
}

} // namespace Dominance
} // namespace ILSpy::Decompiler::FlowAnalysis
