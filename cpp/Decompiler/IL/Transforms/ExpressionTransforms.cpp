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
#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

bool IsLdcI4(const ILInstruction* inst, int value) {
    if (!inst || inst->Op != OpCode::LdcI4) return false;
    return static_cast<const LdcI4*>(inst)->Value == value;
}

// Match an integer constant (LdcI4 or LdcI8), reporting its value as a long.
// Port of ILInstruction.MatchLdcI(out long): the C# additionally unwraps
// SignExtend / ZeroExtend-from-I4 convs around the constant; this port's reader
// does not wrap ldc constants in conv, so the plain LdcI4/LdcI8 cases cover the
// NullableLifting Run(Comp) inputs (the constant the `a == 42` optimization
// emits is a bare ldc.i4 for Nullable<int> / ldc.i8 for Nullable<long>). Mirrors
// the SwitchAnalysis.cpp MatchLdcI (file-local there for the same reason).
bool MatchLdcI(const ILInstruction* inst, long long& val) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdcI4) {
        val = static_cast<const LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == OpCode::LdcI8) {
        val = static_cast<const LdcI8*>(inst)->Value;
        return true;
    }
    return false;
}

// Port of ILInstruction.UnwrapConv(kind): if inst is a Conv of the requested
// ConversionKind, descend into its argument (recursively, unwrapping a chain of
// the same kind); else return inst unchanged. Mirrors the C#
// `inst.UnwrapConv(kind)` (non-owning). Requires the Conv node's ConversionKind
// (D85) -- the prior D81 approximation accepted any conv around the 0.
const ILInstruction* UnwrapConv(const ILInstruction* inst, ConversionKind kind) {
    while (inst && inst->Op == OpCode::Conv) {
        auto* conv = static_cast<const Conv*>(inst);
        if (conv->Kind != kind) break;
        inst = conv->Argument.get();
    }
    return inst;
}

// The C# `inst.Right.UnwrapConv(SignExtend).UnwrapConv(ZeroExtend).MatchLdcI4(0)`
// unwraps sign/zero-extending convs around the 0 before testing it. With the
// Conv node now carrying its ConversionKind (D85) this is faithful: a
// `conv.i(ldc.i4 0)` (SignExtend, I4->I) unwraps to the bare `ldc.i4 0`, while a
// `conv.i4(ldc.i4 0)` (Nop) does not (it is not a sign/zero-extension).
bool IsLdcI4ZeroMaybeConv(const ILInstruction* inst) {
    inst = UnwrapConv(UnwrapConv(inst, ConversionKind::SignExtend),
                      ConversionKind::ZeroExtend);
    return IsLdcI4(inst, 0);
}

bool IsEqualityOrInequality(ComparisonKind k) {
    return k == ComparisonKind::Equality || k == ComparisonKind::Inequality;
}

// Port of ILInstruction.MatchLogicNot(out arg): logic.not(X) is this port's
// `comp(Equality, X, ldc.i4(0))` shape (the reader's brfalse, per the
// SwitchAnalysis / ConditionDetection / MatchInstruction convention). Returns
// true and sets `arg` to the negated expression (comp->Left). Sign-independent,
// so a `comp.eq.un X 0` (Unsigned) is not a brfalse shape. Mirrors the file-local
// MatchLogicNot in NullableLiftingTransform.cpp / MatchInstruction.hpp.
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

// Port of ExpressionTransforms.MatchExpectedShiftSize: the mask a shift's right
// operand is `&`-ed with must be the bit width minus one for the mask to be
// redundant in C# (the shift already masks the count). `a << (b & 31)` is `a << b`
// for an int (I4) shift; `a << (b & 63)` for a long (I8). The native-int (I) case
// is `sizeof(IntPtr) * 8 - 1` -- deferred (this port's SizeOf carries only a name
// string, no IType with GetStackType, so the `size.MatchSizeOf(out var
// sizeofType) && sizeofType.GetStackType() == StackType.I` check cannot be
// faithful); it returns false so the native-int mask is left in place.
bool MatchExpectedShiftSize(const ILInstruction* rhs, StackType resultType) {
    switch (resultType) {
        case StackType::I4:
            return IsLdcI4(rhs, 31);
        case StackType::I8:
            return IsLdcI4(rhs, 63);
        // case StackType::I: deferred -- needs SizeOf with an IType + GetStackType.
        default:
            return false;
    }
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

// Port of ILInstruction.MatchLdLoc(out ILVariable v): a bare LdLoc reports its
// variable. This port has no Block-unwrapping here (the C# MatchLdLoc is a direct
// match); the `&`/`|` on bool? arms are bare value arms (a sub-expression
// value-if, the only shape the fold fires on -- a block-final if has Branch
// arms), so a bare match is faithful. Mirrors the PatternMatchingTransform.cpp
// file-local MatchLdLoc (kept file-local here for the same reason -- no shared
// PatternMatching header in this port).
bool MatchLdLoc(ILInstruction* inst, ILVariable*& v) {
    v = nullptr;
    if (!inst || inst->Op != OpCode::LdLoc) return false;
    v = static_cast<LdLoc*>(inst)->Variable.get();
    return v != nullptr;
}

// Port of ILInstruction.MatchLogicOr(out lhs, out rhs): an IfInstruction whose
// TrueInst is `ldc.i4 1` ("if (a) ldc.i4 1 else b"); lhs = Condition, rhs =
// FalseInst. The C# MatchLdcI4 is a bare match (the arms of a sub-expression
// value-if are bare; ConditionDetection only reshapes block-final ifs). Used by
// MatchThreeValuedLogicConditionPattern on the (possibly logic.not-peeled)
// condition of the outer if.
bool MatchLogicOr(ILInstruction* inst, ILInstruction*& lhs, ILInstruction*& rhs) {
    lhs = nullptr;
    rhs = nullptr;
    if (!inst || inst->Op != OpCode::IfInstruction) return false;
    auto* iff = static_cast<IfInstruction*>(inst);
    if (!IsLdcI4(iff->TrueInst.get(), 1)) return false;
    lhs = iff->Condition.get();
    rhs = iff->FalseInst.get();
    return true;
}

// Port of ILInstruction.MatchLogicAnd(out lhs, out rhs): an IfInstruction whose
// FalseInst is `ldc.i4 0` ("if (a) b else ldc.i4 0"); lhs = Condition, rhs =
// TrueInst. See MatchLogicOr for the bare-arm rationale.
bool MatchLogicAnd(ILInstruction* inst, ILInstruction*& lhs, ILInstruction*& rhs) {
    lhs = nullptr;
    rhs = nullptr;
    if (!inst || inst->Op != OpCode::IfInstruction) return false;
    auto* iff = static_cast<IfInstruction*>(inst);
    if (!IsLdcI4(iff->FalseInst.get(), 0)) return false;
    lhs = iff->Condition.get();
    rhs = iff->TrueInst.get();
    return true;
}

// Port of NullableLiftingTransform.MatchThreeValuedLogicConditionPattern:
// matches `nullable1.GetValueOrDefault() || (!nullable2.GetValueOrDefault() &&
// !nullable1.HasValue)` -- a logic.or whose lhs is a 1-arg GetValueOrDefault on a
// Nullable<bool> and whose rhs is a logic.and of `!nullable2.GetValueOrDefault()`
// and `!nullable1.HasValue`. Reports nullable1/nullable2 (the ldloca-v
// overload of MatchGetValueOrDefault). Each Nullable must be Nullable<bool>.
// Returns true and sets nullable1/nullable2 on a full match.
bool MatchThreeValuedLogicConditionPattern(ILInstruction* condition,
                                            ILVariablePtr& nullable1,
                                            ILVariablePtr& nullable2) {
    nullable1.reset();
    nullable2.reset();
    ILInstruction* lhs = nullptr;
    ILInstruction* rhs = nullptr;
    if (!MatchLogicOr(condition, lhs, rhs)) return false;
    if (!NullableLiftingTransform::MatchGetValueOrDefault(lhs, nullable1)) return false;
    if (!nullable1 || !nullable1->Type) return false;
    if (!NullableLiftingTransform::IsKnownType(
            NullableLiftingTransform::GetUnderlyingTypeOfNullable(nullable1->Type.get()),
            TypeSystem::KnownTypeCode::Boolean))
        return false;
    ILInstruction* andLhs = nullptr;
    ILInstruction* andRhs = nullptr;
    if (!MatchLogicAnd(rhs, andLhs, andRhs)) return false;
    ILInstruction* arg = nullptr;
    if (!MatchLogicNot(andLhs, arg)) return false;
    if (!NullableLiftingTransform::MatchGetValueOrDefault(arg, nullable2)) return false;
    if (!nullable2 || !nullable2->Type) return false;
    if (!NullableLiftingTransform::IsKnownType(
            NullableLiftingTransform::GetUnderlyingTypeOfNullable(nullable2->Type.get()),
            TypeSystem::KnownTypeCode::Boolean))
        return false;
    ILInstruction* arg2 = nullptr;
    if (!MatchLogicNot(andRhs, arg2)) return false;
    return NullableLiftingTransform::MatchHasValueCall(arg2, nullable1.get());
}

// Port of context.TypeSystem.FindType(StackType, Sign) (ReflectionHelper.cs):
// the IType for a stack type + sign. The C# maps Unknown -> SpecialType.UnknownType,
// Ref -> ByRef(Unknown), default -> FindType(ToKnownTypeCode(stackType, sign)).
// This minimal port has no SpecialType/ByRef(Unknown); it builds a KnownType for
// the known primitive codes and returns null for None/Unknown/Ref (the
// conv.nop.lifted case treats a null utype conservatively -- no conv inserted,
// matching the C# `utype.ToPrimitiveType() != PrimitiveType.None` guard which is
// false for UnknownType -> PrimitiveType.Unknown != None is true but a null utype
// here short-circuits to no conv, the safe approximation for a GVO whose ReturnType
// was not resolved). Used by the LiftNormal conv.nop.lifted case to build the
// conv's target type from the GetValueOrDefault call's ResultType (a StackType).
TypeSystem::ITypePtr FindTypeFromStackType(StackType stackType,
                                            TypeSystem::Sign sign = TypeSystem::Sign::None) {
    auto code = TypeSystem::ToKnownTypeCode(stackType, sign);
    if (code == TypeSystem::KnownTypeCode::None) return nullptr;
    return std::make_shared<TypeSystem::KnownType>(code);
}

// Port of NullableLiftingTransform.AnalyzeCondition: walks a condition that is a
// HasValue call (on a Nullable<T>) or a BitAnd(I4) of such calls, collecting the
// nullable variables into `nullableVarsS`. Returns true if the whole tree is
// HasValue calls (and BitAnds of them); false otherwise. This is the gate the
// LiftNormal path consults: `(v1 != null && ... && vn != null) ? trueInst :
// falseInst` (the condition is `v1.HasValue && ... && vn.HasValue`, the BitAnd
// lowering of `&&`). Mirrors the C# `AnalyzeCondition(condition)` which recurses
// into a BitAnd's Left/Right and collects each HasValue call's variable.
bool AnalyzeCondition(ILInstruction* condition,
                      std::vector<ILVariablePtr>& nullableVarS) {
    if (!condition) return false;
    ILVariablePtr v;
    if (NullableLiftingTransform::MatchHasValueCall(condition, v)) {
        if (!v) return false;
        nullableVarS.push_back(v);
        return true;
    }
    if (condition->Op == OpCode::BinaryNumericInstruction) {
        auto* bni = static_cast<BinaryNumericInstruction*>(condition);
        if (bni->Operator != BinaryNumericOperator::BitAnd ||
            bni->ResultType() != StackType::I4)
            return false;
        return AnalyzeCondition(bni->Left.get(), nullableVarS) &&
               AnalyzeCondition(bni->Right.get(), nullableVarS);
    }
    return false;
}

// Port of NullableLiftingTransform.AnalyzeNegatedCondition: logic.not(X) whose
// inner X is an AnalyzeCondition tree (a HasValue call or a BitAnd of them).
// `condition.MatchLogicNot(out var arg) && AnalyzeCondition(arg)` in the C#.
// Used by the relational MatchCompOrDecimal cases (the `!(v1 != null && ...)
// ? true : false` shapes that produce a logic.not-wrapped lifted comparison).
bool AnalyzeNegatedCondition(ILInstruction* condition,
                             std::vector<ILVariablePtr>& nullableVarS) {
    ILInstruction* arg = nullptr;
    if (!MatchLogicNot(condition, arg)) return false;
    return AnalyzeCondition(arg, nullableVarS);
}

// Port of Comp.LogicNot(arg): wrap `condition` in a logic.not
// (`comp(Equality, condition, ldc.i4 0)`, the reader's brfalse shape). The C#
// `Comp.LogicNot` is a static `new Comp(ComparisonKind.Equality, Sign.None, arg,
// new LdcI4(0))` -- it does NOT fold (unlike ConditionDetection::NegateCondition);
// the relational MatchCompOrDecimal cases wrap the already-lifted comparison
// result, so the wrap (not a kind-negate) is the faithful behaviour. The
// returned comp is detached (no parent); the caller wires it into its slot.
std::unique_ptr<ILInstruction> MakeLogicNot(std::unique_ptr<ILInstruction> condition) {
    return std::make_unique<Comp>(
        std::move(condition), std::make_unique<LdcI4>(0),
        ComparisonKind::Equality, false);
}

// Detach `inst` from its parent (TakeChild at its ChildIndex), returning owning
// ownership. Used by the `&`/`|` on bool? path to lift the condition/arms out of
// the if before the if is destroyed (no GC; the non-owning views would dangle).
// Works uniformly whether `inst` is a direct child of the if (e.g. an arm) or a
// sub-expression of the if's Condition (the logic.not-peeled inner expression):
// Parent + ChildIndex identify the slot in both cases.
std::unique_ptr<ILInstruction> DetachFromParent(ILInstruction* inst) {
    if (!inst || !inst->Parent) return nullptr;
    return inst->Parent->TakeChild(inst->ChildIndex);
}

// Detach an arm (trueInst/falseInst) that may be an in-tree child of the parent
// (an if's arm or a BinaryNumericInstruction operand -- has a Parent) or a fresh
// node owned by `sink` with no Parent (the Run(BinaryNumericInstruction) falseInst
// is a fresh LdcI4(0)). For the in-tree case this reduces to DetachFromParent
// (TakeChild at the view's ChildIndex); for the fresh-node case it moves the
// ownership out of `sink`. The Run(IfInstruction) caller passes empty sinks
// (both arms in-tree), so ConsumeArm is behaviourally identical to DetachFromParent
// there; the Run(BinaryNumericInstruction) caller passes a fresh LdcI4(0) in
// falseSink. The logic.not unwrap loop swaps both the views and the sinks, so the
// view<->sink correspondence is preserved across the swap.
std::unique_ptr<ILInstruction> ConsumeArm(ILInstruction* view,
                                          std::unique_ptr<ILInstruction>& sink) {
    if (view && view->Parent) return view->Parent->TakeChild(view->ChildIndex);
    return std::move(sink);
}

// Conservatively recognize a Boolean-typed value for the
// Run(BinaryNumericInstruction) BitAnd gate (the C#
// `inst.InferType(context.TypeSystem).IsKnownType(KnownTypeCode.Boolean)`). The
// `&&`-as-`&` Roslyn optimization applies only to bool operands, so a conservative
// recognizer avoids invoking the nullable lift on `int & int` (the common
// bitwise-and). Recognizes the bool-producing instruction kinds: a Comp (every
// comparison yields bool), a Nullable<T> get_HasValue call, a Nullable<bool>
// GetValueOrDefault call, a BitAnd/BitOr of bool operands (recursive), a BitNot
// bool operands (recursive), a BitNot of a bool operand, and a LdLoc of a
// Boolean-typed variable. Returns false for anything else (conservative), so a
// non-bool BitAnd does not enter the lift (the lift would return null anyway,
// but skipping it avoids wasted work and detaching a fresh falseInst).
bool IsBooleanValue(ILInstruction* inst) {
    if (!inst) return false;
    if (inst->Op == OpCode::Comp) return true;
    ILVariablePtr v;
    if (NullableLiftingTransform::MatchHasValueCall(inst, v)) return true;
    // A 1-arg GetValueOrDefault on Nullable<T> returns the underlying T; for the
    // BitAnd to be Boolean-typed, T must be Boolean (Nullable<bool>). This lets
    // the bool? equality BNI case `BitAnd(GVO(v), HV(v))` (`v == true`) pass the
    // gate -- the C# InferType recognises the GVO call's result as bool.
    if (NullableLiftingTransform::MatchGetValueOrDefault(inst, v) && v && v->Type)
        return NullableLiftingTransform::IsKnownType(
            NullableLiftingTransform::GetUnderlyingTypeOfNullable(v->Type.get()),
            TypeSystem::KnownTypeCode::Boolean);
    if (inst->Op == OpCode::BinaryNumericInstruction) {
        auto* bni = static_cast<BinaryNumericInstruction*>(inst);
        if (bni->Operator == BinaryNumericOperator::BitAnd ||
            bni->Operator == BinaryNumericOperator::BitOr)
            return IsBooleanValue(bni->Left.get()) && IsBooleanValue(bni->Right.get());
        return false;
    }
    if (inst->Op == OpCode::BitNot)
        return IsBooleanValue(static_cast<BitNot*>(inst)->Argument.get());
    if (inst->Op == OpCode::LdLoc) {
        auto* ld = static_cast<LdLoc*>(inst);
        return ld->Variable && ld->Variable->Type &&
               NullableLiftingTransform::IsKnownType(ld->Variable->Type.get(),
                                                     TypeSystem::KnownTypeCode::Boolean);
    }
    return false;
}

// Replace the if with `lifted` (a value node), applying the block-model
// adaptation: a clean ReplaceWith when the if is a sub-expression value, or the
// lifted value becomes a non-terminal statement + a Branch to the next block
// replaces the if-final when the if is a block's FinalInstruction (a value node
// cannot be a block final). The next block is resolved before any mutation (the
// precondition-before-mutation discipline). Returns true if the if was replaced
// (the if is destroyed); false if a block-final if had no fall-through target
// (the if is left intact and the caller must not mutate further). Used by the
// RunIfNullableLift thin caller (after LiftNullableCore produces the lifted
// value) and the `&`/`|` on bool? path within LiftNullableCore.
bool ReplaceIfWithLiftedValue(IfInstruction* iff, std::unique_ptr<ILInstruction> lifted) {
    auto* block = dynamic_cast<Block*>(iff->Parent);
    bool isBlockFinal = block && block->FinalInstruction.get() == iff;
    Block* nextBlock = nullptr;
    if (isBlockFinal) {
        nextBlock = NextBlockInContainer(block);
        if (!nextBlock) return false;  // no fall-through target; leave the if
    }
    if (isBlockFinal) {
        // The lifted value (no side effect) becomes a non-terminal statement
        // (its value is discarded, matching the if-as-statement whose value was
        // discarded) and a Branch to the next block replaces the if-final.
        block->Add(std::move(lifted));
        block->SetFinal(std::make_unique<Branch>(nextBlock));  // destroys the if
    } else {
        // The if is a sub-expression value (e.g. `stloc V(if (...) ..)`);
        // ReplaceWith is a clean in-place swap (the C# `ifInst.ReplaceWith(lifted)`).
        iff->ReplaceWith(std::move(lifted));  // destroys the if
    }
    return true;
}

} // namespace

void ExpressionTransforms::Run(Block& block, int pos, StatementTransformContext& context) {
    // Snapshot the settings for the duration of this Run (the C# stores the
    // StatementTransformContext as a member). IsPatternMatch (in
    // FoldMatchTrueFalse) consults PatternCombinators/RelationalPatterns.
    settings_ = &context.Base.Settings;

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
        // NullableLiftingTransform.Run(comp) -- the C# VisitComp runs it after the
        // head rewrites and before base.VisitComp. The value==0 case is handled by
        // the head rewrites (logic.not / comp(!=0)=>x); RunCompNullableLift only
        // fires for a non-zero constant, so the two are disjoint.
        RunCompNullableLift(comp);
        // base.VisitComp: recurse into the operands (children visited before the
        // tail rewrites, matching the C# order). After RunCompNullableLift the
        // operands may be the freshly-built ldobj + the constant.
        Visit(comp->Left.get());
        Visit(comp->Right.get());
        // The C# `if (inst.IsLifted) return;` after base.VisitComp skips the tail
        // rewrites (FixComparisonKindLdNull / unsigned normalization / ldlen) for
        // a lifted comp. A RunCompNullableLift-produced comp is an eq/ne lifted comp,
        // so the unsigned > 0 / <= 0 tail rewrite (GT/LE only) would not fire on it
        // anyway, but the guard is faithful and forward-compatible.
        if (comp->IsLifted()) return;
        if (VisitCompTailRewrites(comp)) return;  // rewritten + re-visited
        return;
    }
    if (inst->Op == OpCode::IfInstruction) {
        VisitIfInstruction(static_cast<IfInstruction*>(inst));
        return;
    }
    if (inst->Op == OpCode::Box) {
        VisitBox(static_cast<Box*>(inst));
        return;
    }
    if (inst->Op == OpCode::Conv) {
        VisitConv(static_cast<Conv*>(inst));
        return;
    }
    if (inst->Op == OpCode::LdElema) {
        VisitLdElema(static_cast<LdElema*>(inst));
        return;
    }
    if (inst->Op == OpCode::NewArr) {
        VisitNewArr(static_cast<NewArr*>(inst));
        return;
    }
    if (inst->Op == OpCode::BinaryNumericInstruction) {
        VisitBinaryNumericInstruction(static_cast<BinaryNumericInstruction*>(inst));
        return;
    }
    if (inst->Op == OpCode::Call) {
        VisitCall(static_cast<Call*>(inst));
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

void ExpressionTransforms::RunCompNullableLift(Comp* comp) {
    // Port of NullableLiftingTransform.Run(Comp comp): the VS2022.10 / Roslyn
    // 4.10.0 optimization that turns `a == 42` into `a.GetValueOrDefault() == 42`
    // without any HasValue check is recognised and lifted back to
    // `comp.lifted[C#](a == 42)`. The comp must be a non-lifted equality/
    // inequality whose one side is `call GetValueOrDefault(arg)` on
    // System.Nullable<T> and whose other side is a non-zero integer constant.
    // The GetValueOrDefault call is replaced by `ldobj Nullable<T>(arg)` and the
    // comp is marked C#-lifted. Gated on LiftNullables (the C#
    // `context.Settings.LiftNullables`).
    if (!comp) return;
    if (!settings_ || !settings_->LiftNullables) return;
    if (comp->IsLifted()) return;
    if (!IsEqualityOrInequality(comp->Kind)) return;

    long long value = 0;
    // Left is GetValueOrDefault, Right is a non-zero constant.
    ILInstruction* arg = nullptr;
    if (NullableLiftingTransform::MatchGetValueOrDefault(comp->Left.get(), arg) &&
        MatchLdcI(comp->Right.get(), value) && value != 0) {
        auto* call = static_cast<Call*>(comp->Left.get());
        // Capture the declaring type (a shared_ptr copy) before the call is
        // destroyed -- the precondition-before-mutation discipline: TakeChild
        // detaches the argument, then SetChild destroys the old Left (the call,
        // now with a null Arguments[0]). The LdObj loads Nullable<T> from the
        // argument's address (the C# `new LdObj(arg, call.Method.DeclaringType)`).
        TypeSystem::ITypePtr declaringType = call->DeclaringType;
        auto argOwned = call->TakeChild(0);
        comp->LiftingKind = ComparisonLiftingKind::CSharp;
        comp->SetChild(0, std::make_unique<LdObj>(std::move(argOwned), declaringType));
        return;
    }
    // Right is GetValueOrDefault, Left is a non-zero constant.
    if (NullableLiftingTransform::MatchGetValueOrDefault(comp->Right.get(), arg) &&
        MatchLdcI(comp->Left.get(), value) && value != 0) {
        auto* call = static_cast<Call*>(comp->Right.get());
        TypeSystem::ITypePtr declaringType = call->DeclaringType;
        auto argOwned = call->TakeChild(0);
        comp->LiftingKind = ComparisonLiftingKind::CSharp;
        comp->SetChild(1, std::make_unique<LdObj>(std::move(argOwned), declaringType));
        return;
    }
}

void ExpressionTransforms::VisitConv(Conv* inst) {
    if (!inst) return;
    // Visit the argument first (the C# `inst.Argument.AcceptVisitor(this)`) so
    // the Comp/StLoc/Box rewrites cascade into the converted expression before
    // the conv.r.un combining is considered.
    if (inst->Argument) Visit(inst->Argument.get());

    // conv.r4(conv.r.un(x)) / conv.r8(conv.r.un(x)) -> conv.r4.un(x) /
    // conv.r8.un(x). IL conv.r.un does not indicate whether to convert the target
    // to R4 or R8, so the C# compiler usually follows it with an explicit
    // conv.r4 or conv.r8; the two conversions are combined into one that carries
    // the inner conv's input type/sign but the outer's target (R4/R8). The C#
    // checks `inst.TargetType.IsFloatType() && inst.Argument is Conv conv &&
    // conv.Kind == ConversionKind.IntToFloat && conv.TargetType ==
    // PrimitiveType.R`; requires the Conv node's ConversionKind (D85).
    if (!IsFloatType(inst->TargetType) || !inst->Argument ||
        inst->Argument->Op != OpCode::Conv) {
        return;
    }
    auto* inner = static_cast<Conv*>(inst->Argument.get());
    if (inner->Kind != ConversionKind::IntToFloat ||
        inner->TargetType != PrimitiveType::R) {
        return;
    }
    // Capture the inner conv's InputSign before detaching its argument (the
    // inner conv is destroyed when the outer is replaced by the new Conv). The
    // C# `new Conv(conv.Argument, conv.InputType, conv.InputSign, inst.TargetType,
    // inst.CheckForOverflow, inst.IsLifted | conv.IsLifted)` -- the port's Conv
    // constructor derives InputType from the argument's ResultType, which equals
    // the inner's InputType, and IsLifted is dropped (no nullable-lifting model).
    Sign innerSign = inner->InputSign;
    auto innerArg = inner->TakeChild(0);  // detach the inner conv's argument
    auto newConv = std::make_unique<Conv>(std::move(innerArg),
                                           inst->TargetType,
                                           inst->CheckForOverflow,
                                           innerSign);
    inst->ReplaceWith(std::move(newConv));  // destroys inst + the inner conv shell
    // The C# does not RequestRerun here; the argument was already visited above.
}

void ExpressionTransforms::VisitBox(Box* box) {
    if (!box) return;
    // Visit the argument first (the C# `inst.Argument.AcceptVisitor(this)`) so
    // the Comp/StLoc rewrites cascade into the boxed expression before the box
    // is considered for removal.
    if (box->Argument) Visit(box->Argument.get());

    // box ref-type(arg) => arg: for a reference type, box is a no-op (the value
    // is already on the heap). The C# checks `inst.Type.IsReferenceType == true
    // && inst.Argument.ResultType == inst.ResultType`. The ResultType guard (the
    // arg is stack-type O, the box is O) is the real protection against a
    // value-type mis-fire: a value type's argument is I4/I8/F4/.. (never O), so
    // even a type whose Kind defaulted to Class (a non-known TypeRef this port's
    // plain MakeTypeRef leaves as Class) does not fold when the argument is not
    // object-typed. IsReferenceType (TypeUtils) returns nullopt for the
    // uncertain kinds (TypeParameter/ByRef/Pointer/Unknown/...), so the fold is
    // conservative there -- a `box T(arg)` over a generic T does not fold (the
    // C# folds it only when T has a class constraint this minimal type system
    // does not track).
    if (!box->Type) return;
    auto rt = TypeSystem::IsReferenceType(box->Type.get());
    if (!rt || !*rt) return;
    if (!box->Argument || box->Argument->ResultType() != box->ResultType()) return;
    // Detach the argument before the box is destroyed, then replace the box with
    // it in its parent's slot (the C# `inst.ReplaceWith(arg)`).
    auto arg = box->TakeChild(0);
    box->ReplaceWith(std::move(arg));
}

void ExpressionTransforms::VisitLdElema(LdElema* inst) {
    if (!inst) return;
    // base.VisitLdElema: visit the array and the index expressions (the C#
    // recurses into the children), so the Comp/StLoc/Box rewrites cascade into
    // the index computation before the indices are cleaned up.
    if (inst->Array) Visit(inst->Array.get());
    for (auto& idx : inst->Indices) Visit(idx.get());
    CleanUpArrayIndices(inst->Indices);
    // The C# then calls IndexRangeTransform.HandleLdElema(inst, context)
    // (deferred -- needs IndexRangeTransform).
}

void ExpressionTransforms::VisitNewArr(NewArr* inst) {
    if (!inst) return;
    // base.VisitNewArr: visit the index expressions, then clean them up.
    for (auto& idx : inst->Indices) Visit(idx.get());
    CleanUpArrayIndices(inst->Indices);
}

void ExpressionTransforms::CleanUpArrayIndices(
        std::vector<std::unique_ptr<ILInstruction>>& indices) {
    // Port of ExpressionTransforms.CleanUpArrayIndices. A `conv.i` (or
    // `conv.ovf.i`) widening of an array index -- a Conv whose ResultType is I
    // (native int) and whose Kind is SignExtend, ZeroExtend, or a checked
    // Truncate (Kind == Truncate && CheckForOverflow) -- only widens the index to
    // native int and is redundant in C#. An unchecked Truncate (conv.i from I8
    // without overflow check) is a real truncation and is kept. Replacing the
    // conv with its argument mirrors the C# `index.ReplaceWith(conv.Argument)`.
    for (auto& index : indices) {
        if (!index || index->Op != OpCode::Conv) continue;
        auto* conv = static_cast<Conv*>(index.get());
        if (conv->ResultType() != StackType::I) continue;
        bool removable =
            (conv->Kind == ConversionKind::Truncate && conv->CheckForOverflow) ||
            conv->Kind == ConversionKind::SignExtend ||
            conv->Kind == ConversionKind::ZeroExtend;
        if (!removable) continue;
        // Detach the argument (orphaning it) before replacing the conv with it.
        auto arg = conv->TakeChild(0);
        // index.get() is the Conv; ReplaceWith destroys it and puts `arg` in the
        // Indices slot (reparenting it). The slot's ChildIndex is preserved.
        index->ReplaceWith(std::move(arg));
    }
}

void ExpressionTransforms::VisitBinaryNumericInstruction(BinaryNumericInstruction* inst) {
    if (!inst) return;
    // Visit the children first (the C# base.VisitBinaryNumericInstruction) so
    // the Comp/StLoc/Box/Conv rewrites cascade into the operands before the
    // shift-size mask / nullable-lift is considered.
    if (inst->Left) Visit(inst->Left.get());
    if (inst->Right) Visit(inst->Right.get());

    // NullableLiftingTransform.Run(BinaryNumericInstruction): the `&&`-as-`&`
    // Roslyn optimization on bool operands, analysed as-if short-circuit. The C#
    // gates on `InferType == Boolean` for both operands; this port uses the
    // conservative IsBooleanValue recognizer. A successful lift replaces the
    // BitAnd (a value) via ReplaceWith, so re-visiting is not needed (the lifted
    // Comp/NullCoalescing is not a BitAnd). Matches the C# `case BitAnd` arm.
    if (inst->Operator == BinaryNumericOperator::BitAnd &&
        IsBooleanValue(inst->Left.get()) && IsBooleanValue(inst->Right.get())) {
        if (RunBinaryNumericNullableLift(inst)) return;
    }

    // a << (b & 31) => a << b: a shift's right operand masked with the bit-width
    // minus one is redundant in C# (the shift already masks the count). Drop the
    // `& mask` when the mask is the expected width for the shift's result type
    // (31 for I4, 63 for I8). The native-int (I) mask `sizeof(IntPtr) * 8 - 1` is
    // deferred (needs SizeOf with an IType + GetStackType).
    if (inst->Operator != BinaryNumericOperator::ShiftLeft &&
        inst->Operator != BinaryNumericOperator::ShiftRight) {
        return;
    }
    auto* bitAnd = dynamic_cast<BinaryNumericInstruction*>(inst->Right.get());
    if (!bitAnd || bitAnd->Operator != BinaryNumericOperator::BitAnd) return;
    if (!MatchExpectedShiftSize(bitAnd->Right.get(), inst->ResultType())) return;
    // Detach the BitAnd's Left (the real shift amount) before destroying the
    // BitAnd, then wire it as the shift's new Right operand. SetChild(1, ...)
    // destroys the old Right (the BitAnd, now with a null Left) and reparents the
    // detached amount into the slot -- the same TakeChild-then-SetChild pattern
    // VisitBox / VisitConv use.
    auto amount = bitAnd->TakeChild(0);
    inst->SetChild(1, std::move(amount));
}

void ExpressionTransforms::VisitCall(Call* inst) {
    if (!inst) return;
    // call Nullable<T>.GetValueOrDefault(a, b) -> a ?? b: a 2-arg
    // GetValueOrDefault on System.Nullable<T> with a pure fallback folds into a
    // NullCoalescingInstruction (NullableWithValueFallback) whose ValueInst is
    // `ldobj Nullable<T>(a)` and FallbackInst is `b` (UnderlyingResultType = b's
    // ResultType). The C# checks `MatchGetValueOrDefault(inst, out nullableValue,
    // out fallback) && SemanticHelper.IsPure(fallback.Flags)`.
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    if (NullableLiftingTransform::MatchGetValueOrDefault(inst, nullableValue, fallback) &&
        fallback && IsPure(fallback->Flags())) {
        // A Call is always a value (never a block final -- it is not control
        // flow), so ReplaceWith is a clean in-place swap (the C#
        // `inst.ReplaceWith(replacement)`); no block-model adaptation is needed.
        // Capture the declaring type and the fallback's ResultType before
        // detaching the children (the call is destroyed by ReplaceWith -- the
        // precondition-before-mutation discipline).
        TypeSystem::ITypePtr declaringType = inst->DeclaringType;
        StackType underlying = fallback->ResultType();
        auto ldObj = std::make_unique<LdObj>(inst->TakeChild(0), declaringType);
        auto fallbackOwned = inst->TakeChild(1);
        auto replacement = std::make_unique<NullCoalescingInstruction>(
            NullCoalescingKind::NullableWithValueFallback,
            std::move(ldObj),
            std::move(fallbackOwned));
        replacement->UnderlyingResultType = underlying;
        ILInstruction* repPtr = replacement.get();
        inst->ReplaceWith(std::move(replacement));  // destroys inst
        // The C# `replacement.AcceptVisitor(this)`: re-visit the replacement so
        // the Comp/StLoc/Box/Conv rewrites cascade into the ldObj's target and
        // the fallback (the call's two arguments, now the NullCoalescing's
        // children). The NullCoalescingInstruction is not a Call, so the fold
        // does not re-trigger.
        Visit(repPtr);
        return;
    }
    // base.VisitCall: visit the arguments (the C# recurses into the children).
    // The C# then calls TransformArrayInitializers.TransformRuntimeHelpersCreateSpan-
    // Initialization, InlineArrayTransform.RunOnExpression, and
    // TransformAssignment.HandleCompoundAssign -- all deferred (need
    // TransformArrayInitializers / InlineArrayTransform / TransformAssignment).
    for (auto& arg : inst->Arguments) Visit(arg.get());
}

std::unique_ptr<ILInstruction> ExpressionTransforms::LiftNullableCore(
    ILInstruction* condition, ILInstruction* trueInst, ILInstruction* falseInst,
    std::unique_ptr<ILInstruction>& trueSink, std::unique_ptr<ILInstruction>& falseSink) {
    // Port of NullableLiftingTransform.Lift -- the shared core of
    // Run(IfInstruction) and Run(BinaryNumericInstruction). A conditional
    // (condition ? trueInst : falseInst) is analysed for a nullable lift;
    // returns the lifted instruction (owned) or nullptr if no fold fired. The
    // condition is read-only (detached only by the `&`/`|` on bool? path); the
    // arms are detached on-demand via ConsumeArm, which handles an in-tree arm
    // (an if's arm or a BNI operand -- has a Parent) and a fresh-node arm (the
    // BNI falseInst = a fresh LdcI4(0), owned by falseSink). The logic.not
    // unwrap loop swaps both the views and the sinks so the view<->sink
    // correspondence is preserved across the swap. Gated on LiftNullables.
    if (!condition) return nullptr;
    if (!settings_ || !settings_->LiftNullables) return nullptr;

    ILInstruction* inner = nullptr;
    while (MatchLogicNot(condition, inner)) {
        condition = inner;
        std::swap(trueInst, falseInst);
        std::swap(trueSink, falseSink);
    }

    // AnalyzeCondition / LiftNormal path (the section of Lift before the bool?
    // equality folds). AnalyzeCondition walks a BitAnd tree of HasValue calls
    // collecting the nullable vars; LiftNormal then lifts the true arm under the
    // `(v1 != null && ... && vn != null)` guard. Three LiftNormal cases are ported:
    // (1) the `v.HasValue ? ldloc v : fallback => v ?? fallback` early-out (a
    // single nullable var whose true arm is `ldloc v` -> NullCoalescingInstruction
    // (Nullable)), (2) the `v.HasValue ? v.GetValueOrDefault() : fallback =>
    // v ?? fallback` conv.nop.lifted case (a single nullable var whose true arm
    // is a GetValueOrDefault call on that var -> `ldloc v` or `conv.nop.lifted
    // (ldloc v)` when the underlying type differs from the GVO's return type,
    // wrapped in a NullCoalescingInstruction(NullableWithValueFallback)), and
    // (3) the general DoLift path (the else-branch after the conv.nop.lifted
    // case): a recursive lift over GetValueOrDefault/Conv/BinaryNumericInstruction/
    // Comp/BitNot producing a lifted Nullable<T> instruction, gated on the
    // `bits.All(0, nullableVars.Count)` relevance check (every nullableVar must
    // contribute), then wrapped per the isNullCoalescingWithNonNullableFallback /
    // MatchNull gates. The MatchIfInstructionPositiveCondition pre-processing
    // (a Roslyn quirk for a redundant inner `if (v.HasValue) X else Y` true arm,
    // not observable in the current pipeline) and the LiftCSharpUserComparison
    // path (needs Call.Method.IsOperator + CSharpOperators + DoLift) are
    // deferred: when AnalyzeCondition succeeds but no ported case fires, return
    // false (matching the C# which returns null -- the whole Lift returns null,
    // the if stays as-is). The true/false arms are detached (DetachFromParent)
    // before the if is destroyed (no GC; the non-owning views would dangle). The
    // NullCoalescing node is a value (ResultType the fallback's), so
    // ReplaceIfWithLiftedValue applies the block-model adaptation (ReplaceWith
    // for a sub-expression value-if, or the node becomes a non-terminal + a
    // Branch final for a block-final if).
    {
        std::vector<ILVariablePtr> nullableVarS;
        if (AnalyzeCondition(condition, nullableVarS)) {
            // LiftNormal: utype + exprToLift are set by MatchNullableCtor (the
            // NullableCtor true-arm case) or by FindType(trueInst.ResultType) +
            // trueInst (the non-NullableCtor case). isNullCoalescingWithNonNullableFallback
            // distinguishes the wrap kind (NullableWithValueFallback for the
            // non-NullableCtor case, Nullable / no-wrap for the NullableCtor case).
            const TypeSystem::IType* utype = nullptr;
            ILInstruction* ctorArg = nullptr;
            bool isNullCoalescingWithNonNullableFallback = false;
            ILInstruction* exprToLift = nullptr;
            TypeSystem::ITypePtr utypeOwned;
            if (!NullableLiftingTransform::MatchNullableCtor(trueInst, utype,
                                                              ctorArg)) {
                isNullCoalescingWithNonNullableFallback = true;
                utypeOwned = FindTypeFromStackType(trueInst->ResultType());
                utype = utypeOwned.get();
                exprToLift = trueInst;
                // The `v.HasValue ? ldloc v : fallback => v ?? fallback` early-out
                // (the simplest LiftNormal case): a single nullable var whose true
                // arm is `ldloc v` -> NullCoalescingInstruction(Nullable) whose
                // UnderlyingResultType is the underlying type's StackType.
                if (nullableVarS.size() == 1) {
                    ILVariable* vLd = nullptr;
                    if (MatchLdLoc(exprToLift, vLd) &&
                        vLd == nullableVarS[0].get()) {
                        auto trueOwned = ConsumeArm(trueInst, trueSink);
                        auto falseOwned = ConsumeArm(falseInst, falseSink);
                        auto lifted = std::make_unique<NullCoalescingInstruction>(
                            NullCoalescingKind::Nullable,
                            std::move(trueOwned), std::move(falseOwned));
                        lifted->UnderlyingResultType = StackTypeOf(
                            NullableLiftingTransform::GetUnderlyingTypeOfNullable(
                                nullableVarS[0]->Type.get()));
                        return std::move(lifted);
                    }
                }
                // LiftCSharpUserComparison(trueInst, falseInst) is deferred (needs
                // Call.Method.IsOperator + CSharpOperators + DoLift); it returns
                // null in this port.
            } else {
                exprToLift = ctorArg;  // the NullableCtor's argument
            }
            // The conv.nop.lifted case: `v.HasValue ? v.GetValueOrDefault() :
            // fallback => v ?? fallback`. A single nullable var whose exprToLift
            // is a GetValueOrDefault call on that var -> a fresh `ldloc v`, with a
            // `conv.nop.lifted(ldloc v)` inserted when the underlying type differs
            // from the GVO's return type (the C# `!inputUType.Equals(utype) &&
            // utype.ToPrimitiveType() != PrimitiveType.None`; a no-op I4->I4 conv
            // for Nullable<bool> where the underlying Boolean != the I4-stacked
            // Int32). The fresh LdLoc + the (optional) Conv are new nodes (not
            // detached from the if); only falseInst is detached before the if is
            // destroyed. The wrap is NullableWithValueFallback (the non-NullableCtor
            // case) or Nullable / no-wrap (the NullableCtor case, gated on
            // MatchNull(falseInst, utype) -- the `default(Nullable<T>)` fallback).
            if (nullableVarS.size() == 1 && exprToLift &&
                NullableLiftingTransform::MatchGetValueOrDefault(
                    exprToLift, nullableVarS[0].get())) {
                const TypeSystem::IType* inputUType =
                    NullableLiftingTransform::GetUnderlyingTypeOfNullable(
                        nullableVarS[0]->Type.get());
                std::unique_ptr<ILInstruction> lifted =
                    std::make_unique<LdLoc>(nullableVarS[0]);
                if (inputUType && utype && !inputUType->Equals(*utype) &&
                    TypeSystem::ToPrimitiveType(utype) != PrimitiveType::None) {
                    lifted = std::make_unique<Conv>(
                        std::move(lifted),
                        StackTypeOf(inputUType),            // inputUType.GetStackType()
                        TypeSystem::GetSign(inputUType),    // inputUType.GetSign()
                        TypeSystem::ToPrimitiveType(utype), // utype.ToPrimitiveType()
                        false,                             // checkForOverflow
                        true);                             // isLifted
                }
                StackType underlyingResultType = exprToLift->ResultType();
                if (isNullCoalescingWithNonNullableFallback) {
                    auto falseOwned = ConsumeArm(falseInst, falseSink);
                    auto nc = std::make_unique<NullCoalescingInstruction>(
                        NullCoalescingKind::NullableWithValueFallback,
                        std::move(lifted), std::move(falseOwned));
                    nc->UnderlyingResultType = underlyingResultType;
                    return std::move(nc);
                } else if (!NullableLiftingTransform::MatchNull(falseInst, utype)) {
                    auto falseOwned = ConsumeArm(falseInst, falseSink);
                    auto nc = std::make_unique<NullCoalescingInstruction>(
                        NullCoalescingKind::Nullable,
                        std::move(lifted), std::move(falseOwned));
                    nc->UnderlyingResultType = underlyingResultType;
                    return std::move(nc);
                } else {
                    // falseInst is `default(Nullable<T>)` (MatchNull) -- no wrap,
                    // the lifted value is the whole result (the C# returns `lifted`
                    // unwrapped; `v ?? null` is just the lifted `v`).
                    return std::move(lifted);
                }
            }
            // The DoLift path (the LiftNormal else-branch after the conv.nop.lifted
            // case): the general recursive lift over GetValueOrDefault/Conv/
            // BinaryNumericInstruction/Comp/BitNot. DoLift builds a lifted
            // Nullable<T> instruction from exprToLift without modifying it; when
            // it succeeds and every nullableVar is relevant (bits.All), the lifted
            // value is wrapped per the isNullCoalescingWithNonNullableFallback /
            // MatchNull gates (matching LiftNormal). The Call user-defined-operator
            // case and LiftCSharpUserComparison are deferred (DoLift returns failure
            // for them); matching the C# which returns null, do not fall through to
            // the bool? equality folds.
            auto doLift = NullableLiftingTransform::DoLift(exprToLift, nullableVarS);
            if (doLift.Lifted) {
                if (doLift.Bits &&
                    doLift.Bits->All(0, static_cast<int>(nullableVarS.size()))) {
                    std::unique_ptr<ILInstruction> lifted = std::move(doLift.Lifted);
                    StackType underlyingResultType = exprToLift->ResultType();
                    if (isNullCoalescingWithNonNullableFallback) {
                        auto falseOwned = ConsumeArm(falseInst, falseSink);
                        auto nc = std::make_unique<NullCoalescingInstruction>(
                            NullCoalescingKind::NullableWithValueFallback,
                            std::move(lifted), std::move(falseOwned));
                        nc->UnderlyingResultType = underlyingResultType;
                        return std::move(nc);
                    } else if (!NullableLiftingTransform::MatchNull(falseInst, utype)) {
                        auto falseOwned = ConsumeArm(falseInst, falseSink);
                        auto nc = std::make_unique<NullCoalescingInstruction>(
                            NullCoalescingKind::Nullable,
                            std::move(lifted), std::move(falseOwned));
                        nc->UnderlyingResultType = underlyingResultType;
                        return std::move(nc);
                    } else {
                        // falseInst is `default(Nullable<T>)` (MatchNull) -- no
                        // wrap, the lifted value is the whole result.
                        return std::move(lifted);
                    }
                }
                // A nullableVar did not contribute to the lift -- don't lift.
                return nullptr;
            }
            // DoLift failed (or the deferred Call-operator/LiftCSharpUserComparison
            // cases); the whole Lift returns null, the if stays as-is.
            return nullptr;
        }
    }

    // The MatchCompOrDecimal / LiftCSharp* path (the section of Lift after
    // AnalyzeCondition/LiftNormal and before the bool? equality folds). A
    // condition that is a non-lifted Comp (the Decimal-operator Call branch is
    // deferred -- needs Call.Method.IsOperator) may be a C#-style lifted
    // comparison. The equality/inequality cases (LiftCSharpEqualityComparison,
    // the hasValueComp two-nullable case + the single-nullable fall-back) and the
    // relational cases (LiftCSharpComparison, the 4 `comp ? (v1 != null && ...) :
    // ldc.i4` shapes, with a logic.not wrap for the negated-condition shapes) are
    // ported (Comp branch only); the user-defined-operator fall-backs
    // (LiftCSharpUserEqualityComparison, the Decimal branch), the
    // IsGenericNewPattern special case (needs MatchDefaultValue +
    // Call.Method.FullName + TypeKind), the NullPropagation path, and the
    // `&`/`|` on bool? path (D96) are the remaining deferred/ported pieces.
    // Gated on LiftNullables (already checked at the top of RunIfNullableLift).
    // The equality swap (the C# `Swap(ref trueInst, ref falseInst)` for
    // Inequality) is local to the equality branch -- the relational branch uses
    // the original (unswapped) arms.
    {
        CompOrDecimal comp;
        if (NullableLiftingTransform::MatchCompOrDecimal(condition, comp)) {
            if (IsEqualityOrInequality(comp.Kind)) {
                ILInstruction* eqTrueInst = trueInst;
                ILInstruction* eqFalseInst = falseInst;
                if (comp.Kind == ComparisonKind::Inequality)
                    std::swap(eqTrueInst, eqFalseInst);
                if (IsLdcI4(eqFalseInst, 0)) {
                    // (a.GVO() == b.GVO()) ? (a.HV == b.HV) : false ==> a == b
                    auto lifted = NullableLiftingTransform::LiftCSharpEqualityComparison(
                        comp, ComparisonKind::Equality, eqTrueInst);
                    if (lifted)
                        return std::move(lifted);
                    return nullptr;  // lift failed (deferred user path) -- exit
                } else if (IsLdcI4(eqFalseInst, 1)) {
                    // (a.GVO() == b.GVO()) ? (a.HV != b.HV) : true ==> a != b
                    auto lifted = NullableLiftingTransform::LiftCSharpEqualityComparison(
                        comp, ComparisonKind::Inequality, eqTrueInst);
                    if (lifted)
                        return std::move(lifted);
                    return nullptr;
                }
                // IsGenericNewPattern (the Activator.CreateInstance<T>() case)
                // is deferred -- needs MatchDefaultValue + Call.Method.FullName +
                // TypeKind; the C# `else if (!comp.IsLifted && IsGenericNewPattern)`
                // returns trueInst, which this port cannot recognise, so it falls
                // through to the bool? equality folds.
            } else if (!comp.IsLifted) {
                // Relational (< <= > >=): returns false unless all HasValue bits
                // are true. The four shapes produce a C#-lifted Comp; the
                // negated-condition shapes (`!(v1 != null && ...) : true`) wrap the
                // lifted comp in a logic.not (Comp.LogicNot). Each shape's guard
                // (IsLdcI4 && AnalyzeCondition/NegatedCondition) must fully
                // succeed before the lift is attempted; if the guard's IsLdcI4
                // holds but Analyze fails, fall through to the next shape.
                if (IsLdcI4(falseInst, 0)) {
                    std::vector<ILVariablePtr> nullableVars;
                    if (AnalyzeCondition(trueInst, nullableVars)) {
                        auto lifted = NullableLiftingTransform::LiftCSharpComparison(
                            comp, comp.Kind, nullableVars);
                        if (lifted)
                            return std::move(lifted);
                        return nullptr;
                    }
                }
                if (IsLdcI4(trueInst, 0)) {
                    std::vector<ILVariablePtr> nullableVars;
                    if (AnalyzeCondition(falseInst, nullableVars)) {
                        auto lifted = NullableLiftingTransform::LiftCSharpComparison(
                            comp, NegateComparison(comp.Kind), nullableVars);
                        if (lifted)
                            return std::move(lifted);
                        return nullptr;
                    }
                }
                if (IsLdcI4(falseInst, 1)) {
                    std::vector<ILVariablePtr> nullableVars;
                    if (AnalyzeNegatedCondition(trueInst, nullableVars)) {
                        auto lifted = NullableLiftingTransform::LiftCSharpComparison(
                            comp, comp.Kind, nullableVars);
                        if (lifted)
                            return MakeLogicNot(std::move(lifted));
                        return nullptr;
                    }
                }
                if (IsLdcI4(trueInst, 1)) {
                    std::vector<ILVariablePtr> nullableVars;
                    if (AnalyzeNegatedCondition(falseInst, nullableVars)) {
                        auto lifted = NullableLiftingTransform::LiftCSharpComparison(
                            comp, NegateComparison(comp.Kind), nullableVars);
                        if (lifted)
                            return MakeLogicNot(std::move(lifted));
                        return nullptr;
                    }
                }
            }
        }
    }

    // Handle equality comparisons with bool?: the condition is
    // `call GetValueOrDefault(ldloca v)` on a Nullable<bool> (GetUnderlyingTypeOf
    // Nullable(v.Type) is Boolean). The four folds produce a C#-lifted Comp whose
    // Left is `ldloc v` and whose Right is the ldc.i4 constant (1 for `v == true`
    // / `v != true`, 0 for `v == false` / `v != false`).
    ILVariablePtr v;
    if (NullableLiftingTransform::MatchGetValueOrDefault(condition, v) &&
        v && v->Type &&
        NullableLiftingTransform::IsKnownType(
            NullableLiftingTransform::GetUnderlyingTypeOfNullable(v->Type.get()),
            TypeSystem::KnownTypeCode::Boolean)) {
        // v.GetValueOrDefault() ? v.HasValue : false ==> v == true
        if (NullableLiftingTransform::MatchHasValueCall(trueInst, v.get()) &&
            IsLdcI4(falseInst, 0)) {
            return std::make_unique<Comp>(
        std::make_unique<LdLoc>(v),
        std::make_unique<LdcI4>(1),
        ComparisonKind::Equality, ComparisonLiftingKind::CSharp, StackType::I4);
        }
        // v.GetValueOrDefault() ? false : v.HasValue ==> v == false
        if (IsLdcI4(trueInst, 0) &&
            NullableLiftingTransform::MatchHasValueCall(falseInst, v.get())) {
            return std::make_unique<Comp>(
        std::make_unique<LdLoc>(v),
        std::make_unique<LdcI4>(0),
        ComparisonKind::Equality, ComparisonLiftingKind::CSharp, StackType::I4);
        }
        // v.GetValueOrDefault() ? !v.HasValue : true ==> v != true
        if (NullableLiftingTransform::MatchNegatedHasValueCall(trueInst, v.get()) &&
            IsLdcI4(falseInst, 1)) {
            return std::make_unique<Comp>(
        std::make_unique<LdLoc>(v),
        std::make_unique<LdcI4>(1),
        ComparisonKind::Inequality, ComparisonLiftingKind::CSharp, StackType::I4);
        }
        // v.GetValueOrDefault() ? true : !v.HasValue ==> v != false
        if (IsLdcI4(trueInst, 1) &&
            NullableLiftingTransform::MatchNegatedHasValueCall(falseInst, v.get())) {
            return std::make_unique<Comp>(
        std::make_unique<LdLoc>(v),
        std::make_unique<LdcI4>(0),
        ComparisonKind::Inequality, ComparisonLiftingKind::CSharp, StackType::I4);
        }
    }

    // Handle `&` and `|` on bool? (the section of Lift after the bool? equality
    // folds). The arms are bare value arms (a sub-expression value-if, the only
    // shape the fold fires on -- a block-final if has Branch arms), so MatchLdLoc
    // is a bare match. Three shapes, each producing a ThreeValuedBoolAnd/Or
    // (the D95 nodes):
    //   condition ? v : (bool?)false       ==> 3vl.bool.and(condition, v)
    //   condition ? (bool?)true : v        ==> 3vl.bool.or(condition, v)
    //   (n1.GVO || (!n2.GVO && !n1.HV)) ? v : v2
    //     v==n1 && v2==n2                  ==> 3vl.bool.or(v, v2)
    //     v==n2 && v2==n1                  ==> 3vl.bool.and(v2, v)
    // The condition/arms are detached (DetachFromParent) before the if is
    // destroyed (no GC; the non-owning views would dangle). The condition may be
    // the logic.not-peeled inner expression (a sub-expression of iff->Condition),
    // so DetachFromParent uses Parent + ChildIndex uniformly. The ThreeValuedBool
    // nodes are values (ResultType O), so ReplaceIfWithLiftedValue applies the
    // block-model adaptation (ReplaceWith for a sub-expression value-if, or the
    // node becomes a non-terminal + a Branch final for a block-final if). For the
    // two-nullable case the condition (the logic.or pattern) is fully captured by
    // the operands and is discarded with the if (not detached).
    {
        ILVariable* vLd = nullptr;
        if (MatchLdLoc(trueInst, vLd)) {
            // condition ? v : (bool?)false ==> condition & v
            const TypeSystem::IType* utype = nullptr;
            ILInstruction* ctorArg = nullptr;
            if (NullableLiftingTransform::MatchNullableCtor(falseInst, utype, ctorArg) &&
                NullableLiftingTransform::IsKnownType(utype,
                                                       TypeSystem::KnownTypeCode::Boolean) &&
                IsLdcI4(ctorArg, 0)) {
                auto condOwned = DetachFromParent(condition);
                auto trueOwned = ConsumeArm(trueInst, trueSink);
                auto lifted = std::make_unique<ThreeValuedBoolAnd>(
                    std::move(condOwned), std::move(trueOwned));
                return std::move(lifted);
            }
            // condition ? v : v2 (the two-nullable three-valued logic pattern)
            ILVariable* v2 = nullptr;
            if (MatchLdLoc(falseInst, v2)) {
                ILVariablePtr nullable1, nullable2;
                if (MatchThreeValuedLogicConditionPattern(condition, nullable1, nullable2)) {
                    if (vLd == nullable1.get() && v2 == nullable2.get()) {
                        // ==> 3vl.bool.or(v, v2)
                        auto trueOwned = ConsumeArm(trueInst, trueSink);
                        auto falseOwned = ConsumeArm(falseInst, falseSink);
                        auto lifted = std::make_unique<ThreeValuedBoolOr>(
                            std::move(trueOwned), std::move(falseOwned));
                        return std::move(lifted);
                    } else if (vLd == nullable2.get() && v2 == nullable1.get()) {
                        // ==> 3vl.bool.and(v2, v)
                        auto falseOwned = ConsumeArm(falseInst, falseSink);
                        auto trueOwned = ConsumeArm(trueInst, trueSink);
                        auto lifted = std::make_unique<ThreeValuedBoolAnd>(
                            std::move(falseOwned), std::move(trueOwned));
                        return std::move(lifted);
                    }
                }
            }
        } else if (MatchLdLoc(falseInst, vLd)) {
            // condition ? (bool?)true : v ==> condition | v
            const TypeSystem::IType* utype = nullptr;
            ILInstruction* ctorArg = nullptr;
            if (NullableLiftingTransform::MatchNullableCtor(trueInst, utype, ctorArg) &&
                NullableLiftingTransform::IsKnownType(utype,
                                                       TypeSystem::KnownTypeCode::Boolean) &&
                IsLdcI4(ctorArg, 1)) {
                auto condOwned = DetachFromParent(condition);
                auto falseOwned = ConsumeArm(falseInst, falseSink);
                auto lifted = std::make_unique<ThreeValuedBoolOr>(
                    std::move(condOwned), std::move(falseOwned));
                return std::move(lifted);
            }
        }
    }
    return nullptr;
}


bool ExpressionTransforms::RunIfNullableLift(IfInstruction* iff) {
    // Thin caller over LiftNullableCore (the shared Lift core): the if's
    // condition/arms are in-tree, so both sinks are empty and ConsumeArm reduces
    // to DetachFromParent. On a successful lift, ReplaceIfWithLiftedValue applies
    // the block-model adaptation (ReplaceWith for a sub-expression value-if, or
    // the lifted value becomes a non-terminal + a Branch final for a block-final
    // if). Gated on LiftNullables (checked inside LiftNullableCore).
    if (!iff || !iff->Condition) return false;
    std::unique_ptr<ILInstruction> trueSink, falseSink;
    auto lifted = LiftNullableCore(iff->Condition.get(), iff->TrueInst.get(),
                                    iff->FalseInst.get(), trueSink, falseSink);
    if (lifted) return ReplaceIfWithLiftedValue(iff, std::move(lifted));
    return false;
}

bool ExpressionTransforms::RunBinaryNumericNullableLift(BinaryNumericInstruction* bni) {
    // Port of NullableLiftingTransform.Run(BinaryNumericInstruction): the
    // VS2017.8 / Roslyn 2.9 optimization that turns `&&` (short-circuit) into `&`
    // (BitAnd) on bool operands is analysed as-if it were still the short-circuit
    // form -- Lift(bni, bni.Left, bni.Right, new LdcI4(0)) where bni.Left is the
    // condition, bni.Right is the true arm, and a fresh LdcI4(0) is the false arm.
    // The fresh LdcI4(0) is NOT a child of bni, so it is owned by falseSink and
    // consumed via ConsumeArm only when a fold fires that needs it (the LiftNormal
    // conv.nop.lifted / DoLift wraps); folds that build fresh nodes
    // (MatchCompOrDecimal, bool? equality) leave it in falseSink to be discarded.
    // A BNI is always a value (never a block final), so the lifted result replaces
    // bni via a clean ReplaceWith (no block-model adaptation). Gated on
    // LiftNullables (checked inside LiftNullableCore). The caller
    // (VisitBinaryNumericInstruction) additionally gates on both operands being
    // Boolean-typed (the C# InferType == Boolean), via IsBooleanValue.
    if (!bni || !bni->Left) return false;
    std::unique_ptr<ILInstruction> trueSink;  // empty (bni.Right is in-tree)
    std::unique_ptr<ILInstruction> falseSink = std::make_unique<LdcI4>(0);
    auto lifted = LiftNullableCore(bni->Left.get(), bni->Right.get(),
                                    falseSink.get(), trueSink, falseSink);
    if (lifted) { bni->ReplaceWith(std::move(lifted)); return true; }
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

    // The C# then runs NullableLiftingTransform (the Run(IfInstruction) bool?
    // equality fold -- now ported as RunIfNullableLift), then TransformDynamic-
    // AddAssignOrRemoveAssign and UserDefinedLogicTransform (both deferred -- they
    // need DynamicIsEventInstruction and MatchLogicAnd/Or) before the
    // `match(x) ? true : false` fold.
    if (RunIfNullableLift(iff)) return;  // the if was replaced by a lifted Comp
    if (FoldMatchTrueFalse(iff)) return;  // the if was replaced by the match
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

bool ExpressionTransforms::FoldMatchTrueFalse(IfInstruction* iff) {
    // match(x) ? true : false -> match(x): a conditional whose condition is a
    // pattern match and whose arms are ldc.i4 1 / ldc.i4 0 is redundant -- the
    // MatchInstruction already evaluates to 1 (matched) / 0 (not matched), so
    // the if just re-wraps it. Replace the if with the condition (the pattern
    // match). The C# does `inst.ReplaceWith(matchCondition)` (the if is always a
    // non-terminal there); this port's if-as-final block model needs an
    // adaptation when the if is a block's FinalInstruction (a MatchInstruction
    // is a value, not control flow, so it cannot be the final).
    if (!iff || !iff->Condition) return false;
    const ILInstruction* testedOperand = nullptr;
    if (!MatchInstruction::IsPatternMatch(iff->Condition.get(), testedOperand, settings_))
        return false;
    // Both arms must be the ldc.i4 1 / ldc.i4 0 the compiler emits for the
    // `match ? true : false` conversion. Bare LdcI4 (a value-position if) or a
    // single-instruction expression Block wrapping it (the inlined-fall-through
    // shape) are both accepted (ArmIsLdcI4).
    if (!ArmIsLdcI4(iff->TrueInst.get(), 1) || !ArmIsLdcI4(iff->FalseInst.get(), 0))
        return false;

    // The if is the block's FinalInstruction (a statement-if) when its parent is
    // a Block holding it as the final. The MatchInstruction (a value, not control
    // flow) cannot be the final, so that case needs the block-model adaptation
    // (the match becomes a non-terminal + a Branch final); otherwise (the if is a
    // sub-expression value) ReplaceWith is a clean in-place swap. Resolve which
    // case applies and verify the block-final case has a fall-through target
    // BEFORE detaching the condition (a detached condition with no fold would
    // free the match and leave the if with a null condition, corrupting the tree).
    auto* block = dynamic_cast<Block*>(iff->Parent);
    bool isBlockFinal = block && block->FinalInstruction.get() == iff;
    Block* nextBlock = nullptr;
    if (isBlockFinal) {
        nextBlock = NextBlockInContainer(block);
        if (!nextBlock) return false;  // no fall-through target; leave the if intact
    }

    // Detach the condition (the pattern match) before the if is destroyed.
    auto match = iff->TakeChild(0);

    if (isBlockFinal) {
        // The MatchInstruction becomes a non-terminal statement -- its side
        // effect (storing the matched value into Variable) is preserved, and
        // the 1/0 results were discarded either way -- and a Branch to the next
        // block (the positional fall-through) replaces the if-final. This
        // mirrors the HandleConditionalOperator block-model adaptation.
        block->Add(std::move(match));
        block->SetFinal(std::make_unique<Branch>(nextBlock));  // destroys the if
    } else {
        // The if is a sub-expression value (e.g. `stloc V(if (match) 1 else 0)`)
        // or a non-terminal in a Block. ReplaceWith cleanly swaps the if for the
        // match in the parent's slot (the C# in-place ReplaceWith).
        iff->ReplaceWith(std::move(match));  // destroys the if
    }
    // The C# does not RequestRerun here (unlike HandleConditionalOperator): the
    // match was already visited as the if's condition above, so its children
    // are already rewritten. The fold only moves the already-visited match into
    // the if's slot; no re-visit is needed.
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
