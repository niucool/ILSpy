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
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
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
// assignment patterns into the cleaner ILAst forms. The per-statement Run wires
// the three inc/dec folds (TransformPostIncDecOperatorWithInlineStore /
// TransformPostIncDecOperator / TransformPreIncDecOperatorWithInlineStore), each
// with a binary case (D132/D133/D134, the `binary.op(ldloc, 1)` shape that builds
// a NumericCompoundAssign) and an operator-call case (D136, the `call
// op_Increment(arg)` shape that builds a UserDefinedCompoundAssign from the
// operator Call's resolved method metadata). The TransformInlineAssignment*
// StObj/Call cases (need InferType / IsSameMember / IMethod) are deferred.
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

// The short method name (the part after "::") of a resolved Call MethodName
// ("Namespace.Type::Member" -> "Member"). The inline-store inc/dec operator-call
// folds consult the bare name (op_Increment / op_Decrement) rather than
// UserDefinedCompoundAssign::IsIncrementOrDecrement (which also accepts the
// checked variants) -- the C# inline-store cases use the bare name only.
// Mirrors the NullableLiftingTransform::ShortMethodName / the
// CompoundAssignmentInstruction.cpp ShortMethodName helpers.
std::string_view ShortMethodName(std::string_view fullName) {
    const auto pos = fullName.rfind("::");
    return pos != std::string_view::npos ? fullName.substr(pos + 2) : fullName;
}

} // namespace

// ---------------------------------------------------------------------------
// TransformInlineAssignmentLocal: folds the two-instruction inline assignment to
// a local variable. The C# comment:
//   stloc s(value)
//   stloc l(ldloc s)
//     where neither 'stloc s' nor 'stloc l' truncates the value
// -->
//   stloc s(stloc l(value))
// s is a StackSlot (the compiler's evaluation-stack temp), l is a Local or
// Parameter. The fold wraps the value in a nested `stloc l` and replaces the
// outer `stloc s` with the inline-assignment expression so a later transform can
// treat the whole thing as an assignment expression. This is the self-
// contained sibling of TransformInlineAssignmentStObjOrCall (which needs InferType
// / IsSameMember / IMethod for the property/field inline-assign and is deferred);
// it only consults MatchLdLoc + IsImplicitTruncation + the variable Kinds.
// ---------------------------------------------------------------------------
bool TransformAssignment::TransformInlineAssignmentLocal(
    Block& block, int pos, StatementTransformContext& context) {
    // inst = block.Instructions[pos] as StLoc -- the stack-slot store of value.
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size())
        return false;
    auto* inst = dynamic_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (!inst || !inst->Variable) return false;
    // nextInst = block.Instructions.ElementAtOrDefault(pos + 1) as StLoc -- the
    // local/parameter store of ldloc s (the value is forwarded to l).
    const std::size_t nextIdx = static_cast<std::size_t>(pos) + 1;
    StLoc* nextInst = nullptr;
    if (nextIdx < block.Instructions.size())
        nextInst = dynamic_cast<StLoc*>(block.Instructions[nextIdx].get());
    if (!nextInst || !nextInst->Variable) return false;
    // s must be a StackSlot (the compiler's evaluation-stack temp).
    if (inst->Variable->Kind != VariableKind::StackSlot) return false;
    // l must be a Local or Parameter.
    if (!(nextInst->Variable->Kind == VariableKind::Local ||
          nextInst->Variable->Kind == VariableKind::Parameter))
        return false;
    // nextInst.Value must be ldloc s (the C# `nextInst.Value.MatchLdLoc(inst.Variable)`).
    auto* ldloc = dynamic_cast<LdLoc*>(nextInst->Value.get());
    if (!ldloc || !ldloc->Variable || ldloc->Variable.get() != inst->Variable.get())
        return false;
    // 'stloc s' must not implicitly truncate the value for s's type.
    if (IsImplicitTruncation(inst->Value.get(), inst->Variable->Type.get(), false))
        return false;
    // 'stloc l' must not implicitly truncate the value for l's type.
    if (IsImplicitTruncation(inst->Value.get(), nextInst->Variable->Type.get(), false))
        return false;
    // ref locals need to be initialized when they are declared, so avoid inline
    // assignments to ref locals (we can't easily check definite assignment here).
    // The C# `ILVariable.StackType` is `Type.GetStackType()`; the port computes it
    // via StackTypeOf on the variable's Type.
    if (StackTypeOf(nextInst->Variable->Type.get()) == StackType::Ref) return false;

    context.Base.StepOnce("Inline assignment to local variable");
    // Detach inst's Value before RemoveInstructionAt destroys inst (no GC -- a
    // raw pointer into inst would dangle). Capture the variables too (shared_ptr
    // copies outlive the StLocs). `inst->Value` is the StLoc's owning unique_ptr
    // member (not a child via SetChild), so std::move takes ownership cleanly and
    // leaves inst with ChildCount 0.
    auto value = std::move(inst->Value);
    auto stackVar = inst->Variable;
    auto var = nextInst->Variable;
    // Remove the stack-slot store at pos. nextInst shifts down to pos and its
    // ChildIndex is renumbered by RemoveInstructionAt's RenumberChildren, so the
    // subsequent ReplaceWith (which uses Parent + ChildIndex) targets the correct
    // (now pos) slot.
    block.RemoveInstructionAt(static_cast<std::size_t>(pos));
    // Build `stloc s(stloc l(value))` and replace nextInst (now at pos) with it.
    // The outer StLoc wraps a fresh inner StLoc carrying l and the detached value.
    auto inlineAssignment = std::make_unique<StLoc>(
        stackVar, std::make_unique<StLoc>(var, std::move(value)));
    nextInst->ReplaceWith(std::move(inlineAssignment));
    return true;
}

void TransformAssignment::Run(Block& block, int pos, StatementTransformContext& context) {
    // The C# gates the whole Run on both MakeAssignmentExpressions and
    // IntroduceIncrementAndDecrement (the inline-assignment folds need
    // MakeAssignmentExpressions, the inc/dec folds need
    // IntroduceIncrementAndDecrement; the C# top-level gate requires both).
    if (!context.Base.Settings.MakeAssignmentExpressions ||
        !context.Base.Settings.IntroduceIncrementAndDecrement)
        return;
    // The C# dispatches TransformInlineAssignmentStObjOrCall ||
    // TransformInlineAssignmentLocal first (the inline-assignment folds), then
    // the three inc/dec folds. TransformInlineAssignmentStObjOrCall (the
    // StObj/Call inline-assign, needs InferType / IsSameMember / IMethod) is
    // deferred, so only TransformInlineAssignmentLocal is wired here; it runs
    // before the inc/dec folds so a folded inline-assignment short-circuits the
    // position (the driver advances or re-runs). The inline-assignment folds do
    // NOT call RequestRerunCurrentPosition (the C# `return` after them); only the
    // inc/dec folds request a rerun (the C# `context.RequestRerun()`).
    if (TransformInlineAssignmentLocal(block, pos, context))
        return;
    if (TransformPostIncDecOperatorWithInlineStore(block, pos, context) ||
        TransformPostIncDecOperator(block, pos, context) ||
        TransformPreIncDecOperatorWithInlineStore(block, pos, context))
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
    // Set in the operator-call (op_Increment/op_Decrement) branch; nullptr in
    // the binary branch. The two are disjoint (a Call is never a
    // BinaryNumericInstruction), so exactly one is set on the success path.
    Call* operatorCall = nullptr;
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
    } else if ((operatorCall = dynamic_cast<Call*>(value)) != nullptr) {
        // The operator-call (op_Increment/op_Decrement) case: the C# checks
        // `value is Call operatorCall && operatorCall.Method.IsOperator &&
        // operatorCall.Arguments.Count == 1` then the BARE method name (NOT
        // IsIncrementOrDecrement, which also accepts op_CheckedIncrement /
        // op_CheckedDecrement -- the inline-store cases in C# use the bare
        // name only). This port's Call carries IsOperator (set by the IL
        // reader from the op_* name); IsLifted defaults false (this port has
        // no resolver / ILiftedOperator). `stloc` is the operator call's
        // single argument (the inline-store StLoc capturing the old value).
        if (!operatorCall->IsOperator || operatorCall->Arguments.size() != 1)
            return false;
        const auto name = ShortMethodName(operatorCall->MethodName);
        if (name != "op_Increment" && name != "op_Decrement") return false;
        if (operatorCall->IsLifted) return false;  // TODO: lifted user-defined operators
        stloc = dynamic_cast<StLoc*>(operatorCall->Arguments[0].get());
    } else {
        // Any other value shape is not a post-inc/dec the fold handles.
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

    // The result StLoc carries tmp (the inline-store temp), not the target.
    // Capture it before SetChild destroys the store (and the inline store).
    ILVariablePtr tmpVar = stloc->Variable;
    if (binary != nullptr) {
        // Detach the binary's right operand (the constant 1) before the store
        // is destroyed by SetChild (no GC -- a raw pointer into the store
        // would dangle).
        auto rhs = std::move(binary->Right);
        // Build the NumericCompoundAssign from the binary's fields (the C#
        // NumericCompoundAssign constructor copies Operator/Sign/LeftInputType/
        // RightInputType/UnderlyingResultType/IsLifted/CheckForOverflow from
        // the binary) + the fresh LdLoca target + the detached constant + the
        // (possibly sign-swapped) store type, in the EvaluatesToOldValue
        // (post-inc/dec) mode.
        auto nca = std::make_unique<NumericCompoundAssign>(
            binary->Operator, binary->CheckForOverflow, binary->Sign,
            binary->LeftInputType, binary->RightInputType, binary->ResultStackType,
            binary->IsLifted, targetType, CompoundEvalMode::EvaluatesToOldValue,
            std::move(target), targetKind, std::move(rhs));
        // stloc tmp(NumericCompoundAssign.op.old(ldloca target, ldc.i4 1)) =
        // `tmp = target++`. The new StLoc replaces the store at pos; the old
        // store (with its now-null-Right binary and the inline-store StLoc) is
        // destroyed.
        block.SetChild(pos, std::make_unique<StLoc>(tmpVar, std::move(nca)));
    } else {
        // The operator-call case builds a UserDefinedCompoundAssign from the
        // operator Call's resolved method name + declaring type + return stack
        // type (this port models a method by its resolved name + declaring
        // type, like Call -- no IMethod), with a fresh LdcI4(1) value (the
        // post-increment's implicit `1` operand), in the EvaluatesToOldValue
        // (post-inc/dec) mode. Capture the method metadata before SetChild
        // destroys the store (and the call): a string copy + a shared_ptr
        // copy + a StackType. The target LdLoca is already detached (owned
        // by `target`), so no raw pointer into the call is read after the
        // SetChild (the precondition-before-mutation discipline).
        std::string methodName = operatorCall->MethodName;
        TypeSystem::ITypePtr methodDeclaringType = operatorCall->DeclaringType;
        StackType methodReturnType = operatorCall->ReturnType;
        auto uca = std::make_unique<UserDefinedCompoundAssign>(
            std::move(methodName), std::move(methodDeclaringType), methodReturnType,
            CompoundEvalMode::EvaluatesToOldValue, std::move(target), targetKind,
            std::make_unique<LdcI4>(1));
        block.SetChild(pos, std::make_unique<StLoc>(tmpVar, std::move(uca)));
    }
    return true;
}

// ---------------------------------------------------------------------------
// TransformPostIncDecOperator (D133): the non-inline-store local/StLoc
// post-increment/decrement fold. The C# comment:
//   stloc tmp(ldloc target)
//   stloc target(binary.op(ldloc tmp, ldc.i4 1))
// -->
//   stloc tmp(compound.op.old(ldloca target, ldc.i4 1))
// This pattern occurs with legacy csc for static fields, and with Roslyn for most
// post-increments (the WithInlineStore expression form is Roslyn-era). When tmp
// is dead (single-def, load-count 0), the StLoc is replaced with the compound
// assign directly (a statement-level `target++`). The operator-call
// (op_Increment/op_Decrement) case (D136, building a UserDefinedCompoundAssign
// from the operator Call) is wired into the same recognition + fold. The
// StObj/Call compound-store cases (need InferType / IsSameMember / IMethod) are
// deferred.
// ---------------------------------------------------------------------------
bool TransformAssignment::TransformPostIncDecOperator(
    Block& block, int pos, StatementTransformContext& context) {
    // inst = block.Instructions[pos] as StLoc -- the tmp store whose Value is the
    // load of target (stloc tmp(ldloc target)).
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size())
        return false;
    auto* inst = dynamic_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (!inst || !inst->Variable) return false;
    // store = block.Instructions[pos+1] -- the compound store (stloc target(binary.op(ldloc tmp, 1))).
    const std::size_t nextIdx = static_cast<std::size_t>(pos) + 1;
    if (nextIdx >= block.Instructions.size()) return false;
    ILInstruction* store = block.Instructions[nextIdx].get();
    if (!store) return false;
    ILVariablePtr tmpVar = inst->Variable;

    // IsCompoundStore: the store is a stloc V(...) whose Variable is a Local/
    // Parameter; targetType = V.Type, value = the stored value (a non-owning view).
    TypeSystem::ITypePtr targetType;
    ILInstruction* value = nullptr;
    if (!IsCompoundStore(store, targetType, value)) return false;

    // CheckImplicitTruncation: the inst.Value (ldloc target) must not be
    // implicitly truncated for the store's target type. For a non-small-integer
    // target type (the common int case), this returns ValuePreserved immediately.
    // For a small-integer target, the conservative else-branch returns
    // ValueChanged (no InferType), so the fold is rejected.
    const auto truncation =
        CheckImplicitTruncation(inst->Value.get(), targetType.get(), false);
    if (truncation == ImplicitTruncationResult::ValueChanged) return false;
    if (truncation == ImplicitTruncationResult::ValueChangedDueToSignMismatch) {
        // The sign-mismatch case is only fixable when the store is a StObj whose
        // Type equals targetType (the C# swaps stObj.Type). For the StLoc case the
        // store is not a StObj, so the truncation cannot be fixed -- bail.
        return false;
    }

    // IsMatchingCompoundLoad: the load (inst.Value = ldloc target) and the store
    // (stloc target) access the same variable, so the compound-assign target is a
    // fresh LdLoca(target); the finalizeMatch collapses split-fragment variables
    // via RecombineVariables (a no-op for the common same-variable case).
    // forbiddenVariable = tmp rejects a match that moves the store over a use of
    // tmp. The previousInstruction is only consulted by the (deferred) LdObj/StObj
    // case, so nullptr is faithful here.
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Address;
    CompoundFinalizeMatch finalizeMatch;
    if (!IsMatchingCompoundLoad(inst->Value.get(), store, target, targetKind,
                                finalizeMatch, inst->Variable.get()))
        return false;

    // UnwrapSmallIntegerConv: peel the compiler's conv truncation to a small
    // integer that a compound assign to a small-integer local carries.
    Conv* conv = nullptr;
    ILInstruction* unwrapped = UnwrapSmallIntegerConv(value, conv);
    auto* binary = dynamic_cast<BinaryNumericInstruction*>(unwrapped);
    // Set in the operator-call (op_Increment/op_Decrement) branch; nullptr in
    // the binary branch. The two are disjoint (a Call is never a
    // BinaryNumericInstruction), so exactly one is set on the success path.
    Call* operatorCall = nullptr;
    if (binary != nullptr) {
        // Only Add/Sub (the ++ / -- operators) are valid post-inc/dec.
        if (!(binary->Operator == BinaryNumericOperator::Add ||
              binary->Operator == BinaryNumericOperator::Sub))
            return false;
        // binary.Left must be ldloc tmp (the temp capturing the old value of target).
        auto* bleft = dynamic_cast<LdLoc*>(binary->Left.get());
        if (!bleft || bleft->Variable.get() != tmpVar.get()) return false;
        // The PointerType target case (PointerArithmeticOffset.Detect) is deferred;
        // the D129 validator rejects pointer types conservatively, so a pointer
        // target would fail ValidateCompoundAssign below anyway.
        if (!IsLdcOne(binary->Right.get())) return false;
        // When a small-integer conv was unwrapped, fix a sign mismatch between the
        // store type and the conv's target by flipping the store type's sign
        // (SwapSign), so ValidateCompoundAssign's conv-match gate sees the corrected
        // type. Same as the WithInlineStore case.
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
    } else if ((operatorCall = dynamic_cast<Call*>(value)) != nullptr) {
        // The operator-call (op_Increment/op_Decrement) case: the C# checks
        // `value is Call operatorCall && operatorCall.Method.IsOperator &&
        // operatorCall.Arguments.Count == 1` then `Arguments[0].MatchLdLoc(tmpVar)`
        // (the operator's single argument is the tmp load) and
        // `IsIncrementOrDecrement(Method, settings)` (which ALSO accepts the
        // op_CheckedIncrement / op_CheckedDecrement variants gated on the
        // CheckedOperators setting, unlike the inline-store cases which use the
        // bare name). IsIncrementOrDecrement does the IsOperator + IsStatic gate
        // internally; the outer IsOperator is redundant but faithful. The
        // `Debug.Assert(truncation == ValuePreserved)` holds trivially: the
        // ValueChanged / ValueChangedDueToSignMismatch cases already returned
        // false above, so the operator-call branch is reached only with
        // ValuePreserved (a user-defined operator returns the same type).
        if (!operatorCall->IsOperator || operatorCall->Arguments.size() != 1)
            return false;
        auto* arg0 = dynamic_cast<LdLoc*>(operatorCall->Arguments[0].get());
        if (!arg0 || arg0->Variable.get() != tmpVar.get()) return false;
        if (!UserDefinedCompoundAssign::IsIncrementOrDecrement(operatorCall,
                                                                  &context.Base.Settings))
            return false;
        if (operatorCall->IsLifted) return false;  // TODO: lifted user-defined operators
    } else {
        // Any other value shape is not a post-inc/dec the fold handles.
        return false;
    }

    context.Base.StepOnce(binary != nullptr ? "TransformPostIncDecOperator (builtin)"
                                            : "TransformPostIncDecOperator (user-defined)");
    ILFunction* fn = FunctionOf(inst);
    if (finalizeMatch && fn)
        finalizeMatch(*fn);

    if (binary != nullptr) {
        // Detach the binary's right operand (the constant 1) before the store is
        // destroyed by RemoveInstructionAt (no GC -- a raw pointer into the store
        // would dangle).
        auto rhs = std::move(binary->Right);
        // Build the NumericCompoundAssign from the binary's fields + the fresh
        // LdLoca target + the detached constant + the (possibly sign-swapped)
        // store type, in the EvaluatesToOldValue (post-inc/dec) mode.
        auto nca = std::make_unique<NumericCompoundAssign>(
            binary->Operator, binary->CheckForOverflow, binary->Sign,
            binary->LeftInputType, binary->RightInputType, binary->ResultStackType,
            binary->IsLifted, targetType, CompoundEvalMode::EvaluatesToOldValue,
            std::move(target), targetKind, std::move(rhs));
        // inst.Value = nca (replace the ldloc target with the compound assign).
        inst->SetChild(0, std::move(nca));
    } else {
        // The operator-call case builds a UserDefinedCompoundAssign from the
        // operator Call's resolved method name + declaring type + return stack
        // type (this port models a method by its resolved name + declaring
        // type, like Call -- no IMethod), with a fresh LdcI4(1) value (the
        // post-increment's implicit `1` operand), in the EvaluatesToOldValue
        // (post-inc/dec) mode. Capture the method metadata before
        // RemoveInstructionAt destroys the store (and the call): a string copy
        // + a shared_ptr copy + a StackType. The target LdLoca is already
        // detached (owned by `target`); `inst->SetChild(0, ...)` only destroys
        // inst->Value (the ldloc target), not the store, so the call is still
        // valid at the capture (the precondition-before-mutation discipline).
        std::string methodName = operatorCall->MethodName;
        TypeSystem::ITypePtr methodDeclaringType = operatorCall->DeclaringType;
        StackType methodReturnType = operatorCall->ReturnType;
        auto uca = std::make_unique<UserDefinedCompoundAssign>(
            std::move(methodName), std::move(methodDeclaringType), methodReturnType,
            CompoundEvalMode::EvaluatesToOldValue, std::move(target), targetKind,
            std::make_unique<LdcI4>(1));
        // inst.Value = uca (replace the ldloc target with the compound assign).
        inst->SetChild(0, std::move(uca));
    }
    // Remove the store at pos+1 (the stloc target(binary.op(ldloc tmp, 1)) or
    // the stloc target(call op_Increment(ldloc tmp))). RemoveInstructionAt
    // renumbers the remaining instructions' ChildIndex.
    block.RemoveInstructionAt(nextIdx);
    // Recompute variable usage after the removal: the C# InstructionCollection
    // ref-counting cascades Disconnected() through the removed store to the
    // `ldloc tmp` inside it, decrementing tmp.LoadCount. This port has no
    // ref-counting, so the stored counts are stale; a fresh ComputeVariableUsage
    // (the D64 recompute fixpoint precedent) gives the correct LoadCount for the
    // dead-tmp check below.
    if (fn) ComputeVariableUsage(*fn);
    // If tmp is dead (single-def, load-count 0), the StLoc was a
    // statement-level post-increment: replace it with the compound assign
    // directly (a bare `target++`). This matches the C# `inst.ReplaceWith(inst.Value)`.
    if (tmpVar->IsSingleDefinition() && tmpVar->LoadCount == 0) {
        auto val = inst->TakeChild(0);  // detach the NCA/UCA (inst->Value)
        inst->ReplaceWith(std::move(val));  // replace the StLoc with the compound assign
    }
    return true;
}

// ---------------------------------------------------------------------------
// TransformPreIncDecOperatorWithInlineStore (D134): the local/StLoc pre-
// increment/decrement fold (the inline-store expression form). The C# comment:
//   stloc outer(stloc inner(binary.op(ldloc target, ldc.i4 1)))
// -->
//   stloc outer(compound.op.new(ldloca target, ldc.i4 1))
// = `outer = ++target` (the C# `EvaluatesToNewValue` compound assign). The outer
// store captures the new value of target (the inner stloc target's stored value is
// the pre-increment result); the fold collapses the inner stloc into the compound
// assign directly. The shape is a double IsCompoundStore (the outer store's Value is
// another StLoc -- the inline-store expression form). This is a Roslyn-era codegen
// pattern (the legacy csc emits the statement form); the operator-call
// (op_Increment/op_Decrement) case (D136, building a UserDefinedCompoundAssign
// from the operator Call) is wired into the same recognition + fold. The
// StObj/Call compound-store cases (need InferType / IsSameMember / IMethod) are
// deferred.
// ---------------------------------------------------------------------------
bool TransformAssignment::TransformPreIncDecOperatorWithInlineStore(
    Block& block, int pos, StatementTransformContext& context) {
    if (pos < 0 || static_cast<std::size_t>(pos) >= block.Instructions.size())
        return false;
    ILInstruction* store = block.Instructions[static_cast<std::size_t>(pos)].get();
    if (!store) return false;
    // IsCompoundStore on the outer store: a `stloc outer(...)` whose Variable is
    // a Local/Parameter; targetType1 = outer.Type, value1 = the stored value (a
    // non-owning view of the StLoc's Value child -- the inner store).
    TypeSystem::ITypePtr targetType1;
    ILInstruction* value1 = nullptr;
    if (!IsCompoundStore(store, targetType1, value1)) return false;
    // IsCompoundStore on value1: the inner store is a `stloc target(...)` whose
    // Variable is a Local/Parameter; targetType2 = target.Type, value2 = the
    // stored value (the binary, possibly wrapped in a small-integer conv).
    TypeSystem::ITypePtr targetType2;
    ILInstruction* value2 = nullptr;
    if (!IsCompoundStore(value1, targetType2, value2)) return false;
    // The two store types must match (the C# `targetType1 != targetType2` is
    // reference equality; this port uses structural equality via IType::Equals,
    // the faithful equivalent since the C# type system deduplicates IType
    // instances -- the port does not). A null type on either side fails the match.
    if (!targetType1 || !targetType2 || !targetType1->Equals(*targetType2))
        return false;
    auto targetType = targetType1;
    auto* stloc_outer = dynamic_cast<StLoc*>(store);
    auto* stloc_inner = dynamic_cast<StLoc*>(value1);
    // UnwrapSmallIntegerConv: peel the compiler's conv truncation to a small
    // integer that a compound assign to a small-integer local/field carries.
    Conv* conv = nullptr;
    ILInstruction* unwrapped = UnwrapSmallIntegerConv(value2, conv);
    auto* binary = dynamic_cast<BinaryNumericInstruction*>(unwrapped);
    LdLoc* ldloc = nullptr;
    // Set in the operator-call (op_Increment/op_Decrement) branch; nullptr in
    // the binary branch. The two are disjoint (a Call is never a
    // BinaryNumericInstruction), so exactly one is set on the success path.
    Call* operatorCall = nullptr;
    if (binary != nullptr && IsLdcOne(binary->Right.get())) {
        // Only Add/Sub (the ++ / -- operators) are valid pre-inc/dec.
        if (!(binary->Operator == BinaryNumericOperator::Add ||
              binary->Operator == BinaryNumericOperator::Sub))
            return false;
        // When a small-integer conv was unwrapped, fix a sign mismatch between
        // the store type and the conv's target by flipping the store type's
        // sign (the C# `SwapSign`), so ValidateCompoundAssign's conv-match
        // gate sees the corrected type. Same as the PostIncDec cases.
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
        // binary.Left is the ldloc of the target (the variable being
        // incremented). Unlike the PostIncDec WithInlineStore case where
        // binary.Left is the inline-store StLoc, here binary.Left is a bare LdLoc
        // (the inner stloc target is the inline store, and its Value is the
        // binary whose Left reads the target).
        ldloc = dynamic_cast<LdLoc*>(binary->Left.get());
    } else if ((operatorCall = dynamic_cast<Call*>(value2)) != nullptr) {
        // The operator-call (op_Increment/op_Decrement) case: the C# checks
        // `value2 is Call operatorCall && operatorCall.Method.IsOperator &&
        // operatorCall.Arguments.Count == 1` then the BARE method name (NOT
        // IsIncrementOrDecrement, which also accepts op_CheckedIncrement /
        // op_CheckedDecrement -- the inline-store cases in C# use the bare
        // name only). This port's Call carries IsOperator (set by the IL
        // reader from the op_* name); IsLifted defaults false (this port has
        // no resolver / ILiftedOperator). `ldloc` is the operator call's
        // single argument (the LdLoc of the target being incremented).
        if (!operatorCall->IsOperator || operatorCall->Arguments.size() != 1)
            return false;
        const auto name = ShortMethodName(operatorCall->MethodName);
        if (name != "op_Increment" && name != "op_Decrement") return false;
        if (operatorCall->IsLifted) return false;  // TODO: lifted user-defined operators
        ldloc = dynamic_cast<LdLoc*>(operatorCall->Arguments[0].get());
    } else {
        // Any other value2 shape is not a pre-inc/dec the fold handles.
        return false;
    }
    if (stloc_outer == nullptr || stloc_inner == nullptr || ldloc == nullptr)
        return false;
    if (!(stloc_outer->Variable &&
          (stloc_outer->Variable->Kind == VariableKind::Local ||
           stloc_outer->Variable->Kind == VariableKind::StackSlot)))
        return false;
    // IsMatchingCompoundLoad: the load (ldloc = binary.Left) and the inner store
    // (stloc_inner) access the same variable (target), so the compound-assign
    // target is a fresh LdLoca(target); the finalizeMatch collapses split-
    // fragment variables via RecombineVariables (a no-op for the common
    // same-variable case). The C# does not pass a forbiddenVariable here.
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Address;
    CompoundFinalizeMatch finalizeMatch;
    if (!IsMatchingCompoundLoad(ldloc, stloc_inner, target, targetKind,
                                finalizeMatch, nullptr))
        return false;
    // The old value (stloc_outer.Value = stloc_inner) must not be implicitly
    // truncated for the outer variable's type. For a non-small-integer outer
    // type (the common Int32 case), CheckImplicitTruncation returns ValuePreserved
    // immediately (the outer guard). For a small-integer outer, the conservative
    // else-branch (no InferType) returns ValueChanged, rejecting the fold -- a
    // faithfulness gap, not a bug.
    if (IsImplicitTruncation(stloc_outer->Value.get(), stloc_outer->Variable->Type.get(), false))
        return false;

    context.Base.StepOnce("TransformPreIncDecOperatorWithInlineStore");
    if (finalizeMatch) {
        if (ILFunction* fn = FunctionOf(store))
            finalizeMatch(*fn);
    }

    // The result StLoc carries the outer variable (the pre-increment
    // expression's result). Capture it before SetChild destroys the store.
    ILVariablePtr outerVar = stloc_outer->Variable;
    if (binary != nullptr) {
        // Detach the binary's right operand (the constant 1) before the outer
        // store is destroyed by SetChild (no GC -- a raw pointer into the
        // binary would dangle). The binary is inside stloc_inner, inside
        // stloc_outer, inside block.Instructions[pos]; SetChild(pos, ...)
        // destroys all of them.
        auto rhs = std::move(binary->Right);
        // Build the NumericCompoundAssign from the binary's fields + the fresh
        // LdLoca target + the detached constant + the (possibly sign-swapped)
        // store type, in the EvaluatesToNewValue (pre-inc/dec) mode.
        auto nca = std::make_unique<NumericCompoundAssign>(
            binary->Operator, binary->CheckForOverflow, binary->Sign,
            binary->LeftInputType, binary->RightInputType, binary->ResultStackType,
            binary->IsLifted, targetType, CompoundEvalMode::EvaluatesToNewValue,
            std::move(target), targetKind, std::move(rhs));
        block.SetChild(pos, std::make_unique<StLoc>(outerVar, std::move(nca)));
    } else {
        // The operator-call case builds a UserDefinedCompoundAssign from the
        // operator Call's resolved method name + declaring type + return stack
        // type (this port models a method by its resolved name + declaring
        // type, like Call -- no IMethod), with a fresh LdcI4(1) value (the
        // pre-increment's implicit `1` operand), in the EvaluatesToNewValue
        // (pre-inc/dec) mode. Capture the method metadata before SetChild
        // destroys the store (and the call): a string copy + a shared_ptr
        // copy + a StackType. The target LdLoca is already detached (owned
        // by `target`), so no raw pointer into the call is read after the
        // SetChild (the precondition-before-mutation discipline).
        std::string methodName = operatorCall->MethodName;
        TypeSystem::ITypePtr methodDeclaringType = operatorCall->DeclaringType;
        StackType methodReturnType = operatorCall->ReturnType;
        auto uca = std::make_unique<UserDefinedCompoundAssign>(
            std::move(methodName), std::move(methodDeclaringType), methodReturnType,
            CompoundEvalMode::EvaluatesToNewValue, std::move(target), targetKind,
            std::make_unique<LdcI4>(1));
        block.SetChild(pos, std::make_unique<StLoc>(outerVar, std::move(uca)));
    }
    return true;
}

} // namespace ILSpy::Decompiler::IL
