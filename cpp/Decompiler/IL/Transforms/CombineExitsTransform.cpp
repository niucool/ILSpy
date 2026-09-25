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

#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `Block.Unwrap(inst)`: a Block with a single instruction and a Nop
// final unwraps to that instruction. In this port's block model a
// single-instruction expression block has exactly one entry in Instructions and
// no FinalInstruction; a bare non-Block instruction passes through.
ILInstruction* BlockUnwrap(ILInstruction* inst)
{
    if (inst == nullptr)
        return inst;
    if (auto* b = dynamic_cast<Block*>(inst))
    {
        if (b->Instructions.size() == 1 && b->FinalInstruction == nullptr)
            return b->Instructions[0].get();
    }
    return inst;
}

// The C# `Leave.IsLeavingFunction`: the target container's parent is the
// ILFunction. A file-local copy (CombineExitsTransform links only against the
// transforms it invokes; the other consumers carry their own copies).
bool IsLeavingFunction(const Leave* leave)
{
    return leave != nullptr && leave->TargetContainer != nullptr
           && leave->TargetContainer->Parent != nullptr
           && leave->TargetContainer->Parent->Op == OpCode::ILFunction;
}

} // namespace

void CombineExitsTransform::Run(ILFunction& function, ILTransformContext& context)
{
    auto* container = dynamic_cast<BlockContainer*>(function.Body.get());
    if (container == nullptr || container->Blocks.size() != 1)
        return;
    Leave* combinedExit = CombineExits(container->EntryPoint());
    if (combinedExit == nullptr)
        return;
    // The C# folds the freshly-built conditional (e.g. a `comp(x != 0)` condition
    // into `x`). The combined leave is the block's final in this port.
    ExpressionTransforms::RunOnSingleStatement(combinedExit, context);
}

Leave* CombineExitsTransform::CombineExits(Block* block)
{
    if (block == nullptr)
        return nullptr;
    // Block-model adaptation: the C# reads the if and the following leave from
    // Instructions (if second-to-last, leave last). Here the if is the last
    // non-final instruction and the leave is the final.
    if (block->Instructions.empty())
        return nullptr;
    auto* ifInst = dynamic_cast<IfInstruction*>(block->Instructions.back().get());
    auto* leaveElse = dynamic_cast<Leave*>(block->FinalInstruction.get());
    if (ifInst == nullptr || leaveElse == nullptr)
        return nullptr;
    // A non-empty false arm is an else branch, not an unconditional exit.
    if (ifInst->FalseInst != nullptr && !MatchNop(ifInst->FalseInst.get()))
        return nullptr;
    // Try to unwrap the true branch to a single instruction; if it is itself a
    // `if (...) leave(a); leave(b)` block, fold that nested block first.
    ILInstruction* trueInstruction = BlockUnwrap(ifInst->TrueInst.get());
    if (auto* nestedBlock = dynamic_cast<Block*>(trueInstruction))
        trueInstruction = CombineExits(nestedBlock);
    auto* leave = dynamic_cast<Leave*>(trueInstruction);
    if (leave == nullptr)
        return nullptr;
    if (!IsLeavingFunction(leave) || !IsLeavingFunction(leaveElse))
        return nullptr;
    if (leave->Value == nullptr || MatchNop(leave->Value.get())
        || leaveElse->Value == nullptr || MatchNop(leaveElse->Value.get()))
        return nullptr;
    // if (cond) { leave (value) } leave (elseValue)
    //   => leave (if (cond) value else elseValue)
    // The condition and both values are moved into the new nodes before the old
    // if (and any nested block that owns `leave`) is destroyed.
    BlockContainer* target = leave->TargetContainer;
    auto condition = std::move(ifInst->Condition);
    auto trueValue = std::move(leave->Value);
    auto elseValue = std::move(leaveElse->Value);
    auto combined = std::make_unique<Leave>(
        target, std::make_unique<IfInstruction>(std::move(condition),
                                                 std::move(trueValue),
                                                 std::move(elseValue)));
    Leave* result = combined.get();
    // Drop the if (destroying the now-empty nested block arm) and install the
    // combined leave as the block's final.
    block->Instructions.pop_back();
    block->SetFinal(std::move(combined));
    return result;
}

} // namespace ILSpy::Decompiler::IL
