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
// THE SOFTWARE IS PROVIDED "AS WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
// LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// BitSet: a fixed-capacity bitset over the non-negative ints [0, capacity),
// faithful to the `BitSet` class in ICSharpCode.Decompiler/Util/BitSet.cs.
// Backing store is an array of 64-bit words. The capacity is rounded up to a
// whole word (at least one), so a few bits past the requested capacity exist
// and stay clear (the C# `BitSet` makes the same allowance and intentionally
// provides no `SetAll()`).
//
// This is the relevance-analysis data structure the nullable-lifting `DoLift` /
// `DoLiftBinary` machinery returns (`bits.All(0, nullableVars.Count)` is the
// "every nullable var contributed to the result" gate that decides whether a
// lifted expression is allowed), and it is the foundation the deferred
// DefiniteAssignment / Dominance / ReachingDefinitions analyses are built on.
//
// C# `ulong` shift counts are masked to 6 bits (& 0x3F) by the runtime, so
// `1UL << index` and `Mask << index` are well-defined for any `index`; in C++
// a shift of >= 64 bits is undefined behaviour, so every shift is masked to
// the low 6 bits explicitly. The C# `Mask >> -endIndex` end mask is replicated
// as `Mask >> ((64 - (endIndex & 63)) & 63)`, which reproduces the C# masking
// (when `endIndex` is a multiple of 64 the shift collapses to 0 and the whole
// word is selected).

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {

namespace detail {

// Number of trailing zero bits in a 64-bit word, or 64 if the word is zero.
// Faithful to `System.Numerics.BitOperations.TrailingZeroCount(ulong)` as
// vendored in ICSharpCode.Decompiler/Util/BitOperations.cs (0 -> 64). Used by
// `NextSetBit`. `std::countr_zero` is C++20, so compiler intrinsics are used.
inline int TrailingZeroCount64(std::uint64_t value) noexcept {
	if (value == 0)
		return 64;
#if defined(_MSC_VER)
	unsigned long index;
	_BitScanForward64(&index, value);
	return static_cast<int>(index);
#else
	return __builtin_ctzll(value);
#endif
}

}  // namespace detail

class BitSet {
public:
	// A bitset of `capacity` bits, all clear. The capacity is rounded up to a
	// whole 64-bit word (at least one), matching the C# constructor
	// `new ulong[Math.Max(1, WordIndex(capacity + 63))]` -- i.e. `max(1, ceil(capacity / 64))`
	// words. A negative or zero capacity yields a single (all-clear) word.
	explicit BitSet(int capacity = 0)
		: words_(static_cast<std::size_t>(
			std::max(1, (std::max(capacity, 0) + kBitsPerWord - 1) >> kLog2BitsPerWord)), 0) {}

	// The bit at `index`. No bounds check beyond a debug assert; the caller is
	// responsible for staying within capacity (the C# likewise Debug.Asserts
	// `index >= 0` and indexes `words[WordIndex(index)]` unchecked).
	bool operator[](int index) const {
		assert(index >= 0);
		assert(WordIndex(index) < static_cast<int>(words_.size()));
		return (words_[WordIndex(index)] & (kOne << (index & kMask63))) != 0;
	}

	// `Any()` -- true iff at least one bit is set.
	bool Any() const noexcept {
		for (auto w : words_)
			if (w != 0)
				return true;
		return false;
	}

	// `All(start, end)` -- true iff every bit in [startIndex, endIndex) is set.
	// Returns true when startIndex >= endIndex (the empty range is vacuously
	// all-set), matching the C#.
	bool All(int startIndex, int endIndex) const {
		assert(startIndex <= endIndex);
		if (startIndex >= endIndex)
			return true;
		int startWordIndex = WordIndex(startIndex);
		int endWordIndex = WordIndex(endIndex - 1);
		std::uint64_t startMask = kMask << (startIndex & kMask63);
		std::uint64_t endMask = kMask >> ((kBitsPerWord - (endIndex & kMask63)) & kMask63);
		if (startWordIndex == endWordIndex) {
			std::uint64_t bits = startMask & endMask;
			return (words_[startWordIndex] & bits) == bits;
		}
		if ((words_[startWordIndex] & startMask) != startMask)
			return false;
		for (int i = startWordIndex + 1; i < endWordIndex; i++) {
			if (words_[i] != kMask)
				return false;
		}
		return (words_[endWordIndex] & endMask) == endMask;
	}

	// Set relation predicates. The C# Debug.Asserts equal word counts; this
	// port treats out-of-range (missing) words as zero so the predicates are
	// safe and behaviour-preserving for the equal-capacity case the callers use.
	bool SetEquals(const BitSet& other) const {
		std::size_t n = std::max(words_.size(), other.words_.size());
		for (std::size_t i = 0; i < n; i++) {
			if (WordAt(i) != other.WordAt(i))
				return false;
		}
		return true;
	}

	bool IsSubsetOf(const BitSet& other) const {
		for (std::size_t i = 0; i < words_.size(); i++) {
			if ((words_[i] & ~other.WordAt(i)) != 0)
				return false;
		}
		return true;
	}

	bool IsSupersetOf(const BitSet& other) const {
		return other.IsSubsetOf(*this);
	}

	bool IsProperSubsetOf(const BitSet& other) const {
		return IsSubsetOf(other) && !SetEquals(other);
	}

	bool IsProperSupersetOf(const BitSet& other) const {
		return IsSupersetOf(other) && !SetEquals(other);
	}

	bool Overlaps(const BitSet& other) const {
		std::size_t n = std::max(words_.size(), other.words_.size());
		for (std::size_t i = 0; i < n; i++) {
			if ((WordAt(i) & other.WordAt(i)) != 0)
				return true;
		}
		return false;
	}

	void UnionWith(const BitSet& other) {
		for (std::size_t i = 0; i < words_.size(); i++) {
			words_[i] |= other.WordAt(i);
		}
	}

	void IntersectWith(const BitSet& other) {
		for (std::size_t i = 0; i < words_.size(); i++) {
			words_[i] &= other.WordAt(i);
		}
	}

	void Set(int index) {
		assert(index >= 0);
		assert(WordIndex(index) < static_cast<int>(words_.size()));
		words_[WordIndex(index)] |= (kOne << (index & kMask63));
	}

	// Sets every bit in [startIndex, endIndex).
	void Set(int startIndex, int endIndex) {
		assert(startIndex <= endIndex);
		if (startIndex >= endIndex)
			return;
		int startWordIndex = WordIndex(startIndex);
		int endWordIndex = WordIndex(endIndex - 1);
		std::uint64_t startMask = kMask << (startIndex & kMask63);
		std::uint64_t endMask = kMask >> ((kBitsPerWord - (endIndex & kMask63)) & kMask63);
		if (startWordIndex == endWordIndex) {
			words_[startWordIndex] |= (startMask & endMask);
		} else {
			words_[startWordIndex] |= startMask;
			for (int i = startWordIndex + 1; i < endWordIndex; i++) {
				words_[i] = kMask;
			}
			words_[endWordIndex] |= endMask;
		}
	}

	void Clear(int index) {
		assert(index >= 0);
		assert(WordIndex(index) < static_cast<int>(words_.size()));
		words_[WordIndex(index)] &= ~(kOne << (index & kMask63));
	}

	// Clears every bit in [startIndex, endIndex).
	void Clear(int startIndex, int endIndex) {
		assert(startIndex <= endIndex);
		if (startIndex >= endIndex)
			return;
		int startWordIndex = WordIndex(startIndex);
		int endWordIndex = WordIndex(endIndex - 1);
		std::uint64_t startMask = kMask << (startIndex & kMask63);
		std::uint64_t endMask = kMask >> ((kBitsPerWord - (endIndex & kMask63)) & kMask63);
		if (startWordIndex == endWordIndex) {
			words_[startWordIndex] &= ~(startMask & endMask);
		} else {
			words_[startWordIndex] &= ~startMask;
			for (int i = startWordIndex + 1; i < endWordIndex; i++) {
				words_[i] = 0;
			}
			words_[endWordIndex] &= ~endMask;
		}
	}

	void ClearAll() noexcept {
		for (auto& w : words_)
			w = 0;
	}

	// Index of the first set bit at or after `startIndex` and before
	// `endIndex`, or -1 if none. Matches the C# `NextSetBit`.
	int NextSetBit(int startIndex, int endIndex) const {
		assert(startIndex <= endIndex);
		if (startIndex >= endIndex)
			return -1;
		int startWordIndex = WordIndex(startIndex);
		int endWordIndex = WordIndex(endIndex - 1);
		std::uint64_t startMask = kMask << (startIndex & kMask63);
		std::uint64_t endMask = kMask >> ((kBitsPerWord - (endIndex & kMask63)) & kMask63);
		std::uint64_t masked;
		if (startWordIndex == endWordIndex) {
			masked = words_[startWordIndex] & startMask & endMask;
			if (masked != 0)
				return startWordIndex * kBitsPerWord + detail::TrailingZeroCount64(masked);
		} else {
			masked = words_[startWordIndex] & startMask;
			if (masked != 0)
				return startWordIndex * kBitsPerWord + detail::TrailingZeroCount64(masked);
			for (int i = startWordIndex + 1; i < endWordIndex; i++) {
				masked = words_[i];
				if (masked != 0)
					return i * kBitsPerWord + detail::TrailingZeroCount64(masked);
			}
			masked = words_[endWordIndex] & endMask;
			if (masked != 0)
				return endWordIndex * kBitsPerWord + detail::TrailingZeroCount64(masked);
		}
		return -1;
	}

	// Every set bit in [startIndex, endIndex), ascending. Matches the C#
	// `SetBits` enumerable as a vector (the C++-idiomatic eager form; the
	// consumers iterate the results, which a vector supports directly).
	std::vector<int> SetBits(int startIndex, int endIndex) const {
		std::vector<int> result;
		int i = startIndex;
		while (true) {
			int next = NextSetBit(i, endIndex);
			if (next == -1)
				break;
			result.push_back(next);
			i = next + 1;
		}
		return result;
	}

	void ReplaceWith(const BitSet& incoming) {
		words_ = incoming.words_;
	}

	BitSet Clone() const {
		BitSet copy;
		copy.words_ = words_;
		return copy;
	}

	// The rounded-up capacity in bits (a whole number of 64-bit words). The C#
	// exposes no public capacity but reads `words.Length * 64` in `ToString`;
	// provided here as a convenience for tests and the analyses.
	int Capacity() const noexcept {
		return static_cast<int>(words_.size() * kBitsPerWord);
	}

	std::string ToString() const {
		std::string b = "{";
		for (int i = 0; i < static_cast<int>(words_.size() * kBitsPerWord); i++) {
			if ((*this)[i]) {
				if (b.size() > 1)
					b += ", ";
				if (b.size() > 500) {
					b += "...";
					break;
				}
				b += std::to_string(i);
			}
		}
		b += "}";
		return b;
	}

private:
	static constexpr int kBitsPerWord = 64;
	static constexpr int kLog2BitsPerWord = 6;
	static constexpr std::uint64_t kMask = ~0ULL;
	static constexpr std::uint64_t kOne = 1ULL;
	static constexpr int kMask63 = 63;

	static constexpr int WordIndex(int bitIndex) noexcept {
		assert(bitIndex >= 0);
		return bitIndex >> kLog2BitsPerWord;
	}

	// `words_[i]`, or 0 if `i` is out of range (defensive; the C# Debug.Asserts
	// equal word counts so this is only a safety net for mismatched capacities).
	std::uint64_t WordAt(std::size_t i) const noexcept {
		return i < words_.size() ? words_[i] : 0;
	}

	std::vector<std::uint64_t> words_;
};

}  // namespace ILSpy::Decompiler::Util
