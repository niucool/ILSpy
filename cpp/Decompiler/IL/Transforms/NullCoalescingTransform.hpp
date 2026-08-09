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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/NullCoalescingTransform.cs
// (subset). The C# NullCoalescingTransform is an IStatementTransform (a child of
// the GetILTransforms() StatementTransform, after ILInlining /
// ExpressionTransforms / DynamicIsEventAssignmentTransform / TransformAssignment)
// that constructs NullCoalescingInstructions (`if.notnull(a, b)`, the C# `??`
// operator) from `if.notnull` block tails -- the reference-type `??` pattern:
//   stloc s(value)
//   if (comp(ldloc s == ldnull)) { stloc s(fallback) }
//   =>
//   stloc s(if.notnull(value, fallback))
// (then ILInlining folds the single-use `s` into its load). The Nullable<T> `??`
// is handled by NullableLiftingTransform (deferred). This iteration ports the
// TransformRefTypes subset -- the simple case, the temp-variable case, and the
// throw-expression case (the C# 7.0 `a ?? throw ...` form, gated on the
// ThrowExpressions setting, which mutates the Throw's resultType to O so the
// NullCoalescingInstruction's ResultType matches the reference-type value).
// TransformHoistedConstructorArgumentNullGuard and TransformThrowExpression-
// ValueTypes are deferred: they need ILFunction.Method metadata
// (IsConstructor/IsStatic) + ILInlining.IsInConstructorInitializer +
// ILInlining.FindLoadInNext with a movable expression + MatchLogicNot /
// MatchHasValueCall wiring for the value-types case.
//
// Adapted to this port's if-as-final block model: the C# carries the if as a
// non-terminal at `block.Instructions[pos+1]` and removes it via
// `block.Instructions.RemoveAt(pos+1)`; this port makes the IfInstruction the
// block's FinalInstruction, so the if is `block.FinalInstruction` (not
// Instructions[pos+1]) and "removing" it means replacing the if-final with a
// Branch to the next block (the positional fall-through the if's null FalseInst
// represented -- the `??` pattern has no else; the else is the fall-through to
// the use). The block model requires a final, so the Branch is the explicit
// fall-through. The fold requires a next block (the use block); a `??` at the
// very end of a function (no next block) is degenerate and left as-is.
//
// The .NET Framework 4 legacy-csc mscorlib corpus emits no reference-type `??`
// pattern (a corpus probe across 8000 methods found 1797 `comp(eq, ldloc X,
// ldnull)` null-check ifs and 513 `comp(ne, ..)` but zero whose true/false arm
// is a StLoc to the same variable), so this transform fires 0 times on it -- the
// `??` operator's reference-type lowering is a Roslyn-era codegen pattern that
// fires on modern .NET. Ported for faithfulness (matching the DetectCatchWhen-
// ConditionBlocks / LdLocaDupInitObj / SwitchOnNullable precedent): the
// hand-built tests verify the rewrite and the mscorlib sweep verifies the ILAst
// invariant holds across the corpus.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class NullCoalescingTransform : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;

private:
    // TransformRefTypes: the reference-type `??` pattern
    //   stloc s(value); if (comp(ldloc s == ldnull)) { stloc s(fallback) }
    //   => stloc s(if.notnull(value, fallback))
    // (plus the temp-variable variant and the throw-expression `a ?? throw ...`
    // variant, gated on the ThrowExpressions setting). Adapted to the if-as-
    // final block model. Returns true if a fold fired.
    bool TransformRefTypes(Block& block, int pos, StatementTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
