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

// BinaryNumericInstruction: a primitive numeric/bitwise operation (add/sub/mul/
// div/rem/and/or/xor/shl/shr). BinaryInstruction; result is the operands' stack
// type. The Operator + Sign + CheckForOverflow fields follow
// BinaryNumericInstruction.cs (minimal port).

#pragma once

#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

enum class BinaryNumericOperator : std::uint8_t {
    None,
    Add,
    Sub,
    Mul,
    Div,
    Rem,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
};

class BinaryNumericInstruction : public BinaryInstruction {
public:
    BinaryNumericOperator Operator = BinaryNumericOperator::None;
    bool Signed = true;       // false for .un forms
    bool CheckForOverflow = false;
    StackType ResultStackType = StackType::I4;
    // Faithful to the C# BinaryNumericInstruction.IsLifted (the
    // ILiftableInstruction impl): a lifted binary operates on boxed Nullable<T>
    // operands (Left/Right.ResultType == O) and produces a boxed Nullable<T>
    // (ResultType == O). Set only by the NullableLifting DoLift machinery; the
    // IL reader's binary opcodes are never lifted. Default false keeps every
    // existing BinaryNumericInstruction non-lifted.
    bool IsLifted = false;

    // Non-lifted form: the result stack type is the operands' stack type.
    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, StackType resultStackType = StackType::I4)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), ResultStackType(resultStackType) {}

    // Lifted form (the NullableLifting DoLift BinaryNumericInstruction case):
    // the operands are lifted Nullable<T> values (ResultType O), so ResultType()
    // returns O; the underlying result type is the original binary's result
    // type. Mirrors the C# BinaryNumericInstruction(op, left, right,
    // leftInputType, rightInputType, checkForOverflow, sign, isLifted)
    // constructor; this port does not model LeftInputType/RightInputType/Sign
    // (it carries ResultStackType + Signed), so the lifted constructor takes
    // the underlying result type + the original's Signed/CheckForOverflow.
    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, StackType underlyingResultType,
                              bool checkForOverflow, bool signed_, bool isLifted)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), Signed(signed_), CheckForOverflow(checkForOverflow),
          ResultStackType(underlyingResultType), IsLifted(isLifted) {}

    // Faithful to the C# BinaryNumericInstruction.ResultType: O for a lifted
    // binary (a boxed Nullable<T>); the underlying result type otherwise.
    StackType ResultType() const override {
        return IsLifted ? StackType::O : ResultStackType;
    }
    // The underlying (non-lifted) result stack type. Faithful to the C#
    // BinaryNumericInstruction.UnderlyingResultType (ILiftableInstruction).
    StackType UnderlyingResultType() const { return ResultStackType; }
    void WriteTo(std::string& out) const override {
        out += "binary.";
        switch (Operator) {
            case BinaryNumericOperator::Add: out += "add"; break;
            case BinaryNumericOperator::Sub: out += "sub"; break;
            case BinaryNumericOperator::Mul: out += "mul"; break;
            case BinaryNumericOperator::Div: out += "div"; break;
            case BinaryNumericOperator::Rem: out += "rem"; break;
            case BinaryNumericOperator::BitAnd: out += "and"; break;
            case BinaryNumericOperator::BitOr: out += "or"; break;
            case BinaryNumericOperator::BitXor: out += "xor"; break;
            case BinaryNumericOperator::ShiftLeft: out += "shl"; break;
            case BinaryNumericOperator::ShiftRight: out += "shr"; break;
            default: out += "?"; break;
        }
        if (!Signed) out += ".un";
        if (CheckForOverflow) out += ".ovf";
        if (IsLifted) out += ".lifted";
        out += '(';
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
