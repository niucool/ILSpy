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

// Port of ICSharpCode.Decompiler/FlowAnalysis/Dominance.cs: the
// Cooper-Harvey-Kennedy iterative dominator computation and helpers.

#pragma once

#include "Decompiler/FlowAnalysis/ControlFlowNode.hpp"

#include <vector>

namespace ILSpy::Decompiler::FlowAnalysis {
namespace Dominance {

// Compute the dominator tree for all nodes reachable from entryPoint
// (Cooper, Harvey, Kennedy: "A Simple, Fast Dominance Algorithm").
// Precondition: Visited == false on all reachable nodes; ImmediateDominator /
// DominatorTreeChildren unset. Postcondition: Visited false again.
void ComputeDominance(ControlFlowNode* entryPoint);

// The common ancestor of a and b in the dominator tree (they must share one).
ControlFlowNode* FindCommonDominator(ControlFlowNode* a, ControlFlowNode* b);

// Marks every node n for which exit-from-dominated-region is possible without
// leaving a return/throw: node[n->UserIndex] == true iff there is a node
// reachable from n that is not dominated by n. Dominance must be computed and
// UserIndex == position for every node.
std::vector<bool> MarkNodesWithReachableExits(const std::vector<ControlFlowNode*>& cfg);

} // namespace Dominance
} // namespace ILSpy::Decompiler::FlowAnalysis
