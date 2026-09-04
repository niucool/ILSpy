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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Implementation of SingleFileBundle.hpp. See the header for the port
// decisions and the C# cross-references (SingleFileBundle.cs).

#include "Decompiler/SingleFileBundle.hpp"

#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::SingleFileBundle {
namespace {

// The 32-byte bundle signature: SHA-256 for ".net core bundle"
// (Microsoft.NET.HostModel's BundleHeaderPlaceholder.Slice(8)).
constexpr std::uint8_t kSignature[32] = {
    0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38,
    0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
    0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18,
    0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae,
};

// The C# BinaryReader over the memory-mapped bundle view (ReadManifest's
// `using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen:
// true)`), ported as a cursor over the byte buffer. Every read past the end
// of the data throws std::out_of_range carrying the C# EndOfStreamException
// message ("Unable to read beyond the end of the stream." -- the port's
// established exception mapping).
struct Cursor {
    const std::uint8_t* data;
    long long size;
    long long pos;

    void Need(long long count) const {
        if (pos + count > size)
            throw std::out_of_range("Unable to read beyond the end of the stream.");
    }

    std::uint32_t ReadUInt32() {
        Need(4);
        std::uint32_t v = static_cast<std::uint32_t>(data[pos])
            | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
            | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
            | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }

    std::int32_t ReadInt32() { return static_cast<std::int32_t>(ReadUInt32()); }

    long long ReadInt64() {
        Need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<std::uint64_t>(data[pos + i]) << (8 * i);
        pos += 8;
        return static_cast<long long>(v);
    }

    std::uint64_t ReadUInt64() { return static_cast<std::uint64_t>(ReadInt64()); }

    std::uint8_t ReadByte() {
        Need(1);
        return data[pos++];
    }

    // The C# BinaryReader.Read7BitEncodedInt: no 5-byte cap -- a 6th
    // continuation byte trips .NET's `shift == 7 * 5` check (the probed
    // FormatException message), and the shift count masks to 5 bits so the
    // 5-byte 0xFFFFFFFF0F prefix wraps to -1.
    std::int32_t Read7BitEncodedInt() {
        std::uint32_t result = 0;
        int shift = 0;
        for (;;) {
            if (shift == 7 * 5)
                throw std::out_of_range(
                    "Too many bytes in what should have been a 7-bit encoded integer.");
            std::uint8_t b = ReadByte();
            result |= (b & 0x7F) << (shift & 31);
            shift += 7;
            if ((b & 0x80) == 0)
                break;
        }
        return static_cast<std::int32_t>(result);
    }

    // The C# BinaryReader.ReadString: the 7-bit length prefix, then that
    // many raw bytes as UTF-8. A wrapped-negative length throws the probed
    // IOException message; the byte payload throws the EndOfStream message
    // when truncated. The raw UTF-8 bytes pass through unchanged (the
    // std::string keeps them; the filesystem layer takes UTF-8).
    std::string ReadString() {
        std::int32_t length = Read7BitEncodedInt();
        if (length < 0) {
            throw std::out_of_range(
                "BinaryReader encountered an invalid string length of "
                + std::to_string(length) + " characters.");
        }
        Need(length);
        std::string s(reinterpret_cast<const char*>(data + pos), length);
        pos += length;
        return s;
    }
};

// The C# `private static Entry ReadEntry(BinaryReader reader, uint
// bundleMajorVersion)`.
Entry ReadEntry(Cursor& reader, std::uint32_t bundleMajorVersion) {
    Entry entry;
    entry.Offset = reader.ReadInt64();
    entry.Size = reader.ReadInt64();
    entry.CompressedSize = bundleMajorVersion >= 6 ? reader.ReadInt64() : 0;
    entry.Type = static_cast<FileType>(reader.ReadByte());
    entry.RelativePath = reader.ReadString();
    return entry;
}

}  // namespace

bool IsBundle(const std::uint8_t* data, long long size, long long& bundleHeaderOffset) {
    constexpr int kSignatureLength = 32;
    // The C# scan bound: `byte* end = data + (size - 32); for (ptr = data;
    // ptr < end; ptr++)`. A buffer shorter than the signature has no
    // candidate positions (the C# pointer arithmetic underflows there; the
    // port guards the same loop away).
    if (size < kSignatureLength) {
        bundleHeaderOffset = 0;
        return false;
    }
    const std::uint8_t* end = data + (size - kSignatureLength);
    for (const std::uint8_t* ptr = data; ptr < end; ptr++) {
        if (*ptr == 0x8b && std::memcmp(ptr, kSignature, kSignatureLength) == 0) {
            // A genuine bundle stores the 8-byte header offset immediately
            // before the signature, so the signature never appears within
            // the first sizeof(long) bytes of the file. Without this guard,
            // a crafted file with the signature at offset 0..7 reads before
            // the start of the buffer.
            if (ptr - data >= static_cast<long long>(sizeof(long long))) {
                long long offset;
                std::memcpy(&offset, ptr - sizeof(long long), sizeof(long long));
                if (offset > 0 && offset < size) {
                    bundleHeaderOffset = offset;
                    return true;
                }
            }
        }
    }
    bundleHeaderOffset = 0;
    return false;
}

Header ReadManifest(const std::uint8_t* data, long long size, long long bundleHeaderOffset) {
    Cursor reader{ data, size, bundleHeaderOffset };
    Header header;
    header.MajorVersion = reader.ReadUInt32();
    header.MinorVersion = reader.ReadUInt32();

    // Major versions 3, 4 and 5 were skipped to align bundle versioning with
    // the .NET versioning scheme.
    if (header.MajorVersion < 1 || header.MajorVersion > 6) {
        throw std::runtime_error("Unsupported manifest version: "
            + std::to_string(header.MajorVersion) + "." + std::to_string(header.MinorVersion));
    }
    header.FileCount = reader.ReadInt32();
    // FileCount is used below to pre-size the entry array. Each entry occupies
    // at least one byte in the stream, so a count larger than the bytes that
    // remain cannot be honest; reject it instead of attempting a huge
    // allocation for a crafted manifest.
    long long remainingBytes = size - reader.pos;
    if (header.FileCount < 0 || header.FileCount > remainingBytes) {
        throw std::runtime_error("Invalid bundle manifest: FileCount "
            + std::to_string(header.FileCount) + " exceeds available data.");
    }
    header.BundleID = reader.ReadString();
    if (header.MajorVersion >= 2) {
        header.DepsJsonOffset = reader.ReadInt64();
        header.DepsJsonSize = reader.ReadInt64();
        header.RuntimeConfigJsonOffset = reader.ReadInt64();
        header.RuntimeConfigJsonSize = reader.ReadInt64();
        header.Flags = reader.ReadUInt64();
    }
    header.Entries.reserve(static_cast<std::size_t>(header.FileCount));
    for (int i = 0; i < header.FileCount; i++) {
        header.Entries.push_back(ReadEntry(reader, header.MajorVersion));
    }
    return header;
}

}  // namespace ILSpy::Decompiler::SingleFileBundle
