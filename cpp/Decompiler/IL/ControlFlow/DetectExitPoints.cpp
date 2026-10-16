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

#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"

#include <functional>
#include <unordered_set>

namespace ILSpy::Decompiler::IL {

namespace {

// The first block the emitter visits inside `block`: descends into a
// "construct-leading" block (first instruction or final is a
// TryFinally/TryCatch/Using/Lock whose body is a BlockContainer) to the
// construct body's first block (recursively). Otherwise the block itself.
const Block* FirstEmittedBlockOf(const Block* block) {
    while (block) {
        const ILInstruction* entry = nullptr;
        if (!block->Instructions.empty())
            entry = block->Instructions.front().get();
        else
            entry = block->FinalInstruction.get();
        if (!entry) return block;
        const BlockContainer* body = nullptr;
        if (auto* tf = dynamic_cast<const TryFinally*>(entry))
            body = dynamic_cast<const BlockContainer*>(tf->TryBlock.get());
        else if (auto* tc = dynamic_cast<const TryCatch*>(entry))
            body = dynamic_cast<const BlockContainer*>(tc->TryBlock.get());
        else if (auto* u = dynamic_cast<const UsingInstruction*>(entry))
            body = dynamic_cast<const BlockContainer*>(u->Body.get());
        else if (auto* l = dynamic_cast<const LockInstruction*>(entry))
            body = dynamic_cast<const BlockContainer*>(l->Body.get());
        else
            return block;
        if (!body || body->Blocks.empty()) return block;
        block = body->Blocks.front().get();
    }
    return block;
}

// The block a `break;` exits to for `loop`: the block after the loop
// container's holder, descending into a construct-leading next block.
const Block* LoopExitBlock(const BlockContainer* loop) {
    const Block* holder = nullptr;
    for (const ILInstruction* p = loop; p; p = p->Parent) {
        holder = dynamic_cast<const Block*>(p);
        if (holder) break;
    }
    if (!holder) return nullptr;
    const BlockContainer* parent = dynamic_cast<const BlockContainer*>(holder->Parent);
    if (!parent) return nullptr;
    const Block* next = nullptr;
    for (std::size_t i = 0; i + 1 < parent->Blocks.size(); ++i)
        if (parent->Blocks[i].get() == holder) { next = parent->Blocks[i + 1].get(); break; }
    if (!next) return nullptr;
    return FirstEmittedBlockOf(next);
}

// Whether `block`'s final transfers control unconditionally (no fall-through
// to the next sibling). Mirrors RemoveUnreachableBlocks' check.
bool FinalIsUnconditionalTransfer(const Block* block) {
    const ILInstruction* fin = block->FinalInstruction.get();
    if (!fin) return false;
    switch (fin->Op) {
    case OpCode::Branch:
    case OpCode::Leave:
    case OpCode::Throw:
    case OpCode::SwitchInstruction:
        return true;
    case OpCode::IfInstruction: {
        const auto* iff = static_cast<const IfInstruction*>(fin);
        if (!iff->FalseInst || iff->FalseInst->Op == OpCode::Nop) return false;
        auto armTransfers = [](const ILInstruction* arm) -> bool {
            if (!arm || arm->Op == OpCode::Nop) return false;
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
        return false;
    }
}

// Replace every Branch to `exit` within `inst`'s subtree (not descending into
// nested loop containers -- their own exit is different) with a
// Leave(container). Returns the count replaced.
int ReplaceExitBranches(ILInstruction* inst, const Block* exit, BlockContainer* container) {
    if (!inst) return 0;
    // A Branch to exit becomes a Leave(container). (ReplaceWith destroys the
    // Branch; do not touch it after.)
    if (auto* br = dynamic_cast<Branch*>(inst)) {
        if (br->TargetBlock == exit) {
            br->ReplaceWith(std::make_unique<Leave>(container));
            return 1;
        }
        return 0;  // a Branch to elsewhere: no children to recurse
    }
    // Don't descend into a nested loop/while/for/dowhile container -- its exit
    // is its own, not this container's. (A nested switch body is fine -- its
    // section branches may target this container's exit.)
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
        if (c->Kind == ContainerKind::Loop || c->Kind == ContainerKind::While ||
            c->Kind == ContainerKind::For || c->Kind == ContainerKind::DoWhile)
            return 0;
    }
    int n = 0;
    int count = inst->ChildCount();
    for (int i = 0; i < count; ++i)
        n += ReplaceExitBranches(inst->GetChild(i), exit, container);
    return n;
}

void ProcessContainer(BlockContainer* c) {
    if (!c || c->Blocks.empty()) return;
    if (c->Kind != ContainerKind::Loop && c->Kind != ContainerKind::While &&
        c->Kind != ContainerKind::For && c->Kind != ContainerKind::DoWhile)
        return;
    const Block* exit = LoopExitBlock(c);
    if (!exit) return;
    // Replace inner Branch-to-exit with Leave(c), but skip the container's OWN
    // entry block's final (the loop header's Leave/branch is the loop's exit,
    // already materialized by LoopDetection; replacing it would be wrong).
    for (std::size_t i = (c->Kind == ContainerKind::Loop) ? 1 : 0; i < c->Blocks.size(); ++i) {
        Block* b = c->Blocks[i].get();
        for (auto& inst : b->Instructions)
            ReplaceExitBranches(inst.get(), exit, c);
        ReplaceExitBranches(b->FinalInstruction.get(), exit, c);
    }
}

} // namespace

void DetectExitPoints::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    if (!function.Body) return;
    // Process every loop-kind container in the tree.
    std::vector<BlockContainer*> loops;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
            if (c->Kind == ContainerKind::Loop || c->Kind == ContainerKind::While ||
                c->Kind == ContainerKind::For || c->Kind == ContainerKind::DoWhile)
                loops.push_back(c);
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(function.Body.get());
    for (BlockContainer* c : loops) ProcessContainer(c);
    // Recurse: nested loops inside the (now-Leave-rewritten) bodies. Run once
    // more if any replacement happened (a nested loop's exit may now be
    // reachable). Bounded to avoid infinite loop.
    // (The single pass above already recurses via the walk; a second pass
    // handles nested loops whose exit was obscured. Keep it simple: one pass
    // suffices because the walk collected ALL loops including nested ones.)
}

} // namespace ILSpy::Decompiler::IL
