// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MetadataExtensions strong-name assembly-identity family
// (cpp/Decompiler/Metadata/MetadataExtensions.{hpp,cpp}):
//   * CalculatePublicKeyToken -- the SHA-1 public-key-token derivation over a
//     raw #Blob offset (the vendored Util/Sha1ForNonSecretPurposes hashing);
//   * GetPublicKeyToken / GetFullAssemblyName over the assembly definition
//     (the reader extensions) and GetFullAssemblyName over an AssemblyReference
//     row (the PublicKey-flag / Retargetable / culture arms);
//   * TryGetFullAssemblyName (both forms) -- the BadImageFormatException catch
//     over the corrupted-Assembly-row fixture;
//   * MetadataFile::FullName (the C# MetadataFile.FullName property);
//   * MetadataFile::CorBlob (the raw #Blob payload read the token derivation
//     consumes).
//
// Every expected value is gold-dumped from the REAL ICSharpCode.Decompiler
// 11.0 extension methods over the identical fixtures by the FullAsmNameProbe
// gold probe (C:/temp-probe/FullAsmNameProbe): mscorlib 4.8, the GAC
// System.Runtime facade, .NET 10 CoreLib, the Roslyn cs satellite (the real
// non-neutral Culture fixture), tiny.netmodule, the ilasm keyless assembly,
// and the synthetic MetadataBuilder manifest (TestFixtures/
// AssemblyIdentityFixtures.hpp -- the PublicKey-flagged AssemblyRef, the
// Retargetable flag, the cultured rows, and the corrupt-Name variant no real
// file can carry).

#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "TestFixtures/AssemblyIdentityFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::Metadata::CalculatePublicKeyToken;
using ILSpy::Decompiler::Metadata::CorTableIndex;
using ILSpy::Decompiler::Metadata::GetFullAssemblyName;
using ILSpy::Decompiler::Metadata::GetPublicKeyToken;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::TryGetFullAssemblyName;

namespace {

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
           "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

// The GAC .NET Framework System.Runtime facade.
const char* FacadePath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
}

#if defined(_WIN32)
// The .NET 10 shared-runtime CoreLib: the highest installed 10.x
// Microsoft.NETCore.App (the golds pin the .NET 10 metadata shape; a
// newer major has a different type layout, so only 10.x matches).
std::string CoreLibPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    int bestMinor = -1;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        // "10.M.N" -- take the highest 10.x; other majors do not match
        // the pinned golds.
        if (name.rfind("10.", 0) != 0) continue;
        int minor = 0;
        try {
            minor = std::stoi(name.substr(3));
        } catch (const std::logic_error&) {
            continue;
        }
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (minor > bestMinor && fs::exists(candidate, ec)) {
            best = candidate;
            bestMinor = minor;
        }
    }
    return best;
}

// The Roslyn cs satellite assembly shipped in the .NET SDK packs -- the one
// local fixture with a non-neutral Culture column on the Assembly table
// ("cs"); glob-gated like the analyzer-PDB fixture.
std::string SatellitePath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\sdk";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate = it->path().string() +
            "\\Roslyn\\cs\\Microsoft.Build.Tasks.CodeAnalysis.resources.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
}
#endif

// The synthetic manifest written to a temp file (MetadataFile needs a real
// file).
std::string WriteSynth() {
    return WriteTempAssembly(SynthManifestBytes(), "ilspy_tokensynth_test.dll");
}

std::string WriteCorrupt() {
    return WriteTempAssembly(CorruptNameManifestBytes(),
                             "ilspy_tokensynth_corrupt_test.dll");
}

std::string WriteKeyless() {
    return WriteTempAssembly(KeylessAssemblyBytes(), "ilspy_keyless_test.dll");
}

// The Assembly table's PublicKey column offset (the raw blob offset the token
// derivation consumes), read through the port's own raw surface (the
// raw-surface row parameter is 0-based; the Assembly table's single row is
// index 0).
std::uint32_t AssemblyPublicKeyOffset(const MetadataFile& file) {
    return file.CorTableColumnValue(CorTableIndex::Assembly, 0, 3);
}

} // namespace

// ---------------------------------------------------------------------------
// GetPublicKeyToken (the reader extension)
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, GetPublicKeyTokenOverTheRealFixtures)
{
    {
        MetadataFile mscorlib(MscorlibPath());
        ASSERT_TRUE(mscorlib.IsValid());
        EXPECT_EQ(GetPublicKeyToken(mscorlib), "b77a5c561934e089");
    }
    {
        MetadataFile facade(FacadePath());
        ASSERT_TRUE(facade.IsValid());
        EXPECT_EQ(GetPublicKeyToken(facade), "b03f5f7f11d50a3a");
    }
#if defined(_WIN32)
    {
        std::string coreLibPath = CoreLibPath();
        if (coreLibPath.empty()) GTEST_SKIP() << "no .NET 10 runtime installed";
        MetadataFile corelib(coreLibPath);
        ASSERT_TRUE(corelib.IsValid());
        EXPECT_EQ(GetPublicKeyToken(corelib), "7cec85d7bea7798e");
    }
#endif
}

// The keyless ilasm assembly is the nil-PublicKey arm ("null"); the netmodule
// is the not-an-assembly arm (the empty string).
TEST(AssemblyIdentityTest, GetPublicKeyTokenNilAndNetmoduleArms)
{
    {
        std::string path = WriteKeyless();
        ASSERT_FALSE(path.empty());
        MetadataFile keyless(path);
        ASSERT_TRUE(keyless.IsValid());
        EXPECT_EQ(GetPublicKeyToken(keyless), "null");
        // The Assembly table carries the nil blob offset 0.
        EXPECT_EQ(AssemblyPublicKeyOffset(keyless), 0u);
    }
    {
        std::string path = WriteTinyNetModule();
        ASSERT_FALSE(path.empty());
        MetadataFile tiny(path);
        ASSERT_TRUE(tiny.IsValid());
        EXPECT_EQ(GetPublicKeyToken(tiny), "");
    }
}

TEST(AssemblyIdentityTest, GetPublicKeyTokenOverTheSyntheticManifest)
{
    std::string path = WriteSynth();
    ASSERT_FALSE(path.empty());
    MetadataFile synth(path);
    ASSERT_TRUE(synth.IsValid());
    // The 16-byte public key blob 0x10..0x1F (the real GetPublicKeyToken gold).
    EXPECT_EQ(GetPublicKeyToken(synth), "3b75642efd0b722c");
}

#if defined(_WIN32)
// The Roslyn cs satellite: the real non-neutral-culture fixture (Culture=cs
// on the assembly definition, the WPF public key token).
TEST(AssemblyIdentityTest, GetPublicKeyTokenOverTheCultureSatellite)
{
    std::string path = SatellitePath();
    if (path.empty()) GTEST_SKIP() << "no .NET SDK Roslyn satellite installed";
    MetadataFile satellite(path);
    ASSERT_TRUE(satellite.IsValid());
    const std::string full = GetFullAssemblyName(satellite);
    EXPECT_EQ(GetPublicKeyToken(satellite), "31bf3856ad364e35");
    // The Roslyn version moves with the installed SDK (5.3.x in the SDK
    // 10.0.2xx packs, 5.6.x in the 10.0.3xx packs); the identity is
    // re-derived from the file's own Assembly row so the rendering-form
    // assertion stays exact while the fixture version floats.
    const auto ver = satellite.CorTableVersionValue(CorTableIndex::Assembly, 0);
    const std::uint32_t nameOffset = satellite.CorTableColumnValue(
        CorTableIndex::Assembly, 0, 4);
    char version[32];
    std::snprintf(version, sizeof(version), "%u.%u.%u.%u",
        ver.MajorVersion, ver.MinorVersion, ver.BuildNumber,
        ver.RevisionNumber);
    EXPECT_EQ(full,
        satellite.CorString(nameOffset) + std::string(", Version=") + version
            + ", Culture=cs, PublicKeyToken=31bf3856ad364e35");
    // The satellite's single AssemblyRef: System.Runtime (the version the
    // compiling Roslyn targeted -- likewise derived from the file).
    const auto refVer = satellite.CorTableVersionValue(
        CorTableIndex::AssemblyRef, 0);
    const std::uint32_t refNameOffset = satellite.CorTableColumnValue(
        CorTableIndex::AssemblyRef, 0, 3);
    char refVersion[32];
    std::snprintf(refVersion, sizeof(refVersion), "%u.%u.%u.%u",
        refVer.MajorVersion, refVer.MinorVersion, refVer.BuildNumber,
        refVer.RevisionNumber);
    EXPECT_EQ(GetFullAssemblyName(satellite, 0x23000001),
        satellite.CorString(refNameOffset) + std::string(", Version=")
            + refVersion + ", Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
}
#endif

// ---------------------------------------------------------------------------
// GetFullAssemblyName (the reader extension)
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, GetFullAssemblyNameOverTheRealFixtures)
{
    {
        MetadataFile mscorlib(MscorlibPath());
        EXPECT_EQ(GetFullAssemblyName(mscorlib),
                  "mscorlib, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089");
    }
    {
        MetadataFile facade(FacadePath());
        EXPECT_EQ(GetFullAssemblyName(facade),
                  "System.Runtime, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b03f5f7f11d50a3a");
    }
#if defined(_WIN32)
    {
        std::string coreLibPath = CoreLibPath();
        if (coreLibPath.empty()) GTEST_SKIP() << "no .NET 10 runtime installed";
        MetadataFile corelib(coreLibPath);
        EXPECT_EQ(GetFullAssemblyName(corelib),
                  "System.Private.CoreLib, Version=10.0.0.0, Culture=neutral, "
                  "PublicKeyToken=7cec85d7bea7798e");
    }
#endif
    {
        std::string path = WriteKeyless();
        ASSERT_FALSE(path.empty());
        MetadataFile keyless(path);
        // The nil PublicKey column renders PublicKeyToken=null; the keyless
        // ilasm assembly carries Version 0.0.0.0.
        EXPECT_EQ(GetFullAssemblyName(keyless),
                  "t1, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    }
    {
        std::string path = WriteTinyNetModule();
        ASSERT_FALSE(path.empty());
        MetadataFile tiny(path);
        // The netmodule arm: the empty string.
        EXPECT_EQ(GetFullAssemblyName(tiny), "");
    }
}

TEST(AssemblyIdentityTest, GetFullAssemblyNameOverTheSyntheticManifest)
{
    std::string path = WriteSynth();
    ASSERT_FALSE(path.empty());
    MetadataFile synth(path);
    ASSERT_TRUE(synth.IsValid());
    // The synthetic assembly: the non-neutral culture segment and the
    // four-part version.
    EXPECT_EQ(GetFullAssemblyName(synth),
              "TokenSynth, Version=3.14.15.9, Culture=de, "
              "PublicKeyToken=3b75642efd0b722c");
}

// ---------------------------------------------------------------------------
// GetFullAssemblyName (the AssemblyReference extension)
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, GetFullAssemblyNameReferenceVariantOverSystemDll)
{
    MetadataFile system(SystemDllPath());
    ASSERT_TRUE(system.IsValid());
    EXPECT_EQ(GetFullAssemblyName(system, 0x23000001),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(GetFullAssemblyName(system, 0x23000002),
              "System.Configuration, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b03f5f7f11d50a3a");
    EXPECT_EQ(GetFullAssemblyName(system, 0x23000003),
              "System.Xml, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
}

TEST(AssemblyIdentityTest, GetFullAssemblyNameReferenceVariantOverTheFacade)
{
    MetadataFile facade(FacadePath());
    ASSERT_TRUE(facade.IsValid());
    EXPECT_EQ(GetFullAssemblyName(facade, 0x23000001),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(GetFullAssemblyName(facade, 0x23000002),
              "System.Core, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(GetFullAssemblyName(facade, 0x23000003),
              "System, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(GetFullAssemblyName(facade, 0x23000004),
              "System.ComponentModel.Composition, Version=4.0.0.0, "
              "Culture=neutral, PublicKeyToken=b77a5c561934e089");
}

TEST(AssemblyIdentityTest, GetFullAssemblyNameReferenceVariantOverTheSynth)
{
    std::string path = WriteSynth();
    ASSERT_FALSE(path.empty());
    MetadataFile synth(path);
    ASSERT_TRUE(synth.IsValid());
    // Row 1: the nil PublicKeyOrToken renders PublicKeyToken=null.
    EXPECT_EQ(GetFullAssemblyName(synth, 0x23000001),
              "RefNilToken, Version=1.0.0.0, Culture=neutral, "
              "PublicKeyToken=null");
    // Row 2: the PublicKey flag -- the column carries a FULL public key
    // (the 32-byte 0x21..0x40 blob), the token is its SHA-1 derivation.
    EXPECT_EQ(GetFullAssemblyName(synth, 0x23000002),
              "RefPublicKeyFlag, Version=2.0.0.0, Culture=neutral, "
              "PublicKeyToken=5114abb5c819b332");
    // Row 3: the plain 8-byte token plus the non-neutral ref culture.
    EXPECT_EQ(GetFullAssemblyName(synth, 0x23000003),
              "RefPlainToken, Version=4.0.0.0, Culture=fr, "
              "PublicKeyToken=b77a5c561934e089");
    // Row 4: the Retargetable tail.
    EXPECT_EQ(GetFullAssemblyName(synth, 0x23000004),
              "RefRetargetable, Version=5.6.7.8, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089, Retargetable=true");
    // Row 5: both flags together.
    EXPECT_EQ(GetFullAssemblyName(synth, 0x23000005),
              "RefBothFlags, Version=9.0.0.0, Culture=neutral, "
              "PublicKeyToken=5114abb5c819b332, Retargetable=true");
}

TEST(AssemblyIdentityTest, GetFullAssemblyNameReferenceVariantOverTinyNetModule)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MetadataFile tiny(path);
    ASSERT_TRUE(tiny.IsValid());
    // A netmodule carries no assembly manifest, but its own AssemblyRefs
    // render normally (the autodetected mscorlib extern).
    EXPECT_EQ(GetFullAssemblyName(tiny, 0x23000001),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
}

// ---------------------------------------------------------------------------
// TryGetFullAssemblyName (both forms)
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, TryGetFullAssemblyNameSucceedsOverValidFiles)
{
    {
        MetadataFile mscorlib(MscorlibPath());
        std::optional<std::string> name = TryGetFullAssemblyName(mscorlib);
        ASSERT_TRUE(name.has_value());
        EXPECT_EQ(*name,
                  "mscorlib, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089");
    }
    {
        std::string path = WriteTinyNetModule();
        ASSERT_FALSE(path.empty());
        MetadataFile tiny(path);
        // The netmodule arm is a SUCCESS carrying the empty string (the C#
        // TryGetFullAssemblyName returns true with out "").
        std::optional<std::string> name = TryGetFullAssemblyName(tiny);
        ASSERT_TRUE(name.has_value());
        EXPECT_EQ(*name, "");
    }
    {
        std::string path = WriteSynth();
        ASSERT_FALSE(path.empty());
        MetadataFile synth(path);
        std::optional<std::string> ref =
            TryGetFullAssemblyName(synth, 0x23000003);
        ASSERT_TRUE(ref.has_value());
        EXPECT_EQ(*ref,
                  "RefPlainToken, Version=4.0.0.0, Culture=fr, "
                  "PublicKeyToken=b77a5c561934e089");
    }
}

// The corrupted-Assembly-row fixture: the Name string index points past the
// #Strings heap end. The real reader throws BadImageFormatException 'Read
// out of bounds.' on the name read; the port's raw surface throws
// std::invalid_argument (the winmd seek throw) at the same read, and the Try
// form catches it into the false result.
TEST(AssemblyIdentityTest, TryGetFullAssemblyNameCatchesTheCorruptNameRow)
{
    std::string path = WriteCorrupt();
    ASSERT_FALSE(path.empty());
    MetadataFile corrupt(path);
    ASSERT_TRUE(corrupt.IsValid());
    // The file still parses as an assembly (the Assembly row exists -- the
    // corruption is only the string index).
    EXPECT_GT(corrupt.CorTableRowCount(CorTableIndex::Assembly), 0u);
    // The unguarded form propagates the raw-surface throw.
    EXPECT_THROW(GetFullAssemblyName(corrupt), std::invalid_argument);
    // The Try form catches it: nullopt (the C# false result).
    EXPECT_EQ(TryGetFullAssemblyName(corrupt), std::nullopt);
    // The public-key token alone does not read the Name column, so it still
    // resolves (the C# GetPublicKeyToken over the same bytes succeeds too).
    EXPECT_EQ(GetPublicKeyToken(corrupt), "3b75642efd0b722c");
}

// A reference row past the table end: the reader rejects the row and the Try
// form reports the failure (the C# GetAssemblyReference
// BadImageFormatException catch).
TEST(AssemblyIdentityTest, TryGetFullAssemblyNameReferenceVariantCatchesBadRows)
{
    std::string path = WriteSynth();
    ASSERT_FALSE(path.empty());
    MetadataFile synth(path);
    ASSERT_TRUE(synth.IsValid());
    EXPECT_THROW(GetFullAssemblyName(synth, 0x23000006), std::invalid_argument);
    EXPECT_EQ(TryGetFullAssemblyName(synth, 0x23000006), std::nullopt);
}

// ---------------------------------------------------------------------------
// MetadataFile::FullName
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, FullNameFollowsTheAssemblyArm)
{
    {
        MetadataFile mscorlib(MscorlibPath());
        EXPECT_EQ(mscorlib.FullName(),
                  "mscorlib, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089");
    }
    {
        std::string path = WriteTinyNetModule();
        ASSERT_FALSE(path.empty());
        MetadataFile tiny(path);
        // The netmodule arm: FullName falls back to Name.
        EXPECT_EQ(tiny.FullName(), "tiny");
    }
    {
        std::string path = WriteKeyless();
        ASSERT_FALSE(path.empty());
        MetadataFile keyless(path);
        EXPECT_EQ(keyless.FullName(),
                  "t1, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    }
}

// The C# MetadataFile.FullName propagates the BadImageFormatException of a
// corrupt Assembly row (no catch in the property); the port propagates the
// raw-surface throw the same way.
TEST(AssemblyIdentityTest, FullNamePropagatesTheCorruptRowThrow)
{
    std::string path = WriteCorrupt();
    ASSERT_FALSE(path.empty());
    MetadataFile corrupt(path);
    ASSERT_TRUE(corrupt.IsValid());
    EXPECT_THROW(corrupt.FullName(), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// CalculatePublicKeyToken + CorBlob (the pieces the token derivation consumes)
// ---------------------------------------------------------------------------

TEST(AssemblyIdentityTest, CalculatePublicKeyTokenDerivesTokensFromBlobs)
{
    {
        std::string path = WriteSynth();
        ASSERT_FALSE(path.empty());
        MetadataFile synth(path);
        ASSERT_TRUE(synth.IsValid());
        std::uint32_t offset = AssemblyPublicKeyOffset(synth);
        EXPECT_NE(offset, 0u);
        EXPECT_EQ(CalculatePublicKeyToken(synth, offset), "3b75642efd0b722c");
        // The blob itself: the 16 pattern bytes 0x10..0x1F.
        std::vector<std::uint8_t> blob = synth.CorBlob(offset);
        ASSERT_EQ(blob.size(), 16u);
        for (std::size_t i = 0; i < blob.size(); i++)
            EXPECT_EQ(blob[i], 0x10 + i) << "byte " << i;
    }
    {
        MetadataFile mscorlib(MscorlibPath());
        std::uint32_t offset = AssemblyPublicKeyOffset(mscorlib);
        EXPECT_NE(offset, 0u);
        // The .NET Framework's 16-byte "min key" blob -- the classic token
        // b77a5c561934e089 is the SHA-1 over exactly these bytes.
        EXPECT_EQ(mscorlib.CorBlob(offset).size(), 16u);
        EXPECT_EQ(CalculatePublicKeyToken(mscorlib, offset), "b77a5c561934e089");
    }
}

// The raw #Blob read: the nil offset 0 is the nil blob's zero length (the
// empty vector); an offset past the heap throws (the BadImageFormatException
// 'Read out of bounds.' analog).
TEST(AssemblyIdentityTest, CorBlobNilAndOutOfRangeArms)
{
    std::string path = WriteSynth();
    ASSERT_FALSE(path.empty());
    MetadataFile synth(path);
    ASSERT_TRUE(synth.IsValid());
    EXPECT_TRUE(synth.CorBlob(0).empty());
    EXPECT_THROW(synth.CorBlob(0x7FFFFFFF), std::invalid_argument);
}
