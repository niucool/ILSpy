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
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <functional>
#include <queue>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

void WalkContainers(ILInstruction* inst, const std::function<void(BlockContainer*)>& visit) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) visit(c);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkContainers(inst->GetChild(i), visit);
}

// A StLoc to a PinnedLocal, or null.
StLoc* MatchStLocPinned(ILInstruction* inst) {
    auto* st = dynamic_cast<StLoc*>(inst);
    if (!st || !st->Variable) return nullptr;
    if (st->Variable->Kind != VariableKind::PinnedLocal) return nullptr;
    return st;
}

// True if `inst` is ldc.i4(0) or ldnull, unwrapping conv layers (the C#
// IsNullOrZero). Used to recognise the unpin store (`stloc P, 0/null`).
bool IsNullOrZero(ILInstruction* inst) {
    while (auto* conv = dynamic_cast<Conv*>(inst))
        inst = conv->Argument.get();
    if (auto* ldc = dynamic_cast<LdcI4*>(inst)) return ldc->Value == 0;
    if (dynamic_cast<LdNull*>(inst)) return true;
    return false;
}

// The first non-final instruction of `block` is a StLoc to `var` (the unpin
// marker store). The C# matches `block.Instructions[0].MatchStLoc(var, _)`.
bool IsUnpinBlock(Block* block, ILVariable* var) {
    if (block->Instructions.empty()) return false;
    auto* st = dynamic_cast<StLoc*>(block->Instructions[0].get());
    return st && st->Variable.get() == var;
}

// The pinned flag has no effect on value types (C# #2148); only form a region
// for reference/pointer/by-ref locals. The C# checks `Type.IsReferenceType ==
// false`; that is true only for value types (Struct/Enum/Void), null for by-ref
// and pointer, true for class/array/string/interface/delegate.
bool PinnedFlagHasNoEffect(const TypeSystem::IType* t) {
    if (!t) return true;
    auto k = t->Kind();
    return k == TypeSystem::TypeKind::Struct || k == TypeSystem::TypeKind::Enum ||
           k == TypeSystem::TypeKind::Void;
}

BlockContainer* ParentContainerOf(Block* b) {
    return b ? dynamic_cast<BlockContainer*>(b->Parent) : nullptr;
}

// Find the index of `block` in its container, or size() if not found.
std::size_t IndexOf(BlockContainer* c, Block* block) {
    for (std::size_t i = 0; i < c->Blocks.size(); ++i)
        if (c->Blocks[i].get() == block) return i;
    return c->Blocks.size();
}

// If `branch` leaves the pinned region (its target is in `source` but outside
// the reached set), and the target is its only predecessor, strip the target's
// leading unpin store (`stloc P, 0`) -- the pin is no longer reset on that edge.
void HandleBranchLeavingPinnedRegion(ILInstruction* potentialBranch,
                                     const std::vector<int>& reachedEdgesPerBlock,
                                     BlockContainer* source, ILVariable* var) {
    auto* br = dynamic_cast<Branch*>(potentialBranch);
    if (!br || !br->TargetBlock) return;
    Block* target = br->TargetBlock;
    if (target->Parent != source) return;
    std::size_t idx = IndexOf(source, target);
    if (idx >= source->Blocks.size()) return;
    if (reachedEdgesPerBlock[idx] != 0) return;            // target is inside the region
    if (target->IncomingEdgeCount != 1) return;            // unpin is shared; leave it
    if (target->Instructions.empty()) return;
    auto* unpin = dynamic_cast<StLoc*>(target->Instructions[0].get());
    if (unpin && unpin->Variable.get() == var && IsNullOrZero(unpin->Value.get())) {
        target->Instructions.erase(target->Instructions.begin());
        target->RenumberChildren();
    }
}

// Process the arms of a block's final for branches leaving the region. An
// IfInstruction final has Branch arms (true/false); a Branch final is the arm.
void HandleBlockExitsLeavingRegion(Block* block, const std::vector<int>& reached,
                                   BlockContainer* source, ILVariable* var) {
    if (!block->FinalInstruction) return;
    if (auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get())) {
        HandleBranchLeavingPinnedRegion(iff->TrueInst.get(), reached, source, var);
        if (iff->FalseInst)
            HandleBranchLeavingPinnedRegion(iff->FalseInst.get(), reached, source, var);
    } else {
        HandleBranchLeavingPinnedRegion(block->FinalInstruction.get(), reached, source, var);
    }
}

// Split `block` so a pinned-local write is the block's last non-final
// instruction and is followed by a Branch -- the shape DetectPinnedRegion
// expects. If the write is already in that shape, do nothing. (Adapted from the
// C# SplitBlocksAtWritesToPinnedLocals; the "split before" and re-pinning
// un-inline cases are deferred -- they arise with C++/CLI re-pinning.)
bool SplitBlockAtPinnedWrite(BlockContainer* container, std::size_t blockIdx) {
    Block* block = container->Blocks[blockIdx].get();
    for (std::size_t j = 0; j < block->Instructions.size(); ++j) {
        if (!MatchStLocPinned(block->Instructions[j].get())) continue;
        // The instruction after the write: the next non-final, or the final.
        ILInstruction* next = (j + 1 < block->Instructions.size())
            ? block->Instructions[j + 1].get()
            : block->FinalInstruction.get();
        if (dynamic_cast<Branch*>(next)) break;  // already in shape
        // Split after j: move Instructions[j+1..] and the final into a new block,
        // this block falls through to it.
        auto newBlock = std::make_unique<Block>();
        newBlock->StartILOffset = block->StartILOffset;  // approximate
        while (block->Instructions.size() > j + 1) {
            auto inst = std::move(block->Instructions[j + 1]);
            block->Instructions.erase(block->Instructions.begin() + j + 1);
            newBlock->Add(std::move(inst));
        }
        if (block->FinalInstruction)
            newBlock->SetFinal(std::move(block->FinalInstruction));
        block->RenumberChildren();
        newBlock->RenumberChildren();
        block->SetFinal(std::make_unique<Branch>(newBlock.get()));
        container->Blocks.insert(container->Blocks.begin() + blockIdx + 1,
                                  std::move(newBlock));
        for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
            container->Blocks[i]->Parent = container;
            container->Blocks[i]->ChildIndex = static_cast<int>(i);
        }
        return true;
    }
    return false;
}

// Detect and build a PinnedRegion for the pinned-local write that ends `block`.
// Returns true if a region was formed. The simple, non-clone case: the region's
// blocks are all reached only from within the region (no outside edges), so they
// can be moved into a PinnedRegion body wholesale.
bool DetectPinnedRegion(Block* block) {
    if (block->Instructions.empty()) return false;
    auto* stLoc = MatchStLocPinned(block->Instructions.back().get());
    if (!stLoc) return false;
    auto* var = stLoc->Variable.get();
    if (IsNullOrZero(stLoc->Value.get())) return false;  // the write is an unpin
    if (PinnedFlagHasNoEffect(var->Type.get())) return false;
    auto* branch = dynamic_cast<Branch*>(block->FinalInstruction.get());
    if (!branch || !branch->TargetBlock) return false;
    Block* entryBlock = branch->TargetBlock;
    BlockContainer* source = ParentContainerOf(block);
    if (!source) return false;
    if (entryBlock->Parent != source) return false;
    if (IsUnpinBlock(entryBlock, var)) return false;  // empty body -- deferred

    // BFS from entryBlock over branches whose target is in `source`, stopping at
    // unpin blocks. reachedEdgesPerBlock[i] counts the edges into block i that
    // stay inside the region; the initial seed for entryBlock accounts for the
    // pin block's own branch into it (mirrors the C#).
    std::vector<int> reachedEdgesPerBlock(source->Blocks.size(), 0);
    std::queue<Block*> workList;
    reachedEdgesPerBlock[IndexOf(source, entryBlock)]++;
    workList.push(entryBlock);
    while (!workList.empty()) {
        Block* workItem = workList.front();
        workList.pop();
        WalkAll(workItem, [&](ILInstruction* inst) {
            auto* br = dynamic_cast<Branch*>(inst);
            if (!br || !br->TargetBlock) return;
            Block* target = br->TargetBlock;
            if (target->Parent != source) return;
            if (IsUnpinBlock(target, var)) return;  // edge to the unpin -- stop
            std::size_t idx = IndexOf(source, target);
            if (idx >= source->Blocks.size()) return;
            if (reachedEdgesPerBlock[idx]++ == 0) workList.push(target);
        });
    }

    // Validate: every reached block's edges are all inside the region, or it is
    // untouched. A block reachable both inside and outside would need cloning
    // (the C# duplicates it); this subset defers that and leaves the method
    // without the `fixed` sugar rather than duplicate.
    for (std::size_t i = 0; i < source->Blocks.size(); ++i) {
        if (reachedEdgesPerBlock[i] != 0 &&
            reachedEdgesPerBlock[i] != source->Blocks[i]->IncomingEdgeCount)
            return false;
    }

    // Build the body: move the reached blocks into a new container, entry first,
    // then the rest in source order. Strip leading unpin stores on the exit edges.
    // Replaced source slots become empty dummy blocks (erased after the per-block
    // loop) so the caller's range-for over container->Blocks stays valid.
    auto body = std::make_unique<BlockContainer>();
    BlockContainer* bodyPtr = body.get();
    std::size_t entryIdx = IndexOf(source, entryBlock);
    auto moveBlockOut = [&](std::size_t i) {
        HandleBlockExitsLeavingRegion(source->Blocks[i].get(), reachedEdgesPerBlock, source, var);
        bodyPtr->AddBlock(std::move(source->Blocks[i]));
        source->Blocks[i] = std::make_unique<Block>();  // dummy
        source->Blocks[i]->Parent = source;
        source->Blocks[i]->ChildIndex = static_cast<int>(i);
    };
    moveBlockOut(entryIdx);
    for (std::size_t i = 0; i < source->Blocks.size(); ++i) {
        if (i == entryIdx) continue;
        if (reachedEdgesPerBlock[i] > 0) moveBlockOut(i);
    }
    if (body->Blocks.empty()) return false;  // should not happen (entry is in the body)

    // Replace the pin store with the PinnedRegion. Extract the init value first.
    auto stLocOwner = std::move(block->Instructions.back());
    block->Instructions.pop_back();
    auto* stLocRaw = static_cast<StLoc*>(stLocOwner.get());
    auto init = std::move(stLocRaw->Value);
    auto pinnedRegion = std::make_unique<PinnedRegion>(
        stLocRaw->Variable, std::move(init), std::move(body));
    block->Add(std::move(pinnedRegion));

    // The pin block now falls through to the next non-dummy block in the source
    // container (the region blocks were replaced with dummies); make that
    // fall-through explicit with a Branch, or a Leave of the container if none.
    std::size_t pinIdx = IndexOf(source, block);
    Block* nextOuter = nullptr;
    for (std::size_t i = pinIdx + 1; i < source->Blocks.size(); ++i) {
        if (source->Blocks[i] && source->Blocks[i]->FinalInstruction) {
            nextOuter = source->Blocks[i].get();
            break;
        }
    }
    if (nextOuter)
        block->SetFinal(std::make_unique<Branch>(nextOuter));
    else
        block->SetFinal(std::make_unique<Leave>(source));
    block->RenumberChildren();
    return true;
}

} // namespace

void DetectPinnedRegions::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    RecomputeIncomingEdgeCounts(function);
    ComputeVariableUsage(function);

    // For each container: split blocks at pinned writes, then form regions.
    std::vector<BlockContainer*> containers;
    WalkContainers(function.Body.get(), [&](BlockContainer* c) { containers.push_back(c); });
    for (auto* container : containers) {
        for (std::size_t i = 0; i < container->Blocks.size(); ++i)
            (void)SplitBlockAtPinnedWrite(container, i);
        // Recompute edge counts after splitting before the region detection.
        RecomputeIncomingEdgeCounts(function);
        bool formed = false;
        for (auto& block : container->Blocks) {
            if (DetectPinnedRegion(block.get())) formed = true;
        }
        (void)formed;
        // Drop the dummy blocks the region extraction left behind (empty blocks
        // with no final). Keep any block that still has a final (e.g. the unpin
        // block, stripped to just its leave) and the container entry point.
        for (std::size_t i = 0; i < container->Blocks.size();) {
            Block* b = container->Blocks[i].get();
            if (b && b->Instructions.empty() && !b->FinalInstruction) {
                container->Blocks.erase(container->Blocks.begin() + i);
            } else {
                ++i;
            }
        }
        for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
            container->Blocks[i]->Parent = container;
            container->Blocks[i]->ChildIndex = static_cast<int>(i);
        }
        RecomputeIncomingEdgeCounts(function);
    }

    // Leftover writes to the original pinned locals (the pin store is now inside
    // a PinnedRegion; a leftover store the region didn't absorb is dead if pure,
    // else keep its side effect). Mirrors the C# post-pass.
    ComputeVariableUsage(function);
    std::vector<Block*> blocks;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (auto* b = dynamic_cast<Block*>(inst)) blocks.push_back(b);
    });
    for (Block* block : blocks) {
        for (std::size_t i = 0; i < block->Instructions.size();) {
            auto* st = dynamic_cast<StLoc*>(block->Instructions[i].get());
            if (st && st->Variable && st->Variable->Kind == VariableKind::PinnedLocal &&
                st->Variable->LoadCount == 0 && st->Variable->AddressCount == 0) {
                if (IsPure(st->Value ? st->Value->Flags() : InstructionFlags::None)) {
                    block->Instructions.erase(block->Instructions.begin() + i);
                    block->RenumberChildren();
                } else {
                    // Replace the dead pinned store with its value's side effect,
                    // reparenting the value (it was a child of the StLoc).
                    auto value = std::move(st->Value);
                    if (value) { value->Parent = block; value->ChildIndex = static_cast<int>(i); }
                    block->Instructions[i] = std::move(value);
                    block->RenumberChildren();
                    ++i;
                }
            } else {
                ++i;
            }
        }
    }

    RecomputeIncomingEdgeCounts(function);
    ComputeVariableUsage(function);
}

} // namespace ILSpy::Decompiler::IL
