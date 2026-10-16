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

// Port of ICSharpCode.Decompiler/SingleFileBundle.cs: reading .NET 5+
// single-file bundle manifests (the format Microsoft.NET.HostModel's Bundler
// writes -- the CLI's -d/--dump-package path consumes it).
//
// Bundle layout (the Bundler appends everything to a copy of the apphost
// template, whose data section carries a 40-byte placeholder the Bundler
// patches): the embedded files, then the manifest, and the "footer" is the
// PATCHED PLACEHOLDER still sitting inside the apphost region -- 8 bytes
// holding the manifest offset followed by the 32-byte signature (the SHA-256
// of ".net core bundle"). The signature is therefore generally NOT the last
// 32 bytes of the file, and IsBundle scans the whole image for it.
//
// C#-to-C++ porting decisions:
//  * The C# memory-maps the file and walks it through BinaryReader; the port
//    reads the same layout over a caller-supplied byte buffer (the port's
//    span convention -- MetadataFile's image buffer).
//  * BinaryReader.ReadString ports as the 7-bit length prefix with .NET's
//    exact semantics (the shift wraps past 32 bits, a 6th continuation byte
//    throws, a negative length throws the "invalid string length" message),
//    and EndOfStreamException maps to std::out_of_range carrying the C#
//    message -- the established port convention.
//  * InvalidDataException (the version and FileCount validations) maps to
//    std::runtime_error carrying the exact C# messages: the C# callers let
//    it escape to their global catch, and the port's CLI catch renders the
//    message with the same exit code.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::SingleFileBundle {

// The C# `public enum FileType : byte`: identifies the kind of file embedded
// into the bundle.
enum class FileType : std::uint8_t {
    Unknown,            // Type not determined.
    Assembly,           // IL and R2R Assemblies
    NativeBinary,       // NativeBinaries
    DepsJson,           // .deps.json configuration file
    RuntimeConfigJson,  // .runtimeconfig.json configuration file
    Symbols             // PDB Files
};

// The C# `public struct Entry`: one embedded file.
struct Entry {
    long long Offset = 0;
    long long Size = 0;
    // 0 if not compressed, otherwise the compressed size in the bundle.
    long long CompressedSize = 0;
    FileType Type = FileType::Unknown;
    // Path of the embedded file, relative to the bundle source-directory.
    std::string RelativePath;
};

// The C# `public struct Header`: the manifest.
struct Header {
    std::uint32_t MajorVersion = 0;
    std::uint32_t MinorVersion = 0;
    std::int32_t FileCount = 0;
    std::string BundleID;

    // Fields introduced with v2:
    long long DepsJsonOffset = 0;
    long long DepsJsonSize = 0;
    long long RuntimeConfigJsonOffset = 0;
    long long RuntimeConfigJsonSize = 0;
    std::uint64_t Flags = 0;

    std::vector<Entry> Entries;
};

// The C# `IsBundle(byte* data, long size, out long bundleHeaderOffset)`: scan
// for the bundle signature and, when found, take the manifest offset stored
// in the 8 bytes immediately before it. The C# scan bound is
// `ptr < data + size - 32`, so a signature starting within the last 32 bytes
// of the file is not found -- a faithful bound the port keeps (genuine
// bundles carry the signature inside the apphost region, well before the
// end). Returns false for a missing signature, a signature closer than
// sizeof(long) to the start (the crafted-file guard that keeps the offset
// read in bounds), or an offset that is not strictly within the image.
bool IsBundle(const std::uint8_t* data, long long size, long long& bundleHeaderOffset);

// The C# `ReadManifest(Stream)`: decode the manifest at the bundle header
// offset (see SingleFileBundle.cpp for the field order). Throws
// std::runtime_error carrying the C# InvalidDataException messages for an
// unsupported version or a dishonest FileCount, and std::out_of_range for
// reads past the end of the data (the C# EndOfStreamException /
// BinaryReader string-length failures).
Header ReadManifest(const std::uint8_t* data, long long size, long long bundleHeaderOffset);

}  // namespace ILSpy::Decompiler::SingleFileBundle
