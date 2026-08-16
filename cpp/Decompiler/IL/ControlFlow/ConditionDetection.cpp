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
#include "Decompiler/IL/Instructions/TryInstructions.hpp"

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

// Walk the Parent chain to the ILFunction root (mirrors the FunctionOf
// helper in ExpressionTransforms.cpp / NullCoalescingTransform.cpp).
ILFunction* FunctionOf(ILInstruction* inst) {
    for (ILInstruction* p = inst; p != nullptr; p = p->Parent)
        if (p->IsRoot()) return static_cast<ILFunction*>(p);
    return nullptr;
}

// Count every Branch targeting `target` across the whole function subtree.
// The C# IncomingEdgeCount is maintained incrementally and counts ALL Branch
// edges (from any container, including nested and sibling containers); the
// per-container ControlFlowGraph only counts edges from the current
// container's blocks, so it misses edges from sibling/nested containers.
// InlineTrueBranch inlines a FORWARD target (not the next block), which is
// more likely to be targeted from elsewhere, so the whole-function count is
// the faithful single-predecessor gate (a per-container count would miss a
// sibling-container edge and dangle a freed block's TargetBlock pointer).
int CountBranchPredecessors(ILFunction* fn, Block* target) {
    if (!fn || !target) return 0;
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* br = dynamic_cast<Branch*>(inst))
            if (br->TargetBlock == target) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn->Body.get());
    return n;
}

// Whether `inst`'s subtree contains a TryInstruction (TryCatch/TryFinally/
// TryFault) -- the raw EH pattern that UsingTransform/LockTransform/
// DetectPinnedRegions fold into `using`/`lock`/`fixed`. Inlining a block
// whose content contains a TryFinally into an if's true arm breaks those
// transforms' block-arrangement recognition (the stloc + TryFinally must sit
// in the right preceding-block shape), so InlineTrueBranch bails on such a
// target. (The Using/Lock/PinnedRegion nodes are produced by transforms that
// run AFTER ConditionDetection, so at ConditionDetection time the target
// carries a TryFinally, not a UsingInstruction/LockInstruction.)
bool ContainsTryInstruction(ILInstruction* inst) {
    if (!inst) return false;
    if (dynamic_cast<TryInstruction*>(inst)) return true;
    for (int i = 0; i < inst->ChildCount(); ++i)
        if (ContainsTryInstruction(inst->GetChild(i))) return true;
    return false;
}

// The C# ConditionDetection.InlineTrueBranch: `if (cond) br trueBlock` ->
// `if (cond) { trueBlock... }` when trueBlock is single-predecessor (strictly
// dominated). Complements TryInlineIfFallThrough (which inlines the
// FALL-THROUGH into the else): InlineTrueBranch inlines the true arm's target
// regardless of position (a forward jump), where TryInlineIfFallThrough +
// TryInvertIfExit require the target to be the next block. Wired as a FALLBACK
// after the fall-through/invert strategy (the port's strategy covers the common
// single-pred-fall-through cases and produces the structure the downstream
// transforms expect); InlineTrueBranch fires only when those bail (a multi-pred
// fall-through with a single-pred forward true-arm target -- the multi-block
// early-exit chain `if (cond) goto X; <throw chain>; X:` where the throw
// chain makes the fall-through 2-pred). RESTRICTED to Normal containers (the
// function body): firing inside Loop/While/For/DoWhile/Switch containers
// restructures the block layout the downstream MatchForLoop/UsingTransform/
// LockTransform detect (the for/using counts dropped when unrestricted), and
// the early-exit chains InlineTrueBranch targets live in the function body.
bool TryInlineTrueBranch(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex >= container->Blocks.size()) return false;
    if (container->Kind != ContainerKind::Normal) return false;  // function body only
    // Bail if the container has any TryInstruction (try/catch/finally) in its
    // subtree: inlining a block changes the block arrangement the downstream
    // UsingTransform/LockTransform/DetectPinnedRegions expect for the stloc +
    // TryFinally pattern, and the broken shape (a `goto` into a try body) is
    // invalid C#. Conservative -- disables InlineTrueBranch for methods with
    // any try/catch/finally -- but safe (the -38 goto win is from methods
    // without EH).
    for (auto& b : container->Blocks)
        if (ContainsTryInstruction(b.get())) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (!iff->TrueInst || iff->TrueInst->Op != OpCode::Branch) return false;
    // Note: the C# CanInline does NOT require no-else -- it only checks whether
    // the true arm is a Branch to a single-pred block. An if with an else can
    // still have its true arm inlined (the else stays).
    auto* br = static_cast<Branch*>(iff->TrueInst.get());
    Block* target = br->TargetBlock;
    if (!target || target->Parent != container) return false;
    // Single-predecessor across the WHOLE function (not just this container):
    // the target must be reached only by this if's true-arm Branch, so inlining
    // it doesn't dangle any other Branch's TargetBlock pointer (no GC; a freed
    // block a Branch still points at is a use-after-free).
    ILFunction* fn = FunctionOf(block);
    if (CountBranchPredecessors(fn, target) != 1) return false;
    // Build a new Block from the target's content (Instructions + the control
    // flow in FinalInstruction) -- the C# sets ifInst.TrueInst = targetBlock
    // (the Block itself); this port's block model splits the control flow into
    // FinalInstruction, so the inlined Block carries both the non-terminals and
    // the final.
    auto inlineBlock = std::make_unique<Block>();
    inlineBlock->StartILOffset = target->StartILOffset;  // label for GetStartILOffset
    for (auto& inst : target->Instructions) inlineBlock->Add(std::move(inst));
    target->Instructions.clear();
    if (target->FinalInstruction) inlineBlock->SetFinal(std::move(target->FinalInstruction));
    target->FinalInstruction.reset();
    inlineBlock->RenumberChildren();
    iff->TrueInst = std::move(inlineBlock);
    iff->TrueInst->Parent = iff;
    iff->TrueInst->ChildIndex = 1;
    // Remove the now-empty target block from the container and re-parent/
    // re-number the remaining blocks.
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == target) {
            container->Blocks.erase(container->Blocks.begin() + i);
            break;
        }
    }
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        container->Blocks[i]->ChildIndex = static_cast<int>(i);
        container->Blocks[i]->Parent = container;
    }
    return true;
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
    // and the fall-through's exit are both Branches to the same block -- so
    // the common-exit drop will fire and the merge is worthwhile. Without this
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
    inlineBlock->StartILOffset = fallThrough->StartILOffset;  // label = the fall-through's first-instruction offset (for GetStartILOffset)
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
    // if (c) t else f (a value IfInstruction used as a condition, e.g. a
    // LogicAnd/LogicOr built by IntroduceShortCircuit) -- negating it is NOT an
    // arm swap (that is only valid when the arms are boolean complements, which
    // a LogicAnd `if (c) cond2 else 0` is not). The C# uses Comp.LogicNot, which
    // wraps the condition as `comp(eq, cond, 0)` (`cond == 0`); that is faithful
    // for every value if. (The arm-swap is correct for a CONTROL-FLOW if, but
    // NegateCondition is only ever called on a condition, never a block final.)
    if (cond->Op == OpCode::IfInstruction) {
        auto zero = (cond->ResultType() == StackType::O)
            ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
            : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
        return std::make_unique<Comp>(std::move(cond), std::move(zero),
                                      ComparisonKind::Equality);
    }
    // Otherwise wrap as logic.not: comp(x == 0) for primitives, comp(x == null)
    // for object-typed conditions (a bare reference used as a boolean).
    auto zero = (cond->ResultType() == StackType::O)
        ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
        : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
    return std::make_unique<Comp>(std::move(cond), std::move(zero),
                                    ComparisonKind::Equality);
}

// Whether `arm` is an empty if-arm: a Nop, or a Block with no instructions
// and a null/Nop final (the C# ConditionDetection.IsEmpty). Mirrors the
// renderer's isEmptyArm but at the ILAst level, for the swap transform.
bool IsEmptyArm(const ILInstruction* arm) {
    if (!arm) return true;
    if (arm->Op == OpCode::Nop) return true;
    if (auto* b = dynamic_cast<const Block*>(arm)) {
        if (!b->Instructions.empty()) return false;
        if (!b->FinalInstruction) return true;
        return b->FinalInstruction->Op == OpCode::Nop;
    }
    return false;
}

// Swap `if (cond) {} else { work }` to `if (!cond) { work }` (negate the
// condition, move the false arm to the true arm, drop the else). The C#
// ConditionDetection.SwapEmptyThen. This puts the work in the true arm so the
// inline/invert transforms (which read the true arm) can restructure it.
bool TrySwapEmptyThen(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (!IsEmptyArm(iff->TrueInst.get())) return false;
    // Only swap when there IS a non-empty else to move (no point swapping two
    // empty arms; and a no-else if with an empty then is already minimal).
    if (IsEmptyArm(iff->FalseInst.get())) return false;
    iff->Condition = NegateCondition(std::move(iff->Condition));
    if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
    iff->TrueInst = std::move(iff->FalseInst);
    iff->FalseInst.reset();
    if (iff->TrueInst) { iff->TrueInst->Parent = iff; iff->TrueInst->ChildIndex = 1; }
    return true;
}

// `if (cond1) { if (cond2) br X }` (no else; the true arm a Block whose final
// is a nested if-goto) -> `if (cond1 && cond2) br X`. The C# ConditionDetection.
// IntroduceShortCircuit. The port's model: the nested if is the true-arm
// Block's FinalInstruction (not Instructions[0] as in the C#). Combining lets
// the following inline/invert transforms see a single if-goto (the `&&`
// condition) instead of a nested if-goto inside a Block.
bool TryIntroduceShortCircuit(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (!IsEmptyArm(iff->FalseInst.get())) return false;  // only a no-else if
    auto* trueBlock = dynamic_cast<Block*>(iff->TrueInst.get());
    if (!trueBlock || !trueBlock->Instructions.empty()) return false;
    auto* nestedIf = dynamic_cast<IfInstruction*>(trueBlock->FinalInstruction.get());
    if (!nestedIf) return false;
    // condition = LogicAnd(iff->Condition, nestedIf->Condition) =
    // if (iff->Condition) nestedIf->Condition else ldc.i4(0).
    auto combined = std::make_unique<IfInstruction>(
        std::move(iff->Condition), std::move(nestedIf->Condition),
        std::make_unique<LdcI4>(0));
    iff->Condition = std::move(combined);
    iff->Condition->Parent = iff;
    iff->Condition->ChildIndex = 0;
    // The true arm becomes the nested if's true arm (e.g. `br X`).
    iff->TrueInst = std::move(nestedIf->TrueInst);
    if (iff->TrueInst) { iff->TrueInst->Parent = iff; iff->TrueInst->ChildIndex = 1; }
    return true;
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

// The C# ConditionDetection.InlineExitBranch: `...; br nextBlock` ->
// `...; { nextBlock... }` -- merge a block whose FinalInstruction is a Branch
// to a single-pred block in the same container. Runs inside the if-restructuring
// loop (the C# `while (InlineTrueBranch || InlineExitBranch)`) to catch merges the
// if-restructuring enables (CFS already ran before ConditionDetection). Only
// fires on a block whose FinalInstruction is a plain Branch (not an IfInstruction
// -- the if-restructuring target), so it doesn't interfere with the if transforms.
bool TryInlineExitBranch(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex >= container->Blocks.size()) return false;
    // Bail in EH-bearing methods: merging a block changes the block arrangement
    // the downstream UsingTransform/LockTransform/DetectPinnedRegions expect
    // for the stloc + TryFinally pattern (the same regression the InlineTrueBranch
    // EH gate prevents -- a using body restructured into a bare try/finally).
    for (auto& b : container->Blocks)
        if (ContainsTryInstruction(b.get())) return false;
    Block* block = container->Blocks[blockIndex].get();
    if (!block->FinalInstruction || block->FinalInstruction->Op != OpCode::Branch) return false;
    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
    Block* target = br->TargetBlock;
    if (!target || target->Parent != container) return false;
    // Single-predecessor across the whole function (the C# IncomingEdgeCount
    // counts ALL Branch edges; a per-container CFG misses parent-container edges).
    ILFunction* fn = FunctionOf(block);
    if (CountBranchPredecessors(fn, target) != 1) return false;
    // Merge: move target's Instructions to block, replace block's final with
    // target's final, remove target from the container.
    for (auto& inst : target->Instructions) block->Add(std::move(inst));
    target->Instructions.clear();
    block->SetFinal(std::move(target->FinalInstruction));
    target->FinalInstruction.reset();
    block->RenumberChildren();
    // Remove the now-empty target block.
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == target) {
            container->Blocks.erase(container->Blocks.begin() + i);
            break;
        }
    }
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        container->Blocks[i]->ChildIndex = static_cast<int>(i);
        container->Blocks[i]->Parent = container;
    }
    return true;
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

// The C# ConditionDetection.OrderIfBlocks: swap the if's then/else arms to
// match IL order when the false arm comes BEFORE the true arm in IL (the C#
// emits the then-branch first, so the if's then should be the earlier-IL
// arm). Runs after the inline/invert loop as a final cleanup. No block
// removal -- just swaps the arms and negates the condition -- so it is safe
// for the downstream Lock/Using/HighLevelLoop transforms (it does not change
// the block structure they detect, unlike InlineTrueBranch which removes a
// block). The C# `if (IsEmpty(ifInst.FalseInst) ||
// GetStartILOffset(ifInst.TrueInst) <= GetStartILOffset(ifInst.FalseInst))
// return;` -- bail when there is no else, or the true arm is already the
// earlier-IL arm.
bool TryOrderIfBlocks(BlockContainer* container, std::size_t blockIndex) {
    if (blockIndex >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (!iff->TrueInst || !iff->FalseInst) return false;
    if (IsEmptyArm(iff->FalseInst.get())) return false;  // no else (C# IsEmpty)
    bool trueEmpty = false, falseEmpty = false;
    int trueOff = ConditionDetection::GetStartILOffset(iff->TrueInst.get(), trueEmpty);
    int falseOff = ConditionDetection::GetStartILOffset(iff->FalseInst.get(), falseEmpty);
    if (trueOff <= falseOff) return false;  // already in IL order
    // Swap the arms + negate the condition (the C# Comp.LogicNot).
    auto oldTrue = std::move(iff->TrueInst);
    iff->TrueInst = std::move(iff->FalseInst);
    iff->FalseInst = std::move(oldTrue);
    if (iff->TrueInst) { iff->TrueInst->Parent = iff; iff->TrueInst->ChildIndex = 1; }
    if (iff->FalseInst) { iff->FalseInst->Parent = iff; iff->FalseInst->ChildIndex = 2; }
    iff->Condition = NegateCondition(std::move(iff->Condition));
    if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
    return true;
}

// IsKeywordExit (C# ConditionDetection.IsKeywordExit): whether an exit
// instruction has a corresponding keyword and thus doesn't strictly need
// merging. Branches are 'continue' (a back-edge to the loop entry) or a
// non-keyword goto; Leave is 'return' (of the function body), 'break' (to a
// non-Normal container), or a non-keyword leave (a try/using/lock exit to a
// Normal container); all other (throw, using, etc.) are 'Other'.
enum class BlockExitKeyword { None, Return, Break, Continue, Other };
bool IsKeywordExit(ILInstruction* exitInst, BlockExitKeyword& keyword) {
    keyword = BlockExitKeyword::Other;
    if (auto* br = dynamic_cast<Branch*>(exitInst)) {
        // A Branch to a loop's entry block (the back-edge target) is a
        // 'continue'. At ConditionDetection time loops are still
        // ContainerKind::Loop (HighLevelLoopTransform converts to While/For
        // later). The C# also checks the for-increment block via
        // HighLevelLoopTransform.GetIncrementBlock; this port does not, so a
        // for-continue (a branch to the increment block, not the entry) is
        // treated as a non-keyword goto. That only diverges from the C# in the
        // rare `if (cond) continue else {throw}` shape where the C# keeps the
        // throw at the tail (continue > other-keyword) and this port would
        // invert (goto > other-keyword); the corpus does not regress.
        if (br->TargetBlock && br->TargetBlock->Parent) {
            auto* loop = dynamic_cast<BlockContainer*>(br->TargetBlock->Parent);
            if (loop && loop->Kind == ContainerKind::Loop && !loop->Blocks.empty() &&
                loop->Blocks.front().get() == br->TargetBlock) {
                keyword = BlockExitKeyword::Continue;
                return true;
            }
        }
        return false;  // a plain goto (non-keyword)
    }
    if (auto* leave = dynamic_cast<Leave*>(exitInst)) {
        // A Leave of the function body is a 'return'.
        if (leave->TargetContainer && leave->TargetContainer->Parent &&
            leave->TargetContainer->Parent->Op == OpCode::ILFunction) {
            keyword = BlockExitKeyword::Return;
            return true;
        }
        // A Leave to a non-Normal container (a loop/switch) is a 'break'.
        if (leave->TargetContainer &&
            leave->TargetContainer->Kind != ContainerKind::Normal) {
            keyword = BlockExitKeyword::Break;
            return true;
        }
        return false;  // a leave to a Normal container (a try/using/lock exit)
    }
    return true;  // Other (throw, using, etc.) -- a keyword exit
}

// CompareBlockExitPriority (C# ConditionDetection.CompareBlockExitPriority):
// {-1, 0, 1} if exit1 has {lower, equal, higher} priority than exit2. A higher-
// priority exit should be kept as the last instruction in a block even if it
// prevents merging two compatible lower-priority exits. Non-keywords
// (a try/using/lock leave, or a goto) outrank keywords; among non-keywords a
// leave outranks a branch. Among keywords (in a non-switch container, the
// only case modeled here) a continue is highest, break is lowest, and Other
// (throw) vs Return are equal (the C# tiebreaks those by IL order, not modeled
// -- PickBetterBlockExit only tests `> 0`, so a 0 tie is a no-invert, matching
// the C# `> 0` test). The switch-block break special-casing and the
// outer-container/leave-arg-offset tiebreakers are not modeled.
int CompareBlockExitPriority(ILInstruction* exit1, ILInstruction* exit2) {
    BlockExitKeyword k1, k2;
    bool isKw1 = IsKeywordExit(exit1, k1);
    bool isKw2 = IsKeywordExit(exit2, k2);
    // Keywords have lower priority than non-keywords.
    if (isKw1 != isKw2) return isKw1 ? -1 : 1;
    if (isKw1) {
        // Both keywords (non-switch): break is lowest, continue is highest.
        if ((k1 == BlockExitKeyword::Break) != (k2 == BlockExitKeyword::Break))
            return k1 == BlockExitKeyword::Break ? -1 : 1;
        if ((k1 == BlockExitKeyword::Continue) != (k2 == BlockExitKeyword::Continue))
            return k1 == BlockExitKeyword::Continue ? 1 : -1;
        return 0;  // Other vs Return, or the same kind
    }
    // Both non-keywords (only a Branch or a Leave). A non-keyword Branch (goto)
    // has lower priority than a non-keyword Leave (a try/using/lock exit).
    bool isBr1 = exit1 && exit1->Op == OpCode::Branch;
    bool isBr2 = exit2 && exit2->Op == OpCode::Branch;
    if (isBr1 != isBr2) return isBr1 ? -1 : 1;
    return 0;  // the outer-container/IL-order tiebreakers are not modeled
}

// TryPickBetterBlockExit (C# ConditionDetection.PickBetterBlockExit): when an
// if has no else and its true arm exits, compare the true-arm exit's priority
// to the block's fall-through exit; if the true-arm exit outranks it, invert
// the if (ConditionDetection::InvertIf) so the high-priority exit becomes the
// block tail (rendered as a natural keyword) and the low-priority exit becomes
// the if's branch (which a following transform may then drop or merge). This
// is the case TryInlineIfFallThrough's CompatibleCommonExit gate (D203) blocks
// -- a Block true arm whose exit and the fall-through's exit don't branch to
// the same block -- so without PickBetterBlockExit the goto stays in the true
// arm. Bails in EH-bearing containers: the invert moves the fall-through
// block's content into the if's true arm, and if that content is a
// try/using/lock body the downstream UsingTransform/LockTransform block
// arrangement breaks (the same regression the InlineTrueBranch/
// InlineExitBranch EH gates prevent).
bool TryPickBetterBlockExit(BlockContainer* container, std::size_t blockIndex) {
    // Normal containers only (the function body): the invert moves the
    // fall-through block's content into the if's true arm, which disrupts the
    // block arrangement the downstream HighLevelLoopTransform/LoopDetection
    // expect for loop bodies (Loop/While/For/DoWhile containers) -- inverting
    // a loop-body if breaks the back-edge/increment shape and the loop falls
    // back to a goto. Same restriction as InlineTrueBranch (D207).
    if (container->Kind != ContainerKind::Normal) return false;
    if (blockIndex + 1 >= container->Blocks.size()) return false;
    Block* block = container->Blocks[blockIndex].get();
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (iff->FalseInst) return false;  // has an else (C# IsEmpty(falseInst))
    if (!iff->TrueInst) return false;
    // The true arm must exit (EndPointUnreachable).
    ILInstruction* trueExit = TrailingExit(iff->TrueInst.get());
    if (!trueExit || !HasFlag(trueExit->Flags(), InstructionFlags::EndPointUnreachable))
        return false;
    Block* nextBlock = container->Blocks[blockIndex + 1].get();
    if (nextBlock->Parent != container) return false;
    ILInstruction* blockExit = nextBlock->FinalInstruction.get();
    if (!blockExit) return false;
    // EH gate: the invert moves nextBlock's content into the if's true arm;
    // bail if the container carries a try (the using/lock pattern).
    for (auto& b : container->Blocks)
        if (ContainsTryInstruction(b.get())) return false;
    if (CompareBlockExitPriority(trueExit, blockExit) <= 0) return false;
    // The fall-through next block must be single-pred (only this block's
    // fall-through) so the move is semantics-preserving; InvertIf re-checks
    // this via CountBranchPredecessors and bails if multi-pred.
    if (CountBranchPredecessors(FunctionOf(block), nextBlock) != 0) return false;
    ConditionDetection::InvertIf(block, iff);
    return true;
}

} // namespace
// (a Value that is neither null nor a Nop -- this port's reader emits
// `Leave(container)` with no Value for a void leave, and a Nop Value for a
// `leave (nop)` artifact) reports its Value's offset; otherwise the
// instruction's own StartILOffset is returned. `isEmpty` reports whether the
// effective range is empty.
//
// Block adaptation: this port's `Block` shadows the base `ILInstruction`'s
// `StartILOffset` with its own `uint32` field (the block's label, set by the
// reader/BlockBuilder to the first instruction's offset), and the base ILRange
// (StartILOffset/EndILOffset) is NOT propagated through transforms that
// synthesize or merge blocks (ConditionDetection's inversion, CFS, ...), so a
// Block's base ILRange is usually empty. The C# `GetStartILOffset` for a Block
// returns the Block's ILRange start (= the first instruction's offset = the
// label); this port returns the Block's label field for a Block, which is the
// faithful equivalent (the label is always a valid offset, set by the
// reader/BlockBuilder for real blocks and by TryInlineIfFallThrough for the
// synthesized inline Block).
int ConditionDetection::GetStartILOffset(ILInstruction* inst, bool& isEmpty) {
    if (auto* leave = dynamic_cast<Leave*>(inst)) {
        if (leave->Value && leave->Value->Op != OpCode::Nop) {
            isEmpty = leave->Value->IsILRangeEmpty();
            return leave->Value->StartILOffset;
        }
    }
    if (auto* block = dynamic_cast<Block*>(inst)) {
        // The Block's label (its own StartILOffset field) is the first
        // instruction's offset -- the faithful equivalent of the C# Block's
        // ILRange start. The base ILRange is not propagated through
        // block-synthesizing transforms, so consult the label.
        isEmpty = false;
        return static_cast<int>(block->StartILOffset);
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
    // block as the if, so it is single-pred by construction. This port's next
    // block is the positional fall-through (the if has no else, so the block
    // falls through to it), so its total incoming edges = 1 (this fall-through)
    // + any Branch edges targeting it; single-pred is therefore "no Branch
    // edges target it" -- `CountBranchPredecessors == 0`. The whole-function
    // count is used (not the per-container `IncomingEdgeCount`, which is stale
    // mid-fixpoint when InvertIf is called from PickBetterBlockExit inside
    // ConditionDetection::Run; for the post-ConditionDetection callers
    // ReduceNestingTransform/HighLevelLoopTransform the count is fresh, and
    // `CountBranchPredecessors == 0` is equivalent to `IncomingEdgeCount == 1`
    // for a fall-through next block).
    if (CountBranchPredecessors(FunctionOf(block), nextBlock) != 0) return;

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
            // Swap empty-then first: `if (cond) {} else { work }` ->
            // `if (!cond) { work }`, so the work is in the true arm for the
            // inline/invert transforms (which read the true arm).
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TrySwapEmptyThen(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryIntroduceShortCircuit(c, i)) { changed = true; break; }
            }
            if (changed) continue;
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
            if (changed) continue;
            // PickBetterBlockExit (the C# runs it inside the HandleIfInstruction
            // loop after each inline + once after): invert an if with no else
            // when its true-arm exit outranks the block's fall-through exit, so
            // the high-priority exit becomes the block tail. Runs AFTER the
            // inline/invert/drop transforms so the bare-Branch cases they
            // handle (InlineIfFallThrough + InvertIfExit) are already
            // restructured; PickBetterBlockExit is then the fallback for the
            // Block-true-arm case whose exit and the fall-through's exit don't
            // branch to the same block (InlineIfFallThrough's CompatibleCommonExit
            // gate, D203, blocks it). Normal containers only -- inverting a
            // loop-body if breaks the back-edge/increment shape (D207 precedent).
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryPickBetterBlockExit(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            // OrderIfBlocks: swap if/else arms to match IL order (the C# runs
            // it after the inline/invert loop as a final cleanup). Safe -- no
            // block removal, just an arm swap + condition negate.
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryOrderIfBlocks(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            // InlineTrueBranch (fallback, Normal containers only): inline the
            // true arm's single-pred forward target when the fall-through/invert
            // strategy bailed -- the multi-block early-exit chain. Restricted to
            // Normal containers so it does not restructure loop/using/lock
            // bodies the downstream transforms detect.
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryInlineTrueBranch(c, i)) { changed = true; break; }
            }
            if (changed) continue;
            // InlineExitBranch: merge a block whose final is a single-pred `br
            // nextBlock` (the C# HandleIfInstruction loop's unconditional-branch
            // inline, catching merges the if-restructuring enables).
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (TryInlineExitBranch(c, i)) { changed = true; break; }
            }
        } while (changed);
    });
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
