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

// Tests for the AssemblyList port (ICSharpCode.ILSpyX/AssemblyList.cs): the
// open-by-path deduplication, the FindAssembly lookup, the list surface
// (GetAssemblies / Count / ListName), and the auto-load flag.

#include "ILSpyX/AssemblyList.hpp"

#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::LoadedAssembly;

TEST(AssemblyListTest, DefaultListNameIsTestingOnly) {
    AssemblyList list;
    EXPECT_EQ(list.ListName(), "Testing Only");
}

TEST(AssemblyListTest, OpenAssemblyDeduplicatesByFullPath) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& first = list.OpenAssembly(file);
    LoadedAssembly& second = list.OpenAssembly(file);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(list.Count(), 1);
}

TEST(AssemblyListTest, OpenAssemblyNormalizesThePathBeforeDeduplication) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& first = list.OpenAssembly(file);
    // The C# OpenAssembly runs Path.GetFullPath first: an
    // already-absolute path is stable, and a relative spelling of the same
    // file lands on the same entry. Reopen via a relative path from the
    // file's directory.
    fs::path dir = fs::path(file).parent_path();
    fs::path relative = fs::relative(fs::path(file), dir);
    LoadedAssembly& second = list.OpenAssembly((dir / relative).string());
    EXPECT_EQ(&first, &second);
}

TEST(AssemblyListTest, FindAssemblyReturnsNullForUnknownFiles) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    (void)list.OpenAssembly(file);
    EXPECT_EQ(list.FindAssembly(file), &list.OpenAssembly(file));
    EXPECT_EQ(list.FindAssembly(
                  (fs::temp_directory_path() / "never_opened.dll").string()),
        nullptr);
}

TEST(AssemblyListTest, GetAssembliesReturnsTheListInOrder) {
    AssemblyList list;
    std::string file1 = ILSpy::Tests::WriteConnIdResDll();
    std::string file2 = WriteTinyNetModule();
    LoadedAssembly& first = list.OpenAssembly(file1);
    LoadedAssembly& second = list.OpenAssembly(file2);
    ASSERT_EQ(list.Count(), 2);
    const auto& assemblies = list.GetAssemblies();
    ASSERT_EQ(assemblies.size(), 2u);
    EXPECT_EQ(assemblies[0], &first);
    EXPECT_EQ(assemblies[1], &second);
}

TEST(AssemblyListTest, OpenAssemblyCarriesTheAutoLoadFlag) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& autoLoaded = list.OpenAssembly(file, true);
    EXPECT_TRUE(autoLoaded.IsAutoLoaded());
    LoadedAssembly& explicitLoad = list.OpenAssembly(
        WriteTinyNetModule(), false);
    EXPECT_FALSE(explicitLoad.IsAutoLoaded());
}

TEST(AssemblyListTest, OpenAssemblyFromStreamSkipsTheDisk) {
    AssemblyList list;
    // The OpenAssembly(file, stream) form loads the bytes from the
    // provider without touching the (nonexistent) file.
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(image.empty());
    LoadedAssembly& asm_ = list.OpenAssembly(
        "ilspy_la_from_stream.dll",
        [image] { return std::optional<std::vector<std::uint8_t>>(image); });
    (void)asm_.GetLoadResult();
    EXPECT_TRUE(asm_.IsLoadedAsValidAssembly());
    EXPECT_EQ(asm_.ShortName(), "ilspy_la_from_stream");
    // OpenAssembly normalizes the name (Path.GetFullPath: resolved
    // against the current directory).
    EXPECT_EQ(asm_.GetLoadResult().MetadataFile->FileName(),
        fs::absolute("ilspy_la_from_stream.dll").lexically_normal().string());
}

}  // namespace
