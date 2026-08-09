// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

BlockContainer* ParentContainerOf(Block* b) {
    return b ? dynamic_cast<BlockContainer*>(b->Parent) : nullptr;
}

// Collect every BlockContainer in the tree (the function body and any nested
// loop/try/switch containers the transforms introduced).
void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

// Match the source block of the infeasible-path pattern:
//   b0: stloc s(ldc.i4 0|1); br b1
// where s is a StackSlot. Returns the stored constant and the branch target.
bool MatchBlock1(Block* block, ILVariable*& s, int& constantValue, Block*& target) {
    s = nullptr;
    constantValue = 0;
    target = nullptr;
    if (block->Instructions.size() != 1) return false;
    auto* stloc = dynamic_cast<StLoc*>(block->Instructions[0].get());
    if (!stloc || !stloc->Value) return false;
    if (!stloc->Variable || stloc->Variable->Kind != VariableKind::StackSlot) return false;
    auto* ldc = dynamic_cast<LdcI4*>(stloc->Value.get());
    if (!ldc || (ldc->Value != 0 && ldc->Value != 1)) return false;
    auto* br = dynamic_cast<Branch*>(block->FinalInstruction.get());
    if (!br || !br->TargetBlock) return false;
    s = stloc->Variable.get();
    constantValue = ldc->Value;
    target = br->TargetBlock;
    return true;
}

// Match the test block of the infeasible-path pattern and pick the feasible
// exit for the known constant:
//   b1 (>1 pred): if (cond) br X; [fall through to Y]
// cond is either ldloc s (brtrue) or comp(eq, ldloc s, 0) (brfalse = logic.not).
// The feasible exit is the arm the constant drives the test to:
//   brtrue  (cond = ldloc s):      s!=0 -> X,  s==0 -> Y
//   brfalse (cond = comp(eq,s,0)): s==0 -> X,  s!=0 -> Y
// X is the if's TrueInst target; Y is the next block in b1's container (the
// implicit fall-through this port's block model carries).
bool MatchBlock2AndPickExit(Block* block, ILVariable* s, int constantValue,
                            Block*& exit) {
    exit = nullptr;
    if (block->IncomingEdgeCount <= 1) return false;
    if (!block->Instructions.empty()) return false;
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (iff->FalseInst) return false;  // the if must be a pure fall-through (no else)
    // The condition must test s, either bare (brtrue) or as logic.not (brfalse).
    bool negated = false;
    ILInstruction* condInner = nullptr;
    if (auto* comp = dynamic_cast<Comp*>(iff->Condition.get())) {
        if (comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
        auto* rhs = dynamic_cast<LdcI4*>(comp->Right.get());
        if (!rhs || rhs->Value != 0) return false;
        condInner = comp->Left.get();
        negated = true;
    } else {
        condInner = iff->Condition.get();
        negated = false;
    }
    auto* load = dynamic_cast<LdLoc*>(condInner);
    if (!load || load->Variable.get() != s) return false;
    // The if's true arm must be a resolved Branch (to X).
    auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
    if (!trueBr || !trueBr->TargetBlock) return false;
    Block* X = trueBr->TargetBlock;
    // Y is the next block in b1's container (the implicit fall-through).
    BlockContainer* container = ParentContainerOf(block);
    if (!container) return false;
    std::size_t b1idx = 0;
    bool found = false;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) { b1idx = i; found = true; break; }
    }
    if (!found) return false;
    Block* Y = (b1idx + 1 < container->Blocks.size()) ? container->Blocks[b1idx + 1].get() : nullptr;
    bool conditionTrue = negated ? (constantValue == 0) : (constantValue != 0);
    if (conditionTrue) {
        exit = X;
    } else {
        if (!Y) return false;  // no fall-through to redirect to
        exit = Y;
    }
    return true;
}

// True if `target` (at index i > 0 in its container) is reached by positional
// fall-through: the previous block's final is not EndPointUnreachable. Such a
// block is reachable with zero Branch edges and must not be deleted as dead.
bool IsFallThroughTarget(BlockContainer* container, std::size_t i) {
    if (i == 0 || !container || i >= container->Blocks.size()) return false;
    ILInstruction* prevFin = container->Blocks[i - 1]->FinalInstruction.get();
    return !prevFin || !HasFlag(prevFin->Flags(), InstructionFlags::EndPointUnreachable);
}

} // namespace

void RemoveInfeasiblePathTransform::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);

    bool changed = false;
    for (auto* container : containers) {
        for (std::size_t bi = 0; bi < container->Blocks.size(); ++bi) {
            Block* block = container->Blocks[bi].get();
            ILVariable* s = nullptr;
            int constantValue = 0;
            Block* target = nullptr;
            if (!MatchBlock1(block, s, constantValue, target)) continue;
            Block* exit = nullptr;
            if (!MatchBlock2AndPickExit(target, s, constantValue, exit)) continue;
            context.StepOnce("RemoveInfeasiblePath");
            // Redirect block's `br target` straight to the feasible exit; the
            // dead constant store is dropped (block's path no longer reaches
            // the test).
            block->Instructions.clear();
            block->RenumberChildren();
            block->SetFinal(std::make_unique<Branch>(exit));
            changed = true;
        }
    }

    if (changed) {
        RecomputeIncomingEdgeCounts(function);
        // A test block whose every predecessor was redirected is now unreachable;
        // drop it (mirrors C# SortBlocks(deleteUnreachableBlocks: true)). Keep
        // the container entry point (index 0) and positional fall-through targets.
        std::vector<BlockContainer*> freshContainers;
        CollectContainers(function.Body.get(), freshContainers);
        // Keep dead blocks alive until the function returns so snapshot pointers
        // that might still be observed (debug dumps, the invariant walk) stay
        // valid; they are released when `graveyard` goes out of scope.
        std::vector<std::unique_ptr<Block>> graveyard;
        for (auto* container : freshContainers) {
            for (std::size_t i = 0; i < container->Blocks.size();) {
                Block* b = container->Blocks[i].get();
                if (b->IncomingEdgeCount == 0 && i != 0 && !IsFallThroughTarget(container, i)) {
                    graveyard.push_back(std::move(container->Blocks[i]));
                    container->Blocks.erase(container->Blocks.begin() + i);
                    // Don't advance i: the next block shifted into this slot.
                } else {
                    ++i;
                }
            }
            for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
                container->Blocks[i]->Parent = container;
                container->Blocks[i]->ChildIndex = static_cast<int>(i);
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
