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

// The PE debug-directory read -- the first piece of the DebugInfoUtils
// LoadSymbols PDB-discovery chain (the PdbProvider Phase-8 path): the C#
// `PEReader.ReadDebugDirectory()` entry array plus the
// `ReadCodeViewDebugDirectoryData(entry)` CV_INFO_PDB70 decode. The
// real-fixture expectations were dumped from the pinned .NET 10
// System.Reflection.Metadata (the DebugDirProbe); the failure arms no
// installed assembly carries are exercised over hand-patched copies of the
// tiny.netmodule fixture (the PE headers rewritten and debug bytes appended
// past the last section before the file is written).

#include "Decompiler/Metadata/MetadataFile.hpp"

#include "TestFixtures/PatchedNetModule.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::Disassembler::DebugDirectoryEntryType;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Tests::DebugEntry;
using ILSpy::Tests::PatchedNetModule;
using ILSpy::Tests::Rd16;
using ILSpy::Tests::Rd32;
using ILSpy::Tests::RsdsBlob;
using ILSpy::Tests::WritePatchedNetModule;
using ILSpy::Tests::Wr16;
using ILSpy::Tests::Wr32;

constexpr std::array<std::uint8_t, 16> kGuidA = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0F, 0x1E,
};

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::string SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

std::string CoreLibPath() {
#if defined(_WIN32)
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
#else
    return "/usr/share/dotnet/shared/System.Private.CoreLib.dll";
#endif
}

} // namespace

// ---- Real fixtures (exact bytes dumped from the pinned SRM 10 probe) ----

TEST(DebugDirectoryTest, MscorlibDebugDirectory) {
#if defined(_WIN32)
    std::string path = MscorlibPath();
#else
    GTEST_SKIP() << "exact debug-directory bytes are Windows-build-specific";
#endif
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    const auto& e = entries[0];
    EXPECT_EQ(e.Stamp, 0x69F01F2Eu);
    EXPECT_EQ(e.MajorVersion, 0u);
    EXPECT_EQ(e.MinorVersion, 0u);
    EXPECT_EQ(e.Type, DebugDirectoryEntryType::CodeView);
    EXPECT_EQ(e.DataSize, 37);
    EXPECT_EQ(e.DataRelativeVirtualAddress, 0x4E8F90);
    EXPECT_EQ(e.DataPointer, 0x4E7190);
    EXPECT_FALSE(e.IsPortableCodeView());
}

TEST(DebugDirectoryTest, MscorlibCodeViewData) {
#if defined(_WIN32)
    std::string path = MscorlibPath();
#else
    GTEST_SKIP() << "exact debug-directory bytes are Windows-build-specific";
#endif
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    auto cv = file.GetCodeViewDebugDirectoryData(entries[0]);
    ASSERT_TRUE(cv.has_value());
    // The GUID as stored (the canonical little-endian Guid form) and the
    // relative path the adjacent-PDB discovery would resolve.
    const std::array<std::uint8_t, 16> guid = {
        0x4b, 0x79, 0xb2, 0x6f, 0xe2, 0x06, 0x5e, 0x49,
        0x9b, 0x6e, 0xca, 0xec, 0x7c, 0x48, 0x7b, 0x8a,
    };
    EXPECT_EQ(cv->Guid, guid);
    EXPECT_EQ(cv->Age, 2);
    EXPECT_EQ(cv->Path, "mscorlib.pdb");
}

TEST(DebugDirectoryTest, SystemDllDebugDirectoryAndCodeView) {
#if defined(_WIN32)
    std::string path = SystemPath();
#else
    GTEST_SKIP() << "exact debug-directory bytes are Windows-build-specific";
#endif
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].Stamp, 0x6A1F8E0Cu);
    EXPECT_EQ(entries[0].Type, DebugDirectoryEntryType::CodeView);
    EXPECT_EQ(entries[0].DataSize, 35);
    EXPECT_EQ(entries[0].DataRelativeVirtualAddress, 0x32B464);
    EXPECT_EQ(entries[0].DataPointer, 0x329664);
    EXPECT_FALSE(entries[0].IsPortableCodeView());
    auto cv = file.GetCodeViewDebugDirectoryData(entries[0]);
    ASSERT_TRUE(cv.has_value());
    const std::array<std::uint8_t, 16> guid = {
        0x40, 0x56, 0x8f, 0x01, 0x98, 0x26, 0x60, 0x49,
        0xa3, 0x11, 0x2c, 0xa1, 0x02, 0x6a, 0xc6, 0x06,
    };
    EXPECT_EQ(cv->Guid, guid);
    EXPECT_EQ(cv->Age, 2);
    EXPECT_EQ(cv->Path, "System.pdb");
}

// The .NET 10 CoreLib fixture: four entries -- a legacy (native) CodeView
// entry, the Roslyn portable CodeView entry (MinorVersion 0x504D), a
// PdbChecksum, and a Reproducible marker.
TEST(DebugDirectoryTest, CoreLibDebugDirectory) {
    std::string path = CoreLibPath();
    if (path.empty() || !std::filesystem::exists(path))
        GTEST_SKIP() << "CoreLib fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    auto entries = file.GetDebugDirectoryEntries();
    ASSERT_EQ(entries.size(), 4u);

    // Entry 0: the native CodeView entry (the ni.pdb, not portable).
    EXPECT_EQ(entries[0].Stamp, 0x4E0CBF57u);
    EXPECT_EQ(entries[0].MajorVersion, 0x0100u);
    EXPECT_EQ(entries[0].MinorVersion, 0u);
    EXPECT_EQ(entries[0].Type, DebugDirectoryEntryType::CodeView);
    EXPECT_EQ(entries[0].DataSize, 284);
    EXPECT_EQ(entries[0].DataRelativeVirtualAddress, 0x25EC0);
    EXPECT_EQ(entries[0].DataPointer, 0x25EC0);
    EXPECT_FALSE(entries[0].IsPortableCodeView());

    // Entry 1: the Roslyn portable CodeView entry (MinorVersion 0x504D).
    EXPECT_EQ(entries[1].Stamp, 0x4E0CBF57u);
    EXPECT_EQ(entries[1].MajorVersion, 0x0100u);
    EXPECT_EQ(entries[1].MinorVersion, 0x504Du);
    EXPECT_EQ(entries[1].Type, DebugDirectoryEntryType::CodeView);
    EXPECT_EQ(entries[1].DataSize, 131);
    EXPECT_EQ(entries[1].DataRelativeVirtualAddress, 0x25E10);
    EXPECT_EQ(entries[1].DataPointer, 0x25E10);
    EXPECT_TRUE(entries[1].IsPortableCodeView());

    // Entry 2: the PDB checksum; entry 3: the Reproducible marker (all-zero
    // fields, no data).
    EXPECT_EQ(entries[2].Type, DebugDirectoryEntryType::PdbChecksum);
    EXPECT_EQ(static_cast<std::int32_t>(entries[2].Type), 19);
    EXPECT_EQ(entries[2].MajorVersion, 1u);
    EXPECT_EQ(entries[2].DataSize, 39);
    EXPECT_EQ(entries[3].Type, DebugDirectoryEntryType::Reproducible);
    EXPECT_EQ(entries[3].Stamp, 0u);
    EXPECT_EQ(entries[3].MajorVersion, 0u);
    EXPECT_EQ(entries[3].MinorVersion, 0u);
    EXPECT_EQ(entries[3].DataSize, 0);
    EXPECT_EQ(entries[3].DataPointer, 0);
    EXPECT_FALSE(entries[3].IsPortableCodeView());

    // Both CodeView entries decode.
    auto cv0 = file.GetCodeViewDebugDirectoryData(entries[0]);
    ASSERT_TRUE(cv0.has_value());
    EXPECT_EQ(cv0->Age, 1);
    EXPECT_EQ(cv0->Path, "System.Private.CoreLib.ni.pdb");
    auto cv1 = file.GetCodeViewDebugDirectoryData(entries[1]);
    ASSERT_TRUE(cv1.has_value());
    const std::array<std::uint8_t, 16> guid1 = {
        0xf3, 0xe6, 0x5a, 0xb1, 0x3a, 0x4d, 0x7e, 0xe1,
        0x51, 0x2b, 0x8a, 0xac, 0x5a, 0x1f, 0x72, 0xbb,
    };
    EXPECT_EQ(cv1->Guid, guid1);
    EXPECT_EQ(cv1->Age, 1);
    EXPECT_EQ(cv1->Path,
        "/_/src/runtime/artifacts/obj/coreclr/System.Private.CoreLib/"
        "windows.x64.Release/System.Private.CoreLib.pdb");
}

// ---- Synthetic (hand-patched tiny.netmodule) fixtures ----

TEST(DebugDirectoryTest, NoDebugDirectoryIsEmpty) {
    // The unpatched netmodule: its debug data directory is zero/zero.
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_TRUE(file.GetDebugDirectoryEntries().empty());
    std::remove(path.c_str());
}

TEST(DebugDirectoryTest, InvalidFileIsEmptyNoThrow) {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "ilspy_debugdir_garbage.bin";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    ASSERT_NE(out, nullptr);
    const char garbage[] = "not a PE at all";
    std::fwrite(garbage, 1, sizeof(garbage), out);
    std::fclose(out);
    MetadataFile file(path.string());
    EXPECT_FALSE(file.IsValid());
    EXPECT_TRUE(file.GetDebugDirectoryEntries().empty());
    std::remove(path.string().c_str());
}

TEST(DebugDirectoryTest, SyntheticEntriesDecode) {
    std::string blobA = RsdsBlob(kGuidA, 3, "foo.pdb");
    std::string entries = DebugEntry(0, 0x11223344u, 0, 0x504D, 2,
                                     static_cast<std::int32_t>(blobA.size()), 0, 0)
                        + DebugEntry(0, 0, 0, 0, 16, 0, 0, 0);
    // The CodeView entry's data sits right after the two 28-byte entries.
    std::uint32_t blobOff = 28 * 2;
    PatchedNetModule fixture = WritePatchedNetModule(
        entries + blobA, 0 /*dirRva placeholder, fixed below*/, 56);
    // Fill the entry fields relative to the appended region now that the
    // offsets are known: rewrite the debug bytes and re-patch.
    std::string e0 = DebugEntry(0, 0x11223344u, 0, 0x504D, 2,
                                static_cast<std::int32_t>(blobA.size()),
                                static_cast<std::int32_t>(fixture.AppendRva + blobOff),
                                static_cast<std::int32_t>(fixture.AppendPtr + blobOff));
    std::string e1 = DebugEntry(0, 0, 0, 0, 16, 0, 0, 0);
    fixture = WritePatchedNetModule(e0 + e1 + blobA,
                                    fixture.AppendRva, 56);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 2u);

    EXPECT_EQ(debug[0].Stamp, 0x11223344u);
    EXPECT_EQ(debug[0].MajorVersion, 0u);
    EXPECT_EQ(debug[0].MinorVersion, 0x504Du);
    EXPECT_EQ(debug[0].Type, DebugDirectoryEntryType::CodeView);
    EXPECT_EQ(debug[0].DataSize, static_cast<std::int32_t>(blobA.size()));
    EXPECT_EQ(debug[0].DataRelativeVirtualAddress,
              static_cast<std::int32_t>(fixture.AppendRva + blobOff));
    EXPECT_EQ(debug[0].DataPointer,
              static_cast<std::int32_t>(fixture.AppendPtr + blobOff));
    EXPECT_TRUE(debug[0].IsPortableCodeView());

    EXPECT_EQ(debug[1].Type, DebugDirectoryEntryType::Reproducible);
    EXPECT_EQ(debug[1].Stamp, 0u);
    EXPECT_EQ(debug[1].DataSize, 0);
    EXPECT_EQ(debug[1].DataPointer, 0);
    EXPECT_FALSE(debug[1].IsPortableCodeView());

    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, SyntheticCodeViewDataDecodes) {
    std::string blobA = RsdsBlob(kGuidA, 3, "foo.pdb");
    std::string e0 = DebugEntry(0, 0x11223344u, 0, 0x504D, 2,
                                 static_cast<std::int32_t>(blobA.size()), 0, 0);
    // First pass to learn the append offsets, second pass with the filled
    // entry (the same two-pass shape as SyntheticEntriesDecode).
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blobA, 0, 28);
    e0 = DebugEntry(0, 0x11223344u, 0, 0x504D, 2,
                    static_cast<std::int32_t>(blobA.size()),
                    static_cast<std::int32_t>(fixture.AppendRva + 28),
                    static_cast<std::int32_t>(fixture.AppendPtr + 28));
    fixture = WritePatchedNetModule(e0 + blobA, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    auto cv = file.GetCodeViewDebugDirectoryData(debug[0]);
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(cv->Guid, kGuidA);
    EXPECT_EQ(cv->Age, 3);
    EXPECT_EQ(cv->Path, "foo.pdb");
    std::remove(fixture.Path.c_str());
}

// A CV_INFO_PDB70 path with no NUL terminator yields the whole remaining
// block as the path (the C# ReadUtf8NullTerminated end-of-blob behavior --
// no throw).
TEST(DebugDirectoryTest, CodeViewPathWithoutTerminatorYieldsWholeBlock) {
    std::string blob = RsdsBlob(kGuidA, 7, "abc", /*terminated=*/false);
    std::string e0 = DebugEntry(0, 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    e0 = DebugEntry(0, 0, 0, 0, 2, static_cast<std::int32_t>(blob.size()),
                    static_cast<std::int32_t>(fixture.AppendRva + 28),
                    static_cast<std::int32_t>(fixture.AppendPtr + 28));
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    auto cv = file.GetCodeViewDebugDirectoryData(debug[0]);
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(cv->Age, 7);
    EXPECT_EQ(cv->Path, "abc");
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, NonCodeViewEntryThrowsInvalidArgument) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    std::string e0 = DebugEntry(0, 0, 0, 0, 17 /* EmbeddedPortablePdb */,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    e0 = DebugEntry(0, 0, 0, 0, 17, static_cast<std::int32_t>(blob.size()),
                    static_cast<std::int32_t>(fixture.AppendRva + 28),
                    static_cast<std::int32_t>(fixture.AppendPtr + 28));
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    EXPECT_EQ(debug[0].Type, DebugDirectoryEntryType::EmbeddedPortablePdb);
    EXPECT_THROW(file.GetCodeViewDebugDirectoryData(debug[0]),
                 std::invalid_argument);
    std::remove(fixture.Path.c_str());
}

// The legacy Windows PDB signature "NB10" (an NB10 CodeView entry) is not an
// RSDS blob: the decode rejects it.
TEST(DebugDirectoryTest, WrongCodeViewSignatureThrows) {
    std::string blob = "NB10" + std::string(20, '\0');
    std::string e0 = DebugEntry(0, 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    e0 = DebugEntry(0, 0, 0, 0, 2, static_cast<std::int32_t>(blob.size()),
                    static_cast<std::int32_t>(fixture.AppendRva + 28),
                    static_cast<std::int32_t>(fixture.AppendPtr + 28));
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    EXPECT_THROW(file.GetCodeViewDebugDirectoryData(debug[0]),
                 std::out_of_range);
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, TruncatedCodeViewDataThrows) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    // Declare a 10-byte block (shorter than signature + GUID + age).
    std::string e0 = DebugEntry(0, 0, 0, 0, 2, 10,
                                 static_cast<std::int32_t>(0), 0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    e0 = DebugEntry(0, 0, 0, 0, 2, 10,
                    static_cast<std::int32_t>(fixture.AppendRva + 28),
                    static_cast<std::int32_t>(fixture.AppendPtr + 28));
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    EXPECT_THROW(file.GetCodeViewDebugDirectoryData(debug[0]),
                 std::out_of_range);
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, DataPointerPastFileThrows) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    std::string e0 = DebugEntry(0, 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0,
                                 0x7FFFFFF0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    auto debug = file.GetDebugDirectoryEntries();
    ASSERT_EQ(debug.size(), 1u);
    EXPECT_THROW(file.GetCodeViewDebugDirectoryData(debug[0]),
                 std::out_of_range);
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, NonZeroCharacteristicsThrows) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    std::string e0 = DebugEntry(1 /* nonzero Characteristics, reserved */,
                                 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 28);
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_THROW(file.GetDebugDirectoryEntries(), std::out_of_range);
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, BadDirectorySizeThrows) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    std::string e0 = DebugEntry(0, 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    // 30 is not a multiple of the 28-byte entry.
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0, 30);
    fixture = WritePatchedNetModule(e0 + blob, fixture.AppendRva, 30);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_THROW(file.GetDebugDirectoryEntries(), std::out_of_range);
    std::remove(fixture.Path.c_str());
}

TEST(DebugDirectoryTest, UnresolvableRvaThrows) {
    std::string blob = RsdsBlob(kGuidA, 1, "x");
    std::string e0 = DebugEntry(0, 0, 0, 0, 2,
                                 static_cast<std::int32_t>(blob.size()), 0, 0);
    // 0x999999 lies outside every section.
    PatchedNetModule fixture = WritePatchedNetModule(e0 + blob, 0x999999, 28);
    ASSERT_FALSE(fixture.Path.empty());

    MetadataFile file(fixture.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_THROW(file.GetDebugDirectoryEntries(), std::out_of_range);
    std::remove(fixture.Path.c_str());
}
