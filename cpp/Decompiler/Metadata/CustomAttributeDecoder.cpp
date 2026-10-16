// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// CustomAttributeDecoder.cs -- see the header's port conventions. The
// decoder template's bodies are inline in the header (every provider is a
// local of the driving function); this translation unit carries the
// BlobReader method definitions (the SRM System.Reflection.Metadata.BlobReader
// reading members, provider-independent).

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::Metadata {

std::uint16_t BlobReader::ReadUInt16() {
    if (size - pos < 2)
        throw std::invalid_argument(kReadOutOfBounds);
    std::uint16_t v = static_cast<std::uint16_t>(data[pos])
        | (static_cast<std::uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    return v;
}

std::uint8_t BlobReader::ReadByte() {
    if (pos >= size)
        throw std::invalid_argument(kReadOutOfBounds);
    return data[pos++];
}

// The C# `BlobReader.ReadCompressedIntegerOrInvalid()` -- the decompiled
// MemoryBlock.PeekCompressedInteger semantics: a 1-byte form for < 0x80, a
// 2-byte form under the 0x80-0xBF prefix (when 2 bytes remain), a 4-byte
// form under 0xC0-0xDF (when 4 remain); everything else (the reserved
// 0xE0-0xFF prefixes, truncation, end-of-blob) reads INVALID with ZERO
// bytes consumed (the cursor does not move -- the caller re-reads the byte,
// which is how ReadSerializedString's 0xFF null form works).
int BlobReader::ReadCompressedIntegerOrInvalid(int& bytesRead) {
    bytesRead = 0;
    if (pos >= size)
        return 0x7FFFFFFF;  // int.MaxValue
    std::uint8_t b = data[pos];
    long long remaining = static_cast<long long>(size - pos);
    if ((b & 0x80) == 0) {
        pos += 1;
        bytesRead = 1;
        return b;
    }
    if ((b & 0x40) == 0) {
        if (remaining >= 2) {
            pos += 2;
            bytesRead = 2;
            return (static_cast<int>(b & 0x3F) << 8) | data[pos - 1];
        }
    } else if ((b & 0x20) == 0 && remaining >= 4) {
        pos += 4;
        bytesRead = 4;
        return (static_cast<int>(b & 0x1F) << 24)
            | (static_cast<int>(data[pos - 3]) << 16)
            | (static_cast<int>(data[pos - 2]) << 8)
            | static_cast<int>(data[pos - 1]);
    }
    return 0x7FFFFFFF;
}

// The C# `BlobReader.ReadCompressedInteger()` -- the throwing form.
int BlobReader::ReadCompressedInteger() {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(bytesRead);
    if (value == 0x7FFFFFFF)
        throw std::invalid_argument(kInvalidCompressedInteger);
    return value;
}

// The C# `BlobReader.ReadSerializationTypeCode()`: the compressed integer as
// a type code -- a value over 255 (including INVALID) reads as Invalid.
int BlobReader::ReadSerializationTypeCode() {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(bytesRead);
    if (value > 255)
        return 0;  // SerializationTypeCode.Invalid
    return value;
}

// The C# `BlobReader.ReadSignatureTypeCode()`: the compressed integer as a
// signature element type -- the raw 17/18 (CLASS/VALUETYPE) both map to
// TypeHandle (64), a value over 255 (including INVALID) reads as Invalid
// (-1 here, an out-of-range int the caller's switch rejects).
int BlobReader::ReadSignatureTypeCode() {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(bytesRead);
    if (value == 17 || value == 18)
        return 64;  // SignatureTypeCode.TypeHandle
    if (value > 255)
        return -1;  // SignatureTypeCode.Invalid
    return value;
}

// The C# `BlobReader.ReadTypeHandle()`: the TypeDefOrRefOrSpec coded index.
// Returns the raw token (0 = the nil handle: tag 3, a zero row, or an
// invalid compressed integer).
std::uint32_t BlobReader::ReadTypeHandle() {
    int bytesRead;
    std::uint32_t num =
        static_cast<std::uint32_t>(ReadCompressedIntegerOrInvalid(bytesRead));
    static constexpr std::uint32_t kTables[4] = {
        0x02000000,  // TypeDef (tag 0)
        0x01000000,  // TypeRef (tag 1)
        0x1B000000,  // TypeSpec (tag 2)
        0,           // nil (tag 3)
    };
    std::uint32_t table = kTables[num & 3];
    if (num == 0x7FFFFFFF || table == 0)
        return 0;
    return table | (num >> 2);
}

// The C# `BlobReader.ReadSerializedString()`: the SerString -- a compressed
// length then that many UTF-8 bytes; a single 0xFF byte (an invalid compressed
// integer the Try rejects without consuming) reads as NULL.
std::optional<std::string> BlobReader::ReadSerializedString() {
    int bytesRead;
    int length = ReadCompressedIntegerOrInvalid(bytesRead);
    if (length != 0x7FFFFFFF) {
        if (length < 0
            || size - pos < static_cast<std::size_t>(length)) {
            // The C# ReadUTF8's bounds check.
            throw std::invalid_argument(kReadOutOfBounds);
        }
        std::string out(reinterpret_cast<const char*>(data + pos),
            static_cast<std::size_t>(length));
        pos += static_cast<std::size_t>(length);
        return out;
    }
    if (ReadByte() != 0xFF)
        throw std::invalid_argument(kInvalidSerializedString);
    return std::nullopt;
}

} // namespace ILSpy::Decompiler::Metadata
