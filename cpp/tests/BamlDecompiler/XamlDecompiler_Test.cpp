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

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

} // namespace
