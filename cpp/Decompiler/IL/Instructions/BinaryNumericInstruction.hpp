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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// BinaryNumericInstruction: a primitive numeric/bitwise operation (add/sub/mul/
// div/rem/and/or/xor/shl/shr). BinaryInstruction; the result stack type is
// ComputeResultType(op, LeftInputType, RightInputType) (Ecma-335 Table 2/5/6/7).
// The Operator + Sign + CheckForOverflow + LeftInputType/RightInputType +
// IsLifted fields follow BinaryNumericInstruction.cs (the faithful model the
// compound-assignment validation NumericCompoundAssign.IsBinaryCompatibleWithType
// consults; this port previously carried only a Signed bool + ResultStackType).

#pragma once

#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

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
    bool CheckForOverflow = false;
    // The sign of an integer operation that depends on the sign (Add/Sub/Mul/
    // Div/Rem/ShiftRight); Sign.None for sign-independent ops (BitAnd/BitOr/
    // BitXor/ShiftLeft). Faithful to the C# BinaryNumericInstruction.Sign.
    TypeSystem::Sign Sign = TypeSystem::Sign::None;
    // The input stack types of the operands. Faithful to the C#
    // BinaryNumericInstruction.LeftInputType/RightInputType: the non-lifted
    // constructors derive them from the operands' ResultType (the operands are
    // valid after the BinaryInstruction base init moves them into Left/Right);
    // the lifted constructor takes them explicitly (the lifted operands have
    // ResultType O, but the input types are the underlying Nullable<T> types).
    StackType LeftInputType = StackType::I4;
    StackType RightInputType = StackType::I4;
    StackType ResultStackType = StackType::I4;
    // Legacy bool view of Sign: true for Sign.None/Sign.Signed, false for
    // Sign.Unsigned (the .un forms). Kept for the ILAst dump's `.un` suffix
    // and the existing NullableLifting DoLiftBinary call site; the faithful
    // model is the Sign enum above. Derived from Sign in every constructor.
    bool Signed = true;
    // Faithful to the C# BinaryNumericInstruction.IsLifted (the
    // ILiftableInstruction impl): a lifted binary operates on boxed Nullable<T>
    // operands (Left/Right.ResultType == O) and produces a boxed Nullable<T>
    // (ResultType == O). Set only by the NullableLifting DoLift machinery; the
    // IL reader's binary opcodes are never lifted. Default false keeps every
    // existing BinaryNumericInstruction non-lifted.
    bool IsLifted = false;

    // ComputeResultType: Ecma-335 Table 2 (Binary Numeric Operations) / Table 5
    // (Integer Operations) / Table 7 (Overflow Arithmetic Operations) / Table 6
    // (Shift). Faithful to the C# BinaryNumericInstruction.ComputeResultType.
    // Same-type operands (or any shift) yield the left input type; mixed Ref
    // operands yield Ref (or I for sub(&,&)); otherwise Unknown.
    static StackType ComputeResultType(BinaryNumericOperator op, StackType left, StackType right) {
        if (left == right || op == BinaryNumericOperator::ShiftLeft ||
            op == BinaryNumericOperator::ShiftRight) {
            // Shift op codes use Table 6 (the result is the left input type).
            return left;
        }
        if (left == StackType::Ref || right == StackType::Ref) {
            if (left == StackType::Ref && right == StackType::Ref) {
                // sub(&, &) = I
                return StackType::I;
            }
            // add/sub with I or I4 and & = &
            return StackType::Ref;
        }
        return StackType::Unknown;
    }

    // Backward-compatible non-lifted form (the pre-reconciliation tests
    // construct BinaryNumericInstruction with an explicit result stack type,
    // defaulting I4). Derives the input types from the operands' ResultType and
    // defaults Sign.None / CheckForOverflow false, matching the C# 5-arg
    // BinaryNumericInstruction(op, left, right, checkForOverflow:false, sign:None)
    // constructor's derivation. The explicit resultStackType overrides the
    // computed one (this port's reader historically passed I4; the new 5-arg
    // faithful constructor below computes it).
    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, StackType resultStackType = StackType::I4)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), ResultStackType(resultStackType) {
        DeriveInputTypes();
        Signed = (Sign != TypeSystem::Sign::Unsigned);
    }

    // Faithful non-lifted constructor mirroring the C#
    // BinaryNumericInstruction(op, left, right, checkForOverflow, sign): derives
    // LeftInputType/RightInputType from the operands' ResultType and computes the
    // result stack type via ComputeResultType. Used by the IL reader's per-opcode
    // binary emission (the Sign + CheckForOverflow are set faithfully per opcode).
    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, bool checkForOverflow, TypeSystem::Sign sign)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), CheckForOverflow(checkForOverflow), Sign(sign) {
        DeriveInputTypes();
        ResultStackType = ComputeResultType(Operator, LeftInputType, RightInputType);
        Signed = (Sign != TypeSystem::Sign::Unsigned);
    }

    // Lifted form (the NullableLifting DoLift BinaryNumericInstruction case):
    // the operands are lifted Nullable<T> values (ResultType O), so ResultType()
    // returns O; the underlying result type is ComputeResultType(op, input types).
    // Mirrors the C# BinaryNumericInstruction(op, left, right, leftInputType,
    // rightInputType, checkForOverflow, sign, isLifted) constructor -- the input
    // types are taken explicitly (the lifted operands' ResultType is O, but the
    // input types are the underlying Nullable<T> types the operator applies to).
    BinaryNumericInstruction(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
                              BinaryNumericOperator op, StackType leftInputType, StackType rightInputType,
                              bool checkForOverflow, TypeSystem::Sign sign, bool isLifted)
        : BinaryInstruction(OpCode::BinaryNumericInstruction, std::move(left), std::move(right)),
          Operator(op), CheckForOverflow(checkForOverflow), Sign(sign),
          LeftInputType(leftInputType), RightInputType(rightInputType),
          ResultStackType(ComputeResultType(op, leftInputType, rightInputType)),
          Signed(sign != TypeSystem::Sign::Unsigned), IsLifted(isLifted) {}

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
        if (CheckForOverflow) out += ".ovf";
        if (!Signed) out += ".un";
        if (IsLifted) out += ".lifted";
        out += '(';
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }

private:
    // Derive LeftInputType/RightInputType from the operands' ResultType after
    // the BinaryInstruction base init moved them into Left/Right (the members
    // are valid post-init). Faithful to the C# 5-arg constructor's
    // `left.ResultType`/`right.ResultType` derivation.
    void DeriveInputTypes() {
        if (Left) LeftInputType = Left->ResultType();
        if (Right) RightInputType = Right->ResultType();
    }
};

} // namespace ILSpy::Decompiler::IL
