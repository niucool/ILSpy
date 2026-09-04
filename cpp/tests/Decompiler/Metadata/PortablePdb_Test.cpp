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

// Tests for the portable-PDB debug-table reader (cpp/Decompiler/Metadata/
// PortablePdb.hpp): the metadata-only BSJB layout, the #Pdb stream, the
// eight debug tables (0x30-0x37) with the pinned System.Reflection.Metadata
// column-width rules, the sequence-point blob decode, the document-name blob
// decode, and the local-scope range lookups.
//
// The 616-byte synthetic fixture (shared with the PortableDebugInfoProvider
// suite) covers the decode arms no locally installed PDB carries (a document
// switch inside one method's sequence points, a nil MethodDebugInformation
// Document column, a sequence-point blob truncated mid-point, LocalConstant
// rows, a StateMachineMethod row, and TupleElementNames/DynamicLocalVariables
// CustomDebugInformation rows); every expected value in the synthetic tests
// was verified against real System.Reflection.Metadata (the SDK-10 probe
// decodes the identical file). The Roslyn-analyzer fixture adds a real-world
// PDB parse (skipped when no .NET SDK pack is installed).

#include "Decompiler/Metadata/PortablePdb.hpp"
#include "TestFixtures/SyntheticPortablePdb.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Md = ILSpy::Decompiler::Metadata;
using ILSpy::Tests::kSyntheticPdb;
using ILSpy::Tests::LoadSyntheticPdb;

namespace {

// The synthetic fixture (the bytes, the layout map, and the loader) lives in
// the shared TestFixtures/SyntheticPortablePdb.hpp; the Roslyn-analyzer
// fixture below adds a real-world PDB parse.

std::string AnalyzerPdbPath() {
    namespace fs = std::filesystem;
    const char* root =
        "C:\\Program Files\\dotnet\\packs\\Microsoft.WindowsDesktop.App.Ref";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::string candidate = it->path().string()
            + "\\analyzers\\dotnet\\cs\\System.Windows.Forms.Analyzers.CSharp.pdb";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
}

std::string BlobString(const Md::PortablePdb& pdb, std::uint32_t offset) {
    auto blob = pdb.GetBlob(offset);
    return std::string(reinterpret_cast<const char*>(blob.data()), blob.size());
}

std::string GuidString(const std::array<std::uint8_t, 16>& g) {
    // The #GUID heap stores the canonical little-endian binary form; the
    // string form reverses the first three groups (Guid.ToString).
    char buf[40];
    std::snprintf(buf, sizeof(buf),
        "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11],
        g[12], g[13], g[14], g[15]);
    return buf;
}

} // namespace

TEST(PortablePdbTest, ParsesTheMetadataRootAndPdbStream) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());
    EXPECT_EQ(pdb.VersionString(), "PDB v1.0");
    EXPECT_TRUE(pdb.HasPdbStream());

    EXPECT_EQ(pdb.RowCount(Md::PdbTable::Document), 3u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::MethodDebugInformation), 3u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalScope), 5u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalVariable), 4u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalConstant), 2u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::ImportScope), 2u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::StateMachineMethod), 1u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::CustomDebugInformation), 3u);

    EXPECT_EQ(pdb.EntryPointToken(), 0x06000001u);
    const std::uint8_t* id = pdb.PdbId();
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id[0], 0xDE);
    EXPECT_EQ(id[1], 0xAD);
    EXPECT_EQ(id[2], 0xBE);
    EXPECT_EQ(id[3], 0xEF);
    EXPECT_EQ(id[16], 0x02);  // the 4-byte age tail of the 20-byte ID

    // The #Pdb stream referenced only the assembly's MethodDef table, with
    // 4 rows (the count that sizes the LocalScope/StateMachineMethod columns).
    EXPECT_EQ(pdb.ExternalRowCount(0x06), 4u);
    EXPECT_EQ(pdb.ExternalRowCount(0x02), 0u);
}

TEST(PortablePdbTest, DocumentsDecodeNamesHashesAndLanguages) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    Md::PdbDocumentRow doc1 = pdb.GetDocument(1);
    EXPECT_EQ(pdb.GetDocumentName(doc1.NameBlob), "C:\\a.cs");
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(doc1.HashAlgorithmGuid)),
              "8829D00F-11B8-4213-878B-770E8597AC16");  // SHA-256
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(doc1.LanguageGuid)),
              "3F5162F8-07C6-11D3-9053-00C04FA302A1");  // C#
    EXPECT_EQ(BlobString(pdb, doc1.HashBlob), "D1D1");

    Md::PdbDocumentRow doc2 = pdb.GetDocument(2);
    EXPECT_EQ(pdb.GetDocumentName(doc2.NameBlob), "C:\\b.cs");
    EXPECT_EQ(BlobString(pdb, doc2.HashBlob), "D2D2");

    Md::PdbDocumentRow doc3 = pdb.GetDocument(3);
    EXPECT_EQ(pdb.GetDocumentName(doc3.NameBlob), "C:\\c.cs");
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(doc3.HashAlgorithmGuid)),
              "FF1816EC-AA5D-4D10-B87F-C1E2B0B5C0FD");  // SHA-1
    EXPECT_EQ(BlobString(pdb, doc3.HashBlob), "D3D3");

    // Out-of-range rows read as the nil shape; a nil GUID index is Guid.Empty.
    EXPECT_EQ(pdb.GetDocument(4).NameBlob, 0u);
    auto empty = pdb.TryGetGuid(0);
    ASSERT_TRUE(empty.has_value());
    for (std::uint8_t b : *empty) EXPECT_EQ(b, 0);
}

TEST(PortablePdbTest, SequencePointsSwitchDocumentsAndHide) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    // Method 1's row has a non-nil Document column (doc 1); its blob switches
    // to doc 2 for the third point and back for the fourth. Hidden points
    // carry the 0xfeefee marker in both line fields.
    std::vector<Md::PdbSequencePoint> points = pdb.GetSequencePoints(1);
    ASSERT_EQ(points.size(), 4u);
    EXPECT_EQ(points[0].Document, 1u);
    EXPECT_EQ(points[0].Offset, 0u);
    EXPECT_EQ(points[0].StartLine, 10);
    EXPECT_EQ(points[0].StartColumn, 3);
    EXPECT_EQ(points[0].EndLine, 10);
    EXPECT_EQ(points[0].EndColumn, 7);
    EXPECT_FALSE(points[0].IsHidden());

    EXPECT_TRUE(points[1].IsHidden());
    EXPECT_EQ(points[1].Offset, 5u);
    EXPECT_EQ(points[1].Document, 1u);
    EXPECT_EQ(points[1].StartLine, 0xfeefee);
    EXPECT_EQ(points[1].EndLine, 0xfeefee);
    EXPECT_EQ(points[1].StartColumn, 0);
    EXPECT_EQ(points[1].EndColumn, 0);

    EXPECT_EQ(points[2].Document, 2u);
    EXPECT_EQ(points[2].Offset, 10u);
    EXPECT_EQ(points[2].StartLine, 20);
    EXPECT_EQ(points[2].StartColumn, 5);
    EXPECT_EQ(points[2].EndLine, 22);
    EXPECT_EQ(points[2].EndColumn, 1);

    EXPECT_EQ(points[3].Document, 1u);
    EXPECT_EQ(points[3].Offset, 20u);
    EXPECT_EQ(points[3].StartLine, 30);
    EXPECT_EQ(points[3].StartColumn, 8);
    EXPECT_EQ(points[3].EndLine, 30);
    EXPECT_EQ(points[3].EndColumn, 12);

    EXPECT_EQ(pdb.GetLocalSignature(1), 1u);
}

TEST(PortablePdbTest, SequencePointsReadTheBlobDocumentWhenTheColumnIsNil) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    Md::PdbMethodDebugInformationRow mdi = pdb.GetMethodDebugInformation(2);
    EXPECT_EQ(mdi.Document, 0u);  // nil column: the blob names its document
    std::vector<Md::PdbSequencePoint> points = pdb.GetSequencePoints(2);
    ASSERT_EQ(points.size(), 3u);
    EXPECT_EQ(points[0].Document, 3u);
    EXPECT_EQ(points[0].Offset, 0u);
    EXPECT_EQ(points[0].StartLine, 100);
    EXPECT_EQ(points[0].StartColumn, 2);
    EXPECT_EQ(points[0].EndLine, 103);
    EXPECT_EQ(points[0].EndColumn, 3);
    EXPECT_TRUE(points[1].IsHidden());
    EXPECT_EQ(points[1].Offset, 4u);
    EXPECT_EQ(points[1].Document, 3u);
    EXPECT_EQ(points[2].Document, 3u);
    EXPECT_EQ(points[2].Offset, 9u);
    EXPECT_EQ(points[2].StartLine, 102);
    EXPECT_EQ(points[2].StartColumn, 4);
    EXPECT_EQ(points[2].EndLine, 102);
    EXPECT_EQ(points[2].EndColumn, 10);

    EXPECT_EQ(pdb.GetLocalSignature(2), 2u);
}

TEST(PortablePdbTest, MethodsWithoutDebugInformationHaveNoPoints) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    // The MethodDebugInformation table holds three rows; methods past it
    // (method 4, the state machine's MoveNext among them) carry nothing.
    EXPECT_TRUE(pdb.GetSequencePoints(4).empty());
    EXPECT_EQ(pdb.GetMethodDebugInformation(4).SequencePointsBlob, 0u);
    EXPECT_EQ(pdb.GetMethodDebugInformation(5).SequencePointsBlob, 0u);
    EXPECT_EQ(pdb.GetMethodDebugInformation(1).SequencePointsBlob != 0u, true);
}

TEST(PortablePdbTest, TruncatedSequencePointBlobThrows) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    // Method 3's debug-information row points at a blob cut off mid-point:
    // the local signature and the blob document id decode, then the point
    // ends before its column deltas. The raw reader throws (the C#
    // BadImageFormatException the provider's catch turns into the empty
    // list), and the signature prefix still decodes.
    EXPECT_EQ(pdb.GetLocalSignature(3), 3u);
    EXPECT_NE(pdb.GetMethodDebugInformation(3).SequencePointsBlob, 0u);
    EXPECT_THROW(pdb.GetSequencePoints(3), std::out_of_range);
}

TEST(PortablePdbTest, LocalScopesRangePerMethod) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    EXPECT_EQ(pdb.GetLocalScopeRange(1).First, 1);
    EXPECT_EQ(pdb.GetLocalScopeRange(1).Last, 2);
    EXPECT_EQ(pdb.GetLocalScopeRange(2).First, 3);
    EXPECT_EQ(pdb.GetLocalScopeRange(2).Last, 3);
    EXPECT_EQ(pdb.GetLocalScopeRange(3).First, 4);
    EXPECT_EQ(pdb.GetLocalScopeRange(3).Last, 4);
    EXPECT_EQ(pdb.GetLocalScopeRange(4).First, 5);
    EXPECT_EQ(pdb.GetLocalScopeRange(4).Last, 5);
    EXPECT_TRUE(pdb.GetLocalScopeRange(5).IsEmpty());

    Md::PdbLocalScopeRow scope2 = pdb.GetLocalScope(2);
    EXPECT_EQ(scope2.Method, 1u);
    EXPECT_EQ(scope2.ImportScope, 2u);
    EXPECT_EQ(scope2.StartOffset, 2);
    EXPECT_EQ(scope2.Length, 4);
    EXPECT_EQ(scope2.EndOffset(), 6);

    Md::PdbLocalScopeRow scope5 = pdb.GetLocalScope(5);
    EXPECT_EQ(scope5.Method, 4u);
    EXPECT_EQ(scope5.StartOffset, 1);
    EXPECT_EQ(scope5.Length, 2);
}

TEST(PortablePdbTest, LocalVariableAndConstantRangesFollowTheSrmRules) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    // Scope 1: vars [1,2] (next row's start - 1), consts [1,1].
    EXPECT_EQ(pdb.GetLocalVariableRange(1).First, 1);
    EXPECT_EQ(pdb.GetLocalVariableRange(1).Last, 2);
    EXPECT_EQ(pdb.GetLocalConstantRange(1).First, 1);
    EXPECT_EQ(pdb.GetLocalConstantRange(1).Last, 1);
    // Scope 2: vars [3,3], consts [2,2].
    EXPECT_EQ(pdb.GetLocalVariableRange(2).First, 3);
    EXPECT_EQ(pdb.GetLocalVariableRange(2).Last, 3);
    EXPECT_EQ(pdb.GetLocalConstantRange(2).First, 2);
    EXPECT_EQ(pdb.GetLocalConstantRange(2).Last, 2);
    // Scope 3's own starts point past the table ends: empty ranges both ways
    // (the next-row nil start terminates the run at -1).
    EXPECT_TRUE(pdb.GetLocalVariableRange(3).IsEmpty());
    EXPECT_TRUE(pdb.GetLocalConstantRange(3).IsEmpty());
    // Scope 4 has nil own starts: empty.
    EXPECT_TRUE(pdb.GetLocalVariableRange(4).IsEmpty());
    EXPECT_TRUE(pdb.GetLocalConstantRange(4).IsEmpty());
    // Scope 5 is the LAST row: its variables run to the table end -> [4,4].
    EXPECT_EQ(pdb.GetLocalVariableRange(5).First, 4);
    EXPECT_EQ(pdb.GetLocalVariableRange(5).Last, 4);
    EXPECT_TRUE(pdb.GetLocalConstantRange(5).IsEmpty());
}

TEST(PortablePdbTest, LocalVariablesAndConstantsReadRows) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    struct Expected { std::uint16_t attrs; std::uint16_t index; const char* name; };
    const Expected expected[] = {
        {0, 0, "v0"}, {0, 1, "v1"}, {0, 2, "flag"}, {1, 5, "hidden"},
    };
    for (std::uint32_t row = 1; row <= 4; ++row) {
        Md::PdbLocalVariableRow v = pdb.GetLocalVariable(row);
        ASSERT_EQ(v.Attributes, expected[row - 1].attrs);
        ASSERT_EQ(v.Index, expected[row - 1].index);
        ASSERT_EQ(pdb.GetString(v.Name), expected[row - 1].name);
    }

    Md::PdbLocalConstantRow maxVal = pdb.GetLocalConstant(1);
    EXPECT_EQ(pdb.GetString(maxVal.Name), "MaxVal");
    EXPECT_EQ(BlobString(pdb, maxVal.Signature), std::string("\x08", 1));
    Md::PdbLocalConstantRow greeting = pdb.GetLocalConstant(2);
    EXPECT_EQ(pdb.GetString(greeting.Name), "Greeting");
    EXPECT_EQ(BlobString(pdb, greeting.Signature), std::string("\x0E", 1));

    // Out-of-range rows read as the nil shape.
    EXPECT_EQ(pdb.GetLocalVariable(5).Name, 0u);
    EXPECT_EQ(pdb.GetLocalConstant(3).Name, 0u);
}

TEST(PortablePdbTest, ImportScopesChainAndStateMachineKickoff) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    Md::PdbImportScopeRow root = pdb.GetImportScope(1);
    EXPECT_EQ(root.Parent, 0u);  // the root scope's nil parent
    EXPECT_EQ(BlobString(pdb, root.Imports), std::string("\x11\x22\x33", 3));
    Md::PdbImportScopeRow child = pdb.GetImportScope(2);
    EXPECT_EQ(child.Parent, 1u);
    EXPECT_EQ(BlobString(pdb, child.Imports), std::string("\x44", 1));

    Md::PdbStateMachineMethodRow sm = pdb.GetStateMachineMethod(1);
    EXPECT_EQ(sm.MoveNextMethod, 3u);
    EXPECT_EQ(sm.KickoffMethod, 1u);
    EXPECT_EQ(pdb.FindStateMachineKickoffMethod(3), 1u);
    EXPECT_EQ(pdb.FindStateMachineKickoffMethod(1), 0u);
    EXPECT_EQ(pdb.FindStateMachineKickoffMethod(2), 0u);
    EXPECT_EQ(pdb.FindStateMachineKickoffMethod(4), 0u);
}

TEST(PortablePdbTest, CustomDebugInformationResolvesTheCodedParent) {
    Md::PortablePdb pdb = LoadSyntheticPdb();
    ASSERT_TRUE(pdb.IsValid());

    // Row 1: a MethodDef parent (tag 0) with the DynamicLocalVariables kind
    // and a one-byte dynamic-flags blob.
    Md::PdbCustomDebugInformationRow row1 = pdb.GetCustomDebugInformation(1);
    EXPECT_EQ(row1.ParentToken, 0x06000002u);
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(row1.KindGuid)),
              "83C563C4-B4F3-47D5-B824-BA5441477EA8");
    EXPECT_EQ(BlobString(pdb, row1.Value), std::string("\x07", 1));

    // Row 2: a LocalVariable parent (tag 24) with the TupleElementNames kind
    // and the null-terminated UTF-8 name parts.
    Md::PdbCustomDebugInformationRow row2 = pdb.GetCustomDebugInformation(2);
    EXPECT_EQ(row2.ParentToken, 0x33000003u);
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(row2.KindGuid)),
              "ED9FDF71-8879-4747-8ED3-FE5EDE3CE710");
    EXPECT_EQ(BlobString(pdb, row2.Value), std::string("name\0 \0age\0", 11));

    // Row 3: another LocalVariable parent (row 4, the 'hidden' var) with
    // the DynamicLocalVariables kind and a two-byte dynamic-flags blob.
    Md::PdbCustomDebugInformationRow row3 = pdb.GetCustomDebugInformation(3);
    EXPECT_EQ(row3.ParentToken, 0x33000004u);
    EXPECT_EQ(GuidString(*pdb.TryGetGuid(row3.KindGuid)),
              "83C563C4-B4F3-47D5-B824-BA5441477EA8");
    EXPECT_EQ(BlobString(pdb, row3.Value), std::string("\x05\x80", 2));

    EXPECT_EQ(pdb.GetCustomDebugInformation(4).ParentToken, 0u);
}

TEST(PortablePdbTest, RejectsNonPortableAndMalformedImages) {
    // Empty and null buffers.
    EXPECT_FALSE(Md::PortablePdb(nullptr).IsValid());
    EXPECT_FALSE(Md::PortablePdb(std::make_shared<std::vector<std::uint8_t>>()).IsValid());
    // A PE file is not a portable PDB (no BSJB magic at offset 0).
    std::vector<std::uint8_t> mz{'M', 'Z', 0, 0};
    EXPECT_FALSE(Md::PortablePdb::IsPortablePdbImage(mz.data(), mz.size()));
    EXPECT_FALSE(Md::PortablePdb(
        std::make_shared<std::vector<std::uint8_t>>(mz)).IsValid());
    // A truncated BSJB root.
    std::vector<std::uint8_t> truncated{'B', 'S', 'J', 'B'};
    EXPECT_FALSE(Md::PortablePdb(
        std::make_shared<std::vector<std::uint8_t>>(truncated)).IsValid());
    // A metadata root naming an unknown stream.
    std::vector<std::uint8_t> bad = {kSyntheticPdb[0], kSyntheticPdb[1],
                                     kSyntheticPdb[2], kSyntheticPdb[3]};
    bad.insert(bad.end(), {'#', 'X', 'x', 'x'});
    EXPECT_FALSE(Md::PortablePdb(
        std::make_shared<std::vector<std::uint8_t>>(bad)).IsValid());

    // An invalid instance degrades: empty reads, no throws.
    Md::PortablePdb invalid = Md::PortablePdb(nullptr);
    EXPECT_EQ(invalid.RowCount(Md::PdbTable::Document), 0u);
    EXPECT_TRUE(invalid.GetSequencePoints(1).empty());
    EXPECT_EQ(invalid.GetDocument(1).NameBlob, 0u);
    EXPECT_TRUE(invalid.GetLocalScopeRange(1).IsEmpty());
    EXPECT_FALSE(invalid.TryGetGuid(1).has_value());
}

TEST(PortablePdbTest, LoadsARealRoslynPortablePdb) {
    std::string path = AnalyzerPdbPath();
    if (path.empty())
        GTEST_SKIP() << "no .NET SDK WindowsDesktop.App.Ref analyzer PDB installed";

    Md::PortablePdb pdb = Md::PortablePdb::LoadFile(path);
    ASSERT_TRUE(pdb.IsValid());
    EXPECT_EQ(pdb.VersionString(), "PDB v1.0");
    EXPECT_EQ(pdb.EntryPointToken(), 0u);  // a library: no entry point
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::Document), 13u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::MethodDebugInformation), 79u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalScope), 72u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalVariable), 59u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalConstant), 1u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::ImportScope), 57u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::StateMachineMethod), 0u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::CustomDebugInformation), 10u);

    // The #Pdb stream counts the analyzer assembly's own tables; its
    // MethodDef count matches the MethodDebugInformation rows.
    EXPECT_EQ(pdb.ExternalRowCount(0x06), 79u);

    // A real document name, with multi-byte compressed part offsets.
    EXPECT_EQ(pdb.GetDocumentName(pdb.GetDocument(1).NameBlob),
              "/_/src/winforms/src/System.Windows.Forms.Analyzers.CSharp/src/"
              "Properties/AssemblyInfo.cs");

    // Methods before the first body have no debug info.
    EXPECT_TRUE(pdb.GetSequencePoints(1).empty());
    EXPECT_TRUE(pdb.GetSequencePoints(5).empty());
    // Method 6 has one sequence point on document 12.
    std::vector<Md::PdbSequencePoint> points = pdb.GetSequencePoints(6);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0].Document, 12u);
    EXPECT_EQ(points[0].Offset, 0u);
    EXPECT_EQ(points[0].StartLine, 10);
    EXPECT_EQ(points[0].StartColumn, 85);
    EXPECT_EQ(points[0].EndLine, 10);
    EXPECT_EQ(points[0].EndColumn, 184);
    EXPECT_EQ(pdb.GetLocalSignature(6), 0u);  // a body with no locals

    // A real scope: method 35's outer scope holds the "code" local (index 0)
    // and the "qualifier" local constant (a string, sig 0x0E + UTF-16 value).
    Md::PdbLocalScopeRow scope = pdb.GetLocalScope(29);
    EXPECT_EQ(scope.Method, 35u);
    EXPECT_EQ(scope.StartOffset, 0);
    EXPECT_EQ(scope.Length, 181);
    EXPECT_EQ(pdb.GetLocalVariableRange(29).First, 10);
    EXPECT_EQ(pdb.GetLocalVariableRange(29).Last, 10);
    EXPECT_EQ(pdb.GetString(pdb.GetLocalVariable(10).Name), "code");
    EXPECT_EQ(pdb.GetLocalVariable(10).Index, 0);
    EXPECT_EQ(pdb.GetLocalConstantRange(29).First, 1);
    EXPECT_EQ(pdb.GetLocalConstantRange(29).Last, 1);
    EXPECT_EQ(pdb.GetString(pdb.GetLocalConstant(1).Name), "qualifier");
    auto sig = pdb.GetBlob(pdb.GetLocalConstant(1).Signature);
    ASSERT_FALSE(sig.empty());
    EXPECT_EQ(sig[0], 0x0E);
}
