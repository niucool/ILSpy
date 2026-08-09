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
// This iteration ports the two *no-flag* shapes that have a straight
// `call Exit` finally (no `if (flag)` guard):
//   - TransformLockMCS (the mono/mcs shape): `stloc lockObj(lockExpression);
//     call Enter(ldloc lockObj); .try { ... } finally { call Exit(ldloc
//     lockObj); leave }` -> `lock (lockExpression) { ... }`.
//   - TransformLockV2 (the legacy mono shape with a temp): `stloc
//     lockObj(ldloc tempVar); call Enter(ldloc tempVar); .try { ... } finally
//     { call Exit(ldloc lockObj); leave }` -> `lock (ldloc tempVar) { ... }`.
//
// Deferred vs the C#: the three *flag-based* shapes (TransformLockV4 /
// TransformLockV4YieldReturn / TransformLockRoslyn), which guard the Exit call
// with `if (ldloc flag) { call Exit }` in the finally. In this port's
// if-as-final block model the finally's `if (flag) { Exit }` is the block's
// FinalInstruction (not a non-terminal at Instructions[0]) and the trailing
// endfinally `leave` is inlined into the if's FalseInst by ConditionDetection
// (or is the next block); matching that shape is a separate adaptation and
// lands in a later iteration. The flag-based shapes are the ones the modern
// Roslyn and the .NET Framework 4 (legacy csc) compilers emit, so they fire on
// the real corpus; the no-flag MCS/V2 shapes fire only on mono-compiled
// assemblies, so this subset's mscorlib sweep asserts the invariant holds (not a
// fold count), matching the LdLocaDupInitObj / DetectCatchWhenConditionBlocks
// precedent.
//
// Adapted to this port's block model: the C# carries the TryFinally as a
// non-terminal at block.Instructions[i] with the stloc/call at [i-2]/[i-1];
// after the pre-pipeline's CFS merges the EH wrapper block (TryFinally alone)
// with the preceding block (stloc + call + br wrapper), this port has the same
// shape -- TryFinally at Instructions[i], call at [i-1], stloc at [i-2]. The
// finally's endfinally `leave` is this port's FinalInstruction (the C# carries
// it as a non-terminal at Instructions[1]), so MatchExitBlock checks
// Instructions[0] = call Exit and the FinalInstruction = leave(finally
// container) instead of Instructions[1]. SortBlocks(deleteUnreachableBlocks)
// is unsafe in this port (D58), but the rewrite does not delete blocks (it only
// replaces the TryFinally with a LockInstruction and drops two non-terminal
// stores), so no block deletion is needed.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class LockTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
