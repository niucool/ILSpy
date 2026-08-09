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

// LongSet: an immutable set of 64-bit integers represented as a sorted list of
// non-empty, non-overlapping, non-touching intervals, faithful to the
// `LongSet` struct in ICSharpCode.Decompiler/Util/LongSet.cs. The interval form
// is required because the switch-family transforms (SwitchAnalysis) compute
// the *complement* of a finite set (e.g. `new LongSet(val).Invert()`), which is
// infinite and cannot be represented by std::set<int64_t>. LongSet is the
// foundation for SwitchDetection / SwitchOnString / SwitchOnNullable.

#pragma once

#include "Decompiler/Util/Interval.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {

class LongSet {
public:
    LongSet() = default;  // empty

    // A set containing a single value.
    explicit LongSet(long long value)
        : intervals_{LongInterval::Inclusive(value, value)} {}

    // A set containing the values of one interval (empty if the interval is empty).
    explicit LongSet(LongInterval interval) {
        if (!interval.IsEmpty())
            intervals_.push_back(interval);
    }

    // A set built from arbitrary (possibly overlapping/empty/unsorted) intervals;
    // filters empties, sorts by Start, and merges overlapping/touching intervals
    // into the normalized form. Matches the C# `LongSet(IEnumerable<LongInterval>)`.
    explicit LongSet(std::vector<LongInterval> intervals) {
        std::vector<LongInterval> nonEmpty;
        nonEmpty.reserve(intervals.size());
        for (auto& i : intervals)
            if (!i.IsEmpty()) nonEmpty.push_back(i);
        std::sort(nonEmpty.begin(), nonEmpty.end(),
                  [](const LongInterval& a, const LongInterval& b) { return a.Start < b.Start; });
        intervals_ = MergeOverlapping(nonEmpty);
    }

    static LongSet Empty() { return LongSet(); }
    static LongSet Universe() {
        return FromNormalized({LongInterval(LongInterval::Inclusive(std::numeric_limits<long long>::min(),
                                                                    std::numeric_limits<long long>::max()))});
    }

    bool IsEmpty() const { return intervals_.empty(); }

    const std::vector<LongInterval>& Intervals() const { return intervals_; }

    // Number of values. For the universe (and any set whose count overflows
    // uint64), returns uint64 max as a sentinel, matching the C#.
    std::uint64_t Count() const {
        std::uint64_t count = 0;
        for (const auto& interval : intervals_)
            count += static_cast<std::uint64_t>(interval.End) - static_cast<std::uint64_t>(interval.Start);
        if (count == 0 && !intervals_.empty())
            return std::numeric_limits<std::uint64_t>::max();
        return count;
    }

    bool Overlaps(const LongSet& other) const {
        // Two-pointer intersection; true on the first non-empty intersection.
        std::size_t a = 0, b = 0;
        while (a < intervals_.size() && b < other.intervals_.size()) {
            const LongInterval& ia = intervals_[a];
            const LongInterval& ib = other.intervals_[b];
            LongInterval inter = ia.Intersect(ib);
            if (!inter.IsEmpty())
                return true;
            if (ia.InclusiveEnd() < ib.InclusiveEnd())
                ++a;
            else
                ++b;
        }
        return false;
    }

    LongSet IntersectWith(const LongSet& other) const {
        std::vector<LongInterval> out;
        std::size_t a = 0, b = 0;
        while (a < intervals_.size() && b < other.intervals_.size()) {
            const LongInterval& ia = intervals_[a];
            const LongInterval& ib = other.intervals_[b];
            LongInterval inter = ia.Intersect(ib);
            if (!inter.IsEmpty())
                out.push_back(inter);
            if (ia.InclusiveEnd() < ib.InclusiveEnd())
                ++a;
            else
                ++b;
        }
        return FromNormalized(std::move(out));
    }

    LongSet UnionWith(const LongSet& other) const {
        // Merge the two sorted interval lists by Start, then MergeOverlapping.
        std::vector<LongInterval> merged;
        merged.reserve(intervals_.size() + other.intervals_.size());
        std::size_t a = 0, b = 0;
        while (a < intervals_.size() && b < other.intervals_.size()) {
            if (intervals_[a].Start <= other.intervals_[b].Start)
                merged.push_back(intervals_[a++]);
            else
                merged.push_back(other.intervals_[b++]);
        }
        while (a < intervals_.size()) merged.push_back(intervals_[a++]);
        while (b < other.intervals_.size()) merged.push_back(other.intervals_[b++]);
        return FromNormalized(MergeOverlapping(merged));
    }

    LongSet ExceptWith(const LongSet& other) const {
        return IntersectWith(other.Invert());
    }

    LongSet Invert() const {
        if (IsEmpty())
            return Universe();
        std::vector<LongInterval> out;
        out.reserve(intervals_.size() + 1);
        long long prevEnd = std::numeric_limits<long long>::min();  // previous exclusive end
        for (const auto& interval : intervals_) {
            if (interval.Start > prevEnd)
                out.emplace_back(prevEnd, interval.Start);  // [prevEnd, interval.Start)
            prevEnd = interval.End;
        }
        if (prevEnd != std::numeric_limits<long long>::min())
            out.emplace_back(prevEnd, std::numeric_limits<long long>::min());  // [prevEnd, Max]
        return FromNormalized(std::move(out));
    }

    // Returns a new set with val added to each element. Handles signed overflow
    // by splitting the wrapped interval in two.
    LongSet AddOffset(long long val) const {
        if (val == 0)
            return *this;
        std::vector<LongInterval> out;
        out.reserve(intervals_.size() + 1);
        for (const auto& element : intervals_) {
            long long newStart = static_cast<long long>(
                static_cast<std::uint64_t>(element.Start) + static_cast<std::uint64_t>(val));
            long long newInclusiveEnd = static_cast<long long>(
                static_cast<std::uint64_t>(element.InclusiveEnd()) + static_cast<std::uint64_t>(val));
            if (newStart <= newInclusiveEnd) {
                out.push_back(LongInterval::Inclusive(newStart, newInclusiveEnd));
            }
            else {
                // interval split by integer overflow
                out.push_back(LongInterval::Inclusive(newStart, std::numeric_limits<long long>::max()));
                out.push_back(LongInterval::Inclusive(std::numeric_limits<long long>::min(), newInclusiveEnd));
            }
        }
        std::sort(out.begin(), out.end(),
                  [](const LongInterval& a, const LongInterval& b) { return a.Start < b.Start; });
        return FromNormalized(MergeOverlapping(out));
    }

    bool IsSubsetOf(const LongSet& other) const {
        return UnionWith(other).SetEquals(other);
    }
    bool IsSupersetOf(const LongSet& other) const {
        return other.IsSubsetOf(*this);
    }
    bool IsProperSubsetOf(const LongSet& other) const {
        return IsSubsetOf(other) && !SetEquals(other);
    }
    bool IsProperSupersetOf(const LongSet& other) const {
        return IsSupersetOf(other) && !SetEquals(other);
    }

    bool Contains(long long val) const {
        int index = UpperBound(val);
        return index > 0 && intervals_[static_cast<std::size_t>(index) - 1].Contains(val);
    }

    LongInterval ContainingInterval() const {
        if (IsEmpty())
            return LongInterval();
        return LongInterval(intervals_.front().Start, intervals_.back().End);
    }

    bool SetEquals(const LongSet& other) const {
        if (intervals_.size() != other.intervals_.size())
            return false;
        for (std::size_t i = 0; i < intervals_.size(); ++i)
            if (!(intervals_[i] == other.intervals_[i]))
                return false;
        return true;
    }

    // All values, in order. Only safe to fully consume for finite sets; the
    // universe yields an effectively infinite sequence. Callers must ensure
    // finiteness (e.g. the switch seed only enumerates finite case sets).
    std::vector<long long> Values() const {
        std::vector<long long> out;
        for (const auto& i : intervals_) {
            auto r = i.Range();
            out.insert(out.end(), r.begin(), r.end());
        }
        return out;
    }

    std::string ToString() const {
        std::string s;
        bool first = true;
        for (const auto& i : intervals_) {
            if (!first) s += ',';
            s += i.ToString();
            first = false;
        }
        return s;
    }

private:
    std::vector<LongInterval> intervals_;

    // Construct from an already-normalized interval list (no sort/merge).
    static LongSet FromNormalized(std::vector<LongInterval> intervals) {
        LongSet s;
        s.intervals_ = std::move(intervals);
        return s;
    }

    // Given non-empty intervals sorted by Start, merge overlapping or touching
    // intervals into the normalized (non-overlapping, non-touching) form.
    // Matches the C# `MergeOverlapping`. The End == Min sentinel (interval
    // reaching long.MaxValue inclusive) is handled specially: it absorbs all
    // later intervals and prevents the `end` overflow.
    static std::vector<LongInterval> MergeOverlapping(const std::vector<LongInterval>& input) {
        const long long Min = std::numeric_limits<long long>::min();
        std::vector<LongInterval> out;
        long long start = Min, end = Min;
        bool empty = true;
        for (const auto& element : input) {
            if (!(!empty && element.Start <= end)) {
                // flush existing interval
                if (!empty)
                    out.emplace_back(start, end);
                empty = false;
                start = element.Start;
                end = element.End;
            }
            else {
                // element overlaps or touches [start, end), so combine:
                if (element.End == Min) {
                    end = Min;  // element reaches Max inclusive
                }
                else {
                    long long candidate = element.End;
                    // end == Min means the running interval already reaches Max;
                    // keep it (avoid overflow). Otherwise take the max.
                    if (end != Min && candidate > end)
                        end = candidate;
                }
            }
            if (end == Min) {
                // running interval reaches Max inclusive; the rest is contained
                break;
            }
        }
        if (!empty)
            out.emplace_back(start, end);
        return out;
    }

    // Binary search: returns the number of intervals strictly ending at or before
    // val's position, i.e. the index after the interval that might contain val.
    // Relies on the normalized (non-touching) invariant. Matches the C# upper_bound.
    int UpperBound(long long val) const {
        int min = 0;
        int max = static_cast<int>(intervals_.size()) - 1;
        while (max >= min) {
            int m = min + (max - min) / 2;
            const LongInterval& i = intervals_[static_cast<std::size_t>(m)];
            if (val < i.Start) {
                max = m - 1;
                continue;
            }
            if (val > i.End) {
                min = m + 1;
                continue;
            }
            return m + 1;
        }
        return min;
    }

};

} // namespace ILSpy::Decompiler::Util
