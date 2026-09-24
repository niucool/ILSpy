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

// The PDB discovery -- the DebugInfoUtils LoadSymbols/FromFile composition
// over the ported debug-directory read and associated/embedded discovery:
// the portable-CodeView entry's associated PDB, the legacy-CodeView entry's
// adjacent <module>.pdb file (opened with NO ID match, unlike the associated
// discovery), the embedded MPDB entry reached through a portable-CodeView
// entry, and the explicit FromFile path. Every reachable arm's outcome was
// pinned against the REAL ICSharpCode.ILSpyX DebugInfoUtils (the installed
// ilspycmd 11.0 tool's ICSharpCode.ILSpyX.dll, driven over the identical
// fixtures by the DiuProbe SDK-10 project): the associated pair and the
// analyzer pair open through the PortableDebugInfoProvider, the adjacent
// MSF-prefixed file and the no-CodeView-entry fallback construct the
// MonoCecilDebugInfoProvider in the C# (whose eager Cecil read throws
// PdbException/SymbolsNotMatchingException over the fake MSF bytes -- both
// outside LoadSymbols' BadImageFormatException/COMException catch), and the
// port DEFERS the whole MonoCecil bridge (PORT_PLAN.md 5.7: Portable PDB
// only, the documented Windows-PDB gap) -- those arms assert the deferred
// null.

#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"
#include <cstdlib>
#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"
#include "TestFixtures/DiscoveryNetModule.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::PortablePdb;
using ILSpy::Tests::DiscoveryEntry;
using ILSpy::Tests::EmbeddedEntry;
using ILSpy::Tests::FixtureId;
using ILSpy::Tests::PatchedNetModule;
using ILSpy::Tests::PortableCodeViewEntry;
using ILSpy::Tests::RsdsBlob;
using ILSpy::Tests::StoredDeflate;
using ILSpy::Tests::WriteDiscoveryModule;
using ILSpy::Tests::WriteSyntheticPdbFile;
namespace Pdb = ILSpy::ILSpyX::PdbProvider;

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The real Roslyn analyzer DLL/PDB pair shipped in the local dotnet packs
// (a genuinely associated pair: the DLL's CodeView entry ID matches the
// PDB's #Pdb ID).
std::string AnalyzerDllPath() {
#if defined(_WIN32)
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

// The adjacent <module>.pdb the legacy-CodeView arm derives from the
// module's own file name (the fixed temp path WritePatchedNetModule uses).
std::string AdjacentPdbPath() {
    return (fs::temp_directory_path() / "ilspy_debugdir_test.pdb").string();
}

// Removes a file if it exists (the adjacent-PDB tests each set their own
// state up, so a leftover from a previous run must not leak in).
void RemoveFile(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

// Writes the given bytes at the path (false when the file could not be
// written).
bool WriteFileBytes(const std::string& path, const std::string& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return true;
}

// A legacy (non-portable) CodeView entry: MinorVersion 0, an RSDS blob
// whose GUID deliberately does NOT match the synthetic fixture's #Pdb ID
// (the adjacent-file arm opens the file unconditionally -- the no-ID-check
// rule this suite pins) and whose path the adjacent lookup never consults.
DiscoveryEntry LegacyCodeViewEntry() {
    DiscoveryEntry e;
    e.Stamp = 2;
    e.MajorVersion = 0;
    e.MinorVersion = 0;
    e.Type = 2;  // CodeView
    std::array<std::uint8_t, 16> otherGuid = {
        0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22,
        0x33, 0x33, 0x33, 0x33, 0x44, 0x44, 0x44, 0x44,
    };
    e.Blob = RsdsBlob(otherGuid, 1, "build/server/path.pdb");
    return e;
}

// The native Windows PDB (MSF) file header a legacy-PDB file starts with.
constexpr char kLegacyPdbPrefix[] = "Microsoft C/C++ MSF 7.00";

// The dynamic cast the tests inspect the discovered provider through
// (null when LoadSymbols found nothing or deferred the arm).
const Pdb::PortableDebugInfoProvider* AsPortable(
    const std::unique_ptr<ILSpy::Decompiler::DebugInfo::IDebugInfoProvider>& provider) {
    return dynamic_cast<const Pdb::PortableDebugInfoProvider*>(provider.get());
}

}  // namespace

// The C# PEFile.FileName: the path exactly as passed, valid for an
// unparseable file too.
TEST(DebugInfoUtilsTest, FileNameMatchesConstructorPath) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(file.FileName(), path);
    // An unparseable file keeps its path too.
    MetadataFile bogus("no-such-file-at-all.dll");
    EXPECT_FALSE(bogus.IsValid());
    EXPECT_EQ(bogus.FileName(), "no-such-file-at-all.dll");
}

// ---- LoadSymbols: the portable-CodeView associated arm ----

// The matching pair: the portable-CodeView entry resolves its PDB path's
// file name against the module's directory, the adjacent file's #Pdb ID
// matches the entry, and the provider reads it (gold: "Loaded from portable
// PDB: <temp>\foo.pdb", 4 sequence points, first document C:\a.cs).
TEST(DebugInfoUtilsTest, LoadSymbolsOpensAssociatedPdb) {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/build/dir/foo.pdb")});
    std::string pdbPath =
        (fs::path(module.Path).parent_path() / "foo.pdb").string();
    ASSERT_TRUE(WriteSyntheticPdbFile(pdbPath));
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto provider = Pdb::LoadSymbols(file);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_FALSE(portable->IsEmbedded());
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_EQ(portable->SourceFileName(), pdbPath);
    auto points = portable->GetSequencePoints(0x06000001);
    ASSERT_EQ(points.size(), 4u);
    EXPECT_EQ(points[0].DocumentUrl, "C:\\a.cs");
}

// A real associated pair: the Roslyn analyzer DLL/PDB from the dotnet packs
// (gold: the provider over the analyzer .pdb next to the DLL).
TEST(DebugInfoUtilsTest, LoadSymbolsAnalyzerPair) {
    std::string dll = AnalyzerDllPath();
    if (dll.empty() || !fs::exists(dll))
        GTEST_SKIP() << "analyzer pack fixture not present";
    MetadataFile file(dll);
    ASSERT_TRUE(file.IsValid());
    auto provider = Pdb::LoadSymbols(file);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    std::string pdbPath =
        (fs::path(dll).parent_path() / "System.Windows.Forms.Analyzers.CSharp.pdb").string();
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_EQ(portable->SourceFileName(), pdbPath);
    EXPECT_FALSE(portable->IsEmbedded());
}

// ---- LoadSymbols: the legacy-CodeView adjacent-file arm ----

// The adjacent <module>.pdb file opens through the portable provider with
// NO ID match (unlike the associated discovery -- the C# opens whatever
// readable file sits next to the module) and the RSDS path inside the entry
// is never consulted (gold: "Loaded from portable PDB:
// <temp>\ilspy_debugdir_test.pdb").
TEST(DebugInfoUtilsTest, LoadSymbolsLegacyAdjacentPortablePdbHasNoIdCheck) {
    PatchedNetModule module = WriteDiscoveryModule({LegacyCodeViewEntry()});
    std::string adjacent = AdjacentPdbPath();
    RemoveFile(adjacent);
    ASSERT_TRUE(WriteSyntheticPdbFile(adjacent));
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto provider = Pdb::LoadSymbols(file);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + adjacent);
    EXPECT_EQ(portable->SourceFileName(), adjacent);
    EXPECT_EQ(portable->GetSequencePoints(0x06000001).size(), 4u);
    RemoveFile(adjacent);
}

// A native (MSF-prefixed) adjacent file: the C# constructs the
// MonoCecilDebugInfoProvider here (whose eager Cecil read THROWS
// PdbException over a non-MSF payload -- outside LoadSymbols' catch). The
// port defers the Cecil bridge (PORT_PLAN.md 5.7): no provider.
TEST(DebugInfoUtilsTest, LoadSymbolsLegacyAdjacentNativePdbIsDeferred) {
    PatchedNetModule module = WriteDiscoveryModule({LegacyCodeViewEntry()});
    std::string adjacent = AdjacentPdbPath();
    RemoveFile(adjacent);
    ASSERT_TRUE(WriteFileBytes(adjacent, std::string(kLegacyPdbPrefix) + std::string(32, '\0')));
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
    RemoveFile(adjacent);
}

// No adjacent file: the walk continues past the entry and finds nothing
// (gold: null).
TEST(DebugInfoUtilsTest, LoadSymbolsLegacyNoAdjacentPdb) {
    PatchedNetModule module = WriteDiscoveryModule({LegacyCodeViewEntry()});
    std::string adjacent = AdjacentPdbPath();
    RemoveFile(adjacent);
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
}

// ---- LoadSymbols: the embedded arm ----

// The real-world embedded shape: the portable-CodeView entry's file is a
// clean miss, the MPDB entry follows -- the fallback opens the embedded
// PDB (gold: "Embedded in this assembly", SourceFileName = the module
// itself, IsEmbedded true).
TEST(DebugInfoUtilsTest, LoadSymbolsEmbeddedPdb) {
    FixtureId id;
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule({
        PortableCodeViewEntry(id, "some/dir/missing.pdb"),
        EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                      StoredDeflate(bytes->data(), bytes->size())),
    });
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    auto provider = Pdb::LoadSymbols(file);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_TRUE(portable->IsEmbedded());
    EXPECT_EQ(portable->Description(), "Embedded in this assembly");
    EXPECT_EQ(portable->SourceFileName(), module.Path);
    EXPECT_EQ(portable->GetSequencePoints(0x06000001).size(), 4u);
}

// The embedded-only quirk: an MPDB entry with NO CodeView entry never
// reaches the embedded fallback -- TryOpenPortablePdb only calls the
// associated/embedded discovery from a portable-CodeView entry (gold: null
// even though a perfectly valid embedded PDB is sitting in the module).
TEST(DebugInfoUtilsTest, LoadSymbolsEmbeddedOnlyNeverLooks) {
    auto bytes = ILSpy::Tests::SyntheticPdbBytes();
    PatchedNetModule module = WriteDiscoveryModule(
        {EmbeddedEntry(static_cast<std::int32_t>(bytes->size()),
                       StoredDeflate(bytes->data(), bytes->size()))});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
}

// ---- LoadSymbols: the error and the deferred fallback arms ----

// A garbage associated PDB: the discovery parse failure (the C#
// BadImageFormatException, the port's std::out_of_range) is swallowed by
// LoadSymbols' catch and yields null (gold: null).
TEST(DebugInfoUtilsTest, LoadSymbolsGarbageAssociatedPdbIsSwallowed) {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/dir/garbage.pdb")});
    std::string garbagePath =
        (fs::path(module.Path).parent_path() / "garbage.pdb").string();
    RemoveFile(garbagePath);
    ASSERT_TRUE(WriteFileBytes(garbagePath, "GARBAGE NOT A PDB AT ALL"));
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
    RemoveFile(garbagePath);
}

// A module with no CodeView entry at all but an adjacent portable PDB: the
// C# falls straight to the MonoCecilDebugInfoProvider (whose Cecil reader
// THROWS SymbolsNotMatchingException when the adjacent PDB's ID does not
// match the module -- again outside the catch). The port defers the Cecil
// bridge: no provider.
TEST(DebugInfoUtilsTest, LoadSymbolsNoCodeViewEntryAdjacentPdbIsDeferred) {
    DiscoveryEntry repro;
    repro.Stamp = 2;
    repro.MajorVersion = 0x0100;
    repro.MinorVersion = 0x0100;
    repro.Type = 16;  // Reproducible
    repro.Blob = std::string(4, '\0');
    PatchedNetModule module = WriteDiscoveryModule({repro});
    std::string adjacent = AdjacentPdbPath();
    RemoveFile(adjacent);
    ASSERT_TRUE(WriteSyntheticPdbFile(adjacent));
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
    RemoveFile(adjacent);
}

// A module with no debug entries at all (gold: null), and the real-world
// no-PDB shape: mscorlib's legacy CodeView entry with no adjacent PDB file
// (gold: null, no throw).
TEST(DebugInfoUtilsTest, LoadSymbolsNoDebugEntries) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
}

TEST(DebugInfoUtilsTest, LoadSymbolsMscorlibHasNoPdb) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    std::string adjacent =
        (fs::path(path).parent_path()
            / (fs::path(path).stem().string() + ".pdb")).string();
    if (fs::exists(adjacent))
        GTEST_SKIP() << "a PDB sits next to the framework mscorlib";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(Pdb::LoadSymbols(file), nullptr);
}

// ---- FromFile: the explicit -usepdb path ----

// A portable PDB by explicit path: the provider over it, reading through
// (gold: "Loaded from portable PDB: <path>", 4 points).
TEST(DebugInfoUtilsTest, FromFileOpensPortablePdb) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    std::string pdbPath =
        (fs::temp_directory_path() / "ilspy_diu_misc.pdb").string();
    RemoveFile(pdbPath);
    ASSERT_TRUE(WriteSyntheticPdbFile(pdbPath));
    auto provider = Pdb::FromFile(file, pdbPath);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_EQ(portable->SourceFileName(), pdbPath);
    EXPECT_EQ(portable->GetSequencePoints(0x06000001).size(), 4u);
    RemoveFile(pdbPath);
}

// A native-MSF-prefixed file: the C# constructs the MonoCecil provider
// (which THROWS PdbException over the fake payload); the port defers the
// Cecil bridge (PORT_PLAN.md 5.7): no provider.
TEST(DebugInfoUtilsTest, FromFileNativePdbIsDeferred) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    std::string pdbPath =
        (fs::temp_directory_path() / "ilspy_diu_native.pdb").string();
    RemoveFile(pdbPath);
    ASSERT_TRUE(WriteFileBytes(pdbPath, std::string(kLegacyPdbPrefix) + std::string(32, '\0')));
    EXPECT_EQ(Pdb::FromFile(file, pdbPath), nullptr);
    RemoveFile(pdbPath);
}

// A file shorter than the 24-byte legacy prefix: the C# Stream.Read returns
// fewer bytes than the prefix length, so the check misses and the portable
// parse runs -- the deferred C# parse yields the provider whose reads fail
// (gold: the provider with the error-flip Description).
TEST(DebugInfoUtilsTest, FromFileShortFileFallsThroughToPortableParse) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    std::string pdbPath =
        (fs::temp_directory_path() / "ilspy_diu_short.pdb").string();
    RemoveFile(pdbPath);
    ASSERT_TRUE(WriteFileBytes(pdbPath, "BSJB"));
    auto provider = Pdb::FromFile(file, pdbPath);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    // Before any read: no error yet.
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_TRUE(portable->GetSequencePoints(0x06000001).empty());
    // After the failed read: the error state the Description reports.
    EXPECT_EQ(portable->Description(),
        "Error while loading portable PDB: " + pdbPath);
    RemoveFile(pdbPath);
}

// A garbage file (long enough to reach the prefix check but not matching
// it): the same deferred-parse provider shape (gold: identical to the
// short-file arm).
TEST(DebugInfoUtilsTest, FromFileGarbageReportsErrorAfterRead) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    std::string pdbPath =
        (fs::temp_directory_path() / "ilspy_diu_garbage.pdb").string();
    RemoveFile(pdbPath);
    ASSERT_TRUE(WriteFileBytes(pdbPath, "NOT A PDB"));
    auto provider = Pdb::FromFile(file, pdbPath);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_TRUE(portable->GetVariables(0x06000001).empty());
    EXPECT_EQ(portable->Description(),
        "Error while loading portable PDB: " + pdbPath);
    RemoveFile(pdbPath);
}

// A path that does not exist and the empty name (gold: both null).
TEST(DebugInfoUtilsTest, FromFileMissingAndEmpty) {
    PatchedNetModule module = WriteDiscoveryModule({});
    MetadataFile file(module.Path);
    ASSERT_TRUE(file.IsValid());
    std::string pdbPath =
        (fs::temp_directory_path() / "ilspy_diu_missing.pdb").string();
    RemoveFile(pdbPath);
    EXPECT_EQ(Pdb::FromFile(file, pdbPath), nullptr);
    EXPECT_EQ(Pdb::FromFile(file, ""), nullptr);
}
