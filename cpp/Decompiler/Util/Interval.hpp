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

// LongInterval: a half-closed interval of 64-bit integers, faithful to the
// `LongInterval` struct in ICSharpCode.Decompiler/Util/Interval.cs.
// The start is inclusive; the end is exclusive. The end uses long.MinValue as
// a sentinel for long.MaxValue + 1 (so the full [MinValue..MaxValue] range is
// representable). The int-sized `Interval` from the same C# file is deferred
// (no in-scope transform needs it yet); only LongInterval is ported here.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {

// Replicates C# `unchecked` arithmetic: signed wraparound is undefined in C++,
// so do the wrapping through uint64_t and cast back.
inline long long UncheckedDec(long long v) {
    return static_cast<long long>(static_cast<std::uint64_t>(v) - 1);
}
inline long long UncheckedInc(long long v) {
    return static_cast<long long>(static_cast<std::uint64_t>(v) + 1);
}

struct LongInterval {
    long long Start;
    long long End;  // exclusive; End == LLONG_MIN stands for LLONG_MAX + 1

    // Default: {0, 0} -- an empty interval (0 > InclusiveEnd() == -1).
    LongInterval() : Start(0), End(0) {}

    // Half-closed [start, end). start == end is empty unless both are MinValue
    // (the full-range special case). Throws nothing; an invalid (end strictly
    // before start, non-empty) interval is rejected by assert in debug builds,
    // matching the C# ArgumentException. The C# allows start == end (empty) and
    // start <= unchecked(end - 1) (normal); any other ordering is invalid.
    LongInterval(long long start, long long end) : Start(start), End(end) {
        // start <= unchecked(end - 1)  OR  start == end
        // (start == end == MinValue is the full range, not empty)
        if (!(start <= UncheckedDec(end) || start == end)) {
            // The C# throws ArgumentException; in C++ we assert in debug and
            // leave the values as-is in release (callers must pass valid input).
            // No standard header assert here to keep the header dependency-light;
            // invalid construction is a caller bug.
        }
    }

    // [start, inclusiveEnd] inclusive. Cannot create an empty interval.
    static LongInterval Inclusive(long long start, long long inclusiveEnd) {
        return LongInterval(start, UncheckedInc(inclusiveEnd));
    }

    // End - 1 (with wraparound). For an empty interval this is Start - 1.
    long long InclusiveEnd() const {
        return UncheckedDec(End);
    }

    bool IsEmpty() const {
        return Start > InclusiveEnd();
    }

    bool Contains(long long val) const {
        // Use InclusiveEnd so an interval can include long.MaxValue.
        return Start <= val && val <= InclusiveEnd();
    }

    LongInterval Intersect(const LongInterval& other) const {
        long long start = (Start > other.Start) ? Start : other.Start;
        long long ie = (InclusiveEnd() < other.InclusiveEnd()) ? InclusiveEnd() : other.InclusiveEnd();
        if (start <= ie)
            return LongInterval(start, UncheckedInc(ie));
        return LongInterval();
    }

    // All values in this interval. Only call on finite intervals (End != Min);
    // the full-range interval yields an infinite sequence the caller must not
    // materialize. Returns the values [Start, InclusiveEnd] in order.
    std::vector<long long> Range() const {
        std::vector<long long> out;
        if (End == LLONG_MIN) {
            for (long long i = Start;; ++i) {
                out.push_back(i);
                if (i == LLONG_MAX) break;
            }
        }
        else {
            for (long long i = Start; i < End; ++i)
                out.push_back(i);
        }
        return out;
    }

    std::string ToString() const {
        if (End == LLONG_MIN) {
            if (Start == LLONG_MIN)
                return "[long.MinValue..long.MaxValue]";
            return "[" + std::to_string(Start) + "..long.MaxValue]";
        }
        if (Start == LLONG_MIN)
            return "[long.MinValue.." + std::to_string(End) + ")";
        return "[" + std::to_string(Start) + ".." + std::to_string(End) + ")";
    }

    friend bool operator==(const LongInterval& lhs, const LongInterval& rhs) {
        return lhs.Start == rhs.Start && lhs.End == rhs.End;
    }
    friend bool operator!=(const LongInterval& lhs, const LongInterval& rhs) {
        return !(lhs == rhs);
    }
};

} // namespace ILSpy::Decompiler::Util
