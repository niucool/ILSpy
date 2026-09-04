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
#include "ILSpyCmd/ResourceExtensions.hpp"

#include "TestFixtures/ResourcesTestFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace {

namespace Cmd = ILSpy::ILSpyCmd;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Tests::WriteResTestDll;
// TinyNetModule.hpp's helpers live at global scope (its documented layout);
// ordinary lookup finds WriteTinyNetModule from inside this anonymous
// namespace.

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::string SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
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
