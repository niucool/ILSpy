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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The associated/embedded portable-PDB discovery -- the second piece of the
// DebugInfoUtils LoadSymbols PDB-discovery chain (after the PE
// debug-directory read): the C#
// `PEReader.TryOpenAssociatedPortablePdb` (the portable-CodeView entry's PDB
// file, matched by BlobContentId -- the CV GUID + the entry's Stamp vs the
// PDB's #Pdb ID -- with the embedded MPDB blob as the fallback) and
// `PEReader.ReadEmbeddedPortablePdbDebugDirectoryData`. Every expected
// outcome was pinned by driving the REAL .NET 10
// System.Reflection.Metadata over the identical fixtures (the AssocPdbProbe
// SDK-10 project): the real Roslyn analyzer DLL/PDB pair from the local
// dotnet packs, and hand-patched tiny.netmodule PE images carrying a
// portable-CodeView entry and an MPDB entry (a raw-deflate stored-block
// stream for the hand-derivable shape, a python-zlib huffman stream pinned
// as a hex literal for the compressed-block path, both probe-validated).

#include "Decompiler/Metadata/MetadataFile.hpp"

#include "TestFixtures/DiscoveryNetModule.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::Disassembler::DebugDirectoryEntryType;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::PdbStreamProvider;
using ILSpy::Decompiler::Metadata::PdbTable;
using ILSpy::Decompiler::Metadata::PortablePdb;
using ILSpy::Tests::DiscoveryEntry;
using ILSpy::Tests::EmbeddedEntry;
using ILSpy::Tests::FixtureId;
using ILSpy::Tests::MpdbBlob;
using ILSpy::Tests::PatchedNetModule;
using ILSpy::Tests::PortableCodeViewEntry;
using ILSpy::Tests::StoredDeflate;
using ILSpy::Tests::WriteDiscoveryModule;

// The provider state: every requested path (the tests assert the resolved
// paths the discovery asks for), plus the not-found and the fixed-bytes
// overrides (a garbage "file").
struct ProviderState {
    std::vector<std::string> requested;
    bool notFound = false;
    std::shared_ptr<const std::vector<std::uint8_t>> overrideBytes;
};

// The C# DebugInfoUtils.OpenStream shape (the provider the discovery gets):
// the whole file in memory, null when it does not exist.
PdbStreamProvider MakeProvider(ProviderState& state) {
    return [&state](const std::string& fileName)
               -> std::shared_ptr<const std::vector<std::uint8_t>> {
        state.requested.push_back(fileName);
        if (state.overrideBytes) return state.overrideBytes;
        if (state.notFound) return nullptr;
        std::error_code ec;
        if (!std::filesystem::exists(fileName, ec)) return nullptr;
        std::FILE* f = std::fopen(fileName.c_str(), "rb");
        if (f == nullptr) return nullptr;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<std::size_t>(sz));
        std::size_t read = std::fread(bytes->data(), 1, bytes->size(), f);
        bytes->resize(read);
        std::fclose(f);
        return bytes;
    };
}

// Writes the synthetic PDB fixture bytes under `dir/foo.pdb` and returns
// the path (the resolved path a CV entry pointing at "some/dir/foo.pdb"
// opens).
std::string WriteSyntheticPdb(const std::string& dir) {
    namespace fs = std::filesystem;
    fs::path pdb = fs::path(dir) / "foo.pdb";
    if (!ILSpy::Tests::WriteSyntheticPdbFile(pdb.string())) return {};
    return pdb.string();
}

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The real Roslyn analyzer DLL/PDB pair shipped in the local dotnet packs
// (a genuinely associated pair: the DLL's CodeView entry ID matches the
// PDB's #Pdb ID).
std::string AnalyzerDllPath() {
#if defined(_WIN32)
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\packs\\Microsoft.WindowsDesktop.App.Ref";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate =
            it->path().string() + "\\analyzers\\dotnet\\cs\\System.Windows.Forms.Analyzers.CSharp.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
#else
    return {};
#endif
}

}  // namespace

// ---- The real Roslyn analyzer pair (probe-pinned exact values) ----

// The real associated pair: the discovery opens the PDB next to the DLL
// (the CV path's file name resolved against the DLL's directory) and the
// ID matches. Probed against real SRM: pdbPath = the analyzer directory +
// "System.Windows.Forms.Analyzers.CSharp.pdb", the ID
// f06008e11324dc428558da7425035c62de47c2d7, 79 MethodDebugInformation rows.
TEST(AssociatedPortablePdbTest, AnalyzerPair) {
    std::string dll = AnalyzerDllPath();
    if (dll.empty() || !std::filesystem::exists(dll))
        GTEST_SKIP() << "analyzer pack fixture not present";
    MetadataFile file(dll);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    ASSERT_TRUE(file.TryOpenAssociatedPortablePdb(dll, MakeProvider(state), pdb, pdbPath));
    namespace fs = std::filesystem;
    std::string expected = (fs::path(dll).parent_path() / "System.Windows.Forms.Analyzers.CSharp.pdb").string();
    EXPECT_EQ(pdbPath, expected);
    ASSERT_TRUE(pdb.IsValid());
    const std::uint8_t* id = pdb.PdbId();
    ASSERT_NE(id, nullptr);
    const std::array<std::uint8_t, 20> kId = {
        0xf0, 0x60, 0x08, 0xe1, 0x13, 0x24, 0xdc, 0x42,
        0x85, 0x58, 0xda, 0x74, 0x25, 0x03, 0x5c, 0x62,
        0xde, 0x47, 0xc2, 0xd7,
    };
    EXPECT_EQ(std::memcmp(id, kId.data(), 20), 0);
    EXPECT_EQ(pdb.RowCount(PdbTable::MethodDebugInformation), 79u);
    // The provider was asked for the resolved path.
    ASSERT_EQ(state.requested.size(), 1u);
    EXPECT_EQ(state.requested[0], expected);
}

// ---- The associated-file arm over the synthetic fixtures ----

// The matching pair: the patched PE entry carries the synthetic fixture
// PDB's ID (GUID + stamp), the PDB file sits at the resolved path (the CV
// path "some/build/dir/foo.pdb" resolves to the PE's directory +
// "foo.pdb"), and the discovery opens it.
TEST(AssociatedPortablePdbTest, AssociatedFileIdMatch) {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/build/dir/foo.pdb")});
    std::string pdbFile = WriteSyntheticPdb(
        std::filesystem::path(module.Path).parent_path().string());
    ASSERT_FALSE(pdbFile.empty());
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    ASSERT_TRUE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_EQ(pdbPath, pdbFile);
    ASSERT_TRUE(pdb.IsValid());
    EXPECT_EQ(std::memcmp(pdb.PdbId(), id.Guid.data(), 16), 0);
    ASSERT_EQ(state.requested.size(), 1u);
    EXPECT_EQ(state.requested[0], pdbFile);
}

// The entry's Stamp (the TimeDateStamp, not the CV age) is what matches the
// ID: an off-by-one stamp misses (false, no throw), the file still probed.
TEST(AssociatedPortablePdbTest, AssociatedFileIdMismatch) {
    FixtureId id;
    DiscoveryEntry e = PortableCodeViewEntry(id, "some/build/dir/foo.pdb");
    e.Stamp = id.Stamp + 1;
    PatchedNetModule module = WriteDiscoveryModule({e});
    WriteSyntheticPdb(std::filesystem::path(module.Path).parent_path().string());
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    pdb = PortablePdb(nullptr);
    pdbPath = "sentinel";
    EXPECT_FALSE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(pdbPath.empty());  // cleared on entry (the C# null out)
    EXPECT_FALSE(pdb.IsValid());
    ASSERT_EQ(state.requested.size(), 1u);  // the file was probed
}

// A file the provider does not serve is a clean miss (false, no error).
TEST(AssociatedPortablePdbTest, AssociatedFileMissing) {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/build/dir/foo.pdb")});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    state.notFound = true;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_FALSE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(pdbPath.empty());
}

// A garbage associated file (bytes that do not parse as a PDB) records the
// parse error and the discovery rethrows it at the end (the C#
// BadImageFormatException from GetMetadataReader, e.g. "Invalid COR20
// header signature." -- the port's std::out_of_range).
TEST(AssociatedPortablePdbTest, AssociatedFileGarbage) {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/build/dir/foo.pdb")});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    const char garbage[] = "GARBAGE NOT A PDB AT ALL";
    state.overrideBytes = std::make_shared<std::vector<std::uint8_t>>(
        garbage, garbage + sizeof(garbage) - 1);
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                      module.Path, MakeProvider(state), pdb, pdbPath),
                  std::out_of_range);
}

// A module with only a legacy (non-portable) CodeView entry -- mscorlib's
// own shape -- is not the associated discovery's business: false, no
// throw, the provider never called (the legacy handling lives in the
// ILSpyX TryOpenPortablePdb).
TEST(AssociatedPortablePdbTest, LegacyCodeViewOnlyModule) {
    std::string path = MscorlibPath();
    if (!std::filesystem::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_FALSE(file.TryOpenAssociatedPortablePdb(path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(state.requested.empty());
}

// ---- The embedded-PDB arm over the synthetic fixtures ----

// A valid MPDB blob (stored deflate of the synthetic PDB) opens through the
// fallback: true, an empty pdbPath (the C# null -- the PDB is embedded in
// the image itself), the fixture's ID.
TEST(AssociatedPortablePdbTest, EmbeddedPdbOpens) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                       StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    ASSERT_TRUE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(pdbPath.empty());
    ASSERT_TRUE(pdb.IsValid());
    FixtureId id;
    EXPECT_EQ(std::memcmp(pdb.PdbId(), id.Guid.data(), 16), 0);
    EXPECT_TRUE(state.requested.empty());  // no file involved
}

// The direct read: the public member returns the parsed PDB for the entry.
TEST(AssociatedPortablePdbTest, EmbeddedPdbDirectRead) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                       StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    PortablePdb pdb = file.ReadEmbeddedPortablePdbDebugDirectoryData(entries[0]);
    ASSERT_TRUE(pdb.IsValid());
    FixtureId id;
    EXPECT_EQ(std::memcmp(pdb.PdbId(), id.Guid.data(), 16), 0);
}

// A real compressed (huffman) deflate stream -- the format actual
// producers emit. The 465-byte stream is a python zlib level-9 raw deflate
// of the 616-byte fixture (probe-validated against real SRM through the
// identical blob).
TEST(AssociatedPortablePdbTest, EmbeddedPdbHuffmanStream) {
    constexpr char kHex[] =
        "730af6726264606400011e200e707152"
        "2833d433000b30b0321400491520560e"
        "484902894c01e257207e1d03431d2354"
        "2eb8a428332fbd1824bf082816001273"
        "0ff57401d29f80fc3210df29271f64c0"
        "bdb5fbde332abba677ae3efb9e9f8f97"
        "8709280654c2e600b19081054a333140"
        "1dc5f01f2a728481194832835d0551c7"
        "04c58c507161204b1e28220ea45580b4"
        "34505413483332e80165dd80380cc806"
        "f121467381f53201553181cd8198c908"
        "a499c1f2296073a1ce00020db01a46b0"
        "3a46a8dd0c50362354373b90c50a0c49"
        "61866806298658a0583c5024196c8e03"
        "50a690a102a832956106900d0cb03203"
        "86324386b49cc474868ccc9494d43c06"
        "dfc48ab0c41c06f7a2d4d41260a03230"
        "f05fd0ecd82128ecd4de5dced73a7d8d"
        "d88fa440fb63ec97052704331cf05fcc"
        "b4f08d98c4ffd85502be3bea0f3edab0"
        "f5c0dfc2fbf3df5676b8bbf75dfe1777"
        "cfe6b9c091e4a3cd9fb75c75dfa1b22b"
        "c4d1bd6e050393b3154ba25e72314b12"
        "88480612cc318c2c40cc09c47c2c2e86"
        "2e862c2e462e462c2ec62ec6e240cfb1"
        "703183829c8995a95204e87f2e061611"
        "367e26660666a61426165074b0b1b0b0"
        "3033b130327230f2310b2a1933ba70e7"
        "25e6a632283024a6a73230b233b13600"
        "00";
    std::string deflateStream;
    for (std::size_t i = 0; i + 1 < sizeof(kHex); i += 2) {
        auto nib = [](char c) {
            return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        };
        deflateStream.push_back(
            static_cast<char>((nib(kHex[i]) << 4) | nib(kHex[i + 1])));
    }
    ASSERT_EQ(deflateStream.size(), 465u);
    PatchedNetModule module =
        WriteDiscoveryModule({EmbeddedEntry(616, deflateStream)});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    ASSERT_TRUE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    ASSERT_TRUE(pdb.IsValid());
    EXPECT_EQ(pdb.RowCount(PdbTable::LocalScope), 5u);
}

// Trailing bytes past the deflate stream's end-of-block marker are
// ACCEPTED (the C# ReadByte != -1 probe reads the inflated stream, which
// ends at the marker -- the underlying block is not drained). Probed
// against real SRM: the trailing.dll shape opens fine.
TEST(AssociatedPortablePdbTest, EmbeddedPdbTrailingDataAccepted) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    std::string deflateStream = StoredDeflate(bytes->data(), bytes->size());
    deflateStream += "\xAA";  // trailing garbage past the end-of-block
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size()), deflateStream)});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_TRUE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(pdb.IsValid());
}

// The fallback order: the portable-CodeView entry is tried first (a clean
// miss -- its file is not served), the embedded entry second (opens).
TEST(AssociatedPortablePdbTest, EmbeddedPdbFallbackAfterMiss) {
    FixtureId id;
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule({
        PortableCodeViewEntry(id, "some/build/dir/foo.pdb"),
        EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                      StoredDeflate(bytes->data(), bytes->size())),
    });
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    state.notFound = true;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    ASSERT_TRUE(file.TryOpenAssociatedPortablePdb(
        module.Path, MakeProvider(state), pdb, pdbPath));
    EXPECT_TRUE(pdbPath.empty());  // the embedded arm's null path
    EXPECT_TRUE(pdb.IsValid());
    ASSERT_EQ(state.requested.size(), 1u);  // the CV path was probed first
}

// ---- The embedded-PDB failure arms (probe-pinned throw types) ----

// The entry's version must be exactly 1.0: MajorVersion < 256 throws (the
// C# "Unsupported format version: 0.1" -- PortablePdbVersions.Format of the
// raw ushort), recorded and rethrown at the end of the discovery.
TEST(AssociatedPortablePdbTest, EmbeddedPdbBadVersion) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    DiscoveryEntry e = EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                                     StoredDeflate(bytes->data(), bytes->size()));
    e.MajorVersion = 0x0001;
    e.MinorVersion = 0x0000;
    PatchedNetModule module = WriteDiscoveryModule({e});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    // The direct read throws it too.
    EXPECT_THROW(file.ReadEmbeddedPortablePdbDebugDirectoryData(entries[0]),
                 std::out_of_range);
    // Through the discovery: recorded, rethrown at the end.
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                     module.Path, MakeProvider(state), pdb, pdbPath),
                 std::out_of_range);
}

// A MinorVersion other than 256 throws the same way.
TEST(AssociatedPortablePdbTest, EmbeddedPdbBadMinorVersion) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    DiscoveryEntry e = EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                                     StoredDeflate(bytes->data(), bytes->size()));
    e.MinorVersion = 0x0001;
    PatchedNetModule module = WriteDiscoveryModule({e});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_THROW(file.ReadEmbeddedPortablePdbDebugDirectoryData(entries[0]),
                 std::out_of_range);
}

// The "MPDB" signature: a blob starting with anything else throws (the C#
// "Unexpected Embedded Portable PDB data signature value.").
TEST(AssociatedPortablePdbTest, EmbeddedPdbWrongSignature) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    std::string blob = MpdbBlob(static_cast<std::int32_t>(bytes->size()),
                                StoredDeflate(bytes->data(), bytes->size()));
    blob[0] = 'A';  // "APDB"
    DiscoveryEntry e;
    e.MajorVersion = 0x0100;
    e.MinorVersion = 0x0100;
    e.Type = static_cast<std::int32_t>(
        DebugDirectoryEntryType::EmbeddedPortablePdb);
    e.Blob = blob;
    PatchedNetModule module = WriteDiscoveryModule({e});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                     module.Path, MakeProvider(state), pdb, pdbPath),
                 std::out_of_range);
}

// A declared size smaller than the inflated stream: the output fills first
// and more inflated data remains (the C# ReadByte != -1 SizeMismatch).
TEST(AssociatedPortablePdbTest, EmbeddedPdbDeclaredSizeTooSmall) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size() - 16),
                       StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                     module.Path, MakeProvider(state), pdb, pdbPath),
                 std::out_of_range);
}

// A declared size bigger than the inflated stream: the stream ends early
// (num2 != num, the C# SizeMismatch).
TEST(AssociatedPortablePdbTest, EmbeddedPdbDeclaredSizeTooBig) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size() + 16),
                       StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                     module.Path, MakeProvider(state), pdb, pdbPath),
                 std::out_of_range);
}

// A deflate stream truncated mid-block (the input runs out before the
// declared size inflates): the stored block's LEN declares the full size
// but the payload is cut short. The C# CopyTo gets the truncated count and
// num2 != num throws SizeMismatch.
TEST(AssociatedPortablePdbTest, EmbeddedPdbTruncatedDeflate) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    // The full stored-block stream (header + payload), cut 10 bytes short:
    // the block header still declares the full LEN.
    std::string truncated = StoredDeflate(bytes->data(), bytes->size())
                                .substr(0, 5 + bytes->size() - 10);
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size()), truncated)});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    ProviderState state;
    PortablePdb pdb(nullptr);
    std::string pdbPath;
    EXPECT_THROW(file.TryOpenAssociatedPortablePdb(
                     module.Path, MakeProvider(state), pdb, pdbPath),
                 std::out_of_range);
}

// A negative declared size (the C# DataTooBig allocation arm).
TEST(AssociatedPortablePdbTest, EmbeddedPdbNegativeDeclaredSize) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(-1, StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_THROW(file.ReadEmbeddedPortablePdbDebugDirectoryData(entries[0]),
                 std::out_of_range);
}

// An entry that is not an EmbeddedPortablePdb one: the C# ArgumentException.
TEST(AssociatedPortablePdbTest, EmbeddedPdbWrongEntryType) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    DiscoveryEntry e = EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                                      StoredDeflate(bytes->data(), bytes->size()));
    e.Type = static_cast<std::int32_t>(DebugDirectoryEntryType::CodeView);
    PatchedNetModule module = WriteDiscoveryModule({e});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_THROW(file.ReadEmbeddedPortablePdbDebugDirectoryData(entries[0]),
                 std::invalid_argument);
}
