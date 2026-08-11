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

// Port of the EliminateRedundantTryFinally piece of ICSharpCode.Decompiler/IL/
// Transforms/ReduceNestingTransform.cs. The C# compiler sometimes generates a
// try-finally around a `fixed` statement; once DetectPinnedRegions has formed the
// PinnedRegion the try-finally is redundant (its finally is an empty
// `leave (nop)`) and is replaced with the PinnedRegion directly:
//   .try BlockContainer { Block { PinnedRegion ... } }           PinnedRegion ...
//   .finally BlockContainer { Block { leave IL_xxxx (nop) } }  ==>
// The other ReduceNestingTransform pieces (Visit / ReduceNesting /
// ReduceSwitchNesting / ImproveILOrdering / ExtractElseBlock) need a general
// ILInstruction.Clone for the keyword-exit duplication and stay deferred.

#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"

#include <functional>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `inst.MatchNop()` -- a Nop, or (in this port) a null slot (the reader
// emits `Leave(container)` with no Value for a void leave rather than a Nop).
bool IsNop(const ILInstruction* inst) {
    return !inst || inst->Op == OpCode::Nop;
}

// The C# `inst.MatchLeave(container)` -- a Leave targeting `container` whose
// Value is a Nop (a value-less leave of that container).
bool MatchLeave(ILInstruction* inst, const BlockContainer* container) {
    auto* leave = dynamic_cast<Leave*>(inst);
    return leave && leave->TargetContainer == container && IsNop(leave->Value.get());
}

// The C# BlockContainer.SingleInstruction: if the container has one block whose
// only instruction is a single instruction, return that instruction; otherwise
// return the block (or the container if it has multiple blocks). This port's
// block model splits the block's non-terminal `Instructions` from its
// `FinalInstruction` (the C# `Block.Instructions` includes the final), so the
// "single instruction" block is one with no non-terminals and a FinalInstruction.
ILInstruction* SingleInstruction(BlockContainer* c) {
    if (c->Blocks.size() != 1) return c;  // not a Leave -> MatchLeave fails
    Block* b = c->Blocks[0].get();
    if (b->Instructions.empty() && b->FinalInstruction) return b->FinalInstruction.get();
    return b;  // has non-terminals (or is degenerate) -> not a single Leave
}

// True if `tf` is a redundant try-finally around a PinnedRegion that can be
// eliminated. Non-mutating (the caller folds separately so the tree walk can
// find the first candidate safely).
bool CanEliminate(TryFinally* tf) {
    // Finally must be a single value-less Leave of the finally container.
    auto* finallyContainer = dynamic_cast<BlockContainer*>(tf->FinallyBlock.get());
    if (!finallyContainer) return false;
    if (!MatchLeave(SingleInstruction(finallyContainer), finallyContainer)) return false;

    // Try must be a single-block container.
    auto* tryContainer = dynamic_cast<BlockContainer*>(tf->TryBlock.get());
    if (!tryContainer || tryContainer->Blocks.size() != 1) return false;
    Block* tryBlock = tryContainer->Blocks[0].get();

    // The try block's non-terminal instructions must be exactly [PinnedRegion].
    // (The C# counts the final within Instructions; this port carries the final
    // separately, so the PinnedRegion is the sole non-terminal.)
    if (tryBlock->Instructions.size() != 1) return false;
    auto* pinned = dynamic_cast<PinnedRegion*>(tryBlock->Instructions[0].get());
    if (!pinned) return false;

    // The trailing instruction (C# Instructions[1]) is this port's
    // FinalInstruction. The C# Count==1 case has no trailing instruction
    // (a null final here); the C# Count==2 case requires a value-less Leave of
    // the try container. Any other final (a Branch, a Leave elsewhere) means
    // the try block has real trailing control flow and must not be collapsed.
    if (tryBlock->FinalInstruction) {
        if (!MatchLeave(tryBlock->FinalInstruction.get(), tryContainer)) return false;
    }
    return true;
}

// Fold `tf`: detach the PinnedRegion from the try block and replace the
// TryFinally with it. The TryFinally's whole subtree (including the now-empty
// try block and the empty finally container) is destroyed by ReplaceWith.
void Eliminate(TryFinally* tf) {
    auto* tryContainer = static_cast<BlockContainer*>(tf->TryBlock.get());
    Block* tryBlock = tryContainer->Blocks[0].get();
    // TakeChild orphans the PinnedRegion (clears Parent/ChildIndex) and leaves
    // a null in tryBlock->Instructions[0]; the null is destroyed with the
    // TryFinally subtree, so the transient empty slot never reaches
    // CheckInvariant.
    auto pinned = tryBlock->TakeChild(0);
    tf->ReplaceWith(std::move(pinned));
}

} // namespace

void ReduceNestingTransform::Run(ILFunction& function, ILTransformContext& context) {
    // The C# iterates `function.Descendants.OfType<TryFinally>()` and folds
    // each. This port has no GC: folding an outer TryFinally destroys any
    // TryFinallys nested in its try block, dangling their collected pointers.
    // Re-walk the tree for each fold instead (the redundant shape is rare, so
    // the bounded re-walks are cheap) -- the walk stops at the first candidate,
    // folds it, then the outer loop re-walks the mutated tree.
    bool changed = true;
    while (changed) {
        changed = false;
        TryFinally* target = nullptr;
        std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
            if (!inst || target) return;
            if (auto* tf = dynamic_cast<TryFinally*>(inst)) {
                if (CanEliminate(tf)) {
                    target = tf;
                    return;
                }
            }
            for (int i = 0; i < inst->ChildCount() && !target; ++i)
                walk(inst->GetChild(i));
        };
        walk(function.Body.get());
        if (target) {
            context.StepOnce("Removing try-finally around PinnedRegion");
            Eliminate(target);
            changed = true;
        }
    }
}

} // namespace ILSpy::Decompiler::IL
