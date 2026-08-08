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

    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, StackType resultStackType = StackType::I4)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), ResultStackType(resultStackType) {}

    StackType ResultType() const override { return ResultStackType; }
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
        out += '(';
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
