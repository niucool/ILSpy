// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Util/LongDict.cs: an immutable mapping from
// keys of type long to values of type T, stored as sorted disjoint
// [LongInterval, T] pairs. The state-machine analyses (StateRangeAnalysis)
// use it to resolve each state number to the block that handles it.

#pragma once

#include "Decompiler/Util/LongSet.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Util {

// The C# `struct LongDict<T>` -- a template over the value type (the C#
// generic). The constructor takes (LongSet, T) pairs; where multiple entries
// cover the same key the FIRST entry wins (later entries are intersected with
// the still-available key space).
template <typename T>
class LongDict {
public:
    LongDict() = default;

    // The C# ctor: consume each entry's intervals against the remaining
    // available key space, then sort the pairs by the interval start.
    explicit LongDict(std::vector<std::pair<LongSet, T>> entries) {
        LongSet available = LongSet::Universe();
        std::vector<std::pair<LongInterval, T>> pairs;
        for (auto& [key, value] : entries) {
            // The intersection is materialized into a named local: the
            // range-for over `key.IntersectWith(available).Intervals()`
            // would dangle -- Intervals() returns a reference into the
            // intermediate LongSet, whose lifetime the range-for does not
            // extend.
            LongSet intersection = key.IntersectWith(available);
            for (const LongInterval& interval : intersection.Intervals()) {
                pairs.emplace_back(interval, value);
            }
            available = available.ExceptWith(key);
        }
        std::stable_sort(pairs.begin(), pairs.end(),
                         [](const auto& a, const auto& b) {
                             return a.first.Start < b.first.Start;
                         });
        pairs_.reserve(pairs.size());
        for (auto& p : pairs)
            pairs_.push_back(std::move(p));
    }

    // The C# `bool TryGetValue(long key, out T value)`: the binary search for
    // the interval containing the key (the insertion-position complement
    // finds the preceding interval when there is no exact start match).
    bool TryGetValue(long long key, T& value) const {
        // The upper_bound on the interval starts: the first interval whose
        // start is greater than the key; the candidate is the one before it.
        auto it = std::upper_bound(
            pairs_.begin(), pairs_.end(), key,
            [](long long k, const std::pair<LongInterval, T>& p) {
                return k < p.first.Start;
            });
        if (it == pairs_.begin())
            return false;
        --it;
        if (it->first.Contains(key)) {
            value = it->second;
            return true;
        }
        return false;
    }

    // The C# `T GetOrDefault(long key)`.
    T GetOrDefault(long long key) const {
        T value{};
        TryGetValue(key, value);
        return value;
    }

    // The C# enumerator: the (interval, value) pairs in key order.
    const std::vector<std::pair<LongInterval, T>>& Entries() const {
        return pairs_;
    }

private:
    std::vector<std::pair<LongInterval, T>> pairs_;
};

// The C# `static class LongDict` factory: `LongDict.Create(entries)`.
template <typename T>
LongDict<T> MakeLongDict(std::vector<std::pair<LongSet, T>> entries) {
    return LongDict<T>(std::move(entries));
}

} // namespace ILSpy::Decompiler::Util
