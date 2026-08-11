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
// Two pieces are ported so far:
//
//  (1) EliminateRedundantTryFinally: the C# compiler sometimes generates a
//      try-finally around a `fixed` statement; once DetectPinnedRegions has
//      formed the PinnedRegion the try-finally is redundant (its finally is an
//      empty `leave (nop)`) and is replaced with the PinnedRegion directly:
//        .try BlockContainer { Block { PinnedRegion ... } }
//        .finally BlockContainer { Block { leave IL_xxxx (nop) } }  ==>  PinnedRegion ...
//
//  (2) The nesting-reduction heuristics (ComputeStats / UpdateStats /
//      ShouldReduceNesting) plus the self-contained pattern helpers
//      (BlockUnwrap / MatchBranch / MatchLeave / MatchConditionBlock), ported
//      as a tested-but-not-yet-wired foundation ahead of the wired
//      Visit / ReduceNesting / ReduceSwitchNesting / ImproveILOrdering /
//      ExtractElseBlock folds (which need a general ILInstruction.Clone for
//      the keyword-exit duplication [D147, now landed] + the
//      ConditionDetection.InvertIf / GetStartILOffset statics exposed + a
//      block-model corpus probe of the real post-ConditionDetection shape).
//
// Block-model adaptation: the C# Block.Instructions does NOT include the
// FinalInstruction (a void block's final is a Nop, and the control flow
// Leave/Branch are non-terminals in Instructions). This port splits the block's
// non-terminal Instructions from its FinalInstruction (the control flow), so
// the C# `block.Instructions.Last()` (the last non-terminal, which is the
// control flow in the C#) is this port's `block->FinalInstruction`. The
// heuristics are adapted accordingly.

#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
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

} // namespace

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
	// The C# iterates `function.Descendants.OfType<TryFinally>()` and folds
	// each. This port has no GC: folding an outer TryFinally destroys any
	// TryFinallys nested in its try block, dangling their collected pointers.
	// Re-walk the tree for each fold instead (the redundant shape is rare, so
	// the bounded re-walks are cheap) -- the walk stops at the first candidate,
	// folds it, then the outer loop re-walks the mutated tree.
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
