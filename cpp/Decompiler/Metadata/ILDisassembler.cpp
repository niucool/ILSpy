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

#include "Decompiler/Metadata/ILDisassembler.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::Metadata {

namespace {
// Read a little-endian uint32 at pos (bounds-checked against size).
bool ReadU32(const std::uint8_t* base, std::size_t size, std::size_t pos, std::uint32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::uint32_t>(base[pos]) |
         (static_cast<std::uint32_t>(base[pos + 1]) << 8) |
         (static_cast<std::uint32_t>(base[pos + 2]) << 16) |
         (static_cast<std::uint32_t>(base[pos + 3]) << 24);
    return true;
}
} // namespace

ILDisassembly DisassembleIL(Util::Span<const std::uint8_t> il) {
    ILDisassembly result;
    const auto* base = il.data();
    std::size_t size = il.size();
    std::size_t pos = 0;
    while (pos < size) {
        std::size_t start = pos;
        ILOpCode op = DecodeOpCode(base, size, pos);
        OperandType ot = GetOperandType(op);
        std::uint32_t operandSize = 0;

        if (ot == OperandType::Switch) {
            // switch = uint32 n, then n int32 branch targets.
            std::uint32_t n = 0;
            if (!ReadU32(base, size, pos, n)) { result.WalkedClean = false; return result; }
            pos += 4;
            // Guard against a bogus n that would overflow; cap at remaining bytes.
            std::size_t targetsBytes = static_cast<std::size_t>(n) * 4;
            if (n > 0x3FFFFFFFu || pos + targetsBytes > size) {
                result.WalkedClean = false;
                return result;
            }
            pos += targetsBytes;
            operandSize = 4 + static_cast<std::uint32_t>(targetsBytes);
        } else {
            std::uint32_t fixed = OperandFixedSize(ot);
            if (pos + fixed > size) { result.WalkedClean = false; return result; }
            pos += fixed;
            operandSize = fixed;
        }

        ILInstruction ins;
        ins.Offset = static_cast<std::uint32_t>(start);
        ins.OpCode = op;
        ins.Length = static_cast<std::uint32_t>(pos - start);
        ins.OperandSize = operandSize;
        result.Instructions.push_back(ins);
    }
    result.WalkedClean = (pos == size);
    return result;
}

} // namespace ILSpy::Decompiler::Metadata
