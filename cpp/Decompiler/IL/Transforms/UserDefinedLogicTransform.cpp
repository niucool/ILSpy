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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/UserDefinedLogicTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::IL {

namespace {

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). Mirrors the helper in NullCoalescingTransform /
// CachedDelegateInitialization / PatternMatchingTransform / SwitchAnalysis.
// nullptr if `block` is not in a container's Blocks list or is the last block.
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

// The short method name (after "::"), mirroring the NullableLiftingTransform /
// CompoundAssignmentInstruction.cpp ShortMethodName helpers. The Call carries
// the resolved "Namespace.Type::Method" name; the operator-name checks consult
// the short part.
std::string_view ShortMethodName(std::string_view fullName) {
    auto pos = fullName.rfind("::");
    if (pos == std::string_view::npos) return fullName;
    return fullName.substr(pos + 2);
}

// logic.not(X) is this port's `comp(Equality, X, ldc.i4(0))` shape (the reader's
// brfalse, per the SwitchAnalysis / ConditionDetection / MatchInstruction
// convention). Returns true and sets `arg` to the negated expression (comp->Left).
// Sign-independent, so a `comp.eq.un X 0` (Unsigned) is not a brfalse shape.
// Mirrors the file-local MatchLogicNot in ExpressionTransforms.cpp /
// NullableLiftingTransform.cpp / MatchInstruction.hpp.
bool MatchLogicNot(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Comp) return false;
    auto* comp = static_cast<Comp*>(inst);
    if (comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
    if (!comp->Right || comp->Right->Op != OpCode::LdcI4) return false;
    if (static_cast<LdcI4*>(comp->Right.get())->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

bool IsLdLocOf(const ILInstruction* inst, const ILVariable* v) {
    if (!inst || inst->Op != OpCode::LdLoc) return false;
    return static_cast<const LdLoc*>(inst)->Variable.get() == v;
}

// Port of ILVariable.IsUsedWithin: whether `v` occurs as the Variable operand of
// any variable-bearing instruction (LdLoc/LdLoca/StLoc/MatchInstruction/
// TryCatchHandler -- the IInstructionWithVariableOperand kinds) within `inst`.
// A tree walk, matching the D110 TransformCatchVariable CollectCatchVariableUses
// / D130 RecombineVariables ReassignUses precedent (this port never built the
// C# per-variable LoadInstructions/StoreInstructions/AddressInstructions lists).
bool IsUsedWithin(const ILInstruction* inst, const ILVariable* v) {
    if (!inst || !v) return false;
    switch (inst->Op) {
        case OpCode::LdLoc:
            if (static_cast<const LdLoc*>(inst)->Variable.get() == v) return true;
            break;
        case OpCode::LdLoca:
            if (static_cast<const LdLoca*>(inst)->Variable.get() == v) return true;
            break;
        case OpCode::StLoc:
            if (static_cast<const StLoc*>(inst)->Variable.get() == v) return true;
            break;
        default:
            break;
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        if (IsUsedWithin(inst->GetChild(i), v)) return true;
    return false;
}

// Unwrap the legacy pattern's TrueInst Block to the single stloc it carries.
// In the C# block model the TrueInst is a Block with one instruction (the stloc)
// and no final (the fall-through is implicit); Block.Unwrap returns it. In this
// port's block model the Block carries an explicit Branch final (the fall-through
// to the join), so the NullCoalescingTransform BlockUnwrap (which requires no
// final) does not apply. This helper recognises a Block with exactly one
// non-terminal instruction (the stloc) and a Branch final, returning the stloc;
// a bare StLoc trueInst (ConditionDetection unwrapped the Block) is returned
// unchanged. Anything else returns null (not the legacy shape).
StLoc* UnwrapLegacyTrueInst(ILInstruction* trueInst) {
    if (!trueInst) return nullptr;
    if (trueInst->Op == OpCode::StLoc)
        return static_cast<StLoc*>(trueInst);
    if (trueInst->Op != OpCode::Block) return nullptr;
    auto* blk = static_cast<Block*>(trueInst);
    if (blk->Instructions.size() != 1) return nullptr;
    auto* st = dynamic_cast<StLoc*>(blk->Instructions[0].get());
    if (!st) return nullptr;
    // The Block's final must be a Branch (the explicit fall-through to the join);
    // a Leave final would be an early-return shape, not the legacy pattern.
    if (!blk->FinalInstruction || blk->FinalInstruction->Op != OpCode::Branch) return nullptr;
    return st;
}

} // namespace

bool UserDefinedLogicTransform::MatchCondition(const ILInstruction* condition,
                                                ILVariablePtr& v,
                                                std::string& conditionMethodName) {
    v = nullptr;
    conditionMethodName.clear();
    if (!condition || condition->Op != OpCode::Call) return false;
    auto* call = const_cast<Call*>(static_cast<const Call*>(condition));
    // The C# `call.Method.IsOperator && call.Arguments.Count == 1 && !call.IsLifted`.
    if (!call->IsOperator) return false;
    if (call->IsLifted) return false;
    if (call->Arguments.size() != 1) return false;
    auto shortName = ShortMethodName(call->MethodName);
    if (shortName != "op_True" && shortName != "op_False") return false;
    conditionMethodName = std::string(shortName);
    // call.Arguments[0].MatchLdLoc(out v).
    auto* arg = call->Arguments[0].get();
    if (!arg || arg->Op != OpCode::LdLoc) return false;
    v = static_cast<LdLoc*>(arg)->Variable;
    return true;
}

bool UserDefinedLogicTransform::MatchBitwiseCall(const Call* call, const ILVariable* v,
                                                  const std::string& conditionMethodName) {
    if (!call) return false;
    // The C# `call.Method.IsOperator && call.Arguments.Count == 2 && !call.IsLifted`.
    if (!call->IsOperator) return false;
    if (call->IsLifted) return false;
    if (call->Arguments.size() != 2) return false;
    if (!IsLdLocOf(call->Arguments[0].get(), v)) return false;
    auto shortName = ShortMethodName(call->MethodName);
    // conditionMethodName == "op_False" && name == "op_BitwiseAnd"
    // || conditionMethodName == "op_True" && name == "op_BitwiseOr".
    if (conditionMethodName == "op_False" && shortName == "op_BitwiseAnd") return true;
    if (conditionMethodName == "op_True" && shortName == "op_BitwiseOr") return true;
    return false;
}

bool UserDefinedLogicTransform::LegacyPattern(Block& block, int pos,
                                              StatementTransformContext& context)
{
    // The stloc at `pos` must be a store to a StackSlot variable (the C# checks
    // `s.Kind == VariableKind.StackSlot`).
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size()) return false;
    auto* stloc = dynamic_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (!stloc || !stloc->Value) return false;
    if (!stloc->Variable || stloc->Variable->Kind != VariableKind::StackSlot) return false;
    ILVariable* s = stloc->Variable.get();
    auto lhsInst = stloc->Value.get();  // non-owning view; captured before detaching

    // The if is the block's FinalInstruction (this port's block model: the C#
    // reads `block.Instructions[pos + 1]` as the if; here the if is the final).
    auto* iff = dynamic_cast<IfInstruction*>(block.FinalInstruction.get());
    if (!iff) return false;

    // The condition must be logic.not(call op_True/op_False(ldloc s)) (the
    // reader's brfalse form). MatchLogicNot unwraps the logic.not.
    ILInstruction* condition = nullptr;
    if (!MatchLogicNot(iff->Condition.get(), condition)) return false;
    ILVariablePtr s2;
    std::string conditionMethodName;
    if (!MatchCondition(condition, s2, conditionMethodName)) return false;
    if (s2.get() != s) return false;

    // The legacy pattern has no else (the else is the fall-through, which
    // ConditionDetection dropped -> null FalseInst in this port). Bail if there
    // is an else arm (not the legacy pattern -- the C# checks
    // `ifInst.FalseInst.OpCode != OpCode.Nop`; a null FalseInst is this port's
    // Nop equivalent).
    if (iff->FalseInst) return false;

    // The true arm is a Block (or a bare StLoc ConditionDetection unwrapped)
    // carrying `stloc s(call op_BitwiseAnd/op_BitwiseOr(ldloc s, rhsInst))`.
    auto* trueSt = UnwrapLegacyTrueInst(iff->TrueInst.get());
    if (!trueSt) return false;
    if (!trueSt->Value) return false;
    if (trueSt->Variable.get() != s) return false;
    auto* call = dynamic_cast<Call*>(trueSt->Value.get());
    if (!MatchBitwiseCall(call, s, conditionMethodName)) return false;
    // s.IsUsedWithin(call.Arguments[1]) -- the rhs must not reference s, or
    // short-circuiting would change semantics (the user logic operator only
    // evaluates the rhs when the lhs is "true"/"false", so an rhs that reads s
    // would observe a different value).
    if (call->Arguments.size() < 2 || !call->Arguments[1]) return false;
    if (IsUsedWithin(call->Arguments[1].get(), s)) return false;

    context.Base.StepOnce("UserDefinedLogicTransform: legacy pattern");
    // Build the UserDefinedLogicOperator from the call's method metadata + the
    // lhsInst (the stloc's value) + the rhsInst (the call's 2nd argument). Detach
    // both before the old nodes are destroyed (no GC; raw pointers would dangle).
    // The lhsInst is the stloc's Value (TakeChild(0)); the rhsInst is the call's
    // Arguments[1] (TakeChild from the call, which is itself inside the true-arm
    // StLoc destroyed when SetFinal replaces the if-final).
    auto lhsOwned = stloc->TakeChild(0);  // detach lhsInst from the stloc
    // The call is trueSt->Value; detach its 2nd argument before the if (and the
    // true-arm Block/StLoc) is destroyed by SetFinal.
    auto rhsOwned = call->TakeChild(1);
    auto userLogicOp = std::make_unique<UserDefinedLogicOperator>(
        call->MethodName, call->DeclaringType, std::move(lhsOwned), std::move(rhsOwned));
    // stloc.Value = userLogicOp (the C# `((StLoc)block.Instructions[pos]).Value =
    // userLogicOp`).
    stloc->SetChild(0, std::move(userLogicOp));
    // Remove the if (the C# `block.Instructions.RemoveAt(pos + 1)`): replace the
    // if-final with a Branch to the next block (the positional fall-through the
    // if's null FalseInst represented). Destroys the if and the now-empty true-
    // arm Block/StLoc/call (their needed children were detached above).
    Block* nextBlock = NextBlockInContainer(&block);
    // The legacy pattern's true-arm Block branches to the join (the next block);
    // the if's fall-through is also the next block. A fold requires a next block
    // (a degenerate end-of-function legacy pattern does not arise). Bail without
    // mutating if there is no next block -- but we already mutated (SetChild),
    // so this case is unreachable for a well-formed legacy pattern (the true-arm
    // Block's Branch final targets a block). Guard for safety.
    if (!nextBlock) {
        // Restore: put the lhsInst back into the stloc. The rhsOwned was detached
        // from the call (still inside the if's true arm, alive); the call is intact,
        // so re-attach the rhs to the call and the lhs to the stloc.
        auto* call2 = dynamic_cast<Call*>(trueSt->Value.get());
        if (call2) call2->SetChild(1, std::move(rhsOwned));
        stloc->SetChild(0, std::move(lhsOwned));
        return false;
    }
    block.SetFinal(std::make_unique<Branch>(nextBlock));
    // The C# calls context.RequestRerun() so a later ILInlining pass folds the
    // now-single-use stloc s into its load (the user logic operator's Left). The
    // per-statement driver re-runs all children at the current position.
    context.RequestRerunCurrentPosition();
    return true;
}

void UserDefinedLogicTransform::Run(Block& block, int pos, StatementTransformContext& context) {
    // The C# order: LegacyPattern, then RoslynOptimized. The RoslynOptimized
    // pattern (the "in combination with return statement" shape whose if has
    // leave early-return arms and a trailing leave) is deferred -- its block-
    // model shape needs a pre-pipeline corpus probe of the real post-
    // ConditionDetection form, and the .NET Framework 4 legacy-csc mscorlib
    // corpus carries no op_True/op_False operator definitions (so neither
    // pattern fires on it -- both are faithfulness-only on this corpus).
    LegacyPattern(block, pos, context);
}

} // namespace ILSpy::Decompiler::IL
