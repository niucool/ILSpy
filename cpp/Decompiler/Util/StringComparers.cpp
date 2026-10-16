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

// See StringComparers.hpp for the collation contract and the .NET probes
// behind the weight tables.

#include "Decompiler/Util/StringComparers.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::Util {
namespace {

// The primary sort weight of one byte: the NLS punctuation order, then
// digits, then case-folded letters. Every character carries a primary weight
// (the probed .NET behavior: "a-b" sorts before "aa", so even '-' and '.'
// are primary, not ignorable).
std::uint32_t PrimaryWeight(unsigned char c) {
    switch (c) {
        case ' ': return 1;
        case '_': return 2;
        case '-': return 3;
        case ',': return 4;
        case ';': return 5;
        case ':': return 6;
        case '!': return 7;
        case '?': return 8;
        case '.': return 9;
        case '\'': return 10;
        case '"': return 11;
        case '(': return 12;
        case ')': return 13;
        case '[': return 14;
        case ']': return 15;
        case '{': return 16;
        case '}': return 17;
        case '@': return 18;
        case '*': return 19;
        case '/': return 20;
        case '\\': return 21;
        case '&': return 22;
        case '#': return 23;
        case '%': return 24;
        case '`': return 25;
        case '^': return 26;
        case '+': return 27;
        case '<': return 28;
        case '=': return 29;
        case '>': return 30;
        case '|': return 31;
        case '~': return 32;
        case '$': return 33;
        default:
            if (c >= '0' && c <= '9') return 34 + (c - '0');
            if (c >= 'a' && c <= 'z') return 44 + (c - 'a');
            if (c >= 'A' && c <= 'Z') return 44 + (c - 'A');
            if (c >= 0x80) return 1000 + c;
            // Control characters never occur in metadata names; they keep a
            // byte-value order beyond every real character.
            return 900000u + c;
    }
}

// The secondary (case) weight, consulted only when the primary weights are
// all equal: lowercase sorts before uppercase (the probed "a" < "A").
std::uint32_t SecondaryWeight(unsigned char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 2;
    return 0;
}

}  // namespace

int CompareInvariantCulture(std::string_view a, std::string_view b) {
    // Primary pass: elementwise over the primary weights, a shared prefix
    // sorting first (the shorter string wins, matching .NET's "Foo" < "Foo`1").
    std::size_t common = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < common; i++) {
        std::uint32_t wa = PrimaryWeight(static_cast<unsigned char>(a[i]));
        std::uint32_t wb = PrimaryWeight(static_cast<unsigned char>(b[i]));
        if (wa != wb) return wa < wb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    // The primary weights agree on every character, so the strings differ
    // only in case: the per-position case tiebreak decides.
    for (std::size_t i = 0; i < a.size(); i++) {
        std::uint32_t sa = SecondaryWeight(static_cast<unsigned char>(a[i]));
        std::uint32_t sb = SecondaryWeight(static_cast<unsigned char>(b[i]));
        if (sa != sb) return sa < sb ? -1 : 1;
    }
    return 0;
}

}  // namespace ILSpy::Decompiler::Util
