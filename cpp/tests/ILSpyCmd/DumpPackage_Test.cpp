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

// The CLI -d/--dump-package path (IlspyCmdProgram DumpPackage +
// ResolveOutputDirectory): the bundle extraction into the output directory.
// Every stderr line, exit code and extracted byte is pinned against the real
// C# ilspycmd behaviour -- the "Cannot dump assembiles" misspelling included
// -- and the compressed-entry semantics (trailing bytes accepted, truncated
// streams yield their partial production) were probed against real
// System.IO.Compression.DeflateStream on .NET 10. The real .NET 10
// single-file bundle fixture (produced with dotnet publish
// -p:PublishSingleFile=true) is glob-gated: when the local fixture and its
// real-tool dump are present the test verifies the port's extraction against
// the C# engine's own output byte for byte.

#include "ILSpyCmd/IlspyCmdProgram.hpp"
#include "Decompiler/SingleFileBundle.hpp"
#include "TestFixtures/DiscoveryNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace Cmd = ILSpy::ILSpyCmd;
namespace Sfb = ILSpy::Decompiler::SingleFileBundle;
using ILSpy::Tests::StoredDeflate;

// The 32-byte bundle signature: SHA-256 for ".net core bundle".
const std::uint8_t kSignature[32] = {
    0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38,
    0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
    0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18,
    0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae,
};

void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void PutI32(std::vector<std::uint8_t>& b, std::int32_t v) {
    PutU32(b, static_cast<std::uint32_t>(v));
}

void PutI64(std::vector<std::uint8_t>& b, long long v) {
    for (int i = 0; i < 8; i++) b.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v)) >> (8 * i)));
}

void PutU64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 0; i < 8; i++) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void PutLengthPrefixedString(std::vector<std::uint8_t>& b, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    while (len >= 0x80) {
        b.push_back(static_cast<std::uint8_t>(len) | 0x80);
        len >>= 7;
    }
    b.push_back(static_cast<std::uint8_t>(len));
    b.insert(b.end(), s.begin(), s.end());
}

// One manifest entry with the payload bytes to embed in the bundle's file
// region (compressed entries take a raw-deflate stream as their payload and
// declare the plain size separately).
struct DumpEntry {
    std::string path;
    std::vector<std::uint8_t> payload;   // raw bytes, or the deflate stream
    long long declaredSize = -1;         // the Size column (defaults to payload size)
    Sfb::FileType type = Sfb::FileType::Assembly;
    bool compressed = false;
};

// A complete single-file bundle image: [16-byte padding][footer (manifest
// offset + signature)][the entries' payload bytes laid out sequentially]
// [v6 manifest]. Absolute entry offsets are computed by the builder.
std::vector<std::uint8_t> MakeBundle(const std::string& bundleId, const std::vector<DumpEntry>& entries) {
    std::vector<std::uint8_t> b(16, 0);
    const std::size_t footerAt = b.size();
    b.insert(b.end(), 8, 0);
    b.insert(b.end(), kSignature, kSignature + 32);
    std::vector<std::pair<long long, long long>> placed;  // offset, stored size
    for (const DumpEntry& e : entries) {
        placed.push_back({ static_cast<long long>(b.size()), static_cast<long long>(e.payload.size()) });
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
        PutI64(b, entries[i].declaredSize >= 0 ? entries[i].declaredSize : placed[i].second);
        PutI64(b, entries[i].compressed ? placed[i].second : 0);
        b.push_back(static_cast<std::uint8_t>(entries[i].type));
        PutLengthPrefixedString(b, entries[i].path);
    }
    std::uint64_t footerValue = static_cast<std::uint64_t>(manifestOffset);
    std::memcpy(b.data() + footerAt, &footerValue, 8);
    return b;
}

std::vector<std::uint8_t> Bytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::vector<std::uint8_t> DeflateBytes(const std::string& s) {
    return Bytes(StoredDeflate(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

// A fresh directory under the system temp dir, cleaned before use.
fs::path TempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("ilspy_dumppackage_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// Writes the bundle bytes to a file and returns its path (DumpPackage takes a
// file name and reads the image itself).
std::string WriteTempFile(const fs::path& dir, const std::string& name,
    const std::vector<std::uint8_t>& bytes) {
    fs::path file = dir / name;
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return file.string();
}

std::vector<std::uint8_t> ReadFileBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
}

std::string ReadFileText(const fs::path& p) {
    auto bytes = ReadFileBytes(p);
    return std::string(bytes.begin(), bytes.end());
}

// ---------------------------------------------------------------------------
// ResolveOutputDirectory
// ---------------------------------------------------------------------------

TEST(DumpPackageOutputDirectory, UnsetAndWhitespaceResolveToNullopt) {
    // The C# `string.IsNullOrWhiteSpace(outputDirectory)` arm: no -o value
    // means "standard out" for the writers and, for -d, the Path.Combine
    // ArgumentNullException below.
    EXPECT_FALSE(Cmd::ResolveOutputDirectory("").has_value());
    EXPECT_FALSE(Cmd::ResolveOutputDirectory("   ").has_value());
    EXPECT_FALSE(Cmd::ResolveOutputDirectory("\t").has_value());
}

TEST(DumpPackageOutputDirectory, RelativeValueResolvesAgainstCurrentDirectory) {
    auto resolved = Cmd::ResolveOutputDirectory("some-dir");
    ASSERT_TRUE(resolved.has_value());
    // Path.GetFullPath resolves against the current directory.
    EXPECT_EQ(fs::path(*resolved), (fs::current_path() / "some-dir").lexically_normal());

    // '.'/'..' components normalize away (GetFullPath behavior).
    auto up = Cmd::ResolveOutputDirectory("a/../b");
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(fs::path(*up), (fs::current_path() / "b").lexically_normal());
}

// ---------------------------------------------------------------------------
// DumpPackage over synthetic bundles
// ---------------------------------------------------------------------------

TEST(DumpPackage, ExtractsRawSubdirAndCompressedEntries) {
    fs::path dir = TempDir("extract");
    std::vector<DumpEntry> entries;
    DumpEntry raw;
    raw.path = "app.dll";
    raw.payload = Bytes("MZ\x90\x00 raw payload");
    entries.push_back(raw);

    DumpEntry sub;
    sub.path = "sub/dir/data.txt";
    sub.payload = Bytes("nested data");
    sub.type = Sfb::FileType::DepsJson;
    entries.push_back(sub);

    DumpEntry zipped;
    zipped.path = "zipped.bin";
    zipped.payload = DeflateBytes("compressed payload!");
    zipped.declaredSize = 19;
    zipped.compressed = true;
    zipped.type = Sfb::FileType::NativeBinary;
    entries.push_back(zipped);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("bundle-id", entries));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(bundlePath, dir.string(), errorOutput);

    ASSERT_EQ(rc, 0) << errorOutput.str();
    EXPECT_EQ(errorOutput.str(), "");
    EXPECT_EQ(ReadFileText(dir / "app.dll"), "MZ\x90\x00 raw payload");
    EXPECT_EQ(ReadFileText(dir / "sub" / "dir" / "data.txt"), "nested data");
    EXPECT_EQ(ReadFileText(dir / "zipped.bin"), "compressed payload!");
}

TEST(DumpPackage, CompressedEntryAcceptsTrailingBytes) {
    // The C# DeflateStream stops reading at the final block's end: bytes
    // after the deflate stream inside the compressed region are accepted
    // (probed against real System.IO.Compression).
    fs::path dir = TempDir("trailing");
    std::vector<std::uint8_t> deflate = DeflateBytes("payload");
    deflate.insert(deflate.end(), { 0xDE, 0xAD, 0xBE, 0xEF });

    std::vector<DumpEntry> entries;
    DumpEntry zipped;
    zipped.path = "z.bin";
    zipped.payload = deflate;
    zipped.declaredSize = 7;
    zipped.compressed = true;
    entries.push_back(zipped);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("id", entries));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(bundlePath, dir.string(), errorOutput);

    ASSERT_EQ(rc, 0) << errorOutput.str();
    EXPECT_EQ(errorOutput.str(), "");
    EXPECT_EQ(ReadFileText(dir / "z.bin"), "payload");
}

TEST(DumpPackage, SkipsEscapingEntryPaths) {
    // The C# guard: RelativePath with a "../" component (after
    // backslash-to-slash normalization) or a rooted path is skipped with the
    // exact stderr line; the dump continues with the remaining entries.
    fs::path dir = TempDir("escape");
    std::vector<DumpEntry> entries;
    DumpEntry up;
    up.path = "../evil.txt";
    up.payload = Bytes("no");
    entries.push_back(up);

    DumpEntry upBack;
    upBack.path = "..\\evil2.txt";
    upBack.payload = Bytes("no");
    entries.push_back(upBack);

    DumpEntry rooted;
    rooted.path = "C:\\evil3.txt";
    rooted.payload = Bytes("no");
    entries.push_back(rooted);

    DumpEntry slash;
    slash.path = "/evil4.txt";
    slash.payload = Bytes("no");
    entries.push_back(slash);

    DumpEntry drive;
    drive.path = "Q:relative.txt";  // drive-relative is rooted too
    drive.payload = Bytes("no");
    entries.push_back(drive);

    DumpEntry fine;
    fine.path = "a..b/ok.txt";  // ".." inside a component is NOT a traversal
    fine.payload = Bytes("yes");
    entries.push_back(fine);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("id", entries));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(bundlePath, dir.string(), errorOutput);

    ASSERT_EQ(rc, 0) << errorOutput.str();
    // Each skip renders the ORIGINAL RelativePath (the backslash forms
    // included) with the CRLF TextWriter convention.
    EXPECT_EQ(errorOutput.str(),
        "Skipping single-file entry '../evil.txt' because it might refer to a"
        " location outside of the bundle output directory.\r\n"
        "Skipping single-file entry '..\\evil2.txt' because it might refer to a"
        " location outside of the bundle output directory.\r\n"
        "Skipping single-file entry 'C:\\evil3.txt' because it might refer to a"
        " location outside of the bundle output directory.\r\n"
        "Skipping single-file entry '/evil4.txt' because it might refer to a"
        " location outside of the bundle output directory.\r\n"
        "Skipping single-file entry 'Q:relative.txt' because it might refer to a"
        " location outside of the bundle output directory.\r\n");
    EXPECT_EQ(ReadFileText(dir / "a..b" / "ok.txt"), "yes");
    EXPECT_FALSE(fs::exists(dir / "..\\evil2.txt"));
    EXPECT_FALSE(fs::exists(dir / "evil.txt"));
}

TEST(DumpPackage, CorruptedCompressedEntryStopsTheDump) {
    // A declared size that disagrees with the produced size: the exact
    // message plus EX_DATAERR, and the dump returns without writing the
    // corrupt entry (or any later entry -- the C# returns from inside the
    // entry loop).
    fs::path dir = TempDir("corrupt");
    std::vector<DumpEntry> entries;
    DumpEntry zipped;
    zipped.path = "z.bin";
    zipped.payload = DeflateBytes("eight8ch");  // produces 8 bytes
    zipped.declaredSize = 99;                   // ... but declares 99
    zipped.compressed = true;
    entries.push_back(zipped);

    DumpEntry after;
    after.path = "after.txt";
    after.payload = Bytes("later");
    entries.push_back(after);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("id", entries));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(bundlePath, dir.string(), errorOutput);

    EXPECT_EQ(rc, 65);  // ProgramExitCodes.EX_DATAERR
    EXPECT_EQ(errorOutput.str(),
        "Corrupted single-file entry 'z.bin'. Declared decompressed size '99'"
        " is not the same as actual decompressed size '8'.\r\n");
    EXPECT_FALSE(fs::exists(dir / "z.bin"));
    EXPECT_FALSE(fs::exists(dir / "after.txt"));
}

TEST(DumpPackage, TruncatedCompressedEntryReportsPartialProduction) {
    // A compressed region cut mid-block: real DeflateStream yields the bytes
    // it managed to produce (probed -- no exception), so the mismatch fires
    // with the partial count as the actual size.
    fs::path dir = TempDir("truncated");
    // A stored block declaring 10 bytes but carrying only 5.
    std::vector<std::uint8_t> deflate;
    deflate.push_back(0x01);
    deflate.push_back(10); deflate.push_back(0x00);
    deflate.push_back(0xF5); deflate.push_back(0xFF);
    deflate.insert(deflate.end(), { 'a', 'b', 'c', 'd', 'e' });

    std::vector<DumpEntry> entries;
    DumpEntry zipped;
    zipped.path = "z.bin";
    zipped.payload = deflate;
    zipped.declaredSize = 10;
    zipped.compressed = true;
    entries.push_back(zipped);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("id", entries));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(bundlePath, dir.string(), errorOutput);

    EXPECT_EQ(rc, 65);  // ProgramExitCodes.EX_DATAERR
    EXPECT_EQ(errorOutput.str(),
        "Corrupted single-file entry 'z.bin'. Declared decompressed size '10'"
        " is not the same as actual decompressed size '5'.\r\n");
}

TEST(DumpPackage, NotABundleIsRejected) {
    fs::path dir = TempDir("notbundle");
    std::string plainPath = WriteTempFile(dir, "plain.exe", Bytes("this is not a bundle"));
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(plainPath, dir.string(), errorOutput);

    EXPECT_EQ(rc, 65);  // ProgramExitCodes.EX_DATAERR
    // The C# message, including the "assembiles" misspelling in the real
    // source (IlspyCmdProgram.cs DumpPackageAssemblies). The file name is
    // not quoted (the C# interpolates it bare).
    EXPECT_EQ(errorOutput.str(),
        "Cannot dump assembiles for " + plainPath +
        ", because it is not a single file bundle.\r\n");
}

TEST(DumpPackage, MissingOutputDirectoryThrowsForEntries) {
    // The C# Path.Combine(null, RelativePath) ArgumentNullException when -d
    // runs without -o: the global catch renders it with EX_SOFTWARE (the
    // port's caller does the same).
    fs::path dir = TempDir("nodir");
    std::vector<DumpEntry> entries;
    DumpEntry raw;
    raw.path = "app.dll";
    raw.payload = Bytes("x");
    entries.push_back(raw);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", MakeBundle("id", entries));
    std::ostringstream errorOutput;
    try {
        Cmd::DumpPackage(bundlePath, std::nullopt, errorOutput);
        FAIL() << "a bundle with entries must throw without an output directory";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'path1')");
    } catch (...) {
        FAIL() << "wrong exception type";
    }
}

TEST(DumpPackage, MissingFileThrows) {
    // The C# MemoryMappedFile.CreateFromFile throws FileNotFoundException for
    // a missing package; the global catch renders it with EX_SOFTWARE.
    std::ostringstream errorOutput;
    try {
        fs::path nowhere = fs::temp_directory_path() / "ilspy_dumppackage_nowhere";
        Cmd::DumpPackage((fs::path(nowhere) / "missing.exe").string(),
            fs::temp_directory_path().string(), errorOutput);
        FAIL() << "a missing package file must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Unable to find the specified file.");
    } catch (...) {
        FAIL() << "wrong exception type";
    }
}

TEST(DumpPackage, EntryOutsideTheImageThrows) {
    // A manifest entry whose data lies past the end of the package image:
    // the C# UnmanagedMemoryStream reads whatever the memory mapping holds
    // past the file's end (undefined bytes within the last mapped page, an
    // access violation beyond it); the port cannot replicate that behaviour
    // with a plain buffer and refuses instead -- a documented divergence:
    // reading past the image throws rather than writing undefined bytes.
    fs::path dir = TempDir("oob");
    std::vector<DumpEntry> entries;
    DumpEntry raw;
    raw.path = "app.dll";
    raw.payload = Bytes("x");
    entries.push_back(raw);

    auto bundle = MakeBundle("id", entries);
    // Patch the single entry's Offset column (the first 8 bytes of the first
    // entry, right after the manifest header: major/minor/FileCount (12) +
    // the "id" string (1 + 2) + the v2 fields (5 x 8)) to point past the end.
    const std::size_t manifestAt = 16 + 8 + 32 + 1 /* payload 'x' */;
    const std::size_t entryAt = manifestAt + 4 + 4 + 4 + (1 + 2) + (8 * 5);
    std::uint64_t badOffset = static_cast<std::uint64_t>(bundle.size()) + 100;
    std::memcpy(bundle.data() + entryAt, &badOffset, 8);

    // Positive control: the manifest decode sees the patched offset (the
    // column position is right).
    long long headerOffset = 0;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    Sfb::Header header = Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);
    ASSERT_EQ(header.Entries.size(), 1u);
    EXPECT_EQ(header.Entries[0].Offset, static_cast<long long>(bundle.size()) + 100);

    std::string bundlePath = WriteTempFile(dir, "bundle.exe", bundle);
    std::ostringstream errorOutput;
    EXPECT_THROW(Cmd::DumpPackage(bundlePath, dir.string(), errorOutput),
        std::out_of_range);
    EXPECT_FALSE(fs::exists(dir / "app.dll"));
}

// ---------------------------------------------------------------------------
// The real .NET 10 single-file bundle fixture
// ---------------------------------------------------------------------------

// The real bundle (dotnet publish -p:PublishSingleFile=true, framework
// dependent: app.dll + app.deps.json + app.runtimeconfig.json) and the dump
// the REAL C# ilspycmd -d extracted from it. Glob-gated: absent on a machine
// without the fixtures, the test skips (the standing single suite skip is
// the same pattern).
const char* kRealBundlePath =
    "C:\\temp-probe\\sfdtest\\bin\\Release\\net10.0\\win-x64\\publish\\app.exe";
const char* kRealToolDumpDir = "C:\\temp-probe\\dumpfd";

TEST(DumpPackage, RealBundleMatchesRealToolDump) {
    if (!fs::exists(kRealBundlePath) || !fs::exists(fs::path(kRealToolDumpDir) / "app.dll")) {
        GTEST_SKIP() << "the local real-bundle fixture (" << kRealBundlePath
                     << ") or its real-tool dump is absent";
    }
    fs::path dir = TempDir("realbundle");
    std::ostringstream errorOutput;
    int rc = Cmd::DumpPackage(kRealBundlePath, dir.string(), errorOutput);

    ASSERT_EQ(rc, 0) << errorOutput.str();
    EXPECT_EQ(errorOutput.str(), "");
    for (const char* name : { "app.dll", "app.deps.json", "app.runtimeconfig.json" }) {
        fs::path port = dir / name;
        fs::path gold = fs::path(kRealToolDumpDir) / name;
        ASSERT_TRUE(fs::exists(port)) << name << " was not extracted";
        ASSERT_TRUE(fs::exists(gold)) << name << " is missing from the gold dump";
        auto portBytes = ReadFileBytes(port);
        auto goldBytes = ReadFileBytes(gold);
        ASSERT_EQ(portBytes.size(), goldBytes.size()) << name << " differs in size";
        EXPECT_TRUE(std::equal(portBytes.begin(), portBytes.end(), goldBytes.begin()))
            << name << " differs in content";
    }
}

}  // namespace
