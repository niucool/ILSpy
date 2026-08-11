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

// Port of ICSharpCode.Decompiler/IL/Transforms/ReduceNestingTransform.cs.
//
// Three pieces are ported so far, plus a fourth tested foundation:
//
//  (1) EliminateRedundantTryFinally: the C# compiler sometimes generates a
//      try-finally around a `fixed` statement; once DetectPinnedRegions has
//      formed the PinnedRegion the try-finally is redundant (its finally is an
//      empty `leave (nop)`) and is replaced with the PinnedRegion directly:
//        .try BlockContainer { Block { PinnedRegion ... } }
//        .finally BlockContainer { Block { leave IL_xxxx (nop) } }  ==>  PinnedRegion ...
//
//  (2) The nesting-reduction heuristics (ComputeStats / UpdateStats /
//     ShouldReduceNesting) plus the self-contained pattern helpers
//     (BlockUnwrap / MatchBranch / MatchLeave / MatchConditionBlock), ported
//     as a tested foundation for the deferred ReduceNesting / ReduceSwitchNesting /
//     ExtractElseBlock folds.
//
//  (3) ImproveILOrdering (the IL-order-gated InvertIf): the wired fold that
//      re-inverts ConditionDetection's inversion when the IL order is wrong.
//      See the ImproveILOrdering comment below for the block-model adaptation.
//      The trailing-leave handling (CanDuplicateExit) is deferred; the
//      bail-for-non-keyword-Leave-exit guard is ported.
//
//  (4) CanDuplicateExit (a tested-but-not-yet-wired foundation): the helper
//      that decides whether an exit is a duplicable keyword exit
//      (return/break/continue), walking out of a try/pinned/lock container to
//      the following instruction when the exit is a leave of a Normal
//      container. The wired ImproveILOrdering trailing-leave handling and the
//      deferred ReduceNesting / ReduceSwitchNesting folds consult it; no
//      pipeline transform consults it yet (the wired folds are the subsequent
//      iterations).
//  (5) GetElseIfParent (a tested-but-not-yet-wired foundation): the
//      pure-analysis helper that determines whether an IfInstruction is an
//      else-if and reports the preceding parent IfInstruction. The deferred
//      ReduceNesting else-if fold consults it; no pipeline transform consults
//      it yet.
//  (6) EnsureEndPointUnreachable (a tested-but-not-yet-wired foundation): the
//      helper that ensures a block's end point is unreachable by duplicating
//      the [exit] instruction following the end point. The wired ReduceNesting
//      fold (the no-else and else-if-tree cases) consults it to make a
//      then/else block exit before InvertIf swaps it with the fall-through; no
//      pipeline transform consults it yet (the wired fold is the subsequent
//      iteration).
//  (7) RemoveRedundantExit (a tested-but-not-yet-wired foundation): the helper
//      that drops a block's trailing exit when it equals the fall-through. The
//      wired ReduceNesting fold calls it after a successful fold; no pipeline
//      transform consults it yet (the wired fold is the subsequent iteration).
//  (8) ExtractElseBlock (a tested-but-not-yet-wired foundation): the helper
//      that extracts an if's else block -- moves the else block whole into the
//      container after Block A (the if's block) and clears the if's else. The
//      wired ReduceNesting else-if-tree fold calls it after making the then
//      exit; no pipeline transform consults it yet (the wired fold is the
//      subsequent iteration).
//
// Block-model adaptation: the C# Block.Instructions does NOT include the
// FinalInstruction (a void block's final is a Nop, and the control flow
// Leave/Branch are non-terminals in Instructions). This port splits the block's
// non-terminal Instructions from its FinalInstruction (the control flow), so
// the C# `block.Instructions.Last()` (the last non-terminal, which is the
// control flow in the C#) is this port's `block->FinalInstruction`. The
// heuristics and ImproveILOrdering are adapted accordingly.

#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"

#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `inst.MatchNop()` -- a Nop, or (in this port) a null slot (the reader
// emits `Leave(container)` with no Value for a void leave rather than a Nop).
bool IsNop(const ILInstruction* inst) {
	return !inst || inst->Op == OpCode::Nop;
}

// The C# `inst.MatchLeave(container)` -- a Leave targeting `container` whose
// Value is a Nop (a value-less leave of that container).
bool MatchLeave(ILInstruction* inst, const BlockContainer* container) {
	auto* leave = dynamic_cast<Leave*>(inst);
	return leave && leave->TargetContainer == container && IsNop(leave->Value.get());
}

// The C# `inst.MatchBranch(target)` -- a Branch to `target`.
bool MatchBranch(ILInstruction* inst, const Block* target) {
	auto* br = dynamic_cast<Branch*>(inst);
	return br && br->TargetBlock == target;
}

// The C# `inst.MatchBranch(out target)` -- a Branch; reports the TargetBlock.
bool MatchBranch(ILInstruction* inst, Block*& target) {
	auto* br = dynamic_cast<Branch*>(inst);
	if (!br || !br->TargetBlock) return false;
	target = br->TargetBlock;
	return true;
}

// The C# `Block.Unwrap(inst)` -- a Block with a single instruction (one
// non-terminal + a Nop final) unwraps to that instruction. This port's block
// model puts the control flow in FinalInstruction (not Instructions), so the
// "single-instruction expression block" is one with Instructions.size()==1 and
// a null/Nop FinalInstruction. A bare non-Block instruction passes through.
ILInstruction* BlockUnwrap(ILInstruction* inst) {
	if (!inst) return inst;
	if (auto* b = dynamic_cast<Block*>(inst)) {
		if (b->Instructions.size() == 1 && IsNop(b->FinalInstruction.get()))
			return b->Instructions[0].get();
	}
	return inst;
}

// If `inst` is a Block wrapping a single IfInstruction (an else-if), return
// that IfInstruction. Handles both block models: the C# model (the if is the
// sole non-terminal + a Nop final) and this port's model (Instructions empty +
// the if as the FinalInstruction). A bare IfInstruction passes through.
IfInstruction* UnwrapElseIf(ILInstruction* inst) {
	if (!inst) return nullptr;
	if (auto* iff = dynamic_cast<IfInstruction*>(inst)) return iff;
	if (auto* b = dynamic_cast<Block*>(inst)) {
		if (b->Instructions.size() == 1 && IsNop(b->FinalInstruction.get()))
			return dynamic_cast<IfInstruction*>(b->Instructions[0].get());
		if (b->Instructions.empty() && b->FinalInstruction)
			return dynamic_cast<IfInstruction*>(b->FinalInstruction.get());
	}
	return nullptr;
}

// The C# BlockContainer.SingleInstruction: if the container has one block whose
// only instruction is a single instruction, return that instruction; otherwise
// return the block (or the container if it has multiple blocks). This port's
// block model splits the block's non-terminal Instructions from its
// FinalInstruction (the C# includes the final in Instructions), so the
// "single instruction" block is one with no non-terminals and a FinalInstruction.
ILInstruction* SingleInstruction(BlockContainer* c) {
	if (c->Blocks.size() != 1) return c;  // not a Leave -> MatchLeave fails
	Block* b = c->Blocks[0].get();
	if (b->Instructions.empty() && b->FinalInstruction) return b->FinalInstruction.get();
	return b;  // has non-terminals (or is degenerate) -> not a single Leave
}

// True if `tf` is a redundant try-finally around a PinnedRegion that can be
// eliminated. Non-mutating (the caller folds separately so the tree walk can
// find the first candidate safely).
bool CanEliminate(TryFinally* tf) {
	// Finally must be a single value-less Leave of the finally container.
	auto* finallyContainer = dynamic_cast<BlockContainer*>(tf->FinallyBlock.get());
	if (!finallyContainer) return false;
	if (!MatchLeave(SingleInstruction(finallyContainer), finallyContainer)) return false;

	// Try must be a single-block container.
	auto* tryContainer = dynamic_cast<BlockContainer*>(tf->TryBlock.get());
	if (!tryContainer || tryContainer->Blocks.size() != 1) return false;
	Block* tryBlock = tryContainer->Blocks[0].get();

	// The try block's non-terminal instructions must be exactly [PinnedRegion].
	if (tryBlock->Instructions.size() != 1) return false;
	auto* pinned = dynamic_cast<PinnedRegion*>(tryBlock->Instructions[0].get());
	if (!pinned) return false;

	// The trailing instruction (C# Instructions[1]) is this port's
	// FinalInstruction. The C# Count==1 case has no trailing instruction (a
	// null final here); the C# Count==2 case requires a value-less Leave of
	// the try container.
	if (tryBlock->FinalInstruction) {
		if (!MatchLeave(tryBlock->FinalInstruction.get(), tryContainer)) return false;
	}
	return true;
}

// Fold `tf`: detach the PinnedRegion from the try block and replace the
// TryFinally with it. The TryFinally's whole subtree (including the now-empty
// try block and the empty finally container) is destroyed by ReplaceWith.
void Eliminate(TryFinally* tf) {
	auto* tryContainer = static_cast<BlockContainer*>(tf->TryBlock.get());
	Block* tryBlock = tryContainer->Blocks[0].get();
	auto pinned = tryBlock->TakeChild(0);
	tf->ReplaceWith(std::move(pinned));
}

// ---- nesting-reduction heuristics (tested-but-not-yet-wired foundation) ----

// The C# BlockContainer.MatchConditionBlock: `block` is a condition block (a
// single IfInstruction whose false arm leaves `container` and whose true arm
// branches to the loop body). Reports the body start block. Adapted to this
// port's block model: the if is either the sole non-terminal (C# model) or the
// block's FinalInstruction (this port's While model).
bool MatchConditionBlock(BlockContainer* container, Block* block, Block*& bodyStartBlock) {
	bodyStartBlock = nullptr;
	IfInstruction* iff = nullptr;
	if (block->Instructions.size() == 1 && IsNop(block->FinalInstruction.get()))
		iff = dynamic_cast<IfInstruction*>(block->Instructions[0].get());
	else if (block->Instructions.empty() && block->FinalInstruction)
		iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
	if (!iff) return false;
	if (!iff->FalseInst || !MatchLeave(iff->FalseInst.get(), container)) return false;
	return MatchBranch(iff->TrueInst.get(), bodyStartBlock);
}

// Structural equality between two exit instructions (the C#
// `block.Instructions.Last().Match(implicitExit).Success`). Covers the
// keyword-exit kinds the wired ReduceNesting fold duplicates: a value-less
// Leave (return/break -- same TargetContainer, both Value null/Nop, or
// structurally-equal Values for faithfulness) and a Branch (continue -- same
// TargetBlock). Two nodes of different kinds, or a kind this does not handle
// (e.g. a Throw, or any non-exit), compare unequal (conservative: the helper
// does not remove the exit rather than mis-removing).
bool ExitsStructurallyEqual(ILInstruction* a, ILInstruction* b) {
	if (a == b) return true;
	if (!a || !b) return false;
	if (a->Op != b->Op) return false;
	switch (a->Op) {
		case OpCode::Leave: {
			auto* la = static_cast<Leave*>(a);
			auto* lb = static_cast<Leave*>(b);
			if (la->TargetContainer != lb->TargetContainer) return false;
			bool aNop = !la->Value || la->Value->Op == OpCode::Nop;
			bool bNop = !lb->Value || lb->Value->Op == OpCode::Nop;
			if (aNop && bNop) return true;       // both value-less (the keyword-exit case)
			if (aNop != bNop) return false;      // one valued, one not
			return ExitsStructurallyEqual(la->Value.get(), lb->Value.get());
		}
		case OpCode::Branch: {
			auto* ba = static_cast<Branch*>(a);
			auto* bb = static_cast<Branch*>(b);
			return ba->TargetBlock == bb->TargetBlock;
		}
		default:
			return false;  // unhandled kind: compare unequal (conservative)
	}
}

// Whether the then of a no-else if can be made to exit -- a precondition for the
// ReduceNesting no-else fold, which InvertIf requires (the then must have an
// unreachable end point). The C# step 4 (`EnsureEndPointUnreachable(ifInst.
// TrueInst, ...)`) makes the then exit if it falls through; this port's
// EnsureEndPointUnreachable is a no-op for a non-Block and for a Block whose
// final is an IfInstruction (the if-as-final block, whose end point is the if's
// own control flow, not a fall-through). So the then can be made to exit iff it
// is a Block with a Leave/Throw final (already exits) or a Branch/Nop/null
// final (a fall-through EnsureEndPointUnreachable replaces with the exit
// clone), or a bare instruction that already exits (EndPointUnreachable). An
// if-as-final Block or a bare non-exit cannot be made to exit, so the fold
// bails (InvertIf would bail, leaving the fold incomplete).
bool ThenCanBeMadeToExit(ILInstruction* then) {
	if (!then) return false;
	if (auto* b = dynamic_cast<Block*>(then)) {
		auto* final = b->FinalInstruction.get();
		if (!final || final->Op == OpCode::Nop || final->Op == OpCode::Branch) return true;
		if (final->Op == OpCode::Leave || final->Op == OpCode::Throw) return true;
		return false;
	}
	return HasFlag(then->Flags(), InstructionFlags::EndPointUnreachable);
}

// ---- ReduceNesting (the else-if-tree case) helpers ----

// Whether a block's end point is unreachable -- the block exits via a
// Leave/Throw final (a real exit), NOT a Branch/Nop final (a fall-through) or
// an IfInstruction final (the if-as-final block, whose end point is the if's
// own control flow). The C# `Block.HasFlag(EndPointUnreachable)` would read the
// flag directly, but this port's Block uses a Branch for the fall-through (which
// sets EndPointUnreachable unlike the C# Nop final -- the D157 divergence), so
// the faithful check is the FinalInstruction's opcode (the same check
// EnsureEndPointUnreachable uses). Used by the ReduceNesting else-if-tree fold
// to decide whether the trailing exit (Block B) is dead after the else content
// is promoted to the container.
bool BlockExitsReal(Block* b) {
	if (!b) return false;
	auto* f = b->FinalInstruction.get();
	if (!f) return false;  // a null final is a void fall-through
	return f->Op == OpCode::Leave || f->Op == OpCode::Throw;
}

// Remove `blockB` from its container's Blocks list (the dead-exit removal --
// after the else content is promoted to the container and exits, Block B's
// trailing exit is unreachable and is dropped). Re-parents and re-numbers the
// remaining blocks (the insert/remove pattern). Only removes when `blockB` has
// no non-terminal Instructions (it holds only the dead exit); a Block B with
// falseCode is left in place (conservative -- the C# RemoveRange drops the
// trailing exitInst from a single block, which maps to dropping Block B's
// final here; a Block B with content would lose its exit, so it is not touched).
void RemoveEmptyExitBlock(Block* blockB) {
	if (!blockB) return;
	auto* container = dynamic_cast<BlockContainer*>(blockB->Parent);
	if (!container) return;
	if (!blockB->Instructions.empty()) return;  // conservative: only drop a bare-exit Block B
	for (auto it = container->Blocks.begin(); it != container->Blocks.end(); ++it) {
		if (it->get() == blockB) {
			container->Blocks.erase(it);
			break;
		}
	}
	for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
		container->Blocks[i]->Parent = container;
		container->Blocks[i]->ChildIndex = static_cast<int>(i);
	}
}

// ---- ImproveILOrdering (the wired ReduceNestingTransform fold) ----

// Whether `leave` exits the function body (Port of Leave.IsLeavingFunction):
// the target container's parent is the ILFunction. (A copy of the
// HighLevelLoopTransform file-local helper; ReduceNestingTransform does not
// link HighLevelLoopTransform.)
bool IsLeavingFunction(Leave* leave) {
	return leave && leave->TargetContainer && leave->TargetContainer->Parent &&
	       leave->TargetContainer->Parent->Op == OpCode::ILFunction;
}

// The next block in `block`'s container after `block` (the positional
// fall-through). nullptr if `block` is not in a container's Blocks list or is
// the last block. (A copy of the ConditionDetection file-local helper; the
// public ConditionDetection::InvertIf uses the same logic.)
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

// The "exit" of the TrueInst: the C# `block.Instructions.Last()` (the
// falseCode's exit). After ConditionDetection's inversion, the TrueInst IS the
// falseCode+exit (an inlined fall-through). If the TrueInst is a Block wrapping
// [falseCode, exit], the exit is the Block's FinalInstruction; if the TrueInst
// is a bare exit (a Leave/Branch/Throw -- the no-falseCode case), it is the
// TrueInst itself.
ILInstruction* TrueInstExit(ILInstruction* trueInst) {
	if (!trueInst) return nullptr;
	if (auto* b = dynamic_cast<Block*>(trueInst)) return b->FinalInstruction.get();
	return trueInst;
}

// The instruction following `inst` in `block`'s instruction list (the C#
// `block.Instructions[block.Instructions.IndexOf(leavingInst) + 1]`). The C#
// `Block.Instructions` includes the control flow as the last non-terminal
// (FinalInstruction is a Nop), so the instruction after Instructions[i] is
// Instructions[i+1]. This port splits the block's non-terminal Instructions
// from its FinalInstruction (the control flow), so the instruction after
// Instructions[i] is Instructions[i+1] if it exists, else the FinalInstruction
// (the control flow). `inst` must be a non-terminal in `block`'s Instructions
// list (a non-final); returns null if it is not (should not happen for a caller
// that just confirmed `inst` is a non-final in `block`).
ILInstruction* FollowingInstructionInBlock(Block* block, ILInstruction* inst) {
	if (!block || !inst) return nullptr;
	for (std::size_t i = 0; i < block->Instructions.size(); ++i) {
		if (block->Instructions[i].get() == inst) {
			if (i + 1 < block->Instructions.size()) return block->Instructions[i + 1].get();
			return block->FinalInstruction.get();
		}
	}
	return nullptr;
}

// The number of `leave(container)` instructions targeting `container` within
// `inst`'s subtree (the C# `BlockContainer.LeaveCount`). A leave exits its own
// container, so the targeting leaves live within the container's subtree.
// Used to replicate the C# BlockContainer.ComputeFlags EndPointUnreachable
// semantics (set iff LeaveCount == 0) which this port's BlockContainer does not
// reproduce (the base Flags() propagates the leave's EndPointUnreachable up).
int CountLeavesTargeting(ILInstruction* inst, BlockContainer* container) {
	if (!inst || !container) return 0;
	int n = 0;
	if (auto* leave = dynamic_cast<Leave*>(inst)) {
		if (leave->TargetContainer == container) ++n;
	}
	for (int i = 0; i < inst->ChildCount(); ++i)
		n += CountLeavesTargeting(inst->GetChild(i), container);
	return n;
}

// Whether `inst`'s end point is unreachable per the C# semantics. For a
// BlockContainer the C# ComputeFlags sets EndPointUnreachable iff LeaveCount ==
// 0 (no leave targeting the container); this port's BlockContainer does not
// replicate that, so the LeaveCount is counted inline. For every other
// instruction (Block, Leave, Throw, ...) this port's base Flags() is faithful
// to the C# ComputeFlags, so the flag is read directly.
bool EndPointUnreachableCSharp(ILInstruction* inst) {
	if (!inst) return false;
	if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
		return CountLeavesTargeting(c, c) == 0;
	}
	return HasFlag(inst->Flags(), InstructionFlags::EndPointUnreachable);
}

// The C# `ReduceNestingTransform.ImproveILOrdering`: for an if with an
// unreachable end point and no else, inverts to match the IL order of the first
// statement of each branch.
//
//   if (cond) { then (exits) }   falseCode...; exit   -- the C# shape
// -> if (!cond) { falseCode...; exit }   then...        (when then's IL order
//    is after the falseCode's, i.e. the falseCode comes first in IL)
//
// Block-model adaptation (the recurring D73/D75 divergence): the C# reads
// `ifInst` as a NON-TERMINAL at `block.Instructions[i]` with the
// `falseCode...; exit` as sibling instructions after it (the C# Block.Instructions
// includes the control flow; FinalInstruction is a Nop). This port makes the
// IfInstruction the block's FinalInstruction, so the `falseCode...; exit` is
// the NEXT block in the container (the fall-through), and the old then moves
// into that next block. ConditionDetection's TryInlineIfFallThrough +
// TryInvertIfExit already inverted the if (TrueInst = the inlined falseCode+
// exit, FalseInst = null, fall-through = the old then / next block), so
// ImproveILOrdering re-inverts ONLY when the IL order is wrong (the old then /
// next block comes BEFORE the falseCode / TrueInst in IL).
//
// The IL-order gate uses ConditionDetection.GetStartILOffset on the TrueInst
// (the falseCode+exit's start) and the next block (the old then's start). The
// GetStartILOffset Block adaptation (Block label = the first instruction's
// offset) makes both offsets valid for Block TrueInsts and the next block; a
// bare-Leave TrueInst (the no-falseCode case) reports the Leave's own offset
// (or its Value's offset for a valued return), which the pre-pipeline clone
// sites now preserve (the D156 ILRange-propagation fix to CFS branch-to-leave /
// value-return folds, InlineReturnTransform.CloneReturnBlock, LoopDetection
// break-leave, and ClonePureLoad), so the gate fires on the bare-Leave path
// too. A few break-Leaves from the port-specific LoopDetection while-condition
// false-arm / exit-block-null sites and a couple of valued returns still report
// empty (the remaining ~28 of ~2200 bare-Leave candidates); closing those is a
// separate piece.
//
// The trailing-leave handling (the C# `block.Instructions.Last() is Leave &&
// !IsLeavingFunction && TargetContainer.Kind == Normal` check + the
// CanDuplicateExit replacement): the C# checks the if's block's LAST instruction
// (Xn = the old-then exit). In this port's if-as-FinalInstruction model the C#
// if's block `[ifInst, oldThen..., Xn]` is split: the if is this block's
// FinalInstruction (Block A) and the `oldThen...; Xn` is the NEXT block (Block B,
// the fall-through). So Xn = `nextBlock->FinalInstruction` (Block B's control flow),
// NOT this block's FinalInstruction (which is ifInst). If Xn is a non-keyword
// Leave (a Leave of a Normal container that is not the function), InvertIf would
// move it into the if's then where it would render as `goto`; the C# replaces it
// with a keyword exit (return/continue/break) via CanDuplicateExit BEFORE InvertIf,
// so the then gets the keyword exit instead of a goto. If the leave can't be
// duplicated, bail (don't invert). `continueTarget` (the C# parameter, the loop
// entry-point block a `continue` branches to) is tracked by the Visit walk and
// passed in; it is null at the top level. The clone is taken before the
// SetFinal that destroys the old Xn (CanDuplicateExit may report keywordExit =
// Xn itself for a direct return/break/continue).
//
// On the .NET Framework 4 legacy-csc mscorlib corpus the trailing-leave
// handling is faithfulness-only (a corpus probe counted 0 non-keyword-leave Xn
// exits across the gate-would-fire candidates -- they all carry a Branch or
// `other` exit), matching the D59/D60/D69 precedent; the wired ImproveILOrdering
// fold still fires (the IL-order gate fires on the candidates whose
// ConditionDetection inversion put the code in the wrong IL order).
void ImproveILOrdering(Block* block, IfInstruction* ifInst, Block* continueTarget) {
	if (!block || !ifInst) return;
	// The if must be the block's FinalInstruction (the C# `ifInst.Parent == block`).
	if (block->FinalInstruction.get() != ifInst) return;
	// No else (the C# `IsEmpty(ifInst.FalseInst)`).
	if (ifInst->FalseInst) return;
	// The then must exit (the C# `ifInst.TrueInst.HasFlag(EndPointUnreachable)`).
	if (!ifInst->TrueInst) return;
	if (!HasFlag(ifInst->TrueInst->Flags(), InstructionFlags::EndPointUnreachable)) return;
	// The falseCode is the next block (the C# `ifInst != block.Instructions.Last()`;
	// this port has no falseCode sibling -- the falseCode+exit is the next block).
	Block* nextBlock = NextBlockInContainer(block);
	if (!nextBlock) return;
	// IL-order gate: invert only when the falseCode (next block / old then) comes
	// BEFORE the then (TrueInst / falseCode+exit) in IL.
	bool trueEmpty = false, falseEmpty = false;
	int trueStart = ConditionDetection::GetStartILOffset(ifInst->TrueInst.get(), trueEmpty);
	int falseStart = ConditionDetection::GetStartILOffset(nextBlock, falseEmpty);
	if (trueEmpty || falseEmpty || falseStart >= trueStart) return;
	// Trailing-leave handling: the C# checks the if's block's last instruction
	// (Xn = this port's nextBlock->FinalInstruction, the old-then exit). If Xn
	// is a non-keyword Leave, InvertIf would move it into the then where it renders
	// as `goto`; replace it with a keyword exit via CanDuplicateExit first, or bail
	// if it can't be duplicated.
	if (auto* leave = dynamic_cast<Leave*>(nextBlock->FinalInstruction.get())) {
		if (!IsLeavingFunction(leave) && leave->TargetContainer &&
		    leave->TargetContainer->Kind == ContainerKind::Normal) {
			ILInstruction* keywordExit = nullptr;
			if (!ReduceNestingTransform::CanDuplicateExit(nextBlock->FinalInstruction.get(),
			                                  continueTarget, keywordExit))
				return;  // can't replace with a keyword exit; don't invert
			// Clone before SetFinal destroys the old Xn (CanDuplicateExit may
			// report keywordExit = Xn itself for a direct return/break/continue).
			auto keywordExitClone = keywordExit->Clone();
			nextBlock->SetFinal(std::move(keywordExitClone));
		}
	}
	ConditionDetection::InvertIf(block, ifInst);
}

// Visit every BlockContainer's blocks, calling ImproveILOrdering on each
// if-as-FinalInstruction (the C# `Visit` iterates `block.Instructions` and calls
// ImproveILOrdering on each if non-terminal; this port's if-as-final model makes
// the if the block's FinalInstruction, so the visit checks the final). Recurses
// into nested containers (the if's arms, the block's non-terminal containers).
// `continueTarget` (the C# Loop/While/DoWhile tracking for CanDuplicateExit's
// `continue` detection) is set per-container based on the container's Kind
// (Loop/While -> the entry point Blocks[0]; DoWhile -> the last block
// Blocks.back(); Normal/Switch -> inherit the parent's continueTarget), matching
// the C# `Visit(BlockContainer)` switch. The ReduceNesting/ReduceSwitchNesting/
// ExtractElseBlock folds and the content-block recursion are deferred (need the
// full EnsureEndPointUnreachable/ExtractElseBlock helpers + the dominator
// analysis).
void VisitContainers(ILInstruction* inst, Block* continueTarget);
void VisitContainer(BlockContainer* container, Block* continueTarget) {
	// Set continueTarget based on the container's Kind (the C#
	// `Visit(BlockContainer)` switch). Loop/While -> the entry point (Blocks[0]);
	// DoWhile -> the last block (Blocks.back()); Normal/Switch -> inherit the
	// parent's continueTarget (the C# does not override for these kinds).
	switch (container->Kind) {
		case ContainerKind::Loop:
		case ContainerKind::While:
			continueTarget = container->Blocks.empty() ? nullptr : container->Blocks.front().get();
			break;
		case ContainerKind::DoWhile:
			continueTarget = container->Blocks.empty() ? nullptr : container->Blocks.back().get();
			break;
		case ContainerKind::Normal:
		case ContainerKind::Switch:
			break;  // inherit the parent's continueTarget
	}
	for (auto& block : container->Blocks) {
		for (auto& inst : block->Instructions)
			VisitContainers(inst.get(), continueTarget);
		if (auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get())) {
			ImproveILOrdering(block.get(), iff, continueTarget);
			VisitContainers(iff->TrueInst.get(), continueTarget);
			VisitContainers(iff->FalseInst.get(), continueTarget);
		} else if (block->FinalInstruction) {
			VisitContainers(block->FinalInstruction.get(), continueTarget);
		}
	}
}
void VisitContainers(ILInstruction* inst, Block* continueTarget) {
	if (!inst) return;
	if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
		VisitContainer(cont, continueTarget);
		return;
	}
	if (inst->Op == OpCode::ILFunction) return;  // inline ILFunctions already transformed
	for (int i = 0; i < inst->ChildCount(); ++i)
		VisitContainers(inst->GetChild(i), continueTarget);
}

} // namespace

// The C# `ReduceNestingTransform.CanDuplicateExit`: checks whether an exit
// instruction is a duplicable keyword exit (return; break; continue;). The
// wired ImproveILOrdering trailing-leave handling and the deferred
// ReduceNesting / ReduceSwitchNesting folds consult it. `keywordExit` reports
// the keyword exit to duplicate (the exit itself for a direct return/break/
// continue, or the keyword exit found by walking out of a try/pinned/lock
// container).
//
// Block-model adaptation (the recurring D73/D75 divergence): the C# walks up
// from `leave.TargetContainer` until it finds an instruction in a Block that is
// NOT the last instruction in that block (`!(leavingInst.Parent is Block b) ||
// leavingInst == b.Instructions.Last()`), then recurses on the FOLLOWING
// instruction (`block.Instructions[indexOf(leavingInst) + 1]`). The C#
// `Block.Instructions` includes the control flow as the last non-terminal
// (FinalInstruction is a Nop), so "the last instruction" is the control flow;
// this port splits the block's non-terminal Instructions from its FinalInstruction
// (the control flow), so "the last instruction" is the FinalInstruction, and
// the "following instruction" of a non-terminal Instructions[i] is
// Instructions[i+1] if it exists, else the FinalInstruction. The C#
// `SlotInfo == TryFinally.FinallyBlockSlot` (the FinallyBlock slot) maps to this
// port's `ChildIndex == 1` on a TryFinally (the FinallyBlock is at ChildIndex 1);
// likewise `TryFault.FaultBlockSlot` -> `ChildIndex == 1` on a TryFault. The
// Branch-to-continueTarget check is inlined (not MatchBranch) to avoid the
// D149 MatchBranch report-overload ambiguity (a `Block*` lvalue binds to the
// `Block*&` report overload, not the `const Block*` match-against overload).
bool ReduceNestingTransform::CanDuplicateExit(ILInstruction* exit, Block* continueTarget,
                                              ILInstruction*& keywordExit) {
	keywordExit = exit;
	// keyword is continue: a Branch to the continue target.
	if (exit) {
		if (auto* br = dynamic_cast<Branch*>(exit)) {
			if (br->TargetBlock && br->TargetBlock == continueTarget) return true;
		}
	}
	// Only a value-less Leave can be a duplicable return/break.
	auto* leave = dynamic_cast<Leave*>(exit);
	if (!leave || !IsNop(leave->Value.get())) return false;  // don't duplicate valued returns
	if (!leave->TargetContainer) return false;  // defensive (the C# assumes non-null)
	// keyword is return (leaving the function) or break (leaving a non-Normal
	// container -- a Loop/While/DoWhile/Switch).
	if (IsLeavingFunction(leave) || leave->TargetContainer->Kind != ContainerKind::Normal)
		return true;
	// A Leave of a Normal container (a try/pinned/lock/etc. body): walk out to
	// the instruction following the target container and recurse on it. The C#
	// loop continues while (parent is not a Block) OR (leavingInst is the last);
	// the inverse (parent IS a Block AND leavingInst is NOT the last) is the stop.
	ILInstruction* leavingInst = leave->TargetContainer;
	while (true) {
		auto* parentBlock = dynamic_cast<Block*>(leavingInst->Parent);
		if (parentBlock && parentBlock->FinalInstruction.get() != leavingInst) break;
		// TryFinally/TryFault checks (the C# checks the parent's slot).
		if (auto* tf = dynamic_cast<TryFinally*>(leavingInst->Parent)) {
			if (leavingInst->ChildIndex == 1) return false;  // FinallyBlock: cannot duplicate
			// The C# `tryFinally.HasFlag(EndPointUnreachable)` = the try block or the
			// finally block has an unreachable end point (the finally always
			// throws/returns, or the try block has no leave). This port's BlockContainer
			// does not replicate the C# LeaveCount-based ComputeFlags, so the C#
			// semantics are replicated inline (EndPointUnreachableCSharp).
			if (EndPointUnreachableCSharp(tf->TryBlock.get()) ||
			    EndPointUnreachableCSharp(tf->FinallyBlock.get()))
				return false;
		} else if (auto* tfault = dynamic_cast<TryFault*>(leavingInst->Parent)) {
			if (leavingInst->ChildIndex == 1) return false;  // FaultBlock: cannot duplicate
		}
		leavingInst = leavingInst->Parent;
		if (!leavingInst || leavingInst->Op == OpCode::ILFunction) return false;  // defensive: walked past the root
	}
	auto* block = dynamic_cast<Block*>(leavingInst->Parent);
	ILInstruction* targetInst = FollowingInstructionInBlock(block, leavingInst);
	if (!targetInst) return false;  // defensive
	return CanDuplicateExit(targetInst, continueTarget, keywordExit);
}

// The C# `ReduceNestingTransform.GetElseIfParent`: determines whether `ifInst`
// is an else-if (a Block wrapping only that if, nested as the FalseInst of a
// parent IfInstruction) and, if so, returns the preceding parent IfInstruction;
// otherwise null.
//
//   [else-]if (parent-cond) else { ifInst }   -- the C# shape
//
// The C# checks: `Block.Unwrap(ifInst.Parent) == ifInst` (the else block has
// only the if), `ifInst.Parent.Parent is IfInstruction elseIfInst` (the parent
// of the else block is an IfInstruction), and `elseIfInst.FalseInst ==
// ifInst.Parent` (the else block is the false arm, not the true arm).
//
// Block-model adaptation (the recurring D73/D75 divergence): the C#
// `Block.Unwrap` returns the block's sole instruction, where the C#
// `Block.Instructions` includes the control flow as the last non-terminal
// (FinalInstruction is a Nop) -- so the else block's sole instruction is
// `Instructions[0]` with a Nop final. This port splits the block's non-terminal
// Instructions from its FinalInstruction (the control flow), so the else block
// can carry the if either as a non-terminal (the C# model: Instructions.size()
// == 1 with a Nop final) or as the FinalInstruction (this port's if-as-final
// model: Instructions empty with the if as the final). Both shapes unwrap to the
// if; a block with any other shape (more than one instruction, or a single
// non-if instruction) does not.
IfInstruction* ReduceNestingTransform::GetElseIfParent(IfInstruction* ifInst) {
	if (!ifInst) return nullptr;
	auto* elseBlock = dynamic_cast<Block*>(ifInst->Parent);
	if (!elseBlock) return nullptr;
	// `Block.Unwrap(elseBlock) == ifInst`: the else block has only the if.
	bool unwrapsToIf = false;
	if (elseBlock->Instructions.size() == 1 && IsNop(elseBlock->FinalInstruction.get())
	    && elseBlock->Instructions[0].get() == ifInst)
		unwrapsToIf = true;  // C# model: the if is the sole non-terminal + a Nop final
	else if (elseBlock->Instructions.empty() && elseBlock->FinalInstruction.get() == ifInst)
		unwrapsToIf = true;  // if-as-final model: the if is the FinalInstruction
	if (!unwrapsToIf) return nullptr;
	// The parent of the else block must be an IfInstruction (the else-if's parent).
	auto* elseIfInst = dynamic_cast<IfInstruction*>(elseBlock->Parent);
	if (!elseIfInst) return nullptr;
	// The else block must be the false arm (not the true arm).
	if (elseIfInst->FalseInst.get() != elseBlock) return nullptr;
	return elseIfInst;
}

// The C# `ReduceNestingTransform.EnsureEndPointUnreachable`: ensures the end
// point of a block is unreachable by duplicating and appending the [exit]
// instruction following the end point. See the header for the block-model
// adaptation (this port's Block uses a Branch for the fall-through, which sets
// EndPointUnreachable unlike the C# Nop final, so the C# `!HasFlag` check is
// replaced by a FinalInstruction-opcode check: a Branch/Nop/null final is a
// fall-through; a Leave/Throw/IfInstruction final is not).
void ReduceNestingTransform::EnsureEndPointUnreachable(ILInstruction* inst,
                                                        ILInstruction* fallthroughExit) {
	if (!inst || !fallthroughExit) return;
	auto* block = dynamic_cast<Block*>(inst);
	if (!block) return;  // not a Block: the C# asserts EndPointUnreachable
	auto* final = block->FinalInstruction.get();
	// The block falls through (its end point is reachable in the C# sense) iff
	// its FinalInstruction is a Branch (a fall-through) or a Nop/null (a void
	// fall-through), NOT a Leave/Throw (a real exit) or an IfInstruction (the
	// if-as-final block, Block A). A Leave/Throw already makes the end point
	// unreachable; an IfInstruction is the if-as-final block whose end point is
	// the if's own control flow, not a fall-through to duplicate an exit into.
	bool fallsThrough = false;
	if (!final || final->Op == OpCode::Nop) fallsThrough = true;        // void fall-through
	else if (final->Op == OpCode::Branch) fallsThrough = true;         // Branch fall-through
	if (!fallsThrough) return;
	// Replace the fall-through final with a clone of the exit (the C# appends
	// to Instructions, keeping the fall-through as dead code; this port's
	// FinalInstruction IS the control flow, so the fall-through is dropped and
	// the exit becomes the final -- the block's content in Instructions is
	// preserved, and the block now exits via the exit instead of falling
	// through to the same exit).
	auto exitClone = fallthroughExit->Clone();
	block->SetFinal(std::move(exitClone));
}

// The C# `ReduceNestingTransform.RemoveRedundantExit`: removes a redundant
// block exit instruction -- when the block's trailing exit (its last
// instruction / control flow) equals `implicitExit` (the instruction following
// the block's end point, i.e. the fall-through), drop it so the block falls
// through. The wired ReduceNesting fold calls it after a successful fold (the
// fold duplicated the exit into the then/else block, so the block's own
// trailing exit is now redundant). No pipeline transform consults it yet.
//
// Block-model adaptation (the recurring D73/D75 divergence): the C#
// `block.Instructions.Last()` (the last non-terminal, which IS the control
// flow in the C# where FinalInstruction is a Nop) is this port's
// `block->FinalInstruction`, and `RemoveLast()` (leaving a Nop final = a
// fall-through) is a replacement of the final with a fall-through: a Branch to
// the next block in the container (the positional fall-through), or a Nop
// final when there is no next block (the implicit void fall-through). The
// Match is a structural equality (ExitsStructurallyEqual) over the
// keyword-exit kinds (a value-less Leave -- return/break; a Branch --
// continue); other kinds compare unequal so the exit is not removed.
void ReduceNestingTransform::RemoveRedundantExit(Block* block,
                                                   ILInstruction* implicitExit) {
	if (!block || !implicitExit) return;  // a null implicitExit never matches
	auto* final = block->FinalInstruction.get();
	if (!final) return;  // no trailing exit to remove
	if (!ExitsStructurallyEqual(final, implicitExit)) return;
	// "Remove" the final (the C# `block.Instructions.RemoveLast()`): the block
	// now falls through. Replace the final with a Branch to the next block in
	// the container (the positional fall-through), or a Nop when there is no
	// next block (the implicit void fall-through / container leave).
	Block* nextBlock = NextBlockInContainer(block);
	if (nextBlock)
		block->SetFinal(std::make_unique<Branch>(nextBlock));
	else
		block->SetFinal(std::make_unique<Nop>());
}

// The C# `ReduceNestingTransform.ExtractElseBlock`: extracts an if's else
// block. See the header for the block-model adaptation (the else Block moves
// whole into the container after Block A; the if's FalseInst becomes a Nop).
// The C# asserts `ifInst.TrueInst.HasFlag(EndPointUnreachable)` as a
// precondition (the wired fold makes the then exit first); this port is
// defensive and does not assert (the wired fold ensures it).
void ReduceNestingTransform::ExtractElseBlock(IfInstruction* ifInst) {
	if (!ifInst) return;
	auto* block = dynamic_cast<Block*>(ifInst->Parent);
	if (!block) return;  // the if's parent must be a Block (Block A)
	auto* container = dynamic_cast<BlockContainer*>(block->Parent);
	if (!container) return;  // the block's parent must be a BlockContainer
	auto* falseBlock = dynamic_cast<Block*>(ifInst->FalseInst.get());
	if (!falseBlock) return;  // the else must be a Block (the C# casts)
	// Find Block A's index in the container. The C# inserts the else content
	// after the if (`block.Instructions.IndexOf(ifInst) + 1`); this port's if is
	// Block A's FinalInstruction, so "after the if" is after Block A in the
	// container (between Block A and Block B [the exit] when Block B exists).
	std::size_t blockIdx = 0;
	bool found = false;
	for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
		if (container->Blocks[i].get() == block) { blockIdx = i; found = true; break; }
	}
	if (!found) return;  // defensive: Block A is not in the container
	// Detach the else block from the if's FalseInst slot. The C# moves
	// `falseBlock.Instructions` (content + control flow) into the parent block;
	// this port moves the whole else Block into the container as a new sibling
	// after Block A (the if is Block A's FinalInstruction, so the else content
	// cannot be non-terminals in Block A -- it would precede the if-as-final).
	// The else block's Instructions (content) and FinalInstruction (control flow)
	// become the sibling block's content and control flow, and the sibling block
	// falls through to the next block the way the C#'s inlined else content
	// falls through to exitInst.
	auto ownedElse = std::unique_ptr<Block>(
		static_cast<Block*>(ifInst->FalseInst.release()));
	container->Blocks.insert(container->Blocks.begin() + blockIdx + 1,
	                          std::move(ownedElse));
	// Re-parent and re-number all blocks (the insert shifted the later blocks).
	for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
		container->Blocks[i]->Parent = container;
		container->Blocks[i]->ChildIndex = static_cast<int>(i);
	}
	// Clear the if's else (the C# `ifInst.FalseInst = new Nop()`).
	ifInst->FalseInst = std::make_unique<Nop>();
	ifInst->FalseInst->Parent = ifInst;
	ifInst->FalseInst->ChildIndex = 2;
}

// The C# `ReduceNestingTransform.ReduceNesting`: reduces the nesting of an
// if/else-if/else followed by a keyword exit by duplicating the exit into the
// then blocks and moving the else content out, so the (exiting) thens stay
// nested but the else content (the bulk) flattens to the fall-through after the
// if chain. Two cases:
//  * no-else:   if (cond) { then } exit;              -> if (!cond) exit; then...; exit;
//  * else-tree: if (c1) { t1 } else if (c2) { t2 } else { elseBlock } exit;
//              ->  if (c1) { t1; exit; } if (c2) { t2; exit; } elseBlock...; exit;
// `exitInst` is the keyword exit (return/break/continue) the caller found via
// `CanDuplicateExit` (the C# `Visit` passes `keywordExit`). Returns true if the
// fold fired. No pipeline transform consults it yet (the wired Visit walk is the
// subsequent iteration); the corpus sweep in the test suite fires it on real
// candidates to validate safety.
//
// ---- no-else case ----
//
// Block-model adaptation (the recurring D73/D75 divergence): the C# operates on
// a single block `[ifInst, exit]` (the if is a non-terminal, the exit is a
// sibling or the fall-through `nextInstruction`). This port splits that into
// Block A (the if is the FinalInstruction) + Block B (the exit is the
// FinalInstruction, the fall-through from Block A). The C# steps map as:
//  * Step 3 (`EnsureEndPointUnreachable(block, exitInst)`): a no-op in this
//    port. The C# appends `exitInst` to the block when the block falls through
//    to `nextInstruction` (case b), so the block ends in the exit and InvertIf's
//    `GetExit(block)` finds it. This port's Block A always falls through to
//    Block B (which holds the exit), and InvertIf reads the exit from Block B
//    (not from the block's last instruction), so there is nothing to append.
//    (The C# case a -- the exit is a sibling in the block -- does not arise:
//    the if is Block A's FinalInstruction, so nothing follows it in Block A;
//    the exit is always in Block B.)
//  * Step 4 (`EnsureEndPointUnreachable(ifInst.TrueInst, block.Instructions.Last())`):
//    ensure the then exits. The C# uses `block.Instructions.Last()` which (in
//    case b) is the keyword clone appended in step 3, so the then is made to
//    exit via the keyword. This port's step 3 is a no-op, so the equivalent is
//    to use `exitInst` (the keyword) directly -- the then is made to exit via
//    the keyword, matching the C# case-b net result.
//  * Step 5 (`ConditionDetection.InvertIf`): invert. This port's InvertIf reads
//    Block B's FinalInstruction (the exit) into the if's TrueInst and moves the
//    old then into Block B.
//  * Step 6 (`if (!ifInst.TrueInst.Match(exitInst)) ReplaceWith`): ensure the
//    if's TrueInst is the keyword exit. InvertIf made the if's TrueInst = Block
//    B's original FinalInstruction (the exit), which may be a leave-from-try
//    (not the keyword `exitInst` the caller found by walking out of the try);
//    replace it with the keyword clone.
// The no-else fold bails (returns false, no mutation) if the then is shallow
// (`maxDepth < 2`), Block B does not exist or has falseCode (InvertIf needs the
// no-falseCode exit holder), Block B is not single-predecessor (InvertIf's
// requirement -- the C# has the falseCode in the same block, single-pred by
// construction), or the then cannot be made to exit (an if-as-final Block or a
// bare non-exit -- InvertIf would bail, leaving the fold incomplete).
//
// ---- else-if-tree case (and the plain if-else case) ----
//
// The C# do-while walks the else-if tree leaf-to-root, and per iteration calls
// `EnsureEndPointUnreachable(ifInst.TrueInst, exitInst)` (make the then exit),
// checks `ifInst.FalseInst.HasFlag(EndPointUnreachable)` and `RemoveRange`s the
// trailing exit (the dead exitInst when the else exits), then `ExtractElseBlock`
// (inline the else content into the parent block). Block-model adaptation (the
// recurring D73/D75 divergence, the crux D160's deferral flagged): the C# inlines
// the else content into the PARENT BLOCK (which can be a nested else block),
// so the else-if chain flattens into a single block `[ifRoot, ifInner, ...,
// elseContent]`. This port's if-as-final model makes each if a block's
// FinalInstruction, so the else content cannot be inlined into a nested else
// block (it would precede the if-as-final). The faithful adaptation is to
// PROMOTE each else block to the CONTAINER as a new sibling block: after the
// fold the container is `[Block A (ifRoot), Block C (ifInner's holder), ...,
// Block E (else content), Block B (exit)]`, which renders the same as the C#'s
// single-block structure (each block falls through positionally to the next).
//
// The KEY ORDERING difference from the C#: the C# does `ExtractElseBlock`
// leaf-to-root (inlining into the parent block, which always exists). This port
// MUST do `ExtractElseBlock` ROOT-TO-LEAF, because `ExtractElseBlock` moves the
// else block to the CONTAINER of the if's parent block, and an intermediate
// else-if's parent block is another if's FalseInst (not a container) until its
// own parent if's else block has been promoted. After `ExtractElseBlock(ifRoot)`
// the next else-if's holder block is in the container, so `ExtractElseBlock`
// on the next if can promote its else. The `EnsureEndPointUnreachable` calls
// (the then-exit duplication) are independent of the `FalseInst` moves, so they
// all precede the `ExtractElseBlock` calls.
//
// The dead-exit removal (the C# `RemoveRange`): the C# checks
// `ifInst.FalseInst.HasFlag(EndPointUnreachable)` per iteration and removes the
// trailing `exitInst` when the else exits. This port promotes the else content
// to the container (its end point is invariant across the moves), so a single
// check on the promoted else content (`BlockExitsReal`) suffices: when the else
// content exits, Block B (the trailing exit holder) is unreachable and is
// dropped (`RemoveEmptyExitBlock`, only when Block B has no non-terminals -- a
// conservative faithfulness gap for the Block-B-with-falseCode case that does
// not arise in the else-if-tree shape, where the exit is directly after the if).
bool ReduceNestingTransform::ReduceNesting(Block* block, IfInstruction* ifInst,
                                           ILInstruction* exitInst) {
	if (!block || !ifInst || !exitInst) return false;
	// The if must be the block's FinalInstruction (the C# `ifInst.Parent == block`).
	if (block->FinalInstruction.get() != ifInst) return false;
	// Start tallying stats from the root's then (the C# tallies before the
	// no-else/else-if-tree branch).
	int maxStatements = 0, maxDepth = 0;
	UpdateStats(ifInst->TrueInst.get(), maxStatements, maxDepth);

	// ---- no-else case: if (cond) { then } exit; -> if (!cond) exit; then...; exit; ----
	if (IsNop(ifInst->FalseInst.get())) {
		// Heuristic: the then must be deeply nested (the C# `if (maxDepth < 2) return false`).
		if (maxDepth < 2) return false;
		// Block B (the exit holder) must exist, have no falseCode (the no-else case
		// `if (cond) { then } exit;` has the exit directly after the if), and be
		// single-predecessor (InvertIf's requirement; the C# has the falseCode in
		// the same block, single-pred by construction).
		Block* blockB = NextBlockInContainer(block);
		if (!blockB) return false;
		if (!blockB->Instructions.empty()) return false;
		if (blockB->IncomingEdgeCount != 1) return false;
		// The then must exit (or be made to exit by EnsureEndPointUnreachable); an
		// if-as-final Block or a bare non-exit cannot, so InvertIf would bail.
		if (!ThenCanBeMadeToExit(ifInst->TrueInst.get())) return false;
		// Step 4: ensure the then exits via the keyword exit (the C# case-b net
		// result; see the header for why `exitInst` not Block B's original final).
		EnsureEndPointUnreachable(ifInst->TrueInst.get(), exitInst);
		// Step 5: invert (reads Block B's exit into the if's TrueInst, moves the then
		// into Block B).
		ConditionDetection::InvertIf(block, ifInst);
		// Step 6: ensure the if's TrueInst is the keyword exit. InvertIf made it Block
		// B's original final (the exit), which may be a leave-from-try (not the
		// keyword `exitInst` the caller found by walking out of the try).
		if (!ExitsStructurallyEqual(ifInst->TrueInst.get(), exitInst)) {
			auto exitClone = exitInst->Clone();
			ifInst->TrueInst = std::move(exitClone);
			ifInst->TrueInst->Parent = ifInst;
			ifInst->TrueInst->ChildIndex = 1;
		}
		return true;
	}

	// ---- else-if-tree case (and the plain if-else case) ----
	// Root-bail: an else-if tree is reduced as a single group from the root
	// IfInstruction, so a non-root else-if is not itself a reduction candidate.
	if (GetElseIfParent(ifInst) != nullptr) return false;
	// Walk down the else-if chain, tallying each else-if's then. The chain is
	// [ifRoot, ..., leaf] where leaf's FalseInst is the else content (a Block,
	// not a Block-wrapped if). For a plain if-else (no else-if chain), the walk
	// does not descend and leaf == ifRoot.
	std::vector<IfInstruction*> chain;
	chain.push_back(ifInst);
	IfInstruction* cur = ifInst;
	while (auto* elseIfInst = UnwrapElseIf(cur->FalseInst.get())) {
		UpdateStats(elseIfInst->TrueInst.get(), maxStatements, maxDepth);
		chain.push_back(elseIfInst);
		cur = elseIfInst;
	}
	IfInstruction* leaf = cur;
	// The leaf's FalseInst is the else content. A chain with no trailing else
	// (a bare Nop) has no block to reduce (the C# guards this -- #3891).
	auto* elseContent = dynamic_cast<Block*>(leaf->FalseInst.get());
	if (!elseContent || !ShouldReduceNesting(elseContent, maxStatements, maxDepth))
		return false;
	// Block B (the exit holder) is the block after Block A. The dead-exit removal
	// drops it when the else content exits.
	Block* blockB = NextBlockInContainer(block);
	// Make every then in the chain exit via the keyword exit (the C# do-while
	// calls EnsureEndPointUnreachable(ifInst.TrueInst, exitInst) per iteration;
	// the TrueInst slot is independent of the FalseInst moves, so all the
	// EnsureEndPointUnreachable calls can precede the ExtractElseBlock calls).
	for (auto* iff : chain)
		EnsureEndPointUnreachable(iff->TrueInst.get(), exitInst);
	// Extract the else blocks root-to-leaf, promoting each else block to the
	// container as a new sibling after Block A's position. See the header for why
	// root-to-leaf (not the C# leaf-to-root): an intermediate else-if's parent
	// block is not in a container until its own parent if's else block has been
	// promoted. After ExtractElseBlock(ifRoot), the next else-if's holder block
	// is in the container, so ExtractElseBlock on the next if can promote its else.
	for (auto* iff : chain)
		ExtractElseBlock(iff);
	// Dead-exit removal (the C# `if (ifInst.FalseInst.HasFlag(EndPointUnreachable))
	// block.Instructions.RemoveRange(...)` inside the do-while): when the else
	// content exits, the trailing exit (Block B) is unreachable and is dropped.
	// The C# checks the if's FalseInst per iteration; this port promotes the else
	// content to the container (its end point is invariant across the moves), so
	// a single check on the promoted else content suffices.
	if (blockB && BlockExitsReal(elseContent))
		RemoveEmptyExitBlock(blockB);
	return true;
}

// Recursively computes the number of statements and maximum nested depth of an
// instruction. Port of ReduceNestingTransform.ComputeStats, adapted to this
// port's block model (the control flow lives in Block::FinalInstruction, not in
// Instructions as in the C#).
void ReduceNestingTransform::ComputeStats(ILInstruction* inst, int& numStatements,
                                           int& maxDepth, int currentDepth, bool isStatement) {
	if (!inst) return;
	if (isStatement) numStatements++;
	if (currentDepth > maxDepth) maxDepth = currentDepth;

	if (auto* block = dynamic_cast<Block*>(inst)) {
		if (isStatement) numStatements--;  // don't count blocks as statements
		// The C# iterates Instructions (which includes the control flow) and
		// then the FinalInstruction (a Nop). This port's Instructions are the
		// non-terminals and the FinalInstruction is the control flow, so both
		// are counted as statements the way the C# counts the Instructions.
		bool isStmtForChildren = (block->Kind == BlockKind::ControlFlow);
		for (auto& child : block->Instructions)
			ComputeStats(child.get(), numStatements, maxDepth, currentDepth, isStmtForChildren);
		if (block->FinalInstruction)
			ComputeStats(block->FinalInstruction.get(), numStatements, maxDepth, currentDepth, isStmtForChildren);
		return;
	}
	if (auto* container = dynamic_cast<BlockContainer*>(inst)) {
		if (!isStatement) numStatements++;  // a container in an expression adds a statement
		Block* containerBody = container->Blocks.empty() ? nullptr : container->Blocks[0].get();
		// MatchConditionBlock is for For/While loops. This port has While (D140)
		// but not For; the For case is dropped (no For containers arise yet).
		if (container->Kind == ContainerKind::While) {
			if (!containerBody || !MatchConditionBlock(container, containerBody, containerBody))
				return;  // C# throws; this port bails (invalid condition block)
		}
		if (containerBody) {
			// The C# `containerBody.Instructions.Last()` is the control flow
			// (this port's FinalInstruction); don't count the implicit
			// back-edge / leave.
			ILInstruction* lastInst = containerBody->FinalInstruction.get();
			bool isImplicitExit = false;
			if (lastInst) {
				// For/DoWhile branch to Blocks.Last() (the increment/condition block).
				// This port has DoWhile (D143) but not For; the For case is dropped.
				if (container->Kind == ContainerKind::DoWhile
						&& !container->Blocks.empty() && MatchBranch(lastInst, container->Blocks.back().get()))
					isImplicitExit = true;
				else if ((container->Kind == ContainerKind::Loop || container->Kind == ContainerKind::While)
						&& !container->Blocks.empty() && MatchBranch(lastInst, container->Blocks.front().get()))
					isImplicitExit = true;
				else if (container->Kind == ContainerKind::Normal && MatchLeave(lastInst, container))
					isImplicitExit = true;
				else if (container->Kind == ContainerKind::Switch)
					isImplicitExit = true;  // SwitchInstruction counts as a statement already
			}
			if (isImplicitExit) numStatements--;
			ComputeStats(containerBody, numStatements, maxDepth, currentDepth + 1, true);
		}
		return;
	}
	if (auto* iff = dynamic_cast<IfInstruction*>(inst)) {
		if (iff->ResultType() != StackType::Void) {
			// A value-producing if (e.g. a ternary): fall through to the default
			// case (recurse into children as expressions).
			for (int i = 0; i < iff->ChildCount(); ++i)
				ComputeStats(iff->GetChild(i), numStatements, maxDepth, currentDepth, false);
			return;
		}
		// nested then instruction
		ComputeStats(iff->TrueInst.get(), numStatements, maxDepth, currentDepth + 1, true);
		// include all nested else-if instructions at the same depth
		ILInstruction* elseInst = iff->FalseInst.get();
		while (auto* elseIfInst = UnwrapElseIf(elseInst)) {
			numStatements++;
			ComputeStats(elseIfInst->TrueInst.get(), numStatements, maxDepth, currentDepth + 1, true);
			elseInst = elseIfInst->FalseInst.get();
		}
		// include the nested else instruction
		ComputeStats(elseInst, numStatements, maxDepth, currentDepth + 1, true);
		return;
	}
	if (auto* section = dynamic_cast<SwitchSection*>(inst)) {
		numStatements++;  // a statement for each case label
		Block* caseBlock = nullptr;
		if (section->Body && MatchBranch(section->Body.get(), caseBlock) && caseBlock) {
			// section->Parent = SwitchInstruction; .Parent = the block holding
			// the switch; .Parent = that block's container. The case block's
			// parent must be that container (a direct block of the switch).
			ILInstruction* p = section->Parent;
			ILInstruction* pp = p ? p->Parent : nullptr;
			ILInstruction* ppp = pp ? pp->Parent : nullptr;
			if (caseBlock->Parent == ppp)
				ComputeStats(caseBlock, numStatements, maxDepth, currentDepth, true);
		}
		return;
	}
	if (auto* func = dynamic_cast<ILFunction*>(inst)) {
		int bodyStatements = 0;
		int bodyMaxDepth = maxDepth;
		ComputeStats(func->Body.get(), bodyStatements, bodyMaxDepth, currentDepth, true);
		if (bodyStatements >= 2) {  // don't count inline functions
			numStatements += bodyStatements;
			maxDepth = bodyMaxDepth;
		}
		return;
	}
	// default: search each child instruction. Containers will contain
	// statements and contribute to stats.
	int subStatements = 0;
	for (int i = 0; i < inst->ChildCount(); ++i)
		ComputeStats(inst->GetChild(i), subStatements, maxDepth, currentDepth, false);
	numStatements += subStatements;
	if (isStatement && subStatements > 0)
		numStatements--;  // don't double-count the first container
}

// The C# UpdateStats: tally one path's stats and take the max.
void ReduceNestingTransform::UpdateStats(ILInstruction* inst, int& maxStatements, int& maxDepth) {
	int numStatements = 0;
	ComputeStats(inst, numStatements, maxDepth, 0, true);
	maxStatements = std::max(numStatements, maxStatements);
}

// The C# ShouldReduceNesting: if the max depth is 2, always reduce; if 1,
// reduce if this block is the largest; otherwise reduce only if twice as large.
bool ReduceNestingTransform::ShouldReduceNesting(Block* block, int maxStatements, int maxDepth) {
	int maxStatements2 = 0, maxDepth2 = 0;
	UpdateStats(block, maxStatements2, maxDepth2);
	return maxDepth2 >= 2 || (maxDepth2 >= 1 && maxStatements2 > maxStatements) || maxStatements2 >= 2 * maxStatements;
}

void ReduceNestingTransform::Run(ILFunction& function, ILTransformContext& context) {
	// The C# `Run` calls `Visit((BlockContainer)function.Body, null)` (the
	// nesting-reduction + ImproveILOrdering folds) then the
	// EliminateRedundantTryFinally loop. This iteration ports ImproveILOrdering
	// (the IL-order-gated InvertIf, the simplest wired fold); the
	// ReduceNesting/ReduceSwitchNesting/ExtractElseBlock folds are deferred
	// (need the full CanDuplicateExit/EnsureEndPointUnreachable/ExtractElseBlock
	// helpers + the D39 dominator analysis). Visit recurses into every
	// BlockContainer's blocks and calls ImproveILOrdering on each
	// if-as-FinalInstruction.
	VisitContainer(dynamic_cast<BlockContainer*>(function.Body.get()), nullptr);
	// EliminateRedundantTryFinally: the C# iterates
	// `function.Descendants.OfType<TryFinally>()` and folds each. This port has
	// no GC: folding an outer TryFinally destroys any TryFinallys nested in its
	// try block, dangling their collected pointers. Re-walk the tree for each
	// fold instead (the redundant shape is rare, so the bounded re-walks are
	// cheap) -- the walk stops at the first candidate, folds it, then the outer
	// loop re-walks the mutated tree.
	bool changed = true;
	while (changed) {
		changed = false;
		TryFinally* target = nullptr;
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst || target) return;
			if (auto* tf = dynamic_cast<TryFinally*>(inst)) {
				if (CanEliminate(tf)) {
					target = tf;
					return;
				}
			}
			for (int i = 0; i < inst->ChildCount() && !target; ++i)
				walk(inst->GetChild(i));
		};
		walk(function.Body.get());
		if (target) {
			context.StepOnce("Removing try-finally around PinnedRegion");
			Eliminate(target);
			changed = true;
		}
	}
}

} // namespace ILSpy::Decompiler::IL
