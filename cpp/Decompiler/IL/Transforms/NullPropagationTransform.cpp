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
// (subset). See the header for the scope. This file ports the ReferenceType
// mode of `Run` + `TryNullPropagation` (the `ldnull` output case) +
// `IsValidAccessChain` (approximated) + `IntroduceUnwrap`, plus the
// `IsProtectedIfInst` and `MatchNullableRewrap` static helpers. The
// NullableByValue / NullableByReference / UnconstrainedType modes,
// RunStatements, the `default(Nullable<T>)` and `NullCoalescing` output cases
// (need InferType / NullableType.IsNonNullableValueType), and the
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
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"

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

// The Mode enum mirrors the C# NullPropagationTransform.Mode. Only
// ReferenceType is ported here; the others are deferred.
enum class Mode {
    ReferenceType,
    NullableByValue,
    NullableByReference,
    UnconstrainedType,
};

// Port of NullPropagationTransform.IsValidAccessChain (ReferenceType subset):
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
        // IsValidEndOfChain: the ReferenceType end is MatchLdLocRef(testedVar)
        // (ldloc or ldloca of the tested var). The NullableByValue /
        // NullableByReference ends are deferred.
        if (mode == Mode::ReferenceType && MatchLdLocRef(inst, testedVar)) {
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
            if (mode == Mode::ReferenceType && MatchLdLocRef(inst, testedVar))
                return false;
            chainLength++;
            continue;
        }
        // Dynamic* nodes and unknown nodes -> invalid chain
        return false;
    }
    return false;
}

// Port of NullPropagationTransform.IntroduceUnwrap (ReferenceType mode): wraps
// `varLoad` (the load of the tested variable at the end of the access chain) in
// a NullableUnwrap. The varLoad is detached from its parent (TakeChild) and
// re-parented as the NullableUnwrap's Argument; the NullableUnwrap is placed in
// the parent's slot. The refInput flag is true when the varLoad is a LdLoca
// (ResultType Ref).
void IntroduceUnwrap(ILVariable* /*testedVar*/, ILInstruction* varLoad, Mode mode) {
    if (!varLoad || !varLoad->Parent) return;
    StackType resultType = varLoad->ResultType();
    ILInstruction* parent = varLoad->Parent;
    int childIndex = varLoad->ChildIndex;
    auto varLoadOwned = parent->TakeChild(childIndex);
    // ReferenceType / UnconstrainedType: wrap varLoad in nullable.unwrap.
    // NullableByValue / NullableByReference are deferred (they build a fresh
    // LdLoc(testedVar) as the unwrap's Argument).
    if (mode == Mode::ReferenceType || mode == Mode::UnconstrainedType) {
        auto unwrap = std::make_unique<NullableUnwrap>(
            resultType, std::move(varLoadOwned), resultType == StackType::Ref);
        parent->SetChild(childIndex, std::move(unwrap));
    }
}

// Port of NullPropagationTransform.TryNullPropagation (ReferenceType + ldnull
// output subset): `testedVar != null ? nonNullInst : nullInst` folds into
// `testedVar?.nonNullInst` (a NullableRewrap around the access chain, with the
// receiver load rewritten to a NullableUnwrap by IntroduceUnwrap). The
// `default(Nullable<T>)` output case and the `NullCoalescing` output case
// (testedVar != null ? nonNullInst : nullInst where the chain returns a
// non-nullable value type) are deferred (need InferType /
// NullableType.IsNonNullableValueType). Returns the lifted instruction (owned)
// or nullptr if no fold fired. `nonNullInst` is detached from its parent (the
// if) before being wrapped in the NullableRewrap (no GC).
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
    // => `testedVar?.AccessChain` (a NullableRewrap). The default(Nullable<T>)
    // and NullCoalescing output cases are deferred (need InferType).
    if (nullInst && nullInst->Op == OpCode::LdNull) {
        IntroduceUnwrap(testedVar, varLoad, mode);
        auto nonNullOwned = DetachFromParent(nonNullInst);
        return std::make_unique<NullableRewrap>(std::move(nonNullOwned));
    }
    (void)removedRewrapOrNullableCtor;  // deferred: the NullCoalescing case
    return nullptr;
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
    // NullableByValue / NullableByReference (MatchHasValueCall condition) and
    // UnconstrainedType (RunStatements only) are deferred.
    return nullptr;
}

} // namespace ILSpy::Decompiler::IL
