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
// (foundation subset) -- the UnwrapSmallIntegerConv helper. See
// TransformAssignment.hpp for the transform this is a foundation for.

#include "Decompiler/IL/Transforms/TransformAssignment.hpp"

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
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

} // namespace ILSpy::Decompiler::IL
