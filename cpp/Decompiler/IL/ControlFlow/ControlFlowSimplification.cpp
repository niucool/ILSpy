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

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Our Block splits the terminal out of the statement list; the C# counts it in.
// A block holding only an unconditional branch:
bool IsChainBlock(const Block* b) {
    return b && b->Instructions.empty() && b->FinalInstruction &&
           b->FinalInstruction->Op == OpCode::Branch;
}

// No statements left and no terminal -> marked for deletion.
bool IsEmptyMarked(const Block* b) {
    return b && b->Instructions.empty() && !b->FinalInstruction;
}

void MarkForDeletion(Block* b, std::vector<std::unique_ptr<Block>>& graveyard);

BlockContainer* ParentContainerOf(Block* b) {
    return b ? dynamic_cast<BlockContainer*>(b->Parent) : nullptr;
}

// Contentless-block disposal: the C# destroys instructions (its destructor
// decrements edge counts); we move the block out of its container into a
// graveyard vector instead (kept alive until the transform ends, so snapshot
// pointers stay valid). Mirrors the edge-count decrements destruction would do.
void MarkForDeletion(Block* b, std::vector<std::unique_ptr<Block>>& graveyard) {
    if (!b) return;
    if (auto* br = dynamic_cast<Branch*>(b->FinalInstruction.get())) {
        if (br->TargetBlock) --br->TargetBlock->IncomingEdgeCount;
    }
    auto* container = ParentContainerOf(b);
    if (container) {
        auto& blocks = container->Blocks;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].get() == b) {
                graveyard.push_back(std::move(blocks[i]));
                blocks.erase(blocks.begin() + i);
                break;
            }
        }
        for (std::size_t i = 0; i < blocks.size(); ++i) blocks[i]->ChildIndex = static_cast<int>(i);
    } else {
        b->Instructions.clear();
        b->FinalInstruction.reset();
    }
}

// leave / leave-with-nop -> the C# folds branches to these.
bool IsLeaveLike(const ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::Leave) return false;
    const auto* leave = static_cast<const Leave*>(inst);
    return !leave->Value || leave->Value->Op == OpCode::Nop;
}

// Clone a pure-load value (no side effects) so a branch-to-return can be
// folded into a direct leave duplicating the return value. Returns nullptr
// for a value that is not a cheap cloneable pure load (the C# uses
// DetectExitPoints for the general case; this is the seed-level fast path).
std::unique_ptr<ILInstruction> ClonePureLoad(const ILInstruction* v) {
    if (!v) return nullptr;
    switch (v->Op) {
        case OpCode::LdLoc: {
            auto* ld = static_cast<const LdLoc*>(v);
            return std::make_unique<LdLoc>(ld->Variable);
        }
        case OpCode::LdcI4: return std::make_unique<LdcI4>(static_cast<const LdcI4*>(v)->Value);
        case OpCode::LdcI8: return std::make_unique<LdcI8>(static_cast<const LdcI8*>(v)->Value);
        case OpCode::LdcF4: return std::make_unique<LdcF4>(static_cast<const LdcF4*>(v)->Value);
        case OpCode::LdcF8: return std::make_unique<LdcF8>(static_cast<const LdcF8*>(v)->Value);
        case OpCode::LdNull: return std::make_unique<LdNull>();
        case OpCode::LdStr: return std::make_unique<LdStr>(static_cast<const LdStr*>(v)->Value);
        default: return nullptr;
    }
}

void ForEach(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) ForEach(inst->GetChild(i), visit);
}

// True if `target` is the fall-through of its predecessor in the container: the
// previous block's final is not EndPointUnreachable (it falls through). Such a
// block is reachable even with no Branch edges, so CFS must not delete it.
bool IsFallThroughTarget(Block* target) {
    if (!target) return false;
    auto* c = dynamic_cast<BlockContainer*>(target->Parent);
    if (!c) return false;
    for (std::size_t i = 1; i < c->Blocks.size(); ++i) {
        if (c->Blocks[i].get() == target) {
            ILInstruction* fin = c->Blocks[i - 1]->FinalInstruction.get();
            return fin && !HasFlag(fin->Flags(), InstructionFlags::EndPointUnreachable);
        }
    }
    return false;
}

void ForEachContainer(ILInstruction* inst, const std::function<void(BlockContainer*)>& visit) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) visit(c);
    for (int i = 0; i < inst->ChildCount(); ++i) ForEachContainer(inst->GetChild(i), visit);
}

// Dead store of ldc.i4 1 into an unused bool local just before the final
// branch: csc emits this for while(true) (the IsDeadTrueStore case). Null or
// non-bool types disqualify it (we don't know the type -> keep the store).
bool IsDeadTrueStore(const Block* b) {
    if (!b || b->Instructions.empty()) return false;
    const auto* st = dynamic_cast<const StLoc*>(b->Instructions.back().get());
    if (!st || !st->Variable) return false;
    if (st->Variable->LoadCount != 0 || st->Variable->AddressCount != 0) return false;
    const TypeSystem::IType* t = st->Variable->Type.get();
    if (!t || t->ReflectionName() != "System.Boolean") return false;
    const auto* one = dynamic_cast<const LdcI4*>(st->Value.get());
    return one && one->Value == 1;
}

void RemoveDeadStackStores(Block* block, ILTransformContext& context) {
    // Non-aggressive only: the aggressive (RemoveDeadStores) path needs
    // SemanticHelper.IsPure side-effect analysis, which arrives with the later
    // transforms that use it.
    for (int i = static_cast<int>(block->Instructions.size()) - 1; i >= 0; --i) {
        auto* st = dynamic_cast<StLoc*>(block->Instructions[i].get());
        if (!st || !st->Variable) continue;
        if (st->Variable->Kind != VariableKind::StackSlot) continue;
        if (!st->Variable->IsSingleDefinition() || st->Variable->LoadCount != 0) continue;
        context.StepOnce("Remove dead stack store");
        bool simple = st->Value->Op == OpCode::LdLoc || st->Value->Op == OpCode::LdStr;
        if (simple) {
            // Pure value -> remove the whole instruction.
            block->RemoveInstructionAt(static_cast<std::size_t>(i));
        } else {
            // Keep the side effects: replace the store with its value.
            auto value = st->TakeChild(0);
            st->ReplaceWith(std::move(value));
        }
    }
}

void InlineVariableInReturnBlock(Block* block, ILTransformContext& context) {
    // Debug-mode return block: v = <expr>; leave v  ->  leave <expr>.
    if (block->Instructions.size() != 1) return;
    auto* leave = dynamic_cast<Leave*>(block->FinalInstruction.get());
    if (!leave || !leave->Value) return;
    auto* load = dynamic_cast<LdLoc*>(leave->Value.get());
    if (!load || !load->Variable) return;
    ILVariable* v = load->Variable.get();
    if (!v->IsSingleDefinition() || v->LoadCount != 1) return;
    auto* stloc = dynamic_cast<StLoc*>(block->Instructions[0].get());
    if (!stloc || stloc->Variable.get() != v) return;
    context.StepOnce("Inline variable in return block");
    leave->SetChild(0, stloc->TakeChild(0));
    block->Instructions.clear();
    block->RenumberChildren();
}

void SimplifyBranchChains(ILFunction& function, ILTransformContext& context,
                          std::vector<std::unique_ptr<Block>>& graveyard) {
    std::vector<Branch*> branches;
    ForEach(function.Body.get(), [&](ILInstruction* i) {
        if (i->Op == OpCode::Branch) branches.push_back(static_cast<Branch*>(i));
    });
    for (Branch* branch : branches) {
        Block* target = branch->TargetBlock;
        if (!target) continue;  // unresolved offset branch: leave alone
        // Resolve chains of single-branch trampoline blocks to the end block.
        std::vector<Block*> visited;
        while (IsChainBlock(target)) {
            bool cyclic = false;
            for (auto* seen : visited) if (seen == target) { cyclic = true; break; }
            if (cyclic) break;
            visited.push_back(target);
            context.StepOnce("Simplify branch to branch");
            auto* next = static_cast<Branch*>(target->FinalInstruction.get());
            if (next->TargetBlock) ++next->TargetBlock->IncomingEdgeCount;
            --target->IncomingEdgeCount;
            branch->TargetBlock = next->TargetBlock;
            if (target->IncomingEdgeCount == 0 && !IsFallThroughTarget(target))
                MarkForDeletion(target, graveyard);
            target = branch->TargetBlock;
            if (!target) break;
        }
        if (!target) continue;
        // (The C# also moves return blocks into their try scope here; that
        // needs per-scope usage analysis, which is deferred with the rest of
        // the EH-aware flow transforms.)
        if (IsLeaveLike(target->FinalInstruction.get()) &&
            target->Instructions.empty()) {
            // Branching straight to a plain leave: duplicate the leave.
            context.StepOnce("Replace branch to leave with leave");
            auto* targetLeave = static_cast<Leave*>(target->FinalInstruction.get());
            --target->IncomingEdgeCount;
            std::unique_ptr<ILInstruction> dup =
                std::make_unique<Leave>(targetLeave->TargetContainer);
            branch->ReplaceWith(std::move(dup));
        } else if (target->Instructions.empty() &&
                   target->FinalInstruction &&
                   target->FinalInstruction->Op == OpCode::Leave) {
            // Branching to a value-return block (leave with a value): fold to a
            // direct leave duplicating the value when it is a cheap pure load
            // (ldloc/ldc/ldstr/ldnull). This turns `goto returnBlock` into
            // `return value`, eliminating the goto and turning the branch's arm
            // into an exit (the early-return pattern). The general case
            // (non-cloneable values) is DetectExitPoints, deferred.
            auto* targetLeave = static_cast<Leave*>(target->FinalInstruction.get());
            auto cloned = ClonePureLoad(targetLeave->Value.get());
            if (cloned) {
                context.StepOnce("Replace branch to value-return with leave");
                --target->IncomingEdgeCount;
                std::unique_ptr<ILInstruction> dup =
                    std::make_unique<Leave>(targetLeave->TargetContainer, std::move(cloned));
                branch->ReplaceWith(std::move(dup));
            }
        }
        if (target->IncomingEdgeCount == 0 && !IsFallThroughTarget(target))
            MarkForDeletion(target, graveyard);
    }
}

bool CombineBlockWithNextBlock(BlockContainer* container, Block* block,
                               ILTransformContext& context,
                               std::vector<std::unique_ptr<Block>>& graveyard) {
    // Don't create extended basic blocks: a branchy statement before the
    // terminal disqualifies the block.
    if (!block->Instructions.empty() &&
        HasFlag(block->Instructions.back()->Flags(), InstructionFlags::MayBranch))
        return false;
    auto* br = dynamic_cast<Branch*>(block->FinalInstruction.get());
    if (!br) return false;
    Block* target = br->TargetBlock;
    if (!target || ParentContainerOf(target) != container) return false;
    if (target->IncomingEdgeCount != 1 || target == block) return false;
    context.StepOnce("CombineBlockWithNextBlock");

    if (target->StartILOffset < block->StartILOffset && IsDeadTrueStore(block)) {
        // The while(true) condition store is dead; drop it before merging.
        block->Instructions.pop_back();
        block->RenumberChildren();
    }

    // The branch into target is absorbed; its edge is what the merge removes
    // (target's outgoing edges transfer to block).
    --target->IncomingEdgeCount;
    block->FinalInstruction.reset();
    for (auto& inst : target->Instructions) {
        inst->Parent = block;
        block->Instructions.push_back(std::move(inst));
    }
    target->Instructions.clear();
    block->FinalInstruction = std::move(target->FinalInstruction);
    if (block->FinalInstruction) block->FinalInstruction->Parent = block;
    block->RenumberChildren();
    target->FinalInstruction.reset();
    MarkForDeletion(target, graveyard);  // drained; moved out of the container
    return true;
}

void CleanUpEmptyBlocks(ILFunction& function, ILTransformContext& context,
                        std::vector<std::unique_ptr<Block>>& graveyard) {
    ForEachContainer(function.Body.get(), [&](BlockContainer* container) {
        // Index-based iteration: combining absorbs (removes) blocks from the
        // container, which would invalidate range-for iterators.
        for (std::size_t bi = 0; bi < container->Blocks.size(); ++bi) {
            Block* block = container->Blocks[bi].get();
            if (IsEmptyMarked(block)) continue;
            while (CombineBlockWithNextBlock(container, block, context, graveyard)) {
                // one block is absorbed per iteration
            }
        }
    });
}

} // namespace

void ControlFlowSimplification::Run(ILFunction& function, ILTransformContext& context) {
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);

    // (RemoveNopInstructions from the C# is unnecessary: our reader never
    // materializes nop nodes.)
    ForEach(function.Body.get(), [&](ILInstruction* i) {
        auto* block = dynamic_cast<Block*>(i);
        if (!block) return;
        RemoveDeadStackStores(block, context);
        InlineVariableInReturnBlock(block, context);
    });

    std::vector<std::unique_ptr<Block>> graveyard;
    SimplifyBranchChains(function, context, graveyard);
    CleanUpEmptyBlocks(function, context, graveyard);
}

} // namespace ILSpy::Decompiler::IL
