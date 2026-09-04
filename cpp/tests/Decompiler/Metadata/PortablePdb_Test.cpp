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
// The 596-byte synthetic fixture below covers the decode arms no locally
// installed PDB carries (a document switch inside one method's sequence
// points, a nil MethodDebugInformation Document column, LocalConstant rows,
// a StateMachineMethod row, and TupleElementNames/DynamicLocalVariables
// CustomDebugInformation rows); every expected value in the synthetic tests
// was verified against real System.Reflection.Metadata (the SDK-10 probe
// decodes the identical file). The Roslyn-analyzer fixture adds a real-world
// PDB parse (skipped when no .NET SDK pack is installed).

#include "Decompiler/Metadata/PortablePdb.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Md = ILSpy::Decompiler::Metadata;

namespace {

// A minimal portable PDB: three documents (two hash algorithms), two
// MethodDebugInformation rows (method 1 with a non-nil Document column and a
// document switch in its sequence points; method 2 with a nil column whose
// blob names its own document), five LocalScope rows over four methods (the
// mid-row, past-the-end, nil-own-start, and last-row range rules), four
// LocalVariable rows (one DebuggerHidden), two LocalConstant rows, a
// two-row ImportScope chain, one StateMachineMethod row, and two
// CustomDebugInformation rows (a MethodDef parent and a LocalVariable
// parent).
constexpr std::uint8_t kSyntheticPdb[] = {
    0x42, 0x53, 0x4A, 0x42, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00,
    0x50, 0x44, 0x42, 0x20, 0x76, 0x31, 0x2E, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x23, 0x50, 0x64, 0x62, 0x00, 0x00, 0x00, 0x00,
    0x94, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x23, 0x7E, 0x00, 0x00, 0x74, 0x01, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x23, 0x53, 0x74, 0x72, 0x69, 0x6E, 0x67, 0x73, 0x00, 0x00, 0x00, 0x00,
    0x98, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x23, 0x47, 0x55, 0x49, 0x44, 0x00, 0x00, 0x00,
    0xE8, 0x01, 0x00, 0x00, 0x6C, 0x00, 0x00, 0x00, 0x23, 0x42, 0x6C, 0x6F, 0x62, 0x00, 0x00, 0x00,
    0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x0F, 0x0E, 0x0D, 0x0C,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC4, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x01, 0x00,
    0x1F, 0x00, 0x02, 0x00, 0x17, 0x00, 0x01, 0x00, 0x24, 0x00, 0x02, 0x00, 0x1B, 0x00, 0x03, 0x00,
    0x29, 0x00, 0x02, 0x00, 0x01, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x46, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x03, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x0C, 0x00, 0x13, 0x00, 0x56, 0x00, 0x1A, 0x00, 0x58, 0x00, 0x00, 0x00, 0x5A, 0x00,
    0x01, 0x00, 0x5E, 0x00, 0x03, 0x00, 0x01, 0x00, 0x40, 0x00, 0x05, 0x00, 0x6A, 0x00, 0x78, 0x00,
    0x04, 0x00, 0x60, 0x00, 0x00, 0x76, 0x30, 0x00, 0x76, 0x31, 0x00, 0x66, 0x6C, 0x61, 0x67, 0x00,
    0x68, 0x69, 0x64, 0x64, 0x65, 0x6E, 0x00, 0x4D, 0x61, 0x78, 0x56, 0x61, 0x6C, 0x00, 0x47, 0x72,
    0x65, 0x65, 0x74, 0x69, 0x6E, 0x67, 0x00, 0x00, 0x0F, 0xD0, 0x29, 0x88, 0xB8, 0x11, 0x13, 0x42,
    0x87, 0x8B, 0x77, 0x0E, 0x85, 0x97, 0xAC, 0x16, 0xF8, 0x62, 0x51, 0x3F, 0xC6, 0x07, 0xD3, 0x11,
    0x90, 0x53, 0x00, 0xC0, 0x4F, 0xA3, 0x02, 0xA1, 0xEC, 0x16, 0x18, 0xFF, 0x5D, 0xAA, 0x10, 0x4D,
    0xB8, 0x7F, 0xC1, 0xE2, 0xB0, 0xB5, 0xC0, 0xFD, 0x71, 0xDF, 0x9F, 0xED, 0x79, 0x88, 0x47, 0x47,
    0x8E, 0xD3, 0xFE, 0x5E, 0xDE, 0x3C, 0xE7, 0x10, 0xC4, 0x63, 0xC5, 0x83, 0xF3, 0xB4, 0xD5, 0x47,
    0xB8, 0x24, 0xBA, 0x54, 0x41, 0x47, 0x7E, 0xA8, 0x00, 0x02, 0x43, 0x3A, 0x04, 0x61, 0x2E, 0x63,
    0x73, 0x04, 0x62, 0x2E, 0x63, 0x73, 0x04, 0x63, 0x2E, 0x63, 0x73, 0x03, 0x5C, 0x01, 0x04, 0x03,
    0x5C, 0x01, 0x09, 0x03, 0x5C, 0x01, 0x0E, 0x04, 0x44, 0x31, 0x44, 0x31, 0x04, 0x44, 0x32, 0x44,
    0x32, 0x04, 0x44, 0x33, 0x44, 0x33, 0x17, 0x01, 0x00, 0x00, 0x04, 0x0A, 0x03, 0x05, 0x00, 0x00,
    0x00, 0x02, 0x05, 0x02, 0x79, 0x14, 0x04, 0x00, 0x01, 0x0A, 0x00, 0x04, 0x14, 0x06, 0x0F, 0x02,
    0x03, 0x00, 0x03, 0x02, 0x64, 0x02, 0x04, 0x00, 0x00, 0x05, 0x00, 0x06, 0x04, 0x04, 0x01, 0x08,
    0x01, 0x0E, 0x03, 0x11, 0x22, 0x33, 0x01, 0x44, 0x09, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0x61, 0x67,
    0x65, 0x00, 0x01, 0x07,
};

Md::PortablePdb LoadSyntheticPdb() {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        std::begin(kSyntheticPdb), std::end(kSyntheticPdb));
    return Md::PortablePdb(std::move(bytes));
}

// The Roslyn analyzer PDB shipped with the .NET SDK packs (a real portable
// PDB with multi-byte compressed document-name parts, 79 methods, and a
// local constant). The newest installed pack wins, like the CoreLib glob.
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
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::MethodDebugInformation), 2u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalScope), 5u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalVariable), 4u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::LocalConstant), 2u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::ImportScope), 2u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::StateMachineMethod), 1u);
    EXPECT_EQ(pdb.RowCount(Md::PdbTable::CustomDebugInformation), 2u);

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

    // The MethodDebugInformation table holds two rows; methods past it (the
    // state machine's MoveNext among them) carry nothing.
    EXPECT_TRUE(pdb.GetSequencePoints(3).empty());
    EXPECT_TRUE(pdb.GetSequencePoints(4).empty());
    EXPECT_EQ(pdb.GetLocalSignature(3), 0u);
    EXPECT_EQ(pdb.GetMethodDebugInformation(1).SequencePointsBlob != 0u, true);
    EXPECT_EQ(pdb.GetMethodDebugInformation(3).SequencePointsBlob, 0u);
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
    EXPECT_EQ(BlobString(pdb, row2.Value), std::string("name\0age\0", 9));

    EXPECT_EQ(pdb.GetCustomDebugInformation(3).ParentToken, 0u);
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
