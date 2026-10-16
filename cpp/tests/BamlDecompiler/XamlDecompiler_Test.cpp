// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the XamlDecompiler port (ICSharpCode.BamlDecompiler/
// XamlDecompiler.cs + BamlDecompilationResult.cs) -- every expectation is
// gold-pinned against the REAL public XamlDecompiler.Decompile driven
// end-to-end over the REAL BamlDecompilerTypeSystem (the real .NET Framework
// 4.8 mscorlib as the main module plus the GAC-resolving
// UniversalAssemblyResolver), by the gold probe
// C:/temp-probe/XamlDecompilerProbe:
//  * stream A: the ToolBar walk-document shape plus a second AssemblyInfo
//    row nothing resolves (the known-type id 0xFD63 resolving through the
//    KnownThings row, the PI-mapped probe namespace, no main-module element
//    so the XClass pass no-ops, the wrapper collapse) -- pinning the render,
//    the null TypeName, the two-entry AssemblyIdMap full-name Select, and
//    the empty GeneratedMembers.
//  * stream B: the main-module System.String shape -- the full XClass
//    rewrite through the pass chain (the direct-base rename into the
//    PI-mapped namespace with the auto-prefixed x:Class attribute) and the
//    first XClassNames entry becoming the result's TypeName.
//  * stream C: the empty document -- BamlNode.Parse's null root and the C#
//    `ctx.RootNode.Type` deref's NullReferenceException.
//  * the port-writer byte identity: the streams were built through the
//    REAL BamlWriter inside the probe, so the port's own BamlWriter output
//    over the mirrored fixture documents must match the dumped hex
//    byte-for-byte (both engines then decompile the identical bytes).
//  * the Settings property round trip (the C# get/set over the caller's
//    reference, null-tolerant like the C# ctor).

#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/Baml/BamlWriter.hpp"
#include "BamlDecompiler/XamlDecompiler.hpp"

#include "BamlTestSupport.hpp"
#include "BamlDecompiler/BamlDecompilerTypeSystem.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PEReaderParse.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "TestFixtures/PatchedNetModule.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace BDT = ILSpy::BamlDecompiler;
namespace MD = ILSpy::Decompiler::Metadata;

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
using ILSpy::BamlDecompiler::BamlDecompilationResult;
using ILSpy::BamlDecompiler::BamlDecompilerSettings;
using ILSpy::BamlDecompiler::XamlDecompiler;
using ILSpy::Tests::Baml::kMscorlibFullName;
using ILSpy::Tests::Baml::kPresentationFrameworkFullName;
using ILSpy::Tests::Baml::kProbePiNs;
using ILSpy::Tests::Baml::XamlContextFixture;

// Serializes a fixture document into BAML stream bytes the way the probe
// built its streams: the "MSBAML" signature, the 0.60 format versions, and
// the records in list order (the port's byte-identical BamlWriter).
std::vector<std::uint8_t> SerializeStream(Baml::BamlDocument document)
{
    document.Signature = "MSBAML";
    document.ReaderVersion = { 0, 0x60 };
    document.UpdaterVersion = { 0, 0x60 };
    document.WriterVersion = { 0, 0x60 };
    std::vector<std::uint8_t> bytes;
    Baml::WriteDocument(document, bytes);
    return bytes;
}

// Decodes the hex literal form the fixture dumps carry (the gold's
// streamA|hex entries).
std::vector<std::uint8_t> FromHex(const char* hex)
{
    std::vector<std::uint8_t> bytes;
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
        int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
        bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return bytes;
}

std::string ToHex(const std::vector<std::uint8_t>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (std::uint8_t byte : bytes) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0xf]);
    }
    return hex;
}

// The REAL BamlWriter's stream bytes for the probe's MakeDocA records (the
// gold probe's streamA|hex dump): the port's writer over the mirrored
// fixture document must reproduce them exactly.
constexpr const char* kStreamAHex =
    "0c0000004d005300420041004d004c00000060000000600000006000010000000000001b2e12687474703a2f2f70726f62652e70692f6e731753797374656d2e57696e646f77732e436f6e74726f6c7300001c5c00005850726573656e746174696f6e4672616d65776f726b2c2056657273696f6e3d342e302e302e302c2043756c747572653d6e65757472616c2c205075626c69634b6579546f6b656e3d333162663338353661643336346533351c4f01004b6d73636f726c69622c2056657273696f6e3d342e302e302e302c2043756c747572653d6e65757472616c2c205075626c69634b6579546f6b656e3d62373761356335363139333465303839200600000273301d24000000001e53797374656d2e57696e646f77732e436f6e74726f6c732e427574746f6e1f0c000063fd000557696474680363fd0010070568656c6c6f0402";

// The REAL BamlWriter's stream bytes for the probe's MakeDocB records (the
// gold probe's streamB|hex dump).
constexpr const char* kStreamBHex =
    "0c0000004d005300420041004d004c00000060000000600000006000010000000000001b1d12687474703a2f2f70726f62652e70692f6e730653797374656d00001c4f00004b6d73636f726c69622c2056657273696f6e3d342e302e302e302c2043756c747572653d6e65757472616c2c205075626c69634b6579546f6b656e3d623737613563353631393334653038391d13000000000d53797374656d2e537472696e670300000010070568656c6c6f0402";

// The REAL BamlWriter's stream bytes over an empty document (the gold
// probe's streamC|hex dump): just the signature block and the versions.
constexpr const char* kStreamCHex =
    "0c0000004d005300420041004d004c00000060000000600000006000";

// The stream-A document (the probe's MakeDocA): the walk-document shape with
// the second AssemblyInfo row inserted right after the first (the record
// order must match the gold bytes -- the hex comparison below pins it).
Baml::BamlDocument MakeStreamADocument(XamlContextFixture& fixture)
{
    Baml::BamlDocument document = fixture.MakeDocument();
    auto mscorlib = std::make_unique<Baml::AssemblyInfoRecord>();
    mscorlib->AssemblyId = 1;
    mscorlib->AssemblyFullName = kMscorlibFullName;
    // [0] DocumentStart, [1] PIMapping, [2] AssemblyInfo(0) -> insert here.
    document.Records.insert(document.Records.begin() + 3, std::move(mscorlib));
    return document;
}

// The stream-B document (the probe's MakeDocB): the main-module
// System.String shape -- the PI mapping over the System CLR namespace, the
// TypeInfo record the ElementStart resolves through the BAML-record arm,
// and the text content the TextHandler contributes.
Baml::BamlDocument MakeStreamBDocument()
{
    Baml::BamlDocument document;
    auto add = [&document](std::unique_ptr<Baml::BamlRecord> record) {
        document.Add(std::move(record));
    };

    add(std::make_unique<Baml::DocumentStartRecord>());

    auto pi = std::make_unique<Baml::PIMappingRecord>();
    pi->XmlNamespace = kProbePiNs;
    pi->ClrNamespace = "System";
    pi->AssemblyId = 0;
    add(std::move(pi));

    auto assembly = std::make_unique<Baml::AssemblyInfoRecord>();
    assembly->AssemblyId = 0;
    assembly->AssemblyFullName = kMscorlibFullName;
    add(std::move(assembly));

    auto type = std::make_unique<Baml::TypeInfoRecord>();
    type->TypeId = 0;
    type->AssemblyId = 0;
    type->TypeFullName = "System.String";
    add(std::move(type));

    auto element = std::make_unique<Baml::ElementStartRecord>();
    element->TypeId = 0;
    add(std::move(element));

    auto text = std::make_unique<Baml::TextRecord>();
    text->Value = "hello";
    add(std::move(text));

    add(std::make_unique<Baml::ElementEndRecord>());
    add(std::make_unique<Baml::DocumentEndRecord>());
    return document;
}

class XamlDecompilerTest : public ::testing::Test {
protected:
    XamlContextFixture fixture_;
};

TEST_F(XamlDecompilerTest, StreamAToolBarKnownTypeEndToEnd)
{
    // Gold streamA: the real engine's Decompile over the identical bytes.
    // The known-type id 0xFD63 resolves ToolBar through the KnownThings
    // row (a non-main-module type), the PI mapping attaches the probe
    // namespace, no main-module element exists so the XClass pass leaves
    // the document untouched, and the Document pass collapses the wrapper.
    const std::vector<std::uint8_t> bytes = SerializeStream(MakeStreamADocument(fixture_));
    XamlDecompiler decompiler(fixture_.Compilation());
    BamlDecompilationResult result = decompiler.Decompile(bytes.data(), bytes.size());
    EXPECT_EQ(result.Xaml()->ToString(), "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
    EXPECT_FALSE(result.TypeName().has_value());
    ASSERT_EQ(result.AssemblyReferences().size(), 2u);
    EXPECT_EQ(result.AssemblyReferences()[0], kPresentationFrameworkFullName);
    EXPECT_EQ(result.AssemblyReferences()[1], kMscorlibFullName);
    EXPECT_TRUE(result.GeneratedMembers().empty());
}

TEST_F(XamlDecompilerTest, StreamAPortWriterBytesMatchTheRealWriter)
{
    EXPECT_EQ(ToHex(SerializeStream(MakeStreamADocument(fixture_))), kStreamAHex);
}

TEST_F(XamlDecompilerTest, StreamBMainModuleStringRewritesXClass)
{
    // Gold streamB: the XClass pass rewrites the main-module String
    // element to its direct base type's namespaced name -- the PI mapping
    // matches the base type's (System, mscorlib) pair too, so the rename
    // lands in the PI-mapped probe namespace rather than the clr-namespace
    // fallback, with the auto-prefixed x:Class attribute carrying the
    // ORIGINAL type's full name -- and the first XClassNames entry
    // becomes the result's TypeName.
    fixture_.UseRealMscorlibMainFullName();
    // ConfigureRewriteStubs: the String stub's Object base the rename
    // resolves through (the iteration-58 B12 fixture shape).
    (void)fixture_.ObjectType();
    const std::vector<std::uint8_t> bytes = SerializeStream(MakeStreamBDocument());
    XamlDecompiler decompiler(fixture_.Compilation());
    BamlDecompilationResult result = decompiler.Decompile(bytes.data(), bytes.size());
    EXPECT_EQ(result.Xaml()->ToString(),
        "<Object p1:Class=\"System.String\" xmlns:p1=\"http://schemas.microsoft.com/winfx/2006/xaml\""
        " xmlns=\"http://probe.pi/ns\">hello</Object>");
    ASSERT_TRUE(result.TypeName().has_value());
    EXPECT_EQ(result.TypeName()->ReflectionName(), "System.String");
    EXPECT_EQ(result.TypeName()->FullName(), "System.String");
    EXPECT_EQ(result.TypeName()->Name(), "String");
    ASSERT_EQ(result.AssemblyReferences().size(), 1u);
    EXPECT_EQ(result.AssemblyReferences()[0], kMscorlibFullName);
    EXPECT_TRUE(result.GeneratedMembers().empty());
}

TEST_F(XamlDecompilerTest, StreamBPortWriterBytesMatchTheRealWriter)
{
    EXPECT_EQ(ToHex(SerializeStream(MakeStreamBDocument())), kStreamBHex);
}

TEST_F(XamlDecompilerTest, StreamCEmptyDocumentThrowsNullReference)
{
    // Gold streamC: zero records -> BamlNode.Parse returns null -> the C#
    // `ctx.RootNode.Type` deref is the NullReferenceException; the port
    // throws the same message from its explicit null guard.
    const std::vector<std::uint8_t> bytes = SerializeStream(Baml::BamlDocument());
    EXPECT_EQ(ToHex(bytes), kStreamCHex);
    XamlDecompiler decompiler(fixture_.Compilation());
    try {
        (void)decompiler.Decompile(bytes.data(), bytes.size());
        FAIL() << "expected the null-RootNode NullReferenceException";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

TEST_F(XamlDecompilerTest, SettingsPropertyRoundTrip)
{
    // The C# `BamlDecompilerSettings Settings { get; set; }`: the ctor
    // keeps the caller's (possibly null) reference and Decompile passes it
    // through to XamlContext.Construct, whose null arm builds the default.
    BamlDecompilerSettings settings;
    XamlDecompiler decompiler(fixture_.Compilation());
    EXPECT_EQ(decompiler.Settings(), nullptr);
    decompiler.SetSettings(&settings);
    EXPECT_EQ(decompiler.Settings(), &settings);
}

// ---- The ctor family -------------------------------------------------
//
// The C# constructor family over the real engine's CreateTypeSystemFromFile
// chain: the four ctors plus the LoadPEFile failure arms, every expectation
// dumped from the real ilspycmd 11.0 engine (the probe's ctor-family section:
// C1-C4 decompile the identical streams byte-identically; F1-F12 pin the
// failure matrix).

// The .NET Framework 4.x mscorlib the ctor family drives (the repo-wide
// ILSPY_TEST_MSCORLIB convention; the golds only require a mscorlib with
// the 4.0.0.0 full name -- System.String and the standard resource
// surface -- which the mono 4.5-profile mscorlib also satisfies).
std::string FxMscorlibPath()
{
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// Writes `bytes` to a temp file with the given stem and returns the path.
std::string WriteBytesFile(const std::string& stem, const std::string& bytes)
{
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path()
        / ("ilspy_ctorfamily_" + stem + ".bin");
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    if (!bytes.empty())
        std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

// The gold renders (the probe's streamA/streamB drives -- identical over
// C1-C4, the real engine over the real mscorlib type system). Stream B
// needs the main-module full-name shape the stub fixture carries
// (UseRealMscorlibMainFullName): the XClass pass renames through the
// main-module System.String type.
TEST(XamlDecompilerCtorFamily, FileNameSettingsCtorDecompilesTheGoldStreams)
{
#if !defined(_WIN32)
    // The ctor family's file-name+settings form builds its resolver with
    // ThrowOnAssemblyResolveErrors=true (the C# default), so the default
    // BAML references must actually resolve: on Windows the GAC serves
    // the WPF assemblies. A POSIX host has neither the GAC nor the
    // PresentationCore/PresentationFramework set (the mono profiles ship
    // only WindowsBase and System.Xaml), so the unresolved reference
    // throws and this drive needs the Windows host.
    GTEST_SKIP() << "needs the Windows GAC for the WPF default references";
#else
    const std::string mscorlib = FxMscorlibPath();
    if (!std::filesystem::exists(mscorlib)) {
        GTEST_SKIP() << "mscorlib fixture " << mscorlib
                     << " not present on this host";
    }
    BamlDecompilerSettings settings;
    BDT::XamlDecompiler decompiler(mscorlib, settings);

    const std::vector<std::uint8_t> streamA = FromHex(kStreamAHex);
    BamlDecompilationResult resultA =
        decompiler.Decompile(streamA.data(), streamA.size());
    EXPECT_EQ(resultA.Xaml()->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
    EXPECT_FALSE(resultA.TypeName().has_value());
    ASSERT_EQ(resultA.AssemblyReferences().size(), 2u);
    EXPECT_EQ(resultA.AssemblyReferences()[0], kPresentationFrameworkFullName);
    EXPECT_EQ(resultA.AssemblyReferences()[1], kMscorlibFullName);
    EXPECT_TRUE(resultA.GeneratedMembers().empty());

    const std::vector<std::uint8_t> streamB = FromHex(kStreamBHex);
    BamlDecompilationResult resultB =
        decompiler.Decompile(streamB.data(), streamB.size());
    EXPECT_EQ(resultB.Xaml()->ToString(),
        "<Object p1:Class=\"System.String\" xmlns:p1=\"http://schemas.microsoft.com/winfx/2006/xaml\""
        " xmlns=\"http://probe.pi/ns\">hello</Object>");
    ASSERT_TRUE(resultB.TypeName().has_value());
    EXPECT_EQ(resultB.TypeName()->FullName(), "System.String");
    ASSERT_EQ(resultB.AssemblyReferences().size(), 1u);
    EXPECT_EQ(resultB.AssemblyReferences()[0], kMscorlibFullName);
#endif
}

TEST(XamlDecompilerCtorFamily, FileNameResolverCtorDecompilesTheGoldStreams)
{
    const std::string mscorlib = FxMscorlibPath();
    if (!std::filesystem::exists(mscorlib)) {
        GTEST_SKIP() << "mscorlib fixture " << mscorlib
                     << " not present on this host";
    }
    const std::vector<std::uint8_t> streamA = FromHex(kStreamAHex);
    const std::vector<std::uint8_t> streamB = FromHex(kStreamBHex);

    MD::MetadataFile file(mscorlib);
    ASSERT_TRUE(file.IsValid());
    MD::UniversalAssemblyResolver resolver(mscorlib, false,
        MD::DetectTargetFrameworkId(file), std::nullopt,
        MD::PEStreamOptions::Default, MD::MetadataReaderOptions::Default);
    BamlDecompilerSettings settings;
    BDT::XamlDecompiler decompiler(mscorlib, resolver, &settings);

    BamlDecompilationResult resultA =
        decompiler.Decompile(streamA.data(), streamA.size());
    EXPECT_EQ(resultA.Xaml()->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
    BamlDecompilationResult resultB =
        decompiler.Decompile(streamB.data(), streamB.size());
    EXPECT_EQ(resultB.Xaml()->ToString(),
        "<Object p1:Class=\"System.String\" xmlns:p1=\"http://schemas.microsoft.com/winfx/2006/xaml\""
        " xmlns=\"http://probe.pi/ns\">hello</Object>");
    ASSERT_TRUE(resultB.TypeName().has_value());
    EXPECT_EQ(resultB.TypeName()->FullName(), "System.String");
}

TEST(XamlDecompilerCtorFamily, FileResolverCtorDecompilesTheGoldStreams)
{
    const std::string mscorlib = FxMscorlibPath();
    if (!std::filesystem::exists(mscorlib)) {
        GTEST_SKIP() << "mscorlib fixture " << mscorlib
                     << " not present on this host";
    }
    const std::vector<std::uint8_t> streamA = FromHex(kStreamAHex);
    MD::MetadataFile file(mscorlib);
    ASSERT_TRUE(file.IsValid());
    MD::UniversalAssemblyResolver resolver(mscorlib, false,
        MD::DetectTargetFrameworkId(file), std::nullopt,
        MD::PEStreamOptions::Default, MD::MetadataReaderOptions::Default);
    BDT::XamlDecompiler decompiler(file, resolver, nullptr);
    BamlDecompilationResult resultA =
        decompiler.Decompile(streamA.data(), streamA.size());
    EXPECT_EQ(resultA.Xaml()->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
}

TEST(XamlDecompilerCtorFamily, TypeSystemCtorDecompilesTheGoldStreams)
{
    const std::string mscorlib = FxMscorlibPath();
    if (!std::filesystem::exists(mscorlib)) {
        GTEST_SKIP() << "mscorlib fixture " << mscorlib
                     << " not present on this host";
    }
    MD::MetadataFile file(mscorlib);
    ASSERT_TRUE(file.IsValid());
    MD::UniversalAssemblyResolver resolver(mscorlib, false,
        MD::DetectTargetFrameworkId(file), std::nullopt,
        MD::PEStreamOptions::Default, MD::MetadataReaderOptions::Default);
    BDT::BamlDecompilerTypeSystem typeSystem(file, resolver);
    BDT::XamlDecompiler decompiler(typeSystem, nullptr);
    const std::vector<std::uint8_t> streamA = FromHex(kStreamAHex);
    BamlDecompilationResult resultA =
        decompiler.Decompile(streamA.data(), streamA.size());
    EXPECT_EQ(resultA.Xaml()->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
}

// The LoadPEFile failure arms over crafted byte patches of the netmodule
// fixture (the exact .NET messages the real engine throws -- the probe's
// F1-F12 matrix).
TEST(XamlDecompilerCtorFamily, MissingFileThrowsFileNotFoundException)
{
    // F6: the parent directory exists, the file does not. The .NET message
    // names the path as given, and whether the parent exists is a property
    // of the host's real file system -- so the drive uses a Windows path on
    // Windows and a POSIX temp-directory path elsewhere (the probe's C:\
    // fixture only has an existing parent on a Windows host).
#if defined(_WIN32)
    const std::string missing =
        "C:\\temp-probe\\XamlDecompilerProbe\\definitely_missing_xamldec.dll";
#else
    const std::string missing = (std::filesystem::temp_directory_path()
            / "definitely_missing_xamldec.dll")
        .string();
#endif
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(missing, settings);
        FAIL() << "expected the FileNotFoundException";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(),
            ("Could not find file '" + missing + "'.").c_str());
    }
}

TEST(XamlDecompilerCtorFamily, MissingParentThrowsDirectoryNotFoundException)
{
    // F1: the parent directory does not exist either.
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            "C:\\temp-probe\\nonexistent_dir\\missing.dll", settings);
        FAIL() << "expected the DirectoryNotFoundException";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(),
            "Could not find a part of the path 'C:\\temp-probe\\nonexistent_dir\\missing.dll'.");
    }
}

TEST(XamlDecompilerCtorFamily, ShortImageThrowsImageTooSmallOrInvalid)
{
    // F3: an 8-byte MZ image -- the e_lfanew read's `Offset` setter
    // CheckBounds variant.
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f3", std::string(TinyNetModuleBytes(), 0, 8)),
            settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Image is either too small or contains an invalid byte offset or count.");
    }
}

TEST(XamlDecompilerCtorFamily, OutOfRangeLfanewThrowsImageTooSmallOrInvalid)
{
    // F8: an MZ header whose e_lfanew is way past the end.
    std::string bytes = TinyNetModuleBytes();
    ILSpy::Tests::Wr32(bytes, 0x3C, 0x7FFFFFF0u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f8", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Image is either too small or contains an invalid byte offset or count.");
    }
}

TEST(XamlDecompilerCtorFamily, InvalidPeSignatureThrows)
{
    // F9: an MZ header whose e_lfanew points at non-PE bytes.
    std::string bytes = TinyNetModuleBytes();
    bytes[0x80] = '\x11';
    bytes[0x81] = '\x22';
    bytes[0x82] = '\x00';
    bytes[0x83] = '\x00';
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f9", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Invalid PE signature.");
    }
}

TEST(XamlDecompilerCtorFamily, ZeroFileParsesAsCoffOnlyAndThrowsNoMetadata)
{
    // F7: an all-zero 512-byte file -- the COFF-only path with no metadata.
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f7", std::string(512, '\0')), settings);
        FAIL() << "expected the MetadataFileNotSupportedException";
    } catch (const MD::MetadataFileNotSupportedException& ex) {
        EXPECT_STREQ(ex.what(),
            "PE file does not contain any managed metadata.");
    }
}

TEST(XamlDecompilerCtorFamily, NativePeThrowsNoMetadata)
{
    // F4: a valid PE with no CLI directory -- the cor directory entry
    // zeroed out of the netmodule fixture.
    std::string bytes = TinyNetModuleBytes();
    // The optional-header data directories sit at fileHeader + 20 + 96 (PE32);
    // index 14 is the cor header table.
    ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8, 0u);
    ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8 + 4, 0u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f4", bytes), settings);
        FAIL() << "expected the MetadataFileNotSupportedException";
    } catch (const MD::MetadataFileNotSupportedException& ex) {
        EXPECT_STREQ(ex.what(),
            "PE file does not contain any managed metadata.");
    }
}

TEST(XamlDecompilerCtorFamily, CorruptMetadataThrowsOverflow)
{
    // F10: a valid CLI image whose metadata root's stream count is garbage
    // -- the real engine's OverflowException inside GetMetadataReader.
    std::string bytes = TinyNetModuleBytes();
    // The metadata root sits at the cor20 header's MetaData RVA (file
    // offset 0x260); the streams-count field is at root + 0x1C + 2 (after
    // the version string and the flags pair).
    bytes[0x27E] = static_cast<char>(0xFF);
    bytes[0x27F] = static_cast<char>(0xFF);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("f10", bytes), settings);
        FAIL() << "expected the OverflowException";
    } catch (const std::overflow_error& ex) {
        EXPECT_STREQ(ex.what(),
            "Arithmetic operation resulted in an overflow.");
    }
}

TEST(XamlDecompilerCtorFamily, EmptyAndOneByteFilesThrowImageTooSmall)
{
    // F11/F12: the first ReadUInt16 CheckBounds arm.
    for (const std::string& stem : {"f11", "f12"}) {
        const std::string bytes = stem == "f11" ? std::string() : std::string("M", 1);
        try {
            BamlDecompilerSettings settings;
            BDT::XamlDecompiler decompiler(
                WriteBytesFile(stem, bytes), settings);
            FAIL() << "expected the BadImageFormatException";
        } catch (const std::invalid_argument& ex) {
            EXPECT_STREQ(ex.what(), "Image is too small.");
        }
    }
}

TEST(XamlDecompilerCtorFamily, UnknownPeMagicThrows)
{
    // A valid PE signature whose optional-header magic is neither PE32 nor
    // PE32+ (the PEHeader ctor's UnknownPEMagicValue arm).
    std::string bytes = TinyNetModuleBytes();
    bytes[0x98] = static_cast<char>(0x33);
    bytes[0x99] = static_cast<char>(0x33);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("magic", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Unknown PE Magic value.");
    }
}

TEST(XamlDecompilerCtorFamily, NegativeSectionCountThrows)
{
    // A COFF NumberOfSections of -1 (the count-bound arm).
    std::string bytes = TinyNetModuleBytes();
    bytes[0x86] = static_cast<char>(0xFF);
    bytes[0x87] = static_cast<char>(0xFF);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("sections", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid number of sections declared in PE header.");
    }
}

TEST(XamlDecompilerCtorFamily, SmallCorDirectoryThrows)
{
    // A cor directory entry smaller than the 72-byte cor header.
    std::string bytes = TinyNetModuleBytes();
    // The optional-header data directories: fileHeader(0x84) + 20 + 96;
    // index 14's size field is the second int.
    ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8 + 4, 71u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("corsize", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Invalid COR header size.");
    }
}

TEST(XamlDecompilerCtorFamily, OversizedCorDirectoryThrowsSectionTooSmall)
{
    // A cor directory entry whose size crosses its containing section's
    // virtual extent (the canCrossSectionBoundary: false arm).
    std::string bytes = TinyNetModuleBytes();
    ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8 + 4, 0x214u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("corsect", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Section too small.");
    }
}

TEST(XamlDecompilerCtorFamily, UnresolvableMetadataDirectoryThrowsMissingDataDirectory)
{
    // A cor header whose own metadata directory does not resolve into any
    // section.
    std::string bytes = TinyNetModuleBytes();
    // The cor20 header's MetadataDirectory RVA field at cor+8 (the cor
    // header sits at the data-directory RVA 0x2008 -> file 0x208).
    ILSpy::Tests::Wr32(bytes, 0x208 + 8, 0x3000u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("metadir", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Missing data directory.");
    }
}

TEST(XamlDecompilerCtorFamily, EmptyMetadataDirectoryThrowsInvalidMetadataSectionSpan)
{
    // A cor header whose metadata directory has size 0 (the span check's
    // `size <= 0` arm).
    std::string bytes = TinyNetModuleBytes();
    ILSpy::Tests::Wr32(bytes, 0x208 + 8 + 4, 0u);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("metaspan", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Invalid metadata section span.");
    }
}

TEST(XamlDecompilerCtorFamily, ZeroThenFfffImageThrowsUnknownFileFormat)
{
    // A first uint16 of 0 and a next uint16 of 0xFFFF (the
    // SkipDosHeader's UnknownFileFormat arm).
    const std::string bytes = std::string("\x00\x00\xFF\xFF", 4);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("zeroFFFF", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Unknown file format.");
    }
}

TEST(XamlDecompilerCtorFamily, CoffOnlyWithOptionalHeaderThrowsUnknownFileFormat)
{
    // A COFF-only image (a non-MZ first uint16) whose SizeOfOptionalHeader
    // is nonzero (the COFF-only arm's UnknownFileFormat rejection).
    std::string bytes;
    bytes.resize(24, '\0');
    bytes[0] = 'A';
    bytes[1] = 'A';
    // The COFF header parses at offset 0: machine(2) sections(2) stamp(4)
    // symbolPtr(4) symbols(4) sizeOfOptionalHeader(2)@16 characteristics(2)@18.
    bytes[16] = static_cast<char>(0x10);
    try {
        BamlDecompilerSettings settings;
        BDT::XamlDecompiler decompiler(
            WriteBytesFile("coffonly", bytes), settings);
        FAIL() << "expected the BadImageFormatException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Unknown file format.");
    }
}

// The PEReaderParse unit smoke over the real fixtures: the netmodule parses
// (its metadata block), mscorlib parses, and the native-PE shape answers
// no metadata.
TEST(XamlDecompilerCtorFamily, PEReaderParseAnswersTheMetadataBlock)
{
    {
        const std::string netmodule = TinyNetModuleBytes();
        MD::MetadataBlock block = MD::ParsePEReaderHeaders(
            std::vector<std::uint8_t>(netmodule.begin(), netmodule.end()));
        EXPECT_GT(block.size, 0u);
    }
    {
        const std::string mscorlib = FxMscorlibPath();
        if (!std::filesystem::exists(mscorlib)) {
            GTEST_SKIP() << "mscorlib fixture " << mscorlib
                         << " not present on this host";
        }
        std::ifstream input(mscorlib, std::ios::binary);
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        ASSERT_GT(bytes.size(), 0u);
        MD::MetadataBlock block = MD::ParsePEReaderHeaders(bytes);
        EXPECT_GT(block.size, 0u);
    }
    {
        std::string bytes = TinyNetModuleBytes();
        ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8, 0u);
        ILSpy::Tests::Wr32(bytes, 0x84 + 20 + 96 + 14 * 8 + 4, 0u);
        MD::MetadataBlock block = MD::ParsePEReaderHeaders(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        EXPECT_EQ(block.size, 0u);
    }
}

} // namespace
