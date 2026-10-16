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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/InterpolatedStringTransform.cs.
// The C# InterpolatedStringTransform is the last per-statement child of the
// GetILTransforms() StatementTransform (after ILInlining / ExpressionTransforms
// / TransformAssignment / NullCoalescingTransform / NullableLiftingStatement-
// Transform / NullPropagationStatementTransform / [the deferred array/collection/
// expression-tree/index-range/deconstruction/named-argument/unconstrained-generic
// children] / UserDefinedLogicTransform). It folds the C# 10/.NET 6
// `$"..."` lowering via DefaultInterpolatedStringHandler:
//
//   stloc v(newobj DefaultInterpolatedStringHandler..ctor(ldc.i4 literalLength,
//                                                         ldc.i4 formattedCount))
//   call AppendLiteral(ldloca v, ldstr "literal")
//   call AppendFormatted(ldloca v, expr)           // or with alignment / format
//   ...
//   call ToStringAndClear(ldloca v)                // yields the string
//
// into a single Block(Kind=InterpolatedString) holding the stloc + Append calls
// with the ToStringAndClear call as its FinalInstruction, so the back end can
// render it as `$"literal{expr}..."`. The handler variable v is promoted to
// VariableKind::InitializerTarget.
//
// Adapted to this port's block model identically to the C#: the stloc + Append
// calls are non-terminal Instructions of the host block, and the ToStringAndClear
// call is the first instruction after them (block.Instructions[interpolationEnd]);
// FindLoadInNext locates the `ldloca v` inside it (its Parent is the call), the
// call is detached and becomes the replacement block's FinalInstruction, and the
// replacement block takes the call's slot. The stloc + Append calls are moved
// into the replacement block and erased from the host. No if-as-final block-model
// divergence -- the pattern is a flat instruction sequence, not an if/Block tail.
//
// Gated on the StringInterpolation setting (default true). DefaultInterpolated-
// StringHandler is a .NET 6+ type, so the fold fires 0 times on the .NET Framework
// 4 legacy-csc mscorlib corpus (the handler-construction codegen is absent from
// it); it fires on Roslyn-compiled / modern .NET. Ported for faithfulness,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNull-
// able / NullCoalescingTransform precedent: the hand-built tests verify the
// rewrite and the mscorlib sweep verifies the ILAst invariant holds across the
// corpus (not a fold count).

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class InterpolatedStringTransform : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
