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

// Port of ICSharpCode.Decompiler.Tests/Metadata/WebCilFileTests.cs:
// exercises the WebCIL container reader against crafted Wasm/WebCIL
// input. The section-header fields driving the offset arithmetic come
// straight from the file, so a crafted module must be rejected rather
// than reading outside the image (CWE-125) or crashing the loader.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/WebCilFile.hpp"

#include "TestFixtures/ConnIdResFixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::WebCilFile;
using ILSpy::Decompiler::Metadata::WebcilHeader;
using ILSpy::Decompiler::Metadata::WebcilSectionHeader;

constexpr std::uint32_t kWasmMagic = 0x6d736100u;    // "\0asm"
constexpr std::uint32_t kWebcilMagic = 0x4c496257u;  // "WbIL"

struct SectionHeaderRaw {
    std::uint32_t VirtualSize;
    std::uint32_t VirtualAddress;
    std::uint32_t RawDataSize;
    std::uint32_t RawDataPtr;
};

void WriteULEB128(std::vector<std::uint8_t>& buffer, std::uint32_t value)
{
    do {
        std::uint8_t b = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            b |= 0x80;
        }
        buffer.push_back(b);
    } while (value != 0);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void PutU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::vector<std::uint8_t> BuildWebcilPayload(
    std::uint16_t coffSections, std::uint32_t peCliHeaderRVA,
    const std::vector<SectionHeaderRaw>& sections)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, kWebcilMagic);
    PutU16(bytes, 0);  // VersionMajor
    PutU16(bytes, 0);  // VersionMinor
    PutU16(bytes, coffSections);
    PutU16(bytes, 0);  // reserved0
    PutU32(bytes, peCliHeaderRVA);
    PutU32(bytes, 0);  // PECliHeaderSize
    PutU32(bytes, 0);  // PEDebugRVA
    PutU32(bytes, 0);  // PEDebugSize
    for (const auto& s : sections) {
        PutU32(bytes, s.VirtualSize);
        PutU32(bytes, s.VirtualAddress);
        PutU32(bytes, s.RawDataSize);
        PutU32(bytes, s.RawDataPtr);
    }
    return bytes;
}

// Wraps a WebCIL payload in the minimal Wasm container the parser
// recognizes: a Data section whose two data segments are the (skipped)
// first segment and the WebCIL blob.
std::vector<std::uint8_t> BuildWasmContainer(
    const std::vector<std::uint8_t>& webcilPayload)
{
    std::vector<std::uint8_t> content;
    WriteULEB128(content, 2);  // the number of data segments
    content.push_back(1);      // segment 1 kind
    WriteULEB128(content, 0);  // segment 1 length
    content.push_back(1);      // segment 2 kind
    WriteULEB128(content,
        static_cast<std::uint32_t>(webcilPayload.size()));
    content.insert(content.end(), webcilPayload.begin(),
        webcilPayload.end());

    std::vector<std::uint8_t> file;
    PutU32(file, kWasmMagic);
    PutU32(file, 1);           // the Wasm version
    file.push_back(11);        // WasmSectionId::Data
    WriteULEB128(file, static_cast<std::uint32_t>(content.size()));
    file.insert(file.end(), content.begin(), content.end());
    return file;
}

std::string WriteBytes(const std::string& fileName,
    const std::vector<std::uint8_t>& bytes)
{
    fs::path path = fs::temp_directory_path() / fileName;
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) {
        return "";
    }
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

// The C# FromBytes: write to a temp file, parse, delete. The rejections
// assert nullopt; the positive parses assert on the returned offsets.
std::optional<ILSpy::Decompiler::Metadata::WebCilParseResult> FromBytes(
    const std::vector<std::uint8_t>& bytes)
{
    std::string path = WriteBytes("ilspy_webcil_test.wasm", bytes);
    auto result = WebCilFile::TryParse(path);
    std::error_code ec;
    fs::remove(fs::path(path), ec);
    return result;
}

WebcilSectionHeader Section(std::uint32_t virtualAddress,
    std::uint32_t virtualSize, std::uint32_t rawDataPtr,
    std::uint32_t rawDataSize)
{
    // WebcilSectionHeader's field order is
    // {VirtualSize, VirtualAddress, RawDataSize, RawDataPtr} -- the
    // aggregate init is positional.
    WebcilSectionHeader s;
    s.VirtualSize = virtualSize;
    s.VirtualAddress = virtualAddress;
    s.RawDataSize = rawDataSize;
    s.RawDataPtr = rawDataPtr;
    return s;
}

}  // namespace

TEST(WebCilFileTest, TruncatedSectionHeaders_ReturnsNullWithoutReadingPastView) {
    // CoffSections claims far more section headers than the file (or
    // image) holds: the table read overruns and the parse rejects.
    std::vector<SectionHeaderRaw> sections;
    std::vector<std::uint8_t> payload =
        BuildWebcilPayload(0xFFFF, 0, sections);
    EXPECT_FALSE(FromBytes(BuildWasmContainer(payload)).has_value());
}

TEST(WebCilFileTest, CliHeaderRvaInNoSection_ReturnsNull) {
    std::vector<SectionHeaderRaw> sections = {
        SectionHeaderRaw{0x10, 0, 0x10, 0}};
    // PECliHeaderRVA points outside the single section, so RVA
    // translation cannot resolve it.
    std::vector<std::uint8_t> payload = BuildWebcilPayload(1, 0x1000, sections);
    EXPECT_FALSE(FromBytes(BuildWasmContainer(payload)).has_value());
}

TEST(WebCilFileTest, OobSectionHeader_IsRejected) {
    // The exploit shape from the finding: a section spanning the whole
    // RVA space whose raw-data pointer/size point far outside a small
    // image.
    std::vector<WebcilSectionHeader> headers = {Section(0, 0xFFFFFFFF,
        0xFFFFFFFF, 0x7FFFFFFF)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_FALSE(WebCilFile::TryGetSectionDataRange(headers, 0, 0x1000, 0x10,
        offset, length));
}

TEST(WebCilFileTest, RawDataSizeBeyondInt32_IsRejected) {
    std::vector<WebcilSectionHeader> headers = {Section(0, 0x100, 0,
        0x80000000)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_FALSE(WebCilFile::TryGetSectionDataRange(headers, 0,
        0x100000000LL, 0, offset, length));
}

TEST(WebCilFileTest, RawDataRangePastView_IsRejected) {
    std::vector<WebcilSectionHeader> headers = {Section(0, 0x100, 0xF0,
        0x20)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_FALSE(WebCilFile::TryGetSectionDataRange(headers, 0, 0x100, 0,
        offset, length));
}

TEST(WebCilFileTest, RvaInNoSection_IsRejected) {
    std::vector<WebcilSectionHeader> headers = {Section(0x10, 0x10, 0,
        0x10)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_FALSE(WebCilFile::TryGetSectionDataRange(headers, 0, 0x1000,
        0x100, offset, length));
}

TEST(WebCilFileTest, RvaPastRawData_IsRejected) {
    // The RVA sits inside the section's virtual size but past its
    // (smaller) raw data, so there are no bytes to read from it.
    std::vector<WebcilSectionHeader> headers = {Section(0, 0x100, 0, 0x10)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_FALSE(WebCilFile::TryGetSectionDataRange(headers, 0, 0x1000, 0x20,
        offset, length));
}

TEST(WebCilFileTest, InBoundsSection_IsResolved) {
    std::vector<WebcilSectionHeader> headers = {Section(0x20, 0x40, 0x80,
        0x40)};
    std::int64_t offset = 0;
    std::int32_t length = 0;
    EXPECT_TRUE(WebCilFile::TryGetSectionDataRange(headers, 0x10, 0x1000,
        0x30, offset, length));
    // offset = RawDataPtr + webcilOffset + (rva - VirtualAddress)
    //        = 0x80 + 0x10 + 0x10
    EXPECT_EQ(offset, 0xA0);
    // length = the raw data remaining from the RVA
    //        = RawDataSize - (rva - VirtualAddress) = 0x40 - 0x10
    EXPECT_EQ(length, 0x30);
}

TEST(WebCilFileTest, NonWebcilFile_IsRejected) {
    // A plain WASM container without a parseable WebCIL payload.
    std::vector<std::uint8_t> bytes = BuildWasmContainer(
        BuildWebcilPayload(0, 0, {}));
    EXPECT_FALSE(FromBytes(bytes).has_value());
}

// ---- The integration slice: the container presents itself as the
// PE-shaped MetadataFile over the adapted image.

// The minimal PE32 field reads the metadata extraction needs (hand-rolled
// over the fixture bytes; the adapter tests need no PE library).
std::uint16_t GetU16(const std::vector<std::uint8_t>& b, std::size_t off)
{
    return static_cast<std::uint16_t>(b[off] |
                                      (b[off + 1] << 8));
}

std::uint32_t GetU32(const std::vector<std::uint8_t>& b, std::size_t off)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; i--) {
        v = (v << 8) | b[off + i];
    }
    return v;
}

// The PE struct layouts the extraction walks (the winmd pe.h shapes:
// the DOS header 64 bytes, the NT headers 248, the section header 40,
// the optional header's data directory 8 bytes per entry, the COM
// directory at optional offset 96).
constexpr std::size_t kDosSize = 64;
constexpr std::size_t kNtHeaders32Size = 248;
constexpr std::size_t kOptionalHeaderDataDirectoryOffset = 96;
constexpr std::size_t kComDescriptorDirectoryIndex = 14;

// Extracts the ECMA-335 metadata stream from a real PE image (the
// WriteConnIdResDll fixture): e_lfanew -> the COM directory RVA -> the
// section table translation -> the metadata blob bytes.
std::vector<std::uint8_t> ExtractMetadataFromConnIdRes()
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> pe(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    EXPECT_GE(pe.size(), kDosSize + kNtHeaders32Size);
    std::uint32_t lfanew = GetU32(pe, 0x3C);
    std::size_t optional = lfanew + 4 + 20;
    std::uint32_t comRva = GetU32(pe,
        optional + kOptionalHeaderDataDirectoryOffset +
            kComDescriptorDirectoryIndex * 8);
    std::size_t sectionsOffset =
        optional + 224;  // sizeof(image_optional_header32)
    std::uint32_t sectionCount = GetU16(pe, lfanew + 4 + 2);
    // The CLI header (the cor20 header) carries the metadata directory.
    std::uint8_t const* cli = nullptr;
    for (std::uint32_t i = 0; i < sectionCount; i++) {
        std::size_t s = sectionsOffset + i * 40;
        std::uint32_t va = GetU32(pe, s + 12);
        std::uint32_t vs = GetU32(pe, s + 8);
        std::uint32_t raw = GetU32(pe, s + 20);
        if (comRva >= va && comRva < va + vs) {
            cli = pe.data() + raw + (comRva - va);
            break;
        }
    }
    EXPECT_NE(cli, nullptr);
    std::uint32_t mdRva = GetU32(
        std::vector<std::uint8_t>(cli, cli + 72), 8);
    std::uint32_t mdSize = GetU32(
        std::vector<std::uint8_t>(cli, cli + 72), 12);
    // The metadata RVA -> the file offset (the same section walk).
    const std::uint8_t* metadata = nullptr;
    for (std::uint32_t i = 0; i < sectionCount; i++) {
        std::size_t s = sectionsOffset + i * 40;
        std::uint32_t va = GetU32(pe, s + 12);
        std::uint32_t vs = GetU32(pe, s + 8);
        std::uint32_t raw = GetU32(pe, s + 20);
        if (mdRva >= va && mdRva < va + vs) {
            metadata = pe.data() + raw + (mdRva - va);
            break;
        }
    }
    EXPECT_NE(metadata, nullptr);
    EXPECT_EQ(GetU32(std::vector<std::uint8_t>(metadata, metadata + 4), 0),
        0x424a5342u);  // "BSJB"
    return std::vector<std::uint8_t>(metadata, metadata + mdSize);
}

TEST(WebCilFileTest, ContainerPresentsTheEmbeddedMetadataAsThePeShape) {
    // Wrap the ConnIdRes fixture's real metadata in a valid WebCIL
    // container: one section covering the whole blob, the CLI header at
    // its start carrying the metadata directory.
    std::vector<std::uint8_t> metadata = ExtractMetadataFromConnIdRes();
    std::vector<std::uint8_t> payload;
    constexpr std::uint32_t kBlobRva = 0x100;
    // The CLI header (the cor20 header, cb=72): the MetaData directory
    // points into the blob.
    PutU32(payload, 0x4c496257u);  // "WbIL"
    PutU16(payload, 0);            // VersionMajor
    PutU16(payload, 0);            // VersionMinor
    PutU16(payload, 1);            // CoffSections
    PutU16(payload, 0);            // reserved0
    PutU32(payload, 0x100);        // PECliHeaderRVA
    PutU32(payload, 72);           // PECliHeaderSize
    PutU32(payload, 0);            // PEDebugRVA
    PutU32(payload, 0);            // PEDebugSize
    // The COFF-style section table: one section covering the blob.
    PutU32(payload, 0x1000);       // VirtualSize
    PutU32(payload, 0);            // VirtualAddress
    PutU32(payload, 0x1000);       // RawDataSize
    PutU32(payload, 0);            // RawDataPtr
    // Pad to the CLI header RVA.
    payload.resize(0x100, 0);
    // The cor20 header (72 bytes): cb, runtime versions, MetaData.
    PutU32(payload, 72);           // cb
    PutU16(payload, 2);            // MajorRuntimeVersion
    PutU16(payload, 5);            // MinorRuntimeVersion
    PutU32(payload, 0x200);        // MetaData.VirtualAddress
    PutU32(payload, static_cast<std::uint32_t>(metadata.size()));
    PutU32(payload, 0);            // Flags
    PutU32(payload, 0);            // EntryPointToken
    for (int i = 0; i < 4; i++) {  // Resources..ExportAddressTableJumps
        PutU32(payload, 0);
        PutU32(payload, 0);
    }
    PutU32(payload, 0);            // ManagedNativeHeader VA
    PutU32(payload, 0);            // ManagedNativeHeader size
    payload.resize(0x200, 0);
    payload.insert(payload.end(), metadata.begin(), metadata.end());

    std::vector<std::uint8_t> container = BuildWasmContainer(payload);
    auto parsed = FromBytes(container);
    ASSERT_TRUE(parsed.has_value());

    // The adapter image parses as the PE-shaped MetadataFile.
    std::string adaptedPath = WriteBytes("ilspy_webcil_adapted.bin",
        WebCilFile::BuildPeImage(container, *parsed));
    ILSpy::Decompiler::Metadata::MetadataFile adapted(adaptedPath);
    std::error_code ec;
    fs::remove(fs::path(adaptedPath), ec);
    ASSERT_TRUE(adapted.IsValid());
    auto asmDef = adapted.GetAssemblyDefinition();
    ASSERT_TRUE(asmDef.has_value());
    // The fixture's assembly name (WriteConnIdResDll: assembly
    // connid_res, file ilspy_connid_test.dll).
    EXPECT_EQ(asmDef->Name, "connid_res");
}
