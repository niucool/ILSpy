// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The implementation half of XmlConvert.hpp. The NCName tables and the
// escape/verify algorithms mirror the decompiled .NET 10 System.Private.Xml
// XmlConvert.EncodeName(name, first: true, local: true) /
// XmlConvert.VerifyNCName / XmlException.BuildCharExceptionArgs exactly (see
// the header for the probed facts); the char classification tables were
// generated unit-by-unit from the .NET 10 runtime by the XmlNameProbe probe.

#include "XmlConvert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Xml {
namespace {

// One inclusive BMP code-unit range. The tables are sorted ascending and
// non-overlapping (the probe walked the units in order), so the lookup is a
// binary search on the range starts.
struct UnitRange {
    std::uint16_t first;
    std::uint16_t last;
};

bool ContainsRange(const UnitRange* ranges, std::size_t count, std::uint32_t cp)
{
    if (cp > 0xFFFF)
        return false;
    const UnitRange* end = ranges + count;
    // lower_bound positions at the first range whose start exceeds cp; the
    // candidate containing cp is the last range whose start is <= cp -- the
    // one just before it (the tables are sorted ascending by start).
    const UnitRange* it = std::lower_bound(
        ranges, end, cp, [](const UnitRange& r, std::uint32_t value) {
            return r.first <= value;
        });
    if (it == ranges)
        return false;
    --it;
    return cp <= it->last;
}

// The XML 1.0 4th-edition NCName start-char units over the BMP -- the
// IsStartNCNameCharXml4e set (IsLetter | '_'), identical to the
// IsStartNCNameSingleChar set VerifyNCName uses (verified extensionally
// equal by two full-BMP probes). 205 ranges, probed unit-by-unit from the
// .NET 10 runtime.
constexpr UnitRange kStartNameCharRanges[] = {
    { 0x0041, 0x005A },  // 26 units
    { 0x005F, 0x005F },
    { 0x0061, 0x007A },  // 26 units
    { 0x00C0, 0x00D6 },  // 23 units
    { 0x00D8, 0x00F6 },  // 31 units
    { 0x00F8, 0x0131 },  // 58 units
    { 0x0134, 0x013E },  // 11 units
    { 0x0141, 0x0148 },  // 8 units
    { 0x014A, 0x017E },  // 53 units
    { 0x0180, 0x01C3 },  // 68 units
    { 0x01CD, 0x01F0 },  // 36 units
    { 0x01F4, 0x01F5 },  // 2 units
    { 0x01FA, 0x0217 },  // 30 units
    { 0x0250, 0x02A8 },  // 89 units
    { 0x02BB, 0x02C1 },  // 7 units
    { 0x0386, 0x0386 },
    { 0x0388, 0x038A },  // 3 units
    { 0x038C, 0x038C },
    { 0x038E, 0x03A1 },  // 20 units
    { 0x03A3, 0x03CE },  // 44 units
    { 0x03D0, 0x03D6 },  // 7 units
    { 0x03DA, 0x03DA },
    { 0x03DC, 0x03DC },
    { 0x03DE, 0x03DE },
    { 0x03E0, 0x03E0 },
    { 0x03E2, 0x03F3 },  // 18 units
    { 0x0401, 0x040C },  // 12 units
    { 0x040E, 0x044F },  // 66 units
    { 0x0451, 0x045C },  // 12 units
    { 0x045E, 0x0481 },  // 36 units
    { 0x0490, 0x04C4 },  // 53 units
    { 0x04C7, 0x04C8 },  // 2 units
    { 0x04CB, 0x04CC },  // 2 units
    { 0x04D0, 0x04EB },  // 28 units
    { 0x04EE, 0x04F5 },  // 8 units
    { 0x04F8, 0x04F9 },  // 2 units
    { 0x0531, 0x0556 },  // 38 units
    { 0x0559, 0x0559 },
    { 0x0561, 0x0586 },  // 38 units
    { 0x05D0, 0x05EA },  // 27 units
    { 0x05F0, 0x05F2 },  // 3 units
    { 0x0621, 0x063A },  // 26 units
    { 0x0641, 0x064A },  // 10 units
    { 0x0671, 0x06B7 },  // 71 units
    { 0x06BA, 0x06BE },  // 5 units
    { 0x06C0, 0x06CE },  // 15 units
    { 0x06D0, 0x06D3 },  // 4 units
    { 0x06D5, 0x06D5 },
    { 0x06E5, 0x06E6 },  // 2 units
    { 0x0905, 0x0939 },  // 53 units
    { 0x093D, 0x093D },
    { 0x0958, 0x0961 },  // 10 units
    { 0x0985, 0x098C },  // 8 units
    { 0x098F, 0x0990 },  // 2 units
    { 0x0993, 0x09A8 },  // 22 units
    { 0x09AA, 0x09B0 },  // 7 units
    { 0x09B2, 0x09B2 },
    { 0x09B6, 0x09B9 },  // 4 units
    { 0x09DC, 0x09DD },  // 2 units
    { 0x09DF, 0x09E1 },  // 3 units
    { 0x09F0, 0x09F1 },  // 2 units
    { 0x0A05, 0x0A0A },  // 6 units
    { 0x0A0F, 0x0A10 },  // 2 units
    { 0x0A13, 0x0A28 },  // 22 units
    { 0x0A2A, 0x0A30 },  // 7 units
    { 0x0A32, 0x0A33 },  // 2 units
    { 0x0A35, 0x0A36 },  // 2 units
    { 0x0A38, 0x0A39 },  // 2 units
    { 0x0A59, 0x0A5C },  // 4 units
    { 0x0A5E, 0x0A5E },
    { 0x0A72, 0x0A74 },  // 3 units
    { 0x0A85, 0x0A8B },  // 7 units
    { 0x0A8D, 0x0A8D },
    { 0x0A8F, 0x0A91 },  // 3 units
    { 0x0A93, 0x0AA8 },  // 22 units
    { 0x0AAA, 0x0AB0 },  // 7 units
    { 0x0AB2, 0x0AB3 },  // 2 units
    { 0x0AB5, 0x0AB9 },  // 5 units
    { 0x0ABD, 0x0ABD },
    { 0x0AE0, 0x0AE0 },
    { 0x0B05, 0x0B0C },  // 8 units
    { 0x0B0F, 0x0B10 },  // 2 units
    { 0x0B13, 0x0B28 },  // 22 units
    { 0x0B2A, 0x0B30 },  // 7 units
    { 0x0B32, 0x0B33 },  // 2 units
    { 0x0B36, 0x0B39 },  // 4 units
    { 0x0B3D, 0x0B3D },
    { 0x0B5C, 0x0B5D },  // 2 units
    { 0x0B5F, 0x0B61 },  // 3 units
    { 0x0B85, 0x0B8A },  // 6 units
    { 0x0B8E, 0x0B90 },  // 3 units
    { 0x0B92, 0x0B95 },  // 4 units
    { 0x0B99, 0x0B9A },  // 2 units
    { 0x0B9C, 0x0B9C },
    { 0x0B9E, 0x0B9F },  // 2 units
    { 0x0BA3, 0x0BA4 },  // 2 units
    { 0x0BA8, 0x0BAA },  // 3 units
    { 0x0BAE, 0x0BB5 },  // 8 units
    { 0x0BB7, 0x0BB9 },  // 3 units
    { 0x0C05, 0x0C0C },  // 8 units
    { 0x0C0E, 0x0C10 },  // 3 units
    { 0x0C12, 0x0C28 },  // 23 units
    { 0x0C2A, 0x0C33 },  // 10 units
    { 0x0C35, 0x0C39 },  // 5 units
    { 0x0C60, 0x0C61 },  // 2 units
    { 0x0C85, 0x0C8C },  // 8 units
    { 0x0C8E, 0x0C90 },  // 3 units
    { 0x0C92, 0x0CA8 },  // 23 units
    { 0x0CAA, 0x0CB3 },  // 10 units
    { 0x0CB5, 0x0CB9 },  // 5 units
    { 0x0CDE, 0x0CDE },
    { 0x0CE0, 0x0CE1 },  // 2 units
    { 0x0D05, 0x0D0C },  // 8 units
    { 0x0D0E, 0x0D10 },  // 3 units
    { 0x0D12, 0x0D28 },  // 23 units
    { 0x0D2A, 0x0D39 },  // 16 units
    { 0x0D60, 0x0D61 },  // 2 units
    { 0x0E01, 0x0E2E },  // 46 units
    { 0x0E30, 0x0E30 },
    { 0x0E32, 0x0E33 },  // 2 units
    { 0x0E40, 0x0E45 },  // 6 units
    { 0x0E81, 0x0E82 },  // 2 units
    { 0x0E84, 0x0E84 },
    { 0x0E87, 0x0E88 },  // 2 units
    { 0x0E8A, 0x0E8A },
    { 0x0E8D, 0x0E8D },
    { 0x0E94, 0x0E97 },  // 4 units
    { 0x0E99, 0x0E9F },  // 7 units
    { 0x0EA1, 0x0EA3 },  // 3 units
    { 0x0EA5, 0x0EA5 },
    { 0x0EA7, 0x0EA7 },
    { 0x0EAA, 0x0EAB },  // 2 units
    { 0x0EAD, 0x0EAE },  // 2 units
    { 0x0EB0, 0x0EB0 },
    { 0x0EB2, 0x0EB3 },  // 2 units
    { 0x0EBD, 0x0EBD },
    { 0x0EC0, 0x0EC4 },  // 5 units
    { 0x0F40, 0x0F47 },  // 8 units
    { 0x0F49, 0x0F69 },  // 33 units
    { 0x10A0, 0x10C5 },  // 38 units
    { 0x10D0, 0x10F6 },  // 39 units
    { 0x1100, 0x1100 },
    { 0x1102, 0x1103 },  // 2 units
    { 0x1105, 0x1107 },  // 3 units
    { 0x1109, 0x1109 },
    { 0x110B, 0x110C },  // 2 units
    { 0x110E, 0x1112 },  // 5 units
    { 0x113C, 0x113C },
    { 0x113E, 0x113E },
    { 0x1140, 0x1140 },
    { 0x114C, 0x114C },
    { 0x114E, 0x114E },
    { 0x1150, 0x1150 },
    { 0x1154, 0x1155 },  // 2 units
    { 0x1159, 0x1159 },
    { 0x115F, 0x1161 },  // 3 units
    { 0x1163, 0x1163 },
    { 0x1165, 0x1165 },
    { 0x1167, 0x1167 },
    { 0x1169, 0x1169 },
    { 0x116D, 0x116E },  // 2 units
    { 0x1172, 0x1173 },  // 2 units
    { 0x1175, 0x1175 },
    { 0x119E, 0x119E },
    { 0x11A8, 0x11A8 },
    { 0x11AB, 0x11AB },
    { 0x11AE, 0x11AF },  // 2 units
    { 0x11B7, 0x11B8 },  // 2 units
    { 0x11BA, 0x11BA },
    { 0x11BC, 0x11C2 },  // 7 units
    { 0x11EB, 0x11EB },
    { 0x11F0, 0x11F0 },
    { 0x11F9, 0x11F9 },
    { 0x1E00, 0x1E9B },  // 156 units
    { 0x1EA0, 0x1EF9 },  // 90 units
    { 0x1F00, 0x1F15 },  // 22 units
    { 0x1F18, 0x1F1D },  // 6 units
    { 0x1F20, 0x1F45 },  // 38 units
    { 0x1F48, 0x1F4D },  // 6 units
    { 0x1F50, 0x1F57 },  // 8 units
    { 0x1F59, 0x1F59 },
    { 0x1F5B, 0x1F5B },
    { 0x1F5D, 0x1F5D },
    { 0x1F5F, 0x1F7D },  // 31 units
    { 0x1F80, 0x1FB4 },  // 53 units
    { 0x1FB6, 0x1FBC },  // 7 units
    { 0x1FBE, 0x1FBE },
    { 0x1FC2, 0x1FC4 },  // 3 units
    { 0x1FC6, 0x1FCC },  // 7 units
    { 0x1FD0, 0x1FD3 },  // 4 units
    { 0x1FD6, 0x1FDB },  // 6 units
    { 0x1FE0, 0x1FEC },  // 13 units
    { 0x1FF2, 0x1FF4 },  // 3 units
    { 0x1FF6, 0x1FFC },  // 7 units
    { 0x2126, 0x2126 },
    { 0x212A, 0x212B },  // 2 units
    { 0x212E, 0x212E },
    { 0x2180, 0x2182 },  // 3 units
    { 0x3007, 0x3007 },
    { 0x3021, 0x3029 },  // 9 units
    { 0x3041, 0x3094 },  // 84 units
    { 0x30A1, 0x30FA },  // 90 units
    { 0x3105, 0x312C },  // 40 units
    { 0x4E00, 0x9FA5 },  // 20902 units
    { 0xAC00, 0xD7A3 },  // 11172 units
};
// The XML 1.0 4th-edition NCName continuation units over the BMP (the
// start chars included -- the merged IsNCNameCharXml4e/IsNCNameSingleChar
// set). 287 ranges, probed unit-by-unit from the .NET 10 runtime.
constexpr UnitRange kNameCharRanges[] = {
    { 0x002D, 0x002E },  // 2 units
    { 0x0030, 0x0039 },  // 10 units
    { 0x0041, 0x005A },  // 26 units
    { 0x005F, 0x005F },
    { 0x0061, 0x007A },  // 26 units
    { 0x00B7, 0x00B7 },
    { 0x00C0, 0x00D6 },  // 23 units
    { 0x00D8, 0x00F6 },  // 31 units
    { 0x00F8, 0x0131 },  // 58 units
    { 0x0134, 0x013E },  // 11 units
    { 0x0141, 0x0148 },  // 8 units
    { 0x014A, 0x017E },  // 53 units
    { 0x0180, 0x01C3 },  // 68 units
    { 0x01CD, 0x01F0 },  // 36 units
    { 0x01F4, 0x01F5 },  // 2 units
    { 0x01FA, 0x0217 },  // 30 units
    { 0x0250, 0x02A8 },  // 89 units
    { 0x02BB, 0x02C1 },  // 7 units
    { 0x02D0, 0x02D1 },  // 2 units
    { 0x0300, 0x0345 },  // 70 units
    { 0x0360, 0x0361 },  // 2 units
    { 0x0386, 0x038A },  // 5 units
    { 0x038C, 0x038C },
    { 0x038E, 0x03A1 },  // 20 units
    { 0x03A3, 0x03CE },  // 44 units
    { 0x03D0, 0x03D6 },  // 7 units
    { 0x03DA, 0x03DA },
    { 0x03DC, 0x03DC },
    { 0x03DE, 0x03DE },
    { 0x03E0, 0x03E0 },
    { 0x03E2, 0x03F3 },  // 18 units
    { 0x0401, 0x040C },  // 12 units
    { 0x040E, 0x044F },  // 66 units
    { 0x0451, 0x045C },  // 12 units
    { 0x045E, 0x0481 },  // 36 units
    { 0x0483, 0x0486 },  // 4 units
    { 0x0490, 0x04C4 },  // 53 units
    { 0x04C7, 0x04C8 },  // 2 units
    { 0x04CB, 0x04CC },  // 2 units
    { 0x04D0, 0x04EB },  // 28 units
    { 0x04EE, 0x04F5 },  // 8 units
    { 0x04F8, 0x04F9 },  // 2 units
    { 0x0531, 0x0556 },  // 38 units
    { 0x0559, 0x0559 },
    { 0x0561, 0x0586 },  // 38 units
    { 0x0591, 0x05A1 },  // 17 units
    { 0x05A3, 0x05B9 },  // 23 units
    { 0x05BB, 0x05BD },  // 3 units
    { 0x05BF, 0x05BF },
    { 0x05C1, 0x05C2 },  // 2 units
    { 0x05C4, 0x05C4 },
    { 0x05D0, 0x05EA },  // 27 units
    { 0x05F0, 0x05F2 },  // 3 units
    { 0x0621, 0x063A },  // 26 units
    { 0x0640, 0x0652 },  // 19 units
    { 0x0660, 0x0669 },  // 10 units
    { 0x0670, 0x06B7 },  // 72 units
    { 0x06BA, 0x06BE },  // 5 units
    { 0x06C0, 0x06CE },  // 15 units
    { 0x06D0, 0x06D3 },  // 4 units
    { 0x06D5, 0x06E8 },  // 20 units
    { 0x06EA, 0x06ED },  // 4 units
    { 0x06F0, 0x06F9 },  // 10 units
    { 0x0901, 0x0903 },  // 3 units
    { 0x0905, 0x0939 },  // 53 units
    { 0x093C, 0x094D },  // 18 units
    { 0x0951, 0x0954 },  // 4 units
    { 0x0958, 0x0963 },  // 12 units
    { 0x0966, 0x096F },  // 10 units
    { 0x0981, 0x0983 },  // 3 units
    { 0x0985, 0x098C },  // 8 units
    { 0x098F, 0x0990 },  // 2 units
    { 0x0993, 0x09A8 },  // 22 units
    { 0x09AA, 0x09B0 },  // 7 units
    { 0x09B2, 0x09B2 },
    { 0x09B6, 0x09B9 },  // 4 units
    { 0x09BC, 0x09BC },
    { 0x09BE, 0x09C4 },  // 7 units
    { 0x09C7, 0x09C8 },  // 2 units
    { 0x09CB, 0x09CD },  // 3 units
    { 0x09D7, 0x09D7 },
    { 0x09DC, 0x09DD },  // 2 units
    { 0x09DF, 0x09E3 },  // 5 units
    { 0x09E6, 0x09F1 },  // 12 units
    { 0x0A02, 0x0A02 },
    { 0x0A05, 0x0A0A },  // 6 units
    { 0x0A0F, 0x0A10 },  // 2 units
    { 0x0A13, 0x0A28 },  // 22 units
    { 0x0A2A, 0x0A30 },  // 7 units
    { 0x0A32, 0x0A33 },  // 2 units
    { 0x0A35, 0x0A36 },  // 2 units
    { 0x0A38, 0x0A39 },  // 2 units
    { 0x0A3C, 0x0A3C },
    { 0x0A3E, 0x0A42 },  // 5 units
    { 0x0A47, 0x0A48 },  // 2 units
    { 0x0A4B, 0x0A4D },  // 3 units
    { 0x0A59, 0x0A5C },  // 4 units
    { 0x0A5E, 0x0A5E },
    { 0x0A66, 0x0A74 },  // 15 units
    { 0x0A81, 0x0A83 },  // 3 units
    { 0x0A85, 0x0A8B },  // 7 units
    { 0x0A8D, 0x0A8D },
    { 0x0A8F, 0x0A91 },  // 3 units
    { 0x0A93, 0x0AA8 },  // 22 units
    { 0x0AAA, 0x0AB0 },  // 7 units
    { 0x0AB2, 0x0AB3 },  // 2 units
    { 0x0AB5, 0x0AB9 },  // 5 units
    { 0x0ABC, 0x0AC5 },  // 10 units
    { 0x0AC7, 0x0AC9 },  // 3 units
    { 0x0ACB, 0x0ACD },  // 3 units
    { 0x0AE0, 0x0AE0 },
    { 0x0AE6, 0x0AEF },  // 10 units
    { 0x0B01, 0x0B03 },  // 3 units
    { 0x0B05, 0x0B0C },  // 8 units
    { 0x0B0F, 0x0B10 },  // 2 units
    { 0x0B13, 0x0B28 },  // 22 units
    { 0x0B2A, 0x0B30 },  // 7 units
    { 0x0B32, 0x0B33 },  // 2 units
    { 0x0B36, 0x0B39 },  // 4 units
    { 0x0B3C, 0x0B43 },  // 8 units
    { 0x0B47, 0x0B48 },  // 2 units
    { 0x0B4B, 0x0B4D },  // 3 units
    { 0x0B56, 0x0B57 },  // 2 units
    { 0x0B5C, 0x0B5D },  // 2 units
    { 0x0B5F, 0x0B61 },  // 3 units
    { 0x0B66, 0x0B6F },  // 10 units
    { 0x0B82, 0x0B83 },  // 2 units
    { 0x0B85, 0x0B8A },  // 6 units
    { 0x0B8E, 0x0B90 },  // 3 units
    { 0x0B92, 0x0B95 },  // 4 units
    { 0x0B99, 0x0B9A },  // 2 units
    { 0x0B9C, 0x0B9C },
    { 0x0B9E, 0x0B9F },  // 2 units
    { 0x0BA3, 0x0BA4 },  // 2 units
    { 0x0BA8, 0x0BAA },  // 3 units
    { 0x0BAE, 0x0BB5 },  // 8 units
    { 0x0BB7, 0x0BB9 },  // 3 units
    { 0x0BBE, 0x0BC2 },  // 5 units
    { 0x0BC6, 0x0BC8 },  // 3 units
    { 0x0BCA, 0x0BCD },  // 4 units
    { 0x0BD7, 0x0BD7 },
    { 0x0BE7, 0x0BEF },  // 9 units
    { 0x0C01, 0x0C03 },  // 3 units
    { 0x0C05, 0x0C0C },  // 8 units
    { 0x0C0E, 0x0C10 },  // 3 units
    { 0x0C12, 0x0C28 },  // 23 units
    { 0x0C2A, 0x0C33 },  // 10 units
    { 0x0C35, 0x0C39 },  // 5 units
    { 0x0C3E, 0x0C44 },  // 7 units
    { 0x0C46, 0x0C48 },  // 3 units
    { 0x0C4A, 0x0C4D },  // 4 units
    { 0x0C55, 0x0C56 },  // 2 units
    { 0x0C60, 0x0C61 },  // 2 units
    { 0x0C66, 0x0C6F },  // 10 units
    { 0x0C82, 0x0C83 },  // 2 units
    { 0x0C85, 0x0C8C },  // 8 units
    { 0x0C8E, 0x0C90 },  // 3 units
    { 0x0C92, 0x0CA8 },  // 23 units
    { 0x0CAA, 0x0CB3 },  // 10 units
    { 0x0CB5, 0x0CB9 },  // 5 units
    { 0x0CBE, 0x0CC4 },  // 7 units
    { 0x0CC6, 0x0CC8 },  // 3 units
    { 0x0CCA, 0x0CCD },  // 4 units
    { 0x0CD5, 0x0CD6 },  // 2 units
    { 0x0CDE, 0x0CDE },
    { 0x0CE0, 0x0CE1 },  // 2 units
    { 0x0CE6, 0x0CEF },  // 10 units
    { 0x0D02, 0x0D03 },  // 2 units
    { 0x0D05, 0x0D0C },  // 8 units
    { 0x0D0E, 0x0D10 },  // 3 units
    { 0x0D12, 0x0D28 },  // 23 units
    { 0x0D2A, 0x0D39 },  // 16 units
    { 0x0D3E, 0x0D43 },  // 6 units
    { 0x0D46, 0x0D48 },  // 3 units
    { 0x0D4A, 0x0D4D },  // 4 units
    { 0x0D57, 0x0D57 },
    { 0x0D60, 0x0D61 },  // 2 units
    { 0x0D66, 0x0D6F },  // 10 units
    { 0x0E01, 0x0E2E },  // 46 units
    { 0x0E30, 0x0E3A },  // 11 units
    { 0x0E40, 0x0E4E },  // 15 units
    { 0x0E50, 0x0E59 },  // 10 units
    { 0x0E81, 0x0E82 },  // 2 units
    { 0x0E84, 0x0E84 },
    { 0x0E87, 0x0E88 },  // 2 units
    { 0x0E8A, 0x0E8A },
    { 0x0E8D, 0x0E8D },
    { 0x0E94, 0x0E97 },  // 4 units
    { 0x0E99, 0x0E9F },  // 7 units
    { 0x0EA1, 0x0EA3 },  // 3 units
    { 0x0EA5, 0x0EA5 },
    { 0x0EA7, 0x0EA7 },
    { 0x0EAA, 0x0EAB },  // 2 units
    { 0x0EAD, 0x0EAE },  // 2 units
    { 0x0EB0, 0x0EB9 },  // 10 units
    { 0x0EBB, 0x0EBD },  // 3 units
    { 0x0EC0, 0x0EC4 },  // 5 units
    { 0x0EC6, 0x0EC6 },
    { 0x0EC8, 0x0ECD },  // 6 units
    { 0x0ED0, 0x0ED9 },  // 10 units
    { 0x0F18, 0x0F19 },  // 2 units
    { 0x0F20, 0x0F29 },  // 10 units
    { 0x0F35, 0x0F35 },
    { 0x0F37, 0x0F37 },
    { 0x0F39, 0x0F39 },
    { 0x0F3E, 0x0F47 },  // 10 units
    { 0x0F49, 0x0F69 },  // 33 units
    { 0x0F71, 0x0F84 },  // 20 units
    { 0x0F86, 0x0F8B },  // 6 units
    { 0x0F90, 0x0F95 },  // 6 units
    { 0x0F97, 0x0F97 },
    { 0x0F99, 0x0FAD },  // 21 units
    { 0x0FB1, 0x0FB7 },  // 7 units
    { 0x0FB9, 0x0FB9 },
    { 0x10A0, 0x10C5 },  // 38 units
    { 0x10D0, 0x10F6 },  // 39 units
    { 0x1100, 0x1100 },
    { 0x1102, 0x1103 },  // 2 units
    { 0x1105, 0x1107 },  // 3 units
    { 0x1109, 0x1109 },
    { 0x110B, 0x110C },  // 2 units
    { 0x110E, 0x1112 },  // 5 units
    { 0x113C, 0x113C },
    { 0x113E, 0x113E },
    { 0x1140, 0x1140 },
    { 0x114C, 0x114C },
    { 0x114E, 0x114E },
    { 0x1150, 0x1150 },
    { 0x1154, 0x1155 },  // 2 units
    { 0x1159, 0x1159 },
    { 0x115F, 0x1161 },  // 3 units
    { 0x1163, 0x1163 },
    { 0x1165, 0x1165 },
    { 0x1167, 0x1167 },
    { 0x1169, 0x1169 },
    { 0x116D, 0x116E },  // 2 units
    { 0x1172, 0x1173 },  // 2 units
    { 0x1175, 0x1175 },
    { 0x119E, 0x119E },
    { 0x11A8, 0x11A8 },
    { 0x11AB, 0x11AB },
    { 0x11AE, 0x11AF },  // 2 units
    { 0x11B7, 0x11B8 },  // 2 units
    { 0x11BA, 0x11BA },
    { 0x11BC, 0x11C2 },  // 7 units
    { 0x11EB, 0x11EB },
    { 0x11F0, 0x11F0 },
    { 0x11F9, 0x11F9 },
    { 0x1E00, 0x1E9B },  // 156 units
    { 0x1EA0, 0x1EF9 },  // 90 units
    { 0x1F00, 0x1F15 },  // 22 units
    { 0x1F18, 0x1F1D },  // 6 units
    { 0x1F20, 0x1F45 },  // 38 units
    { 0x1F48, 0x1F4D },  // 6 units
    { 0x1F50, 0x1F57 },  // 8 units
    { 0x1F59, 0x1F59 },
    { 0x1F5B, 0x1F5B },
    { 0x1F5D, 0x1F5D },
    { 0x1F5F, 0x1F7D },  // 31 units
    { 0x1F80, 0x1FB4 },  // 53 units
    { 0x1FB6, 0x1FBC },  // 7 units
    { 0x1FBE, 0x1FBE },
    { 0x1FC2, 0x1FC4 },  // 3 units
    { 0x1FC6, 0x1FCC },  // 7 units
    { 0x1FD0, 0x1FD3 },  // 4 units
    { 0x1FD6, 0x1FDB },  // 6 units
    { 0x1FE0, 0x1FEC },  // 13 units
    { 0x1FF2, 0x1FF4 },  // 3 units
    { 0x1FF6, 0x1FFC },  // 7 units
    { 0x20D0, 0x20DC },  // 13 units
    { 0x20E1, 0x20E1 },
    { 0x2126, 0x2126 },
    { 0x212A, 0x212B },  // 2 units
    { 0x212E, 0x212E },
    { 0x2180, 0x2182 },  // 3 units
    { 0x3005, 0x3005 },
    { 0x3007, 0x3007 },
    { 0x3021, 0x302F },  // 15 units
    { 0x3031, 0x3035 },  // 5 units
    { 0x3041, 0x3094 },  // 84 units
    { 0x3099, 0x309A },  // 2 units
    { 0x309D, 0x309E },  // 2 units
    { 0x30A1, 0x30FA },  // 90 units
    { 0x30FC, 0x30FE },  // 3 units
    { 0x3105, 0x312C },  // 40 units
    { 0x4E00, 0x9FA5 },  // 20902 units
    { 0xAC00, 0xD7A3 },  // 11172 units
};


bool IsStartNameChar(std::uint32_t cp)
{
    // The first UTF-16 unit of a supplementary code point is a high
    // surrogate, which is never a name char (the .NET tables are BMP-only).
    return ContainsRange(kStartNameCharRanges, std::size(kStartNameCharRanges), cp);
}

bool IsNameChar(std::uint32_t cp)
{
    return ContainsRange(kNameCharRanges, std::size(kNameCharRanges), cp);
}

bool IsHexDigit(std::uint32_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F');
}

bool IsHighSurrogateUnit(std::uint32_t cp)
{
    return cp >= 0xD800 && cp <= 0xDBFF;
}

bool IsLowSurrogateUnit(std::uint32_t cp)
{
    return cp >= 0xDC00 && cp <= 0xDFFF;
}

// One decoded code point with the byte span it came from. The decoder is
// permissive: it accepts every well-formed-looking sequence that decodes to
// [0x80, 0x10FFFF] including the surrogate range (a lone surrogate reaches
// the port only as WTF-8 bytes and must escape per-unit like .NET), combines
// an adjacent surrogate pair into one code point (the .NET unit view treats
// them as one char), and maps malformed input to U+FFFD.
struct DecodedUnit {
    std::uint32_t cp;
    std::uint32_t offset;
    std::uint32_t length;
};

std::vector<DecodedUnit> DecodeAll(const std::string& s)
{
    std::vector<DecodedUnit> units;
    units.reserve(s.size());
    const auto size = static_cast<std::uint32_t>(s.size());
    std::uint32_t i = 0;
    while (i < size) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        std::uint32_t len;
        std::uint32_t cp;
        if (b < 0x80) {
            len = 1;
            cp = b;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < size && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
            len = 2;
            cp = (b & 0x1F) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        } else if ((b & 0xF0) == 0xE0 && i + 2 < size && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80) {
            len = 3;
            cp = (b & 0x0F) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6 | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        } else if ((b & 0xF8) == 0xF0 && i + 3 < size && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 3]) & 0xC0) == 0x80) {
            len = 4;
            cp = (b & 0x07) << 18 | (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12 | (static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6 | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        } else {
            // Malformed: a replacement char consuming the single bad byte.
            units.push_back({ 0xFFFD, i, 1 });
            ++i;
            continue;
        }
        if (cp > 0x10FFFF || (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) {
            // Overlong or out-of-range: treat as malformed.
            units.push_back({ 0xFFFD, i, 1 });
            ++i;
            continue;
        }
        if (IsHighSurrogateUnit(cp)) {
            // A high surrogate followed by a low surrogate is one code
            // point (the .NET UTF-16 unit view); anything else keeps the
            // lone surrogate as its own unit.
            if (i + len + 2 < size) {
                unsigned char b1 = static_cast<unsigned char>(s[i + len]);
                unsigned char b2 = static_cast<unsigned char>(s[i + len + 1]);
                unsigned char b3 = static_cast<unsigned char>(s[i + len + 2]);
                if ((b1 & 0xF0) == 0xE0 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                    std::uint32_t low = (b1 & 0x0F) << 12 | (b2 & 0x3F) << 6 | (b3 & 0x3F);
                    if (IsLowSurrogateUnit(low)) {
                        units.push_back({ 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00), i, len + 3 });
                        i += len + 3;
                        continue;
                    }
                }
            }
        }
        units.push_back({ cp, i, len });
        i += len;
    }
    return units;
}

// Appends the UTF-8 encoding of a code point (WTF-8 for surrogate units;
// the 4-byte form for supplementary points).
void AppendUnitUtf8(std::string& out, std::uint32_t cp)
{
    char buf[4];
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        buf[0] = static_cast<char>(0xC0 | cp >> 6);
        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
        out.append(buf, 2);
    } else if (cp < 0x10000) {
        buf[0] = static_cast<char>(0xE0 | cp >> 12);
        buf[1] = static_cast<char>(0x80 | (cp >> 6 & 0x3F));
        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
        out.append(buf, 3);
    } else {
        buf[0] = static_cast<char>(0xF0 | cp >> 18);
        buf[1] = static_cast<char>(0x80 | (cp >> 12 & 0x3F));
        buf[2] = static_cast<char>(0x80 | (cp >> 6 & 0x3F));
        buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
        out.append(buf, 4);
    }
}

// The first UTF-16 unit of a code point (the high half for supplementary
// points, the point itself otherwise).
std::uint32_t FirstUnit(std::uint32_t cp)
{
    if (cp > 0xFFFF)
        return 0xD800 + ((cp - 0x10000) >> 10);
    return cp;
}

// Formats an int32 the way the C# interpolation $"0x{value:X2}" does:
// uppercase hex, at least two digits, the full 32-bit width for negative
// values (0xFFFF2462 -- the lone-surrogate quirk, see the header).
void AppendHexX2(std::string& out, std::int32_t value)
{
    out += "0x";
    char buf[8];
    std::uint32_t u = static_cast<std::uint32_t>(value);
    int digits = 0;
    while (u != 0) {
        buf[digits++] = "0123456789ABCDEF"[u & 0xF];
        u >>= 4;
    }
    if (digits < 2) {
        out.push_back('0');
        if (digits == 0)
            out.push_back('0');
    }
    for (int k = digits - 1; k >= 0; --k)
        out.push_back(buf[k]);
}

// The XmlException.BuildCharExceptionArgs pair: the display string for the
// invalid unit (the '.' stand-in for NUL, both units for a high surrogate
// followed by any non-NUL unit) and the "0x{...:X2}" value.
std::string InvalidCharDisplay(std::uint32_t invChar, std::uint32_t nextChar)
{
    if (invChar == 0)
        return ".";
    if (IsHighSurrogateUnit(invChar) && nextChar != 0) {
        std::string out;
        if (IsLowSurrogateUnit(nextChar)) {
            // A real pair: the display is the supplementary character, so the
            // natural UTF-8 encoding of the combined code point.
            AppendUnitUtf8(out, 0x10000 + ((invChar - 0xD800) << 10) + (nextChar - 0xDC00));
        } else {
            // The lone-surrogate quirk: .NET renders the invalid unit and the
            // next unit verbatim (an unpaired surrogate half reaches the
            // port as WTF-8 bytes; the display keeps them).
            AppendUnitUtf8(out, invChar);
            AppendUnitUtf8(out, nextChar);
        }
        return out;
    }
    std::string out;
    AppendUnitUtf8(out, invChar);
    return out;
}

std::int32_t CombineSurrogateValue(std::uint32_t nextChar, std::uint32_t invChar)
{
    // XmlCharType.CombineSurrogateChar verbatim, including the int32
    // arithmetic that turns a lone surrogate followed by a non-low-surrogate
    // unit into the garbage negative value .NET prints in full width.
    return static_cast<std::int32_t>((static_cast<std::int32_t>(nextChar) - 56320)
        | (((static_cast<std::int32_t>(invChar) - 55296) << 10) + 65536));
}

std::int32_t InvalidCharValue(std::uint32_t invChar, std::uint32_t nextChar)
{
    if (IsHighSurrogateUnit(invChar) && nextChar != 0)
        return CombineSurrogateValue(nextChar, invChar);
    return static_cast<std::int32_t>(invChar);
}

// Throws the XmlException XmlConvert.CreateInvalidNameCharException builds:
// the first-unit form when index is 0, the later-unit form otherwise.
[[noreturn]] void ThrowInvalidNameChar(const std::vector<DecodedUnit>& units, std::size_t index)
{
    std::uint32_t invChar = FirstUnit(units[index].cp);
    std::uint32_t nextChar = 0;
    if (index + 1 < units.size())
        nextChar = FirstUnit(units[index + 1].cp);
    // A supplementary code point always reports its own pair: the invalid
    // unit is the high surrogate and the next unit its low half.
    if (units[index].cp > 0xFFFF)
        nextChar = 0xDC00 + ((units[index].cp - 0x10000) & 0x3FF);
    std::string display = InvalidCharDisplay(invChar, nextChar);
    std::string value;
    AppendHexX2(value, InvalidCharValue(invChar, nextChar));
    if (index == 0)
        throw XmlException("Name cannot begin with the '" + display + "' character, hexadecimal value " + value + ".");
    throw XmlException("The '" + display + "' character, hexadecimal value " + value + ", cannot be included in a name.");
}

void AppendEscape(std::string& out, std::uint32_t cp)
{
    // "_x" + X4/X8 uppercase + "_" (the .NET X4 for BMP units, X8 for the
    // combined surrogate value of a supplementary point).
    out += "_x";
    char buf[8];
    std::uint32_t u = cp;
    int width = cp > 0xFFFF ? 8 : 4;
    for (int k = width - 1; k >= 0; --k) {
        buf[k] = "0123456789ABCDEF"[u & 0xF];
        u >>= 4;
    }
    out.append(buf, static_cast<std::size_t>(width));
    out += '_';
}

// Matches the .NET 10 generated regex `(?<=_)[Xx][0-9a-fA-F]{4}(?:_|[0-9a-fA-F]{4}_)`
// at the given code-point index (the caller guarantees a '_' precedes it);
// returns the match length in code points, 0 for no match.
std::size_t MatchEscapePattern(const std::vector<DecodedUnit>& units, std::size_t p)
{
    auto at = [&](std::size_t k) { return units[p + k].cp; };
    std::size_t n = units.size();
    if (at(0) != 'x' && at(0) != 'X')
        return 0;
    for (int k = 1; k <= 4; ++k)
        if (p + static_cast<std::size_t>(k) >= n || !IsHexDigit(at(static_cast<std::size_t>(k))))
            return 0;
    if (p + 5 < n && at(5) == '_')
        return 6;
    if (p + 9 < n && at(9) == '_') {
        for (int k = 5; k <= 8; ++k)
            if (!IsHexDigit(at(static_cast<std::size_t>(k))))
                return 0;
        return 10;
    }
    return 0;
}

} // namespace

std::string VerifyNCName(const std::string& name)
{
    if (name.empty())
        throw std::invalid_argument("The value cannot be an empty string. (Parameter 'name')");
    std::vector<DecodedUnit> units = DecodeAll(name);
    // ValidateNames.ParseNCName: a bad start unit reports index 0; the walk
    // then stops at the first unit that is not a continuation char.
    if (units.empty() || !IsStartNameChar(units[0].cp))
        ThrowInvalidNameChar(units, 0);
    for (std::size_t i = 1; i < units.size(); ++i) {
        if (!IsNameChar(units[i].cp))
            ThrowInvalidNameChar(units, i);
    }
    return name;
}

std::string VerifyName(const std::string& name)
{
    // ArgumentException.ThrowIfNullOrEmpty(name, "name") -- the null arm is
    // unreachable (std::string has no null).
    if (name.empty())
        throw std::invalid_argument("The value cannot be an empty string. (Parameter 'name')");
    std::vector<DecodedUnit> units = DecodeAll(name);
    // ValidateNames.ParseNameNoNamespaces: a Name may start with an NCName
    // start char OR ':', and ':' is a valid continuation unit (multiple
    // colons included).
    std::size_t i = 0;
    if (i < units.size()) {
        if (!IsStartNameChar(units[i].cp) && units[i].cp != ':')
            ThrowInvalidNameChar(units, 0);
        for (i++; i < units.size() && (IsNameChar(units[i].cp) || units[i].cp == ':'); i++) {
        }
    }
    // XmlConvert.VerifyName: the parse must consume the whole name.
    if (i != units.size())
        ThrowInvalidNameChar(units, i);
    return name;
}

std::string EncodeLocalName(const std::string& name)
{
    if (name.empty())
        return name;
    std::vector<DecodedUnit> units = DecodeAll(name);
    std::size_t count = units.size();

    // The pending-underscore scan: every regex match (searched from the
    // first '_') marks the '_' just before it as one whose literal copy
    // would be ambiguous, so it is escaped as _x005F_ instead. Matches do
    // not overlap.
    std::vector<std::size_t> pendingUnderscores;
    std::size_t firstUnderscore = 0;
    while (firstUnderscore < count && units[firstUnderscore].cp != '_')
        ++firstUnderscore;
    if (firstUnderscore < count) {
        std::size_t p = firstUnderscore + 1;
        while (p < count) {
            if (units[p - 1].cp == '_') {
                std::size_t len = MatchEscapePattern(units, p);
                if (len != 0) {
                    pendingUnderscores.push_back(p - 1);
                    p += len;
                    continue;
                }
            }
            ++p;
        }
    }
    std::size_t pendingIdx = 0;
    auto pendingAt = [&](std::int64_t i) -> std::int64_t {
        return pendingIdx < pendingUnderscores.size() ? static_cast<std::int64_t>(pendingUnderscores[pendingIdx]) : -1;
    };
    auto advancePending = [&]() {
        if (pendingIdx < pendingUnderscores.size())
            ++pendingIdx;
    };

    std::string sb;
    std::size_t num = 0; // start of the current literal run
    std::size_t i = 0;

    // The first unit: escape when it is not a start char or when it is the
    // '_' of a potential escape sequence.
    if (!IsStartNameChar(units[0].cp) || pendingAt(0) == 0) {
        sb += "_x";
        char hex[8];
        std::uint32_t u = units[0].cp;
        int width = units[0].cp > 0xFFFF ? 8 : 4;
        for (int k = width - 1; k >= 0; --k) {
            hex[k] = "0123456789ABCDEF"[u & 0xF];
            u >>= 4;
        }
        sb.append(hex, static_cast<std::size_t>(width));
        sb += '_';
        i = 1;
        num = 1;
        if (pendingAt(0) == 0)
            advancePending();
    }

    for (; i < count; ++i) {
        std::int64_t pending = pendingAt(i);
        if (!IsNameChar(units[i].cp) || pending == static_cast<std::int64_t>(i)) {
            if (pending == static_cast<std::int64_t>(i))
                advancePending();
            // The literal run before the escaped unit.
            if (i > num)
                sb.append(name, units[num].offset, units[i].offset - units[num].offset);
            AppendEscape(sb, units[i].cp);
            num = i + 1;
        }
    }
    if (num == 0)
        return name;
    if (num < count)
        sb.append(name, units[num].offset, name.size() - units[num].offset);
    return sb;
}

} // namespace ILSpy::Decompiler::Xml
