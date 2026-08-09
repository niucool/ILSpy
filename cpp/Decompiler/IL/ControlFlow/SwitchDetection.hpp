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

// Port of ICSharpCode.Decompiler/IL/ControlFlow/SwitchDetection.cs.
//
// SwitchDetection reconstructs a C# switch compiled to a sequence of if-
// statements (non-contiguous case labels) as a single ILAst SwitchInstruction.
// The full transform (Run/ProcessBlock/UseCSharpSwitch/AnalyzeControlFlow/
// LoopContext/AddNullCase) depends on several pieces this port does not yet
// carry: the LoopContext inner class (which needs HighLevelLoopTransform's
// MatchIncrementBlock/MatchDoWhileConditionBlock helpers), the CFG continue-
// break analysis, NullableLiftingTransform.MatchHasValueCall (AddNullCase),
// SwitchOnStringTransform.MatchComputeStringOrReadOnlySpanHashCall
// (MatchRoslynSwitchOnString), and the SparseIntegerSwitch setting gate. Those
// land in later iterations; this header exposes the self-contained subset that
// is reachable today: SimplifySwitchInstruction, which the C# pipeline calls
// twice -- once from ControlFlowSimplification (1st pass, before branch-chain
// collapse) and once from SwitchDetection.ProcessBlock (2nd pass, when a block
// does not form a switch). It operates on SwitchInstructions the IL reader
// already emits from `switch` opcodes, so wiring the 1st pass into CFS makes
// real progress on the corpus without the deferred dependencies.
//
// SimplifySwitchInstruction:
//   - de-duplicates sections whose body is a Branch to the same block, merging
//     their label sets (UnionWith);
//   - AdjustLabels: moves an Add/Sub offset from `switch(V +/- val)` into the
//     section labels (the value becomes `V`, each label is AddOffset-shifted);
//   - SortSwitchSections: orders the sections (by label value when the
//     SortSwitchSections setting is on, otherwise by branch-target IL offset,
//     preserving the original case order).

#pragma once

#include "Decompiler/FlowAnalysis/ControlFlowNode.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <vector>

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;
class ControlFlowGraph;
class SwitchInstruction;

class SwitchDetection {
public:
    // De-duplicate, adjust labels, and sort the SwitchInstruction that is the
    // block's final instruction (if any). No-op when the block does not end in a
    // SwitchInstruction. Called by ControlFlowSimplification (1st pass) and by
    // SwitchDetection.ProcessBlock (2nd pass). Mirrors the C# static
    // SwitchDetection.SimplifySwitchInstruction.
    static void SimplifySwitchInstruction(Block* block, ILTransformContext& context);

    // Move an Add/Sub offset from `sw.Value` into the section labels. No-op when
    // the value is not a non-overflow-checked BinaryNumericInstruction Add/Sub
    // with a constant right operand. Mirrors the C# static AdjustLabels.
    static void AdjustLabels(SwitchInstruction* sw, ILTransformContext& context);

    // Order sw.Sections by label value (SortSwitchSections setting on) or by
    // branch-target IL offset (setting off, the C# default; preserves the
    // original case order). Mirrors the C# static SortSwitchSections.
    static void SortSwitchSections(SwitchInstruction* sw, ILTransformContext& context);

    // Port of SwitchDetection.LoopContext: the continue/break analysis over a
    // per-container control-flow graph that SwitchDetection.Run needs to decide
    // whether a detected if-chain can become a `switch` without `goto` statements.
    // A `continue;` jumps to the loop's increment or do-while condition block
    // (the back-edge block the loop head dominates); this class maps each such
    // block to its continue depth (1 for the innermost loop, higher for outer
    // loops) and lists the blocks a `break;` would target. Constructed for one
    // `contextNode` (the switch head) and its dominator tree. The full
    // SwitchDetection.Run that consumes this is deferred; this class is exposed
    // so the foundation is testable in isolation.
    class LoopContext {
    public:
        // Build the continue-depth map for `contextNode` over `cfg`: walk
        // contextNode's successors, collecting every node that dominates
        // contextNode (a loop head), then map each loop head's continue target
        // (its increment/do-while-condition block, via the HighLevelLoopTransform
        // helpers) to a depth in post-order. Mirrors the C# constructor.
        LoopContext(const ControlFlowGraph& cfg, FlowAnalysis::ControlFlowNode* contextNode);

        // Whether `node` is a `continue;` target. Mirrors MatchContinue(node).
        bool MatchContinue(FlowAnalysis::ControlFlowNode* node) const;

        // Whether `node` is a continue target at exactly `depth`. Mirrors
        // MatchContinue(node, int depth). (The C# also has MatchContinue(node,
        // out int depth); this port exposes GetContinueDepth instead, since a
        // C++ `int&` out-overload alongside the by-value `int` overload is
        // ambiguous for an lvalue argument.)
        bool MatchContinue(FlowAnalysis::ControlFlowNode* node, int depth) const;

        // The continue depth of `node` (0 when it is not a continue target).
        // Mirrors GetContinueDepth.
        int GetContinueDepth(FlowAnalysis::ControlFlowNode* node) const;

        // The blocks a `break;` from the dominator subtree would jump to: every
        // successor of the subtree (excluding continue targets) that the
        // `dominator` does not itself dominate and that is not a depth-1 continue.
        // Mirrors GetBreakTargets. Returned in pre-order traversal order (may
        // contain duplicates; the caller dedups).
        std::vector<FlowAnalysis::ControlFlowNode*> GetBreakTargets(FlowAnalysis::ControlFlowNode* dominator) const;

    private:
        std::vector<std::pair<FlowAnalysis::ControlFlowNode*, int>> continueDepth_;

        static FlowAnalysis::ControlFlowNode* FindContinue(FlowAnalysis::ControlFlowNode* loopHead);
    };
};

} // namespace ILSpy::Decompiler::IL
