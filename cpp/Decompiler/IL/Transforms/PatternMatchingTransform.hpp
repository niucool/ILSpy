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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/PatternMatchingTransform.cs.
// Detects the C# 7.0 `is` patterns the Roslyn compiler emits for `expr is T x`
// (a type test plus a variable capture) and rewrites the isinst + null-test
// block tail into a single MatchInstruction condition:
//
//   if (match.type[T].notnull(V = testedOperand)) br matchedBlock; br nullBlock
//
// Two shapes are recognised (matching the C#):
//   - PatternMatchValueTypes: `if (isinst T(x) == null) br falseBlock; br unboxBlock`
//     where unboxBlock is `stloc V(unbox.any T(x))` -- folds to a MatchInstruction
//     whose true arm is the unboxBlock (the captured value flows into V there).
//   - PatternMatchRefTypes: `stloc V(isinst T(x)); if (V == null) br falseBlock;
//     br trueBlock` -- folds to a MatchInstruction whose true arm is trueBlock.
//
// The transform runs after LoopDetection and before ConditionDetection (per
// GetILTransforms()), so ifs are still block finals with positional fall-through
// (ConditionDetection has not added else arms). Adapted to this port's
// if-as-final block model: the IfInstruction is the block's FinalInstruction
// (not a non-terminal at Instructions[Count-2]), and the false arm is the next
// block in the container (positional fall-through, not an explicit Branch the
// block owns) -- so the swap of the two arms on a `== null` / logic.not condition
// is done by tracking the two target blocks and reconstructing the if, keeping
// the fall-through positional when the null path is the next block and
// materialising an explicit FalseInst otherwise.
//
// Deferred vs the C#: the recursive sub-pattern detection
// (DetectPropertySubPatterns / DetectPropertySubPattern / MatchNullCheckPattern
// / MatchNullableHasValueCheckPattern) needs DetectExitPoints.CompatibleExitInstruction
// (ExitPoints.cs, deferred) and PropertyOrFieldAccess (IMember/IField accessor
// metadata). It is skipped here, so the top-level `is T` / `is T x` pattern is
// produced without the recursive property patterns (`expr is C { P: var x }`);
// the MatchInstruction node and SubPatterns slot already support them when the
// helper lands. CheckAllUsesDominatedBy is implemented with a tree walk
// (collect every use of the variable) rather than the C#'s per-variable
// LoadInstructions/AddressInstructions/StoreInstructions lists this port does
// not maintain. The container's empty-block sweep (container.Blocks.RemoveAll)
// is skipped: removing a block with empty Instructions but a live FinalInstruction
// would dangle branches targeting it (the D58 delete-unreachable hazard), so
// the dead blocks stay (harmless). Gated on the PatternMatching setting
// (default true).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class PatternMatchingTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
