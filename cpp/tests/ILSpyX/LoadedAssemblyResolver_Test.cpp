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

// Resolver-integration tests for the LoadedAssembly port: the
// MyAssemblyResolver nested class (the C# ResolveCoreAsync step order),
// the ReferenceLoadInfo messages, and the GetAssemblyResolver /
// GetAssemblyReferenceClassifier factories.
//
// The connid fixture's metadata (pinned by the probe): Name
// "connid_res", FullName "connid_res, Version=0.0.0.0, Culture=neutral,
// PublicKeyToken=null", detected TFM ".NETCoreApp,Version=v10.0". The
// universal resolver finds nothing for that TFM on this host (no shared
// framework dir carries the fixture), so the assembly-list and
// similar-name arms are the observable ones.

#include "ILSpyX/AssemblyList.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

using ILSpy::Decompiler::Metadata::AssemblyNameReference;
using ILSpy::Decompiler::Metadata::MessageKind;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::ReferenceLoadInfo;
using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::LoadedAssembly;

// The C# exact full name of the connid fixture (probed).
const char* const kConnIdFullName =
    "connid_res, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null";

// A provided resolver answering with one fixed module (the C#
// `IAssemblyResolver? assemblyResolver` ctor parameter).
class FixedResolver final : public ILSpy::Decompiler::Metadata::IAssemblyResolver {
public:
    explicit FixedResolver(const std::string& path)
        : module_(path)
    {
    }

    const MetadataFile* Resolve(
        const ILSpy::Decompiler::Metadata::IAssemblyReference&) const override
    {
        return &module_;
    }
    const MetadataFile* ResolveModule(const MetadataFile&,
        const std::string&) const override
    {
        return &module_;
    }

private:
    MetadataFile module_;
};

// Copies a fixture to <dir>/<name> and returns the path.
std::string CopyTo(const std::string& source, const fs::path& dir,
    const std::string& name)
{
    fs::path target = dir / name;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    return target.string();
}

TEST(LoadedAssemblyResolverTest, ResolveFindsAMatchingAssemblyInTheList) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    auto resolver = asm_.GetAssemblyResolver();
    auto reference = AssemblyNameReference::Parse(kConnIdFullName);
    const MetadataFile* module = resolver->Resolve(reference);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Name(), "connid_res");
    // The exact-match arm records the C# message (AddMessageOnce, Info).
    const ReferenceLoadInfo& info = asm_.LoadedAssemblyReferencesInfo();
    bool found = false;
    for (const auto* entry : info.Entries()) {
        if (entry->FullName() == kConnIdFullName) {
            ASSERT_FALSE(entry->Messages().empty());
            EXPECT_EQ(entry->Messages().front().first, MessageKind::Info);
            EXPECT_EQ(entry->Messages().front().second,
                "Success - Found in Assembly List");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(LoadedAssemblyResolverTest, ProvidedResolverAnswersFirst) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    FixedResolver provided(file);
    LoadedAssembly::Options options;
    options.AssemblyResolver = &provided;
    LoadedAssembly withProvided(list, WriteTinyNetModule(), options);
    (void)withProvided.GetLoadResult();
    auto resolver = withProvided.GetAssemblyResolver();
    auto reference = AssemblyNameReference::Parse(kConnIdFullName);
    const MetadataFile* module = resolver->Resolve(reference);
    // The provided resolver's module, not the list's (the C#
    // ProvidedByParentResolver outcome).
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->FileName(), file);
}

TEST(LoadedAssemblyResolverTest, UnknownReferenceReportsTheError) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    auto resolver = asm_.GetAssemblyResolver();
    auto reference = AssemblyNameReference::Parse(
        "no_such_asm, Version=1.2.3.4, Culture=neutral, PublicKeyToken=null");
    EXPECT_EQ(resolver->Resolve(reference), nullptr);
    const ReferenceLoadInfo& info = asm_.LoadedAssemblyReferencesInfo();
    bool found = false;
    for (const auto* entry : info.Entries()) {
        if (entry->FullName() ==
            "no_such_asm, Version=1.2.3.4, Culture=neutral, "
            "PublicKeyToken=null") {
            ASSERT_FALSE(entry->Messages().empty());
            EXPECT_EQ(entry->Messages().front().first, MessageKind::Error);
            EXPECT_EQ(entry->Messages().front().second,
                "Could not find reference: no_such_asm, Version=1.2.3.4, "
                "Culture=neutral, PublicKeyToken=null");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(LoadedAssemblyResolverTest, SimilarNameMatchesWithDifferentVersion) {
    AssemblyList list;
    // A hermetic directory: the universal resolver's search-directory arm
    // looks in the main assembly's directory, and a stray copy of the
    // fixture next to it would take the disk-load arm instead.
    fs::path dir = fs::temp_directory_path() / "ilspy_la_similar";
    fs::create_directories(dir);
    std::string file = CopyTo(ILSpy::Tests::WriteConnIdResDll(), dir,
        "connid_main.dll");
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    auto resolver = asm_.GetAssemblyResolver();
    // Same short name, different version: the exact-match arm misses (a
    // different full name), the universal resolver finds nothing in the
    // hermetic directory, and the similar-name arm answers with the
    // loaded assembly.
    auto reference = AssemblyNameReference::Parse(
        "connid_res, Version=9.0.0.0, Culture=neutral, PublicKeyToken=null");
    const MetadataFile* module = resolver->Resolve(reference);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Name(), "connid_res");
    const ReferenceLoadInfo& info = asm_.LoadedAssemblyReferencesInfo();
    bool found = false;
    for (const auto* entry : info.Entries()) {
        if (entry->FullName() ==
            "connid_res, Version=9.0.0.0, Culture=neutral, "
            "PublicKeyToken=null") {
            ASSERT_FALSE(entry->Messages().empty());
            EXPECT_EQ(entry->Messages().front().first, MessageKind::Info);
            EXPECT_EQ(entry->Messages().front().second,
                std::string("Success - Found in Assembly List with "
                            "different TFM or version: ") +
                    file);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(LoadedAssemblyResolverTest, ResolveModuleLoadsFromTheMainModuleDirectory) {
    AssemblyList list;
    // The tiny netmodule sits next to the main module on disk.
    std::string tiny = WriteTinyNetModule();
    fs::path dir = fs::temp_directory_path() / "ilspy_la_resmod";
    fs::create_directories(dir);
    std::string besideMain = CopyTo(tiny, dir, "tiny.netmodule");
    std::string mainPath = CopyTo(ILSpy::Tests::WriteConnIdResDll(), dir,
        "main.dll");
    LoadedAssembly& mainAsm = list.OpenAssembly(mainPath);
    const auto& mainModule = mainAsm.GetMetadataFile();
    auto resolver = mainAsm.GetAssemblyResolver();
    const MetadataFile* module = resolver->ResolveModule(mainModule,
        "tiny.netmodule");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->FileName(), besideMain);
}

TEST(LoadedAssemblyResolverTest,
    ResolveModuleFallsBackToASimilarModuleInTheList) {
    AssemblyList list;
    // The tiny netmodule is loaded but NOT beside the main module.
    std::string tiny = WriteTinyNetModule();
    std::string mainPath = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& mainAsm = list.OpenAssembly(mainPath);
    LoadedAssembly& tinyAsm = list.OpenAssembly(tiny);
    (void)tinyAsm.GetLoadResult();
    const auto& mainModule = mainAsm.GetMetadataFile();
    auto resolver = mainAsm.GetAssemblyResolver();
    // The tiny module's Module-table name (pinned by the
    // DotNetCorePathFinderExtensions fixture table): "tiny".
    const MetadataFile* module = resolver->ResolveModule(mainModule, "tiny");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->FileName(), tiny);
    // The resolver records on the PARENT's load info (the C#
    // referenceLoadInfo = parent.LoadedAssemblyReferencesInfo).
    const ReferenceLoadInfo& info = mainAsm.LoadedAssemblyReferencesInfo();
    bool found = false;
    for (const auto* entry : info.Entries()) {
        if (entry->FullName() == "tiny") {
            ASSERT_FALSE(entry->Messages().empty());
            EXPECT_EQ(entry->Messages().front().first, MessageKind::Info);
            EXPECT_EQ(entry->Messages().front().second,
                "Success - Found in Assembly List");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(LoadedAssemblyResolverTest, ClassifierFactoryIsCached) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // The C# LazyInitializer semantics: one universal resolver per
    // LoadedAssembly, created on first demand.
    const auto* first = &asm_.GetAssemblyReferenceClassifier(false);
    const auto* second = &asm_.GetAssemblyReferenceClassifier(false);
    EXPECT_EQ(first, second);
}

TEST(LoadedAssemblyResolverTest, OpenAssemblyOnTheResolverListAutoLoads) {
    AssemblyList list;
    // The loadOnDemand arm opens whatever the universal resolver finds
    // with isAutoLoaded: true. The disk arm is reached through
    // ResolveModule here (the universal resolver finds nothing for the
    // fixture's TFM on this host); pin the flag via the module load.
    std::string tiny = WriteTinyNetModule();
    fs::path dir = fs::temp_directory_path() / "ilspy_la_autoload";
    fs::create_directories(dir);
    std::string besideMain = CopyTo(tiny, dir, "tiny.netmodule");
    std::string mainPath = CopyTo(ILSpy::Tests::WriteConnIdResDll(), dir,
        "main.dll");
    LoadedAssembly& mainAsm = list.OpenAssembly(mainPath);
    const auto& mainModule = mainAsm.GetMetadataFile();
    auto resolver = mainAsm.GetAssemblyResolver(/*loadOnDemand=*/true);
    (void)resolver->ResolveModule(mainModule, "tiny.netmodule");
    // The module was opened through the list, auto-loaded.
    LoadedAssembly* autoLoaded = list.FindAssembly(besideMain);
    ASSERT_NE(autoLoaded, nullptr);
    EXPECT_TRUE(autoLoaded->IsAutoLoaded());
}

}  // namespace
