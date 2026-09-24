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
#include "ILSpyX/FileLoaders/XamarinCompressedFileLoader.hpp"

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

}  // namespace

// ---- The registry.

TEST(FileLoaderRegistryTest, DefaultRegistrationOrder)
{
    FL::FileLoaderRegistry registry;
    // Xamarin, Bundle, PE, Archive (WebCil and Metadata are the documented
    // deferrals -- see LoadResult.hpp).
    ASSERT_EQ(registry.RegisteredLoaders().size(), 4u);
    EXPECT_NE(dynamic_cast<const FL::XamarinCompressedFileLoader*>(
                  registry.RegisteredLoaders()[0].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::BundleFileLoader*>(
                  registry.RegisteredLoaders()[1].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::PEFileLoader*>(
                  registry.RegisteredLoaders()[2].get()),
        nullptr);
    EXPECT_NE(dynamic_cast<const FL::ArchiveFileLoader*>(
                  registry.RegisteredLoaders()[3].get()),
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
