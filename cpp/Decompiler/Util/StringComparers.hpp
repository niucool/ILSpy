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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The .NET culture-linguistic string comparison, emulated for the
// printable-ASCII subset. The C# code this port reproduces sorts with
// `Comparer<string>.Default` (`Enumerable.OrderBy`'s default comparer), which
// compares strings through `string.CompareTo` -- the CURRENT CULTURE's
// linguistic collation, not an ordinal comparison. The two C# consumers that
// matter (the SortByNameProcessor and the disassembler test harness that
// generated the SortMembers gold fixture) run under the invariant culture or
// en-US, which agree on every printable-ASCII case, so the port carries the
// invariant collation as a deterministic table.
//
// The collation (verified against .NET's StringComparer.InvariantCulture and
// the en-US current culture, pair by pair):
//   * PRIMARY: every character has a primary sort weight. Punctuation sorts
//     before digits, digits before letters, in the NLS order
//     ` _ - , ; : ! ? . ' " ( ) [ ] { } @ * / \ & # % ` ^ + < = > | ~ $`
//     (space first, then that list, then 0-9, then a-z case-folded).
//     So "_test" < "Test" and "Foo`1" < "FooBar" -- the opposite of what
//     std::string's ordinal comparison says, and exactly what the
//     SortMembers.expected.il gold fixture shows.
//   * SECONDARY: when the primary weights are all equal (the strings differ
//     only in character case), lowercase sorts before uppercase
//     ("a" < "A", "abc" < "ABC", "aBc" < "AbC").
//   * Non-ASCII bytes compare after the ASCII block by byte value (a
//     documented divergence: .NET compares UTF-16 code units under ICU
//     collation; metadata names in this port's corpus are ASCII).
//
// This is a C++-only addition (the C# comparison lives in the BCL's
// CompareInfo machinery, not in the ported sources); it exists so the
// SortByNameProcessor reproduces the C# sort order byte for byte.

#pragma once

#include <string_view>

namespace ILSpy::Decompiler::Util {

// The .NET invariant-culture string comparison (string.CompareTo /
// StringComparer.InvariantCulture) over the printable-ASCII subset:
// negative when `a` sorts before `b`, positive when after, zero when equal.
int CompareInvariantCulture(std::string_view a, std::string_view b);

// The same comparison as a strict-weak-ordering predicate for the standard
// algorithms (std::sort/std::stable_sort's `comp(a, b)` form).
inline bool InvariantCultureLess(std::string_view a, std::string_view b) {
    return CompareInvariantCulture(a, b) < 0;
}

}  // namespace ILSpy::Decompiler::Util
