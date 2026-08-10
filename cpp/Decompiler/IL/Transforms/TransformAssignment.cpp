// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
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

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformAssignment.cs
// (the per-statement Run wiring + the shared helpers). See
// TransformAssignment.hpp for the transform this is a foundation for.

#include "Decompiler/IL/Transforms/TransformAssignment.hpp"

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::IL {

ILInstruction* UnwrapSmallIntegerConv(ILInstruction* inst, Conv*& conv) {
    conv = dynamic_cast<Conv*>(inst);
    if (conv != nullptr && conv->Kind == ConversionKind::Truncate &&
        IsSmallIntegerType(conv->TargetType)) {
        // For compound assignments to small integers, the compiler emits a
        // "conv" instruction (a Truncate to the small-integer TargetType).
        // Return the conv's argument so the caller can build the compound
        // assign from the underlying binary and validate the conv separately.
        return conv->Argument.get();
    }
    // Not a small-integer-truncating conv: leave the instruction unchanged.
    // (conv may still be set to the dynamic_cast result for a non-Truncate or
    // non-small-integer conv, matching the C# `conv = inst as Conv` assignment;
    // the caller only consults `conv` when the returned instruction is a
    // BinaryNumericInstruction, which a Conv never is, so a stale non-null
    // conv on the no-unwrap path is harmless.)
    return inst;
}

namespace {

// The KnownTypeCode of a small-integer type, or None when `type` is not a known
// small-integer type. The C# `type.GetEnumUnderlyingType().GetDefinition()?.
// KnownTypeCode` reduces to the KnownType's code directly here: this port's
// IsSmallIntegerType(IType*) is only true for the small-integer KnownTypes
// (Boolean/SByte/Byte/Char/Int16/UInt16 -- an Enum KnownType reports GetSize 0,
// not small, so GetEnumUnderlyingType is moot for the small-integer guard). The
// ldc.i4 case in CheckImplicitTruncation consults this to decide whether the
// constant fits the target's range.
TypeSystem::KnownTypeCode SmallIntegerKnownTypeCode(const TypeSystem::IType* type) {
    if (!type) return TypeSystem::KnownTypeCode::None;
    if (const auto* k = dynamic_cast<const TypeSystem::KnownType*>(type)) {
        return k->Code();
    }
    return TypeSystem::KnownTypeCode::None;
}

// Port of TransformAssignment.CommonImplicitTruncation: combine the truncation
// results of the two arms of a BitAnd/BitOr/BitXor or an IfInstruction. Equal
// results pass through; any mismatch yields ValueChanged (if only one side can
// be fixed by a sign change, the other side's sign must not be flipped).
ImplicitTruncationResult CommonImplicitTruncation(ImplicitTruncationResult left,
                                                   ImplicitTruncationResult right) {
    if (left == right) return left;
    return ImplicitTruncationResult::ValueChanged;
}

} // namespace

ImplicitTruncationResult CheckImplicitTruncation(const ILInstruction* value,
                                                 const TypeSystem::IType* type,
                                                 bool /*allowNullableValue*/) {
    if (!TypeSystem::IsSmallIntegerType(type)) {
        // Implicit truncation in ILAst only happens for small integer types;
        // other types of implicit truncation in IL cause the ILReader to insert
        // conv instructions.
        return ImplicitTruncationResult::ValuePreserved;
    }
    // With small integer types, test whether the value might be changed by
    // truncation (GetSize) followed by sign/zero extension (GetSign).
    if (value && value->Op == OpCode::LdcI4) {
        const auto val = static_cast<const LdcI4*>(value)->Value;
        bool valueFits = false;
        switch (SmallIntegerKnownTypeCode(type)) {
            case TypeSystem::KnownTypeCode::Boolean:
                valueFits = (val == 0 || val == 1);
                break;
            case TypeSystem::KnownTypeCode::Byte:
                valueFits = (val >= 0 && val <= 255);
                break;
            case TypeSystem::KnownTypeCode::SByte:
                valueFits = (val >= -128 && val <= 127);
                break;
            case TypeSystem::KnownTypeCode::Int16:
                valueFits = (val >= -32768 && val <= 32767);
                break;
            case TypeSystem::KnownTypeCode::UInt16:
            case TypeSystem::KnownTypeCode::Char:
                valueFits = (val >= 0 && val <= 65535);
                break;
            default:
                valueFits = false;
                break;
        }
        return valueFits ? ImplicitTruncationResult::ValuePreserved
                         : ImplicitTruncationResult::ValueChanged;
    }
    if (const auto* conv = dynamic_cast<const Conv*>(value)) {
        const PrimitiveType primitiveType = TypeSystem::ToPrimitiveType(type);
        const PrimitiveType convTargetType = conv->TargetType;
        if (convTargetType == primitiveType) {
            return ImplicitTruncationResult::ValuePreserved;
        }
        if (GetSize(primitiveType) == GetSize(convTargetType) &&
            GetSign(primitiveType) != GetSign(convTargetType) &&
            HasOppositeSign(primitiveType)) {
            return ImplicitTruncationResult::ValueChangedDueToSignMismatch;
        }
        return ImplicitTruncationResult::ValueChanged;
    }
    if (value && value->Op == OpCode::Comp) {
        // comp returns 0 or 1, which always fits.
        return ImplicitTruncationResult::ValuePreserved;
    }
    if (const auto* bni = dynamic_cast<const BinaryNumericInstruction*>(value)) {
        switch (bni->Operator) {
            case BinaryNumericOperator::BitAnd:
            case BinaryNumericOperator::BitOr:
            case BinaryNumericOperator::BitXor: {
                // If both input values fit without truncation, the result fits.
                const auto leftTruncation =
                    CheckImplicitTruncation(bni->Left.get(), type, false);
                // If the left side is truncating and a sign change is not
                // possible, the right side need not be evaluated.
                if (leftTruncation == ImplicitTruncationResult::ValueChanged) {
                    return ImplicitTruncationResult::ValueChanged;
                }
                const auto rightTruncation =
                    CheckImplicitTruncation(bni->Right.get(), type, false);
                return CommonImplicitTruncation(leftTruncation, rightTruncation);
            }
            default:
                break;
        }
    }
    if (const auto* ifInst = dynamic_cast<const IfInstruction*>(value)) {
        const auto trueTruncation =
            CheckImplicitTruncation(ifInst->TrueInst.get(), type, false);
        if (trueTruncation == ImplicitTruncationResult::ValueChanged) {
            return ImplicitTruncationResult::ValueChanged;
        }
        const auto falseTruncation =
            CheckImplicitTruncation(ifInst->FalseInst.get(), type, false);
        return CommonImplicitTruncation(trueTruncation, falseTruncation);
    }
    // The C# else-branch consults value.InferType(compilation) to compare the
    // inferred type's size/sign against the target. This minimal type system
    // has no InferType, so the unmodeled shapes are approximated conservatively
    // as ValueChanged (the value might be changed by truncation), matching the
    // C# Unknown case -- a compound assignment to a small integer with an
    // unmodeled RHS does not fold.
    return ImplicitTruncationResult::ValueChanged;
}

bool IsImplicitTruncation(const ILInstruction* value,
                          const TypeSystem::IType* type,
                          bool allowNullableValue) {
    return CheckImplicitTruncation(value, type, allowNullableValue) !=
           ImplicitTruncationResult::ValuePreserved;
}

// ---------------------------------------------------------------------------
// IsCompoundStore / IsMatchingCompoundLoad / ValidateCompoundAssign (the
// D131 foundation subset). The C# IsCompoundStore has three cases -- StObj
// (the target's real type via InferType + IsCompatibleTypeForMemoryAccess),
// Call (IsSameMember + IMethod for the property-setter gate), and StLoc
// (Variable.Kind). This port has no InferType and no IsSameMember/IMethod, so
// only the StLoc case is ported (the self-contained Variable.Kind check); the
// StObj/Call cases are deferred (return false). Likewise IsMatchingCompoundLoad
// has three cases -- LdObj/StObj (IsDuplicatedAddressComputation +
// previousInstruction), MatchingGetterAndSetterCalls (IMethod/AccessorOwner),
// and LdLoc/StLoc (variable equality + a fresh LdLoca target +
// RecombineVariables finalizeMatch). Only the LdLoc/StLoc case is ported; the
// other two are deferred. ValidateCompoundAssign is the full wrapper (it calls
// the already-ported IsBinaryCompatibleWithType + the conv-match check).
// ---------------------------------------------------------------------------

namespace {

// Port of ILVariableEqualityComparer.Instance.Equals. Two variables are
// "equal" when they are the same object, or split fragments of one original
// variable (same Kind + Index). StackSlot/PatternLocal are always distinct.
// This port's ILVariable has no Function field (variables from different
// functions are distinct shared_ptrs, already handled by the `x == y` check)
// and no StateMachineField, so the equality reduces to the Index check. `x` and
// `y` are non-owning views (may be null).
bool VariablesEqual(const ILVariable* x, const ILVariable* y) {
    if (x == y) return true;
    if (x == nullptr || y == nullptr) return false;
    if (x->Kind == VariableKind::StackSlot || y->Kind == VariableKind::StackSlot)
        return false;
    if (x->Kind == VariableKind::PatternLocal || y->Kind == VariableKind::PatternLocal)
        return false;
    if (x->Kind != y->Kind) return false;
    // Index == -1 marks a synthetic slot (the C# Index == null); a real
    // Parameter/Local carries a non-negative index. Two split fragments share
    // the same Kind + Index.
    if (x->Index >= 0) return x->Index == y->Index;
    return false;
}

} // namespace

bool IsCompoundStore(ILInstruction* inst, TypeSystem::ITypePtr& storeType,
                     ILInstruction*& value) {
    storeType = nullptr;
    value = nullptr;
    if (const auto* stloc = dynamic_cast<StLoc*>(inst)) {
        // The StLoc case: a local or parameter store. The store type is the
        // variable's type; the value is the stored value (a non-owning view of
        // the StLoc's Value child). Returns true (the C# also accepts PinnedLocal
        // / PinnedRegionLocal here via the `Local || Parameter` Kind check --
        // those Kinds are not produced for a plain stloc in this port, so the
        // two-Kind check is faithful).
        if (stloc->Variable &&
            (stloc->Variable->Kind == VariableKind::Local ||
             stloc->Variable->Kind == VariableKind::Parameter)) {
            storeType = stloc->Variable->Type;
            value = stloc->Value.get();
            return true;
        }
    }
    // StObj (needs InferType for the target's real type) and Call (needs
    // IsSameMember + IMethod for the property-setter gate) are deferred.
    return false;
}

bool IsMatchingCompoundLoad(ILInstruction* load, ILInstruction* store,
                            std::unique_ptr<ILInstruction>& target,
                            CompoundTargetKind& targetKind,
                            CompoundFinalizeMatch& finalizeMatch,
                            const ILVariable* forbiddenVariable) {
    target = nullptr;
    targetKind = CompoundTargetKind::Address;
    finalizeMatch = nullptr;
    auto* ldloc = dynamic_cast<LdLoc*>(load);
    auto* stloc = dynamic_cast<StLoc*>(store);
    if (ldloc && stloc && VariablesEqual(ldloc->Variable.get(), stloc->Variable.get())) {
        // The LdLoc/StLoc case: the load and store are the same variable. The
        // compound-assign target is a fresh LdLoca of that variable (the C#
        // `new LdLoca(ldloc.Variable)`); TargetKind is Address; the finalizeMatch
        // collapses split-fragment variables via RecombineVariables (a no-op
        // when they are the same object).
        if (forbiddenVariable != nullptr &&
            VariablesEqual(ldloc->Variable.get(), forbiddenVariable)) {
            return false;
        }
        auto ldVar = ldloc->Variable;       // shared_ptr copy (outlives the load)
        auto stVar = stloc->Variable;        // shared_ptr copy (outlives the store)
        target = std::make_unique<LdLoca>(ldVar);
        targetKind = CompoundTargetKind::Address;
        finalizeMatch = [ldVar, stVar](ILFunction& fn) {
            fn.RecombineVariables(ldVar, stVar);
        };
        return true;
    }
    // LdObj/StObj (needs IsDuplicatedAddressComputation + previousInstruction)
    // and MatchingGetterAndSetterCalls (needs IMethod/AccessorOwner) are
    // deferred -- return false.
    return false;
}

bool ValidateCompoundAssign(const BinaryNumericInstruction* binary, const Conv* conv,
                            const TypeSystem::IType* targetType,
                            const ILTransformSettings* settings) {
    if (!NumericCompoundAssign::IsBinaryCompatibleWithType(binary, targetType, settings))
        return false;
    if (conv != nullptr) {
        // The unwrapped small-integer conv must match the (possibly sign-
        // swapped) target type's PrimitiveType and the binary's overflow-check
        // flag, otherwise the conv does not faithfully represent the truncation
        // the compound assign performs.
        if (!(conv->TargetType == TypeSystem::ToPrimitiveType(targetType) &&
              conv->CheckForOverflow == binary->CheckForOverflow))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TransformAssignment IStatementTransform (D132): the per-statement Run wiring
// + the TransformPostIncDecOperatorWithInlineStore binary case. The C#
// TransformAssignment is an IStatementTransform (a child of the GetILTransforms()
// StatementTransform, after ILInlining / ExpressionTransforms / the deferred
// DynamicIsEventAssignmentTransform) that rewrites inline-assignment and compound-
// assignment patterns into the cleaner ILAst forms. This iteration ports the
// self-contained TransformPostIncDecOperatorWithInlineStore binary case (the
// local/StLoc post-increment/decrement fold the D131 helpers unblock); the
// operator-call (op_Increment/op_Decrement) case (needs the
// UserDefinedCompoundAssign node + Call.IsLifted) and the
// TransformInlineAssignment* / TransformPostIncDecOperator (non-inline-store) /
// TransformPreIncDecOperatorWithInlineStore StObj/Call cases (need InferType /
// IsSameMember / IMethod) are deferred.
// ---------------------------------------------------------------------------

namespace {

// Find the ILFunction root that owns `inst` by walking the Parent chain to the
// root (ILFunction::IsRoot). The per-statement Run has no function handle (the
// StatementTransformContext carries only the block + settings), so the
// finalizeMatch callback (which calls ILFunction::RecombineVariables) needs the
// function this way. Mirrors the ExpressionTransforms.cpp / NullCoalescing-
// Transform.cpp FunctionOf helpers.
ILFunction* FunctionOf(ILInstruction* inst) {
    for (ILInstruction* p = inst; p != nullptr; p = p->Parent)
        if (p->IsRoot()) return static_cast<ILFunction*>(p);
    return nullptr;
}

// Match the constant 1 for the post-inc/dec binary's right operand. The C#
// `binary.Right.MatchLdcI(1) || binary.Right.MatchLdcF4(1) ||
// binary.Right.MatchLdcF8(1)`: MatchLdcI matches an LdcI4 or LdcI8 with value 1
// (the integer post-increment `V++` emits `ldc.i4 1`); MatchLdcF4/LdcF8 match
// the float 1.0 (a `double++`/`float++` post-increment). This port's reader does
// not wrap ldc constants in conv, so the bare LdcI4/LdcI8/LdcF4/LdcF8 cases
// cover the inputs.
bool IsLdcOne(const ILInstruction* inst) {
    if (!inst) return false;
    switch (inst->Op) {
        case OpCode::LdcI4:
            return static_cast<const LdcI4*>(inst)->Value == 1;
        case OpCode::LdcI8:
            return static_cast<const LdcI8*>(inst)->Value == 1;
        case OpCode::LdcF4:
            return static_cast<const LdcF4*>(inst)->Value == 1.0f;
        case OpCode::LdcF8:
            return static_cast<const LdcF8*>(inst)->Value == 1.0;
        default:
            return false;
    }
}

} // namespace

void TransformAssignment::Run(Block& block, int pos, StatementTransformContext& context) {
    // The C# gates the inline-assignment folds on MakeAssignmentExpressions and
    // the inc/dec folds on IntroduceIncrementAndDecrement (both must be true for
    // a compound assign). TransformPostIncDecOperatorWithInlineStore is an
    // inc/dec fold, so it is gated on IntroduceIncrementAndDecrement (the C#
    // also requires MakeAssignmentExpressions for the other folds, but the
    // inc/dec-only gate is faithful for this subset).
    if (!context.Base.Settings.IntroduceIncrementAndDecrement)
        return;
    if (TransformPostIncDecOperatorWithInlineStore(block, pos, context))
        context.RequestRerunCurrentPosition();
}

bool TransformAssignment::TransformPostIncDecOperatorWithInlineStore(
    Block& block, int pos, StatementTransformContext& context) {
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size())
        return false;
    ILInstruction* store = block.Instructions[static_cast<std::size_t>(pos)].get();
    if (!store) return false;
    // IsCompoundStore: the store is a `stloc V(...)` whose Variable is a
    // Local/Parameter; `targetType` is V.Type, `value` is the stored value (a
    // non-owning view of the StLoc's Value child).
    TypeSystem::ITypePtr targetType;
    ILInstruction* value = nullptr;
    if (!IsCompoundStore(store, targetType, value)) return false;

    // UnwrapSmallIntegerConv: peel the compiler's `conv` truncation to a small
    // integer that a compound assign to a small-integer local/field carries.
    Conv* conv = nullptr;
    ILInstruction* unwrapped = UnwrapSmallIntegerConv(value, conv);
    auto* binary = dynamic_cast<BinaryNumericInstruction*>(unwrapped);
    StLoc* stloc = nullptr;
    if (binary != nullptr && IsLdcOne(binary->Right.get())) {
        // Only Add/Sub (the ++ / -- operators) are valid post-inc/dec.
        if (!(binary->Operator == BinaryNumericOperator::Add ||
              binary->Operator == BinaryNumericOperator::Sub))
            return false;
        // When a small-integer conv was unwrapped, fix a sign mismatch between
        // the store type and the conv's target by flipping the store type's
        // sign (the C# `SwapSign`), so ValidateCompoundAssign's conv-match
        // gate sees the corrected type. SwapSign returns null for a type with
        // no opposite sign (a degenerate case that cannot arise for a real
        // small-integer size-match+sign-mismatch); keep the original type in
        // that case so the conv-match gate rejects the fold rather than
        // passing a null type.
        if (conv != nullptr) {
            const PrimitiveType primitiveType = TypeSystem::ToPrimitiveType(targetType.get());
            if (GetSize(primitiveType) == GetSize(conv->TargetType) &&
                GetSign(primitiveType) != GetSign(conv->TargetType)) {
                if (auto swapped = TypeSystem::SwapSign(targetType.get()))
                    targetType = std::move(swapped);
            }
        }
        if (!ValidateCompoundAssign(binary, conv, targetType.get(), &context.Base.Settings))
            return false;
        // binary.Left is the "inline store" -- a `stloc tmp(ldloc target)` that
        // captures the old value into tmp and yields it (the binary's left
        // operand). The fold promotes tmp to the compound assign's result.
        stloc = dynamic_cast<StLoc*>(binary->Left.get());
    } else {
        // The operator-call (op_Increment/op_Decrement) case is deferred: it
        // needs the UserDefinedCompoundAssign node + Call.IsLifted. Bail
        // conservatively (no fold) for any other value shape.
        return false;
    }
    if (stloc == nullptr) return false;
    if (!(stloc->Variable &&
          (stloc->Variable->Kind == VariableKind::Local ||
           stloc->Variable->Kind == VariableKind::StackSlot)))
        return false;
    // IsMatchingCompoundLoad: the load `stloc.Value` (ldloc target) and the
    // store (stloc target) access the same variable, so the compound-assign
    // target is a fresh LdLoca(target); the finalizeMatch collapses split-
    // fragment variables via RecombineVariables (a no-op for the common
    // same-variable case). forbiddenVariable = tmp (stloc->Variable) rejects a
    // match that moves the store over a use of tmp.
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Address;
    CompoundFinalizeMatch finalizeMatch;
    if (!IsMatchingCompoundLoad(stloc->Value.get(), store, target, targetKind,
                                finalizeMatch, stloc->Variable.get()))
        return false;
    // The old value (stloc.Value = ldloc target) must not be implicitly
    // truncated for tmp's type, otherwise the compound assign would lose the
    // truncation.
    if (IsImplicitTruncation(stloc->Value.get(), stloc->Variable->Type.get(), false))
        return false;

    context.Base.StepOnce("TransformPostIncDecOperatorWithInlineStore");
    if (finalizeMatch) {
        if (ILFunction* fn = FunctionOf(store))
            finalizeMatch(*fn);
    }

    // Detach the binary's right operand (the constant 1) before the store is
    // destroyed by SetChild (no GC -- a raw pointer into the store would dangle).
    auto rhs = std::move(binary->Right);
    // Build the NumericCompoundAssign from the binary's fields (the C#
    // NumericCompoundAssign constructor copies Operator/Sign/LeftInputType/
    // RightInputType/UnderlyingResultType/IsLifted/CheckForOverflow from the
    // binary) + the fresh LdLoca target + the detached constant + the (possibly
    // sign-swapped) store type, in the EvaluatesToOldValue (post-inc/dec) mode.
    auto nca = std::make_unique<NumericCompoundAssign>(
        binary->Operator, binary->CheckForOverflow, binary->Sign,
        binary->LeftInputType, binary->RightInputType, binary->ResultStackType,
        binary->IsLifted, targetType, CompoundEvalMode::EvaluatesToOldValue,
        std::move(target), targetKind, std::move(rhs));
    // stloc tmp(NumericCompoundAssign.op.old(ldloca target, ldc.i4 1)) =
    // `tmp = target++`. The new StLoc replaces the store at pos; the old store
    // (with its now-null-Right binary and the inline-store StLoc) is destroyed.
    ILVariablePtr tmpVar = stloc->Variable;
    block.SetChild(pos, std::make_unique<StLoc>(tmpVar, std::move(nca)));
    return true;
}

} // namespace ILSpy::Decompiler::IL
