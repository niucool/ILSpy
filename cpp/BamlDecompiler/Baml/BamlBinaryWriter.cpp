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
// BamlWriter.cs (Ki, 2015, MIT): the System.IO.BinaryWriter primitives the
// BAML record writes and the defer patches go through.

#include "BamlDecompiler/Baml/BamlBinaryWriter.hpp"

#include "Decompiler/Util/Utf.hpp"

namespace ILSpy::BamlDecompiler::Baml {

void BamlBinaryWriter::SetPosition(std::int64_t position) {
    position_ = static_cast<std::size_t>(position);
}

void BamlBinaryWriter::WriteRaw(std::uint8_t value) {
    if (position_ < out_.size()) {
        out_[position_] = value;
    } else {
        // The MemoryStream semantics of writing at a past-the-end position:
        // the gap fills with zeros (unreachable through this layer's own
        // callers, which only seek backward to written offsets).
        out_.resize(position_, 0);
        out_.push_back(value);
    }
    position_++;
}

void BamlBinaryWriter::WriteByte(std::uint8_t value) {
    WriteRaw(value);
}

void BamlBinaryWriter::WriteUInt16(std::uint16_t value) {
    WriteRaw(static_cast<std::uint8_t>(value));
    WriteRaw(static_cast<std::uint8_t>(value >> 8));
}

void BamlBinaryWriter::WriteUInt32(std::uint32_t value) {
    WriteRaw(static_cast<std::uint8_t>(value));
    WriteRaw(static_cast<std::uint8_t>(value >> 8));
    WriteRaw(static_cast<std::uint8_t>(value >> 16));
    WriteRaw(static_cast<std::uint8_t>(value >> 24));
}

void BamlBinaryWriter::WriteBoolean(bool value) {
    WriteRaw(value ? 1 : 0);
}

void BamlBinaryWriter::WriteEncodedInt(std::int32_t value) {
    // The C# Write7BitEncodedInt over a non-negative int.
    std::uint32_t v = static_cast<std::uint32_t>(value);
    for (;;) {
        std::uint8_t b = static_cast<std::uint8_t>(v & 0x7F);
        v >>= 7;
        if (v != 0) {
            WriteRaw(static_cast<std::uint8_t>(b | 0x80));
        } else {
            WriteRaw(b);
            return;
        }
    }
}

void BamlBinaryWriter::WriteString(const std::string& value) {
    WriteEncodedInt(static_cast<std::int32_t>(value.size()));
    for (char c : value)
        WriteRaw(static_cast<std::uint8_t>(c));
}

void BamlBinaryWriter::WriteBytes(const std::vector<std::uint8_t>& value) {
    for (std::uint8_t b : value)
        WriteRaw(b);
}

void BamlBinaryWriter::WriteChars(const std::string& value) {
    std::u16string units = ILSpy::Decompiler::Util::Utf8ToUtf16(value);
    for (char16_t c : units) {
        WriteRaw(static_cast<std::uint8_t>(static_cast<std::uint16_t>(c)));
        WriteRaw(static_cast<std::uint8_t>(static_cast<std::uint16_t>(c) >> 8));
    }
}

} // namespace ILSpy::BamlDecompiler::Baml
