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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/PointerArithmeticOffset.hpp"

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <cstdint>
#include <optional>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `inst.UnwrapConv(kind)` (ILInstruction, Instructions.cs): recursively
// descend Conv chains of the requested ConversionKind. Mirrors the file-local
// UnwrapConv copies in the Transforms (the "copied next to its consumer"
// convention).
const ILInstruction* UnwrapConv(const ILInstruction* inst, ConversionKind kind) {
    while (inst && inst->Op == OpCode::Conv) {
        auto* conv = static_cast<const Conv*>(inst);
        if (conv->Kind != kind) break;
        inst = conv->Argument.get();
    }
    return inst;
}

// The C# `MatchLdcI(out long)` over LdcI4/LdcI8 (PatternMatching.cs line 8604
// is the I4 arm; the C# MatchLdcI(out long) unwraps sign/zero-extending convs
// around the constant, but this port's reader never wraps ldc constants in conv,
// so the plain forms cover every consumer). Mirrors the file-local MatchLdcI
// copies in SwitchAnalysis.cpp / ExpressionTransforms.cpp.
bool MatchLdcI(const ILInstruction* inst, std::int64_t& val) {
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

} // namespace

std::optional<int> PointerArithmeticOffset::ComputeSizeOf(const TypeSystem::IType* type) {
    // The C# `type.GetEnumUnderlyingType().GetDefinition()?.KnownTypeCode`:
    // unwrap the enum's underlying type, then read the definition's KnownTypeCode
    // (null for every non-definition type -- the port's KnownType wrappers carry
    // no definition, so the code answers None there too, matching the C#
    // FindType(KnownTypeCode) results that are real definitions in the full type
    // system and wrappers over MinimalCorlib here... the dual-shape fallback the
    // TypeUtils.GetSize fix (gnhf 105) established applies).
    if (!type) return std::nullopt;
    const TypeSystem::IType* underlying = TypeSystem::GetEnumUnderlyingType(type);
    TypeSystem::KnownTypeCode code = TypeSystem::KnownTypeCode::None;
    const TypeSystem::ITypeDefinition* def =
        underlying != nullptr ? underlying->GetDefinition() : nullptr;
    if (def != nullptr)
        code = def->KnownTypeCode();
    if (code == TypeSystem::KnownTypeCode::None) {
        if (const auto* k = dynamic_cast<const TypeSystem::KnownType*>(underlying))
            code = k->Code();
        if (code == TypeSystem::KnownTypeCode::None) return std::nullopt;
    }
    switch (code) {
        case TypeSystem::KnownTypeCode::Boolean:
        case TypeSystem::KnownTypeCode::SByte:
        case TypeSystem::KnownTypeCode::Byte:
            return 1;
        case TypeSystem::KnownTypeCode::Char:
        case TypeSystem::KnownTypeCode::Int16:
        case TypeSystem::KnownTypeCode::UInt16:
            return 2;
        case TypeSystem::KnownTypeCode::Int32:
        case TypeSystem::KnownTypeCode::UInt32:
        case TypeSystem::KnownTypeCode::Single:
            return 4;
        case TypeSystem::KnownTypeCode::Int64:
        case TypeSystem::KnownTypeCode::UInt64:
        case TypeSystem::KnownTypeCode::Double:
            return 8;
        case TypeSystem::KnownTypeCode::Decimal:
            return 16;
        default:
            return std::nullopt;
    }
}

PointerArithmeticOffset::DetectOutcome PointerArithmeticOffset::Detect(
    const ILInstruction* byteOffsetInst, const TypeSystem::IType* pointerElementType,
    bool checkForOverflow, bool unwrapZeroExtension)
{
    DetectOutcome outcome;
    if (pointerElementType == nullptr) return outcome;
    // `byteOffsetInst is Conv conv && conv.InputType == StackType.I8 &&
    // conv.ResultType == StackType.I`: unwrap the wide-to-native conv the
    // reader puts around the byte count.
    if (byteOffsetInst != nullptr && byteOffsetInst->Op == OpCode::Conv) {
        auto* conv = static_cast<const Conv*>(byteOffsetInst);
        if (conv->InputType == StackType::I8 && conv->ResultType() == StackType::I)
            byteOffsetInst = conv->Argument.get();
    }
    std::optional<int> elementSize = ComputeSizeOf(pointerElementType);
    if (elementSize == std::optional<int>(1)) {
        outcome.Inst = byteOffsetInst;
        return outcome;
    }
    // `byteOffsetInst is BinaryNumericInstruction mul && mul.Operator == Mul`:
    // the C# branch enters only for a Mul (a non-Mul binary numeric falls to
    // the SizeOf/LdcI4 checks below, both statically false for that node).
    if (byteOffsetInst != nullptr &&
        byteOffsetInst->Op == OpCode::BinaryNumericInstruction) {
        auto* mul = static_cast<const BinaryNumericInstruction*>(byteOffsetInst);
        if (mul->Operator == BinaryNumericOperator::Mul) {
            if (mul->IsLifted) return outcome;
            if (mul->CheckForOverflow != checkForOverflow) return outcome;
            // `(elementSize > 0 && mul.Right.MatchLdcI(elementSize.Value))`
            // `|| (mul.Right.UnwrapConv(SignExtend) is SizeOf sizeOf &&
            //     NormalizeTypeVisitor.TypeErasure.EquivalentTypes(sizeOf.Type,
            //                                                     pointerElementType))`.
            bool sizeMatches = false;
            if (elementSize.has_value() && *elementSize > 0) {
                std::int64_t rhsValue = 0;
                if (MatchLdcI(mul->Right.get(), rhsValue))
                    sizeMatches = rhsValue == *elementSize;
            }
            if (!sizeMatches && mul->Right != nullptr) {
                const ILInstruction* rhs =
                    UnwrapConv(mul->Right.get(), ConversionKind::SignExtend);
                if (rhs != nullptr && rhs->Op == OpCode::SizeOf) {
                    auto* sizeOf = static_cast<const SizeOf*>(rhs);
                    if (sizeOf->Type != nullptr) {
                        auto* element =
                            const_cast<TypeSystem::IType*>(pointerElementType);
                        sizeMatches =
                            TypeSystem::NormalizeTypeVisitor::TypeErasure()
                                .EquivalentTypes(
                                    *const_cast<TypeSystem::IType*>(
                                        sizeOf->Type.get()),
                                    *element);
                    }
                }
            }
            if (sizeMatches) {
                const ILInstruction* countOffsetInst = mul->Left.get();
                if (unwrapZeroExtension)
                    countOffsetInst =
                        UnwrapConv(countOffsetInst, ConversionKind::ZeroExtend);
                outcome.Inst = countOffsetInst;
                return outcome;
            }
            // A Mul that failed the size match: the C# branch A ends in
            // `return null`.
            return outcome;
        }
    }
    if (byteOffsetInst != nullptr) {
        // `byteOffsetInst.UnwrapConv(SignExtend) is SizeOf sizeOf &&
        // sizeOf.Type.Equals(pointerElementType)`: a bare sizeof of the element
        // type is a one-element offset (the compiler constant-folded the
        // `* 1`).
        const ILInstruction* rhs =
            UnwrapConv(byteOffsetInst, ConversionKind::SignExtend);
        if (rhs != nullptr && rhs->Op == OpCode::SizeOf) {
            auto* sizeOf = static_cast<const SizeOf*>(rhs);
            if (sizeOf->Type != nullptr &&
                sizeOf->Type->Equals(*pointerElementType)) {
                auto constant = std::make_unique<LdcI4>(1);
                constant->SetILRange(*byteOffsetInst);
                outcome.Inst = constant.get();
                outcome.Owned = std::move(constant);
                return outcome;
            }
        }
        // `byteOffsetInst.MatchLdcI(out long val)`: a constant byte offset the
        // compiler constant-folded the multiplication for.
        std::int64_t val = 0;
        if (MatchLdcI(byteOffsetInst, val)) {
            if (elementSize.has_value() && *elementSize > 0 &&
                val % *elementSize == 0 && val > 0) {
                val /= *elementSize;
                if (val <= INT32_MAX) {
                    auto constant =
                        std::make_unique<LdcI4>(static_cast<std::int32_t>(val));
                    constant->SetILRange(*byteOffsetInst);
                    outcome.Inst = constant.get();
                    outcome.Owned = std::move(constant);
                    return outcome;
                }
            }
        }
    }
    return outcome;
}

bool PointerArithmeticOffset::IsFixedVariable(const ILInstruction* inst) {
    if (inst == nullptr) return false;
    switch (inst->Op) {
        case OpCode::LdLoca:
            // The C# `ldloca.Variable.CaptureScope == null` -- the port's IL
            // reader never sets a capture scope (the closure machinery is not
            // ported yet), so every local is uncaptured (the
            // TranslatedExpression.cpp IsFixedVariableInstruction convention).
            return true;
        case OpCode::LdFlda:
            if (const auto* ldflda = dynamic_cast<const LdFlda*>(inst))
                return ldflda->Target != nullptr &&
                       IsFixedVariable(ldflda->Target.get());
            return false;
        default:
            return inst->ResultType() == StackType::I;
    }
}

} // namespace ILSpy::Decompiler::IL
