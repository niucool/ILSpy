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

// Port of ICSharpCode.Decompiler/IL/Transforms/IndexRangeTransform.cs -- the
// C# 8 System.Index / System.Range feature recovery. The transform matches
//   stloc len(get_Length(ldloc container))          (optional)
//   stloc range(rangeVarInit)                       (optional, `var r = ..`)
//   stloc offset(GetOffset / sub(len, i))           (the start offset)
//   complex_expr(get_Item(container, offset) | Slice(container, off, len))
// and rewrites the access into a SyntheticRangeIndexAccessor call
// (`container[System.Index]` / `container[Range]`) for the C# renderer.
//
// Ported: HandleLdElema (the `array[^i]` / `array[GetOffset]` ldelema case),
// the Run driver with TransformIndexing / TransformSlicing, MatchGetOffset,
// MatchSliceLength, MatchContainerLengthStore / MatchContainerLength /
// MatchContainerVar, IsSlicingMethod, CheckContainerLengthVariableUseCount,
// MatchIndexImplicitConv, MatchIndexFromRange, MakeIndex / MakeRange, the
// IndexMethods member scan, and CSharpWillGenerateIndexer. The C# local
// functions (closing over startPos / the match state) port to an explicit
// state struct with member functions.
//
// Deferred with its surface: ExtendSlicing (the second-pass extension that
// merges a previously-built partial `newobj Range(...)` pattern into a full
// range expression -- it needs the Ancestors walk over the container-length
// load sites and re-runs on the already-rewritten statements; the single-pass
// indexing/slicing patterns are complete). The IL-range accumulation
// (AddILRange of the folded instructions) is a sequence-point detail the port
// folds by leaving the surviving instruction's own range untouched.

#pragma once

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class LdElema;

class IndexRangeTransform : public IStatementTransform {
public:
    // The C# `static bool HandleLdElema(LdElema ldelema, ILTransformContext
    // context)` (called by the expression transforms for `array[System.Index]`).
    // The port takes the settings through the context (the port's
    // ILTransformContext carries the settings + the type system).
    static bool HandleLdElema(LdElema& ldelema, ILTransformContext& context);

    void Run(Block& block, int pos, StatementTransformContext& context) override;

    // The C# `static bool IsSlicingMethod(IMethod method)` (used by both the
    // Run driver and the CallBuilder's Slice rendering probes).
    static bool IsSlicingMethod(const TypeSystem::IMethod* method);

private:
    // The C# local functions `TransformIndexing()` / `TransformSlicing(bool)`
    // inside Run. The shared state (the captured variables of the C# local
    // functions) is the IndexRangeState struct defined in the .cpp.
    void TransformIndexing(struct IndexRangeState& state);
    void ExtendSlicing(struct IndexRangeState& state);
    void TransformSlicing(struct IndexRangeState& state,
                          bool sliceLengthWasMisdetectedAsStartOffset = false);
};

} // namespace ILSpy::Decompiler::IL