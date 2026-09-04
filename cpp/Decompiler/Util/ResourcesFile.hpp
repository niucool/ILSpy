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
// This slice ports the header parse and the entry-name listing the CLI's
// --list-resources path consumes (EnumerateResourcePaths); the value
// decode (LoadObjectV1/V2, GetResourceValue, ResourceSerializedObject and
// the GetBytesForSerializedObject position walk) is deferred to the
// --resource extraction slice -- the members the deferred decode needs
// (the version, typeTable, usesSerializationFormat and data-section
// state) are captured by the constructor now, so that slice adds only the
// decode itself.
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
//  * The C# reads through a BinaryReader over a MemoryStream, where a Seek
//    past the end succeeds and only a subsequent read throws. The port's
//    Seek clamps a past-the-end position to the end of the span, which is
//    observably equivalent: every read at or past the end throws exactly
//    where the C#'s EndOfStreamException would.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util {

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
    // (below) because the deferred GetResourceValue composes them; the
    // name-only overload stays the public C# surface.
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

    // The C# `version`: the RuntimeResourceSet header version (1 or 2).
    int Version() const noexcept { return version_; }

private:
    // The BinaryReader primitives over the blob span (the ctor's reads).
    // Each throws std::out_of_range past the end of the span (the C#
    // EndOfStreamException of a BinaryReader read).
    std::uint8_t ReadByte();
    std::int32_t ReadInt32();
    // The C# MyBinaryReader.Read7BitEncodedInt: up to 5 payload bytes, the
    // 5th allowed to set the sign bit (the value can be negative).
    std::int32_t Read7BitEncodedInt();
    // The C# BinaryReader.ReadString: a 7-bit-encoded byte length, then
    // that many UTF-8 bytes (kept as the port's UTF-8 string).
    std::string ReadString();
    // The C# blocking `reader.Read(bytes, byteLen - count, count)` loop:
    // exactly n bytes or std::out_of_range.
    void ReadExact(std::uint8_t* out, std::size_t n);
    // The C# MyBinaryReader.Seek (a BaseStream.Seek): the past-the-end
    // clamp is documented on the class.
    void SeekCurrent(std::int64_t delta);
    void SeekBegin(std::int64_t pos);

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
