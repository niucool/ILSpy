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
// BamlReader.cs (Ki, 2015, MIT): the System.IO.BinaryReader primitives the
// BAML record reads go through (see BamlBinaryReader.hpp for the probed
// .NET 10 semantics this implements).

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"

#include "Decompiler/Util/Utf.hpp"

#include <cstring>
#include <stdexcept>

namespace ILSpy::BamlDecompiler::Baml {

void BamlBinaryReader::Need(std::size_t count) const {
    if (position_ + count > size_)
        throw std::out_of_range("Unable to read beyond the end of the stream.");
}

void BamlBinaryReader::SetPosition(std::int64_t position) {
    position_ = static_cast<std::size_t>(position);
}

std::uint8_t BamlBinaryReader::ReadByte() {
    Need(1);
    return data_[position_++];
}

std::uint16_t BamlBinaryReader::ReadUInt16() {
    Need(2);
    std::uint16_t value = static_cast<std::uint16_t>(data_[position_])
        | static_cast<std::uint16_t>(data_[position_ + 1]) << 8;
    position_ += 2;
    return value;
}

std::uint32_t BamlBinaryReader::ReadUInt32() {
    Need(4);
    std::uint32_t value = static_cast<std::uint32_t>(data_[position_])
        | static_cast<std::uint32_t>(data_[position_ + 1]) << 8
        | static_cast<std::uint32_t>(data_[position_ + 2]) << 16
        | static_cast<std::uint32_t>(data_[position_ + 3]) << 24;
    position_ += 4;
    return value;
}

bool BamlBinaryReader::ReadBoolean() {
    return ReadByte() != 0;
}

std::int32_t BamlBinaryReader::ReadInt32() {
    Need(4);
    std::int32_t value = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data_[position_])
        | static_cast<std::uint32_t>(data_[position_ + 1]) << 8
        | static_cast<std::uint32_t>(data_[position_ + 2]) << 16
        | static_cast<std::uint32_t>(data_[position_ + 3]) << 24);
    position_ += 4;
    return value;
}

double BamlBinaryReader::ReadDouble() {
    Need(8);
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; i++)
        bits |= static_cast<std::uint64_t>(data_[position_ + i]) << (8 * i);
    position_ += 8;
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::int32_t BamlBinaryReader::ReadEncodedInt() {
    // The C# shift count masks to 5 bits (the shift operator's low bits),
    // so a 5-byte prefix wraps; the 6th continuation byte trips the shift
    // check (the probed FormatException message).
    std::uint32_t result = 0;
    int shift = 0;
    for (;;) {
        if (shift == 7 * 5)
            throw std::out_of_range(
                "Too many bytes in what should have been a 7-bit encoded integer.");
        std::uint8_t b = ReadByte();
        result |= static_cast<std::uint32_t>(b & 0x7F) << (shift & 31);
        shift += 7;
        if ((b & 0x80) == 0)
            break;
    }
    return static_cast<std::int32_t>(result);
}

std::string BamlBinaryReader::ReadString() {
    std::int32_t length = ReadEncodedInt();
    if (length < 0) {
        throw std::out_of_range(
            "BinaryReader encountered an invalid string length of "
            + std::to_string(length) + " characters.");
    }
    Need(static_cast<std::size_t>(length));
    std::string value(reinterpret_cast<const char*>(data_ + position_), length);
    position_ += static_cast<std::size_t>(length);
    return value;
}

std::vector<std::uint8_t> BamlBinaryReader::ReadBytes(std::int32_t count) {
    if (count < 0) {
        // The probed ArgumentOutOfRangeException message.
        std::string value = "count ('" + std::to_string(count)
            + "') must be a non-negative value. (Parameter 'count')\nActual value was "
            + std::to_string(count) + ".";
        throw std::out_of_range(value);
    }
    std::size_t available = size_ - (position_ < size_ ? position_ : size_);
    std::size_t take = std::min(static_cast<std::size_t>(count), available);
    std::vector<std::uint8_t> value(data_ + position_, data_ + position_ + take);
    position_ += take;
    return value;
}

std::string BamlBinaryReader::ReadChars(std::size_t count) {
    // The Encoding.Unicode reader: count UTF-16 code units. The C# reads
    // into a char buffer in chunks and returns whatever it decoded when
    // the stream ends (no throw), so the port reads the complete units
    // the remaining bytes form and drops the odd tail byte.
    std::size_t available = size_ - (position_ < size_ ? position_ : size_);
    std::size_t take = std::min(count * 2, available) & ~std::size_t{1};
    std::u16string units;
    units.resize(take / 2);
    for (std::size_t i = 0; i < units.size(); i++) {
        units[i] = static_cast<char16_t>(
            static_cast<std::uint16_t>(data_[position_ + 2 * i])
            | static_cast<std::uint16_t>(data_[position_ + 2 * i + 1]) << 8);
    }
    position_ += take;
    return ILSpy::Decompiler::Util::Utf16ToUtf8(units);
}

} // namespace ILSpy::BamlDecompiler::Baml
