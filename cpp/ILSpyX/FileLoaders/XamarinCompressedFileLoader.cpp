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

// Implementation of ILSpyX/FileLoaders/XamarinCompressedFileLoader.hpp (the
// porting decisions are on the header and LoadResult.hpp).

#include "ILSpyX/FileLoaders/XamarinCompressedFileLoader.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

#include <lz4.h>

namespace ILSpy::ILSpyX::FileLoaders {

std::optional<LoadResult> XamarinCompressedFileLoader::Load(
    const std::string& fileName, const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context) const
{
    // Magic used for Xamarin compressed module header ('XALZ',
    // little-endian).
    constexpr std::uint32_t kCompressedDataMagic = 0x5A4C4158u;

    // Read the compressed file header. The C# ReadUInt32's
    // EndOfStreamException on a stream too short to carry the magic
    // returns null: too short to be an XALZ module, pass it through.
    if (size < 4)
        return std::nullopt;
    const std::uint32_t magic = static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
    if (magic != kCompressedDataMagic)
        return std::nullopt;
    // The magic identifies this as an XALZ module, so it must carry the
    // full 12-byte header: magic, descriptor table index, and uncompressed
    // length. A shorter stream is a truncated/corrupt module; fail
    // consistently as InvalidDataException.
    if (size < 12)
        throw std::out_of_range(
            "Invalid Xamarin compressed module: truncated header.");
    const std::uint32_t declaredUncompressedLength =
        static_cast<std::uint32_t>(data[8])
        | (static_cast<std::uint32_t>(data[9]) << 8)
        | (static_cast<std::uint32_t>(data[10]) << 16)
        | (static_cast<std::uint32_t>(data[11]) << 24);

    // The compressed payload is whatever follows the 12-byte header, not
    // the whole file. The declared uncompressed length is
    // attacker-controlled: reject implausible values before renting
    // buffers -- a negative/oversized size, or one larger than any LZ4
    // block of this payload could produce (an LZ4 block expands by at
    // most 255x, so a smaller payload claiming a larger output is a
    // malformed or decompression-bomb header).
    const long long compressedLength =
        static_cast<long long>(size) - 12;
    constexpr long long kMaxLZ4ExpansionRatio = 255;
    if (compressedLength <= 0
        || compressedLength > std::numeric_limits<int>::max()
        || declaredUncompressedLength == 0
        || declaredUncompressedLength
            > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        || static_cast<long long>(declaredUncompressedLength)
            > compressedLength * kMaxLZ4ExpansionRatio) {
        throw std::out_of_range(
            "Invalid Xamarin compressed module: declared length is out of range.");
    }

    // Decompress; LZ4_decompress_safe returns the number of bytes written,
    // or negative on failure. The header declares the exact decompressed
    // size, so anything other than an exact match (a negative error code
    // or a short, truncated decode) means the payload is corrupt and must
    // not be parsed as a partial module.
    const int uncompressedLength = static_cast<int>(declaredUncompressedLength);
    const int compressed = static_cast<int>(compressedLength);
    std::vector<std::uint8_t> dst(
        static_cast<std::size_t>(uncompressedLength));
    int decodedLength = LZ4_decompress_safe(
        reinterpret_cast<const char*>(data + 12),
        reinterpret_cast<char*>(dst.data()), compressed, uncompressedLength);
    if (decodedLength != uncompressedLength) {
        throw std::out_of_range(
            "Invalid Xamarin compressed module: decompressed size does not "
            "match the header.");
    }

    // Load the module from the decompressed data buffer, sliced to the
    // declared length (the C# MemoryStream over the rented buffer). The
    // MetadataReaderOptions the C# passes (the WinRT projections flag) has
    // no port reader surface (the PEFileLoader divergence note).
    (void)context;
    LoadResult result;
    result.MetadataFile = std::make_unique<Decompiler::Metadata::MetadataFile>(
        fileName, std::move(dst));
    return result;
}

}  // namespace ILSpy::ILSpyX::FileLoaders
