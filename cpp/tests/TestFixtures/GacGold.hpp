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

// The gold data for the UniversalAssemblyResolver GAC-machinery tests,
// dumped from the real installed ICSharpCode.Decompiler 11.0 (the
// C:/temp-probe/UarProbe gold probe) over THIS machine's real .NET
// Framework 4.8 GAC -- a machine-pinned snapshot: a Windows Update that
// adds or removes a GAC assembly moves the count and the digest (regenerate
// this fixture from the probe when that happens).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ILSpy::Tests {

// The EnumerateGac snapshot: every entry's full name round-trips through
// AssemblyNameReference.Parse (the machine-independent invariant).
inline constexpr std::size_t kGacEntryCount = 608;
inline constexpr std::size_t kGacRoundTripOk = 608;
inline constexpr std::size_t kGacRoundTripBad = 0;
// The FNV-1a-64 over 'N|count=<n>' + 'N|roundtrip ok=<ok> bad=<bad>' + the
// 608 sorted 'N|<FullName>' lines (StringComparer.Ordinal -- byte-ordinal
// for the all-ASCII snapshot names).
inline constexpr std::uint64_t kGacSortedNamesDigest = 0x4E40291C5105F74EULL;

// The K known-entry subset (the mscorlib/System/System.Core entries,
// ordinal sorted).
inline constexpr std::array<std::string_view, 4> kGacKnownEntries = {
    "System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "System.Core, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
};

// The X crafted folder-name regex matrix: the inputs and the exact gold
// renders (FAIL, or the OK capture groups) of the real .NET
// Regex.Match over the EnumerateGac pattern (parallel arrays).
inline constexpr std::array<std::string_view, 31>
    kGacFolderNameInputs = {
    "4.0.0.0__b77a5c561934e089",
    "v4.0_4.0.0.0__b77a5c561934e089",
    "10.0.0.0_en_31bf3856ad364e35",
    "v4.0_10.0.0.0_de-de_31bf3856ad364e35",
    "1.0.0.0_de_31bf3856ad364e35",
    "1.0.0.0__",
    "1.0.0.0_",
    "a_b_c_d",
    "a__b_c",
    "a_b_c",
    "a_b",
    "a_",
    "a",
    "abc",
    "_a_b",
    "__a_b",
    "v4.0_a_b",
    "v4.0_a_b_c",
    "x_v4.0_a_b_c",
    "v4.0_a_b_c_d",
    "v4.0__",
    "v4.0___",
    "v4.0_",
    "v40_a_b",
    "4.0.0.0__b77a5c561934e089_extra",
    "no_underscore_at_all",
    "1.2.3.4_aa_bb_cc_dd",
    "1.0_2.0_b77a5c561934e089",
    "__b77a5c561934e089",
    "_b77a5c561934e089",
    "",
};

inline constexpr std::array<std::string_view, 31>
    kGacFolderNameExpected = {
    "OK|v=4.0.0.0|c=|p=b77a5c561934e089",
    "OK|v=4.0.0.0|c=|p=b77a5c561934e089",
    "OK|v=10.0.0.0|c=en|p=31bf3856ad364e35",
    "OK|v=10.0.0.0|c=de-de|p=31bf3856ad364e35",
    "OK|v=1.0.0.0|c=de|p=31bf3856ad364e35",
    "FAIL",
    "FAIL",
    "OK|v=a|c=b|p=c",
    "OK|v=a|c=|p=b",
    "OK|v=a|c=b|p=c",
    "FAIL",
    "FAIL",
    "FAIL",
    "FAIL",
    "FAIL",
    "FAIL",
    "OK|v=v4.0|c=a|p=b",
    "OK|v=a|c=b|p=c",
    "OK|v=x|c=v4.0|p=a",
    "OK|v=a|c=b|p=c",
    "FAIL",
    "FAIL",
    "FAIL",
    "OK|v=v40|c=a|p=b",
    "OK|v=4.0.0.0|c=|p=b77a5c561934e089",
    "OK|v=no|c=underscore|p=at",
    "OK|v=1.2.3.4|c=aa|p=bb",
    "OK|v=1.0|c=2.0|p=b77a5c561934e089",
    "FAIL",
    "FAIL",
    "FAIL",
};


}  // namespace ILSpy::Tests
