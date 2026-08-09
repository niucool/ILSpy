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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/VariableKind.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). Mirrors the helper in CachedDelegateInitialization
// / PatternMatchingTransform / SwitchAnalysis. nullptr if `block` is not in a
// container's Blocks list or is the last block.
Block* NextBlockInContainer(Block* block) {
    if (!block) return nullptr;
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
        }
    }
    return nullptr;
}

// comp(Equality, X, ldnull) -> X (either side may be the null). The reader's
// brfalse on an object-typed value uses Equality (`comp(eq, ldloc s, ldnull)`);
// ConditionDetection inverts the brtrue `comp(ne, ..)` form to Equality via
// TryInvertIfExit (see D44/D53). Sign-independent.
bool MatchCompEqualsNull(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (!comp || comp->Kind != ComparisonKind::Equality) return false;
    auto* rhs = comp->Right.get();
    auto* lhs = comp->Left.get();
    if (rhs && rhs->Op == OpCode::LdNull) { arg = lhs; return true; }
    if (lhs && lhs->Op == OpCode::LdNull) { arg = rhs; return true; }
    return false;
}

bool IsLdLocOf(const ILInstruction* inst, const ILVariable* v) {
    if (!inst || inst->Op != OpCode::LdLoc) return false;
    return static_cast<const LdLoc*>(inst)->Variable.get() == v;
}

// Port of Block.Unwrap: a single-instruction expression Block (no
// FinalInstruction) unwraps to its one instruction; anything else (a control-
// flow Block with a final, a multi-instruction Block, or a bare instruction)
// returns the input unchanged. The C# `Block.Unwrap(trueInst)` used by
// NullCoalescingTransform unwraps the inlined-fall-through Block arm.
ILInstruction* BlockUnwrap(ILInstruction* arm) {
    if (!arm || arm->Op != OpCode::Block) return arm;
    auto* blk = static_cast<Block*>(arm);
    if (blk->FinalInstruction || blk->Instructions.size() != 1) return arm;
    return blk->Instructions[0].get();
}

} // namespace

void NullCoalescingTransform::Run(Block& block, int pos, StatementTransformContext& context) {
    if (TransformRefTypes(block, pos, context)) return;
    // TransformHoistedConstructorArgumentNullGuard and TransformThrowExpression-
    // ValueTypes are deferred (need ILFunction.Method metadata +
    // ILInlining.IsInConstructorInitializer + ILInlining.FindLoadInNext with a
    // movable expression + MatchLogicNot / MatchHasValueCall wiring for the
    // value-types case). The reference-type `a ?? throw ...` arm is handled in
    // TransformRefTypes (gated on the ThrowExpressions setting).
}

bool NullCoalescingTransform::TransformRefTypes(Block& block, int pos,
                                                StatementTransformContext& context) {
    // The stloc at `pos` must be a store to a StackSlot variable (the C# checks
    // `stloc.Variable.Kind != VariableKind.StackSlot`).
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size()) return false;
    auto* stloc = dynamic_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (!stloc || !stloc->Value) return false;
    if (!stloc->Variable || stloc->Variable->Kind != VariableKind::StackSlot) return false;
    ILVariable* s = stloc->Variable.get();

    // The if is the block's FinalInstruction (this port's block model: the C#
    // reads `block.Instructions[pos+1].MatchIfInstruction`; here the if is the
    // final, not a non-terminal at pos+1).
    auto* iff = dynamic_cast<IfInstruction*>(block.FinalInstruction.get());
    if (!iff) return false;

    // The condition must be comp(eq, ldloc s, ldnull) (the reader's brfalse
    // form, which ConditionDetection inverts the brtrue `comp(ne, ..)` form to
    // via TryDropCommonExit + TryInvertIfExit -- see D44/D53).
    ILInstruction* condArg = nullptr;
    if (!MatchCompEqualsNull(iff->Condition.get(), condArg)) return false;
    if (!IsLdLocOf(condArg, s)) return false;

    // The ?? pattern has no else (the else is the fall-through to the use, which
    // ConditionDetection dropped). Bail if there is an else arm (not the ??
    // pattern -- the C# does not check, but the ?? never has an else, so bailing
    // is safer and avoids losing the else when replacing the if-final).
    if (iff->FalseInst) return false;

    // The fall-through target (the use block) -- needed to replace the if-final
    // with a Branch to it. Resolve BEFORE detaching anything (the precondition-
    // before-mutation discipline: a detached condition with no fold would corrupt
    // the tree).
    Block* nextBlock = NextBlockInContainer(&block);
    if (!nextBlock) return false;  // no fall-through; the ?? is at the function end (degenerate)

    // Unwrap the true arm (Block.Unwrap): a single-instruction expression Block
    // unwraps to its instruction.
    ILInstruction* trueInst = BlockUnwrap(iff->TrueInst.get());

    // Simple case: trueInst is stloc s(fallback).
    //   stloc s(value); if (comp(eq, s, ldnull)) { Block { stloc s(fallback) } }
    //   => stloc s(if.notnull(value, fallback))
    if (trueInst && trueInst->Op == OpCode::StLoc) {
        auto* fbSt = static_cast<StLoc*>(trueInst);
        if (fbSt->Variable.get() == s && fbSt->Value) {
            context.Base.StepOnce("NullCoalescingTransform: simple (reference types)");
            // Detach the value (a) and the fallback (b) before the old nodes are
            // destroyed (no GC; a raw pointer would dangle). The if and its TrueInst
            // Block are destroyed when SetFinal replaces the if-final.
            auto value = stloc->TakeChild(0);
            auto fallback = fbSt->TakeChild(0);
            auto nc = std::make_unique<NullCoalescingInstruction>(
                NullCoalescingKind::Ref, std::move(value), std::move(fallback));
            stloc->SetChild(0, std::move(nc));  // stloc.Value = if.notnull(value, fallback)
            // Replace the if-final with a Branch to the next block (the explicit
            // fall-through the if's null FalseInst represented). Destroys the if
            // and the now-empty TrueInst Block.
            block.SetFinal(std::make_unique<Branch>(nextBlock));
            // Try to inline the single-use stloc s into its load (the C#
            // ILInlining.InlineOneIfPossible). The use is typically in the next
            // block (not the same block), so this is usually a no-op here and a
            // later CFS / ILInlining pass merges the blocks and folds it; the
            // call is faithful to the C# and handles the same-block use case.
            InlineOneIfPossible(&block, pos, context.Base);
            return true;
        }
    }

    // Temp case: the true arm is a 2-instruction expression Block
    //   { stloc temp(fallback); stloc s(ldloc temp) }
    // (the compiler used a temp for the fallback). temp must be single-def and
    // loaded exactly once (the copy into s).
    if (iff->TrueInst && iff->TrueInst->Op == OpCode::Block) {
        auto* trueBlock = static_cast<Block*>(iff->TrueInst.get());
        if (!trueBlock->FinalInstruction && trueBlock->Instructions.size() == 2) {
            auto* st0 = dynamic_cast<StLoc*>(trueBlock->Instructions[0].get());
            auto* st1 = dynamic_cast<StLoc*>(trueBlock->Instructions[1].get());
            if (st0 && st1 && st0->Value && st1->Value &&
                st1->Variable.get() == s &&
                st0->Variable && st0->Variable->IsSingleDefinition() &&
                st0->Variable->LoadCount == 1 &&
                IsLdLocOf(st1->Value.get(), st0->Variable.get())) {
                context.Base.StepOnce(
                    "NullCoalescingTransform: with temporary variable (reference types)");
                auto value = stloc->TakeChild(0);       // the value (a)
                auto fallback = st0->TakeChild(0);        // the fallback (b) -- the temp's value
                auto nc = std::make_unique<NullCoalescingInstruction>(
                    NullCoalescingKind::Ref, std::move(value), std::move(fallback));
                stloc->SetChild(0, std::move(nc));
                block.SetFinal(std::make_unique<Branch>(nextBlock));
                InlineOneIfPossible(&block, pos, context.Base);
                return true;
            }
        }
    }

    // The throw-expression case: `stloc s(value); if (comp(eq, s, ldnull))
    // throw(arg) }` -> `stloc s(if.notnull(value, throw(arg)))` (the C# 7.0
    // `a ?? throw ...` form). The Throw is normally Void-result, but the
    // throw-expression form mutates it to O so the NullCoalescingInstruction's
    // ResultType (the FallbackInst's, the Throw) matches the reference-type
    // value. Gated on the ThrowExpressions setting (DecompilerSettings, default
    // true -- false only for the C# 6 / .NET Framework 1.x profile). The true
    // arm may be a bare Throw or a single-instruction expression Block wrapping
    // one (Block.Unwrap).
    if (context.Base.Settings.ThrowExpressions) {
        ILInstruction* throwArm = BlockUnwrap(iff->TrueInst.get());
        if (throwArm && throwArm->Op == OpCode::Throw) {
            auto* throwInst = static_cast<Throw*>(throwArm);
            context.Base.StepOnce(
                "NullCoalescingTransform: reference types + throw expression");
            auto value = stloc->TakeChild(0);
            // Detach the Throw's argument before the if is destroyed (no GC; a
            // raw pointer would dangle). Build a fresh Throw carrying the
            // argument and the O result type -- the throw-expression form
            // mutates the Throw's resultType to O so the NullCoalescing-
            // Instruction's ResultType (the FallbackInst's) matches the
            // reference-type value, faithful to the C#
            // `throwInst.resultType = StackType.O`.
            auto throwArg = throwInst->TakeChild(0);
            auto newThrow = std::make_unique<Throw>(std::move(throwArg));
            newThrow->resultType = StackType::O;
            auto nc = std::make_unique<NullCoalescingInstruction>(
                NullCoalescingKind::Ref, std::move(value), std::move(newThrow));
            stloc->SetChild(0, std::move(nc));
            block.SetFinal(std::make_unique<Branch>(nextBlock));
            InlineOneIfPossible(&block, pos, context.Base);
            return true;
        }
    }

    return false;
}

} // namespace ILSpy::Decompiler::IL
