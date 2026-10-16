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

// LdcDecimal: push a System.Decimal constant. Leaf (SimpleInstruction), no
// flags, result O. Port of the C# LdcDecimal in the generated Instructions.cs.
//
// The C# node carries a `decimal Value`; this port models System.Decimal's
// internal layout faithfully as DecimalValue { lo, mid, hi (the 96-bit
// unsigned mantissa), isNegative, scale (0..28) } -- the same components the
// 5-arg `new decimal(lo, mid, hi, isNegative, scale)` constructor takes -- so
// the value round-trips without a native decimal type. Factory helpers build
// the value from the int/uint/long/ulong single-arg constructors and the
// Decimal.One/Zero/MinusOne named fields.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::IL {

// Faithful model of System.Decimal's internal layout: a 96-bit unsigned
// mantissa (lo/mid/hi), a sign, and a scale (number of decimal digits to the
// right of the point, 0..28). Matches the fields the C#
// `new decimal(int lo, int mid, int hi, bool isNegative, byte scale)`
// constructor exposes, so the value produced by any decimal constructor or
// named constant field is representable.
struct DecimalValue {
	std::uint32_t lo = 0;
	std::uint32_t mid = 0;
	std::uint32_t hi = 0;
	bool isNegative = false;
	std::uint8_t scale = 0;

	bool IsZero() const { return lo == 0 && mid == 0 && hi == 0; }

	friend bool operator==(const DecimalValue& a, const DecimalValue& b) {
		return a.lo == b.lo && a.mid == b.mid && a.hi == b.hi &&
			a.isNegative == b.isNegative && a.scale == b.scale;
	}
	friend bool operator!=(const DecimalValue& a, const DecimalValue& b) { return !(a == b); }

	// The C# decimal literal string (without the trailing `m` suffix the seed
	// adds). Mirrors System.Decimal.ToString(): the 96-bit mantissa formatted as
	// decimal digits with the point inserted `scale` places from the right,
	// preserving trailing zeros (the scale), with a leading `-` when negative
	// (zero is always unsigned).
	std::string ToString() const {
		std::string digits = MantissaToDigits();
		if (scale == 0) {
			if (IsZero()) return "0";
			return (isNegative ? "-" : "") + digits;
		}
		// Insert the decimal point `scale` digits from the right, padding with
		// leading zeros if the mantissa has fewer digits than the scale.
		if (digits.size() <= scale) {
			digits = std::string(static_cast<std::size_t>(scale) - digits.size() + 1, '0') + digits;
		}
		digits.insert(digits.end() - static_cast<std::ptrdiff_t>(scale), '.');
		if (IsZero()) return digits;  // zero is unsigned (e.g. "0.00")
		return (isNegative ? "-" : "") + digits;
	}

	// Format the 96-bit mantissa as a decimal digit string via repeated divmod
	// by 1e9 (the largest power of ten whose remainder fits in 32 bits).
	std::string MantissaToDigits() const {
		if (IsZero()) return "0";
		std::uint32_t words[3] = {lo, mid, hi};
		// Collect 9-digit groups, least significant first.
		std::string groups[4];  // 96 bits / 30 bits per group => at most 4 groups
		int groupCount = 0;
		while (words[0] != 0 || words[1] != 0 || words[2] != 0) {
			std::uint64_t rem = 0;
			for (int i = 2; i >= 0; --i) {
				std::uint64_t cur = (rem << 32) | words[i];
				words[i] = static_cast<std::uint32_t>(cur / 1000000000ull);
				rem = cur % 1000000000ull;
			}
			groups[groupCount++] = std::to_string(static_cast<std::uint32_t>(rem));
		}
		// The most significant group needs no padding; the rest are zero-padded
		// to 9 digits.
		std::string result = groups[groupCount - 1];
		for (int i = groupCount - 2; i >= 0; --i) {
			result += std::string(9 - groups[i].size(), '0') + groups[i];
		}
		return result;
	}

	// Factory helpers mirroring the C# decimal single-arg constructors and the
	// Decimal.One/Zero/MinusOne named-constant fields.
	static DecimalValue FromInt32(std::int32_t v) {
		DecimalValue d;
		if (v < 0) {
			// -(int64_t)v avoids the INT_MIN overflow; the magnitude fits in uint32.
			d.lo = static_cast<std::uint32_t>(-(static_cast<std::int64_t>(v)));
			d.isNegative = true;
		} else {
			d.lo = static_cast<std::uint32_t>(v);
		}
		return d;
	}
	static DecimalValue FromUInt32(std::uint32_t v) {
		DecimalValue d;
		d.lo = v;
		return d;
	}
	static DecimalValue FromInt64(std::int64_t v) {
		DecimalValue d;
		std::uint64_t mag = v < 0 ? (0ull - static_cast<std::uint64_t>(v))
		                          : static_cast<std::uint64_t>(v);
		d.lo = static_cast<std::uint32_t>(mag & 0xFFFFFFFFull);
		d.mid = static_cast<std::uint32_t>(mag >> 32);
		d.isNegative = v < 0;
		return d;
	}
	static DecimalValue FromUInt64(std::uint64_t v) {
		DecimalValue d;
		d.lo = static_cast<std::uint32_t>(v & 0xFFFFFFFFull);
		d.mid = static_cast<std::uint32_t>(v >> 32);
		return d;
	}
	// The 5-arg `new decimal(lo, mid, hi, isNegative, scale)` constructor.
	static DecimalValue FromBits(std::uint32_t lo, std::uint32_t mid, std::uint32_t hi,
	                             bool isNegative, std::uint8_t scale) {
		DecimalValue d;
		d.lo = lo;
		d.mid = mid;
		d.hi = hi;
		d.isNegative = isNegative;
		d.scale = scale;
		return d;
	}
	static DecimalValue One() { return FromInt32(1); }
	static DecimalValue Zero() { return {}; }
	static DecimalValue MinusOne() { return FromInt32(-1); }
};

class LdcDecimal : public SimpleInstruction {
public:
	DecimalValue Value;
	explicit LdcDecimal(DecimalValue v = DecimalValue{}) : SimpleInstruction(OpCode::LdcDecimal), Value(v) {}
	StackType ResultType() const override { return StackType::O; }
	void WriteTo(std::string& out) const override {
		out += "ldc.decimal(";
		out += Value.ToString();
		out += ')';
	}
};

} // namespace ILSpy::Decompiler::IL
