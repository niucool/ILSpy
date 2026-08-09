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

#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"

#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkContainers(ILInstruction* inst, const std::function<void(BlockContainer*)>& visit) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) visit(c);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkContainers(inst->GetChild(i), visit);
}

// Try to inline the fall-through block (the next block in the container after
// `block`) into the IfInstruction that is `block`'s final. The fall-through
// must have exactly one predecessor (this block) so inlining doesn't change
// the CFG for any other path. Returns true if a block was inlined.
bool TryInlineIfFallThrough(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex + 1 >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    // Only the simple `if (cond) br target` shape (no else yet).
    if (!iff->TrueInst || iff->TrueInst->Op != OpCode::Branch) return false;
    if (iff->FalseInst) return false;  // already has an else

    Block* fallThrough = container->Blocks[blockIndex + 1].get();
    if (fallThrough->Parent != container) return false;

    // Check that fallThrough has exactly one predecessor via the CFG.
    ControlFlowGraph cfg(container);
    auto* ftNode = cfg.GetNode(fallThrough);
    if (!ftNode || ftNode->Predecessors.size() != 1) return false;
    if (ftNode->Predecessors[0] != cfg.GetNode(block)) return false;

    // Move fallThrough's instructions + final into a new Block that becomes
    // the IfInstruction's FalseInst.
    auto inlineBlock = std::make_unique<Block>();
    Block* inlinePtr = inlineBlock.get();
    for (auto& inst : fallThrough->Instructions)
        inlineBlock->Add(std::move(inst));  // Add sets parent/index
    fallThrough->Instructions.clear();
    if (fallThrough->FinalInstruction)
        inlineBlock->SetFinal(std::move(fallThrough->FinalInstruction));
    fallThrough->FinalInstruction.reset();
    inlinePtr->RenumberChildren();

    // Wire the IfInstruction: TrueInst stays as the Branch (the "else exit"),
    // FalseInst becomes the inlined body.
    iff->FalseInst = std::move(inlineBlock);
    iff->FalseInst->Parent = iff;
    iff->FalseInst->ChildIndex = 2;
    // Renumber iff's children: Condition(0), TrueInst(1), FalseInst(2) -- already set by IfInstruction ctor?
    // IfInstruction doesn't have a RenumberChildren; the ctor set them. FalseInst was null;
    // we set it manually above. The TrueInst and Condition are unchanged.

    // Remove the now-empty fallThrough block from the container.
    container->Blocks.erase(container->Blocks.begin() + (blockIndex + 1));
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        container->Blocks[i]->ChildIndex = static_cast<int>(i);
        container->Blocks[i]->Parent = container;
    }

    return true;
}

} // namespace

void ConditionDetection::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Process each container (Normal, Loop, Switch) in reverse block order so
    // inlining a later block doesn't invalidate earlier indices. Repeat until no
    // more inlinings are possible (a fixpoint: inlining block i+1 into block i
    // may make block i eligible for inlining block i+2 into its new FalseInst,
    // but that's handled by the IfInstruction inside the FalseInst on the next
    // pass over the nested containers).
    WalkContainers(function.Body.get(), [&](BlockContainer* c) {
        bool changed;
        do {
            changed = false;
            for (std::size_t i = c->Blocks.size(); i-- > 0;) {
                if (TryInlineIfFallThrough(c, i)) {
                    changed = true;
                    break;  // restart from the end (indices shifted)
                }
            }
        } while (changed);
    });
}

} // namespace ILSpy::Decompiler::IL
