// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including but not limitation, the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// LdLen: ldlen -- the length of an array. UnaryInstruction; DirectFlags += MayThrow
// (NullReferenceException). Faithful to the C# LdLen: the node carries a StackType
// resultType (I for the raw `ldlen` opcode, which pushes a native int; I4 for the
// synthetic `ldlen.i4`; I8 for `ldlen.i8`). The raw `ldlen` returns native int I, and
// ExpressionTransforms.VisitConv folds `conv.i4(ldlen)` / `conv.i8(ldlen)` into a
// single `LdLen(I4, ..)` / `LdLen(I8, ..)` so the conversion is folded into the load
// (the C# `conv.i4(ldlen array) => ldlen.i4(array)`).

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class LdLen : public UnaryInstruction {
public:
    StackType resultType = StackType::I;

    // Faithful to the C# `LdLen(StackType type, ILInstruction array)`. The raw
    // `ldlen` opcode pushes a native int (StackType::I); VisitConv's
    // conv.iN(ldlen) fold produces the I4/I8 variants.
    explicit LdLen(StackType type, std::unique_ptr<ILInstruction> argument = nullptr)
        : UnaryInstruction(OpCode::LdLen, std::move(argument)), resultType(type) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return resultType; }
    void WriteTo(std::string& out) const override {
        out += "ldlen.";
        out += StackTypeName(resultType);
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }

    // The C# WriteToCore writes `OpCode` then `.` then `resultType`; StackType has
    // no ToString in this port, so this static helper mirrors the C# stack-type
    // names used in the ILAst dump.
    static const char* StackTypeName(StackType s) {
        switch (s) {
            case StackType::I4: return "I4";
            case StackType::I: return "I";
            case StackType::I8: return "I8";
            case StackType::F4: return "F4";
            case StackType::F8: return "F8";
            case StackType::O: return "O";
            case StackType::Ref: return "Ref";
            case StackType::Void: return "Void";
            case StackType::Unknown: return "Unknown";
        }
        return "?";
    }
};

} // namespace ILSpy::Decompiler::IL
