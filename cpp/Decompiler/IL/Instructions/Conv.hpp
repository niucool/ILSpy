// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

// Conv: a numeric conversion. UnaryInstruction. Faithful to Conv.cs: carries
// Kind (ConversionKind), InputType (StackType), InputSign (Sign), TargetType
// (PrimitiveType), and CheckForOverflow. The Kind is computed in the constructor
// via GetConversionKind (Ecma-335 Table 8). Result is GetStackType(TargetType);
// a Conv with CheckForOverflow may throw.
//
// The C# ILReader emits one Conv per conv.* opcode with the (targetType,
// checkForOverflow, inputSign) the opcode implies; the InputType is the
// argument's ResultType, and InputSign is the opcode's sign when the conversion
// needs one (overflow checking, or int->float), else None -- matching the C#
// Conv constructor's `needsSign` rule.

#pragma once

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

using ILSpy::Decompiler::TypeSystem::Sign;

class Conv : public UnaryInstruction {
public:
    PrimitiveType TargetType = PrimitiveType::None;
    bool CheckForOverflow = false;
    StackType InputType = StackType::Unknown;
    Sign InputSign = Sign::None;
    ConversionKind Kind = ConversionKind::Invalid;
    // Faithful to the C# Conv.IsLifted (the ILiftableInstruction impl): a lifted
    // conv operates on a boxed Nullable<T> (Argument.ResultType == O, not
    // InputType) and produces a boxed Nullable<T> result (ResultType == O). Set
    // by the NullableLifting conv.nop.lifted case; the IL reader's conv.* opcodes
    // are never lifted. Default false keeps every existing Conv non-lifted.
    bool IsLifted = false;

    // Non-lifted form: inputType is the argument's ResultType. Mirrors the C#
    // Conv(argument, targetType, checkForOverflow, inputSign) constructor;
    // InputSign is the passed sign when the conversion needs one (overflow
    // checking, or int->float), else None; Kind is derived.
    Conv(std::unique_ptr<ILInstruction> argument, PrimitiveType targetType,
         bool checkForOverflow, Sign inputSign)
        : UnaryInstruction(OpCode::Conv, std::move(argument)),
          TargetType(targetType), CheckForOverflow(checkForOverflow) {
        InputType = Argument ? Argument->ResultType() : StackType::Unknown;
        bool needsSign = checkForOverflow || (!IsFloatType(InputType) && IsFloatType(targetType));
        InputSign = needsSign ? inputSign : Sign::None;
        Kind = GetConversionKind(TargetType, InputType, InputSign);
    }

    // Lifted form: inputType is passed explicitly (the argument's ResultType is O
    // when lifted, not InputType). Mirrors the C# Conv(argument, inputType,
    // inputSign, targetType, checkForOverflow, isLifted) constructor used by the
    // NullableLifting conv.nop.lifted case. The 2nd arg (StackType) vs the
    // non-lifted constructor's 2nd (PrimitiveType) keeps the overloads disjoint
    // for typed enum arguments. ResultType is O (a boxed Nullable<T>);
    // UnderlyingResultType is GetStackType(TargetType).
    Conv(std::unique_ptr<ILInstruction> argument, StackType inputType,
         Sign inputSign, PrimitiveType targetType, bool checkForOverflow,
         bool isLifted = false)
        : UnaryInstruction(OpCode::Conv, std::move(argument)),
          TargetType(targetType), CheckForOverflow(checkForOverflow),
          InputType(inputType), IsLifted(isLifted) {
        bool needsSign = checkForOverflow || (!IsFloatType(InputType) && IsFloatType(targetType));
        InputSign = needsSign ? inputSign : Sign::None;
        Kind = GetConversionKind(TargetType, InputType, InputSign);
    }

    StackType ResultType() const override {
        return IsLifted ? StackType::O : GetStackType(TargetType);
    }

    // The underlying (non-lifted) result stack type. Faithful to the C#
    // Conv.UnderlyingResultType (ILiftableInstruction): GetStackType(TargetType).
    StackType UnderlyingResultType() const { return GetStackType(TargetType); }

    InstructionFlags DirectFlags() const override {
        return CheckForOverflow ? InstructionFlags::MayThrow : InstructionFlags::None;
    }

    void WriteTo(std::string& out) const override {
        out += "conv.";
        switch (TargetType) {
            case PrimitiveType::I1: out += "i1"; break;
            case PrimitiveType::I2: out += "i2"; break;
            case PrimitiveType::I4: out += "i4"; break;
            case PrimitiveType::I8: out += "i8"; break;
            case PrimitiveType::U1: out += "u1"; break;
            case PrimitiveType::U2: out += "u2"; break;
            case PrimitiveType::U4: out += "u4"; break;
            case PrimitiveType::U8: out += "u8"; break;
            case PrimitiveType::I: out += "i"; break;
            case PrimitiveType::U: out += "u"; break;
            case PrimitiveType::R4: out += "r4"; break;
            case PrimitiveType::R8: out += "r8"; break;
            case PrimitiveType::R: out += "r.un"; break;
            default: out += "?"; break;
        }
        if (CheckForOverflow) out += ".ovf";
        if (InputSign == Sign::Unsigned) out += ".unsigned";
        else if (InputSign == Sign::Signed) out += ".signed";
        if (IsLifted) out += ".lifted";
        out += ' ';
        out += StackTypeName(InputType);
        out += "->";
        out += StackTypeName(IsLifted ? StackType::O : GetStackType(TargetType));
        out += ' ';
        switch (Kind) {
            case ConversionKind::SignExtend: out += "<sign extend>"; break;
            case ConversionKind::ZeroExtend: out += "<zero extend>"; break;
            case ConversionKind::Invalid: out += "<invalid>"; break;
            default: break;
        }
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }

private:
    static const char* StackTypeName(StackType s) {
        switch (s) {
            case StackType::I4: return "I4";
            case StackType::I: return "I";
            case StackType::I8: return "I8";
            case StackType::F4: return "F4";
            case StackType::F8: return "F8";
            case StackType::O: return "O";
            case StackType::Ref: return "Ref";
            case StackType::Unknown: return "Unknown";
            case StackType::Void: return "Void";
            default: return "?";
        }
    }
};

} // namespace ILSpy::Decompiler::IL
