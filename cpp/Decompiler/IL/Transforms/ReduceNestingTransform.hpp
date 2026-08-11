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

// Port of ICSharpCode.Decompiler/IL/Transforms/ReduceNestingTransform.cs.
//
// ReduceNestingTransform improves code quality by duplicating keyword exits
// (return/break/continue) to reduce nesting and restoring IL order, plus a
// separate EliminateRedundantTryFinally pass.
//
// This iteration ports:
//  * EliminateRedundantTryFinally (the C# compiler sometimes wraps a `fixed`
//    block in a try-finally whose finally is an empty `leave (nop)`; once
//    DetectPinnedRegions has formed the PinnedRegion the try-finally is
//    redundant and is replaced with the PinnedRegion directly).
//  * The nesting-reduction heuristics (ComputeStats / UpdateStats /
//    ShouldReduceNesting) as a tested foundation, plus the self-contained
//    pattern helpers (BlockUnwrap / MatchBranch / MatchLeave /
//    MatchConditionBlock) the heuristics and the future nesting-reduction folds
//    consult.
//  * ImproveILOrdering (the IL-order-gated InvertIf): for an if-as-FinalInstruction
//    with an unreachable TrueInst and no else, re-inverts ConditionDetection's
//    inversion when the IL order is wrong (the old then / next block comes
//    BEFORE the falseCode / TrueInst in IL). The gate consults the D150
//    ConditionDetection.GetStartILOffset (which the Block-label adaptation
//    makes valid for Block TrueInsts and the next block) and the D151
//    ConditionDetection.InvertIf. The trailing-leave handling (the C#
//    `block.Instructions.Last() is Leave` check + the CanDuplicateExit
//    replacement) is wired: when the if's block's last instruction (Xn = this
//    port's nextBlock->FinalInstruction, the old-then exit) is a non-keyword
//    Leave, it is replaced with a keyword exit (return/continue/break) via
//    CanDuplicateExit before InvertIf, or the fold bails if it can't be
//    duplicated. `continueTarget` (the loop entry-point block a `continue`
//    branches to) is tracked by the Visit walk per-container (Loop/While -> the
//    entry point; DoWhile -> the last block). The ReduceNesting /
//    ReduceSwitchNesting / ExtractElseBlock folds (the rest of the C# `Visit`)
//    are deferred (need the full EnsureEndPointUnreachable / ExtractElseBlock
//    helpers + the D39 dominator analysis for ReduceSwitchNesting).
//  * CanDuplicateExit (a tested-but-not-yet-wired foundation): the helper that
//    decides whether an exit is a duplicable keyword exit (return/break/
//    continue), walking out of a try/pinned/lock container to the following
//    instruction when the exit is a leave of a Normal container. The wired
//    ImproveILOrdering trailing-leave handling and the deferred ReduceNesting /
//    ReduceSwitchNesting folds consult it; no pipeline transform consults it
//    yet (the wired folds are the subsequent iterations).
//
// Block-model adaptation: the C# Block.Instructions does NOT include the
// FinalInstruction (a void block's final is a Nop, and the control flow
// Leave/Branch are non-terminals in Instructions). This port splits the block's
// non-terminal Instructions from its FinalInstruction (the control flow), so
// the C# `block.Instructions.Last()` (the last non-terminal, which is the
// control flow in the C#) is this port's `block->FinalInstruction`. The
// heuristics and ImproveILOrdering are adapted accordingly: the C# `Visit`
// iterates `block.Instructions` and calls ImproveILOrdering on each if
// non-terminal; this port's if-as-final model makes the if the block's
// FinalInstruction, so the visit checks the final and the falseCode+exit (the
// C# siblings after the if) is the next block in the container.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;
class IfInstruction;
class ILInstruction;

class ReduceNestingTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // Heuristics for the (deferred) nesting-reduction folds, ported as a tested
    // foundation. ComputeStats tallies the number of statements and the maximum
    // nested depth of an instruction; UpdateStats takes the max over one path;
    // ShouldReduceNesting decides whether duplicating exits into the sibling
    // blocks to reduce the nesting of `block` by 1 is worthwhile.
    static void ComputeStats(ILInstruction* inst, int& numStatements, int& maxDepth,
                             int currentDepth, bool isStatement = true);
    static void UpdateStats(ILInstruction* inst, int& maxStatements, int& maxDepth);
    static bool ShouldReduceNesting(Block* block, int maxStatements, int maxDepth);

    // The C# `ReduceNestingTransform.CanDuplicateExit`: checks whether an exit
    // instruction is a duplicable keyword exit (return; break; continue;). The
    // wired ImproveILOrdering trailing-leave handling and the deferred
    // ReduceNesting / ReduceSwitchNesting folds consult it. `keywordExit`
    // reports the keyword exit to duplicate (the exit itself for a direct
    // return/break/continue, or the keyword exit found by walking out of a
    // try/pinned/lock container). `continueTarget` is the loop entry-point block
    // a `continue` branches to (null at the top level).
    static bool CanDuplicateExit(ILInstruction* exit, Block* continueTarget,
                                 ILInstruction*& keywordExit);
};

} // namespace ILSpy::Decompiler::IL
