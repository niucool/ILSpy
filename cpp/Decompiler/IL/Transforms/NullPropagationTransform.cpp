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

// Port of ICSharpCode.Decompiler/IL/Transforms/NullPropagationTransform.cs
// (subset). See the header for the scope. This file ports the ReferenceType,
// NullableByValue, and NullableByReference modes of `Run` + `TryNullPropagation`
// (the `ldnull`, `default(Nullable<T>)`, and `NullCoalescing` output cases) +
// `IsValidAccessChain` (approximated) + `IntroduceUnwrap`, plus the
// `IsProtectedIfInst` and `MatchNullableRewrap` static helpers, and the
// void-call subset of `RunStatements` (via `NullPropagationStatementTransform`,
// the `?.` statement form). The UnconstrainedType mode, the
// TransformNullPropagationOnUnconstrainedGenericExpression pattern, and the
// AddressOf / LdObjIfRef / Dynamic* access-chain cases are deferred.

#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

namespace {

// File-local MatchLdLoc: a bare LdLoc reports its variable. Duplicated from
// ExpressionTransforms.cpp (the shared helper is file-local there; this port
// follows the MatchLdLoc precedent of being duplicated where needed).
bool MatchLdLoc(ILInstruction* inst, ILVariable*& v) {
    v = nullptr;
    if (!inst || inst->Op != OpCode::LdLoc) return false;
    v = static_cast<LdLoc*>(inst)->Variable.get();
    return v != nullptr;
}

// File-local MatchLdLoca: a bare LdLoca reports its variable.
bool MatchLdLoca(ILInstruction* inst, ILVariable*& v) {
    v = nullptr;
    if (!inst || inst->Op != OpCode::LdLoca) return false;
    v = static_cast<LdLoca*>(inst)->Variable.get();
    return v != nullptr;
}

// Port of ILInstruction.MatchLdLocRef(v): matches `ldloc(v)` or `ldloca(v)`.
// Used by IsValidAccessChain's ReferenceType end-of-chain check.
bool MatchLdLocRef(ILInstruction* inst, ILVariable* v) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdLoc)
        return static_cast<LdLoc*>(inst)->Variable.get() == v;
    if (inst->Op == OpCode::LdLoca)
        return static_cast<LdLoca*>(inst)->Variable.get() == v;
    return false;
}

// File-local IsLdcI4: a bare LdcI4 with the given value. Duplicated from
// ExpressionTransforms.cpp (file-local there).
bool IsLdcI4(const ILInstruction* inst, int value) {
    if (!inst || inst->Op != OpCode::LdcI4) return false;
    return static_cast<const LdcI4*>(inst)->Value == value;
}

// File-local MatchLogicOr: an IfInstruction whose TrueInst is ldc.i4 1.
// Duplicated from ExpressionTransforms.cpp (file-local there). Used by
// IsProtectedIfInst.
bool MatchLogicOr(ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::IfInstruction) return false;
    auto* iff = static_cast<IfInstruction*>(inst);
    return IsLdcI4(iff->TrueInst.get(), 1);
}

// File-local MatchLogicAnd: an IfInstruction whose FalseInst is ldc.i4 0.
// Duplicated from ExpressionTransforms.cpp (file-local there). Used by
// IsProtectedIfInst.
bool MatchLogicAnd(ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::IfInstruction) return false;
    auto* iff = static_cast<IfInstruction*>(inst);
    return IsLdcI4(iff->FalseInst.get(), 0);
}

// File-local IsInConditionSlot: the instruction occurs in a condition slot
// (an IfInstruction's Condition at ChildIndex 0, or nested in one).
// Duplicated from ExpressionTransforms.cpp (file-local there). Used by
// IsProtectedIfInst.
bool IsInConditionSlot(const ILInstruction* inst) {
    if (!inst || !inst->Parent) return false;
    auto* parent = inst->Parent;
    if (parent->Op == OpCode::IfInstruction && inst->ChildIndex == 0)
        return true;
    return IsInConditionSlot(parent);
}

// DetachFromParent: TakeChild at the instruction's Parent+ChildIndex. Returns
// the owning unique_ptr (or nullptr if the instruction has no parent).
std::unique_ptr<ILInstruction> DetachFromParent(ILInstruction* inst) {
    if (!inst || !inst->Parent) return nullptr;
    return inst->Parent->TakeChild(inst->ChildIndex);
}

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). Mirrors the helper in NullCoalescingTransform /
// ExpressionTransforms / SwitchAnalysis. nullptr if `block` is not in a
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

// The Mode enum mirrors the C# NullPropagationTransform.Mode. ReferenceType,
// NullableByValue, and NullableByReference are ported here; UnconstrainedType
// is deferred (RunStatements only).
enum class Mode {
    ReferenceType,
    NullableByValue,
    NullableByReference,
    UnconstrainedType,
};

// Port of NullPropagationTransform.IsValidAccessChain.IsValidEndOfChain:
// dispatches on the mode. ReferenceType -> `ldloc`/`ldloca` of `testedVar`
// (MatchLdLocRef). NullableByValue -> `call GetValueOrDefault(ldloca testedVar)`
// (the match-against-v MatchGetValueOrDefault overload, D98).
// NullableByReference -> `call GetValueOrDefault(ldloc testedVar)` (the 1-arg
// report MatchGetValueOrDefault overload, D68, plus a match-against-v LdLoc
// check done inline -- a file-local match-against-v MatchLdLoc overload would
// be ambiguous with the extract MatchLdLoc(ILVariable*&) for an ILVariable*
// lvalue, the D66/D94 precedent). UnconstrainedType is deferred.
bool IsValidEndOfChain(ILInstruction* inst, ILVariable* testedVar, Mode mode) {
    if (!inst) return false;
    if (mode == Mode::ReferenceType)
        return MatchLdLocRef(inst, testedVar);
    if (mode == Mode::NullableByValue)
        return NullableLiftingTransform::MatchGetValueOrDefault(inst, testedVar);
    if (mode == Mode::NullableByReference) {
        ILInstruction* arg = nullptr;
        if (!NullableLiftingTransform::MatchGetValueOrDefault(inst, arg))
            return false;
        if (!arg || arg->Op != OpCode::LdLoc) return false;
        return static_cast<LdLoc*>(arg)->Variable.get() == testedVar;
    }
    return false;  // UnconstrainedType deferred
}

// Port of NullPropagationTransform.IsValidAccessChain:
// walks the access chain from `inst` down to the load of `testedVar`, checking
// every node is a valid `?.` access (a field load, a field address, an
// instance method call, an array length, an array element address, or a nested
// nullable.unwrap). Sets `finalLoad` to the load of testedVar at the end of the
// chain. Returns true when the chain is valid and has length >= 1 (at least one
// access after the load). The Call case is approximated: this port's Call
// carries no IsStatic / IsExtensionMethod / IsAccessor / IsGetter / ConstrainedTo
// metadata, so `IsInstanceCall` (true for instance call/callvirt, false for
// static and newobj) is the faithful gate; the AddressOf / LdObjIfRef special
// cases are not modeled and skipped; the Dynamic* nodes are not modeled and
// rejected. The LdFld (LdObj(LdFlda(target, f), type)) / LdFlda / LdLen /
// LdElema / NullableUnwrap cases are faithfully matched.
bool IsValidAccessChain(ILVariable* testedVar, Mode mode, ILInstruction* inst,
                         ILInstruction*& finalLoad) {
    finalLoad = nullptr;
    int chainLength = 0;
    while (inst) {
        // IsValidEndOfChain: dispatches on the mode -- ReferenceType ends in
        // ldloc/ldloca of testedVar; NullableByValue ends in
        // `call GetValueOrDefault(ldloca testedVar)`; NullableByReference ends
        // in `call GetValueOrDefault(ldloc testedVar)`.
        if (IsValidEndOfChain(inst, testedVar, mode)) {
            finalLoad = inst;
            return chainLength >= 1;
        }
        if (inst->Op == OpCode::LdObj) {
            // LdFld = LdObj(LdFlda(target, field), type) -> walk to the LdFlda's target
            auto* ldObj = static_cast<LdObj*>(inst);
            if (ldObj->Target && ldObj->Target->Op == OpCode::LdFlda) {
                auto* ldFlda = static_cast<LdFlda*>(ldObj->Target.get());
                inst = ldFlda->Target.get();
                chainLength++;
                continue;
            }
            return false;
        }
        if (inst->Op == OpCode::LdFlda) {
            // LdFlda(target, field) -> walk to target. The C# AddressOf/Struct
            // special case (target is AddressOf && field declaring type is a
            // struct -> walk to the AddressOf's Value) is not modeled (no
            // AddressOf node, no field declaring type); walk to the target
            // directly, matching the C# else-branch.
            auto* ldFlda = static_cast<LdFlda*>(inst);
            inst = ldFlda->Target.get();
            chainLength++;
            continue;
        }
        if (inst->Op == OpCode::Call) {
            auto* call = static_cast<Call*>(inst);
            if (call->IsNewObj) return false;  // not a NewObj
            if (call->Arguments.empty()) return false;
            // Approximation: only instance methods (IsInstanceCall) can be
            // `?.`-ed. The C# also allows extension methods (static but
            // callable via `?.`); this port carries no IsExtensionMethod, so
            // all static calls are rejected (conservative). The C# also rejects
            // non-getter accessors (setters/adders/removers); this port carries
            // no IsAccessor, so all instance calls are accepted (a faithfulness
            // gap, not a crash -- the transform fires 0 on the legacy-csc
            // corpus).
            if (!call->IsInstanceCall) return false;
            inst = call->Arguments[0].get();
            // ConstrainedTo / AddressOf / LdObjIfRef special cases not modeled.
            // ArgumentsAfterFirstMayUnwrapNull: any argument after the first
            // carrying MayUnwrapNull means the chain contains a `?.` not
            // directly part of it.
            for (std::size_t i = 1; i < call->Arguments.size(); ++i) {
                if (call->Arguments[i] &&
                    HasFlag(call->Arguments[i]->Flags(), InstructionFlags::MayUnwrapNull))
                    return false;
            }
            chainLength++;
            continue;
        }
        if (inst->Op == OpCode::LdLen) {
            inst = static_cast<LdLen*>(inst)->Argument.get();
            chainLength++;
            continue;
        }
        if (inst->Op == OpCode::LdElema) {
            auto* ldElema = static_cast<LdElema*>(inst);
            for (auto& idx : ldElema->Indices) {
                if (idx && HasFlag(idx->Flags(), InstructionFlags::MayUnwrapNull))
                    return false;
            }
            inst = ldElema->Array.get();
            chainLength++;
            continue;
        }
        if (inst->Op == OpCode::NullableUnwrap) {
            auto* unwrap = static_cast<NullableUnwrap*>(inst);
            inst = unwrap->Argument.get();
            // The RefInput/AddressOf special case is not modeled (no AddressOf).
            // The argument of the unwrap cannot be the end of the chain (that
            // would produce two `?.` immediately following each other).
            if (IsValidEndOfChain(inst, testedVar, mode))
                return false;
            chainLength++;
            continue;
        }
        // Dynamic* nodes and unknown nodes -> invalid chain
        return false;
    }
    return false;
}

// Port of NullPropagationTransform.IntroduceUnwrap: wraps the receiver load at
// the end of the access chain (`varLoad`) in a NullableUnwrap, placed in
// varLoad's parent slot. ReferenceType / UnconstrainedType reuse `varLoad`
// itself as the unwrap's Argument (the C# `new NullableUnwrap(varLoad.ResultType,
// varLoad, refInput: varLoad.ResultType == Ref)`), detaching it via TakeChild.
// NullableByValue / NullableByReference discard the `varLoad` (a
// `call GetValueOrDefault(ldloca/ldloc testedVar)`) and build a FRESH
// `LdLoc(testedVar)` as the unwrap's Argument (the C# `new LdLoc(testedVar)`),
// with refInput=true for NullableByReference. The shared_ptr to `testedVar` is
// taken from the call's first argument (the ldloca/ldloc), which holds the same
// variable; it is extracted BEFORE the TakeChild so a bail leaves the tree
// intact (no dangling empty slot).
void IntroduceUnwrap(ILVariable* /*testedVar*/, ILInstruction* varLoad, Mode mode) {
    if (!varLoad || !varLoad->Parent) return;
    ILInstruction* parent = varLoad->Parent;
    int childIndex = varLoad->ChildIndex;
    if (mode == Mode::ReferenceType || mode == Mode::UnconstrainedType) {
        StackType resultType = varLoad->ResultType();
        auto varLoadOwned = parent->TakeChild(childIndex);
        auto unwrap = std::make_unique<NullableUnwrap>(
            resultType, std::move(varLoadOwned), resultType == StackType::Ref);
        parent->SetChild(childIndex, std::move(unwrap));
        return;
    }
    if (mode == Mode::NullableByValue || mode == Mode::NullableByReference) {
        // varLoad is `call GetValueOrDefault(ldloca/ldloc testedVar)`. Get the
        // shared_ptr to testedVar from the call's first argument (the ldloca/
        // ldloc), which holds the same variable, BEFORE detaching the varLoad.
        ILVariablePtr varPtr;
        if (varLoad->Op == OpCode::Call) {
            auto* call = static_cast<Call*>(varLoad);
            if (!call->Arguments.empty()) {
                ILInstruction* firstArg = call->Arguments[0].get();
                if (firstArg && firstArg->Op == OpCode::LdLoca)
                    varPtr = static_cast<LdLoca*>(firstArg)->Variable;
                else if (firstArg && firstArg->Op == OpCode::LdLoc)
                    varPtr = static_cast<LdLoc*>(firstArg)->Variable;
            }
        }
        if (!varPtr) return;  // bail before TakeChild (tree intact)
        StackType resultType = varLoad->ResultType();
        auto varLoadOwned = parent->TakeChild(childIndex);  // detached; discarded
        auto ldLoc = std::make_unique<LdLoc>(std::move(varPtr));
        bool refInput = (mode == Mode::NullableByReference);
        auto unwrap = std::make_unique<NullableUnwrap>(
            resultType, std::move(ldLoc), refInput);
        parent->SetChild(childIndex, std::move(unwrap));
        return;
    }
}

// Port of a minimal `ILInstruction.InferType(typeSystem)` for the access
// chain root: returns the result type IType of `nonNullInst` (the access chain
// root, after NullableRewrap/NullableCtor stripping). A Call -> the method's
// return type IType (`Call::ReturnIType`, resolved at reader time from the
// method signature); a LdObj (LdFld = LdObj(LdFlda(..), type)) -> the LdObj's
// Type; otherwise nullptr (conservative -- the NullCoalescing output case does
// not fire). The C# InferType uses the resolver and covers more node kinds; this
// minimal port covers the access-chain roots the `?.` lowering produces (a Call
// or a field load), matching the D52/D72/D73 permissive-type-recognition
// precedent. Used by TryNullPropagation's NullCoalescing output case to gate on
// NullableType.IsNonNullableValueType(returnType) && !returnType.IsByRefLike.
const TypeSystem::IType* InferAccessChainType(ILInstruction* inst) {
    if (!inst) return nullptr;
    if (inst->Op == OpCode::Call)
        return static_cast<Call*>(inst)->ReturnIType.get();
    if (inst->Op == OpCode::LdObj)
        return static_cast<LdObj*>(inst)->Type.get();
    return nullptr;
}

// Port of NullPropagationTransform.TryNullPropagation (ReferenceType + ldnull
// output subset): `testedVar != null ? nonNullInst : nullInst` folds into
// `testedVar?.nonNullInst` (a NullableRewrap around the access chain, with the
// receiver load rewritten to a NullableUnwrap by IntroduceUnwrap). The
// `default(Nullable<T>)` output case and the `NullCoalescing` output case
// (testedVar != null ? nonNullInst : nullInst where the chain returns a
// non-nullable value type, not a by-ref-like type, and the NullableRewrap /
// NullableCtor was NOT stripped) are now ported. Returns the lifted instruction
// (owned) or nullptr if no fold fired. `nonNullInst` is detached from its
// parent (the if) before being wrapped in the NullableRewrap (no GC).
std::unique_ptr<ILInstruction> TryNullPropagation(ILVariable* testedVar,
                                                   ILInstruction* nonNullInst,
                                                   ILInstruction* nullInst,
                                                   Mode mode) {
    bool removedRewrapOrNullableCtor = false;
    const TypeSystem::IType* unusedType = nullptr;
    ILInstruction* ctorArg = nullptr;
    if (NullableLiftingTransform::MatchNullableCtor(nonNullInst, unusedType, ctorArg)) {
        nonNullInst = ctorArg;
        removedRewrapOrNullableCtor = true;
    } else if (NullPropagationTransform::MatchNullableRewrap(nonNullInst, ctorArg)) {
        nonNullInst = ctorArg;
        removedRewrapOrNullableCtor = true;
    }
    ILInstruction* varLoad = nullptr;
    if (!IsValidAccessChain(testedVar, mode, nonNullInst, varLoad))
        return nullptr;
    // The ldnull output case: `testedVar != null ? testedVar.AccessChain : null`
    // => `testedVar?.AccessChain` (a NullableRewrap).
    if (nullInst && nullInst->Op == OpCode::LdNull) {
        IntroduceUnwrap(testedVar, varLoad, mode);
        auto nonNullOwned = DetachFromParent(nonNullInst);
        return std::make_unique<NullableRewrap>(std::move(nonNullOwned));
    }
    // The default(Nullable<T>) output case:
    // `testedVar != null ? testedVar.AccessChain : default(Nullable<T>)`
    // => `testedVar?.AccessChain` (a NullableRewrap). MatchNull recognises
    // `default(Nullable<T>)` (a DefaultValue whose Type is a Nullable<T>) -- the
    // faithful equivalent of the C# `nullInst.MatchDefaultValue(out type) &&
    // type.IsKnownType(KnownTypeCode.NullableOfT)`: GetUnderlyingTypeOfNullable
    // checks the generic definition is KnownType(NullableOfT) (what the C#
    // IsKnownType's GetDefinition() yields for a ParameterizedType).
    {
        const TypeSystem::IType* nullUnderlying = nullptr;
        if (nullInst && NullableLiftingTransform::MatchNull(nullInst, nullUnderlying)) {
            IntroduceUnwrap(testedVar, varLoad, mode);
            auto nonNullOwned = DetachFromParent(nonNullInst);
            return std::make_unique<NullableRewrap>(std::move(nonNullOwned));
        }
    }
    // The NullCoalescing output case: `testedVar != null ?
    // testedVar.AccessChain : nullInst` where the access chain returns a
    // non-nullable value type (and is not a by-ref-like type that cannot be
    // wrapped in Nullable<T>) => `testedVar?.AccessChain ?? nullInst`. Only
    // valid when the NullableRewrap/NullableCtor was NOT stripped (the access
    // chain is directly the non-null arm, not a Nullable<T>-wrapped value).
    // `returnType` is the access chain root's result IType (InferAccessChainType);
    // the C# uses `nonNullInst.InferType(context.TypeSystem)`. The
    // NullCoalescingInstruction(NullableWithValueFallback) wraps the
    // NullableRewrap (the `?.`) with the fallback (the `??`), with
    // UnderlyingResultType = nullInst->ResultType() (the C#
    // `UnderlyingResultType = nullInst.ResultType`).
    if (!removedRewrapOrNullableCtor && nullInst) {
        const TypeSystem::IType* returnType = InferAccessChainType(nonNullInst);
        if (returnType
            && NullableLiftingTransform::IsNonNullableValueType(returnType)
            && !NullableLiftingTransform::IsByRefLike(returnType)) {
            // Capture the fallback's ResultType before detaching (the C#
            // `UnderlyingResultType = nullInst.ResultType`), following the
            // precondition-before-mutation discipline.
            StackType underlyingResultType = nullInst->ResultType();
            IntroduceUnwrap(testedVar, varLoad, mode);
            auto nonNullOwned = DetachFromParent(nonNullInst);
            auto rewrap = std::make_unique<NullableRewrap>(std::move(nonNullOwned));
            auto nullOwned = DetachFromParent(nullInst);
            auto nc = std::make_unique<NullCoalescingInstruction>(
                NullCoalescingKind::NullableWithValueFallback,
                std::move(rewrap),
                std::move(nullOwned));
            nc->UnderlyingResultType = underlyingResultType;
            return nc;
        }
    }
    return nullptr;
}

// Port of NullPropagationTransform.TryNullPropForVoidCall: the void-call `?.`
// statement form -- `if (testedVar != null) { testedVar.AccessChain(); }`
// folds into `testedVar?.AccessChain();` (a void NullableRewrap, the `?.`
// statement whose value is discarded). `ifInst` is the block's FinalInstruction
// (this port's if-as-final model); `body` is `ifInst->TrueInst` as a Block with
// exactly one instruction (the void call); the body instruction may be wrapped
// in a NullableRewrap (stripped before the access chain analysis, matching the
// C# `bodyInst.MatchNullableRewrap`). `hostBlock` is the block whose
// FinalInstruction is `ifInst` (for the block-model adaptation: the void
// NullableRewrap becomes a non-terminal and a Branch to the next block replaces
// the if-final, the same adaptation NullCoalescingTransform / ReplaceIfWithLiftedValue
// use). Returns true if the fold fired. The bodyInst is detached from the body
// Block (TakeChild) before the if is destroyed (no GC); the access chain
// inside bodyInst is mutated by IntroduceUnwrap (the receiver load is wrapped in
// a NullableUnwrap) while bodyInst is still in the body Block, then detached.
bool TryNullPropForVoidCall(ILVariable* testedVar, Mode mode,
                             IfInstruction* ifInst, Block* hostBlock,
                             ILTransformContext& context) {
    if (!ifInst || !ifInst->TrueInst) return false;
    if (ifInst->TrueInst->Op != OpCode::Block) return false;
    auto* body = static_cast<Block*>(ifInst->TrueInst.get());
    if (body->Instructions.size() != 1) return false;
    ILInstruction* bodyInst = body->Instructions[0].get();
    if (!bodyInst) return false;
    // Strip a NullableRewrap wrapper from the body instruction (the C#
    // `bodyInst.MatchNullableRewrap(out arg); bodyInst = arg`).
    ILInstruction* rewrapArg = nullptr;
    if (NullPropagationTransform::MatchNullableRewrap(bodyInst, rewrapArg))
        bodyInst = rewrapArg;
    if (!bodyInst) return false;
    // IsValidAccessChain: the body instruction must be a valid `?.` access chain
    // on `testedVar` with chainLength >= 1.
    ILInstruction* varLoad = nullptr;
    if (!IsValidAccessChain(testedVar, mode, bodyInst, varLoad)) return false;
    if (!varLoad) return false;
    // Resolve the fall-through target BEFORE mutating (the precondition-before-
    // mutation discipline: a detached bodyInst with no fold would corrupt the tree).
    Block* nextBlock = NextBlockInContainer(hostBlock);
    if (!nextBlock) return false;  // no fall-through; degenerate end-of-function `?.`
    context.StepOnce("Null-propagation (void call)");
    // IntroduceUnwrap: wrap the receiver load at the end of the access chain in
    // a NullableUnwrap. This mutates the access chain inside bodyInst (which is
    // still in the body Block / the stripped NullableRewrap at this point).
    IntroduceUnwrap(testedVar, varLoad, mode);
    // Detach bodyInst from its parent. When the NullableRewrap was NOT stripped,
    // bodyInst is body->Instructions[0] (detached from the body Block). When it
    // WAS stripped, bodyInst is the inner instruction (the Call) inside the
    // NullableRewrap (body->Instructions[0]->Argument); DetachFromParent detaches
    // it from the NullableRewrap, leaving the NullableRewrap with a null Argument
    // (destroyed harmlessly when the if-final is replaced below). The body Block /
    // the NullableRewrap are owned by the if's TrueInst and are destroyed when the
    // if-final is replaced (no GC; the detached bodyInst survives).
    auto bodyInstOwned = DetachFromParent(bodyInst);
    if (!bodyInstOwned) return false;  // bodyInst had no parent (shouldn't happen)
    // Build the void NullableRewrap from the body instruction. For a void call
    // (ReturnType Void) the NullableRewrap's ResultType is Void (the `?.`
    // statement form); for a non-void access chain it is O (the nullable result).
    auto rewrap = std::make_unique<NullableRewrap>(std::move(bodyInstOwned));
    // Block-model adaptation: the if is the host block's FinalInstruction. The
    // void NullableRewrap becomes a non-terminal statement (its value is
    // discarded, matching the if-as-statement) and a Branch to the next block
    // (the fall-through the if's null FalseInst represented) replaces the
    // if-final. Destroys the if and the now-empty body Block.
    hostBlock->Add(std::move(rewrap));
    hostBlock->SetFinal(std::make_unique<Branch>(nextBlock));
    return true;
}

} // namespace

// static
bool NullPropagationTransform::IsProtectedIfInst(IfInstruction* ifInst) {
    if (!ifInst) return false;
    if (!MatchLogicOr(ifInst) && !MatchLogicAnd(ifInst)) return false;
    return IsInConditionSlot(ifInst);
}

// static
bool NullPropagationTransform::MatchNullableRewrap(ILInstruction* inst,
                                                    ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::NullableRewrap) return false;
    arg = static_cast<NullableRewrap*>(inst)->Argument.get();
    return arg != nullptr;
}

// static
std::unique_ptr<ILInstruction> NullPropagationTransform::Run(
    ILInstruction* condition, ILInstruction* trueInst, ILInstruction* falseInst) {
    if (!condition) return nullptr;
    // ReferenceType mode: `comp(ldloc(testedVar) ==/!= null)`.
    if (condition->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(condition);
        ILVariable* testedVar = nullptr;
        if (!MatchLdLoc(comp->Left.get(), testedVar)) return nullptr;
        if (!comp->Right || comp->Right->Op != OpCode::LdNull) return nullptr;
        if (comp->LiftingKind != ComparisonLiftingKind::None) return nullptr;
        if (comp->Kind == ComparisonKind::Equality) {
            // testedVar == null ? trueInst : falseInst
            // => TryNullPropagation(testedVar, falseInst, trueInst)
            return TryNullPropagation(testedVar, falseInst, trueInst,
                                       Mode::ReferenceType);
        }
        if (comp->Kind == ComparisonKind::Inequality) {
            // testedVar != null ? trueInst : falseInst
            return TryNullPropagation(testedVar, trueInst, falseInst,
                                       Mode::ReferenceType);
        }
    }
    // NullableByValue / NullableByReference: `call get_HasValue(loadInst)` on a
    // Nullable<T>, where loadInst is `ldloca v` (NullableByValue) or `ldloc v`
    // (NullableByReference). `loadInst.HasValue ? trueInst : falseInst` folds
    // into the `?.` lowering. UnconstrainedType is RunStatements-only
    // (deferred).
    {
        ILInstruction* loadInst = nullptr;
        if (NullableLiftingTransform::MatchHasValueCall(condition, loadInst)) {
            ILVariable* testedVar = nullptr;
            if (MatchLdLoca(loadInst, testedVar)) {
                return TryNullPropagation(testedVar, trueInst, falseInst,
                                           Mode::NullableByValue);
            }
            if (MatchLdLoc(loadInst, testedVar)) {
                return TryNullPropagation(testedVar, trueInst, falseInst,
                                           Mode::NullableByReference);
            }
        }
    }
    return nullptr;
}

void NullPropagationStatementTransform::Run(Block& block, int pos,
                                             StatementTransformContext& context) {
    if (!context.Base.Settings.NullPropagation) return;
    // The void-call `?.` if is the block's FinalInstruction (this port's
    // if-as-final model). The per-statement driver visits the if-final at the
    // last non-terminal position (pos == size-1) or for an if-final-only block
    // (pos == -1 == size-1). Bail at any other position so the fold fires once
    // (matching how ExpressionTransforms visits the if-final at pos == size-1).
    if (pos != static_cast<int>(block.Instructions.size()) - 1) return;
    auto* iff = dynamic_cast<IfInstruction*>(block.FinalInstruction.get());
    if (!iff) return;
    // The `?.` void-call pattern has no else (the C# `ifInst.FalseInst.MatchNop()`;
    // this port's if-as-final with no else has FalseInst == nullptr). Bail if there
    // is an else arm (not the `?.` void-call shape).
    if (iff->FalseInst) return;
    // ReferenceType mode: `comp(ldloc v != null)` (Inequality only, matching the
    // C# `comp.Kind == ComparisonKind.Inequality && comp.Left.MatchLdLoc &&
    // comp.Right.MatchLdNull`).
    if (iff->Condition && iff->Condition->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(iff->Condition.get());
        if (comp->Kind == ComparisonKind::Inequality &&
            comp->LiftingKind == ComparisonLiftingKind::None) {
            ILVariable* testedVar = nullptr;
            if (MatchLdLoc(comp->Left.get(), testedVar) &&
                comp->Right && comp->Right->Op == OpCode::LdNull) {
                TryNullPropForVoidCall(testedVar, Mode::ReferenceType,
                                        iff, &block, context.Base);
                return;
            }
        }
    }
    // NullableByValue / NullableByReference: `call get_HasValue(loadInst)` on a
    // Nullable<T>, where loadInst is `ldloca v` (NullableByValue) or `ldloc v`
    // (NullableByReference). UnconstrainedType is RunStatements-only (deferred).
    {
        ILInstruction* loadInst = nullptr;
        if (NullableLiftingTransform::MatchHasValueCall(iff->Condition.get(), loadInst)) {
            ILVariable* testedVar = nullptr;
            if (MatchLdLoca(loadInst, testedVar)) {
                TryNullPropForVoidCall(testedVar, Mode::NullableByValue,
                                        iff, &block, context.Base);
                return;
            }
            if (MatchLdLoc(loadInst, testedVar)) {
                TryNullPropForVoidCall(testedVar, Mode::NullableByReference,
                                        iff, &block, context.Base);
                return;
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
