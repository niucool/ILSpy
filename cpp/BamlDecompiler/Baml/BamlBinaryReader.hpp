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

// Port of the BamlBinaryReader half of ICSharpCode.BamlDecompiler/Baml/
// BamlReader.cs (Ki, 2015, MIT): the System.IO.BinaryReader the BAML record
// reads go through, over the resource blob's bytes (the byte-span convention,
// ResourcesFile's shape).
//
// The primitive semantics are the probed .NET 10 System.IO.BinaryReader
// behavior (the same pins the SingleFileBundle cursor carries):
//  * ReadByte/ReadUInt16/ReadUInt32 throw the EndOfStreamException message
//    ("Unable to read beyond the end of the stream.") when the payload is
//    truncated.
//  * Read7BitEncodedInt (the C# `ReadEncodedInt`) reads at most 5 bytes; a
//    6th continuation byte throws the FormatException message ("Too many
//    bytes in what should have been a 7-bit encoded integer."), and the
//    shift count masks to 5 bits so the 5-byte 0xFFFFFFFF0F prefix wraps
//    to -1.
//  * ReadString reads the 7-bit-encoded byte length, then that many raw
//    bytes. A negative (wrapped) length throws the IOException message
//    ("BinaryReader encountered an invalid string length of {n}
//    characters."); a truncated byte payload throws the EndOfStream
//    message. The bytes pass through verbatim (the port's std::string
//    holds the raw UTF-8 bytes; the C# decodes to UTF-16 and its writer
//    re-encodes, which is observably identical for the well-formed input
//    every real stream carries).
//  * ReadBytes(count) returns the bytes available up to count -- a
//    truncated read returns a SHORT vector without throwing (the C#
//    returns a short array; only ReadString and the fixed-size reads
//    throw). A negative count throws the ArgumentOutOfRangeException
//    message ("count ('{n}') must be a non-negative value. (Parameter
//    'count')\nActual value was {n}.").
//  * ReadChars(count) reads count UTF-16 code units -- the Encoding.Unicode
//    reader the signature block uses. It never throws for a short stream:
//    it returns the complete units the remaining bytes form (the odd tail
//    byte is dropped), which is why the signature check fails as a plain
//    mismatch (NotSupportedException) rather than an end-of-stream error.
//
// Exception mapping: every throw is std::out_of_range carrying the exact C#
// message (the port's established convention for the malformed-data family
// -- the C# XamlDecompiler lets all of these escape to the CLI's global
// catch).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

class BamlBinaryReader {
public:
    // The C# `new BinaryReader(stream)` over the blob's bytes: reads start
    // at offset 0 (every caller passes a whole .baml stream).
    BamlBinaryReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    // The C# BaseStream.Position (the reader tracks it; record Position
    // fields record it as a long).
    std::int64_t Position() const { return static_cast<std::int64_t>(position_); }

    // The C# `BaseStream.Position = pos`: seeks. The only seek-backs are
    // the defer-record patches, always to an already-written offset.
    void SetPosition(std::int64_t position);

    std::uint8_t ReadByte();
    std::uint16_t ReadUInt16();
    std::uint32_t ReadUInt32();
    bool ReadBoolean();

    // The C# `ReadEncodedInt` (BinaryReader.Read7BitEncodedInt).
    std::int32_t ReadEncodedInt();

    // The C# BinaryReader.ReadString (the default UTF-8 reader): the 7-bit
    // byte length, then that many raw bytes.
    std::string ReadString();

    // The C# BinaryReader.ReadBytes: at most `count` bytes, fewer when the
    // stream ends first (no throw). A negative count throws the
    // ArgumentOutOfRangeException message.
    std::vector<std::uint8_t> ReadBytes(std::int32_t count);

    // The C# BinaryReader(stream, Encoding.Unicode).ReadChars(count):
    // `count` UTF-16 code units as raw UTF-8-passed bytes (the port's
    // string), dropping an odd trailing byte. Short streams return the
    // complete units only (no throw).
    std::string ReadChars(std::size_t count);

private:
    void Need(std::size_t count) const;

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
};

} // namespace ILSpy::BamlDecompiler::Baml
