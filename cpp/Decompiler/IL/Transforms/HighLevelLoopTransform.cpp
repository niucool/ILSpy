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
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <cassert>
#include <functional>
#include <vector>

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

// Negate a condition (port of Comp.LogicNot): fold into a Comp when possible.
static std::unique_ptr<ILInstruction> NegateCondition(std::unique_ptr<ILInstruction> cond) {
    if (!cond) return cond;
    if (auto* comp = dynamic_cast<Comp*>(cond.get())) {
        if (comp->Kind == ComparisonKind::Equality && comp->Right &&
            comp->Right->Op == OpCode::LdcI4 &&
            static_cast<LdcI4*>(comp->Right.get())->Value == 0)
            return std::move(comp->Left);  // logic.not(x == 0) -> x
        comp->Kind = NegateComparison(comp->Kind);
        return cond;
    }
    if (auto* iff = dynamic_cast<IfInstruction*>(cond.get())) {
        auto t = std::move(iff->TrueInst);
        iff->TrueInst = std::move(iff->FalseInst);
        iff->FalseInst = std::move(t);
        if (iff->TrueInst) iff->TrueInst->ChildIndex = 1;
        if (iff->FalseInst) iff->FalseInst->ChildIndex = 2;
        return cond;
    }
    auto zero = (cond->ResultType() == StackType::O)
        ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
        : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
    return std::make_unique<Comp>(std::move(cond), std::move(zero), ComparisonKind::Equality);
}

void HighLevelLoopTransform::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Walk every Loop-kind container. (In our model the if is the block's
    // FinalInstruction; the C# reads it from Instructions[0] because the C#
    // block carries the if as a non-terminal with an explicit fall-through
    // Branch. Our entry point's if-as-final with no else (FalseInst null) is the
    // same shape.)
    std::vector<BlockContainer*> loops;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* c = dynamic_cast<BlockContainer*>(inst))
            if (c->Kind == ContainerKind::Loop) loops.push_back(c);
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(function.Body.get());
    for (BlockContainer* loop : loops) {
        if (loop->Blocks.empty()) continue;
        Block* entry = loop->Blocks.front().get();
        auto* iff = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get());
        if (!iff) continue;
        // Shape 1 (C#): `if (cond) leave loop` (TrueInst a Leave, no else).
        // The while condition is the negated cond. Negate, the leave becomes the
        // false arm (break), a branch to the body becomes the true arm.
        if (!iff->FalseInst && iff->TrueInst && iff->TrueInst->Op == OpCode::Leave) {
            auto* leave = static_cast<Leave*>(iff->TrueInst.get());
            if (leave->TargetContainer != loop) continue;
            iff->Condition = NegateCondition(std::move(iff->Condition));
            if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
            iff->FalseInst = std::move(iff->TrueInst);
            if (iff->FalseInst) { iff->FalseInst->Parent = iff; iff->FalseInst->ChildIndex = 2; }
            iff->TrueInst = std::make_unique<Branch>(loop->Blocks.size() > 1 ? loop->Blocks[1].get() : entry);
            iff->TrueInst->Parent = iff;
            iff->TrueInst->ChildIndex = 1;
            loop->Kind = ContainerKind::While;
            continue;
        }
        // Shape 2 (this port's LoopDetection): `if (cond) br body else leave loop`
        // (TrueInst a Branch to a body block, FalseInst a Leave(loop)). The while
        // condition is cond (no negation). Just mark as While -- the seed extracts
        // the condition and renders the body blocks.
        if (iff->FalseInst && iff->FalseInst->Op == OpCode::Leave &&
            iff->TrueInst && iff->TrueInst->Op == OpCode::Branch) {
            auto* leave = static_cast<Leave*>(iff->FalseInst.get());
            if (leave->TargetContainer != loop) continue;
            loop->Kind = ContainerKind::While;
            continue;
        }
        // MatchDoWhileLoop: a loop whose last block is a do-while condition --
        // an if with a true-arm Branch to the loop header (continue) and a
        // fall-through that exits the loop (break). The entry point has no
        // while-condition if (it falls straight into the body). Mark as DoWhile
        // so the seed renders `do { ... } while (cond)`.
        if (loop->Blocks.size() >= 2) {
            Block* last = loop->Blocks.back().get();
            Block* header = loop->Blocks.front().get();
            if (auto* condIf = dynamic_cast<IfInstruction*>(last->FinalInstruction.get())) {
                if (!condIf->FalseInst && condIf->TrueInst &&
                    condIf->TrueInst->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(condIf->TrueInst.get());
                    // The true arm branches to the header (the do-while
                    // continue); the fall-through exits the loop (the entry
                    // point's final is NOT a while-condition if).
                    if (br->TargetBlock == header) {
                        // The entry must not itself be a while-condition (that's
                        // the While shape, already handled above).
                        auto* entryIf = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get());
                        bool entryIsWhileCond = entryIf &&
                            ((entryIf->TrueInst && entryIf->TrueInst->Op == OpCode::Leave) ||
                             (entryIf->FalseInst && entryIf->FalseInst->Op == OpCode::Leave));
                        if (!entryIsWhileCond) {
                            loop->Kind = ContainerKind::DoWhile;
                            continue;
                        }
                    }
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
