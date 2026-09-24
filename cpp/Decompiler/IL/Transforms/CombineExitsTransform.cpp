// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `Block.Unwrap(ifInst.TrueInst)` -- unwrap single-instruction blocks
// so the true-branch probe sees through the `if (...) { ... }` nesting. The
// C# Unwrap loops; one level is what the CombineExits shape needs (the nested
// `if` inside a block), but the loop keeps the semantics exact.
ILInstruction* UnwrapTrueBranch(ILInstruction* inst) {
    while (auto* block = dynamic_cast<Block*>(inst)) {
        if (block->ChildCount() != 1) return block;
        inst = block->GetChild(0);
    }
    return inst;
}

// The C# `inst.MatchNop()` -- a Nop node, or (this port's null-slot
// convention) a null child.
bool MatchNop(const ILInstruction* inst) {
    return inst == nullptr || dynamic_cast<const Nop*>(inst) != nullptr;
}

} // namespace

Leave* CombineExitsTransform::CombineExits(Block* block,
                                           BlockContainer* functionBody) {

    if (block == nullptr || functionBody == nullptr) return nullptr;
    const int count = block->ChildCount();
    if (count < 2) return nullptr;
    auto* ifInst =
        dynamic_cast<IfInstruction*>(block->GetChild(count - 2));
    auto* leaveElse = dynamic_cast<Leave*>(block->GetChild(count - 1));
    if (ifInst == nullptr || leaveElse == nullptr) return nullptr;
    if (!MatchNop(ifInst->FalseInst.get())) return nullptr;

    // Unwrap the true branch to a single instruction; a two-instruction
    // nested block is first combined recursively (the C# nested-shape case:
    // an inner if/leave pair folds before the outer probe continues).
    ILInstruction* trueInstruction = UnwrapTrueBranch(ifInst->TrueInst.get());
    if (auto* nestedBlock = dynamic_cast<Block*>(trueInstruction)) {
        if (nestedBlock->ChildCount() == 2)
            trueInstruction = CombineExits(nestedBlock, functionBody);
    }
    auto* leave = dynamic_cast<Leave*>(trueInstruction);
    if (leave == nullptr) return nullptr;
    if (leave->TargetContainer != functionBody ||
        leaveElse->TargetContainer != functionBody) {
        return nullptr;
    }
    if (MatchNop(leave->Value.get()) || MatchNop(leaveElse->Value.get()))
        return nullptr;

    // if (cond) { leave(value); }  leave(elseValue);
    // =>  leave(if (cond) value else elseValue)
    // The children move out of the old nodes (the C# passes the references
    // to the new IfInstruction; the old nodes die in the ReplaceWith below).
    auto value = std::make_unique<IfInstruction>(
        std::move(ifInst->Condition), std::move(leave->Value),
        std::move(leaveElse->Value));
    value->AddILRange(ifInst->StartILOffset, ifInst->EndILOffset);
    auto combinedLeave =
        std::make_unique<Leave>(leaveElse->TargetContainer, std::move(value));
    combinedLeave->AddILRange(leaveElse->StartILOffset,
                              leaveElse->EndILOffset);
    combinedLeave->AddILRange(leave->StartILOffset, leave->EndILOffset);
    Leave* combinedPtr = combinedLeave.get();

    // The combined leave takes the if's slot; the old trailing leave's slot
    // is removed (the C# RemoveAt(combinedLeave.ChildIndex + 1) -- the port
    // clears the final slot or erases from the instruction list, depending
    // on where the old leave lived).
    const bool elseWasFinal =
        leaveElse->Parent == block &&
        leaveElse == static_cast<ILInstruction*>(block->FinalInstruction.get());
    ifInst->ReplaceWith(std::move(combinedLeave));
    if (elseWasFinal) {
        block->SetFinal(nullptr);
    } else {
        block->RemoveInstructionAt(
            static_cast<std::size_t>(leaveElse->ChildIndex));
    }
    return combinedPtr;
}

void CombineExitsTransform::Run(ILFunction& function,
                                ILTransformContext& context) {
    auto* container = dynamic_cast<BlockContainer*>(function.Body.get());
    if (container == nullptr || container->Blocks.size() != 1) return;
    Leave* combinedExit = CombineExits(container->EntryPoint(), container);
    if (combinedExit == nullptr) return;
    // The C# tail `ExpressionTransforms.RunOnSingleStatement(combinedExit,
    // context)` re-runs the per-statement expression driver over the folded
    // leave. The port's per-statement driver lives inside StatementTransform
    // (no standalone statement-entry yet), so the polish pass is deferred
    // with it -- the fold itself is complete.
    context.StepOnce("CombineExits");
    (void)combinedExit;
}

} // namespace ILSpy::Decompiler::IL
