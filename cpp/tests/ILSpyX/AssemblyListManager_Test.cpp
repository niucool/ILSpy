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

// Tests for the AssemblyList mutation surface (Unload / Clear / Move /
// Sort / Reload / HotReplace / SaveAsXml) and the AssemblyListManager
// (the list registry over an ISettingsProvider, the round-trip through
// the settings XML, and the framework-directory filter).

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/AssemblyListManager.hpp"

#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace Xml = ILSpy::Decompiler::Xml;

using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::AssemblyListManager;
using ILSpy::ILSpyX::LoadedAssembly;

// The in-memory ISettingsProvider the round-trip tests share: one root
// element, Update applying actions to it (the C# test double shape).
class InMemorySettingsProvider final :
    public ILSpy::ILSpyX::Settings::ISettingsProvider {
public:
    InMemorySettingsProvider()
        : root_(std::make_shared<Xml::XElement>(Xml::XName("Settings")))
    {
    }

    std::shared_ptr<Xml::XElement> Section(
        const std::string& name) const override
    {
        // The C# indexer: the section element, or a fresh empty element.
        for (const auto& child : root_->Elements()) {
            if (child->Name().LocalName() == name) {
                return std::shared_ptr<Xml::XElement>(root_, child.get());
            }
        }
        return std::make_shared<Xml::XElement>(Xml::XName(name));
    }

    void Update(
        const std::function<void(Xml::XElement&)>& action) override
    {
        action(*root_);
    }

    void SaveSettings(std::shared_ptr<Xml::XElement> section) override
    {
        savedSection_ = std::move(section);
    }

    const Xml::XElement& Root() const { return *root_; }

private:
    std::shared_ptr<Xml::XElement> root_;
    std::shared_ptr<Xml::XElement> savedSection_;
};

TEST(AssemblyListMutatorsTest, UnloadRemovesTheAssemblyFromTheList) {
    AssemblyList list;
    std::string file1 = ILSpy::Tests::WriteConnIdResDll();
    std::string file2 = WriteTinyNetModule();
    LoadedAssembly& first = list.OpenAssembly(file1);
    LoadedAssembly& second = list.OpenAssembly(file2);
    list.Unload(first);
    EXPECT_EQ(list.Count(), 1);
    EXPECT_EQ(list.GetAssemblies()[0], &second);
    EXPECT_EQ(list.FindAssembly(file1), nullptr);
}

TEST(AssemblyListMutatorsTest, ClearEmptiesTheList) {
    AssemblyList list;
    std::string file1 = ILSpy::Tests::WriteConnIdResDll();
    std::string file2 = WriteTinyNetModule();
    (void)list.OpenAssembly(file1);
    (void)list.OpenAssembly(file2);
    list.Clear();
    EXPECT_EQ(list.Count(), 0);
    EXPECT_EQ(list.FindAssembly(file1), nullptr);
    EXPECT_EQ(list.FindAssembly(file2), nullptr);
}

TEST(AssemblyListMutatorsTest, MoveReordersWithTheIndexAdjustment) {
    AssemblyList list;
    std::string fileA = ILSpy::Tests::WriteConnIdResDll();
    std::string fileB = WriteTinyNetModule();
    std::string fileC = ILSpy::Tests::WriteConnIdResDll();
    // Distinct files (three entries).
    fileC = fileA + ".copy.dll";
    {
        std::ifstream src(fileA, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(src)),
            std::istreambuf_iterator<char>());
        std::FILE* out = std::fopen(fileC.c_str(), "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), out);
        std::fclose(out);
    }
    LoadedAssembly& a = list.OpenAssembly(fileA);
    LoadedAssembly& b = list.OpenAssembly(fileB);
    LoadedAssembly& c = list.OpenAssembly(fileC);
    // Move c to the front.
    std::vector<LoadedAssembly*> toMove = {&c};
    list.Move(toMove, 0);
    {
        const auto& assemblies = list.GetAssemblies();
        ASSERT_EQ(assemblies.size(), 3u);
        EXPECT_EQ(assemblies[0], &c);
        EXPECT_EQ(assemblies[1], &a);
        EXPECT_EQ(assemblies[2], &b);
    }
    // Move a to index 2: a is removed first (index 0 < 2), so the
    // insertion lands at 1 -- the C# index-decrement rule.
    std::vector<LoadedAssembly*> moveA = {&a};
    list.Move(moveA, 2);
    {
        const auto& assemblies = list.GetAssemblies();
        ASSERT_EQ(assemblies.size(), 3u);
        EXPECT_EQ(assemblies[0], &c);
        EXPECT_EQ(assemblies[1], &a);
        EXPECT_EQ(assemblies[2], &b);
    }
}

TEST(AssemblyListMutatorsTest, SortOrdersByComparer) {
    AssemblyList list;
    std::string fileA = ILSpy::Tests::WriteConnIdResDll();
    std::string fileB = WriteTinyNetModule();
    (void)list.OpenAssembly(fileA);  // ilspy_connid_test
    (void)list.OpenAssembly(fileB);  // ilspy_tiny_test
    list.Sort([](const LoadedAssembly& x, const LoadedAssembly& y) {
        return x.ShortName().compare(y.ShortName());
    });
    const auto& assemblies = list.GetAssemblies();
    ASSERT_EQ(assemblies.size(), 2u);
    EXPECT_EQ(assemblies[0]->ShortName(), "ilspy_connid_test");
    EXPECT_EQ(assemblies[1]->ShortName(), "ilspy_tiny_test");
}

TEST(AssemblyListMutatorsTest, ReloadAssemblyCarriesTheFlags) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& original = list.OpenAssembly(file);
    original.SetIsAutoLoaded(true);
    original.SetTargetFrameworkIdOverride(".NETFramework,Version=v4.8");
    LoadedAssembly* reloaded = list.ReloadAssembly(original);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_NE(reloaded, &original);
    EXPECT_EQ(reloaded->FileName(), file);
    EXPECT_TRUE(reloaded->IsAutoLoaded());
    ASSERT_TRUE(reloaded->TargetFrameworkIdOverride().has_value());
    EXPECT_EQ(*reloaded->TargetFrameworkIdOverride(),
        ".NETFramework,Version=v4.8");
    EXPECT_EQ(list.Count(), 1);
}

TEST(AssemblyListMutatorsTest, ReloadAssemblyByPathReturnsNullForUnknown) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    (void)list.OpenAssembly(file);
    EXPECT_EQ(list.ReloadAssembly(
                  (fs::temp_directory_path() / "never.dll").string()),
        nullptr);
}

TEST(AssemblyListMutatorsTest, HotReplaceAssemblySwapsFromAStream) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& original = list.OpenAssembly(file);
    std::ifstream in(file, std::ios::binary);
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LoadedAssembly* replaced = list.HotReplaceAssembly(file,
        [image] { return std::optional<std::vector<std::uint8_t>>(image); });
    ASSERT_NE(replaced, nullptr);
    EXPECT_NE(replaced, &original);
    EXPECT_EQ(list.Count(), 1);
    // The swap is visible through both surfaces.
    EXPECT_EQ(list.GetAssemblies()[0], replaced);
    EXPECT_EQ(list.FindAssembly(file), replaced);
    // The crafted stream loads (the demand triggers it).
    (void)replaced->GetLoadResult();
    EXPECT_TRUE(replaced->IsLoadedAsValidAssembly());
}

TEST(AssemblyListMutatorsTest, HotReplaceAssemblyReturnsNullForUnknown) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    (void)list.OpenAssembly(file);
    EXPECT_EQ(list.HotReplaceAssembly(
                  (fs::temp_directory_path() / "never.dll").string(), {}),
        nullptr);
}

TEST(AssemblyListMutatorsTest, SaveAsXmlSkipsAutoLoadedAssemblies) {
    AssemblyList list;
    std::string file1 = ILSpy::Tests::WriteConnIdResDll();
    std::string file2 = WriteTinyNetModule();
    LoadedAssembly& normal = list.OpenAssembly(file1);
    (void)list.OpenAssembly(file2, /*isAutoLoaded=*/true);
    normal.SetTargetFrameworkIdOverride(".NETFramework,Version=v4.8");
    auto xml = list.SaveAsXml();
    ASSERT_NE(xml, nullptr);
    EXPECT_EQ(xml->Name().LocalName(), "List");
    // The name attribute + one Assembly element (the auto-loaded one is
    // not persisted) + the TargetFramework attribute.
    bool sawName = false;
    for (const auto* attr = xml->FirstAttribute(); attr != nullptr;
         attr = attr->NextAttribute()) {
        if (attr->Name().LocalName() == "name") {
            EXPECT_EQ(attr->Value(), "Testing Only");
            sawName = true;
        }
    }
    EXPECT_TRUE(sawName);
    int assemblyCount = 0;
    for (const auto& child : xml->Elements()) {
        EXPECT_EQ(child->Name().LocalName(), "Assembly");
        EXPECT_EQ(child->Value(), file1);
        bool hasOverride = false;
        for (const auto* attr = child->FirstAttribute(); attr != nullptr;
             attr = attr->NextAttribute()) {
            if (attr->Name().LocalName() == "TargetFramework") {
                EXPECT_EQ(attr->Value(), ".NETFramework,Version=v4.8");
                hasOverride = true;
            }
        }
        EXPECT_TRUE(hasOverride);
        ++assemblyCount;
    }
    EXPECT_EQ(assemblyCount, 1);
}

TEST(AssemblyListManagerTest, LoadListRegistersTheName) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> loadedList = manager.LoadList("MyList");
    EXPECT_EQ(loadedList->ListName(), "MyList");
    EXPECT_TRUE(manager.ContainsList("MyList"));
}

TEST(AssemblyListManagerTest, SaveListThenLoadListRoundTrips) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    {
        std::unique_ptr<AssemblyList> listPtr = manager.LoadList("RoundTrip");
        std::string file = ILSpy::Tests::WriteConnIdResDll();
        LoadedAssembly& asm_ = listPtr->OpenAssembly(file);
        (void)asm_.GetLoadResult();
        LoadedAssembly& autoLoaded =
            listPtr->OpenAssembly(file + ".auto", /*isAutoLoaded=*/true);
        (void)autoLoaded;
        // The C# OpenAssembly persists through RefreshSave; the auto entry
        // is excluded from SaveAsXml. Remove it from the list entirely so
        // the file's absence does not matter.
        listPtr->Unload(autoLoaded);
    }
    std::unique_ptr<AssemblyList> loaded = manager.LoadList("RoundTrip");
    EXPECT_EQ(loaded->Count(), 1);
    EXPECT_EQ(loaded->GetAssemblies()[0]->FileName(),
        ILSpy::Tests::WriteConnIdResDll());
}

TEST(AssemblyListManagerTest, DeleteListRemovesItFromTheSettings) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    (void)manager.LoadList("Doomed");
    EXPECT_TRUE(manager.DeleteList("Doomed"));
    EXPECT_FALSE(manager.ContainsList("Doomed"));
    EXPECT_FALSE(manager.DeleteList("Doomed"));
    // Reloading yields a fresh empty list (the name is no longer stored).
    std::unique_ptr<AssemblyList> fresh = manager.LoadList("Doomed");
    EXPECT_EQ(fresh->Count(), 0);
}

TEST(AssemblyListManagerTest, CloneAndRenamePreserveTheAssemblies) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    {
        std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Source");
        std::string file = ILSpy::Tests::WriteConnIdResDll();
        LoadedAssembly& asm_ = listPtr->OpenAssembly(file);
        (void)asm_.GetLoadResult();
    }
    EXPECT_TRUE(manager.CloneList("Source", "Clone"));
    EXPECT_TRUE(manager.ContainsList("Clone"));
    std::unique_ptr<AssemblyList> clone = manager.LoadList("Clone");
    EXPECT_EQ(clone->Count(), 1);
    EXPECT_TRUE(manager.RenameList("Source", "Renamed"));
    EXPECT_FALSE(manager.ContainsList("Source"));
    EXPECT_TRUE(manager.ContainsList("Renamed"));
    EXPECT_EQ(manager.LoadList("Renamed")->Count(), 1);
}

TEST(AssemblyListManagerTest, IsIncludedFrameworkFilePinsTheFilter) {
    EXPECT_FALSE(AssemblyListManager::IsIncludedFrameworkFile(
        "Microsoft.DiaSymReader.Native.amd64.dll"));
    EXPECT_FALSE(AssemblyListManager::IsIncludedFrameworkFile("x_cor3.dll"));
    EXPECT_FALSE(AssemblyListManager::IsIncludedFrameworkFile("X_COR3.DLL"));
    EXPECT_TRUE(AssemblyListManager::IsIncludedFrameworkFile("System.dll"));
    EXPECT_TRUE(AssemblyListManager::IsIncludedFrameworkFile("netstandard.dll"));
    EXPECT_TRUE(AssemblyListManager::IsIncludedFrameworkFile("mscorlib.dll"));
    EXPECT_FALSE(AssemblyListManager::IsIncludedFrameworkFile("coreclr.dll"));
}

TEST(AssemblyListManagerTest, AddFrameworkAssembliesFromDirectoryFilters) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Fx");
    fs::path dir = fs::temp_directory_path() / "ilspy_mgr_fx";
    fs::create_directories(dir);
    std::string src = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(src, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    for (const char* name : {"System.dll", "junk_cor3.dll", "helper.dll"}) {
        std::FILE* out = std::fopen((dir / name).string().c_str(), "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), out);
        std::fclose(out);
    }
    manager.AddFrameworkAssembliesFromDirectory(*listPtr, dir.string());
    // Only the upper-case-first names survive the filter (System.dll);
    // junk_cor3 (the _cor3 rule) and helper (lower-case first) do not.
    ASSERT_EQ(listPtr->Count(), 1);
    EXPECT_EQ(listPtr->GetAssemblies()[0]->FileName(),
        (dir / "System.dll").string());
}

TEST(AssemblyListManagerTest, OpenAssemblyUsesTheManagerRegistryAndFlags) {
    auto provider = std::make_shared<InMemorySettingsProvider>();
    AssemblyListManager manager(provider);
    manager.SetUseDebugSymbols(true);
    std::unique_ptr<AssemblyList> listPtr = manager.LoadList("Wired");
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = listPtr->OpenAssembly(file);
    (void)asm_.GetLoadResult();
    EXPECT_TRUE(asm_.IsLoadedAsValidAssembly());
}

}  // namespace
