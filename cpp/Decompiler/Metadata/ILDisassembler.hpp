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

// A minimal IL disassembler: walks a method body's IL bytes, decoding each
// opcode (via ILOpCodes) and sizing its operand (via OperandType, with Switch
// read as n + n*4 dwords). Produces a flat instruction list. This is the core
// of Phase 6's ReflectionDisassembler and a prerequisite for Phase 3's ILReader
// (which decodes the same opcodes into the ILAst). Operands are not resolved to
// tokens here -- that comes with the full disassembler and the IL reader.

#pragma once

#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Util/Span.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

struct ILInstruction {
    std::uint32_t Offset;      // byte offset within the method body
    ILOpCode OpCode;
    std::uint32_t Length;      // total bytes: opcode(s) + operand
    std::uint32_t OperandSize;// operand bytes (0 for None)
};

struct ILDisassembly {
    std::vector<ILInstruction> Instructions;
    bool WalkedClean;         // true iff the walk consumed exactly CodeSize bytes
};

// Disassemble a method body's IL bytes. Walks opcodes until the stream ends;
// sets WalkedClean iff the walk consumed exactly `il.size()` bytes (no over- or
// under-run, no partial trailing operand). Never throws: a malformed body
// yields WalkedClean=false with the instructions decoded up to the fault.
ILDisassembly DisassembleIL(Util::Span<const std::uint8_t> il);

} // namespace ILSpy::Decompiler::Metadata
