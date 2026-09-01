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

// Port of ICSharpCode.Decompiler/Disassembler/ILParser.cs: the opcode-level
// helpers MethodBodyDisassembler consumes -- the method-body header size, the
// branch/switch target decoders, the operand skipper, the variable-index
// decoders, and the SetBranchTargets pre-pass that marks the offsets
// instructions branch to. DecodeOpCode lives in Metadata/ILOpCodes.hpp (the
// shared table port) and the fixed OperandSize switch in
// Metadata/OperandType.hpp (OperandFixedSize); this file reuses both.
//
// The C# methods are extensions over `ref BlobReader` (SRM). The port carries
// the reader as the (base, size, pos) cursor triple -- pos mutates in place,
// matching the Metadata::DecodeOpCode convention -- so `blob.RemainingBytes`
// is `size - pos` and `blob.Length` is `size`.

#pragma once

#include "Decompiler/Metadata/ILOpCodes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {
class BitSet;
}

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

// ILParser.SkipOperand: advances the cursor past `opCode`'s operand. A switch
// operand skips 4 + 4n bytes; an operand truncated by the end of the stream
// skips to the end (the C# "ignore missing/partial operand at end of body").
void SkipOperand(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode);

// ILParser.DecodeBranchTarget: reads the signed branch delta and returns
// `delta + pos` (the position after the operand). A truncated operand yields
// INT32_MIN (the C# int.MinValue) and leaves the cursor in place.
int DecodeBranchTarget(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode);

// ILParser.DecodeSwitchTargets: reads the switch operand -- a count followed
// by that many signed deltas, each relative to the position after the whole
// operand. A stream too short for the count yields an empty list and consumes
// everything; a count larger than the remaining bytes clamps to the available
// dwords and then skips to the end (the C# numTargetOverflow path).
std::vector<int> DecodeSwitchTargets(const std::uint8_t* base, std::size_t size,
	std::size_t& pos);

// ILParser.DecodeUserString: reads the 0x70xxxxxx user-string token and
// resolves it through the module's #US heap (an invalid token degrades to the
// empty string, matching MetadataFile::GetUserString's graceful behaviour).
std::string DecodeUserString(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	const Metadata::MetadataFile& module);

// ILParser.DecodeIndex: the variable-index operand (a byte for the
// ShortVariable opcodes, a word for the Variable ones). Any other operand
// kind throws std::invalid_argument (the C# ArgumentException).
int DecodeIndex(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode);

// ILParser.IsReturn: ret, endfilter, endfinally -- the opcodes that terminate
// a basic block for the blank-line placement.
bool IsReturn(Metadata::ILOpCode opCode);

// ILParser.GetHeaderSize: 1 for a tiny header ((b & 3) == 2), Size * 4 for a
// fat one ((b & 3) == 3, Size = bits 12-15 of the 16-bit LE header word).
// A stream too short for the header bytes throws std::out_of_range, mirroring
// the C# BlobReader overflow that the (future) Disassemble caller catches.
int GetHeaderSize(const std::uint8_t* base, std::size_t size);

// ILParser.SetBranchTargets: walks the whole stream from `pos`, marking in
// SetBranchTargets every offset a branch or switch targets, and leaves the
// cursor at the stream end. Out-of-range targets are skipped.
void SetBranchTargets(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Util::BitSet& branchTargets);

}  // namespace ILSpy::Decompiler::Disassembler
