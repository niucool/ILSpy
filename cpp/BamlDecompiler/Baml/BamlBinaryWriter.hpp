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

// Port of the BamlBinaryWriter half of ICSharpCode.BamlDecompiler/Baml/
// BamlWriter.cs (Ki, 2015, MIT): the System.IO.BinaryWriter the BAML record
// writes and the defer-record patches go through, over a growable byte
// vector standing in for the C# Stream.
//
// The writer keeps its own position: WriteDocument's defer pass seeks
// backward (the C# `BaseStream.Seek(pos, SeekOrigin.Begin)`) to patch the
// size/offset placeholders, so writes at an already-written offset
// overwrite in place and writes at the end append.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

class BamlBinaryWriter {
public:
    explicit BamlBinaryWriter(std::vector<std::uint8_t>& out)
        : out_(out) {}

    std::int64_t Position() const { return static_cast<std::int64_t>(position_); }

    // The C# `BaseStream.Seek(pos, SeekOrigin.Begin)` / `BaseStream.Position
    // = pos`: seeks within the written bytes.
    void SetPosition(std::int64_t position);

    void WriteByte(std::uint8_t value);
    void WriteUInt16(std::uint16_t value);
    void WriteUInt32(std::uint32_t value);
    void WriteBoolean(bool value);

    // The C# `WriteEncodedInt` (BinaryWriter.Write7BitEncodedInt): the
    // 7-bit encoding of a non-negative int.
    void WriteEncodedInt(std::int32_t value);

    // The C# BinaryWriter.Write(string) (the default UTF-8 writer): the
    // 7-bit-encoded byte length, then the raw bytes.
    void WriteString(const std::string& value);

    void WriteBytes(const std::vector<std::uint8_t>& value);

    // The signature block's `BinaryWriter(str, Encoding.Unicode).Write(char[])`:
    // the raw UTF-16LE bytes of the string's UTF-16 form.
    void WriteChars(const std::string& value);

private:
    void WriteRaw(std::uint8_t value);

    std::vector<std::uint8_t>& out_;
    std::size_t position_ = 0;
};

} // namespace ILSpy::BamlDecompiler::Baml
