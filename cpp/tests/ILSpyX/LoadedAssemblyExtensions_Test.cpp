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

// Tests for the LoadedPackage resolver half (the PackageFolder
// IAssemblyResolver surface and the ResolveFileName entry cache), the
// GetAllAssemblies recursion, and the LoadedAssemblyExtensions free
// functions (GetLoadedAssembly / GetAssemblyResolver /
// GetDebugInfoOrNull / GetTypeSystemOrNull).

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/LoadedAssemblyExtensions.hpp"
#include "ILSpyX/AssemblyListManager.hpp"
#include "TestFixtures/InMemorySettingsProvider.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"

#include <gtest/gtest.h>

#include <miniz/miniz.h>
#include <miniz/miniz_zip.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using ILSpy::Decompiler::Metadata::AssemblyNameReference;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::LoadedAssembly;
using ILSpy::ILSpyX::AssemblyListManager;
using ILSpy::ILSpyX::LoadedPackage;
using ILSpy::Tests::InMemorySettingsProvider;
using ILSpy::ILSpyX::PackageFolder;

fs::path TempDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("ilspy_laext_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// Builds a zip containing the connid fixture bytes as <name>.
std::string WriteZipWithAssembly(const fs::path& dir, const std::string& name)
{
    std::string src = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(src, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    const std::string zipPath = (dir / "pkg.zip").string();
    mz_zip_archive zip {};
    if (!mz_zip_writer_init_file(&zip, zipPath.c_str(), 0)) {
        return "";
    }
    mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
        MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return zipPath;
}

TEST(LoadedPackageResolverTest, ResolveFileNameLoadsTheEntry) {
    fs::path dir = TempDir("resolve");
    std::string zipPath = WriteZipWithAssembly(dir, "app.dll");
    // The zip entries load through the registry loaders: a manager-based
    // list (the testing ctor carries no registry).
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Zip");
    LoadedAssembly& zipAsm = listPtr->OpenAssembly(zipPath);
    const auto& loadResult = zipAsm.GetLoadResult();
    ASSERT_NE(loadResult.Package, nullptr);
    // The package carries the wrapper (the C# `package.LoadedAssembly =
    // this` after the load).
    EXPECT_EQ(loadResult.Package->GetLoadedAssembly(), &zipAsm);
    const PackageFolder& root = loadResult.Package->RootFolder();
    LoadedAssembly* entry = root.ResolveFileName("app.dll");
    ASSERT_NE(entry, nullptr);
    // The demand triggers the entry's load (the stream-provider path).
    (void)entry->GetLoadResult();
    EXPECT_TRUE(entry->IsLoadedAsValidAssembly());
    // The stream came from the entry, not from disk.
    EXPECT_EQ(entry->FileName(), "app.dll");
    // The cache: a second lookup returns the same instance.
    EXPECT_EQ(root.ResolveFileName("app.dll"), entry);
}

TEST(LoadedPackageResolverTest, ResolveFileNameReturnsNullWithoutWrapper) {
    // A package opened directly (no LoadedAssembly) has no wrapper, so
    // ResolveFileName answers null (the C# `package.LoadedAssembly ==
    // null` guard).
    fs::path dir = TempDir("nowrap");
    std::string zipPath = WriteZipWithAssembly(dir, "app.dll");
    auto package = LoadedPackage::FromZipFile(zipPath);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->RootFolder().ResolveFileName("app.dll"), nullptr);
}

TEST(LoadedPackageResolverTest, ResolveFindsTheEntryByReferenceName) {
    fs::path dir = TempDir("byref");
    std::string zipPath = WriteZipWithAssembly(dir, "app.dll");
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Zip");
    LoadedAssembly& zipAsm = listPtr->OpenAssembly(zipPath);
    (void)zipAsm.GetLoadResult();
    const PackageFolder& root =
        zipAsm.GetLoadResult().Package->RootFolder();
    // The C# Resolve: ResolveFileName(reference.Name + ".dll").
    auto reference = AssemblyNameReference::Parse("app, Version=1.0.0.0");
    const MetadataFile* module = root.Resolve(reference);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Name(), "connid_res");
}

TEST(LoadedPackageResolverTest, ResolveModuleFindsTheEntry) {
    fs::path dir = TempDir("bymod");
    std::string zipPath = WriteZipWithAssembly(dir, "lib/app.dll");
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Zip");
    LoadedAssembly& zipAsm = listPtr->OpenAssembly(zipPath);
    (void)zipAsm.GetLoadResult();
    // The C# ResolveModule ignores the main module on the package path;
    // the tiny netmodule stands in for the caller's main module.
    std::string tiny = WriteTinyNetModule();
    LoadedAssembly& tinyAsm = listPtr->OpenAssembly(tiny);
    const auto& mainModule = tinyAsm.GetMetadataFile();
    const PackageFolder& lib =
        *zipAsm.GetLoadResult().Package->RootFolder().Folders()[0];
    // The C# ResolveModule: ResolveFileName(moduleName + ".dll").
    const MetadataFile* module = lib.ResolveModule(mainModule, "app");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Name(), "connid_res");
}

TEST(LoadedPackageResolverTest, GetAllAssembliesRecursesIntoPackages) {
    fs::path dir = TempDir("recurse");
    std::string zipPath = WriteZipWithAssembly(dir, "lib/app.dll");
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Zip");
    LoadedAssembly& zipAsm = listPtr->OpenAssembly(zipPath);
    (void)zipAsm.GetLoadResult();
    // The snapshot recursion: the C# GetAllAssembliesAsync does NOT
    // include the package wrapper itself -- only its .dll/.exe entries
    // (each resolved through ResolveFileName on its containing folder).
    const auto& all = listPtr->GetAllAssemblies();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0]->FileName(), "app.dll");
    EXPECT_NE(all[0], &zipAsm);
}

TEST(LoadedPackageResolverTest, GetAllAssembliesIncludesFailedLoads) {
    AssemblyList list;
    std::string garbage = TempDir("failed").string() + "/garbage.bin";
    {
        std::FILE* out = std::fopen(garbage.c_str(), "wb");
        std::fputs("not a pe file", out);
        std::fclose(out);
    }
    LoadedAssembly& bad = list.OpenAssembly(garbage);
    try {
        (void)bad.GetLoadResult();
    } catch (const std::exception&) {
        // The C# faulted-task shape.
    }
    (void)list.OpenAssembly(ILSpy::Tests::WriteConnIdResDll());
    // The C# GetAllAssembliesAsync catch arm: a faulted load is added
    // anyway.
    const auto& all = list.GetAllAssemblies();
    ASSERT_EQ(all.size(), 2u);
}

TEST(LoadedAssemblyExtensionsTest, GetLoadedAssemblyRoundTrips) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    const auto& module = asm_.GetMetadataFile();
    EXPECT_EQ(&ILSpy::ILSpyX::GetLoadedAssembly(module), &asm_);
}

TEST(LoadedAssemblyExtensionsTest, GetLoadedAssemblyThrowsForUntrackedFiles) {
    // A MetadataFile opened directly (no LoadedAssembly) is not
    // associated with one: the C# ArgumentException message.
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    MetadataFile direct(file);
    EXPECT_THROW(
        try { (void)ILSpy::ILSpyX::GetLoadedAssembly(direct); } catch (
            const std::invalid_argument& ex) {
            EXPECT_STREQ(ex.what(),
                "The specified file is not associated with a "
                "LoadedAssembly!");
            throw;
        },
        std::invalid_argument);
}

TEST(LoadedAssemblyExtensionsTest, GetAssemblyResolverDelegates) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    const auto& module = asm_.GetMetadataFile();
    auto resolver = ILSpy::ILSpyX::GetAssemblyResolver(module);
    auto reference = AssemblyNameReference::Parse(
        "connid_res, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    EXPECT_NE(resolver->Resolve(reference), nullptr);
}

TEST(LoadedAssemblyExtensionsTest, GetDebugInfoOrNullWithoutSymbolsIsNull) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    const auto& module = asm_.GetMetadataFile();
    // No debug symbols requested: the provider slot is empty.
    EXPECT_EQ(ILSpy::ILSpyX::GetDebugInfoOrNull(module), nullptr);
}

TEST(LoadedAssemblyExtensionsTest, GetTypeSystemOrNullCachesTheCompilation) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    const auto& module = asm_.GetMetadataFile();
    auto first = ILSpy::ILSpyX::GetTypeSystemOrNull(module);
    ASSERT_NE(first, nullptr);
    // The uncached-options compilation carries the module's types plus
    // the minimal corlib.
    EXPECT_EQ(first->MainModule().AssemblyName(), "connid_res");
    auto second = ILSpy::ILSpyX::GetTypeSystemOrNull(module);
    EXPECT_EQ(first, second);
}

}  // namespace
