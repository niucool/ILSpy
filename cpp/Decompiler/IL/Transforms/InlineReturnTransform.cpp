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

#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Visit every node in the tree (pre-order).
void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

// Walk up the parent chain to the nearest enclosing BlockContainer
// (BlockContainer.FindClosestContainer in the C#).
BlockContainer* FindClosestContainer(ILInstruction* inst) {
    for (ILInstruction* p = inst; p != nullptr; p = p->Parent)
        if (auto* c = dynamic_cast<BlockContainer*>(p)) return c;
    return nullptr;
}

// Remove `block` from its container, returning ownership and renumbering the
// remaining blocks (Block.Remove in the C#).
std::unique_ptr<Block> RemoveBlockFromContainer(Block* block) {
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    auto& blocks = container->Blocks;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].get() == block) {
            auto ptr = std::move(blocks[i]);
            blocks.erase(blocks.begin() + i);
            for (std::size_t j = 0; j < blocks.size(); ++j) {
                blocks[j]->ChildIndex = static_cast<int>(j);
                blocks[j]->Parent = container;
            }
            return ptr;
        }
    }
    return nullptr;
}

// Deep-clone a return block: a Block whose final is a Leave of a LdLoc. The
// cloned Leave targets the same container and loads the same variable.
std::unique_ptr<Block> CloneReturnBlock(Block* block) {
    auto* leave = dynamic_cast<Leave*>(block->FinalInstruction.get());
    auto cloned = std::make_unique<Block>();
    cloned->StartILOffset = block->StartILOffset;
    if (leave) {
        auto* ldloc = dynamic_cast<LdLoc*>(leave->Value.get());
        if (ldloc) {
            cloned->SetFinal(std::make_unique<Leave>(
                leave->TargetContainer, std::make_unique<LdLoc>(ldloc->Variable)));
        } else {
            cloned->SetFinal(std::make_unique<Leave>(leave->TargetContainer));
        }
    }
    return cloned;
}

} // namespace

void InlineReturnTransform::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    RecomputeIncomingEdgeCounts(function);

    // Collect candidate return blocks: a block whose sole content is a Leave
    // of a LdLoc(Local).
    struct Candidate { Block* leaveBlock; ILVariable* returnVar; };
    std::vector<Candidate> candidates;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        auto* leave = dynamic_cast<Leave*>(inst);
        if (!leave) return;
        auto* block = dynamic_cast<Block*>(leave->Parent);
        if (!block) return;
        if (!block->Instructions.empty()) return;       // block must be solely the leave
        if (block->FinalInstruction.get() != leave) return;
        auto* ldloc = dynamic_cast<LdLoc*>(leave->Value.get());
        if (!ldloc || !ldloc->Variable) return;
        if (ldloc->Variable->Kind != VariableKind::Local) return;
        candidates.push_back({block, ldloc->Variable.get()});
    });

    for (auto& c : candidates) {
        // Gather every store to this variable.
        std::vector<StLoc*> stores;
        WalkAll(function.Body.get(), [&](ILInstruction* inst) {
            auto* st = dynamic_cast<StLoc*>(inst);
            if (st && st->Variable.get() == c.returnVar) stores.push_back(st);
        });
        if (stores.empty()) continue;

        // Every store must be the last instruction of its block, followed by a
        // branch to the return block, inside some container. If any store does
        // not fit the pattern, leave this variable untouched.
        struct Item { BlockContainer* container; Branch* br; };
        std::vector<Item> items;
        bool ok = true;
        for (StLoc* store : stores) {
            auto* storeBlock = dynamic_cast<Block*>(store->Parent);
            if (!storeBlock) { ok = false; break; }
            if (store->ChildIndex != static_cast<int>(storeBlock->Instructions.size()) - 1) {
                ok = false; break;
            }
            auto* br = dynamic_cast<Branch*>(storeBlock->FinalInstruction.get());
            if (!br || br->TargetBlock != c.leaveBlock) { ok = false; break; }
            auto* container = FindClosestContainer(store);
            if (!container) { ok = false; break; }
            items.push_back({container, br});
        }
        if (!ok || items.empty()) continue;

        // Move the last remaining predecessor's copy, clone the rest. The
        // running count mirrors the C# incremental IncomingEdgeCount update
        // (repointing a branch away from the block decrements its count).
        int remaining = c.leaveBlock->IncomingEdgeCount;
        for (auto& item : items) {
            Block* newBlock;
            if (remaining == 1) {
                auto ptr = RemoveBlockFromContainer(c.leaveBlock);
                newBlock = ptr.get();
                item.container->AddBlock(std::move(ptr));
            } else {
                auto clone = CloneReturnBlock(c.leaveBlock);
                newBlock = clone.get();
                item.container->AddBlock(std::move(clone));
            }
            item.br->TargetBlock = newBlock;
            --remaining;
        }
    }

    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
