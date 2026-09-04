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

// The synthetic .baml fixture and its write gold: the 352-byte record stream
// from the BamlProbe make_synth.py crafter (covering ~50 record types
// including the full defer block: DeferableContentStart +
// DefAttributeKeyString + StaticResource/KeyElement keys), and the byte
// stream the REAL ICSharpCode.BamlDecompiler BamlWriter.WriteDocument
// produces over it (BamlWriteProbe). Every field expectation over this
// fixture was dumped from the real reader
// (C:\temp-probe\BamlProbe\out\synth.dump.txt).
//
// The write gold differs from the fixture exactly in the RoutedEvent
// record's payload: the C# ReadData reads AttributeId then Value while
// WriteData writes Value then AttributeId (positions 236..243), so a
// written RoutedEvent does not read back to the same values -- the
// faithful-port asymmetry the write-gold comparison pins.

#pragma once

#include <cstring>
#include <string>

namespace ILSpy::Tests {

// synth.baml (352 bytes)
inline constexpr char kSynthBamlHex[] =
    "0c0000004d005300420041004d004c0000006000000060000000600001000700"
    "000000141001700675726e3a70770200010002001b0d0675726e3a7077027077"
    "00001c2300001f50726573656e746174696f6e55492c2056657273696f6e3d34"
    "2e302e302e30200905000568656c6c6f1d0b00000000055379732e541e0f0100"
    "0000075379732e53657209001f0b0000000000044e616d652a2c07002b030000"
    "002f0100026531042e03000505000001762407000001760800060901000200de"
    "adbeef230400210009001006047465787411060274630800330305000f0d036c"
    "697411000000220000001209020005436c69636b34070369676e400035640000"
    "00070000003608000000070500210500050022050001000b0600320000380700"
    "00000c090800370355000a0d09000e081905016b010004251a00000026090500"
    "0000000000013001000031280100000000000000002903020000100301740402";

// The decoded fixture bytes.
inline std::string SynthBamlBytes() {
    std::string bytes;
    bytes.reserve(std::strlen(kSynthBamlHex) / 2);
    for (const char* p = kSynthBamlHex; p[0] && p[1]; p += 2) {
        int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
        int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
        bytes.push_back(static_cast<char>(hi * 16 + lo));
    }
    return bytes;
}

// the real writer output over the synth doc (352 bytes)
inline constexpr char kSynthBamlWriteGoldHex[] =
    "0c0000004d005300420041004d004c0000006000000060000000600001000700"
    "000000141001700675726e3a70770200010002001b0d0675726e3a7077027077"
    "00001c2300001f50726573656e746174696f6e55492c2056657273696f6e3d34"
    "2e302e302e30200905000568656c6c6f1d0b00000000055379732e541e0f0100"
    "0000075379732e53657209001f0b0000000000044e616d652a2c07002b030000"
    "002f0100026531042e03000505000001762407000001760800060901000200de"
    "adbeef230400210009001006047465787411060274630800330305000f0d036c"
    "69741100000022000000120905436c69636b020034070369676e400035640000"
    "00070000003608000000070500210500050022050001000b0600320000380700"
    "00000c090800370355000a0d09000e081905016b010004251a00000026090500"
    "0000000000013001000031280100000000000000002903020000100301740402";

// The decoded fixture bytes.
inline std::string SynthBamlWriteGoldBytes() {
    std::string bytes;
    bytes.reserve(std::strlen(kSynthBamlWriteGoldHex) / 2);
    for (const char* p = kSynthBamlWriteGoldHex; p[0] && p[1]; p += 2) {
        int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
        int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
        bytes.push_back(static_cast<char>(hi * 16 + lo));
    }
    return bytes;
}

} // namespace ILSpy::Tests
