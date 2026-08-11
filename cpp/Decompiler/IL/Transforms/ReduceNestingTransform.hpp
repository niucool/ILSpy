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
//    ShouldReduceNesting) as a tested-but-not-yet-wired foundation, plus the
//    self-contained pattern helpers (BlockUnwrap / MatchBranch / MatchLeave /
//    MatchConditionBlock) the heuristics and the future nesting-reduction folds
//    consult. These are pure analysis helpers -- no tree mutation -- ported
//    ahead of the wired Visit / ReduceNesting / ReduceSwitchNesting /
//    ImproveILOrdering / ExtractElseBlock folds (which need a general
//    ILInstruction.Clone for the keyword-exit duplication [D147, now landed] +
//    the ConditionDetection.InvertIf / GetStartILOffset statics exposed + a
//    block-model corpus probe of the real post-ConditionDetection shape, the
//    recurring D73/D75 divergence).
//
// Block-model adaptation: the C# Block.Instructions does NOT include the
// FinalInstruction (a void block's final is a Nop, and the control flow
// Leave/Branch are non-terminals in Instructions). This port splits the block's
// non-terminal Instructions from its FinalInstruction (the control flow), so
// the C# `block.Instructions.Last()` (the last non-terminal, which is the
// control flow in the C#) is this port's `block->FinalInstruction`. The
// heuristics are adapted accordingly: a Block's control flow (this port's
// FinalInstruction) is counted as a statement the way the C# counts it as the
// last element of Instructions.

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
};

} // namespace ILSpy::Decompiler::IL
