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

#include "WebCilFile.hpp"

// The adapter below lays out the synthetic PE header with the same
// structures the winmd database and the port's PeImage reader parse the
// file with (the vendored reader is included only through
// WinmdInclude.hpp -- see its _DEBUG note).
#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The C# constants (WebCilFile.cs): "\0asm" and "WbIL" as little-endian
// uint32s.
constexpr std::uint32_t kWasmMagic = 0x6d736100u;
constexpr std::uint32_t kWebcilMagic = 0x4c496257u;

// The bounds-checked reader over the image: every read past the end
// throws std::out_of_range (the port's stand-in for the C# BinaryReader's
// EndOfStreamException, which FromFile's catch turns into "not a WebCIL
// file").
class SpanReader {
public:
    explicit SpanReader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes) {}

    std::size_t Position() const { return position_; }
    std::size_t Length() const { return bytes_.size(); }

    void Seek(std::size_t position) { position_ = position; }

    // The C# `stream.Seek(offset, SeekOrigin.Current)` -- a relative
    // skip; .NET allows seeking past the end, so this only moves the
    // cursor (the following read throws).
    void Skip(std::uint64_t count) { position_ += count; }

    std::uint8_t ReadU8()
    {
        if (position_ >= bytes_.size()) {
            throw std::out_of_range("WebCIL: unexpected end of stream");
        }
        return bytes_[position_++];
    }

    std::uint16_t ReadU16()
    {
        std::uint16_t value = ReadU8();
        value |= static_cast<std::uint16_t>(ReadU8()) << 8;
        return value;
    }

    std::uint32_t ReadU32()
    {
        std::uint32_t value = ReadU16();
        value |= static_cast<std::uint32_t>(ReadU16()) << 16;
        return value;
    }

    std::int32_t ReadI32() { return static_cast<std::int32_t>(ReadU32()); }

    // The C# `BinaryReader.ReadULEB128` extension
    // (MetadataExtensions.cs): the little-endian base-128 varint, with
    // the same >= 35-bit shift overflow guard (the C# throws
    // OverflowException -- the port's std::out_of_range, caught by the
    // same try block).
    std::uint32_t ReadULEB128()
    {
        std::uint32_t value = 0;
        int shift = 0;
        while (true) {
            std::uint8_t b = ReadU8();
            value |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) {
                break;
            }
            shift += 7;
            if (shift >= 35) {
                throw std::out_of_range("WebCIL: ULEB128 overflow");
            }
        }
        return value;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

std::optional<std::vector<std::uint8_t>> ReadAllBytes(
    std::string_view path)
{
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) {
        // The C# MemoryMappedFile.CreateFromFile's IOException -> null
        // arm (a missing/unopenable file is "not a WebCIL file").
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    return bytes;
}

// The C# `TranslateRVA` (private static): resolves an RVA to its file
// offset inside the container (the WebCIL blob start + the section's raw
// offset + the delta). Throws std::out_of_range on a miss (the C#
// BadImageFormatException "RVA not found in any section").
std::int64_t TranslateRVA(
    const std::vector<WebcilSectionHeader>& sections, std::int64_t webcilOffset,
    std::uint32_t rva)
{
    for (const auto& section : sections) {
        if (rva >= section.VirtualAddress &&
            rva < static_cast<std::int64_t>(section.VirtualAddress) +
                    section.VirtualSize) {
            return static_cast<std::int64_t>(section.RawDataPtr) +
                   webcilOffset + (static_cast<std::int64_t>(rva) -
                                      section.VirtualAddress);
        }
    }
    throw std::out_of_range("WebCIL: RVA not found in any section");
}

// The C# `TryReadWebCilSegment` (private static): reads the WebCIL header
// and section table at the reader's current position and resolves the CLI
// header / metadata offsets. Returns false (not throws) for the shapes the
// C# returns false on; the overruns throw and the caller's catch reduces
// the parse to nullopt, like the C# catch.
bool TryReadWebCilSegment(SpanReader& reader, WebcilHeader& header,
    std::int64_t& metadataOffset, std::int64_t& webcilOffset,
    std::vector<WebcilSectionHeader>& sectionHeaders)
{
    webcilOffset = static_cast<std::int64_t>(reader.Position());

    if (reader.ReadU32() != kWebcilMagic) {
        return false;
    }

    header.VersionMajor = reader.ReadU16();
    header.VersionMinor = reader.ReadU16();
    header.CoffSections = reader.ReadU16();
    (void)reader.ReadU16();  // reserved0
    header.PECliHeaderRVA = reader.ReadU32();
    header.PECliHeaderSize = reader.ReadU32();
    header.PEDebugRVA = reader.ReadU32();
    header.PEDebugSize = reader.ReadU32();

    sectionHeaders.assign(header.CoffSections, WebcilSectionHeader{});
    for (std::uint16_t i = 0; i < header.CoffSections; i++) {
        sectionHeaders[i].VirtualSize = reader.ReadU32();
        sectionHeaders[i].VirtualAddress = reader.ReadU32();
        sectionHeaders[i].RawDataSize = reader.ReadU32();
        sectionHeaders[i].RawDataPtr = reader.ReadU32();
    }

    std::int64_t corHeaderStart = TranslateRVA(
        sectionHeaders, webcilOffset, header.PECliHeaderRVA);
    if (corHeaderStart < 0 ||
        corHeaderStart > static_cast<std::int64_t>(reader.Length())) {
        // The C# seek (which succeeds even past the end) is followed by
        // a read whose EndOfStreamException the catch turns into null;
        // the port's bounds check reaches the same verdict directly.
        return false;
    }
    reader.Seek(static_cast<std::size_t>(corHeaderStart));
    (void)reader.ReadI32();  // byteCount (the CLI header's cb)
    (void)reader.ReadU16();  // major version
    (void)reader.ReadU16();  // minor version
    metadataOffset = TranslateRVA(
        sectionHeaders, webcilOffset,
        static_cast<std::uint32_t>(reader.ReadI32()));
    if (metadataOffset < 0 ||
        metadataOffset > static_cast<std::int64_t>(reader.Length())) {
        // The C# `Seek(metadataOffset, Begin) != metadataOffset` arm.
        return false;
    }
    reader.Seek(static_cast<std::size_t>(metadataOffset));
    return true;
}

}  // namespace

std::optional<WebCilParseResult> WebCilFile::TryParse(std::string_view path)
{
    auto bytes = ReadAllBytes(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    try {
        SpanReader reader(*bytes);

        // The C# magic/version checks over the mapped view.
        if (bytes->size() < 8) {
            return std::nullopt;
        }
        if (reader.ReadU32() != kWasmMagic) {
            return std::nullopt;
        }
        if (reader.ReadU32() != 1) {
            return std::nullopt;
        }

        // The C# section walk: id + ULEB128 size per section; a Custom
        // section of size 0 ends the walk (the C# then probes the Data
        // sections).
        std::int64_t metadataOffset = -1;
        std::vector<WasmSection> sections;
        while (reader.Position() < reader.Length()) {
            WasmSectionId id = static_cast<WasmSectionId>(reader.ReadU8());
            std::uint32_t size = reader.ReadULEB128();
            sections.push_back(WasmSection{id, reader.Position(), size});
            if (id == WasmSectionId::Custom && size == 0) {
                break;
            }
            reader.Skip(size);
        }

        // The C# Data-section probe: two segments, the first skipped,
        // the second the WebCIL blob.
        std::optional<WebCilParseResult> result;
        for (const auto& section : sections) {
            if (section.Id != WasmSectionId::Data || metadataOffset > -1) {
                continue;
            }
            reader.Seek(static_cast<std::size_t>(section.Offset));
            std::uint32_t numSegments = reader.ReadULEB128();
            if (numSegments != 2) {
                continue;
            }
            // The first segment (its kind flag and length are read, the
            // content skipped).
            if (reader.ReadU8() != 1) {
                continue;
            }
            std::uint64_t segmentLength = reader.ReadULEB128();
            reader.Skip(segmentLength);
            // The second segment: its kind flag, then the WebCIL payload.
            if (reader.ReadU8() != 1) {
                continue;
            }
            (void)reader.ReadULEB128();
            WebCilParseResult parsed;
            WebcilHeader header;
            std::vector<WebcilSectionHeader> sectionHeaders;
            if (!TryReadWebCilSegment(reader, header,
                    parsed.MetadataOffset, parsed.WebcilOffset,
                    sectionHeaders)) {
                continue;
            }
            parsed.Header = header;
            parsed.SectionHeaders = std::move(sectionHeaders);
            parsed.WasmSections = std::move(sections);
            metadataOffset = parsed.MetadataOffset;
            result = std::move(parsed);
            break;
        }
        return result;
    } catch (const std::out_of_range&) {
        // The C# catch (EndOfStreamException or OverflowException or
        // BadImageFormatException): a crafted or truncated module is
        // "not a WebCIL file", never a load error.
        return std::nullopt;
    }
}

bool WebCilFile::TryGetSectionDataRange(
    const std::vector<WebcilSectionHeader>& sectionHeaders,
    std::int64_t webcilOffset, std::int64_t viewLength, std::int32_t rva,
    std::int64_t& offset, std::int32_t& length)
{
    offset = 0;
    length = 0;
    for (const auto& section : sectionHeaders) {
        if (rva >= section.VirtualAddress &&
            rva < static_cast<std::int64_t>(section.VirtualAddress) +
                    section.VirtualSize) {
            std::int64_t delta = static_cast<std::int64_t>(rva) -
                                 section.VirtualAddress;
            std::int64_t o = static_cast<std::int64_t>(section.RawDataPtr) +
                             webcilOffset + delta;
            // The length is the raw data remaining from the RVA to the
            // end of the section, matching PEReader.GetSectionData
            // (callers such as GetInitialValue treat SectionData.Length
            // as the bytes available starting at the RVA). A section
            // whose virtual size exceeds its raw data can place the RVA
            // past the raw bytes, leaving nothing to read.
            std::int64_t remaining =
                static_cast<std::int64_t>(section.RawDataSize) - delta;
            if (o < 0 || remaining < 0 || remaining > INT32_MAX ||
                o > viewLength || remaining > viewLength - o) {
                return false;
            }
            offset = o;
            length = static_cast<std::int32_t>(remaining);
            return true;
        }
    }
    return false;
}

std::vector<std::uint8_t> WebCilFile::BuildPeImage(
    const std::vector<std::uint8_t>& container,
    const WebCilParseResult& parsed)
{
    namespace impl = winmd::impl;
    constexpr std::size_t dosSize = sizeof(impl::image_dos_header);
    constexpr std::size_t ntSize = sizeof(impl::image_nt_headers32);
    static_assert(ntSize == 248, "the standard PE32 NT header layout");
    const std::size_t sectionSize = sizeof(impl::image_section_header);
    static_assert(sectionSize == 40, "the standard section header layout");
    // The original container bytes start here in the adapted image; the
    // section raw pointers shift by this plus the WebCIL blob offset.
    const std::size_t headerEnd =
        dosSize + ntSize + parsed.SectionHeaders.size() * sectionSize;

    std::vector<std::uint8_t> image;
    image.reserve(headerEnd + container.size());
    image.resize(dosSize + ntSize, 0);

    auto* dos = reinterpret_cast<impl::image_dos_header*>(image.data());
    dos->e_signature = 0x5A4D;  // "MZ"
    dos->e_lfanew = static_cast<std::int32_t>(dosSize);

    auto* nt = reinterpret_cast<impl::image_nt_headers32*>(
        image.data() + dosSize);
    nt->Signature = 0x00004550;  // "PE\0\0"
    nt->FileHeader.Machine = 0x014C;  // IMAGE_FILE_MACHINE_I386
    nt->FileHeader.NumberOfSections = static_cast<std::uint16_t>(
        parsed.SectionHeaders.size());
    nt->FileHeader.SizeOfOptionalHeader =
        sizeof(impl::image_optional_header32);
    nt->FileHeader.Characteristics = 0x0102;  // executable image, 32-bit
    auto& optional = nt->OptionalHeader;
    optional.Magic = 0x10B;  // PE32
    optional.NumberOfRvaAndSizes = 16;
    // The COM descriptor points at the REAL CLI header inside the WebCIL
    // payload (the header TryReadWebCilSegment read the metadata location
    // from) -- the winmd database's PE walk then finds the genuine
    // metadata stream without any duplication.
    optional.DataDirectory[14].VirtualAddress = parsed.Header.PECliHeaderRVA;
    optional.DataDirectory[14].Size = parsed.Header.PECliHeaderSize;

    image.resize(headerEnd, 0);
    for (std::size_t i = 0; i < parsed.SectionHeaders.size(); i++) {
        const auto& source = parsed.SectionHeaders[i];
        auto* header = reinterpret_cast<impl::image_section_header*>(
            image.data() + dosSize + ntSize + i * sectionSize);
        header->Misc.VirtualSize = source.VirtualSize;
        header->VirtualAddress = source.VirtualAddress;
        header->SizeOfRawData = source.RawDataSize;
        header->PointerToRawData = static_cast<std::uint32_t>(
            headerEnd + parsed.WebcilOffset + source.RawDataPtr);
    }

    image.insert(image.end(), container.begin(), container.end());
    return image;
}

}  // namespace ILSpy::Decompiler::Metadata
