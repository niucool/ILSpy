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

// Port of ICSharpCode.Decompiler/Metadata/OperandType.cs. The operand kind of an
// IL opcode, used to size and read the operand that follows the opcode byte(s).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::Metadata {

enum class OperandType : std::uint8_t {
    BrTarget,
    Field,
    I,
    I8,
    Method,
    None,
    R = 7,
    Sig = 9,
    String,
    Switch,
    Tok,
    Type,
    Variable,
    ShortBrTarget,
    ShortI,
    ShortR,
    ShortVariable,
    // Sentinel for "undefined opcode" (matches the C# (OperandType)255 guard).
    Undefined = 255,
};

// Fixed operand size in bytes for a given OperandType. Switch is special-cased
// by the caller (n+1 dwords); ShortI is 1, I is 4, I8/R is 8, Variable is 2,
// ShortVariable/ShortBrTarget is 1, None is 0. Mirrors ILParser.OperandSize.
inline std::uint32_t OperandFixedSize(OperandType t) {
    switch (t) {
        case OperandType::I8:
        case OperandType::R: return 8;
        case OperandType::BrTarget:
        case OperandType::Field:
        case OperandType::Method:
        case OperandType::I:
        case OperandType::Sig:
        case OperandType::String:
        case OperandType::Tok:
        case OperandType::Type:
        case OperandType::ShortR: return 4;
        case OperandType::Switch: return 4; // minimum; caller reads n*4
        case OperandType::Variable: return 2;
        case OperandType::ShortVariable:
        case OperandType::ShortBrTarget:
        case OperandType::ShortI: return 1;
        default: return 0;
    }
}

} // namespace ILSpy::Decompiler::Metadata
