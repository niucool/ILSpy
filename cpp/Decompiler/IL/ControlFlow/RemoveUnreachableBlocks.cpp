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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"

#include <functional>
#include <unordered_set>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Whether `block`'s final transfers control unconditionally (no fall-through
// to the next sibling block). A null final falls through; an IfInstruction's
// false path falls through (no else, or a Nop else); a Branch/Leave/Throw/
// SwitchInstruction final does not. An if-else falls through only if an arm
// falls through.
bool FinalIsUnconditionalTransfer(const Block* block) {
    const ILInstruction* fin = block->FinalInstruction.get();
    if (!fin) return false;  // null: fall-through
    switch (fin->Op) {
    case OpCode::Branch:
    case OpCode::Leave:
    case OpCode::Throw:
    case OpCode::SwitchInstruction:
        return true;
    case OpCode::IfInstruction: {
        const auto* iff = static_cast<const IfInstruction*>(fin);
        if (!iff->FalseInst || iff->FalseInst->Op == OpCode::Nop) return false;  // false path falls through
        auto armTransfers = [](const ILInstruction* arm) -> bool {
            if (!arm) return false;  // null arm: fall-through
            if (arm->Op == OpCode::Nop) return false;
            if (auto* b = dynamic_cast<const Block*>(arm)) {
                const ILInstruction* af = b->FinalInstruction.get();
                if (!af) return false;
                return af->Op == OpCode::Branch || af->Op == OpCode::Leave ||
                       af->Op == OpCode::Throw || af->Op == OpCode::SwitchInstruction;
            }
            return arm->Op == OpCode::Branch || arm->Op == OpCode::Leave ||
                   arm->Op == OpCode::Throw || arm->Op == OpCode::SwitchInstruction;
        };
        return armTransfers(iff->TrueInst.get()) && armTransfers(iff->FalseInst.get());
    }
    default:
        return false;  // any other final: conservatively fall-through
    }
}

// Collect every BlockContainer that `inst`'s subtree owns as a direct child
// statement (a container nested in a Block's Instructions or final). Used to
// mark the container's entry block reachable when its owner block runs.
void CollectOwnedContainers(const ILInstruction* inst, std::vector<const BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<const BlockContainer*>(inst)) { out.push_back(c); return; }
    // A Block's Instructions/final may own containers; recurse into Blocks but
    // not into containers (a container's own blocks are handled when the
    // container's entry is processed).
    if (auto* b = dynamic_cast<const Block*>(inst)) {
        for (const auto& i : b->Instructions) CollectOwnedContainers(i.get(), out);
        CollectOwnedContainers(b->FinalInstruction.get(), out);
        return;
    }
    // Construct nodes (TryFinally/Using/Lock/etc.) own containers as children.
    for (int i = 0; i < inst->ChildCount(); ++i) CollectOwnedContainers(inst->GetChild(i), out);
}

// Collect every Branch.TargetBlock reachable from `inst`'s subtree.
void CollectBranchTargets(const ILInstruction* inst,
                          const std::unordered_map<std::uint32_t, const Block*>& blockByOffset,
                          std::vector<const Block*>& out) {
    if (!inst) return;
    if (auto* br = dynamic_cast<const Branch*>(inst)) {
        if (br->TargetBlock) out.push_back(br->TargetBlock);
        else if (br->HasOffset) {
            // Unresolved offset branch: resolve via the offset map.
            auto it = blockByOffset.find(br->TargetOffset);
            if (it != blockByOffset.end()) out.push_back(it->second);
        }
        return;
    }
    if (auto* sw = dynamic_cast<const SwitchInstruction*>(inst)) {
        for (const auto& sec : sw->Sections) {
            if (sec && sec->Body) {
                if (auto* b = dynamic_cast<const Branch*>(sec->Body.get())) {
                    if (b->TargetBlock) out.push_back(b->TargetBlock);
                    else if (b->HasOffset) {
                        auto it = blockByOffset.find(b->TargetOffset);
                        if (it != blockByOffset.end()) out.push_back(it->second);
                    }
                } else
                    CollectBranchTargets(sec->Body.get(), blockByOffset, out);
            }
        }
        return;
    }
    // Don't descend into containers (their internal branches are handled when
    // the container's blocks are processed); but DO descend into if-arms and
    // blocks to find nested branches.
    if (auto* b = dynamic_cast<const Block*>(inst)) {
        for (const auto& i : b->Instructions) CollectBranchTargets(i.get(), blockByOffset, out);
        CollectBranchTargets(b->FinalInstruction.get(), blockByOffset, out);
        return;
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CollectBranchTargets(inst->GetChild(i), blockByOffset, out);
}

} // namespace

void RemoveUnreachableBlocks::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    if (!function.Body || function.Body->Blocks.empty()) return;

    // Global offset -> block map (for unresolved offset branches -- a Branch
    // with HasOffset=true, TargetBlock null -- that the structure transforms
    // may leave behind; the renderer resolves these by offset, so reachability
    // must too, or it would remove the offset target as "unreachable").
    std::unordered_map<std::uint32_t, const Block*> blockByOffset;
    std::function<void(ILInstruction*)> collectOffsets = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* b = dynamic_cast<Block*>(inst)) {
            blockByOffset[b->StartILOffset] = b;
        }
        if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
            for (const auto& bb : c->Blocks) collectOffsets(bb.get());
            return;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) collectOffsets(inst->GetChild(i));
    };
    collectOffsets(function.Body.get());

    // Whole-function reachability from the function-body entry. A block is
    // reachable if it is the function-body entry, the first block of a
    // container owned by a reachable block, a Branch target of a reachable
    // block, or the fall-through successor of a reachable block (the next
    // sibling in its container, when the prior block's final is not an
    // unconditional transfer). This is a single worklist pass over all blocks
    // in all containers (cross-container branches mark their targets, which a
    // per-container pass would miss, leaving a dangling Branch).
    std::unordered_set<const Block*> reachable;
    std::vector<const Block*> worklist;
    // Mark B reachable, and also mark every ancestor BLOCK that owns B (a
    // nested block is reached only by executing its owner block, which owns
    // the container B is in -- destroying the owner would destroy B).
    std::function<void(const Block*)> mark = [&](const Block* b) {
        if (!b) return;
        // Walk up from b, marking ancestor owner blocks reachable.
        for (const ILInstruction* p = b->Parent; p;) {
            // p is the container b is in (or an ancestor construct). The Block
            // that owns the container/construct is the reachability ancestor.
            if (auto* bb = dynamic_cast<const Block*>(p)) {
                if (reachable.insert(bb).second) worklist.push_back(bb);
                p = bb->Parent;
                continue;
            }
            p = p->Parent;
        }
        if (reachable.insert(b).second) worklist.push_back(b);
    };
    mark(function.Body->Blocks.front().get());
    while (!worklist.empty()) {
        const Block* b = worklist.back();
        worklist.pop_back();
        // Branch targets (the block's instructions + final; recurses into
        // if-arms and nested blocks, but not into containers).
        std::vector<const Block*> targets;
        for (const auto& inst : b->Instructions) CollectBranchTargets(inst.get(), blockByOffset, targets);
        CollectBranchTargets(b->FinalInstruction.get(), blockByOffset, targets);
        for (const Block* t : targets) mark(t);
        // Fall-through to the next sibling block in the same container.
        auto* container = dynamic_cast<const BlockContainer*>(b->Parent);
        if (container && !FinalIsUnconditionalTransfer(b)) {
            for (std::size_t i = 0; i + 1 < container->Blocks.size(); ++i) {
                if (container->Blocks[i].get() == b) { mark(container->Blocks[i + 1].get()); break; }
            }
        }
        // Entering containers this block owns (as instructions or final): the
        // container's first block is reachable.
        std::vector<const BlockContainer*> owned;
        for (const auto& inst : b->Instructions) CollectOwnedContainers(inst.get(), owned);
        CollectOwnedContainers(b->FinalInstruction.get(), owned);
        for (const BlockContainer* c : owned)
            if (!c->Blocks.empty()) mark(c->Blocks.front().get());
    }

    // Erase unreachable blocks from every container. A container whose first
    // block (the entry) is NOT globally reachable is a dead container -- leave
    // it intact (removing its blocks would leave dangling Branch pointers in
    // the kept entry block -> use-after-free; the C# drops the whole dead
    // container node, a rarer case this subset defers). For a reachable
    // container, remove non-entry blocks that are not globally reachable. The
    // entry is always reachable for a live container (entered from its owner).
    std::function<void(BlockContainer*)> clean = [&](BlockContainer* c) {
        if (!c || c->Blocks.empty()) return;
        if (!reachable.count(c->Blocks.front().get())) return;  // dead container
        bool anyErased = false;
        for (auto it = c->Blocks.begin() + 1; it != c->Blocks.end();) {
            if (reachable.count(it->get())) { ++it; continue; }
            it = c->Blocks.erase(it);
            anyErased = true;
        }
        if (anyErased) {
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                c->Blocks[i]->ChildIndex = static_cast<int>(i);
                c->Blocks[i]->Parent = c;
            }
        }
        // Recurse into nested containers of the surviving blocks.
        for (auto& b : c->Blocks) {
            std::function<void(ILInstruction*)> walk = [&](ILInstruction* n) {
                if (!n) return;
                if (auto* cc = dynamic_cast<BlockContainer*>(n)) clean(cc);
                for (int i = 0; i < n->ChildCount(); ++i) walk(n->GetChild(i));
            };
            for (const auto& inst : b->Instructions) walk(inst.get());
            walk(b->FinalInstruction.get());
        }
    };
    clean(function.Body.get());
}

} // namespace ILSpy::Decompiler::IL
