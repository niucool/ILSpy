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

// Port of the static helper methods of
// ICSharpCode.Decompiler/IL/Transforms/HighLevelLoopTransform.cs that the
// switch-detection family (SwitchDetection.LoopContext) and the later while/for
// loop transform depend on. The full HighLevelLoopTransform (the while/for
// restructuring, MatchForLoop, GetIncrementBlock) is a larger slice and is
// deferred; this header exposes only the self-contained shape matchers that
// identify the back-edge blocks a C# `continue;` jumps to:
//
//   - MatchIncrement: stloc v(add(ldloc v, const)) -> the loop's increment
//     variable (the `v++` / `v += step` of a for loop). The C# also matches a
//     CompoundAssignmentInstruction (compound.assign(ldloca v, ..)); this port
//     does not yet model compound-assign nodes, so that arm is deferred.
//   - MatchIncrementBlock: a block whose final is a Branch to the loop head and
//     whose other instructions are all simple statements -- the for-loop
//     increment block, the target of a `continue;`.
//   - MatchDoWhileConditionBlock: a block whose final is an if with no else (a
//     do-while condition), branching to the loop head on true and falling
//     through to the exit on false -- also a `continue;` target.
//   - IsSimpleStatement: the statement kinds allowed in a for-loop increment
//     block (calls and assignments).
//
// Adapted to this port's block model: the IfInstruction is the block's
// FinalInstruction with an implicit fall-through to the next block in the
// container (the C# carries the if as a non-terminal at Instructions[Count-2]
// with an explicit fall-through Branch as the block's last instruction). So
// MatchDoWhileConditionBlock reads the if from block->FinalInstruction (not
// Instructions[Count-2]) and the fall-through from the next block in the
// container (not Instructions.Last()).

#pragma once

#include "Decompiler/IL/ILVariable.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class ILInstruction;
class ILVariable;
class ILFunction;
struct ILTransformContext;

// The static helper subset of HighLevelLoopTransform. The full transform (the
// while/for restructuring) is deferred; only the shape matchers LoopContext and
// the later while/for loop transform depend on are exposed here, as static
// methods so call sites read `HighLevelLoopTransform::MatchIncrementBlock(..)`
// as in the C#.
class HighLevelLoopTransform {
public:
    // Port of HighLevelLoopTransform.MatchIncrement: returns true and sets
    // `variable` when `inst` is `stloc v(add(ldloc v, ..))` -- a numeric loop
    // increment. The CompoundAssignmentInstruction arm is deferred (no node).
    static bool MatchIncrement(ILInstruction* inst, ILVariablePtr& variable);

    // Port of HighLevelLoopTransform.MatchIncrementBlock: returns true and sets
    // `loopHead` when the block's final instruction is a Branch to `loopHead` and
    // every other (non-final) instruction is a simple statement. Adapted: the
    // branch is the block's FinalInstruction (the C# reads Instructions.Last()).
    static bool MatchIncrementBlock(Block* block, Block*& loopHead);

    // Port of HighLevelLoopTransform.MatchDoWhileConditionBlock: returns true
    // and sets `target1`/`target2` when the block's final is an if with no else
    // whose true arm is a Branch (or a return) and whose fall-through is the next
    // block in the container (or a return). target1 is the if's true-branch
    // target; the fall-through (target2) is the next block in the container.
    // Adapted: the if is block->FinalInstruction (the C# reads
    // Instructions[Count-2]); the fall-through is the next block in the container
    // (the C# reads Instructions.Last()).
    static bool MatchDoWhileConditionBlock(Block* block, Block*& target1, Block*& target2);

    // Port of HighLevelLoopTransform.IsSimpleStatement: a call or assignment.
    // In this port call/callvirt/newobj all emit a Call node, and the compound-
    // assign nodes are not modeled, so only Call/StLoc/StObj are recognized.
    static bool IsSimpleStatement(ILInstruction* inst);

    // Port of HighLevelLoopTransform.Run (MatchWhileLoop subset): for each Loop
    // container whose entry point's first instruction is `if (cond) leave loop`
    // (the while-condition break -- break when cond is true => `while (!cond)`),
    // transform it into a While container: negate the condition, the leave
    // becomes the false arm (break), a branch to the body becomes the true arm,
// and the rest of the entry point is extracted into a body block. MatchForLoop
// and MatchDoWhileLoop are deferred. Adapted to the if-as-final block model
// (the if is the entry point's FinalInstruction, not Instructions[Count-2]).
    static void Run(ILFunction& function, ILTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
