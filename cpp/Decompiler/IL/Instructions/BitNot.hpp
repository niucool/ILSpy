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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// BitNot: the bitwise-NOT instruction (`not` / `not.ovf` on the evaluation
// stack). UnaryInstruction; the result is the operand's stack type. The lifted
// form (built by NullableLiftingTransform.DoLift) operates on a boxed
// Nullable<T> (Argument.ResultType == O) and produces a boxed Nullable<T>
// (ResultType == O); the underlying result type is the non-lifted result type
// passed to the lifted constructor, faithful to the generated Instructions.cs
// + the hand-written UnaryInstruction.cs BitNot (an ILiftableInstruction).

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class BitNot : public UnaryInstruction {
public:
    // Faithful to the C# BitNot.IsLifted (the ILiftableInstruction impl): a
    // lifted BitNot operates on a boxed Nullable<T> argument and produces a
    // boxed Nullable<T> result. Set only by the NullableLifting lift
    // machinery; the IL reader's `not` opcode is never lifted. Default false
    // keeps every existing BitNot non-lifted.
    bool IsLifted = false;
    // The underlying (non-lifted) result stack type. Faithful to the C#
    // BitNot.UnderlyingResultType (ILiftableInstruction): the result type of
    // the non-lifted `not` (the argument's stack type).
    StackType UnderlyingResultType = StackType::I4;

    // Non-lifted form: the underlying result type is the argument's stack type.
    explicit BitNot(std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::BitNot, std::move(argument)) {
        UnderlyingResultType = Argument ? Argument->ResultType() : StackType::I4;
    }

    // Lifted form (the NullableLifting DoLift BitNot case): the argument is a
    // lifted Nullable<T> (ResultType O), so ResultType() returns O; the
    // underlying result type is the original BitNot's result type. Mirrors the
    // C# BitNot(arg, isLifted, stackType) constructor.
    BitNot(std::unique_ptr<ILInstruction> argument, bool isLifted, StackType stackType)
        : UnaryInstruction(OpCode::BitNot, std::move(argument)),
          IsLifted(isLifted), UnderlyingResultType(stackType) {}

    // Faithful to the C# BitNot.ResultType: the argument's result type. For a
    // lifted BitNot the argument is a lifted Nullable<T> (ResultType O), so
    // the result is O; for a non-lifted BitNot it is the operand's stack type.
    StackType ResultType() const override {
        return Argument ? Argument->ResultType() : StackType::I4;
    }

    // A `not` has no direct side effects or throws (the C# DirectFlags is None).
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }

    void WriteTo(std::string& out) const override {
        out += "bitnot";
        if (IsLifted) out += ".lifted";
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
