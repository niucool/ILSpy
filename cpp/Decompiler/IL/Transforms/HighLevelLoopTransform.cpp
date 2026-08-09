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

// Implementation of the HighLevelLoopTransform static helpers. See the header
// for the block-model adaptation notes.

#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

// The block that follows `block` in its container (the implicit fall-through
// target of a non-EndPointUnreachable final), or nullptr when `block` is the
// last block. Mirrors SwitchAnalysis's local helper.
static Block* NextBlockInContainer(Block* block) {
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
        }
    }
    return nullptr;
}

// Whether `leave` exits the function body (Port of Leave.IsLeavingFunction):
// the target container's parent is the ILFunction. Used to recognize a `return`
// where the C# MatchReturn would.
static bool IsLeavingFunction(Leave* leave) {
    return leave && leave->TargetContainer && leave->TargetContainer->Parent &&
           leave->TargetContainer->Parent->Op == OpCode::ILFunction;
}

bool HighLevelLoopTransform::IsSimpleStatement(ILInstruction* inst) {
    if (!inst) return false;
    // call/callvirt/newobj all emit a Call node in this port; the compound-assign
    // opcodes (NumericCompoundAssign/UserDefinedCompoundAssign) have no node
    // class yet, so no instruction carries them. Recognize the call and the
    // store kinds that exist today.
    return inst->Op == OpCode::Call || inst->Op == OpCode::StLoc || inst->Op == OpCode::StObj;
}

bool HighLevelLoopTransform::MatchIncrement(ILInstruction* inst, ILVariablePtr& variable) {
    if (!inst || inst->Op != OpCode::StLoc) return false;
    auto* st = static_cast<StLoc*>(inst);
    ILInstruction* value = st->Value.get();
    if (!value || value->Op != OpCode::BinaryNumericInstruction) return false;
    auto* bop = static_cast<BinaryNumericInstruction*>(value);
    if (bop->Operator != BinaryNumericOperator::Add) return false;
    ILInstruction* left = bop->Left.get();
    if (!left || left->Op != OpCode::LdLoc) return false;
    auto* ld = static_cast<LdLoc*>(left);
    if (ld->Variable.get() != st->Variable.get()) return false;
    variable = st->Variable;
    return true;
}

bool HighLevelLoopTransform::MatchIncrementBlock(Block* block, Block*& loopHead) {
    loopHead = nullptr;
    if (!block) return false;
    // The block's final must be a Branch to the loop head (the C# reads
    // Instructions.Last()); the other instructions (the C# SkipLast(1)) must all
    // be simple statements.
    auto* br = dynamic_cast<Branch*>(block->FinalInstruction.get());
    if (!br || !br->TargetBlock) return false;
    for (auto& inst : block->Instructions) {
        if (!IsSimpleStatement(inst.get())) return false;
    }
    loopHead = br->TargetBlock;
    return true;
}

bool HighLevelLoopTransform::MatchDoWhileConditionBlock(Block* block, Block*& target1, Block*& target2) {
    target1 = target2 = nullptr;
    if (!block) return false;
    // The if is the block's final (the C# reads Instructions[Count-2]); it must
    // have no else (FalseInst null -- the C# checks FalseInst.MatchNop()).
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff || iff->FalseInst) return false;

    // True arm: a Branch to target1, or a return (a Leave exiting the function).
    if (auto* br = dynamic_cast<Branch*>(iff->TrueInst.get())) {
        target1 = br->TargetBlock;
    } else if (auto* leave = dynamic_cast<Leave*>(iff->TrueInst.get())) {
        if (!IsLeavingFunction(leave)) return false;
        target1 = nullptr;  // return: no block target
    } else {
        return false;
    }

    // Fall-through (false arm): the next block in the container (the C# reads
    // Instructions.Last() as an explicit fall-through Branch). When there is no
    // next block the fall-through exits the container; treat it as a return
    // (target2 = null) only when that exit leaves the function body, matching
    // the C# last.MatchReturn.
    Block* next = NextBlockInContainer(block);
    if (next) {
        target2 = next;
    } else {
        auto* container = dynamic_cast<BlockContainer*>(block->Parent);
        if (!container || !container->Parent || container->Parent->Op != OpCode::ILFunction)
            return false;  // falls out of a nested container: a leave, not a return
        target2 = nullptr;  // return fall-through
    }
    return true;
}

} // namespace ILSpy::Decompiler::IL
