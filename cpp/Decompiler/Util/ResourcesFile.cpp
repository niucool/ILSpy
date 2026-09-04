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
#include <cstring>
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
        throw EndOfStreamError(
            "Unable to read beyond the end of the stream.");
    return data_[pos_++];
}

std::int32_t ResourcesFile::ReadInt32() {
    // The C# EndOfStreamException message of a BinaryReader read past the
    // end ("Unable to read beyond the end of the stream.").
    if (size_ - pos_ < 4)
        throw EndOfStreamError("Unable to read beyond the end of the stream.");
    std::int32_t v = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data_[pos_])
        | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8)
        | (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16)
        | (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24));
    pos_ += 4;
    return v;
}

std::int16_t ResourcesFile::ReadInt16() {
    if (size_ - pos_ < 2)
        throw EndOfStreamError("Unable to read beyond the end of the stream.");
    std::int16_t v = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(data_[pos_])
        | (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8));
    pos_ += 2;
    return v;
}

std::uint16_t ResourcesFile::ReadUInt16() {
    if (size_ - pos_ < 2)
        throw EndOfStreamError("Unable to read beyond the end of the stream.");
    std::uint16_t v = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data_[pos_])
        | (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8));
    pos_ += 2;
    return v;
}

std::uint32_t ResourcesFile::ReadUInt32() {
    return static_cast<std::uint32_t>(ReadInt32());
}

std::int64_t ResourcesFile::ReadInt64() {
    if (size_ - pos_ < 8)
        throw EndOfStreamError("Unable to read beyond the end of the stream.");
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | data_[pos_ + static_cast<std::size_t>(i)];
    pos_ += 8;
    return static_cast<std::int64_t>(v);
}

std::uint64_t ResourcesFile::ReadUInt64() {
    return static_cast<std::uint64_t>(ReadInt64());
}

bool ResourcesFile::ReadBoolean() {
    // The C# BinaryReader.ReadBoolean: a nonzero byte is true.
    return ReadByte() != 0;
}

float ResourcesFile::ReadSingle() {
    std::uint32_t bits = ReadUInt32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

double ResourcesFile::ReadDouble() {
    std::uint64_t bits = ReadUInt64();
    double v;
    std::memcpy(&v, &bits, sizeof(v));
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
        throw EndOfStreamError("Unable to read beyond the end of the stream.");
    std::string s;
    if (len > 0) {
        s.assign(reinterpret_cast<const char*>(data_ + pos_),
            static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
    }
    return s;
}

std::vector<std::uint8_t> ResourcesFile::ReadBytes(std::size_t count) {
    // The C# BinaryReader.ReadBytes: a partial read at the end of the
    // stream returns what is available (no throw).
    std::size_t take = std::min(count, size_ - pos_);
    std::vector<std::uint8_t> v(data_ + pos_, data_ + pos_ + take);
    pos_ += take;
    return v;
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

std::string ResourcesFile::FindType(int typeIndex) {
    // The C# FindType: the type-table entry with its
    // BadImageFormatException("Type index out of bounds") arm (a plain
    // throw -- it passes through LoadObject's EndOfStream wrap untouched).
    if (typeIndex < 0
        || typeIndex >= static_cast<int>(typeTable_.size()))
        throw std::out_of_range("Type index out of bounds");
    return typeTable_[static_cast<std::size_t>(typeIndex)];
}

ResourceValue ResourcesFile::LoadObject(int dataOffset) {
    // The C# LoadObject: the version dispatch with the
    // catch (EndOfStreamException) wrap -- the EndOfStream arms of the
    // decode's reads rethrow as BadImageFormatException("Invalid resource
    // file"); the BadImageFormatException arms (FindType, the negative
    // lengths, the invalid type code) are a different C# type and pass
    // through untouched (the port's plain std::out_of_range throws).
    try {
        if (version_ == 1)
            return LoadObjectV1(dataOffset);
        return LoadObjectV2(dataOffset);
    } catch (const EndOfStreamError&) {
        throw std::out_of_range("Invalid resource file");
    }
}

ResourceValue ResourcesFile::LoadObjectV1(int dataOffset) {
    // The C# LoadObjectV1: the type-table INDEX (not a ResourceTypeCode),
    // the -1 null marker, the assembly-name strip (the first comma), and
    // the type-name switch.
    SeekBegin(static_cast<std::int64_t>(dataSectionPosition_) + dataOffset);
    int typeIndex = Read7BitEncodedInt();
    if (typeIndex == -1)
        return ResourceValue{};  // the null marker
    std::string typeName = FindType(typeIndex);
    std::string localTypeName = typeName;
    int comma = static_cast<int>(typeName.find(','));
    if (comma > 0)
        localTypeName = typeName.substr(0, static_cast<std::size_t>(comma));

    ResourceValue v;
    if (localTypeName == "System.String") {
        v.kind = ResourceValue::Kind::String;
        v.str = ReadString();
    } else if (localTypeName == "System.Byte") {
        v.kind = ResourceValue::Kind::Byte;
        v.integer = ReadByte();
    } else if (localTypeName == "System.SByte") {
        v.kind = ResourceValue::Kind::SByte;
        v.integer = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int8_t>(ReadByte())));
    } else if (localTypeName == "System.Int16") {
        v.kind = ResourceValue::Kind::Int16;
        v.integer = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(ReadInt16()));
    } else if (localTypeName == "System.UInt16") {
        v.kind = ResourceValue::Kind::UInt16;
        v.integer = ReadUInt16();
    } else if (localTypeName == "System.Int32") {
        v.kind = ResourceValue::Kind::Int32;
        v.integer = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(ReadInt32()));
    } else if (localTypeName == "System.UInt32") {
        v.kind = ResourceValue::Kind::UInt32;
        v.integer = ReadUInt32();
    } else if (localTypeName == "System.Int64") {
        v.kind = ResourceValue::Kind::Int64;
        v.integer = static_cast<std::uint64_t>(ReadInt64());
    } else if (localTypeName == "System.UInt64") {
        v.kind = ResourceValue::Kind::UInt64;
        v.integer = ReadUInt64();
    } else if (localTypeName == "System.Single") {
        v.kind = ResourceValue::Kind::Single;
        v.single = ReadSingle();
    } else if (localTypeName == "System.Double") {
        v.kind = ResourceValue::Kind::Double;
        v.doubleValue = ReadDouble();
    } else if (localTypeName == "System.DateTime") {
        // The C# `new DateTime(reader.ReadInt64())`: the raw int64 with
        // the kind bits masked off for the Ticks the render reads (the C#
        // ctor's ArgumentOutOfRangeException for an out-of-range ticks
        // value is an edge the fixtures do not carry).
        v.kind = ResourceValue::Kind::DateTime;
        std::uint64_t raw = static_cast<std::uint64_t>(ReadInt64());
        v.ticks = static_cast<std::int64_t>(raw & 0x3FFFFFFFFFFFFFFFull);
    } else if (localTypeName == "System.TimeSpan") {
        v.kind = ResourceValue::Kind::TimeSpan;
        v.ticks = ReadInt64();
    } else if (localTypeName == "System.Decimal") {
        // The C# `new decimal(bits)`: the four int32s in the ReadDecimal
        // order [lo, mid, hi, flags].
        v.kind = ResourceValue::Kind::Decimal;
        for (int i = 0; i < 4; i++)
            v.decimalBits[i] = ReadUInt32();
    } else {
        // The C# default arm: the serialized user type. The position is
        // after the type index; the port computes the byte region eagerly
        // (the header contract's documented divergence). FindType runs
        // unconditionally (its bounds check is the ctor argument the C#
        // evaluates regardless of the format); the ResourceSerializedObject
        // ctor nulls the TypeName when the container is not a
        // serialization-format one.
        v.kind = ResourceValue::Kind::SerializedObject;
        std::string typeName = FindType(typeIndex);
        v.typeName = usesSerializationFormat_ ? std::move(typeName)
                                               : std::string();
        v.bytes = GetBytesForSerializedObject(
            pos_, usesSerializationFormat_);
    }
    return v;
}

ResourceValue ResourcesFile::LoadObjectV2(int dataOffset) {
    // The C# LoadObjectV2: the ResourceTypeCode switch.
    SeekBegin(static_cast<std::int64_t>(dataSectionPosition_) + dataOffset);
    std::int32_t raw = Read7BitEncodedInt();
    ResourceTypeCode typeCode = static_cast<ResourceTypeCode>(raw);

    ResourceValue v;
    switch (typeCode) {
        case ResourceTypeCode::Null:
            return v;
        case ResourceTypeCode::String:
            v.kind = ResourceValue::Kind::String;
            v.str = ReadString();
            return v;
        case ResourceTypeCode::Boolean:
            v.kind = ResourceValue::Kind::Boolean;
            v.boolean = ReadBoolean();
            return v;
        case ResourceTypeCode::Char:
            v.kind = ResourceValue::Kind::Char;
            v.character = ReadUInt16();
            return v;
        case ResourceTypeCode::Byte:
            v.kind = ResourceValue::Kind::Byte;
            v.integer = ReadByte();
            return v;
        case ResourceTypeCode::SByte:
            v.kind = ResourceValue::Kind::SByte;
            v.integer = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(static_cast<std::int8_t>(ReadByte())));
            return v;
        case ResourceTypeCode::Int16:
            v.kind = ResourceValue::Kind::Int16;
            v.integer = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(ReadInt16()));
            return v;
        case ResourceTypeCode::UInt16:
            v.kind = ResourceValue::Kind::UInt16;
            v.integer = ReadUInt16();
            return v;
        case ResourceTypeCode::Int32:
            v.kind = ResourceValue::Kind::Int32;
            v.integer = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(ReadInt32()));
            return v;
        case ResourceTypeCode::UInt32:
            v.kind = ResourceValue::Kind::UInt32;
            v.integer = ReadUInt32();
            return v;
        case ResourceTypeCode::Int64:
            v.kind = ResourceValue::Kind::Int64;
            v.integer = static_cast<std::uint64_t>(ReadInt64());
            return v;
        case ResourceTypeCode::UInt64:
            v.kind = ResourceValue::Kind::UInt64;
            v.integer = ReadUInt64();
            return v;
        case ResourceTypeCode::Single:
            v.kind = ResourceValue::Kind::Single;
            v.single = ReadSingle();
            return v;
        case ResourceTypeCode::Double:
            v.kind = ResourceValue::Kind::Double;
            v.doubleValue = ReadDouble();
            return v;
        case ResourceTypeCode::Decimal:
            v.kind = ResourceValue::Kind::Decimal;
            for (int i = 0; i < 4; i++)
                v.decimalBits[i] = ReadUInt32();
            return v;
        case ResourceTypeCode::DateTime: {
            // The C# `DateTime.FromBinary(data)`: the kind bits (the two
            // high bits) are masked off for the ticks the render reads; the
            // Local-kind timezone conversion and the out-of-range ticks
            // throw are edges the fixtures do not carry.
            v.kind = ResourceValue::Kind::DateTime;
            std::uint64_t data = static_cast<std::uint64_t>(ReadInt64());
            v.ticks = static_cast<std::int64_t>(data & 0x3FFFFFFFFFFFFFFFull);
            return v;
        }
        case ResourceTypeCode::TimeSpan:
            v.kind = ResourceValue::Kind::TimeSpan;
            v.ticks = ReadInt64();
            return v;
        case ResourceTypeCode::ByteArray: {
            std::int32_t len = ReadInt32();
            if (len < 0)
                throw std::out_of_range("Resource with negative length");
            v.kind = ResourceValue::Kind::ByteArray;
            v.bytes = ReadBytes(static_cast<std::size_t>(len));
            return v;
        }
        case ResourceTypeCode::Stream: {
            std::int32_t len = ReadInt32();
            if (len < 0)
                throw std::out_of_range("Resource with negative length");
            v.kind = ResourceValue::Kind::Stream;
            v.bytes = ReadBytes(static_cast<std::size_t>(len));
            return v;
        }
        default:
            if (raw < static_cast<std::int32_t>(ResourceTypeCode::StartOfUserTypes))
                throw std::out_of_range("Invalid typeCode");
            // The serialized user type: the position is after the type
            // code; the port computes the byte region eagerly (the header
            // contract's documented divergence). FindType runs
            // unconditionally (its bounds check is the ctor argument the
            // C# evaluates regardless of the format); the
            // ResourceSerializedObject ctor nulls the TypeName when the
            // container is not a serialization-format one.
            v.kind = ResourceValue::Kind::SerializedObject;
            {
                std::string typeName = FindType(
                    raw - static_cast<std::int32_t>(
                        ResourceTypeCode::StartOfUserTypes));
                v.typeName = usesSerializationFormat_
                    ? std::move(typeName)
                    : std::string();
            }
            v.bytes = GetBytesForSerializedObject(
                pos_, usesSerializationFormat_);
            return v;
    }
}

std::vector<std::int64_t> ResourcesFile::GetStartPositions() {
    // The C# GetStartPositions (the LazyInit cache is unobservable -- the
    // same values every time): the sorted absolute starts of every name
    // entry and every data entry.
    std::vector<std::int64_t> positions;
    positions.reserve(static_cast<std::size_t>(numResources_) * 2);
    for (int i = 0; i < numResources_; i++) {
        positions.push_back(
            static_cast<std::int64_t>(nameSectionPosition_)
            + namePositions_[static_cast<std::size_t>(i)]);
        positions.push_back(
            static_cast<std::int64_t>(dataSectionPosition_)
            + GetResourceDataOffset(i));
    }
    std::sort(positions.begin(), positions.end());
    return positions;
}

std::vector<std::uint8_t> ResourcesFile::GetBytesForSerializedObject(
    std::size_t pos, bool usesSerializationFormat) {
    std::vector<std::int64_t> positions = GetStartPositions();
    // The C# Array.BinarySearch: an exact hit keeps the found index (the
    // zero-length region -- endPos == pos), a miss takes the insertion
    // index (the next position after pos); std::lower_bound gives both.
    std::size_t i = static_cast<std::size_t>(
        std::lower_bound(positions.begin(), positions.end(),
            static_cast<std::int64_t>(pos))
        - positions.begin());
    std::int64_t endPos = (i == positions.size())
        ? static_cast<std::int64_t>(size_)
        : positions[i];
    std::int64_t len = endPos - static_cast<std::int64_t>(pos);
    SeekBegin(static_cast<std::int64_t>(pos));
    if (usesSerializationFormat) {
        // The [SerializationFormat kind][length] wrapper. The walk runs
        // OUTSIDE the C# LoadObject EndOfStream wrap (the C# GetBytes is
        // the caller's separate lazy call), so its EndOfStream arms convert
        // to the plain type here to escape the wrap with the same message.
        try {
            Read7BitEncodedInt();  // the kind (a Debug.Assert in the C#)
            len = Read7BitEncodedInt();
        } catch (const EndOfStreamError&) {
            throw std::out_of_range(
                "Unable to read beyond the end of the stream.");
        }
    }
    // A negative length (an overlapping region only a crafted container
    // produces) reads as an empty region -- the C# ReadBytes would throw
    // ArgumentOutOfRangeException; a documented divergence no
    // well-formed container reaches.
    if (len < 0)
        len = 0;
    return ReadBytes(static_cast<std::size_t>(len));
}

ResourceValue ResourcesFile::GetResourceValue(int index) {
    int dataOffset;
    GetResourceName(index, dataOffset);
    return LoadObject(dataOffset);
}

}  // namespace ILSpy::Decompiler::Util
