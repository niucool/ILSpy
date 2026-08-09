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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <functional>

namespace ILSpy::Decompiler::IL {

namespace {

void Walk(ILInstruction* inst, const std::function<void(Block*)>& visit) {
    if (!inst) return;
    if (inst->Op == OpCode::Block) visit(static_cast<Block*>(inst));
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

void StatementTransform::Run(ILFunction& function, ILTransformContext& context) {
    if (children_.empty()) return;

    // The per-statement ILInlining reads v->StoreCount/LoadCount/AddressCount to
    // decide VariableCanBeUsedForInlining; the intervening transforms
    // (ConditionDetection / Lock / Using / CachedDelegate / CachedReadOnlySpan)
    // may have changed them since the last ComputeVariableUsage, so recompute so
    // the per-statement children read fresh counts (the C# maintains these
    // incrementally).
    ComputeVariableUsage(function);

    Walk(function.Body.get(), [&](Block* block) { RunBlock(block, context); });

    // Recompute so the next transform (AssignVariableNames) reads fresh counts
    // after the per-statement inlining removed stores/loads.
    ComputeVariableUsage(function);
}

void StatementTransform::RunBlock(Block* block, ILTransformContext& context) {
    if (!block) return;

    StatementTransformContext ctx(context, block);
    // The C# driver starts at IndexOfFirstAlreadyTransformedInstruction - 1; for
    // a freshly-entered block that is the last instruction. This port's block
    // model carries the final separately (FinalInstruction, not in
    // Instructions), so the per-statement positions range over the
    // non-terminal Instructions only (the C# ranges over Instructions including
    // the final, but the final is never a StLoc, so the first iteration at the
    // final is a no-op -- starting at the last non-terminal is equivalent).
    int pos = static_cast<int>(block->Instructions.size()) - 1;
    while (pos >= 0) {
        if (ctx.HasRerunPosition()) {
            // A child requested a rerun at an earlier position; jump back. The
            // children only ever request a position <= the current pos (the C#
            // invariant `rerunPosition >= pos`), so this never skips an
            // unprocessed statement.
            pos = ctx.RerunPosition();
            ctx.ClearRerunPosition();
        }
        for (auto& child : children_) {
            child->Run(*block, pos, ctx);
            if (ctx.HasRerunCurrentPosition()) {
                ctx.ClearRerunCurrentPosition();
                ctx.RequestRerun(pos);
            }
            if (ctx.HasRerunPosition()) break;
        }
        if (!ctx.HasRerunPosition()) --pos;
    }
}

} // namespace ILSpy::Decompiler::IL
