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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The --list-resources feature (one C# source pair, so its tests share one
// file): the ResourceExtensions.EnumerateResourcePaths port and the
// IlspyCmdProgram.ListResources render over it. The real-assembly
// expectations (the mscorlib/System.dll rows, kind partitions and entry
// counts) were probed through the real ilspycmd 11.0 --list-resources and
// the real PEFile.Resources/ResourcesFile pair; the synthetic
// resource-manifest fixture (ResourcesTestFixtures.hpp) covers the arms no
// installed assembly carries (the .resources-suffixed garbage blob's
// raw-name fallback, the File-linked row's exclusion) with paths dumped
// from the real EnumerateResourcePaths logic over the same bytes.

#include "ILSpyCmd/IlspyCmdProgram.hpp"
#include <cstdlib>
#include "ILSpyCmd/ResourceExtensions.hpp"

#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include "TestFixtures/BamlResFixtures.hpp"
#include "TestFixtures/ResourcesTestFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace Cmd = ILSpy::ILSpyCmd;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Tests::WriteResTestDll;
using ILSpy::Tests::WriteValTestDll;
// TinyNetModule.hpp's helpers live at global scope (its documented layout);
// ordinary lookup finds WriteTinyNetModule from inside this anonymous
// namespace.

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::string SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    // The corpus convention (see PORT_LOG_BAML.md): when
    // ILSPY_TEST_MSCORLIB points into the .NET Framework 4.8
    // reference-assembly corpus, System.dll is the mscorlib's sibling.
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB");
        env != nullptr && std::filesystem::exists(env)) {
        std::filesystem::path dir = std::filesystem::path(env).parent_path();
        return (dir / "System.dll").string();
    }
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

}  // namespace

// ---- The ManifestResource metadata reads (the MetadataFile side)

// mscorlib 4.8's eight rows: five File-linked .nlp rows (excluded from the
// path list), then the three embedded ones the probe pinned (the
// mscorlib.resources container, charinfo.nlp, codepages.nlp).
TEST(ResourceExtensionsTest, MscorlibManifestResourceReads)
{
    // The row set (the five .nlp files) is the .NET Framework 4.8
    // mscorlib's own manifest -- a Windows fixture.
    if (!std::filesystem::exists(MscorlibPath())) {
        GTEST_SKIP() << "mscorlib fixture " << MscorlibPath()
                     << " not present on this host";
    }
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    auto resources = mscorlib.GetManifestResources();
    ASSERT_EQ(resources.size(), 8u);
    EXPECT_EQ(resources[0].Token, 0x28000001u);
    EXPECT_EQ(resources[0].Name, "normidna.nlp");
    EXPECT_EQ(resources[0].Offset, 0u);
    EXPECT_EQ(resources[0].Attributes, 1u);  // ManifestResourceAttributes.Public
    EXPECT_EQ(resources[0].ImplementationToken, 0x26000001u);  // the File row
    EXPECT_EQ(resources[0].Kind,
        MetadataFile::ManifestResourceKind::Linked);
    // The five linked .nlp rows target the five File-table rows.
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(resources[i].Kind,
            MetadataFile::ManifestResourceKind::Linked) << i;
        EXPECT_EQ(resources[i].ImplementationToken,
            0x26000001u + i) << i;
    }
    EXPECT_EQ(resources[5].Token, 0x28000006u);
    EXPECT_EQ(resources[5].Name, "mscorlib.resources");
    EXPECT_EQ(resources[5].Offset, 0u);
    EXPECT_EQ(resources[5].ImplementationToken, 0u);
    EXPECT_EQ(resources[5].Kind,
        MetadataFile::ManifestResourceKind::Embedded);
    EXPECT_EQ(resources[6].Name, "charinfo.nlp");
    EXPECT_EQ(resources[6].Offset, 353040u);
    EXPECT_EQ(resources[6].Kind,
        MetadataFile::ManifestResourceKind::Embedded);
    EXPECT_EQ(resources[7].Name, "codepages.nlp");
    EXPECT_EQ(resources[7].Offset, 390040u);
    EXPECT_EQ(resources[7].Kind,
        MetadataFile::ManifestResourceKind::Embedded);

    // The embedded-resource reads (the TryOpenStream/TryGetLength pair):
    // the blob sizes the probe pinned, and no stream for a linked row or a
    // bogus token.
    auto blob = mscorlib.TryGetManifestResourceData(0x28000006u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 353031u);
    blob = mscorlib.TryGetManifestResourceData(0x28000007u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 36992u);
    blob = mscorlib.TryGetManifestResourceData(0x28000008u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 612764u);
    EXPECT_FALSE(mscorlib.TryGetManifestResourceData(0x28000001u).has_value());
    EXPECT_FALSE(
        mscorlib.TryGetManifestResourceData(0x28000009u).has_value());
    EXPECT_FALSE(
        mscorlib.TryGetManifestResourceData(0x06000001u).has_value());

    // The blob bytes: charinfo.nlp's blob starts with the UTF-16LE "cha"
    // (the resource data itself starts right after the length prefix).
    blob = mscorlib.TryGetManifestResourceData(0x28000007u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ((*blob)[0], 0x63u);
    EXPECT_EQ((*blob)[1], 0x00u);
    EXPECT_EQ((*blob)[2], 0x68u);
    EXPECT_EQ((*blob)[3], 0x00u);
}

// ---- EnumerateResourcePaths over the real assemblies

// mscorlib: 3172 container entries then the two raw .nlp names -- the
// exact partition the real tool prints (3174 lines).
TEST(ResourceExtensionsTest, MscorlibPaths)
{
    // The 3174-line path list is the .NET Framework 4.8 mscorlib's own
    // manifest -- a Windows fixture.
    if (!std::filesystem::exists(MscorlibPath())) {
        GTEST_SKIP() << "mscorlib fixture " << MscorlibPath()
                     << " not present on this host";
    }
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    auto paths = Cmd::EnumerateResourcePaths(mscorlib);
    ASSERT_EQ(paths.size(), 3174u);
    EXPECT_EQ(paths[0], "mscorlib.resources/Format_MissingIncompleteDate");
    EXPECT_EQ(paths[1], "mscorlib.resources/Interop.COM_TypeMismatch");
    EXPECT_EQ(paths[3171],
        "mscorlib.resources/Argument_BadPersistableModuleInTransientAssembly");
    EXPECT_EQ(paths[3172], "charinfo.nlp");
    EXPECT_EQ(paths[3173], "codepages.nlp");
}

// System.dll: the container plus the seven raw .bmp names (1688 lines).
TEST(ResourceExtensionsTest, SystemDllPaths)
{
    // The 1688-line path list and the seven toolbox-bitmap rows are the
    // .NET Framework 4.8 System.dll's own manifest -- a Windows fixture.
    if (!std::filesystem::exists(SystemDllPath())) {
        GTEST_SKIP() << "System.dll fixture " << SystemDllPath()
                     << " not present on this host";
    }
    MetadataFile systemDll(SystemDllPath());
    ASSERT_TRUE(systemDll.IsValid());
    auto paths = Cmd::EnumerateResourcePaths(systemDll);
    ASSERT_EQ(paths.size(), 1688u);
    EXPECT_EQ(paths[0], "System.resources/Config_system_already_set");
    EXPECT_EQ(paths[1], "System.resources/ProcessMaxWorkingSet");
    EXPECT_EQ(paths[1680], "System.resources/net_webstatus_Timeout");
    // The seven toolbox-bitmap resources the probe listed.
    EXPECT_EQ(paths[1681], "System.Diagnostics.EventLog.bmp");
    EXPECT_EQ(paths[1682], "System.Diagnostics.PerformanceCounter.bmp");
    EXPECT_EQ(paths[1683], "System.Diagnostics.Process.bmp");
    EXPECT_EQ(paths[1684], "System.IO.FileSystemWatcher.bmp");
    EXPECT_EQ(paths[1685], "System.Timers.Timer.bmp");
    EXPECT_EQ(paths[1686], "System.ComponentModel.BackgroundWorker.bmp");
    EXPECT_EQ(paths[1687], "System.IO.Ports.SerialPort.bmp");
}

// ---- The synthetic resource manifest: every path shape in one fixture

// The four ManifestResource rows resolve exactly as the generator
// authored them: the valid container, the garbage .resources-suffixed
// blob, the plain resource, and the File-linked row.
TEST(ResourceExtensionsTest, ResTestManifestReads)
{
    std::string path = WriteResTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    auto resources = module.GetManifestResources();
    ASSERT_EQ(resources.size(), 4u);
    EXPECT_EQ(resources[0].Token, 0x28000001u);
    EXPECT_EQ(resources[0].Name, "test.resources");
    EXPECT_EQ(resources[0].Offset, 0u);
    EXPECT_EQ(resources[0].ImplementationToken, 0u);
    EXPECT_EQ(resources[0].Kind,
        MetadataFile::ManifestResourceKind::Embedded);
    EXPECT_EQ(resources[1].Token, 0x28000002u);
    EXPECT_EQ(resources[1].Name, "bad.resources");
    EXPECT_EQ(resources[1].Kind,
        MetadataFile::ManifestResourceKind::Embedded);
    EXPECT_EQ(resources[2].Token, 0x28000003u);
    EXPECT_EQ(resources[2].Name, "plain.nlp");
    EXPECT_EQ(resources[2].Kind,
        MetadataFile::ManifestResourceKind::Embedded);
    EXPECT_EQ(resources[3].Token, 0x28000004u);
    EXPECT_EQ(resources[3].Name, "linked.nlp");
    EXPECT_EQ(resources[3].ImplementationToken, 0x26000001u);
    EXPECT_EQ(resources[3].Kind, MetadataFile::ManifestResourceKind::Linked);

    // The embedded blobs: the container is the 348-byte ResourceWriter
    // fixture, the garbage blob the four bytes, the plain resource the
    // three bytes; the linked row has no blob.
    auto blob = module.TryGetManifestResourceData(0x28000001u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 348u);
    blob = module.TryGetManifestResourceData(0x28000002u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 4u);
    EXPECT_EQ((*blob)[0], 0xDEu);
    EXPECT_EQ((*blob)[3], 0xEFu);
    blob = module.TryGetManifestResourceData(0x28000003u);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 3u);
    EXPECT_EQ((*blob)[0], 0x11u);
    EXPECT_EQ((*blob)[2], 0x33u);
    EXPECT_FALSE(module.TryGetManifestResourceData(0x28000004u).has_value());
}

// The path list over the synthetic manifest: the exact seven paths the
// real EnumerateResourcePaths logic yields (the container's five entries
// in row order, the garbage .resources blob's raw-name fallback, the plain
// resource; the linked row excluded).
TEST(ResourceExtensionsTest, ResTestManifestPaths)
{
    std::string path = WriteResTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    auto paths = Cmd::EnumerateResourcePaths(module);
    ASSERT_EQ(paths.size(), 7u);
    EXPECT_EQ(paths[0],
        "test.resources/Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87");
    EXPECT_EQ(paths[1], "test.resources/Alpha");
    EXPECT_EQ(paths[2], "test.resources/Gamma");
    EXPECT_EQ(paths[3], "test.resources/Delta");
    EXPECT_EQ(paths[4], "test.resources/Beta");
    EXPECT_EQ(paths[5], "bad.resources");
    EXPECT_EQ(paths[6], "plain.nlp");
}

// ---- The ListResources render (the C# `output.WriteLine(path)` shape)

// The whole render over the synthetic manifest: the seven CRLF-terminated
// lines, exactly the bytes the real tool prints.
TEST(ResourceExtensionsTest, ListResourcesRender)
{
    std::string path = WriteResTestDll();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    EXPECT_EQ(Cmd::ListResources(path, output), 0);
    EXPECT_EQ(output.str(),
        "test.resources/Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87\r\n"
        "test.resources/Alpha\r\n"
        "test.resources/Gamma\r\n"
        "test.resources/Delta\r\n"
        "test.resources/Beta\r\n"
        "bad.resources\r\n"
        "plain.nlp\r\n");
}

// The netmodule carries no ManifestResource rows: the render is empty.
TEST(ResourceExtensionsTest, ListResourcesNoResources)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    EXPECT_EQ(Cmd::ListResources(path, output), 0);
    EXPECT_EQ(output.str(), "");
}

// An unparseable file: the port's MetadataFile never throws, so the
// render is empty and the exit code stays 0 (main.cpp gates IsValid
// before dispatching).
TEST(ResourceExtensionsTest, ListResourcesUnparseableFile)
{
    std::ostringstream output;
    EXPECT_EQ(Cmd::ListResources("Z:\\no\\such\\file.dll", output), 0);
    EXPECT_EQ(output.str(), "");
}

// ---- The resource lookup (TryGetResource)

// The whole-path arm: an embedded resource's own name (case-insensitive)
// yields its whole blob as a ByteArray -- the restest manifest's container
// row and its garbage/plain rows.
TEST(ResourceExtensionsTest, TryGetResourceWholePath)
{
    std::string path = WriteResTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());

    // The container row: the whole 348-byte blob (the bytes after the
    // 4-byte length prefix).
    {
        auto value = Cmd::TryGetResource(module, "test.resources");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::ByteArray);
        EXPECT_EQ(value->bytes.size(), 348u);
    }
    // The case-insensitive whole-name match.
    {
        auto value = Cmd::TryGetResource(module, "TEST.RESOURCES");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->bytes.size(), 348u);
    }
    // The garbage .resources-suffixed blob: a whole-path byte arm (the
    // parse failure only affects the entry arm).
    {
        auto value = Cmd::TryGetResource(module, "bad.resources");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->bytes,
            (std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    }
    // The plain embedded blob.
    {
        auto value = Cmd::TryGetResource(module, "plain.nlp");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->bytes,
            (std::vector<std::uint8_t>{0x11, 0x22, 0x33}));
    }
}

// The entry arm: the "<container>/<entry>" path into a .resources
// container (the case-insensitive first match in the container's row
// order), with the values the real TryReadResourcesEntry logic yielded
// over the same bytes -- the string entry as-is, the Stream values reduced
// to their byte arrays, and the serialized user types reduced to their
// GetBytes() regions.
TEST(ResourceExtensionsTest, TryGetResourceEntries)
{
    std::string path = WriteValTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());

    {
        auto value = Cmd::TryGetResource(module, "v2.resources/Str");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::String);
        EXPECT_EQ(value->str, "one");
    }
    // The case-insensitive entry match (the whole path folds).
    {
        auto value = Cmd::TryGetResource(module, "V2.RESOURCES/STR");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->str, "one");
    }
    // The integral entry as its decoded value (not bytes).
    {
        auto value = Cmd::TryGetResource(module, "v2.resources/Int");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::Int32);
        EXPECT_EQ(static_cast<std::int64_t>(value->integer), 42);
    }
    // The Stream value: the byte-array copy.
    {
        auto value = Cmd::TryGetResource(module, "v2.resources/Stream");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::ByteArray);
        EXPECT_EQ(value->bytes, (std::vector<std::uint8_t>{7, 8, 9}));
    }
    // The serialized user type: the GetBytes() region as bytes.
    {
        auto value = Cmd::TryGetResource(module, "v1.resources/V1User");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::ByteArray);
        EXPECT_EQ(value->bytes,
            (std::vector<std::uint8_t>{0xCC, 0xDD, 0xEE}));
    }
    {
        auto value = Cmd::TryGetResource(module,
            "serfmt.resources/SerUserLast");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->bytes,
            (std::vector<std::uint8_t>{0x55, 0x66}));
    }
}

// The not-found shapes: an unknown whole name, an unknown entry name
// inside a valid container, an entry path into a NON-.resources-suffixed
// resource (the prefix arm only applies to .resources containers), the
// entry path into the garbage container (the parse failure), and the
// File-linked row (never listed, never found).
TEST(ResourceExtensionsTest, TryGetResourceNotFound)
{
    std::string path = WriteResTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());

    EXPECT_FALSE(Cmd::TryGetResource(module, "nope").has_value());
    EXPECT_FALSE(
        Cmd::TryGetResource(module, "test.resources/Nope").has_value());
    // plain.nlp does not end with .resources: its prefix never matches.
    EXPECT_FALSE(
        Cmd::TryGetResource(module, "plain.nlp/x").has_value());
    // The garbage container: the parse failure falls back to not-found.
    EXPECT_FALSE(
        Cmd::TryGetResource(module, "bad.resources/x").has_value());
    // The linked row is filtered out of the walk.
    EXPECT_FALSE(Cmd::TryGetResource(module, "linked.nlp").has_value());
}

// The real mscorlib container: a string entry through the entry arm (the
// probe-pinned value) and the whole charinfo.nlp blob through the
// whole-path arm.
TEST(ResourceExtensionsTest, TryGetResourceMscorlib)
{
    // The pinned entries (the charinfo.nlp blob and the exact resource
    // strings) are the .NET Framework 4.8 mscorlib's own resources -- a
    // Windows fixture.
    if (!std::filesystem::exists(MscorlibPath())) {
        GTEST_SKIP() << "mscorlib fixture " << MscorlibPath()
                     << " not present on this host";
    }
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    {
        auto value = Cmd::TryGetResource(
            mscorlib, "mscorlib.resources/Format_MissingIncompleteDate");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::String);
        EXPECT_EQ(value->str,
            "There must be at least a partial date with a year present in "
            "the input.");
    }
    {
        auto value = Cmd::TryGetResource(mscorlib, "charinfo.nlp");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->kind,
            ILSpy::Decompiler::Util::ResourceValue::Kind::ByteArray);
        EXPECT_EQ(value->bytes.size(), 36992u);
    }
}

// ---- The DecompileBaml BamlDecompiler bridge (the landed Phase-9 port)

// The fixture's page.xaml.baml blob: the crafted ToolBar stream's real
// decompile (the XamlDecompilerProbe gold render over the identical
// stream bytes, and the real ilspycmd --resource over the identical
// fixture bytes renders the same line).
TEST(ResourceExtensionsTest, DecompileBamlToolBarRender)
{
    std::string path = ILSpy::Tests::WriteBamlResDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    auto value = Cmd::TryGetResource(module, "page.xaml.baml");
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(value->kind,
        ILSpy::Decompiler::Util::ResourceValue::Kind::ByteArray);

    ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
        path, false,
        ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(module,
            std::nullopt));
    ILSpy::BamlDecompiler::BamlDecompilerSettings settings;
    auto xaml = Cmd::DecompileBaml(module, resolver, value->bytes.data(),
        value->bytes.size(), settings);
    ASSERT_NE(xaml, nullptr);
    EXPECT_EQ(xaml->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
}

// The fixture's page2.xaml.baml blob: the crafted main-module System.String
// stream through the full rewrite chain (the XClass rename render), and
// the bad.baml blob rejected by the BamlReader's signature-length check
// (the C# InvalidDataException the CLI's global catch turns into
// EX_SOFTWARE).
TEST(ResourceExtensionsTest, DecompileBamlXClassRenameAndGarbage)
{
    std::string path = ILSpy::Tests::WriteBamlResDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
        path, false,
        ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(module,
            std::nullopt));
    ILSpy::BamlDecompiler::BamlDecompilerSettings settings;
    {
        auto value = Cmd::TryGetResource(module, "page2.xaml.baml");
        ASSERT_TRUE(value.has_value());
        auto xaml = Cmd::DecompileBaml(module, resolver, value->bytes.data(),
            value->bytes.size(), settings);
        ASSERT_NE(xaml, nullptr);
        EXPECT_EQ(xaml->ToString(),
            "<String xmlns=\"http://probe.pi/ns\">hello</String>");
    }
    {
        auto value = Cmd::TryGetResource(module, "bad.baml");
        ASSERT_TRUE(value.has_value());
        EXPECT_THROW(
            (void)Cmd::DecompileBaml(module, resolver, value->bytes.data(),
                value->bytes.size(), settings),
            std::exception);
    }
}
