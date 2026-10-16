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

// Port of ICSharpCode.ILSpyX/FileLoaders/XamarinCompressedFileLoader.cs:
// the XALZ ('XALZ' little-endian 0x5A4C4158) compressed-module loader.
//
// C#-to-C++ porting decisions:
//  * The C# LZ4Codec.Decode ports to lz4's LZ4_decompress_safe (the same
//    block decode: the written count, or negative on failure).
//  * The C# InvalidDataException arms port as std::out_of_range with the
//    exact messages (the LoadedPackage convention).
//  * The C# "truncated payload" arm (a stream whose Length lies about the
//    bytes it can deliver) is unreachable in the port's (data, size) blob
//    form -- the payload is always exactly the bytes behind the 12-byte
//    header. A documented divergence: nothing to port there.
//  * The decompressed module loads through the same MetadataFile
//    in-memory construction PEFileLoader uses (the never-throwing
//    divergence; the C# PEFile ctor throws BadImageFormatException).

#pragma once

#include "ILSpyX/FileLoaders/LoadResult.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class XamarinCompressedFileLoader : IFileLoader`.
class XamarinCompressedFileLoader final : public IFileLoader {
public:
    std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const override;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
