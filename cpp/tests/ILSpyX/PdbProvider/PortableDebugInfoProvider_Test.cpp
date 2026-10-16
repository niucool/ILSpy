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

// Tests for the PortableDebugInfoProvider (cpp/ILSpyX/PdbProvider/), the
// IDebugInfoProvider implementation over the portable-PDB debug-table reader.
//
// Every expected value over the synthetic fixture was dumped from the REAL
// ICSharpCode.ILSpyX PortableDebugInfoProvider (the installed ilspycmd 11.0
// tool's ICSharpCode.ILSpyX.dll, driven over the identical fixture bytes by
// the SDK-10 probe): the Description/SourceFileName/IsEmbedded shapes, the
// sequence-point and variable lists, the name lookups, and the tuple/dynamic
// CustomDebugInformation decodes -- including the truncated-blob catch arm
// (the raw SRM enumeration throws BadImageFormatException, the provider
// returns the empty list) and the nil-token whole-table local-scope quirk
// (the C# LocalScopeHandleCollection maps a nil method row to the whole
// table). The Roslyn-analyzer fixture adds a real-world PDB (glob-gated).

#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"
#include "TestFixtures/SyntheticPortablePdb.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace DI = ILSpy::Decompiler::DebugInfo;
namespace Pdb = ILSpy::ILSpyX::PdbProvider;

namespace {

// A provider over the synthetic fixture, in the embedded shape.
Pdb::PortableDebugInfoProvider EmbeddedProvider() {
    return Pdb::PortableDebugInfoProvider("Module.dll", ILSpy::Tests::LoadSyntheticPdb());
}

// A provider over bytes that do not parse as a portable PDB -- the state
// whose reads all fail and whose Description reports the load error.
Pdb::PortableDebugInfoProvider InvalidProvider(const std::string& pdbFileName) {
    std::optional<std::string> name;
    if (!pdbFileName.empty()) name = pdbFileName;
    return Pdb::PortableDebugInfoProvider("Module.dll",
        ILSpy::Decompiler::Metadata::PortablePdb(
            std::make_shared<std::vector<std::uint8_t>>(64, 0xAB)),
        std::move(name));
}

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

std::string JoinVariables(const std::vector<DI::Variable>& variables) {
    std::string result;
    for (const auto& v : variables) {
        if (!result.empty()) result += ' ';
        result += "{index=" + std::to_string(v.Index) + ",name='" + v.Name + "'}";
    }
    return result;
}

}  // namespace

TEST(PortableDebugInfoProviderTest, DescriptionAndSourceFileNameShapes) {
    // The embedded shape: no pdbFileName.
    Pdb::PortableDebugInfoProvider embedded = EmbeddedProvider();
    EXPECT_TRUE(embedded.IsEmbedded());
    EXPECT_EQ(embedded.Description(), "Embedded in this assembly");
    EXPECT_EQ(embedded.SourceFileName(), "Module.dll");

    // The file-backed shape.
    Pdb::PortableDebugInfoProvider fileBacked("Module.dll",
        ILSpy::Tests::LoadSyntheticPdb(), std::string("C:\\x\\synthetic.pdb"));
    EXPECT_FALSE(fileBacked.IsEmbedded());
    EXPECT_EQ(fileBacked.Description(),
              "Loaded from portable PDB: C:\\x\\synthetic.pdb");
    EXPECT_EQ(fileBacked.SourceFileName(), "C:\\x\\synthetic.pdb");
}

TEST(PortableDebugInfoProviderTest, ErrorArmDegradesReadsAndReportsInDescription) {
    // Before any read, the error state is not reported (the C# hasError is
    // flipped by the reads, not the constructor).
    Pdb::PortableDebugInfoProvider embedded = InvalidProvider("");
    EXPECT_EQ(embedded.Description(), "Embedded in this assembly");
    EXPECT_TRUE(embedded.GetSequencePoints(0x06000001).empty());
    EXPECT_TRUE(embedded.GetVariables(0x06000001).empty());
    std::string name = "stale";
    EXPECT_FALSE(embedded.TryGetName(0x06000001, 0, name));
    EXPECT_TRUE(name.empty());
    DI::PdbExtraTypeInfo extra;
    EXPECT_FALSE(embedded.TryGetExtraTypeInfo(0x06000001, 0, extra));
    EXPECT_FALSE(extra.TupleElementNames.has_value());
    EXPECT_FALSE(extra.DynamicFlags.has_value());
    EXPECT_EQ(embedded.Description(),
              "Error while loading the PDB stream embedded in this assembly");

    // The file-backed error arm keeps reporting the PDB file name.
    Pdb::PortableDebugInfoProvider fileBacked =
        InvalidProvider("C:\\some\\bad.pdb");
    EXPECT_TRUE(fileBacked.GetVariables(0x06000001).empty());
    EXPECT_EQ(fileBacked.Description(),
              "Error while loading portable PDB: C:\\some\\bad.pdb");
    EXPECT_EQ(fileBacked.SourceFileName(), "C:\\some\\bad.pdb");
}

TEST(PortableDebugInfoProviderTest, GetSequencePointsMapsDocumentNames) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    std::vector<DI::SequencePoint> points = provider.GetSequencePoints(0x06000001);
    ASSERT_EQ(points.size(), 4u);

    EXPECT_EQ(points[0].Offset, 0);
    EXPECT_EQ(points[0].StartLine, 10);
    EXPECT_EQ(points[0].StartColumn, 3);
    EXPECT_EQ(points[0].EndLine, 10);
    EXPECT_EQ(points[0].EndColumn, 7);
    EXPECT_EQ(points[0].DocumentUrl, "C:\\a.cs");
    EXPECT_FALSE(points[0].IsHidden());

    EXPECT_TRUE(points[1].IsHidden());
    EXPECT_EQ(points[1].Offset, 5);
    EXPECT_EQ(points[1].DocumentUrl, "C:\\a.cs");

    EXPECT_EQ(points[2].Offset, 10);
    EXPECT_EQ(points[2].StartLine, 20);
    EXPECT_EQ(points[2].StartColumn, 5);
    EXPECT_EQ(points[2].EndLine, 22);
    EXPECT_EQ(points[2].EndColumn, 1);
    EXPECT_EQ(points[2].DocumentUrl, "C:\\b.cs");

    EXPECT_EQ(points[3].Offset, 20);
    EXPECT_EQ(points[3].StartLine, 30);
    EXPECT_EQ(points[3].StartColumn, 8);
    EXPECT_EQ(points[3].EndLine, 30);
    EXPECT_EQ(points[3].EndColumn, 12);
    EXPECT_EQ(points[3].DocumentUrl, "C:\\a.cs");
}

TEST(PortableDebugInfoProviderTest, GetSequencePointsOfTruncatedBlobIsEmpty) {
    // Method 3's sequence-point blob is cut off mid-point: the raw reader
    // throws, and the provider's catch turns that into the empty list (the
    // real C# returns the same empty list through its
    // BadImageFormatException catch).
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    EXPECT_TRUE(provider.GetSequencePoints(0x06000003).empty());
}

TEST(PortableDebugInfoProviderTest, GetSequencePointsDegradationShapes) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    // Method 4 has no debug information; methods past the referenced table
    // and the nil token have none either.
    EXPECT_TRUE(provider.GetSequencePoints(0x06000004).empty());
    EXPECT_TRUE(provider.GetSequencePoints(0x0600270F).empty());
    EXPECT_TRUE(provider.GetSequencePoints(0).empty());
}

TEST(PortableDebugInfoProviderTest, GetVariablesWalksTheMethodScopes) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    EXPECT_EQ(JoinVariables(provider.GetVariables(0x06000001)),
              "{index=0,name='v0'} {index=1,name='v1'} {index=2,name='flag'}");
    EXPECT_TRUE(provider.GetVariables(0x06000002).empty());
    EXPECT_TRUE(provider.GetVariables(0x06000003).empty());
    EXPECT_EQ(JoinVariables(provider.GetVariables(0x06000004)),
              "{index=5,name='hidden'}");
    // The C# LocalScopeHandleCollection maps the nil method row to the
    // whole LocalScope table.
    EXPECT_EQ(JoinVariables(provider.GetVariables(0)),
              "{index=0,name='v0'} {index=1,name='v1'} {index=2,name='flag'} "
              "{index=5,name='hidden'}");
}

TEST(PortableDebugInfoProviderTest, TryGetNameFindsTheLocalAtTheSlotIndex) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    std::string name;
    ASSERT_TRUE(provider.TryGetName(0x06000001, 0, name));
    EXPECT_EQ(name, "v0");
    ASSERT_TRUE(provider.TryGetName(0x06000001, 1, name));
    EXPECT_EQ(name, "v1");
    ASSERT_TRUE(provider.TryGetName(0x06000001, 2, name));
    EXPECT_EQ(name, "flag");
    ASSERT_TRUE(provider.TryGetName(0x06000004, 5, name));
    EXPECT_EQ(name, "hidden");
    // The nil-token whole-table walk finds the first local at the index.
    ASSERT_TRUE(provider.TryGetName(0, 0, name));
    EXPECT_EQ(name, "v0");
    ASSERT_TRUE(provider.TryGetName(0, 5, name));
    EXPECT_EQ(name, "hidden");

    EXPECT_FALSE(provider.TryGetName(0x06000001, 5, name));
    EXPECT_TRUE(name.empty());
    EXPECT_FALSE(provider.TryGetName(0x06000002, 0, name));
    EXPECT_FALSE(provider.TryGetName(0x0600270F, 0, name));
}

TEST(PortableDebugInfoProviderTest, TryGetExtraTypeInfoDecodesTupleElementNames) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    DI::PdbExtraTypeInfo extra;
    ASSERT_TRUE(provider.TryGetExtraTypeInfo(0x06000001, 2, extra));
    ASSERT_TRUE(extra.TupleElementNames.has_value());
    ASSERT_EQ(extra.TupleElementNames->size(), 3u);
    EXPECT_EQ((*extra.TupleElementNames)[0], "name");
    // The whitespace-only part is the C# null entry (the unnamed tuple
    // element marker), carried as the empty string.
    EXPECT_EQ((*extra.TupleElementNames)[1], "");
    EXPECT_EQ((*extra.TupleElementNames)[2], "age");
    EXPECT_FALSE(extra.DynamicFlags.has_value());
}

TEST(PortableDebugInfoProviderTest, TryGetExtraTypeInfoDecodesDynamicFlags) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    DI::PdbExtraTypeInfo extra;
    ASSERT_TRUE(provider.TryGetExtraTypeInfo(0x06000004, 5, extra));
    ASSERT_TRUE(extra.DynamicFlags.has_value());
    ASSERT_EQ(extra.DynamicFlags->size(), 16u);
    const bool expected[] = { true, false, true, false, false, false, false,
        false, false, false, false, false, false, false, false, true };
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ((*extra.DynamicFlags)[i], expected[i]) << "flag " << i;
    }
    EXPECT_FALSE(extra.TupleElementNames.has_value());
}

TEST(PortableDebugInfoProviderTest, TryGetExtraTypeInfoRejectsOtherParents) {
    Pdb::PortableDebugInfoProvider provider = EmbeddedProvider();
    DI::PdbExtraTypeInfo extra;
    // v0 and v1 carry no CustomDebugInformation rows; the DynamicLocalVariables
    // row parented to a MethodDef never matches a local.
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x06000001, 0, extra));
    EXPECT_FALSE(extra.TupleElementNames.has_value());
    EXPECT_FALSE(extra.DynamicFlags.has_value());
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x06000001, 1, extra));
    // No local 5 in method 1; method 2's locals are none; the nil-token walk
    // finds v0 but no row is parented to it.
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x06000001, 5, extra));
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x06000002, 0, extra));
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0, 0, extra));
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x0600270F, 0, extra));
}

TEST(PortableDebugInfoProviderTest, LoadsARealRoslynPortablePdb) {
    std::string path = AnalyzerPdbPath();
    if (path.empty())
        GTEST_SKIP() << "no .NET SDK WindowsDesktop.App.Ref analyzer PDB installed";

    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr)
            GTEST_SKIP() << "analyzer PDB unreadable";
        std::uint8_t buffer[8192];
        std::size_t read;
        while ((read = std::fread(buffer, 1, sizeof(buffer), f)) != 0)
            bytes->insert(bytes->end(), buffer, buffer + read);
        std::fclose(f);
    }
    ILSpy::Decompiler::Metadata::PortablePdb pdb(std::move(bytes));
    ASSERT_TRUE(pdb.IsValid());
    // The reader is copied into the provider (a shared-pointer copy); the
    // raw decodes below stay available for the cross-validation sweep.
    Pdb::PortableDebugInfoProvider provider(
        "System.Windows.Forms.Analyzers.CSharp.dll", pdb, path);

    EXPECT_FALSE(provider.IsEmbedded());
    EXPECT_EQ(provider.Description(), "Loaded from portable PDB: " + path);
    EXPECT_EQ(provider.SourceFileName(), path);

    // Method 6 has one sequence point on the SR resources document.
    std::vector<DI::SequencePoint> points = provider.GetSequencePoints(0x06000006);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0].Offset, 0);
    EXPECT_EQ(points[0].StartLine, 10);
    EXPECT_EQ(points[0].StartColumn, 85);
    EXPECT_EQ(points[0].EndLine, 10);
    EXPECT_EQ(points[0].EndColumn, 184);
    EXPECT_EQ(points[0].DocumentUrl,
              "/_/src/winforms/artifacts/obj/System.Windows.Forms.Analyzers.CSharp/"
              "Release/netstandard2.0/System.Windows.Forms.Analyzers.CSharp.Resources.SR.cs");

    // Method 35's outer scope holds the "code" local.
    std::vector<DI::Variable> variables = provider.GetVariables(0x06000023);
    ASSERT_EQ(JoinVariables(variables), "{index=0,name='code'}");
    std::string name;
    ASSERT_TRUE(provider.TryGetName(0x06000023, 0, name));
    EXPECT_EQ(name, "code");

    // The analyzer PDB carries no tuple/dynamic rows: every local reports
    // no extra type info.
    DI::PdbExtraTypeInfo extra;
    EXPECT_FALSE(provider.TryGetExtraTypeInfo(0x06000023, 0, extra));

    // A full no-throw sweep over every method: the provider's sequence
    // points agree with the raw reader's decode, and every variable the
    // provider reports is found by a name lookup.
    for (std::uint32_t methodRow = 1; methodRow <= 79; ++methodRow) {
        std::uint32_t token = 0x06000000u | methodRow;
        std::vector<DI::SequencePoint> got = provider.GetSequencePoints(token);
        std::vector<ILSpy::Decompiler::Metadata::PdbSequencePoint> raw =
            pdb.GetSequencePoints(methodRow);
        ASSERT_EQ(got.size(), raw.size()) << "method " << methodRow;
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_EQ(got[i].Offset, static_cast<int>(raw[i].Offset));
            EXPECT_EQ(got[i].StartLine, raw[i].StartLine);
            EXPECT_EQ(got[i].EndLine, raw[i].EndLine);
        }
        for (const DI::Variable& v : provider.GetVariables(token)) {
            std::string localName;
            EXPECT_TRUE(provider.TryGetName(token, v.Index, localName))
                << "method " << methodRow;
        }
    }
}
