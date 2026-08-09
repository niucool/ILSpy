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
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
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

// Negate a boolean condition, folding into a Comp when possible (mirrors
// Comp.LogicNot + ExpressionTransforms.VisitLogicNot). logic.not(comp(a op b))
// -> comp(a op.Negate b); logic.not(logic.not x) -> x; otherwise wrap as
// comp(x == 0).
std::unique_ptr<ILInstruction> NegateCondition(std::unique_ptr<ILInstruction> cond) {
    if (!cond) return cond;
    if (auto* comp = dynamic_cast<Comp*>(cond.get())) {
        // logic.not(comp(inner == 0)) is comp(Equality, inner, ldc.i4 0): unwrap.
        if (comp->Kind == ComparisonKind::Equality && comp->Right &&
            comp->Right->Op == OpCode::LdcI4) {
            if (static_cast<LdcI4*>(comp->Right.get())->Value == 0) {
                return std::move(comp->Left);
            }
        }
        comp->Kind = NegateComparison(comp->Kind);
        return cond;
    }
    // if (c) t else f  ->  if (c) f else t  (swap branches negates the condition)
    if (auto* iff = dynamic_cast<IfInstruction*>(cond.get())) {
        auto t = std::move(iff->TrueInst);
        iff->TrueInst = std::move(iff->FalseInst);
        iff->FalseInst = std::move(t);
        if (iff->TrueInst) iff->TrueInst->ChildIndex = 1;
        if (iff->FalseInst) iff->FalseInst->ChildIndex = 2;
        return cond;
    }
    return std::make_unique<Comp>(std::move(cond), std::make_unique<LdcI4>(0),
                                    ComparisonKind::Equality);
}

// Invert `if (cond) br X else { exit }` to `if (!cond) { exit }` (fall-through
// to X) when X is the next block in the container. Eliminates the goto to X.
// The else must be an exit (EndPointUnreachable) so the true arm after the
// swap is the sole terminating path; the false arm falls through.
bool TryInvertIfExit(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex + 1 >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (!iff->TrueInst || iff->TrueInst->Op != OpCode::Branch) return false;
    if (!iff->FalseInst) return false;
    if (!HasFlag(iff->FalseInst->Flags(), InstructionFlags::EndPointUnreachable)) return false;
    auto* br = static_cast<Branch*>(iff->TrueInst.get());
    Block* target = br->TargetBlock;
    if (!target || container->Blocks[blockIndex + 1].get() != target) return false;

    // Negate condition, move the exit into the true arm, drop the goto.
    iff->Condition = NegateCondition(std::move(iff->Condition));
    if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
    iff->TrueInst = std::move(iff->FalseInst);  // exit becomes the true arm
    if (iff->TrueInst) { iff->TrueInst->Parent = iff; iff->TrueInst->ChildIndex = 1; }
    iff->FalseInst.reset();  // drop the goto; fall through to target
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
        // Invert if-goto-else-exit into if-not-cond-exit when the goto target is
        // the fall-through block, eliminating the goto. Iterate to a fixpoint so
        // a chain of if-throw checks collapses one block at a time.
        bool inverted;
        do {
            inverted = false;
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryInvertIfExit(c, i)) {
                    inverted = true;
                    break;
                }
            }
        } while (inverted);
    });
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
