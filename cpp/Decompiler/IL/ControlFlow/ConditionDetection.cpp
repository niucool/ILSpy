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
#include "Decompiler/IL/Instructions/LdNull.hpp"
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

// The trailing exit instruction of an if-arm or block: a Block's final, or
// the arm itself if it is a bare exit. nullptr if it is not an exit
// (Branch/Leave/Throw -- something EndPointUnreachable).
ILInstruction* TrailingExit(ILInstruction* arm) {
    if (!arm) return nullptr;
    if (auto* b = dynamic_cast<Block*>(arm)) return b->FinalInstruction.get();
    return arm;
}

// Two exits are compatible for a common-exit merge if both are Branches to the
// same block (the shared-tail pattern). Leaves/throws are not merged here.
bool CompatibleCommonExit(ILInstruction* e1, ILInstruction* e2) {
    if (!e1 || !e2 || e1->Op != OpCode::Branch || e2->Op != OpCode::Branch) return false;
    return static_cast<Branch*>(e1)->TargetBlock == static_cast<Branch*>(e2)->TargetBlock;
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
    // The if must have no else yet, and its true arm must be unreachable
    // (a bare Branch, or a Block ending in a goto/leave/throw) so the if's
    // fall-through is only the cond-false path -- inlining the fall-through
    // into the else is then semantics-preserving.
    if (!iff->TrueInst || !HasFlag(iff->TrueInst->Flags(), InstructionFlags::EndPointUnreachable)) return false;
    if (iff->FalseInst) return false;  // already has an else

    Block* fallThrough = container->Blocks[blockIndex + 1].get();
    if (fallThrough->Parent != container) return false;

    // Check that fallThrough has exactly one predecessor via the CFG.
    ControlFlowGraph cfg(container);
    auto* ftNode = cfg.GetNode(fallThrough);
    if (!ftNode || ftNode->Predecessors.size() != 1) return false;
    if (ftNode->Predecessors[0] != cfg.GetNode(block)) return false;

    // Gate: when the true arm is NOT a bare Branch (i.e. it is a Block ending
    // in an exit, the post-invert shape), only inline if the true arm's exit
    // and the fall-through's exit are both Branches to the same block -- so the
    // common-exit drop will fire and the merge is worthwhile. Without this
    // gate the post-invert inline would wrap the "rest of the method" in an
    // else (the early-exit pattern: true arm is a throw, fall-through is the
    // happy path). A bare Branch (the `if-goto` shape) is the first inline that
    // enables the invert -- no gate.
    if (iff->TrueInst->Op != OpCode::Branch) {
        if (!CompatibleCommonExit(TrailingExit(iff->TrueInst.get()),
                                  fallThrough->FinalInstruction.get()))
            return false;
    }

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
    // Otherwise wrap as logic.not: comp(x == 0) for primitives, comp(x == null)
    // for object-typed conditions (a bare reference used as a boolean).
    auto zero = (cond->ResultType() == StackType::O)
        ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
        : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
    return std::make_unique<Comp>(std::move(cond), std::move(zero),
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

// Get the trailing Branch of an if-arm (a Block's final, or the arm itself if
// it is a bare Branch). nullptr if the arm does not end in a Branch.
Branch* TrailingBranch(ILInstruction* arm) {
    if (!arm) return nullptr;
    if (auto* b = dynamic_cast<Block*>(arm))
        return dynamic_cast<Branch*>(b->FinalInstruction.get());
    return dynamic_cast<Branch*>(arm);
}

// Drop the trailing Branch from an if-arm. A Block arm has its FinalInstruction
// nulled (it falls through to after the if); a bare Branch arm becomes an
// empty Block (an arm must not be null).
void DropTrailingBranch(std::unique_ptr<ILInstruction>& arm) {
    if (!arm) return;
    if (auto* b = dynamic_cast<Block*>(arm.get())) {
        b->FinalInstruction.reset();
        b->RenumberChildren();
    } else {
        auto empty = std::make_unique<Block>();
        empty->Parent = arm->Parent;
        empty->ChildIndex = arm->ChildIndex;
        arm = std::move(empty);
    }
}

// `if (cond) { ...; goto X } else { ...; goto X }` where X is the next block:
// drop both gotos. The block falls through to X, and the if arms fall through
// to after the if (then to X). This is the shared-tail / common-exit merge.
bool TryDropCommonExit(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex + 1 >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff || !iff->TrueInst || !iff->FalseInst) return false;
    auto* tb = TrailingBranch(iff->TrueInst.get());
    auto* fb = TrailingBranch(iff->FalseInst.get());
    if (!tb || !fb || !tb->TargetBlock) return false;
    if (tb->TargetBlock != fb->TargetBlock) return false;
    // X must be the next block so the block falls through to it after the merge.
    if (container->Blocks[blockIndex + 1].get() != tb->TargetBlock) return false;
    DropTrailingBranch(iff->TrueInst);
    DropTrailingBranch(iff->FalseInst);
    return true;
}

// Drop a trailing Branch from a single if-arm when it targets the next block
// in the container (the fall-through). `if (cond) { ...; goto X }` where X is
// the next block: the goto is redundant -- the arm falls through to after the
// if, then the block falls through to X. Handles one arm at a time so the
// fixpoint re-processes. A bare-Branch arm (no body) that targets the next
// block becomes a null arm (the if falls through on that path).
bool TryDropTrailingGotoToNext(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex + 1 >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    Block* nextBlock = container->Blocks[blockIndex + 1].get();
    if (iff->TrueInst) {
        if (auto* br = TrailingBranch(iff->TrueInst.get())) {
            if (br->TargetBlock == nextBlock) {
                DropTrailingBranch(iff->TrueInst);
                return true;
            }
        }
    }
    if (iff->FalseInst) {
        if (auto* br = TrailingBranch(iff->FalseInst.get())) {
            if (br->TargetBlock == nextBlock) {
                DropTrailingBranch(iff->FalseInst);
                return true;
            }
        }
    }
    return false;
}

// The next block in `block`'s container after `block` (the positional
// fall-through). nullptr if `block` is not in a container's Blocks list or is
// the last block. Mirrors the helper in PatternMatchingTransform /
// SwitchAnalysis / CachedDelegateInitialization.
Block* NextBlockInContainer(Block* block) {
    if (!block) return nullptr;
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block)
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
    }
    return nullptr;
}

} // namespace

// The C# `ConditionDetection.GetStartILOffset` (see header). A valued Leave
// (a Value that is neither null nor a Nop -- this port's reader emits
// `Leave(container)` with no Value for a void leave, and a Nop Value for a
// `leave (nop)` artifact) reports its Value's offset; otherwise the
// instruction's own StartILOffset is returned. `isEmpty` reports whether the
// effective range is empty.
int ConditionDetection::GetStartILOffset(ILInstruction* inst, bool& isEmpty) {
    if (auto* leave = dynamic_cast<Leave*>(inst)) {
        if (leave->Value && leave->Value->Op != OpCode::Nop) {
            isEmpty = leave->Value->IsILRangeEmpty();
            return leave->Value->StartILOffset;
        }
    }
    isEmpty = inst ? inst->IsILRangeEmpty() : true;
    return inst ? inst->StartILOffset : 0;
}

// The C# `ConditionDetection.InvertIf` (see header). The C# reads `ifInst`
// as a non-terminal at `block.Instructions[i]` with the `falseCode...; exit`
// as sibling instructions after it; this port makes the `IfInstruction` the
// block's `FinalInstruction`, so the `falseCode...; exit` is the next block in
// the container. The next block must be single-predecessor (only this block's
// fall-through) so moving its content into the if's TrueInst and the old then
// into the next block is semantics-preserving.
void ConditionDetection::InvertIf(Block* block, IfInstruction* ifInst) {
    if (!block || !ifInst) return;
    // `ifInst` must be the block's FinalInstruction (the C# `ifInst.Parent == block`).
    if (block->FinalInstruction.get() != ifInst) return;
    // No else (the C# `IsEmpty(ifInst.FalseInst)` -- a null FalseInst is this
    // port's "no else", since the reader emits a void if with a null FalseInst
    // rather than a Nop FalseInst).
    if (ifInst->FalseInst) return;
    // The then must exit (the C# `ifInst.TrueInst.HasFlag(EndPointUnreachable)`).
    if (!ifInst->TrueInst) return;
    if (!HasFlag(ifInst->TrueInst->Flags(), InstructionFlags::EndPointUnreachable)) return;

    Block* nextBlock = NextBlockInContainer(block);
    if (!nextBlock) return;  // no falseCode; degenerate

    // The next block must be single-predecessor (only this block's fall-through)
    // so the move is semantics-preserving. The C# has the falseCode in the same
    // block as the if, so it is single-pred by construction; this port checks
    // `IncomingEdgeCount == 1` explicitly (RecomputeIncomingEdgeCounts counts
    // the positional fall-through edge, so a 1-pred next block is one with no
    // Branch edges targeting it).
    if (nextBlock->IncomingEdgeCount != 1) return;

    // Save the old TrueInst (then). Detach it before the slot is reassigned.
    auto thenOwned = std::move(ifInst->TrueInst);

    // Build the if's new TrueInst from the next block's content.
    //   Case 1 (falseCode non-empty): wrap the next block's content in a Block
    //     (the C# `ExtractBlock` into a `new Block()`).
    //   Case 2 (no falseCode, just the exit): the if's then is the bare exit
    //     (the C# `ifInst.TrueInst = exitInst`).
    if (nextBlock->Instructions.empty()) {
        ifInst->TrueInst = std::move(nextBlock->FinalInstruction);  // the bare exit
    } else {
        auto newBlock = std::make_unique<Block>();
        for (auto& inst : nextBlock->Instructions) newBlock->Add(std::move(inst));
        nextBlock->Instructions.clear();
        if (nextBlock->FinalInstruction) newBlock->SetFinal(std::move(nextBlock->FinalInstruction));
        newBlock->RenumberChildren();
        ifInst->TrueInst = std::move(newBlock);
    }
    if (ifInst->TrueInst) { ifInst->TrueInst->Parent = ifInst; ifInst->TrueInst->ChildIndex = 1; }
    nextBlock->FinalInstruction.reset();

    // Move the old then into the next block (the "after the if" position). The
    // C# `block.Instructions.AddRange(thenBlock.Instructions)` (then-Block) or
    // `block.Instructions.Add(thenInst)` (bare exit); this port's then-Block
    // carries the control flow in `FinalInstruction` (not in `Instructions` as
    // in the C#), so both the non-terminals and the final move across.
    if (auto* thenBlock = dynamic_cast<Block*>(thenOwned.get())) {
        for (auto& inst : thenBlock->Instructions) nextBlock->Add(std::move(inst));
        if (thenBlock->FinalInstruction) nextBlock->SetFinal(std::move(thenBlock->FinalInstruction));
    } else {
        nextBlock->SetFinal(std::move(thenOwned));  // then is a bare exit -> next block's final
    }
    nextBlock->RenumberChildren();

    // Negate the condition. The C# `Comp.LogicNot` (a non-folding wrap) +
    // `ExpressionTransforms.RunOnSingleStatement` (the re-visit that folds);
    // `NegateCondition` folds directly, giving the same net result without the
    // re-visit (RunOnSingleStatement is not ported; the wired
    // ImproveILOrdering/ReduceNesting folds handle the re-visit if needed).
    ifInst->Condition = NegateCondition(std::move(ifInst->Condition));
    if (ifInst->Condition) { ifInst->Condition->Parent = ifInst; ifInst->Condition->ChildIndex = 0; }
}

void ConditionDetection::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Run the three transforms in a combined fixpoint so a step that enables
    // another (inline a post-invert fall-through; drop a common exit after an
    // inline) is picked up on the next iteration. Try inline first (reverse
    // block order), then invert, then drop-common-exit; restart on any change.
    WalkContainers(function.Body.get(), [&](BlockContainer* c) {
        bool changed;
        do {
            changed = false;
            for (std::size_t i = c->Blocks.size(); i-- > 0;) {
                if (TryInlineIfFallThrough(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryInvertIfExit(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryDropCommonExit(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryDropTrailingGotoToNext(c, i)) { changed = true; break; }
            }
        } while (changed);
    });
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
