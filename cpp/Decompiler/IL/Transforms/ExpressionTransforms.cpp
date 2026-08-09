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
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
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

} // namespace

void ExpressionTransforms::Run(Block& block, int pos, StatementTransformContext& /*context*/) {
    // Visit the statement at `pos` and its children (the C# AcceptVisitor on
    // block.Instructions[pos]). The sentinel pos = -1 (the driver's signal for an
    // if-final-only block -- see StatementTransform::RunBlock) means "no
    // non-terminal statement"; only the if-final's condition is visited below.
    if (pos >= 0 && static_cast<std::size_t>(pos) < block.Instructions.size()) {
        Visit(block.Instructions[static_cast<std::size_t>(pos)].get());
    }

    // Block-model compensation: this port's IfInstruction is the block's
    // FinalInstruction (the C# carries the if as a non-terminal at
    // Instructions[Count-2], so the per-statement driver visits it and recurses
    // into its condition). Visit the if's condition at the last non-terminal
    // position (pos == size - 1, the port's equivalent of the C# visiting the if
    // at Count-2) so the Comp rewrites -- e.g. `if (comp(x != 0))` -> `if (x)` --
    // fire on if-final blocks. For an if-final-only block (size == 0) the driver
    // calls this with pos = -1 == size - 1, so the condition is visited there too.
    if (pos == static_cast<int>(block.Instructions.size()) - 1) {
        auto* iff = dynamic_cast<IfInstruction*>(block.FinalInstruction.get());
        if (iff && iff->Condition) Visit(iff->Condition.get());
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

} // namespace ILSpy::Decompiler::IL
