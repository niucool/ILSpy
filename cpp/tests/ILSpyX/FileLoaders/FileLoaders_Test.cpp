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

// Tests for the FileLoaders port (ICSharpCode.ILSpyX/FileLoaders/): the
// registry's default population and precedence, each ported loader's
// claim/gate contract, and the XALZ header-validation matrix.

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/FileLoaders/ArchiveFileLoader.hpp"
#include "ILSpyX/FileLoaders/BundleFileLoader.hpp"
#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"
#include "ILSpyX/FileLoaders/PEFileLoader.hpp"
#include "ILSpyX/FileLoaders/WebCilFileLoader.hpp"
#include "ILSpyX/FileLoaders/XamarinCompressedFileLoader.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "ILSpyX/AssemblyListManager.hpp"
#include "ILSpyX/LoadedAssemblyExtensions.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/InMemorySettingsProvider.hpp"

#include <cstdlib>
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lz4.h>
#include <miniz/miniz.h>
#include <miniz/miniz_zip.h>

namespace {

namespace fs = std::filesystem;
namespace FL = ILSpy::ILSpyX::FileLoaders;
using FL::FileLoadContext;
using FL::LoadResult;
using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::LoadedAssembly;

std::vector<std::uint8_t> Bytes(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

fs::path TempDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("ilspy_fileloaders_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string WriteFile(const fs::path& dir, const std::string& name,
    const std::vector<std::uint8_t>& bytes)
{
    fs::path file = dir / name;
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return file.string();
}

// The XALZ 12-byte header: magic, descriptor-table index (unused), the
// declared uncompressed length.
std::vector<std::uint8_t> XalzWrap(const std::vector<std::uint8_t>& payload,
    std::uint32_t declaredLength, bool honestPayload = true)
{
    std::vector<std::uint8_t> out;
    auto put32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; i++)
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    put32(0x5A4C4158u);  // 'XALZ', little-endian
    put32(0);            // descriptor table index
    put32(declaredLength);
    if (honestPayload)
        out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// ---- The WebCIL container builders (duplicated file-locally from
// WebCilFile_Test.cpp per the file-local helper convention).

void PutU16WebCil(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void PutU32WebCil(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

std::uint16_t GetU16WebCil(const std::vector<std::uint8_t>& b,
    std::size_t off)
{
    return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

std::uint32_t GetU32WebCil(const std::vector<std::uint8_t>& b,
    std::size_t off)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; i--) {
        v = (v << 8) | b[off + i];
    }
    return v;
}

// Builds the WebCIL payload around a real metadata stream: one section
// covering the blob, the CLI header at RVA 0x100 carrying the metadata
// directory (the metadata itself at blob RVA 0x200).
std::vector<std::uint8_t> BuildWebCilContainerOver(
    const std::vector<std::uint8_t>& metadata)
{
    std::vector<std::uint8_t> payload;
    PutU32WebCil(payload, 0x4c496257u);  // "WbIL"
    PutU16WebCil(payload, 0);            // VersionMajor
    PutU16WebCil(payload, 0);            // VersionMinor
    PutU16WebCil(payload, 1);            // CoffSections
    PutU16WebCil(payload, 0);            // reserved0
    PutU32WebCil(payload, 0x100);        // PECliHeaderRVA
    PutU32WebCil(payload, 72);           // PECliHeaderSize
    PutU32WebCil(payload, 0);            // PEDebugRVA
    PutU32WebCil(payload, 0);            // PEDebugSize
    PutU32WebCil(payload, 0x1000);       // VirtualSize
    PutU32WebCil(payload, 0);            // VirtualAddress
    PutU32WebCil(payload, 0x1000);       // RawDataSize
    PutU32WebCil(payload, 0);            // RawDataPtr
    payload.resize(0x100, 0);
    PutU32WebCil(payload, 72);           // the cor20 cb
    PutU16WebCil(payload, 2);            // MajorRuntimeVersion
    PutU16WebCil(payload, 5);            // MinorRuntimeVersion
    PutU32WebCil(payload, 0x200);        // MetaData.VirtualAddress
    PutU32WebCil(payload,
        static_cast<std::uint32_t>(metadata.size()));  // MetaData.Size
    PutU32WebCil(payload, 0);            // Flags
    PutU32WebCil(payload, 0);            // EntryPointToken
    for (int i = 0; i < 4; i++) {
        PutU32WebCil(payload, 0);        // Resources etc.
        PutU32WebCil(payload, 0);
    }
    PutU32WebCil(payload, 0);            // ManagedNativeHeader VA
    PutU32WebCil(payload, 0);            // ManagedNativeHeader size
    payload.resize(0x200, 0);
    payload.insert(payload.end(), metadata.begin(), metadata.end());

    // The WASM container: the magic + version, one Data section holding
    // the two segments (the skipped first, the WebCIL blob second).
    std::vector<std::uint8_t> container;
    PutU32WebCil(container, 0x6d736100u);  // "\0asm"
    PutU32WebCil(container, 1);            // the Wasm version
    container.push_back(11);               // WasmSectionId::Data
    // The section content: ULEB sizes are small enough to be single-byte.
    std::vector<std::uint8_t> content;
    content.push_back(2);                  // two segments
    content.push_back(1);                  // segment 1 kind
    content.push_back(0);                  // segment 1 length
    content.push_back(1);                  // segment 2 kind
    while (payload.size() >= 0x80) {
        // The multi-byte ULEB128 path (defensive; the payloads here are
        // small).
        std::uint32_t remaining =
            static_cast<std::uint32_t>(payload.size());
        while (remaining >= 0x80) {
            content.push_back(static_cast<std::uint8_t>(remaining) | 0x80);
            remaining >>= 7;
        }
        content.push_back(static_cast<std::uint8_t>(remaining));
        break;
    }
    if (payload.size() < 0x80) {
        content.push_back(static_cast<std::uint8_t>(payload.size()));
    }
    content.insert(content.end(), payload.begin(), payload.end());
    // ULEB128 the content length (single byte for small content).
    if (content.size() < 0x80) {
        container.push_back(static_cast<std::uint8_t>(content.size()));
    } else {
        std::uint32_t remaining = static_cast<std::uint32_t>(content.size());
        while (remaining >= 0x80) {
            container.push_back(static_cast<std::uint8_t>(remaining) | 0x80);
            remaining >>= 7;
        }
        container.push_back(static_cast<std::uint8_t>(remaining));
    }
    container.insert(container.end(), content.begin(), content.end());
    return container;
}

// Extracts the ECMA-335 metadata stream from the ConnIdRes fixture PE.
std::vector<std::uint8_t> ExtractMetadataFromConnIdRes()
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> pe(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    std::uint32_t lfanew = GetU32WebCil(pe, 0x3C);
    std::size_t optional = lfanew + 4 + 20;
    std::uint32_t comRva = GetU32WebCil(pe,
        optional + 96 + 14 * 8);
    std::size_t sectionsOffset = optional + 224;
    std::uint32_t sectionCount = GetU16WebCil(pe, lfanew + 4 + 2);
    std::uint8_t const* cli = nullptr;
    for (std::uint32_t i = 0; i < sectionCount; i++) {
        std::size_t s = sectionsOffset + i * 40;
        std::uint32_t va = GetU32WebCil(pe, s + 12);
        std::uint32_t vs = GetU32WebCil(pe, s + 8);
        std::uint32_t raw = GetU32WebCil(pe, s + 20);
        if (comRva >= va && comRva < va + vs) {
            cli = pe.data() + raw + (comRva - va);
            break;
        }
    }
    std::uint8_t const* metadata = nullptr;
    std::uint32_t mdRva = *reinterpret_cast<const std::uint32_t*>(cli + 8);
    std::uint32_t mdSize =
        *reinterpret_cast<const std::uint32_t*>(cli + 12);
    for (std::uint32_t i = 0; i < sectionCount; i++) {
        std::size_t s = sectionsOffset + i * 40;
        std::uint32_t va = GetU32WebCil(pe, s + 12);
        std::uint32_t vs = GetU32WebCil(pe, s + 8);
        std::uint32_t raw = GetU32WebCil(pe, s + 20);
        if (mdRva >= va && mdRva < va + vs) {
            metadata = pe.data() + raw + (mdRva - va);
            break;
        }
    }
    return std::vector<std::uint8_t>(metadata, metadata + mdSize);
}

// Builds the faithful WebCIL container over a WHOLE PE image: the
// WebCIL header + the PE's own section table (the raw pointers shifted
// by the payload-header size; the virtual addresses verbatim) + the PE
// bytes verbatim. The CLI header sits at the PE's own COM-directory RVA,
// so the metadata, the method bodies, and every other RVA resolve
// through the WebCIL translation exactly as they would in a real
// .wasm WebCIL file produced by the toolchain.
std::vector<std::uint8_t> BuildWebCilContainerOverPe(
    const std::vector<std::uint8_t>& pe)
{
    // The payload layout: the WbIL header (the magic + the four uint16
    // fields + the four uint32 fields = 28 bytes) followed by the COFF
    // table, followed by the PE bytes verbatim. The copied section
    // table's raw pointers are rebased onto the payload: rawBase marks
    // where the copied raw regions begin (right after the table), and
    // each pointer is shifted by it.
    constexpr std::size_t kWbilHeaderSize = 28;
    std::uint32_t lfanew = GetU32WebCil(pe, 0x3C);
    std::uint32_t sectionCount = GetU16WebCil(pe, lfanew + 4 + 2);
    // The optional header: PE32 (magic 0x10B) has a 224-byte header with
    // the data directory at offset 96; PE32+ (0x20B) is 240 bytes with
    // the directory at 112. mscorlib (the corpus fixture) is PE32+.
    std::size_t optionalOffset = lfanew + 4 + 20;
    std::uint16_t optMagic = GetU16WebCil(pe, optionalOffset);
    std::size_t dataDirectoryOffset = optMagic == 0x20B ? 112 : 96;
    std::size_t optionalSize = optMagic == 0x20B ? 240 : 224;
    std::size_t sectionsOffset = optionalOffset + optionalSize;
    std::uint32_t comRva = GetU32WebCil(pe,
        optionalOffset + dataDirectoryOffset + 14 * 8);
    const std::uint32_t rawBase = static_cast<std::uint32_t>(
        kWbilHeaderSize + sectionCount * 16);

    std::vector<std::uint8_t> payload;
    PutU32WebCil(payload, 0x4c496257u);  // "WbIL"
    PutU16WebCil(payload, 0);            // VersionMajor
    PutU16WebCil(payload, 0);            // VersionMinor
    PutU16WebCil(payload, static_cast<std::uint16_t>(sectionCount));
    PutU16WebCil(payload, 0);            // reserved0
    PutU32WebCil(payload, comRva);       // PECliHeaderRVA
    PutU32WebCil(payload, 72);           // PECliHeaderSize
    PutU32WebCil(payload, 0);            // PEDebugRVA
    PutU32WebCil(payload, 0);            // PEDebugSize
    for (std::uint32_t i = 0; i < sectionCount; i++) {
        std::size_t s = sectionsOffset + i * 40;
        std::uint32_t virtualSize = GetU32WebCil(pe, s + 8);
        std::uint32_t virtualAddress = GetU32WebCil(pe, s + 12);
        std::uint32_t rawDataSize = GetU32WebCil(pe, s + 16);
        std::uint32_t rawDataPtr = GetU32WebCil(pe, s + 20);
        PutU32WebCil(payload, virtualSize);
        PutU32WebCil(payload, virtualAddress);
        PutU32WebCil(payload, rawDataSize);
        PutU32WebCil(payload, static_cast<std::uint32_t>(
            rawBase + rawDataPtr));
    }
    // The raw data region starts right after the section table; the PE
    // bytes land verbatim there (the PE's own headers included, so every
    // RVA's containing-section walk lands on the copied bytes).
    payload.insert(payload.end(), pe.begin(), pe.end());

    // The WASM container: the magic + version, one Data section holding
    // the two segments (the skipped first, the WebCIL blob second).
    std::vector<std::uint8_t> container;
    PutU32WebCil(container, 0x6d736100u);  // "\0asm"
    PutU32WebCil(container, 1);            // the Wasm version
    container.push_back(11);               // WasmSectionId::Data
    std::vector<std::uint8_t> content;
    content.push_back(2);                  // two segments
    content.push_back(1);                  // segment 1 kind
    content.push_back(0);                  // segment 1 length
    content.push_back(1);                  // segment 2 kind
    std::uint32_t remaining = static_cast<std::uint32_t>(payload.size());
    while (remaining >= 0x80) {
        content.push_back(static_cast<std::uint8_t>(remaining) | 0x80);
        remaining >>= 7;
    }
    content.push_back(static_cast<std::uint8_t>(remaining));
    content.insert(content.end(), payload.begin(), payload.end());
    std::uint32_t remaining2 = static_cast<std::uint32_t>(content.size());
    while (remaining2 >= 0x80) {
        container.push_back(static_cast<std::uint8_t>(remaining2) | 0x80);
        remaining2 >>= 7;
    }
    container.push_back(static_cast<std::uint8_t>(remaining2));
    container.insert(container.end(), content.begin(), content.end());
    return container;
}

}  // namespace

// ---- The registry.

TEST(FileLoaderRegistryTest, DefaultRegistrationOrder)
{
    FL::FileLoaderRegistry registry;
    // Xamarin, WebCil, Bundle, PE, Archive (Metadata is the one remaining
    // documented deferral -- see LoadResult.hpp).
    ASSERT_EQ(registry.RegisteredLoaders().size(), 5u);
    EXPECT_NE(dynamic_cast<const FL::XamarinCompressedFileLoader*>(
                  registry.RegisteredLoaders()[0].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::WebCilFileLoader*>(
                  registry.RegisteredLoaders()[1].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::BundleFileLoader*>(
                  registry.RegisteredLoaders()[2].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::PEFileLoader*>(
                  registry.RegisteredLoaders()[3].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::ArchiveFileLoader*>(
                  registry.RegisteredLoaders()[4].get()),
        nullptr);
}

TEST(FileLoaderRegistryTest, RegisterAppendsAndRejectsNull)
{
    FL::FileLoaderRegistry registry;
    const std::size_t before = registry.RegisteredLoaders().size();
    registry.Register(std::make_unique<FL::PEFileLoader>());
    EXPECT_EQ(registry.RegisteredLoaders().size(), before + 1);
    EXPECT_NE(dynamic_cast<const FL::PEFileLoader*>(
                  registry.RegisteredLoaders().back().get()),
        nullptr);

    try {
        registry.Register(nullptr);
        FAIL() << "expected the ArgumentNullException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'loader')");
    }
}

TEST(FileLoaderRegistryTest, RegistryPicksTheRightLoader)
{
    FL::FileLoaderRegistry registry;
    fs::path dir = TempDir("select");
    FileLoadContext context;

    // A plain PE module: PEFileLoader claims it (the first three do not).
    std::string modulePath =
        WriteFile(dir, "tiny.netmodule", Bytes(TinyNetModuleBytes()));
    std::vector<std::uint8_t> image = Bytes(TinyNetModuleBytes());
    std::optional<LoadResult> result;
    for (const auto& loader : registry.RegisteredLoaders()) {
        result = loader->Load(modulePath, image.data(), image.size(), context);
        if (result)
            break;
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->MetadataFile, nullptr);
    EXPECT_TRUE(result->MetadataFile->IsValid());
    EXPECT_TRUE(result->Package == nullptr);
    EXPECT_TRUE(result->IsSuccess());

    // A zip: only the archive loader claims it.
    std::string zipPath = (dir / "pkg.zip").string();
    {
        mz_zip_archive zip{};
        ASSERT_TRUE(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0));
        const std::string text = "some content";
        ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "a.txt", text.data(),
            text.size(), MZ_DEFAULT_COMPRESSION));
        ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
        mz_zip_writer_end(&zip);
    }
    std::vector<std::uint8_t> zipBytes = Bytes("zip placeholder");
    {
        std::ifstream in(zipPath, std::ios::binary);
        zipBytes.assign((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
    }
    for (const auto& loader : registry.RegisteredLoaders()) {
        result = loader->Load(zipPath, zipBytes.data(), zipBytes.size(), context);
        if (result)
            break;
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->Package, nullptr);
    EXPECT_EQ(result->Package->Kind(), ILSpy::ILSpyX::LoadedPackage::PackageKind::Zip);
    EXPECT_TRUE(result->MetadataFile == nullptr);
}

// ---- PEFileLoader.

TEST(PEFileLoaderTest, MzGateClaimsOnlyMzImages)
{
    FL::PEFileLoader loader;
    FileLoadContext context;
    const std::string moduleBytes = TinyNetModuleBytes();

    const std::string empty;
    EXPECT_EQ(loader.Load("tiny.netmodule",
                   reinterpret_cast<const std::uint8_t*>(empty.data()),
                   empty.size(), context),
        std::nullopt);  // empty
    const std::string oneByte = "M";
    EXPECT_EQ(loader.Load("tiny.netmodule",
                   reinterpret_cast<const std::uint8_t*>(oneByte.data()),
                   oneByte.size(), context),
        std::nullopt);  // length 1
    const std::string zipped = "ZM hello";
    EXPECT_EQ(loader.Load("a.txt",
                   reinterpret_cast<const std::uint8_t*>(zipped.data()),
                   zipped.size(), context),
        std::nullopt);  // not MZ
}

TEST(PEFileLoaderTest, LoadsThePeModule)
{
    FL::PEFileLoader loader;
    FileLoadContext context;
    const std::string moduleBytes = TinyNetModuleBytes();
    auto result = loader.Load("tiny.netmodule",
        reinterpret_cast<const std::uint8_t*>(moduleBytes.data()),
        moduleBytes.size(), context);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->MetadataFile, nullptr);
    EXPECT_TRUE(result->MetadataFile->IsValid());
    EXPECT_EQ(result->MetadataFile->FileName(), "tiny.netmodule");
    EXPECT_TRUE(result->Package == nullptr);
    EXPECT_TRUE(result->IsSuccess());
}

TEST(PEFileLoaderTest, UnparseableMzImageIsInvalidNotThrown)
{
    // The port's never-throwing MetadataFile: garbage behind the MZ gate
    // reports IsValid() == false where the C# throws
    // BadImageFormatException (the established port divergence).
    FL::PEFileLoader loader;
    FileLoadContext context;
    const std::string garbage = "MZ garbage not a PE";
    auto result = loader.Load("fake.dll",
        reinterpret_cast<const std::uint8_t*>(garbage.data()),
        garbage.size(), context);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->MetadataFile, nullptr);
    EXPECT_FALSE(result->MetadataFile->IsValid());
}

// ---- BundleFileLoader.

TEST(BundleFileLoaderTest, ClaimsBundlesByFileNameAndHonorsParentBundle)
{
    FL::BundleFileLoader loader;
    FileLoadContext context;

    // Not a bundle: null.
    fs::path dir = TempDir("bundle");
    std::string plainPath =
        WriteFile(dir, "plain.dll", Bytes("MZ plain payload"));
    std::vector<std::uint8_t> plain = Bytes("MZ plain payload");
    EXPECT_EQ(loader.Load(plainPath, plain.data(), plain.size(), context),
        std::nullopt);

    // A real bundle (the LoadedPackage fixture shape): claimed by NAME
    // (the payload bytes are not even consulted).
    const std::uint8_t signature[32] = {
        0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38,
        0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
        0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18,
        0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae,
    };
    std::vector<std::uint8_t> image(16, 0);
    const std::size_t footerAt = image.size();
    image.insert(image.end(), 8, 0);
    image.insert(image.end(), signature, signature + 32);
    image.push_back('M');
    image.push_back('Z');
    const std::size_t manifestOffset = image.size();
    auto put32 = [&image](std::uint32_t v) {
        for (int i = 0; i < 4; i++)
            image.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto put64 = [&image](std::uint64_t v) {
        for (int i = 0; i < 8; i++)
            image.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto putString = [&image](const std::string& s) {
        std::uint32_t len = static_cast<std::uint32_t>(s.size());
        image.push_back(static_cast<std::uint8_t>(len));
        image.insert(image.end(), s.begin(), s.end());
    };
    put32(6);  // major
    put32(0);  // minor
    put32(1);  // file count
    putString("id");
    put64(0);  // deps json offset
    put64(0);
    put64(0);  // runtimeconfig offset
    put64(0);
    put64(0);  // flags
    // The one entry: the two MZ bytes placed above, at offset footerAt +
    // 40, size 2, uncompressed.
    put64(static_cast<std::uint64_t>(footerAt + 40));
    put64(2);
    put64(0);  // compressed size
    image.push_back(static_cast<std::uint8_t>(
        ILSpy::Decompiler::SingleFileBundle::FileType::Assembly));
    putString("app.dll");
    std::uint64_t footerValue = static_cast<std::uint64_t>(manifestOffset);
    std::memcpy(image.data() + footerAt, &footerValue, 8);

    std::string bundlePath = WriteFile(dir, "apphost.exe", image);
    auto result = loader.Load(bundlePath, plain.data(), plain.size(), context);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->Package, nullptr);
    EXPECT_EQ(result->Package->Kind(),
        ILSpy::ILSpyX::LoadedPackage::PackageKind::Bundle);
    EXPECT_EQ(result->Package->Entries().size(), 1u);
    EXPECT_EQ(result->Package->Entries()[0]->Name(), "app.dll");

    // The ParentBundle guard: a loader invoked from inside a bundle
    // declines. The guard reads the LoadedAssembly wrapper (the C#
    // FileLoadContext.ParentBundle), so load the bundle through a list to
    // obtain one.
    ILSpy::ILSpyX::AssemblyList list;
    auto& bundleAsm = list.OpenAssembly(bundlePath);
    FileLoadContext inside;
    inside.ParentBundle = &bundleAsm;
    EXPECT_EQ(loader.Load(bundlePath, image.data(), image.size(), inside),
        std::nullopt);
}

// ---- ArchiveFileLoader.

TEST(ArchiveFileLoaderTest, ClaimsZipsAndHonorsParentBundle)
{
    FL::ArchiveFileLoader loader;
    FileLoadContext context;

    fs::path dir = TempDir("archive");
    std::string zipPath = (dir / "pkg.zip").string();
    {
        mz_zip_archive zip{};
        ASSERT_TRUE(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0));
        const std::string text = "entry payload";
        ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "e.txt", text.data(),
            text.size(), MZ_DEFAULT_COMPRESSION));
        ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
        mz_zip_writer_end(&zip);
    }
    std::vector<std::uint8_t> placeholder = Bytes("irrelevant");

    auto result =
        loader.Load(zipPath, placeholder.data(), placeholder.size(), context);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->Package, nullptr);
    EXPECT_EQ(result->Package->Entries().size(), 1u);

    // A non-zip: the FromZipFile InvalidDataException is swallowed to
    // null.
    std::string plainPath =
        WriteFile(dir, "plain.bin", Bytes("MZ plain payload"));
    EXPECT_EQ(loader.Load(plainPath, placeholder.data(), placeholder.size(),
                   context),
        std::nullopt);

    // The ParentBundle guard (the wrapper the zip was loaded through).
    ILSpy::ILSpyX::AssemblyList list;
    auto& zipAsm = list.OpenAssembly(zipPath);
    FileLoadContext inside;
    inside.ParentBundle = &zipAsm;
    EXPECT_EQ(loader.Load(zipPath, placeholder.data(), placeholder.size(), inside),
        std::nullopt);
}

// ---- XamarinCompressedFileLoader.

TEST(XamarinCompressedFileLoaderTest, ShortAndNonXalzStreamsPassThrough)
{
    FL::XamarinCompressedFileLoader loader;
    FileLoadContext context;

    // Under 4 bytes: the magic read's EndOfStream -- the C# returns null
    // (too short to be an XALZ module; pass it through).
    EXPECT_EQ(loader.Load("a.bin",
                   reinterpret_cast<const std::uint8_t*>("XA"), 2, context),
        std::nullopt);
    // A different magic: not XALZ.
    const std::string other = "LZAX?not really";
    EXPECT_EQ(loader.Load("a.bin",
                   reinterpret_cast<const std::uint8_t*>(other.data()),
                   other.size(), context),
        std::nullopt);
}

TEST(XamarinCompressedFileLoaderTest, TruncatedHeaderThrows)
{
    FL::XamarinCompressedFileLoader loader;
    FileLoadContext context;
    // The magic identifies the file as XALZ, so the full 12-byte header is
    // mandatory.
    const std::vector<std::uint8_t> truncated = XalzWrap({}, 0);
    std::vector<std::uint8_t> partial(truncated.begin(), truncated.begin() + 8);
    try {
        loader.Load("a.dll", partial.data(), partial.size(), context);
        FAIL() << "expected the truncated-header error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid Xamarin compressed module: truncated header.");
    }
}

TEST(XamarinCompressedFileLoaderTest, ImplausibleDeclaredLengthsThrow)
{
    FL::XamarinCompressedFileLoader loader;
    FileLoadContext context;

    // A zero declared length.
    const std::vector<std::uint8_t> zero = XalzWrap({}, 0);
    try {
        loader.Load("a.dll", zero.data(), zero.size(), context);
        FAIL() << "expected the out-of-range error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid Xamarin compressed module: declared length is out of range.");
    }

    // A declared length beyond the 255x LZ4 expansion bound of the
    // payload.
    const std::string smallPayload(4, 'x');
    const std::vector<std::uint8_t> bomb =
        XalzWrap(Bytes(smallPayload), 4u * 255u + 1u);
    try {
        loader.Load("a.dll", bomb.data(), bomb.size(), context);
        FAIL() << "expected the out-of-range error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid Xamarin compressed module: declared length is out of range.");
    }

    // A header with no payload at all (compressed length 0).
    std::vector<std::uint8_t> bareHeader = XalzWrap({}, 100);
    try {
        loader.Load("a.dll", bareHeader.data(), bareHeader.size(), context);
        FAIL() << "expected the out-of-range error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid Xamarin compressed module: declared length is out of range.");
    }
}

TEST(XamarinCompressedFileLoaderTest, DecompressesAndLoadsTheModule)
{
    FL::XamarinCompressedFileLoader loader;
    FileLoadContext context;

    // LZ4-compress the tiny netmodule and wrap it in the XALZ header.
    const std::string moduleBytes = TinyNetModuleBytes();
    const int srcSize = static_cast<int>(moduleBytes.size());
    // lz4's bound: never larger than srcSize + srcSize/255 + 16.
    const int bound = srcSize + srcSize / 255 + 16;
    std::vector<char> compressed(static_cast<std::size_t>(bound));
    const int compressedSize =
        LZ4_compress_default(moduleBytes.data(), compressed.data(),
            srcSize, bound);
    ASSERT_GT(compressedSize, 0);
    compressed.resize(static_cast<std::size_t>(compressedSize));

    std::vector<std::uint8_t> xalz = XalzWrap(
        std::vector<std::uint8_t>(compressed.begin(), compressed.end()),
        static_cast<std::uint32_t>(moduleBytes.size()));

    auto result =
        loader.Load("tiny.dll", xalz.data(), xalz.size(), context);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->MetadataFile, nullptr);
    EXPECT_TRUE(result->MetadataFile->IsValid());
    EXPECT_EQ(result->MetadataFile->FileName(), "tiny.dll");
    EXPECT_EQ(result->MetadataFile->Name(), "tiny");  // the netmodule.s module name
}

TEST(XamarinCompressedFileLoaderTest, CorruptPayloadThrowsTheSizeMismatch)
{
    FL::XamarinCompressedFileLoader loader;
    FileLoadContext context;

    // A payload that decodes short of the declared length (the magic and
    // lengths are plausible; the LZ4 stream itself is the wrong size).
    const std::string moduleBytes = TinyNetModuleBytes();
    const int srcSize = static_cast<int>(moduleBytes.size());
    const int bound = srcSize + srcSize / 255 + 16;
    std::vector<char> compressed(static_cast<std::size_t>(bound));
    const int compressedSize =
        LZ4_compress_default(moduleBytes.data(), compressed.data(),
            srcSize, bound);
    ASSERT_GT(compressedSize, 0);
    compressed.resize(static_cast<std::size_t>(compressedSize));

    std::vector<std::uint8_t> xalz = XalzWrap(
        std::vector<std::uint8_t>(compressed.begin(), compressed.end()),
        static_cast<std::uint32_t>(moduleBytes.size() + 10));
    try {
        loader.Load("tiny.dll", xalz.data(), xalz.size(), context);
        FAIL() << "expected the decompressed-size error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid Xamarin compressed module: decompressed size does not "
            "match the header.");
    }
}

// The WebCilFileLoader over the real registry surface: the container
// written to disk, loaded through the loader, presented as the PE-shaped
// MetadataFile.
TEST(WebCilFileLoaderTest, LoadsAValidContainerAsThePeShape)
{
    // A valid container over the ConnIdRes metadata, written to disk.
    std::vector<std::uint8_t> metadata = ExtractMetadataFromConnIdRes();
    std::vector<std::uint8_t> container =
        BuildWebCilContainerOver(metadata);
    std::string path = fs::temp_directory_path() /
        ("ilspy_webcil_loader_" + std::to_string(std::rand()) + ".wasm");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(container.data()),
            static_cast<std::streamsize>(container.size()));
    }

    FL::WebCilFileLoader loader;
    FL::FileLoadContext context;  // no parent bundle
    auto result = loader.Load(path, container.data(), container.size(),
        context);
    std::error_code ec;
    fs::remove(fs::path(path), ec);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->MetadataFile, nullptr);
    EXPECT_TRUE(result->MetadataFile->IsValid());
    auto asmDef = result->MetadataFile->GetAssemblyDefinition();
    ASSERT_TRUE(asmDef.has_value());
    EXPECT_EQ(asmDef->Name, "connid_res");
}

TEST(WebCilFileLoaderTest, DeclinesInsideABundle)
{
    // The C# `if (settings.ParentBundle != null) return null;`: a bundle
    // entry is never a WebCIL container.
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& bundle = list.OpenAssembly(file);
    FL::WebCilFileLoader loader;
    FL::FileLoadContext context;
    context.ParentBundle = &bundle;
    EXPECT_FALSE(loader
            .Load(file, nullptr, 0, context)
            .has_value());
}

// The full pipeline over a faithful container: the whole ConnIdRes PE
// wrapped verbatim (its own section table, its CLI header at its own
// COM-directory RVA) loads through the manager-backed AssemblyList (the
// C# GUI path: the manager carries the default loader registry -- the
// bare testing AssemblyList ctor has a null registry and would fall
// through to the PE fallback) -- the registry loop, the loaded-module
// registration, the metadata surface, the method bodies, and the type
// system all resolve through the adapter.
TEST(WebCilFileLoaderTest, FullPipelineOverAFaithfulContainer)
{
    std::string pePath = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(pePath, std::ios::binary);
    std::vector<std::uint8_t> pe(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    ASSERT_GE(pe.size(), 512u);

    std::vector<std::uint8_t> container =
        BuildWebCilContainerOverPe(pe);
    std::string containerPath = fs::temp_directory_path() /
        "ilspy_webcil_connid_full.wasm";
    {
        std::ofstream out(containerPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(container.data()),
            static_cast<std::streamsize>(container.size()));
    }

    // The whole LoadedAssembly pipeline: open, demand, inspect. The
    // manager-backed list carries the default registry (the C# app
    // path).
    auto provider = std::make_shared<ILSpy::Tests::InMemorySettingsProvider>();
    ILSpy::ILSpyX::AssemblyListManager manager(provider);
    AssemblyList list(manager, "WebCilE2E");
    LoadedAssembly& asm_ = list.OpenAssembly(containerPath);
    const auto& loadResult = asm_.GetLoadResult();
    ASSERT_NE(loadResult.MetadataFile, nullptr);
    ASSERT_TRUE(loadResult.MetadataFile->IsValid());
    EXPECT_EQ(asm_.ShortName(), "ilspy_webcil_connid_full");
    auto asmDef = loadResult.MetadataFile->GetAssemblyDefinition();
    ASSERT_TRUE(asmDef.has_value());
    EXPECT_EQ(asmDef->Name, "connid_res");

    // The method bodies resolve through the WebCIL translation (the
    // ConnIdRes fixture's bodies live at their PE RVAs inside the
    // wrapped image).
    auto methods = loadResult.MetadataFile->MethodDefs();
    int bodiesDecoded = 0;
    for (const auto& m : methods) {
        if (m.RVA == 0) continue;
        auto body = loadResult.MetadataFile->GetMethodBody(m.RVA);
        if (body.IsValid()) ++bodiesDecoded;
    }
    EXPECT_GT(bodiesDecoded, 0) << "no WebCIL method body decoded";

    // The type-system arm (the C# IModuleReference.Resolve): the
    // compilation over the WebCIL-loaded module resolves its types.
    auto compilation = ILSpy::ILSpyX::GetTypeSystemOrNull(
        *loadResult.MetadataFile);
    ASSERT_NE(compilation, nullptr);
    EXPECT_EQ(compilation->MainModule().AssemblyName(), "connid_res");

    std::error_code ec;
    fs::remove(fs::path(containerPath), ec);
}

// The corpus-anchored proving case: the real net48 mscorlib wrapped in
// a faithful WebCIL container; its thousands of real method bodies
// decode through the WebCIL translation identically to the plain PE.
// Gated on ILSPY_TEST_MSCORLIB (the MethodBody_Test pattern) because
// the corpus is provisioned out of band.
TEST(WebCilFileLoaderTest, CorpuMscorlibBodiesDecodeThroughWebCil) {
    const char* env = std::getenv("ILSPY_TEST_MSCORLIB");
    if (env == nullptr || !std::filesystem::exists(env)) {
        GTEST_SKIP() << "corpus not provisioned";
    }
    std::ifstream in(env, std::ios::binary);
    std::vector<std::uint8_t> pe(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    ASSERT_GE(pe.size(), 1024u);

    std::vector<std::uint8_t> container =
        BuildWebCilContainerOverPe(pe);
    std::string containerPath = fs::temp_directory_path() /
        "ilspy_webcil_mscorlib.wasm";
    {
        std::ofstream out(containerPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(container.data()),
            static_cast<std::streamsize>(container.size()));
    }

    // The manager-backed list (the C# app path: the manager carries the
    // default loader registry; the bare testing AssemblyList has none).
    auto provider = std::make_shared<ILSpy::Tests::InMemorySettingsProvider>();
    ILSpy::ILSpyX::AssemblyListManager manager(provider);
    AssemblyList list(manager, "WebCilCorpus");
    LoadedAssembly& asm_ = list.OpenAssembly(containerPath);
    const auto& loadResult = asm_.GetLoadResult();
    ASSERT_NE(loadResult.MetadataFile, nullptr);
    ASSERT_TRUE(loadResult.MetadataFile->IsValid());

    auto methods = loadResult.MetadataFile->MethodDefs();
    ASSERT_GT(methods.size(), 1000u);
    int tiny = 0, fat = 0, decoded = 0;
    for (const auto& m : methods) {
        if (m.RVA == 0) continue;  // abstract/extern/pinvoke-only
        auto body = loadResult.MetadataFile->GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        // The IL span length must match CodeSize for every decoded body
        // (the MethodBody_Test invariant, now through the WebCIL path).
        ASSERT_EQ(body.IL().size(), body.CodeSize());
        ++decoded;
        if (body.IsFat()) ++fat; else ++tiny;
    }
    EXPECT_GT(decoded, 1000) << "decoded too few WebCIL method bodies";
    EXPECT_GT(tiny, 0);
    EXPECT_GT(fat, 0);

    std::error_code ec;
    fs::remove(fs::path(containerPath), ec);
}
