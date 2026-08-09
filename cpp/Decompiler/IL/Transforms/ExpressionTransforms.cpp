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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

bool IsLdcI4(const ILInstruction* inst, int value) {
    if (!inst || inst->Op != OpCode::LdcI4) return false;
    return static_cast<const LdcI4*>(inst)->Value == value;
}

// The C# `inst.Right.UnwrapConv(SignExtend).UnwrapConv(ZeroExtend).MatchLdcI4(0)`
// unwraps sign/zero-extending convs around the 0 before testing it. This port's
// Conv carries no Kind, so UnwrapConv-by-Kind cannot be faithful; the common
// shape is a bare `ldc.i4 0` (or a conv wrapping it), both accepted here. A conv
// of any target type wrapping `ldc.i4 0` is treated as 0 -- the rare non-
// sign/zero-extend conv around 0 would be mis-recognised, but it does not arise
// from the IL reader's comparisons today.
bool IsLdcI4ZeroMaybeConv(const ILInstruction* inst) {
    if (IsLdcI4(inst, 0)) return true;
    if (inst && inst->Op == OpCode::Conv) {
        auto* conv = static_cast<const Conv*>(inst);
        if (conv->Argument) return IsLdcI4(conv->Argument.get(), 0);
    }
    return false;
}

bool IsEqualityOrInequality(ComparisonKind k) {
    return k == ComparisonKind::Equality || k == ComparisonKind::Inequality;
}

// Approximation of the C# `comp.InputType.IsFloatType()`. The port's Comp carries
// no InputType, so the operands' StackType is the best available signal: a
// comparison of F4/F8 values is a float comparison. Used to suppress the
// logic.not push on float comparisons (negating a float ordering is not a
// simple kind flip), matching the C# `!comp.InputType.IsFloatType()` guard.
bool IsFloatComp(const Comp* comp) {
    auto IsFloat = [](const std::unique_ptr<ILInstruction>& inst) {
        if (!inst) return false;
        StackType s = inst->ResultType();
        return s == StackType::F4 || s == StackType::F8;
    };
    return IsFloat(comp->Left) || IsFloat(comp->Right);
}

// Port of IfInstruction.IsInConditionSlot: the instruction occurs in a context
// where it is being compared with 0 (an if condition, or nested in an if's
// true/false arm, or as one side of a comp whose other side is ldc.i4 0). The C#
// checks SlotInfo; this port uses Parent + ChildIndex (Condition = 0, TrueInst =
// 1, FalseInst = 2). The NullCoalescingInstruction.FallbackInstSlot case is
// dropped (no NullCoalescing node in this port).
bool IsInConditionSlot(const ILInstruction* inst) {
    if (!inst || !inst->Parent) return false;
    const ILInstruction* parent = inst->Parent;
    if (parent->Op == OpCode::IfInstruction) {
        if (inst->ChildIndex == 0) return true;  // Condition slot
        // TrueInst / FalseInst: recurse on the parent if (the inner if is itself
        // in a condition slot of an outer if).
        if (inst->ChildIndex == 1 || inst->ChildIndex == 2)
            return IsInConditionSlot(parent);
    } else if (parent->Op == OpCode::Comp) {
        auto* comp = static_cast<const Comp*>(parent);
        if (inst->ChildIndex == 0 && IsLdcI4(comp->Right.get(), 0)) return true;
        if (inst->ChildIndex == 1 && IsLdcI4(comp->Left.get(), 0)) return true;
    }
    return false;
}

// The next block in the block's container (the if-final's positional
// fall-through), or null if the block is last in its container. Mirrors the
// SwitchAnalysis / CachedDelegateInitialization / HighLevelLoopTransform helper.
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

// Negate a boolean condition, folding into a Comp when possible (mirrors the
// ConditionDetection::NegateCondition helper and the C# Comp.LogicNot):
// logic.not(comp(inner == 0)) -> inner; logic.not(comp(a op b)) ->
// comp(a op.Negate b); otherwise wrap as comp(cond == 0) (primitives) or
// comp(cond == null) (object-typed). The returned instruction is detached (no
// parent); the caller wires it into its slot.
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
    auto zero = (cond->ResultType() == StackType::O)
        ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
        : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
    return std::make_unique<Comp>(std::move(cond), std::move(zero),
                                  ComparisonKind::Equality);
}

// True if `arm` is `ldc.i4 value`, either a bare LdcI4 or an expression Block
// (no FinalInstruction) wrapping a single LdcI4 instruction. The C# arms are
// bare LdcI4 after ConditionDetection; this port may wrap a single-instruction
// arm in a Block (the inlined-fall-through shape), so both forms are accepted.
bool ArmIsLdcI4(const ILInstruction* arm, int value) {
    if (!arm) return false;
    if (arm->Op == OpCode::LdcI4)
        return static_cast<const LdcI4*>(arm)->Value == value;
    if (arm->Op == OpCode::Block) {
        auto* blk = static_cast<const Block*>(arm);
        if (blk->FinalInstruction || blk->Instructions.size() != 1) return false;
        return ArmIsLdcI4(blk->Instructions[0].get(), value);
    }
    return false;
}

} // namespace

void ExpressionTransforms::Run(Block& block, int pos, StatementTransformContext& /*context*/) {
    // Visit the statement at `pos` and its children (the C# AcceptVisitor on
    // block.Instructions[pos]). The sentinel pos = -1 (the driver's signal for an
    // if-final-only block -- see StatementTransform::RunBlock) means "no
    // non-terminal statement"; only the if-final is visited below.
    if (pos >= 0 && static_cast<std::size_t>(pos) < block.Instructions.size()) {
        Visit(block.Instructions[static_cast<std::size_t>(pos)].get());
    }

    // Block-model compensation: this port's IfInstruction is the block's
    // FinalInstruction (the C# carries the if as a non-terminal at
    // Instructions[Count-2], so the per-statement driver visits it and recurses
    // into its condition/arms). Visit the if-final at the last non-terminal
    // position (pos == size - 1, the port's equivalent of the C# visiting the if
    // at Count-2) so the VisitIfInstruction rewrites -- the Comp rewrites on the
    // condition, HandleConditionalOperator (the ternary fold), and the logic.and/or
    // canonicalization -- fire on if-final blocks. For an if-final-only block
    // (size == 0) the driver calls this with pos = -1 == size - 1, so the if-final
    // is visited there too.
    if (pos == static_cast<int>(block.Instructions.size()) - 1) {
        auto* iff = dynamic_cast<IfInstruction*>(block.FinalInstruction.get());
        if (iff) VisitIfInstruction(iff);
    }
}

void ExpressionTransforms::Visit(ILInstruction* inst) {
    if (!inst) return;
    if (inst->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(inst);
        if (VisitCompHeadRewrites(comp)) return;  // rewritten + re-visited
        // base.VisitComp: recurse into the operands (children visited before the
        // tail rewrites, matching the C# order).
        Visit(comp->Left.get());
        Visit(comp->Right.get());
        if (VisitCompTailRewrites(comp)) return;  // rewritten + re-visited
        return;
    }
    if (inst->Op == OpCode::IfInstruction) {
        VisitIfInstruction(static_cast<IfInstruction*>(inst));
        return;
    }
    // Default: recurse into children (the C# ILVisitor.Default).
    for (int i = 0; i < inst->ChildCount(); ++i) Visit(inst->GetChild(i));
}

bool ExpressionTransforms::VisitCompHeadRewrites(Comp* comp) {
    // 1. logic.not: comp(Equality, X, ldc.i4 0) is `!X` (the reader's brfalse
    //    shape; see SwitchAnalysis / ConditionDetection). The C# MatchLogicNot
    //    is Kind == Equality && LiftingKind.None && Right.MatchLdcI4(0); the port
    //    drops LiftingKind (no nullable lifting modelled). If X is itself a Comp,
    //    push the negation into it (`!(a OP b)` -> `a OP.Negate b`); else keep the
    //    logic.not and just visit X's subtree. The float guard suppresses the
    //    push on a float ordering (the C# `!comp.InputType.IsFloatType()`); an
    //    equality/inequality is always pushed (the `|| IsEqualityOrInequality`).
    if (comp->Kind == ComparisonKind::Equality && IsLdcI4(comp->Right.get(), 0)) {
        auto* inner = dynamic_cast<Comp*>(comp->Left.get());
        if (inner) {
            if (!IsFloatComp(inner) || IsEqualityOrInequality(inner->Kind)) {
                inner->Kind = NegateComparison(inner->Kind);
                auto innerOwned = comp->TakeChild(0);  // detach the inner comp
                // inner still points at the (now-owned) object after the move.
                comp->ReplaceWith(std::move(innerOwned));
            }
            // Re-visit the inner comp (now in comp's old slot if pushed, else
            // still comp->Left) -- matches the C# `comp.AcceptVisitor(this)`.
            Visit(inner);
            return true;
        }
        // arg is not a Comp: the C# checks MatchLogicAnd/MatchLogicOr (deferred
        // -- they need IfInstruction true/false-arm patterns plus new Comp/If
        // construction) and otherwise visits arg. Visit arg's subtree and keep
        // the logic.not (`!arg`).
        Visit(comp->Left.get());
        return true;
    }

    // 2. comp(x != 0) => x: drop a redundant inequality against 0 when the comp
    //    is in a condition slot (e.g. an if condition) or its left is itself a
    //    comp (`comp(comp(...) != 0)` -> `comp(...)`). The C# guards on
    //    LiftingKind.None (dropped, no nullable lifting) and IsInConditionSlot.
    if (comp->Kind == ComparisonKind::Inequality && IsLdcI4(comp->Right.get(), 0) &&
        (IsInConditionSlot(comp) || (comp->Left && comp->Left->Op == OpCode::Comp))) {
        auto left = comp->TakeChild(0);
        ILInstruction* leftPtr = left.get();
        comp->ReplaceWith(std::move(left));
        Visit(leftPtr);
        return true;
    }

    return false;
}

bool ExpressionTransforms::VisitCompTailRewrites(Comp* comp) {
    // comp.unsigned(left > 0) => comp(left != 0)
    // comp.unsigned(left <= 0) => comp(left == 0)
    // An unsigned comparison against 0 is really a (non-)zero test: `x >u 0` is
    // `x != 0`, `x <=u 0` is `x == 0`. The C# unwraps sign/zero-extending convs
    // around the 0 first (UnwrapConv); the port approximates with
    // IsLdcI4ZeroMaybeConv (no Conv Kind). Runs after the operands are visited
    // (the C# order); the re-visit re-runs the head rewrites so a result in a
    // condition slot (e.g. `if (x >u 0)`) cascades to `if (x)`.
    if (comp->Unsigned && IsLdcI4ZeroMaybeConv(comp->Right.get()) &&
        (comp->Kind == ComparisonKind::GreaterThan ||
         comp->Kind == ComparisonKind::LessThanOrEqual)) {
        comp->Kind = (comp->Kind == ComparisonKind::GreaterThan)
                         ? ComparisonKind::Inequality
                         : ComparisonKind::Equality;
        Visit(comp);  // re-visit (the C# `VisitComp(inst); return;`)
        return true;
    }
    return false;
}

void ExpressionTransforms::VisitIfInstruction(IfInstruction* iff) {
    if (!iff) return;
    // The C# visits TrueInst and FalseInst (recursing), then runs
    // HandleConditionalOperator, the logic.and/or canonicalization, and finally
    // visits the condition. Skip control-flow Block arms (the C# skips
    // BlockKind.ControlFlow): an arm Block with a FinalInstruction is a
    // control-flow block the block transform already handled; an expression Block
    // arm (no FinalInstruction) and a bare non-Block arm are visited.
    VisitArm(iff->TrueInst.get());
    VisitArm(iff->FalseInst.get());

    if (HandleConditionalOperator(iff)) return;  // the if-final was destroyed

    CanonicalizeLogicAndOr(iff);  // may swap arms + negate the condition

    // Process the condition after the potential modifications (the C# order).
    if (iff->Condition) Visit(iff->Condition.get());
}

void ExpressionTransforms::VisitArm(ILInstruction* arm) {
    if (!arm) return;
    if (arm->Op == OpCode::Block) {
        auto* blk = static_cast<Block*>(arm);
        // A Block arm with a FinalInstruction is a control-flow block (the C#
        // skips BlockKind.ControlFlow -- those were already handled by the block
        // transform). An expression Block arm (no FinalInstruction) is visited so
        // the Comp/StLoc rewrites fire on its instructions.
        if (blk->FinalInstruction) return;
        for (auto& inst : blk->Instructions) Visit(inst.get());
        return;
    }
    Visit(arm);
}

bool ExpressionTransforms::HandleConditionalOperator(IfInstruction* iff) {
    // if (cond) stloc A(V1) else stloc A(V2) --> stloc A(if (cond) V1 else V2)
    // Both arms must be expression Blocks (no FinalInstruction) with exactly one
    // StLoc to the same variable. The C# does an in-place ReplaceWith (the if is a
    // non-terminal at Instructions[Count-2]); this port makes the if the block's
    // FinalInstruction, so the StLoc goes into the block's Instructions and a
    // Branch to the next block (the positional fall-through) replaces the if-final.
    auto* trueBlock = dynamic_cast<Block*>(iff->TrueInst.get());
    if (!trueBlock || trueBlock->FinalInstruction ||
        trueBlock->Instructions.size() != 1)
        return false;
    auto* falseBlock = dynamic_cast<Block*>(iff->FalseInst.get());
    if (!falseBlock || falseBlock->FinalInstruction ||
        falseBlock->Instructions.size() != 1)
        return false;
    auto* trueSt = dynamic_cast<StLoc*>(trueBlock->Instructions[0].get());
    if (!trueSt || !trueSt->Value) return false;
    auto* falseSt = dynamic_cast<StLoc*>(falseBlock->Instructions[0].get());
    if (!falseSt || !falseSt->Value) return false;
    // Both stores must target the same variable (the conditional operator's temp).
    if (trueSt->Variable.get() != falseSt->Variable.get()) return false;

    // The if-final's block and its positional fall-through (the common exit the
    // arms branch to, which TryDropCommonExit already made the next block). The
    // fold requires a next block so the new Branch final is well-defined; a
    // ternary whose exit is not the next block (rare) is left as if/else.
    auto* block = dynamic_cast<Block*>(iff->Parent);
    if (!block || block->FinalInstruction.get() != iff) return false;
    Block* nextBlock = NextBlockInContainer(block);
    if (!nextBlock) return false;

    // Detach the values and the condition (the arm StLocs and the Block arms are
    // destroyed with the if when the final is replaced).
    auto value1 = trueSt->TakeChild(0);   // V1 (the true-arm value)
    auto value2 = falseSt->TakeChild(0);   // V2 (the false-arm value)
    auto cond = iff->TakeChild(0);         // the if's condition
    ILVariablePtr v = trueSt->Variable;    // the temp (shared_ptr copy)

    // stloc A(if (!cond) V2 else V1)) -- the C# negates the condition so the
    // true arm stays V1 and the false arm V2 (matching `cond ? V1 : V2`).
    auto negCond = NegateCondition(std::move(cond));
    auto newIf = std::make_unique<IfInstruction>(std::move(negCond),
                                                  std::move(value2),
                                                  std::move(value1));
    auto stLoc = std::make_unique<StLoc>(v, std::move(newIf));
    ILInstruction* stPtr = stLoc.get();

    // The StLoc becomes a non-terminal in the block; a Branch to the next block
    // (the positional fall-through) replaces the if-final. The if (and its Block
    // arms, now value-less shells) is destroyed by SetFinal.
    block->Add(std::move(stLoc));
    block->SetFinal(std::make_unique<Branch>(nextBlock));

    // Cascade the Comp rewrites into the new StLoc's value (the negated
    // condition and the arm values), matching the C# `context.RequestRerun()`.
    Visit(stPtr);
    return true;
}

bool ExpressionTransforms::CanonicalizeLogicAndOr(IfInstruction* iff) {
    // Bring LogicAnd/LogicOr into their canonical forms:
    //   if (cond) ldc.i4 0 else RHS --> if (!cond) RHS else ldc.i4 0
    //   if (cond) RHS else ldc.i4 1 --> if (!cond) ldc.i4 1 else RHS
    // Be careful: when both LHS and RHS are the constant 1, we must not swap the
    // arguments as it would lead to an infinite transform loop (the C# guard
    // `!inst.FalseInst.MatchLdcI4(0)` / `!inst.TrueInst.MatchLdcI4(1)`).
    bool swap = false;
    if (ArmIsLdcI4(iff->TrueInst.get(), 0) && !ArmIsLdcI4(iff->FalseInst.get(), 0))
        swap = true;
    else if (ArmIsLdcI4(iff->FalseInst.get(), 1) && !ArmIsLdcI4(iff->TrueInst.get(), 1))
        swap = true;
    if (!swap) return false;

    auto t = std::move(iff->TrueInst);
    iff->TrueInst = std::move(iff->FalseInst);
    iff->FalseInst = std::move(t);
    if (iff->TrueInst) { iff->TrueInst->Parent = iff; iff->TrueInst->ChildIndex = 1; }
    if (iff->FalseInst) { iff->FalseInst->Parent = iff; iff->FalseInst->ChildIndex = 2; }
    // Negate the condition (Comp.LogicNot) and re-wire it into the Condition slot.
    iff->Condition = NegateCondition(std::move(iff->Condition));
    if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
    return true;
}

} // namespace ILSpy::Decompiler::IL
