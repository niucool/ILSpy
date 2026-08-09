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

// Port of ICSharpCode.Decompiler/IL/Transforms/ILInlining.cs (subset). Inlines
// single-use StLoc values into their single load site and removes dead pure
// stores. The third transform in GetILTransforms(), after SplitVariables (which
// the port defers until the reaching-definitions dataflow lands; this subset
// works on the per-variable usage counts ComputeVariableUsage provides).
//
// The C# ILInlining implements three interfaces: IILTransform (whole-function),
// IBlockTransform (per-block), and IStatementTransform (per-statement, the first
// child of the GetILTransforms() StatementTransform). This port implements the
// IILTransform entry (the early whole-function pass) and the IStatementTransform
// entry (the second inlining pass the StatementTransform runs interleaved with
// the other per-statement transforms); the IBlockTransform entry is not wired
// (the port models BlockILTransform post-order transforms as IILTransform).
// The IStatementTransform Run loops InlineOneIfPossible at the given position
// until no change (the C# per-statement overload); the AllowInliningOfLdloca
// option (the ldloca-into-addressof path the C# second pass enables, which needs
// an AddressOf node + IsGeneratedTemporaryForAddressOf + ClassifyExpression) is
// deferred to a later iteration.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class ILInlining : public IILTransform, public IStatementTransform {
public:
    // IILTransform: the whole-function inlining pass (runs early in the
    // pipeline, before InlineReturnTransform).
    void Run(ILFunction& function, ILTransformContext& context) override;
    // IStatementTransform: the per-statement inlining pass (the first child of
    // the StatementTransform). Loops InlineOneIfPossible at `pos` until no
    // change, mirroring the C# ILInlining.Run(Block, pos, ctx) overload.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
