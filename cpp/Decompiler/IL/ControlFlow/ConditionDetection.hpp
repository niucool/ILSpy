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

// Port of ICSharpCode.Decompiler/IL/ControlFlow/ConditionDetection.cs (subset).
// Converts `if (cond) goto target` + single-predecessor fall-through blocks
// into structured if/else by inlining the fall-through into the
// IfInstruction's FalseInst. Skipped: the full exit-point analysis, nested
// condition stacking, and the BlockILTransform post-order driver.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class IfInstruction;

class ConditionDetection : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `ConditionDetection.GetStartILOffset`: the IL byte-offset of the
    // first IL instruction an ILAst instruction represents, for IL-order
    // comparisons (the wired ReduceNestingTransform.ImproveILOrdering fold
    // consults it to decide whether inverting an if to match IL order helps).
    // Some compilers merge the leave instructions for different arguments
    // using stack variables; these get split and inlined, but the Leave's
    // Value's ILRange is a better indicator of the actual location, so a
    // valued Leave (a non-Nop Value) reports its Value's offset. Otherwise the
    // instruction's own StartILOffset is returned. `isEmpty` reports whether
    // the range is empty (the C# `out bool isEmpty`).
    static int GetStartILOffset(ILInstruction* inst, bool& isEmpty);

    // The C# `ConditionDetection.InvertIf` (the `internal static`): the
    // "invert if to match IL order / reduce nesting" operation.
    //   if (cond) { then (exits) }   falseCode...; exit
    // ->
    //   if (!cond) { falseCode...; exit }   then...
    // The C# reads `ifInst` as a non-terminal at `block.Instructions[i]` with
    // the `falseCode...; exit` as sibling instructions after it (the C#
    // `Block.Instructions` includes the control flow; `FinalInstruction` is a
    // Nop). This port makes the `IfInstruction` the block's `FinalInstruction`,
    // so the `falseCode...; exit` is the NEXT block in the container (the
    // fall-through), and the old then moves into that next block (the
    // "after the if" position). The next block must be single-predecessor (only
    // this block's fall-through) so the move is semantics-preserving -- the C#
    // has the falseCode in the same block as the if, so it is single-pred by
    // construction; this port checks `IncomingEdgeCount == 1` explicitly.
    // Assumes `ifInst` is `block`'s `FinalInstruction` with a null `FalseInst`
    // (no else) and an unreachable `TrueInst` (the C# `Debug.Assert`s).
    // `ExpressionTransforms.RunOnSingleStatement` (the C# re-visit that folds
    // `Comp.LogicNot`) is not ported; `NegateCondition` folds directly.
    static void InvertIf(Block* block, IfInstruction* ifInst);
};

} // namespace ILSpy::Decompiler::IL
