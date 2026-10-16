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

// Port of ICSharpCode.Decompiler/IL/Transforms/DeconstructionTransform.cs
// (subset): detects that a run of statements is a lowered deconstruction
// assignment -- rooted in a Deconstruct call -- and folds it into a single
// DeconstructInstruction:
//   call Deconstruct(target, ldloca out0, ...) [+ nested Deconstruct calls]
//   stloc conv0(conv(...)) ...
//   assignments ...
// =>
//   deconstruct { init: pattern: conversions: assignments: }
//
// Ported: TransformDeconstruction, MatchDeconstructionSequence (the escape
// retry loop + the leading-element check), MatchDeconstruction /
// MatchDeconstructionCall / MatchNestedDeconstructions (the defensive-copy
// arm + the BindsOnElementType guard), MatchConversions / MatchConversion,
// MatchAssignments / MatchAssignment (the forwarding + conversion-tail
// delayed actions), FindIndex, the C# IsConsumableByEnclosingDeconstruction
// with TryFindEnclosingDeconstructionCall, InlineDeconstructionInitializer
// (the FindLoadInNext Deconstruction arm + IsBefore), the Deconstruct
// match-pattern builder, and the delayedAction composition.
//
// Deferred with their surfaces (documented at the sites):
// - The tuple-designation arms (MatchNestedTupleDesignations /
//   ResolveTupleContainer / TupleNode / MatchTupleElementRead/Store /
//   BuildTuplePatternMatch): they need the port's deferred
//   TupleType.FromUnderlyingType + the tuple-element field parsing; the
//   Deconstruct-call-rooted patterns are complete.
// - The port's variable use-lists (the C# LoadInstructions /
//   AddressInstructions / StoreInstructions) are on-demand walks over the
//   tree (the ILVariable keeps only the counts), so the single-use
//   `is [var x]` shape checks in the C# map to count checks plus the
//   walk-derived sites.
// - The IsAssignment StObj arm's pointer-target element-type recovery (the
//   C# InferType-based ByReferenceType split) uses the store's own type.
// - The IL-range accumulation over the folded instructions (the C#
//   AddILRange calls) is a sequence-point detail the port leaves to the
//   surviving instruction's own range.

#pragma once

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/DeconstructInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

struct DeconstructionCall;
struct DeconstructionMatcher;

// The file-local-probe convention: the test hooks into the matcher entry
// (the C# test suite drives the transform through its public surface, but
// this port's matcher is file-local, so the probe re-exports it).
DeconstructionCall* MatchDeconstructionCallProbe(ILInstruction* inst,
                                                 ILInstruction*& testedOperand);

class DeconstructionTransform : public IStatementTransform {
public:
    ~DeconstructionTransform() override;

    void Run(Block& block, int pos, StatementTransformContext& context) override;

private:
    // The C# `bool TransformDeconstruction(Block, int pos)`.
    static bool TransformDeconstruction(DeconstructionMatcher& matcher,
                                        Block& block, int pos);
    // The C# `bool InlineDeconstructionInitializer(Block, int pos)`.
    static bool InlineDeconstructionInitializer(DeconstructionMatcher& matcher,
                                                Block& block, int pos);
    // The C# `bool IsConsumableByEnclosingDeconstruction(Block, int pos)` +
    // `static bool TryFindEnclosingDeconstructionCall(Block, int pos, out int)`.
    static bool IsConsumableByEnclosingDeconstruction(DeconstructionMatcher& matcher,
                                                      Block& block, int pos);
    static bool TryFindEnclosingDeconstructionCall(Block& block, int pos,
                                                   int& enclosingPos);
};

} // namespace ILSpy::Decompiler::IL