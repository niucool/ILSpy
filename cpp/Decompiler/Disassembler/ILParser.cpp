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

// Port of ICSharpCode.Disassembler/ILParser.cs -- see ILParser.hpp.

#include "Decompiler/Disassembler/ILParser.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/OperandType.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <limits>
#include <stdexcept>

namespace ILSpy::Decompiler::Disassembler {

namespace {

// The little-endian primitive reads, bounds-checked: the C# BlobReader reads
// throw when the stream is exhausted; the guarded callers never hit that, so
// the port's reads simply return 0 past the end where the C# caller was
// already guarding (and DecodeIndex throws explicitly instead).
std::uint8_t ReadU8(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
	if (pos >= size) return 0;
	return base[pos++];
}

std::int8_t ReadI8(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
	return static_cast<std::int8_t>(ReadU8(base, size, pos));
}

std::uint16_t ReadU16(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
	std::uint16_t lo = ReadU8(base, size, pos);
	std::uint16_t hi = ReadU8(base, size, pos);
	return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint32_t ReadU32(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
	std::uint32_t b0 = ReadU8(base, size, pos);
	std::uint32_t b1 = ReadU8(base, size, pos);
	std::uint32_t b2 = ReadU8(base, size, pos);
	std::uint32_t b3 = ReadU8(base, size, pos);
	return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

std::int32_t ReadI32(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
	return static_cast<std::int32_t>(ReadU32(base, size, pos));
}

// The C# `unchecked(relOffset + blob.Offset)` wraparound: int32 addition of
// the signed delta and the post-operand offset.
int WrapAdd(std::int32_t relOffset, std::size_t pos) {
	return static_cast<int>(static_cast<std::uint32_t>(relOffset)
		+ static_cast<std::uint32_t>(pos));
}

}  // namespace

// ---------------------------------------------------------------------------
// SkipOperand (ILParser.cs lines 78-103).
// ---------------------------------------------------------------------------
void SkipOperand(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode)
{
	auto opType = Metadata::GetOperandType(opCode);
	int operandSize;
	if (opType == Metadata::OperandType::Switch) {
		std::uint32_t n = (size - pos) >= 4
			? ReadU32(base, size, pos)
			: std::numeric_limits<std::uint32_t>::max();
		if (n < static_cast<std::uint32_t>(
			    std::numeric_limits<int>::max() / 4)) {
			operandSize = static_cast<int>(n * 4);
		} else {
			operandSize = std::numeric_limits<int>::max();
		}
	} else {
		operandSize = static_cast<int>(Metadata::OperandFixedSize(opType));
	}
	if (static_cast<std::uint32_t>(operandSize) <= size - pos) {
		pos += static_cast<std::size_t>(operandSize);
	} else {
		// ignore missing/partial operand at end of body
		pos = size;
	}
}

// ---------------------------------------------------------------------------
// DecodeBranchTarget (ILParser.cs lines 105-121).
// ---------------------------------------------------------------------------
int DecodeBranchTarget(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode)
{
	int opSize = Metadata::GetBranchOperandSize(opCode);
	if (opSize <= static_cast<int>(size - pos)) {
		std::int32_t relOffset =
			opSize == 4 ? ReadI32(base, size, pos) : ReadI8(base, size, pos);
		return WrapAdd(relOffset, pos);
	} else {
		return std::numeric_limits<int>::min();
	}
}

// ---------------------------------------------------------------------------
// DecodeSwitchTargets (ILParser.cs lines 123-149).
// ---------------------------------------------------------------------------
std::vector<int> DecodeSwitchTargets(const std::uint8_t* base, std::size_t size,
	std::size_t& pos)
{
	if (size - pos < 4) {
		pos += size - pos;
		return {};
	}
	std::uint32_t numTargets = ReadU32(base, size, pos);
	bool numTargetOverflow = false;
	if (numTargets > (size - pos) / 4) {
		numTargets = static_cast<std::uint32_t>((size - pos) / 4);
		numTargetOverflow = true;
	}
	std::vector<int> targets(numTargets);
	int offset = static_cast<int>(pos + 4 * targets.size());
	for (std::size_t i = 0; i < targets.size(); i++) {
		// The C# `unchecked(blob.ReadInt32() + offset)` wraparound.
		targets[i] = static_cast<int>(static_cast<std::uint32_t>(ReadI32(base, size, pos))
			+ static_cast<std::uint32_t>(offset));
	}
	if (numTargetOverflow) {
		pos += size - pos;
	}
	return targets;
}

// ---------------------------------------------------------------------------
// DecodeUserString (ILParser.cs lines 151-153).
// ---------------------------------------------------------------------------
std::string DecodeUserString(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	const Metadata::MetadataFile& module)
{
	return module.GetUserString(ReadU32(base, size, pos));
}

// ---------------------------------------------------------------------------
// DecodeIndex (ILParser.cs lines 155-167).
// ---------------------------------------------------------------------------
int DecodeIndex(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Metadata::ILOpCode opCode)
{
	switch (Metadata::GetOperandType(opCode)) {
		case Metadata::OperandType::ShortVariable:
			if (pos >= size) throw std::out_of_range("variable index truncated");
			return ReadU8(base, size, pos);
		case Metadata::OperandType::Variable:
			if (pos + 2 > size) throw std::out_of_range("variable index truncated");
			return ReadU16(base, size, pos);
		default:
			throw std::invalid_argument("opcode not supported");
	}
}

// ---------------------------------------------------------------------------
// IsReturn (ILParser.cs lines 169-173).
// ---------------------------------------------------------------------------
bool IsReturn(Metadata::ILOpCode opCode)
{
	return opCode == Metadata::ILOpCode::Ret || opCode == Metadata::ILOpCode::Endfilter
		|| opCode == Metadata::ILOpCode::Endfinally;
}

// ---------------------------------------------------------------------------
// GetHeaderSize (ILParser.cs lines 162-175).
// ---------------------------------------------------------------------------
int GetHeaderSize(const std::uint8_t* base, std::size_t size)
{
	if (size < 1) throw std::out_of_range("method body header truncated");
	std::uint8_t header = base[0];
	if ((header & 3) == 3) {
		// fat
		if (size < 2) throw std::out_of_range("method body header truncated");
		std::uint16_t largeHeader =
			static_cast<std::uint16_t>((base[1] << 8) | header);
		return static_cast<std::uint8_t>(largeHeader >> 12) * 4;
	} else {
		// tiny
		return 1;
	}
}

// ---------------------------------------------------------------------------
// SetBranchTargets (ILParser.cs lines 177-201).
// ---------------------------------------------------------------------------
void SetBranchTargets(const std::uint8_t* base, std::size_t size, std::size_t& pos,
	Util::BitSet& branchTargets)
{
	while (pos < size) {
		auto opCode = Metadata::DecodeOpCode(base, size, pos);
		if (opCode == Metadata::ILOpCode::Switch) {
			for (auto target : DecodeSwitchTargets(base, size, pos)) {
				if (target >= 0 && target < static_cast<int>(size))
					branchTargets.Set(target);
			}
		} else if (Metadata::IsBranch(opCode)) {
			int target = DecodeBranchTarget(base, size, pos, opCode);
			if (target >= 0 && target < static_cast<int>(size))
				branchTargets.Set(target);
		} else {
			SkipOperand(base, size, pos, opCode);
		}
	}
}

}  // namespace ILSpy::Decompiler::Disassembler
