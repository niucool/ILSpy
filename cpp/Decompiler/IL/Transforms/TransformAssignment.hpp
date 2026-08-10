// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformAssignment.cs
// (foundation subset). The C# TransformAssignment is an IStatementTransform (a
// child of the GetILTransforms() StatementTransform, after ILInlining /
// ExpressionTransforms / DynamicIsEventAssignmentTransform) that rewrites
// inline-assignment and compound-assignment patterns into the cleaner ILAst
// forms -- the NumericCompoundAssign (`V op= rhs`) / UserDefinedCompoundAssign
// nodes the D125 foundation ported, and the inline-assignment Block shape. Its
// Run() dispatches to TransformInlineAssignmentStObjOrCall /
// TransformInlineAssignmentLocal (gated on MakeAssignmentExpressions) and
// TransformPostIncDecOperatorWithInlineStore / TransformPostIncDecOperator /
// TransformPreIncDecOperatorWithInlineStore (gated on
// IntroduceIncrementAndDecrement), each of which consults the shared
// IsCompoundStore / IsMatchingCompoundLoad / UnwrapSmallIntegerConv /
// ValidateCompoundAssign helpers + RecombineVariables.
//
// This iteration ports the self-contained UnwrapSmallIntegerConv helper as a
// tested-but-not-yet-wired foundation, ahead of the full transform (the
// established MatchInstruction / UsingInstruction / NullCoalescingInstruction /
// NumericCompoundAssign precedent). The helper the compound-assignment folds
// consult to peel the compiler's `conv` truncation to a small integer that a
// compound assign to a small-integer local/field carries:
//   stloc V(conv.i1(binary.add(ldloc V, ldc.i4 1)))
// the `conv.i1` (a Truncate to a small-integer TargetType) is the implicit
// truncation C# performs on the small-integer store; UnwrapSmallIntegerConv
// returns the conv's Argument (the binary) so the transform can build the
// NumericCompoundAssign from the binary + record the conv for ValidateCompoundAssign.
//
// The full TransformAssignment (IsCompoundStore / IsMatchingCompoundLoad /
// ValidateCompoundAssign + the BNI Sign/input-type reconciliation + the
// RecombineVariables finalizeMatch + the per-statement Run wiring) is a larger
// slice and a subsequent iteration.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

namespace ILSpy::Decompiler::IL {

class Conv;

// Port of TransformAssignment.UnwrapSmallIntegerConv. For a compound assignment
// to a small integer, the compiler emits a `conv` (a Truncate to a
// small-integer TargetType) wrapping the binary; this peels that conv and
// reports it via `conv` so the caller can pass it to ValidateCompoundAssign.
// Returns `inst` unchanged (with `conv` set to nullptr) when `inst` is not such
// a conv -- the non-truncating / non-small-integer-target case. `inst` is a
// non-owning view (the caller holds the ownership); the returned pointer is
// either `inst` itself or `inst`'s Argument child (still owned by `inst`).
ILInstruction* UnwrapSmallIntegerConv(ILInstruction* inst, Conv*& conv);

} // namespace ILSpy::Decompiler::IL
