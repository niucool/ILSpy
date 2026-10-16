// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the metadata-backed AssemblyReference row wrapper port
// (cpp/Decompiler/Metadata/AssemblyNameReference.{hpp,cpp} -- the
// `AssemblyReference : IAssemblyReference` class, AssemblyReferences.cs lines
// 219-330): the lazy Name/FullName pair with the corrupt-row catch fallbacks,
// the raw version/culture columns, and the public-key-token derivation.
//
// Every expectation is gold-pinned against the REAL ICSharpCode.Decompiler
// 11.0's public `Metadata.AssemblyReference` class driven over the identical
// fixtures (the C:/temp-probe/ResolutionProbe gold probe, section B):
//   * System.dll's three real AssemblyRefs (mscorlib / System.Configuration /
//     System.Xml),
//   * tiny.netmodule's mscorlib ref,
//   * the iteration-63 synthetic manifest's five arm rows (the nil
//     PublicKeyOrToken, the PublicKey-flag full key with the SHA-1-derived
//     token BYTES, the plain token + culture, the Retargetable flag, both
//     flags),
//   * the corrupt-AssemblyRef variant (the first row's Name column re-pointed
//     past the #Strings heap end -- the "AR:{Handle}" /
//     "fullname(AR:{Handle})" catch fallbacks, the nil-column PublicKeyToken
//     null, and the fixed-width Version column still reading through the
//     corruption).

#include "TestFixtures/AssemblyIdentityFixtures.hpp"
#include "TestFixtures/ResolutionFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

std::string Hex(const std::vector<std::uint8_t>& bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (std::uint8_t b : bytes)
    {
        result += digits[b >> 4];
        result += digits[b & 0xF];
    }
    return result;
}

std::string Culture(const TM::AssemblyReference& ar)
{
    std::optional<std::string> culture = ar.Culture();
    return culture.has_value() ? "'" + *culture + "'" : "<null>";
}

std::string Pkt(const TM::AssemblyReference& ar)
{
    std::optional<std::vector<std::uint8_t>> pkt = ar.GetPublicKeyToken();
    return pkt.has_value() ? Hex(*pkt) : std::string("<null>");
}

} // namespace

// The three real System.dll rows: the identity surface end to end.
TEST(AssemblyReferenceTest, SystemDllRows)
{
    TM::MetadataFile file(SystemPath());
    std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
        file.GetAssemblyReferences();
    ASSERT_EQ(rows.size(), 3u);

    TM::AssemblyReference mscorlib(file, rows[0].Token);
    EXPECT_EQ(mscorlib.Name(), "mscorlib");
    EXPECT_EQ(mscorlib.FullName(),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(mscorlib.Version()->ToString(), "4.0.0.0");
    EXPECT_EQ(Culture(mscorlib), "''");
    EXPECT_EQ(Pkt(mscorlib), "b77a5c561934e089");
    EXPECT_FALSE(mscorlib.IsWindowsRuntime());
    EXPECT_FALSE(mscorlib.IsRetargetable());

    TM::AssemblyReference configuration(file, rows[1].Token);
    EXPECT_EQ(configuration.Name(), "System.Configuration");
    EXPECT_EQ(configuration.FullName(),
              "System.Configuration, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b03f5f7f11d50a3a");
    EXPECT_EQ(Pkt(configuration), "b03f5f7f11d50a3a");

    TM::AssemblyReference xml(file, rows[2].Token);
    EXPECT_EQ(xml.Name(), "System.Xml");
    EXPECT_EQ(Pkt(xml), "b77a5c561934e089");

    // The interface accessor and GetPublicKeyToken agree.
    EXPECT_EQ(Hex(*mscorlib.PublicKeyToken()), "b77a5c561934e089");
}

// tiny.netmodule's mscorlib ref (the netmodule fixture).
TEST(AssemblyReferenceTest, TinyNetModuleRow)
{
    std::string tinyPath = WriteResolutionTempFile(
        TinyNetModuleBytes(), "ilspy_asmref_tiny.netmodule");
    ASSERT_FALSE(tinyPath.empty());
    TM::MetadataFile file(tinyPath);
    std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
        file.GetAssemblyReferences();
    ASSERT_EQ(rows.size(), 1u);
    TM::AssemblyReference ar(file, rows[0].Token);
    EXPECT_EQ(ar.Name(), "mscorlib");
    EXPECT_EQ(ar.FullName(),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(ar.Version()->ToString(), "4.0.0.0");
}

// The synthetic manifest's five arm rows: the nil key, the PublicKey-flag
// SHA-1 token, the plain token + culture, Retargetable, and both flags.
TEST(AssemblyReferenceTest, SyntheticManifestArmRows)
{
    std::string path = WriteTempAssembly(SynthManifestBytes(),
                                         "ilspy_asmref_synth.dll");
    ASSERT_FALSE(path.empty());
    TM::MetadataFile file(path);
    std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
        file.GetAssemblyReferences();
    ASSERT_EQ(rows.size(), 5u);

    {
        // (a) the nil PublicKeyOrToken -> the null token.
        TM::AssemblyReference ar(file, rows[0].Token);
        EXPECT_EQ(ar.Name(), "RefNilToken");
        EXPECT_EQ(ar.FullName(),
                  "RefNilToken, Version=1.0.0.0, Culture=neutral, "
                  "PublicKeyToken=null");
        EXPECT_EQ(ar.Version()->ToString(), "1.0.0.0");
        EXPECT_EQ(Culture(ar), "''");
        EXPECT_EQ(Pkt(ar), "<null>");
        EXPECT_FALSE(ar.IsWindowsRuntime());
        EXPECT_FALSE(ar.IsRetargetable());
    }
    {
        // (b) the PublicKey flag: the SHA-1-derived token BYTES (the digest's
        // last 8 bytes -- the token STRING 5114abb5c819b332 is these bytes
        // rendered).
        TM::AssemblyReference ar(file, rows[1].Token);
        EXPECT_EQ(ar.Name(), "RefPublicKeyFlag");
        EXPECT_EQ(ar.FullName(),
                  "RefPublicKeyFlag, Version=2.0.0.0, Culture=neutral, "
                  "PublicKeyToken=5114abb5c819b332");
        EXPECT_EQ(Pkt(ar), "32b319c8b5ab1451");
    }
    {
        // (c) the plain 8-byte token + the non-neutral culture.
        TM::AssemblyReference ar(file, rows[2].Token);
        EXPECT_EQ(ar.Name(), "RefPlainToken");
        EXPECT_EQ(ar.FullName(),
                  "RefPlainToken, Version=4.0.0.0, Culture=fr, "
                  "PublicKeyToken=b77a5c561934e089");
        EXPECT_EQ(Culture(ar), "'fr'");
        EXPECT_EQ(Pkt(ar), "b77a5c561934e089");
    }
    {
        // (d) the Retargetable flag over the plain token.
        TM::AssemblyReference ar(file, rows[3].Token);
        EXPECT_EQ(ar.Name(), "RefRetargetable");
        EXPECT_EQ(ar.FullName(),
                  "RefRetargetable, Version=5.6.7.8, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089, Retargetable=true");
        EXPECT_EQ(ar.Version()->ToString(), "5.6.7.8");
        EXPECT_TRUE(ar.IsRetargetable());
        EXPECT_FALSE(ar.IsWindowsRuntime());
    }
    {
        // (e) both flags together.
        TM::AssemblyReference ar(file, rows[4].Token);
        EXPECT_EQ(ar.Name(), "RefBothFlags");
        EXPECT_EQ(ar.FullName(),
                  "RefBothFlags, Version=9.0.0.0, Culture=neutral, "
                  "PublicKeyToken=5114abb5c819b332, Retargetable=true");
        EXPECT_EQ(Pkt(ar), "32b319c8b5ab1451");
        EXPECT_TRUE(ar.IsRetargetable());
    }
}

// The corrupt-AssemblyRef variant: the first row's Name column points past
// the #Strings heap end -- the catch fallbacks and the fixed-width/raw
// columns that still read through the corruption.
TEST(AssemblyReferenceTest, CorruptRowFallbacks)
{
    std::string path = WriteResolutionTempFile(CorruptRefManifestBytes(),
                                               "ilspy_asmref_corrupt.dll");
    ASSERT_FALSE(path.empty());
    TM::MetadataFile file(path);
    // The whole-table GetAssemblyReferences walk DEGRADES at the corrupt
    // first row (its Name read breaks the walk), so the row tokens come from
    // the raw row count: row 1 is the corrupt row, row 2 the plain one.
    ASSERT_EQ(file.CorTableRowCount(TM::CorTableIndex::AssemblyRef), 2u);

    TM::AssemblyReference corrupt(file, 0x23000001u);
    // The C# `$"AR:{Handle}"` fallback -- the SRM handle struct's ToString is
    // its TYPE NAME (no override), so the interpolation renders the
    // fully-qualified handle type.
    EXPECT_EQ(corrupt.Name(),
              "AR:System.Reflection.Metadata.AssemblyReferenceHandle");
    EXPECT_EQ(corrupt.FullName(),
              "fullname(AR:System.Reflection.Metadata."
              "AssemblyReferenceHandle)");
    // The fixed-width Version column reads through the corruption.
    EXPECT_EQ(corrupt.Version()->ToString(), "1.0.0.0");
    // The Culture column is a DIFFERENT heap index and still reads (the
    // corrupt row's culture column is nil -> "").
    EXPECT_EQ(Culture(corrupt), "''");
    // The nil PublicKeyOrToken column returns null WITHOUT any name read
    // (no throw through the corruption).
    EXPECT_EQ(Pkt(corrupt), "<null>");
    EXPECT_FALSE(corrupt.IsWindowsRuntime());
    EXPECT_FALSE(corrupt.IsRetargetable());

    // The SECOND row is independent (the lazy caches are per-instance) and
    // reads fine.
    TM::AssemblyReference plain(file, 0x23000002u);
    EXPECT_EQ(plain.Name(), "RefPlainToken");
    EXPECT_EQ(plain.FullName(),
              "RefPlainToken, Version=4.0.0.0, Culture=fr, "
              "PublicKeyToken=null");
    EXPECT_EQ(Culture(plain), "'fr'");
    EXPECT_EQ(Pkt(plain), "<null>");
}

// The nil token (the nil handle): the C# reads a default row whose name read
// throws BadImageFormatException -- the port's underflowed row index hits the
// raw bounds check and takes the same catch fallbacks.
TEST(AssemblyReferenceTest, NilTokenTakesTheFallbackArms)
{
    TM::MetadataFile file(SystemPath());
    TM::AssemblyReference ar(file, 0);
    EXPECT_EQ(ar.Name(),
              "AR:System.Reflection.Metadata.AssemblyReferenceHandle");
    EXPECT_EQ(ar.FullName(),
              "fullname(AR:System.Reflection.Metadata."
              "AssemblyReferenceHandle)");
}
