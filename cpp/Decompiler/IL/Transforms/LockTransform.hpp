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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/LockTransform.cs (subset).
// Detects the C# `lock` statement's Monitor.Enter/Exit try/finally pattern and
// folds it into a LockInstruction (`lock (expr) { body }`).
//
// Two families are ported:
//   - The *no-flag* shapes (MCS / V2): a straight `call Exit` finally (no
//     `if (flag)` guard). `stloc lockObj(lockExpression); call Enter(ldloc
//     lockObj); .try { ... } finally { call Exit(ldloc lockObj); leave }` ->
//     `lock (lockExpression) { ... }` (MCS); V2 is the temp variant. These
//     fire only on mono-compiled assemblies.
//   - The *flag-based* Roslyn shape (the dominant .NET Framework 4 / Roslyn
//     codegen): `stloc obj(lockExpression); stloc flag(ldc.i4 0); .try { call
//     Enter(ldloc obj, ldloca flag); body } finally { if (!flag) leave; call
//     Exit(ldloc obj); leave }` -> `lock (lockExpression) { body }`.
//
// Block-model adaptation (flag-based): the C# carries the finally's `if (flag)
// { Exit }` and the endfinally `leave` as two non-terminals of ONE block
// (Instructions[0]/[1]); this port's if-as-final model and ConditionDetection
// leave the brfalse-skip as a separate first block, so the flag finally is TWO
// blocks -- `if (comp(eq,flag,0)) leave` then `call Exit; leave` -- and
// MatchExitBlockFlag matches that shape (the C# single-block `if(flag){Exit}`
// shape does not arise here). The stloc obj / stloc flag sit either in the
// TryFinally's own block (the C# same-block indexing) or, when CFS did not
// merge the EH wrapper with the preceding block (the dominant mscorlib case:
// the fall-through branch into the TryFinally resolves to the try entry inside
// the try container, not the wrapper, so the wrapper has IncomingEdgeCount==0
// and CFS leaves the stlocs in a separate preceding block), in the preceding
// block in the same container. The preceding-block fold absorbs the
// TryFinally's block final into the preceding block and drops the now-empty
// TryFinally block (the preceding block's `br` into the try would dangle into
// the lock body once the TryFinally becomes a LockInstruction, so it is
// discarded -- the LockInstruction subsumes the try entry); the dropped block
// is moved to a graveyard vector so the container iteration stays valid.
//
// Deferred vs the C#: the V4 / V4YieldReturn flag shapes (which pass an inline
// `stloc obj(lockExpression)` as the Enter call's first argument rather than a
// separate `stloc obj`), and the single-block `if(flag){Exit}` finally shape;
// neither appears in the .NET Framework 4 mscorlib corpus (0 inline-stloc Enter
// args, 0 single-block flag finallys across ~600 lock methods), so the hand-
// built tests cover the Roslyn shape and the mscorlib sweep asserts the fold
// count.
//
// General block-model notes: the C# carries the TryFinally as a non-terminal
// at block.Instructions[i] with the stloc/call at [i-2]/[i-1]; after the pre-
// pipeline's CFS merges the EH wrapper block (TryFinally alone) with the
// preceding block (stloc + call + br wrapper), the no-flag shapes have the
// same shape here -- TryFinally at Instructions[i], call at [i-1], stloc at
// [i-2]. The finally's endfinally `leave` is this port's FinalInstruction (the
// C# carries it as a non-terminal at Instructions[1]), so MatchExitBlockNoFlag
// checks Instructions[0] = call Exit and the FinalInstruction = leave(finally
// container). SortBlocks(deleteUnreachableBlocks) is unsafe in this port
// (D58); the no-flag rewrite does not delete blocks (it only replaces the
// TryFinally with a LockInstruction and drops two non-terminal stores), and
// the flag rewrite moves the dropped block to a graveyard rather than erasing
// it in place.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class LockTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
