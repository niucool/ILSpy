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

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
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
};

} // namespace ILSpy::Decompiler::IL
