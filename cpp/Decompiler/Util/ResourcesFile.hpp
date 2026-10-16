// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Util/ResourcesFile.cs (Daniel Grunwald,
// 2018, based on the .NET Core ResourceReader, made available under the
// MIT license by the .NET Foundation) -- the .resources container format
// reader (the ResourceManager header V1/V2 + RuntimeResourceSet V1/V2
// layouts).
// This slice ports the whole file: the header parse, the entry-name
// listing, and the entry-value decode (LoadObjectV1/V2 over the type
// table / ResourceTypeCode, and the ResourceSerializedObject byte walk
// GetBytesForSerializedObject) -- the surface the CLI's --list-resources
// and --resource paths consume.
//
// C#-to-C++ porting decisions:
//  * The C# ctor takes a seekable Stream; the port takes the resource
//    blob's bytes directly (the byte-span convention, PortablePdb's shape)
//    -- the caller reads the blob out of the assembly (MetadataFile::
//    TryGetManifestResourceData) and the reader never copies it.
//  * Every C# BadImageFormatException and EndOfStreamException arm maps to
//    std::out_of_range carrying the exact C# message (the ILParser
//    truncated-operand convention): the CLI's catch (the C#
//    `ex is BadImageFormatException || ex is EndOfStreamException` filter)
//    treats the two identically, so the port's single exception type
//    preserves every observable behavior.
//  * The C# `object?` GetResourceValue returns ports as ResourceValue (the
//    tagged value below): the C# value's runtime kind plus its payload.
//    The C# ResourceSerializedObject reads its bytes lazily through the
//    live ResourcesFile (GetBytes on the caller's next call); the port has
//    no way to keep the file alive past GetResourceValue, so the
//    SerializedObject kind carries the bytes computed eagerly at
//    GetResourceValue time -- observably identical through every caller
//    that reads the bytes (the bytes are the same read, and the CLI calls
//    GetBytes immediately), with one divergence: the eager read can throw
//    for a malformed serialized region where the C# would not (a caller
//    that ignores the value); the walk's own EndOfStream arms convert
//    their throws to the plain type so they escape LoadObject's wrap
//    exactly like the C#'s lazy call site does.
//  * The C# reads through a BinaryReader over a MemoryStream, where a Seek
//    past the end succeeds and only a subsequent read throws. The port's
//    Seek clamps a past-the-end position to the end of the span, which is
//    observably equivalent: every read at or past the end throws exactly
//    where the C#'s EndOfStreamException would.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {

// The C# `System.Resources.ResourceTypeCode` internal enum (the .NET 10
// values, probed from System.Private.CoreLib): the LoadObjectV2 type codes
// read as the 7-bit encoded int after each entry's data offset. Null is 0
// on modern .NET (the .NET Framework -1 survives only as the V1 type-index
// null marker); LastPrimitive (16, TimeSpan's alias) is not distinct enough
// to matter to any decode arm.
enum class ResourceTypeCode : std::int32_t {
    Null = 0,
    String = 1,
    Boolean = 2,
    Char = 3,
    Byte = 4,
    SByte = 5,
    Int16 = 6,
    UInt16 = 7,
    Int32 = 8,
    UInt32 = 9,
    Int64 = 10,
    UInt64 = 11,
    Single = 12,
    Double = 13,
    Decimal = 14,
    DateTime = 15,
    TimeSpan = 16,
    ByteArray = 0x20,
    Stream = 0x21,
    StartOfUserTypes = 0x40,
};

// The C# `object?` GetResourceValue returns: the value's runtime kind and
// payload. Only the member the kind selects is set.
struct ResourceValue {
    enum class Kind {
        Null,
        Boolean,
        Char,
        String,
        Byte,
        SByte,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Single,
        Double,
        Decimal,
        DateTime,
        TimeSpan,
        ByteArray,
        Stream,
        SerializedObject,
    };

    Kind kind = Kind::Null;
    bool boolean = false;                  // Boolean
    std::uint16_t character = 0;           // Char: the UTF-16 code unit
    std::string str;                       // String (UTF-8)
    std::uint64_t integer = 0;            // the integral kinds (the signed kinds' two's-complement bits)
    float single = 0.0f;                   // Single
    double doubleValue = 0.0;              // Double
    std::uint32_t decimalBits[4] = {0, 0, 0, 0};  // Decimal: [lo, mid, hi, flags] (the ReadDecimal order)
    std::int64_t ticks = 0;                // DateTime / TimeSpan: the raw int64
    std::vector<std::uint8_t> bytes;       // ByteArray / Stream / SerializedObject
    std::string typeName;                  // SerializedObject's TypeName ("" = the C# null)
};

class ResourcesFile {
public:
    // The C# `ResourcesFile(Stream stream, bool leaveOpen = true)` over
    // the blob bytes: parses the ResourceManager header (the magic number
    // 0xBEEFCACE, the header version and its skip count, and -- for header
    // version 1 -- the reader/resource-set type strings whose
    // DeserializingResourceReader spelling decides usesSerializationFormat)
    // and the RuntimeResourceSet header (the version, the resource and
    // type counts, the type table, the 8-byte alignment padding, the name
    // hash array, the name positions, and the data-section offset),
    // validating each field exactly as the C# does. Throws
    // std::out_of_range (the C# BadImageFormatException/EndOfStreamException
    // arms) with the C# messages for a malformed or truncated blob.
    ResourcesFile(const std::uint8_t* data, std::size_t size);

    // The C# `ResourceCount`: the resource count from the header (the
    // number of entries, not the byte size).
    int ResourceCount() const noexcept { return numResources_; }

    // The C# `GetResourceName(int index)`: entry index's name (the
    // name-section entry at nameSectionPosition + namePositions[index] --
    // a 7-bit-encoded byte length, that many UTF-16LE bytes, then the
    // int32 data offset). The odd trailing byte of an odd-length name
    // decodes as U+FFFD (the Encoding.Unicode end-of-input behavior).
    // Throws std::out_of_range for an out-of-range index (the C#
    // IndexOutOfRangeException -- not one of the caught kinds, so a caller
    // passing a bad index propagates it) and for a malformed or truncated
    // entry (the C# BadImageFormatException arms). Non-const: the C#
    // seeks the shared reader before each read, so the port's reads
    // advance the cursor.
    std::string GetResourceName(int index);

    // The C# private GetResourceName(int index, out int dataOffset): the
    // name plus the entry's value offset relative to the data section.
    // The port exposes the two-argument form and GetResourceDataOffset
    // (below) because GetResourceValue composes them; the name-only
    // overload stays the public C# surface.
    std::string GetResourceName(int index, int& dataOffset);

    // The C# private GetResourceDataOffset(int index): the entry's value
    // offset relative to the data section.
    int GetResourceDataOffset(int index);

    // The C# `usesSerializationFormat` reader-type check result: whether
    // the header's reader type is the System.Resources.Extensions
    // DeserializingResourceReader (whose user-type values carry the
    // SerializationFormat wrapper the deferred value decode unwraps).
    // Always false when the header version is greater than 1 (the reader
    // type string is only read for version 1).
    bool UsesSerializationFormat() const noexcept {
        return usesSerializationFormat_;
    }

    // The C# `object? GetResourceValue(int index)`: entry index's value,
    // decoded at its data-section offset -- the V1 path (the type-table
    // index with the assembly-name strip) or the V2 path (the ResourceTypeCode
    // switch), the special ByteArray/Stream shapes, and the serialized
    // user types (the SerializedObject kind with the GetBytesForSerialized
    // Object region walk). Throws std::out_of_range for a malformed entry
    // (the C# BadImageFormatException arms, incl. the EndOfStream wrap
    // "Invalid resource file"; see EndOfStreamError below).
    ResourceValue GetResourceValue(int index);

    // The C# `version`: the RuntimeResourceSet header version (1 or 2).
    int Version() const noexcept { return version_; }

private:
    // The C# EndOfStreamException arms of the read primitives: a
    // std::out_of_range subclass so every existing std::out_of_range catch
    // still sees it, letting LoadObject wrap exactly the EndOfStream throws
    // into the C# BadImageFormatException("Invalid resource file") the way
    // the C# catch (EndOfStreamException) does (the BadImageFormatException
    // arms throw the plain type and pass through the wrap untouched).
    class EndOfStreamError final : public std::out_of_range {
    public:
        explicit EndOfStreamError(const char* message)
            : std::out_of_range(message) {}
    };

    // The BinaryReader primitives over the blob span (the ctor's reads).
    // Each throws std::out_of_range past the end of the span (the C#
    // EndOfStreamException of a BinaryReader read).
    std::uint8_t ReadByte();
    std::int32_t ReadInt32();
    std::int16_t ReadInt16();
    std::uint16_t ReadUInt16();
    std::uint32_t ReadUInt32();
    std::int64_t ReadInt64();
    std::uint64_t ReadUInt64();
    bool ReadBoolean();
    float ReadSingle();
    double ReadDouble();
    // The C# MyBinaryReader.Read7BitEncodedInt: up to 5 payload bytes, the
    // 5th allowed to set the sign bit (the value can be negative).
    std::int32_t Read7BitEncodedInt();
    // The C# BinaryReader.ReadString: a 7-bit-encoded byte length then that
    // many UTF-8 bytes (kept as the port's UTF-8 string).
    std::string ReadString();
    // The C# BinaryReader.ReadBytes(int): reads up to count bytes -- a
    // partial read at the end of the stream returns what is available,
    // NOT the EndOfStreamException.
    std::vector<std::uint8_t> ReadBytes(std::size_t count);
    // The C# blocking `reader.Read(bytes, byteLen - count, count)` loop:
    // exactly n bytes or std::out_of_range.
    void ReadExact(std::uint8_t* out, std::size_t n);
    // The C# MyBinaryReader.Seek (a BaseStream.Seek): the past-the-end
    // clamp is documented on the class.
    void SeekCurrent(std::int64_t delta);
    void SeekBegin(std::int64_t pos);

    // The C# `string FindType(int typeIndex)`: the type-table entry, with
    // its "Type index out of bounds" BadImageFormatException arm.
    std::string FindType(int typeIndex);
    // The C# `object? LoadObject(int dataOffset)`: the version dispatch with
    // the EndOfStreamException wrap ("Invalid resource file").
    ResourceValue LoadObject(int dataOffset);
    // The C# LoadObjectV1: the type-table index (the -1 null marker), the
    // assembly-name strip, and the type-name switch (String, the integral
    // and floating reads, DateTime, TimeSpan, Decimal, and the serialized
    // user type in the default arm).
    ResourceValue LoadObjectV1(int dataOffset);
    // The C# LoadObjectV2: the ResourceTypeCode switch (the primitives, the
    // ByteArray and Stream shapes, and the serialized user type in the
    // default arm).
    ResourceValue LoadObjectV2(int dataOffset);
    // The C# `long[] GetStartPositions()`: the sorted absolute starts of
    // every name entry and every data entry (the serialized-object region
    // boundaries).
    std::vector<std::int64_t> GetStartPositions();
    // The C# `internal byte[] GetBytesForSerializedObject(long pos, bool
    // usesSerializationFormat)`: the serialized object's byte region --
    // from pos to the next entry start (or the file length), skipping the
    // [SerializationFormat kind][length] wrapper for a serialization-format
    // container.
    std::vector<std::uint8_t> GetBytesForSerializedObject(
        std::size_t pos, bool usesSerializationFormat);

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t pos_ = 0;  // the reader cursor

    int version_ = 0;
    bool usesSerializationFormat_ = false;
    int numResources_ = 0;
    std::vector<std::string> typeTable_;
    std::vector<std::int32_t> namePositions_;
    // The name and data section starts, absolute in the blob (the C#
    // `nameSectionPosition`/`dataSectionPosition` longs with the C#
    // `fileStartPosition` being 0 -- the span starts at the blob).
    std::size_t nameSectionPosition_ = 0;
    std::size_t dataSectionPosition_ = 0;
};

}  // namespace ILSpy::Decompiler::Util
