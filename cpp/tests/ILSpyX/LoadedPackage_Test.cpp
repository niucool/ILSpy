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

// Tests for the LoadedPackage port (ICSharpCode.ILSpyX/LoadedPackage.cs):
// the folder-tree construction, the zip package (built with miniz's
// writer, read back through the package model), the bundle package (the
// DumpPackage fixture shape), and the Resource / ByteArrayResource base
// surface.

#include "ILSpyX/LoadedPackage.hpp"

#include "TestFixtures/DiscoveryNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <miniz/miniz.h>
#include <miniz/miniz_zip.h>

namespace {

namespace fs = std::filesystem;
namespace Sfb = ILSpy::Decompiler::SingleFileBundle;
using ILSpy::ILSpyX::ByteArrayResource;
using ILSpy::ILSpyX::LoadedPackage;
using ILSpy::ILSpyX::PackageEntry;
using ILSpy::ILSpyX::PackageFolder;

std::vector<std::uint8_t> Bytes(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::string Text(const std::optional<std::vector<std::uint8_t>>& blob)
{
    if (!blob)
        return "(nullopt)";
    return std::string(blob->begin(), blob->end());
}

fs::path TempDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("ilspy_loadedpackage_" + name);
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

// ---- The bundle fixture (the DumpPackage test's MakeBundle shape).

const std::uint8_t kSignature[32] = {
    0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38,
    0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
    0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18,
    0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae,
};

void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void PutI32(std::vector<std::uint8_t>& b, std::int32_t v)
{
    PutU32(b, static_cast<std::uint32_t>(v));
}

void PutI64(std::vector<std::uint8_t>& b, long long v)
{
    for (int i = 0; i < 8; i++)
        b.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(v)) >> (8 * i)));
}

void PutU64(std::vector<std::uint8_t>& b, std::uint64_t v)
{
    for (int i = 0; i < 8; i++)
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void PutLengthPrefixedString(std::vector<std::uint8_t>& b, const std::string& s)
{
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    while (len >= 0x80) {
        b.push_back(static_cast<std::uint8_t>(len) | 0x80);
        len >>= 7;
    }
    b.push_back(static_cast<std::uint8_t>(len));
    b.insert(b.end(), s.begin(), s.end());
}

struct BundleEntrySpec {
    std::string path;
    std::vector<std::uint8_t> payload;  // raw bytes, or the raw-deflate stream
    long long declaredSize = -1;       // the Size column (defaults to payload)
    bool compressed = false;
};

std::vector<std::uint8_t> MakeBundle(const std::string& bundleId,
    const std::vector<BundleEntrySpec>& entries)
{
    std::vector<std::uint8_t> b(16, 0);
    const std::size_t footerAt = b.size();
    b.insert(b.end(), 8, 0);
    b.insert(b.end(), kSignature, kSignature + 32);
    std::vector<std::pair<long long, long long>> placed;  // offset, stored size
    for (const BundleEntrySpec& e : entries) {
        placed.push_back({ static_cast<long long>(b.size()),
            static_cast<long long>(e.payload.size()) });
        b.insert(b.end(), e.payload.begin(), e.payload.end());
    }
    const std::size_t manifestOffset = b.size();
    PutU32(b, 6);  // major
    PutU32(b, 0);  // minor
    PutI32(b, static_cast<std::int32_t>(entries.size()));
    PutLengthPrefixedString(b, bundleId);
    PutI64(b, 0);  // deps json offset
    PutI64(b, 0);
    PutI64(b, 0);  // runtimeconfig json offset
    PutI64(b, 0);
    PutU64(b, 0);  // flags
    for (std::size_t i = 0; i < entries.size(); i++) {
        PutI64(b, placed[i].first);
        PutI64(b, entries[i].declaredSize >= 0 ? entries[i].declaredSize
                                               : placed[i].second);
        PutI64(b, entries[i].compressed ? placed[i].second : 0);
        b.push_back(static_cast<std::uint8_t>(Sfb::FileType::Assembly));
        PutLengthPrefixedString(b, entries[i].path);
    }
    std::uint64_t footerValue = static_cast<std::uint64_t>(manifestOffset);
    std::memcpy(b.data() + footerAt, &footerValue, 8);
    return b;
}

// ---- A fake entry the ctor-tree tests feed directly.

class StubEntry final : public PackageEntry {
public:
    StubEntry(std::string name, std::string content)
        : name_(std::move(name)), content_(std::move(content))
    {
    }

    std::string Name() const override { return name_; }
    std::string PackageQualifiedFileName() const override
    {
        return "stub://" + name_;
    }
    std::string FullName() const override { return name_; }
    std::optional<std::vector<std::uint8_t>> TryOpenStream() const override
    {
        return Bytes(content_);
    }
    std::optional<long long> TryGetLength() const override
    {
        return static_cast<long long>(content_.size());
    }

private:
    std::string name_;
    std::string content_;
};

std::shared_ptr<PackageEntry> Stub(std::string name, std::string content = "")
{
    return std::make_shared<StubEntry>(std::move(name), std::move(content));
}

const PackageFolder* FindFolder(const PackageFolder& folder,
    const std::string& name)
{
    for (const auto& sub : folder.Folders()) {
        if (sub->Name() == name)
            return sub.get();
    }
    return nullptr;
}

}  // namespace

// ---- The Resource base surface.

TEST(LoadedPackageTest, ByteArrayResourceServesItsBytes)
{
    ByteArrayResource resource("name", Bytes("payload"));
    EXPECT_EQ(resource.Name(), "name");
    EXPECT_TRUE(Text(resource.TryOpenStream()).empty() == false);
    EXPECT_EQ(Text(resource.TryOpenStream()), "payload");
    EXPECT_EQ(resource.TryGetLength(), std::optional<long long>(7));
    // The C# defaults: Embedded, Public.
    EXPECT_EQ(resource.ResourceType(),
        ILSpy::ILSpyX::ResourceType::Embedded);
    EXPECT_EQ(resource.Attributes(), 0x01u);
}

// ---- The folder tree construction.

TEST(LoadedPackageTest, CtorBuildsTheFolderTree)
{
    std::vector<std::shared_ptr<PackageEntry>> entries{
        Stub("root.txt", "r"),
        Stub("dir/sub/file.txt", "a"),
        Stub("dir/back.txt", "b"),
        Stub("dir/"),           // a directory row: no file name
        Stub("other/deep/x.txt", "c"),
    };
    LoadedPackage package(LoadedPackage::PackageKind::Zip, entries);

    EXPECT_EQ(package.Kind(), LoadedPackage::PackageKind::Zip);
    EXPECT_EQ(package.Entries().size(), 5u);
    EXPECT_FALSE(package.BundleHeader().has_value());

    const PackageFolder& root = package.RootFolder();
    EXPECT_EQ(root.Name(), "");
    EXPECT_EQ(root.Parent(), nullptr);
    // root.txt plus the two top-level folders.
    ASSERT_EQ(root.Entries().size(), 1u);
    EXPECT_EQ(root.Entries()[0]->Name(), "root.txt");
    ASSERT_EQ(root.Folders().size(), 2u);
    EXPECT_EQ(root.Folders()[0]->Name(), "dir");
    EXPECT_EQ(root.Folders()[1]->Name(), "other");

    const PackageFolder* dir = root.Folders()[0].get();
    EXPECT_EQ(dir->Parent(), &root);
    ASSERT_EQ(dir->Entries().size(), 1u);
    EXPECT_EQ(dir->Entries()[0]->Name(), "back.txt");
    ASSERT_EQ(dir->Folders().size(), 1u);

    const PackageFolder* sub = dir->Folders()[0].get();
    EXPECT_EQ(sub->Name(), "sub");
    ASSERT_EQ(sub->Entries().size(), 1u);
    EXPECT_EQ(sub->Entries()[0]->Name(), "file.txt");
    // The FolderEntry rename: the tree's Name is the basename, FullName
    // keeps the stored path.
    EXPECT_EQ(sub->Entries()[0]->FullName(), "dir/sub/file.txt");

    const PackageFolder* deep = FindFolder(*root.Folders()[1], "deep");
    ASSERT_NE(deep, nullptr);
    ASSERT_EQ(deep->Entries().size(), 1u);
    EXPECT_EQ(deep->Entries()[0]->Name(), "x.txt");
}

TEST(LoadedPackageTest, CtorTreatsBackslashSeparatorsAsDirectories)
{
    std::vector<std::shared_ptr<PackageEntry>> entries{
        Stub("a\\b\\c.txt", "x"),
    };
    LoadedPackage package(LoadedPackage::PackageKind::Bundle, entries);
    const PackageFolder* a = FindFolder(package.RootFolder(), "a");
    ASSERT_NE(a, nullptr);
    const PackageFolder* b = FindFolder(*a, "b");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->Entries().size(), 1u);
    EXPECT_EQ(b->Entries()[0]->Name(), "c.txt");
}

// ---- The zip package.

TEST(LoadedPackageTest, FromZipFileReadsEntriesAndStreams)
{
    fs::path dir = TempDir("zip");
    std::string zipPath = (dir / "pkg.zip").string();
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0));
    const std::string hello = "hello zip payload";
    const std::string nested = "nested content";
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "top.txt",
        hello.data(), hello.size(), MZ_DEFAULT_COMPRESSION));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "lib/sub.dll",
        nested.data(), nested.size(), MZ_DEFAULT_COMPRESSION));
    ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);

    auto package = LoadedPackage::FromZipFile(zipPath);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->Kind(), LoadedPackage::PackageKind::Zip);
    ASSERT_EQ(package->Entries().size(), 2u);

    EXPECT_EQ(package->Entries()[0]->Name(), "top.txt");
    EXPECT_EQ(package->Entries()[0]->FullName(), "top.txt");
    EXPECT_EQ(package->Entries()[0]->PackageQualifiedFileName(),
        "zip://" + zipPath + ";top.txt");
    EXPECT_EQ(Text(package->Entries()[0]->TryOpenStream()), hello);
    // "hello zip payload" is 17 bytes.
    EXPECT_EQ(package->Entries()[0]->TryGetLength(),
        std::optional<long long>(17));

    EXPECT_EQ(package->Entries()[1]->Name(), "lib/sub.dll");
    EXPECT_EQ(Text(package->Entries()[1]->TryOpenStream()), nested);
    // "nested content" is 14 bytes.
    EXPECT_EQ(package->Entries()[1]->TryGetLength(),
        std::optional<long long>(14));

    // The folder tree mirrors the zip structure.
    const PackageFolder* lib = FindFolder(package->RootFolder(), "lib");
    ASSERT_NE(lib, nullptr);
    ASSERT_EQ(lib->Entries().size(), 1u);
    EXPECT_EQ(lib->Entries()[0]->Name(), "sub.dll");
}

TEST(LoadedPackageTest, FromZipFileThrowsForNonZipFiles)
{
    fs::path dir = TempDir("badzip");
    std::string path = WriteFile(dir, "not-a.zip", Bytes("MZ? just text"));
    try {
        LoadedPackage::FromZipFile(path);
        FAIL() << "expected the End-of-Central-Directory error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "End of Central Directory record could not be found.");
    }
}

TEST(LoadedPackageTest, FromZipFileDirectoryRowsCarryNoFileName)
{
    // A zip whose only member is a directory row ("dir/"): the entry list
    // keeps it, but the folder tree gets no file for it.
    fs::path dir = TempDir("zipdir");
    std::string zipPath = (dir / "dirs.zip").string();
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "somedir/",
        nullptr, 0, MZ_DEFAULT_COMPRESSION));
    ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);

    auto package = LoadedPackage::FromZipFile(zipPath);
    ASSERT_NE(package, nullptr);
    // A directory row has an empty final component, and the C# tree only
    // materializes folders for entries WITH file names -- so the row
    // contributes nothing at all.
    EXPECT_TRUE(package->RootFolder().Folders().empty());
}

// ---- The bundle package.

TEST(LoadedPackageTest, FromBundleReadsEntriesUncompressedAndCompressed)
{
    fs::path dir = TempDir("bundle");
    const std::string plain = "plain bundle payload";
    const std::string squishy = "repeated repeated repeated payload";
    const std::string deflated = ILSpy::Tests::StoredDeflate(
        reinterpret_cast<const std::uint8_t*>(squishy.data()),
        squishy.size());
    std::vector<std::uint8_t> image = MakeBundle("bundle-id", {
        { "app.dll", Bytes(plain), -1, false },
        { "squished.dll", Bytes(deflated),
            static_cast<long long>(squishy.size()), true },
    });
    std::string bundlePath = WriteFile(dir, "app.exe", image);

    auto package = LoadedPackage::FromBundle(bundlePath);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->Kind(), LoadedPackage::PackageKind::Bundle);
    ASSERT_TRUE(package->BundleHeader().has_value());
    EXPECT_EQ(package->BundleHeader()->BundleID, "bundle-id");
    EXPECT_EQ(package->BundleHeader()->FileCount, 2);
    ASSERT_EQ(package->Entries().size(), 2u);

    EXPECT_EQ(package->Entries()[0]->Name(), "app.dll");
    EXPECT_EQ(package->Entries()[0]->FullName(), "app.dll");
    EXPECT_EQ(package->Entries()[0]->PackageQualifiedFileName(),
        "bundle://" + bundlePath + ";app.dll");
    EXPECT_EQ(Text(package->Entries()[0]->TryOpenStream()), plain);
    // "plain bundle payload" is 20 bytes.
    EXPECT_EQ(package->Entries()[0]->TryGetLength(),
        std::optional<long long>(20));

    EXPECT_EQ(package->Entries()[1]->Name(), "squished.dll");
    EXPECT_EQ(Text(package->Entries()[1]->TryOpenStream()), squishy);
    // "repeated repeated repeated payload" is 34 bytes.
    EXPECT_EQ(package->Entries()[1]->TryGetLength(),
        std::optional<long long>(34));
}

TEST(LoadedPackageTest, FromBundleYieldsNullForNonBundles)
{
    fs::path dir = TempDir("notbundle");
    std::string path = WriteFile(dir, "plain.exe", Bytes("MZ payload"));
    EXPECT_EQ(LoadedPackage::FromBundle(path), nullptr);
}

TEST(LoadedPackageTest, BundleEntrySizeMismatchIsCorrupted)
{
    fs::path dir = TempDir("corrupt");
    const std::string squishy = "repeated repeated repeated payload";
    const std::string deflated = ILSpy::Tests::StoredDeflate(
        reinterpret_cast<const std::uint8_t*>(squishy.data()),
        squishy.size());
    // Declare a size one byte short of the true decompressed length (34).
    std::vector<std::uint8_t> image = MakeBundle("id", {
        { "bad.dll", Bytes(deflated),
            static_cast<long long>(squishy.size() - 1), true },
    });
    std::string bundlePath = WriteFile(dir, "bad.exe", image);

    auto package = LoadedPackage::FromBundle(bundlePath);
    ASSERT_NE(package, nullptr);
    try {
        package->Entries()[0]->TryOpenStream();
        FAIL() << "expected the corrupted-entry error";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Corrupted single-file entry 'bad.dll'. Declared decompressed "
            "size '33' is not the same as actual decompressed size '34'.");
    }
}
