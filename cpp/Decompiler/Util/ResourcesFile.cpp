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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The .resources container reader implementation (ResourcesFile.hpp has the
// full contract): the ResourceManager/RuntimeResourceSet header parse and
// the entry-name listing, the C# BadImageFormatException/EndOfStreamException
// arms rendered as std::out_of_range with the exact C# messages.

#include "Decompiler/Util/ResourcesFile.hpp"

#include "Decompiler/Util/Utf.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ILSpy::Decompiler::Util {

// The C# `const string ResourcesHeaderCorrupted`.
static constexpr const char* kResourcesHeaderCorrupted =
    "Resources header corrupted.";

std::uint8_t ResourcesFile::ReadByte() {
    // The C# EndOfStreamException message of a BinaryReader read past the
    // end (SR.IO_ReadBeyondEndOfFile).
    if (pos_ >= size_)
        throw std::out_of_range(
            "Unable to read beyond the end of the stream.");
    return data_[pos_++];
}

std::int32_t ResourcesFile::ReadInt32() {
    // The C# EndOfStreamException message of a BinaryReader read past the
    // end ("Unable to read beyond the end of the stream.").
    if (size_ - pos_ < 4)
        throw std::out_of_range("Unable to read beyond the end of the stream.");
    std::int32_t v = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data_[pos_])
        | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8)
        | (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16)
        | (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24));
    pos_ += 4;
    return v;
}

std::int32_t ResourcesFile::Read7BitEncodedInt() {
    // The C# BinaryReader.Read7BitEncodedInt (.NET Core's shape): 7 payload
    // bits per byte, continuing while the high bit is set. The C# shift
    // operator takes the count's low 5 bits, so the shift beyond the 5th
    // byte wraps -- reproduced with the same mask (a length that long
    // cannot survive the read bounds checks either way).
    std::uint32_t result = 0;
    int shift = 0;
    std::uint8_t b;
    do {
        b = ReadByte();
        result |= static_cast<std::uint32_t>(b & 0x7Fu) << (shift & 31);
        shift += 7;
    } while ((b & 0x80) != 0);
    return static_cast<std::int32_t>(result);
}

std::string ResourcesFile::ReadString() {
    // The C# BinaryReader.ReadString: a 7-bit-encoded byte length then that
    // many UTF-8 bytes (the port's strings are UTF-8, so the bytes pass
    // through). A truncated string throws (the C# EndOfStreamException).
    std::int32_t len = Read7BitEncodedInt();
    if (len < 0 || static_cast<std::uint64_t>(len) > size_ - pos_)
        throw std::out_of_range("Unable to read beyond the end of the stream.");
    std::string s;
    if (len > 0) {
        s.assign(reinterpret_cast<const char*>(data_ + pos_),
            static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
    }
    return s;
}

void ResourcesFile::ReadExact(std::uint8_t* out, std::size_t n) {
    // The C# blocking `reader.Read(bytes, byteLen - count, count)` loop:
    // a read may return PARTIAL data (the C# MemoryStream.Read returns
    // whatever is available); only a ZERO-return read is the corrupted
    // end-of-stream arm (not the BinaryReader's EndOfStreamException).
    std::size_t read = 0;
    while (read < n) {
        std::size_t avail = size_ - pos_;
        if (avail == 0)
            throw std::out_of_range(
                "End of stream within a resource name");
        std::size_t take = std::min(avail, n - read);
        for (std::size_t i = 0; i < take; i++) out[read + i] = data_[pos_ + i];
        pos_ += take;
        read += take;
    }
}

void ResourcesFile::SeekCurrent(std::int64_t delta) {
    // The C# seeks the underlying MemoryStream, where a position past the
    // end is legal; the clamp to the end is behavior-equivalent (every read
    // at or past the end throws).
    std::int64_t p = static_cast<std::int64_t>(pos_) + delta;
    if (p < 0) p = 0;
    if (p > static_cast<std::int64_t>(size_)) p = static_cast<std::int64_t>(size_);
    pos_ = static_cast<std::size_t>(p);
}

void ResourcesFile::SeekBegin(std::int64_t pos) {
    std::int64_t p = pos;
    if (p < 0) p = 0;
    if (p > static_cast<std::int64_t>(size_)) p = static_cast<std::int64_t>(size_);
    pos_ = static_cast<std::size_t>(p);
}

ResourcesFile::ResourcesFile(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {
    // The C# `fileStartPosition = stream.Position` -- the span starts at
    // the blob, so every position below is relative to data_.

    // Read ResourceManager header: check for the magic number.
    std::int32_t magicNum = ReadInt32();
    if (magicNum != static_cast<std::int32_t>(static_cast<std::uint32_t>(0xBEEFCACE)))
        throw std::out_of_range("Not a .resources file - invalid magic number");
    // Assuming this is ResourceManager header V1 or greater, hopefully
    // after the version number there is a number of bytes to skip to
    // bypass the rest of the ResMgr header.
    std::int32_t resMgrHeaderVersion = ReadInt32();
    std::int32_t numBytesToSkip = ReadInt32();
    if (numBytesToSkip < 0 || resMgrHeaderVersion < 0)
        throw std::out_of_range(kResourcesHeaderCorrupted);
    if (resMgrHeaderVersion > 1) {
        SeekCurrent(numBytesToSkip);
    } else {
        // We don't care about numBytesToSkip; read the rest of the header.

        // readerType:
        std::string readerType = ReadString();
        usesSerializationFormat_ = readerType ==
            "System.Resources.Extensions.DeserializingResourceReader, "
            "System.Resources.Extensions, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=cc7b13ffcd2ddd51";
        // resourceSetType:
        ReadString();
    }

    // Read RuntimeResourceSet header: do the file version check.
    version_ = ReadInt32();
    if (version_ != 2 && version_ != 1)
        throw std::out_of_range(
            "Unsupported resource set version: " + std::to_string(version_));

    numResources_ = ReadInt32();
    if (numResources_ < 0)
        throw std::out_of_range(kResourcesHeaderCorrupted);

    // Read type positions into type positions array.
    // But delay initialize the type table.
    std::int32_t numTypes = ReadInt32();
    if (numTypes < 0)
        throw std::out_of_range(kResourcesHeaderCorrupted);
    typeTable_.reserve(static_cast<std::size_t>(numTypes));
    for (std::int32_t i = 0; i < numTypes; i++) {
        typeTable_.push_back(ReadString());
    }

    // Prepare to read in the array of name hashes.  Note: the name hashes
    // array is aligned to 8 bytes so we can use pointers into it on 64 bit
    // machines.  Skip over alignment stuff.  All public .resources files
    // should be aligned.  No need to verify the byte values.
    {
        std::uint64_t pos = pos_;  // the C# `pos - fileStartPosition`
        std::int32_t alignBytes = static_cast<std::int32_t>(pos & 7);
        if (alignBytes != 0) {
            for (std::int32_t i = 0; i < 8 - alignBytes; i++) {
                ReadByte();
            }
        }
    }

    // Skip over the array of name hashes.  The C# wraps the skip in
    // checked arithmetic (4 * numResources overflowing int32 throws
    // OverflowException, rethrown as the corrupted-header
    // BadImageFormatException); the seek itself may pass the end (the
    // name-position reads below then throw).
    if (numResources_ > 0
        && static_cast<std::uint32_t>(numResources_)
            > std::numeric_limits<std::int32_t>::max() / 4u) {
        throw std::out_of_range(kResourcesHeaderCorrupted);
    }
    SeekCurrent(static_cast<std::int64_t>(4) * numResources_);

    // Read in the array of relative positions for all the names.
    namePositions_.resize(static_cast<std::size_t>(numResources_));
    for (std::int32_t i = 0; i < numResources_; i++) {
        std::int32_t namePosition = ReadInt32();
        if (namePosition < 0)
            throw std::out_of_range(kResourcesHeaderCorrupted);
        namePositions_[static_cast<std::size_t>(i)] = namePosition;
    }

    // Read location of data section.
    std::int32_t dataSectionOffset = ReadInt32();
    if (dataSectionOffset < 0)
        throw std::out_of_range(kResourcesHeaderCorrupted);

    // Store current location as start of name section.
    nameSectionPosition_ = pos_;
    dataSectionPosition_ = static_cast<std::size_t>(
        static_cast<std::uint64_t>(static_cast<std::int64_t>(dataSectionOffset)));

    // _nameSectionOffset should be <= _dataSectionOffset; if not, it's corrupt
    if (dataSectionPosition_ < nameSectionPosition_)
        throw std::out_of_range(kResourcesHeaderCorrupted);
}

std::string ResourcesFile::GetResourceName(int index) {
    int dataOffset;
    return GetResourceName(index, dataOffset);
}

int ResourcesFile::GetResourceDataOffset(int index) {
    int dataOffset;
    GetResourceName(index, dataOffset);
    return dataOffset;
}

std::string ResourcesFile::GetResourceName(int index, int& dataOffset) {
    if (index < 0 || index >= numResources_)
        throw std::out_of_range("resource index out of range");
    std::int64_t pos = static_cast<std::int64_t>(nameSectionPosition_)
        + namePositions_[static_cast<std::size_t>(index)];
    SeekBegin(pos);
    // Can't use reader.ReadString, since it's using UTF-8!
    std::int32_t byteLen = Read7BitEncodedInt();
    if (byteLen < 0)
        throw std::out_of_range("Resource name has negative length");
    // We must read byteLen bytes, or we have a corrupted file.  Use a
    // blocking read in case the stream doesn't give us back everything
    // immediately.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteLen));
    ReadExact(bytes.data(), bytes.size());
    dataOffset = ReadInt32();
    if (dataOffset < 0)
        throw std::out_of_range("Negative data offset");

    // Encoding.Unicode.GetString(bytes): the UTF-16LE units, with an odd
    // trailing byte decoding as U+FFFD (the decoder's end-of-input
    // behavior).
    std::size_t units = bytes.size() / 2;
    std::u16string text(units, u'\0');
    for (std::size_t i = 0; i < units; i++) {
        text[i] = static_cast<char16_t>(
            bytes[i * 2] | (static_cast<std::uint16_t>(bytes[i * 2 + 1]) << 8));
    }
    std::string result = Utf16ToUtf8(text);
    if (bytes.size() % 2 != 0) {
        // The odd trailing byte: a lone low byte decodes as U+FFFD (its
        // UTF-8 encoding, EF BF BD).
        result.append("\xEF\xBF\xBD");
    }
    return result;
}

}  // namespace ILSpy::Decompiler::Util
