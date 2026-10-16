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

// Tests for the DotNetCorePathFinder / ReferenceLoadInfo /
// ParseTargetFramework port (the first UniversalAssemblyResolver sub-slice:
// the enums, the ParseTargetFramework classifier, the load-info
// bookkeeping, and the .deps.json/shared-framework/reference-pack path
// finder).
//
// Every expectation is gold-dumped from the REAL ICSharpCode.Decompiler
// 11.0 over the identical fixtures by the DncpfProbe gold probe
// (C:/temp-probe/DncpfProbe): the Path BCL matrix, the 37-case
// ParseTargetFramework matrix, the ReferenceLoadInfo drives, the sixteen
// crafted .deps.json scenarios, and the real-machine drives (the dotnet
// install, the version-folder walk, the reference-assembly packs).

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinder.hpp"
#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace {

namespace fs = std::filesystem;
using ILSpy::Decompiler::Metadata::AssemblyNameReference;
using ILSpy::Decompiler::Metadata::DotNetCorePathFinder;
using ILSpy::Decompiler::Metadata::MessageKind;
using ILSpy::Decompiler::Metadata::ParseTargetFramework;
using ILSpy::Decompiler::Metadata::ParsedTargetFramework;
using ILSpy::Decompiler::Metadata::ReferenceLoadInfo;
using ILSpy::Decompiler::Metadata::TargetFrameworkIdentifier;
using ILSpy::Decompiler::TypeSystem::Version;

// The probe's fixture root (the gold expectations embed these exact paths;
// the setup recreates the layout before the drives).
const char* WorkRoot() {
    return "C:\\temp-probe\\DncpfProbe\\work";
}

std::string Show(const std::optional<std::string>& s) {
    return s ? "[" + *s + "]" : "<null>";
}

std::string ShowVersion(const std::optional<Version>& v) {
    return v ? "[" + v->ToString() + "]" : "<null>";
}

void WriteText(const std::string& path, const std::string& text) {
    fs::path file(path);
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

void WriteByte(const std::string& path) {
    fs::path file(path);
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << "x";
}

#if defined(_WIN32)
void SetNuGetPackages(const char* value) {
    ASSERT_EQ(0, _putenv_s("NUGET_PACKAGES", value));
}
#else
void SetNuGetPackages(const char* value) {
    ASSERT_EQ(0, setenv("NUGET_PACKAGES", value, 1));
}
#endif

// The probe's BuildLayout: the crafted NuGet package layout, the app-dir
// siblings, the version-folder fixture, the search-pattern quirk folder,
// and the sixteen .deps.json manifests.
void BuildLayout() {
    const std::string work = WorkRoot();
    std::error_code ec;
    fs::remove_all(fs::path(work), ec);
    const std::string nuget = work + "\\nuget";
    const std::string appDir = work + "\\app";
    fs::create_directories(appDir);

    auto pkg = [&](const std::string& name, const std::string& version,
        const std::string& itemPath, const std::string& dllName, bool exe = false) {
        const std::string dir = nuget + "\\" + name + "\\" + version + "\\" + itemPath;
        fs::create_directories(fs::path(dir));
        WriteByte(dir + "\\" + dllName + (exe ? ".exe" : ".dll"));
    };
    pkg("first", "1.0.0", "lib\\net6.0", "First.Library");
    pkg("second", "2.0.0", "runtimes\\win-x64\\lib\\netstandard2.0", "Second");
    pkg("exeonly", "3.0.0", "lib\\net6.0", "ExeOnly", true);
    pkg("deep", "1.2.3", "lib\\net8.0", "Deep.Lib");
    pkg("good", "1.0.0", "lib\\net6.0", "Empty");
    fs::create_directories(fs::path(work + "\\wsroot"));

    WriteByte(appDir + "\\Sibling.dll");
    WriteByte(appDir + "\\ExeSibling.exe");

    const std::string vroot = work + "\\versions";
    auto ver = [&](const std::string& name, const std::vector<std::string>& files) {
        for (const std::string& file : files) {
            WriteByte(vroot + "\\" + name + "\\" + file);
        }
    };
    ver("1.0", {"x.dll"});
    ver("2.0", {"x.dll"});
    ver("2.5", {"no-dll.txt"});
    ver("3.0-beta1", {"x.dll"});
    ver("10.0.8", {"x.dll"});
    ver("weird", {"x.dll"});
    ver("4.0", {"sub\\nested.dll"});
    ver("5.0", {"y.dlly"});
    ver("6.0", {"y.dllx"});
    fs::create_directories(fs::path(vroot + "\\7.0\\empty"));

    const std::string quirk = work + "\\quirk";
    for (const std::string& name :
        {"plain.dll", "upper.DLL", "noext", "long.dlly", "x.dllx", "dot.dot.dll"}) {
        WriteByte(quirk + "\\" + name);
    }

    // The sixteen .deps.json manifests (the probe's RegisterManifests).
    const std::string tfm = ".NETCoreApp,Version=v6.0";
    auto manifest = [&](const std::string& tag, const std::string& text) {
        WriteText(work + "\\apps\\" + tag + "\\" + tag + ".deps.json", text);
    };
    manifest("s1", R"json({
  "targets": {
    ".NETCoreApp,Version=v6.0": {
      "first/1.0.0": { "runtime": { "lib/net6.0/First.Library.dll": { } } },
      "second/2.0.0": { "runtime": { "runtimes/win-x64/lib/netstandard2.0/Second.dll": { } } }
    }
  },
  "libraries": {
    "first/1.0.0": { "type": "package", "path": "first/1.0.0" },
    "second/2.0.0": { "type": "package", "path": "second/2.0.0" }
  }
})json");
    manifest("s2", R"json({
  "targets": { ".NETCoreApp,Version=v6.0": { "unknownver/9.9.9": { "runtime": { "lib/net6.0/UnknownVer.Lib.dll": { } } } } },
  "libraries": { "unknownver": { "type": "package", "path": "unknownver" } }
})json");
    manifest("s3", "{ \"libraries\": { } }");
    manifest("s4", "[1, 2, 3]");
    manifest("s5", R"json({ "targets": { ".NETCoreApp,Version=v9.9.9": { } }, "libraries": { "first/1.0.0": { "type": "package", "path": "x" } } })json");
    manifest("s6", R"json({ "targets": { ".NETCoreApp,Version=v6.0": { } } })json");
    manifest("s7", R"json({ "targets": { ".NETCoreApp,Version=v6.0": { } }, "libraries": { "first/1.0.0": 5 } })json");
    manifest("s8", "{ \"targets\": }");
    manifest("s9", "{ \"a\": 1, \"a\": 2 }");
    manifest("s10", "");
    manifest("s11",
        "// leading comment\n"
        "{ \"targets\": { \".NETCoreApp,Version=v6.0\": { \"first/1.0.0\": { \"runtime\": { \"lib/net6.0/First.Library.dll\": { } } } } },\n"
        "  \"libraries\": { \"first/1.0.0\": { \"type\": \"package\", \"path\": \"first/1.0.0\" } } } /* tail */ garbage");
    manifest("s12", R"json({ "targets": { ".NETCoreApp,Version=v6.0": {
      "good/1.0.0": { "runtime": { "lib/net6.0/Empty.dll": { } } },
      "bad/1.0.0": { "runtime": "not-an-object" },
      "missing/1.0.0": { } } },
  "libraries": {
    "good/1.0.0": { "type": "package", "path": "good" },
    "bad/1.0.0": { "type": "package", "path": "bad" },
    "missing/1.0.0": { "type": "package", "path": "missing" },
    "notargets/1.0.0": { "type": "package", "path": "nt" } } })json");
    manifest("s13",
        "{ \"targets\": { \".NETCoreApp,Version=v6.0\": { } }, "
        "\"libraries\": { \"first/1.0.0\": { \"type\": 7, \"path\": true } }, }");
    manifest("s14", R"json({ "targets": { ".NETCoreApp,Version=v6.0": { "x/1.0.0": { "runtime": { "": { } } } } },
  "libraries": { "x/1.0.0": { "type": "package", "path": "x" } } })json");
    manifest("s15", "null");
    manifest("s16",
        "{ \"targets\": { \".NETCoreApp,Version=v6.0\": { \"deep/1.2.3\": { \"runtime\": { \"lib/net8.0/Deep.Lib.dll\": { } } } } }, "
        "\"libraries\": { \"deep/1.2.3\": { \"type\": \"package\", \"path\": \"deep\" } } }");
    // A truncated structure: the TextScanner Read at EOF.
    manifest("s17", "{ \"targets\":");
    // s13 minus the trailing comma: the non-string type/path loads fine
    // (the dead AsString fields), and "first/1.0.0" is not in targets.
    manifest("s18",
        "{ \"targets\": { \".NETCoreApp,Version=v6.0\": { } }, "
        "\"libraries\": { \"first/1.0.0\": { \"type\": 7, \"path\": true } } }");
}

}  // namespace

// ---------------------------------------------------------------------------
// ParseTargetFramework (the T gold section).

TEST(ParseTargetFrameworkTest, ZeroVersionRendersAllFourComponents) {
    EXPECT_EQ("0.0.0.0", ILSpy::Decompiler::Metadata::ZeroVersion().ToString());
}

TEST(ParseTargetFrameworkTest, EmptyInputYieldsNetFrameworkZero) {
    ParsedTargetFramework parsed = ParseTargetFramework("");
    EXPECT_EQ(TargetFrameworkIdentifier::NETFramework, parsed.Identifier);
    EXPECT_EQ(Version(0, 0, 0, 0), parsed.ParsedVersion);
}

TEST(ParseTargetFrameworkTest, NetCoreAppMatrix) {
    struct Case {
        const char* input;
        TargetFrameworkIdentifier identifier;
        Version version;
    };
    const Case cases[] = {
        {".NETCoreApp,Version=v6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".NETCoreApp,Version=v5.0", TargetFrameworkIdentifier::NET, Version(5, 0, 0)},
        {".NETCoreApp,Version=v10.0", TargetFrameworkIdentifier::NET, Version(10, 0, 0)},
        {".NETCoreApp,Version=v4.7.2", TargetFrameworkIdentifier::NETCoreApp, Version(4, 7, 2)},
        {".NETCoreApp,Version=v6.0.1", TargetFrameworkIdentifier::NET, Version(6, 0, 1)},
        // The revision is dropped: v6.0.0.99 -> 6.0.0.
        {".NETCoreApp,Version=v6.0.0.99", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
    };
    for (const Case& c : cases) {
        ParsedTargetFramework parsed = ParseTargetFramework(c.input);
        EXPECT_EQ(c.identifier, parsed.Identifier) << c.input;
        EXPECT_EQ(c.version, parsed.ParsedVersion) << c.input;
    }
}

TEST(ParseTargetFrameworkTest, ParseFailureMatrixYieldsZeroVersion) {
    const char* cases[] = {
        ".NETCoreApp,Version=6",        // a single component fails
        ".NETCoreApp,Version=v1.2.3.4.5", // five components fail
        ".NETCoreApp,Version=abc",
        ".NETCoreApp,Version=",
        ".NETCoreApp,Version",          // no '=' -> the token is skipped
        ".NETCoreApp,=v6.0",            // an empty key never matches
        ".NETCoreApp,Version=v6.0=extra", // three split parts -> skipped
        ".NETCoreApp,Version=-v6.0",    // a negative component fails
    };
    for (const char* input : cases) {
        ParsedTargetFramework parsed = ParseTargetFramework(input);
        EXPECT_EQ(TargetFrameworkIdentifier::NETCoreApp, parsed.Identifier) << input;
        EXPECT_EQ(Version(0, 0, 0, 0), parsed.ParsedVersion) << input;
    }
}

TEST(ParseTargetFrameworkTest, WhitespaceAndCaseMatrix) {
    struct Case {
        const char* input;
        TargetFrameworkIdentifier identifier;
        Version version;
    };
    const Case cases[] = {
        {".NETCoreApp,Version=v 6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".NETCoreApp, Version = v6.0 ", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".netcoreapp,version=v6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".NETCOREAPP,VERSION=V6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        // The TrimStart char set strips BOTH v's, the sign is a per-component
        // NumberStyles.Integer shape, and the leading space survives to the
        // tolerant component parse.
        {".NETCoreApp,Version=vv6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".NETCoreApp,Version=+6.0", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
        {".NETCoreApp,Version=v6.0,Extra=1", TargetFrameworkIdentifier::NET, Version(6, 0, 0)},
    };
    for (const Case& c : cases) {
        ParsedTargetFramework parsed = ParseTargetFramework(c.input);
        EXPECT_EQ(c.identifier, parsed.Identifier) << c.input;
        EXPECT_EQ(c.version, parsed.ParsedVersion) << c.input;
    }
}

TEST(ParseTargetFrameworkTest, IdentifierMatrix) {
    struct Case {
        const char* input;
        TargetFrameworkIdentifier identifier;
        Version version;
    };
    const Case cases[] = {
        {" .NETSTANDARD ,Version=v2.0", TargetFrameworkIdentifier::NETStandard, Version(2, 0, 0)},
        {".NETStandard,Version=v2.1", TargetFrameworkIdentifier::NETStandard, Version(2, 1, 0)},
        {".NETStandard,Version=v2.0", TargetFrameworkIdentifier::NETStandard, Version(2, 0, 0)},
        {".NETStandard", TargetFrameworkIdentifier::NETStandard, Version(0, 0, 0, 0)},
        {".NETFramework,Version=v4.8", TargetFrameworkIdentifier::NETFramework, Version(4, 8, 0)},
        {"Silverlight,Version=v5.0", TargetFrameworkIdentifier::Silverlight, Version(5, 0, 0)},
        {"silverlight,version=v5.0", TargetFrameworkIdentifier::Silverlight, Version(5, 0, 0)},
        {"junk,Version=v6.0", TargetFrameworkIdentifier::NETFramework, Version(6, 0, 0)},
        // The whole string is one token when there is no comma: the head
        // never matches and the identifier falls back to NETFramework.
        {".NETCoreApp;Version=v6.0", TargetFrameworkIdentifier::NETFramework, Version(0, 0, 0, 0)},
        {"junk", TargetFrameworkIdentifier::NETFramework, Version(0, 0, 0, 0)},
        {"junk,a=b", TargetFrameworkIdentifier::NETFramework, Version(0, 0, 0, 0)},
    };
    for (const Case& c : cases) {
        ParsedTargetFramework parsed = ParseTargetFramework(c.input);
        EXPECT_EQ(c.identifier, parsed.Identifier) << c.input;
        EXPECT_EQ(c.version, parsed.ParsedVersion) << c.input;
    }
}

TEST(ParseTargetFrameworkTest, LongSFoldsIntoNetStandard) {
    // U+017F (long s) is the only BMP unit whose invariant uppercase is an
    // ASCII letter, so ".NET\u017FTANDARD" matches ".NETSTANDARD".
    ParsedTargetFramework parsed = ParseTargetFramework(".NET\xC5\xBF" "TANDARD,Version=v2.0");
    EXPECT_EQ(TargetFrameworkIdentifier::NETStandard, parsed.Identifier);
    EXPECT_EQ(Version(2, 0, 0), parsed.ParsedVersion);
}

// ---------------------------------------------------------------------------
// ReferenceLoadInfo (the L gold section).

TEST(ReferenceLoadInfoTest, AddMessageAccumulatesInOrder) {
    ReferenceLoadInfo info;
    info.AddMessage("A", MessageKind::Warning, "w1");
    info.AddMessage("A", MessageKind::Error, "e1");
    info.AddMessage("A", MessageKind::Warning, "w1");
    ASSERT_EQ(1u, info.Entries().size());
    const auto* a = info.Entries()[0];
    EXPECT_EQ("A", a->FullName());
    EXPECT_TRUE(a->HasErrors());
    ASSERT_EQ(3u, a->Messages().size());
    EXPECT_EQ(MessageKind::Warning, a->Messages()[0].first);
    EXPECT_EQ("w1", a->Messages()[0].second);
    EXPECT_EQ(MessageKind::Error, a->Messages()[1].first);
    EXPECT_EQ("e1", a->Messages()[1].second);
    EXPECT_EQ(MessageKind::Warning, a->Messages()[2].first);
}

TEST(ReferenceLoadInfoTest, AddMessageOnceDedupMatrix) {
    ReferenceLoadInfo info;
    info.AddMessageOnce("B", MessageKind::Warning, "w1");   // created
    info.AddMessageOnce("B", MessageKind::Warning, "w1");   // both equal -> skipped
    info.AddMessageOnce("B", MessageKind::Error, "w1");     // text equal -> skipped
    info.AddMessageOnce("B", MessageKind::Error, "e2");     // both differ -> appended
    info.AddMessageOnce("B", MessageKind::Error, "e2");     // both equal -> skipped
    info.AddMessageOnce("B", MessageKind::Error, "e1");     // kind equal -> skipped
    info.AddMessage("C", MessageKind::Info, "i1");
    ASSERT_EQ(2u, info.Entries().size());
    const auto* b = info.Entries()[0];
    EXPECT_EQ("B", b->FullName());
    ASSERT_EQ(2u, b->Messages().size());
    EXPECT_EQ(MessageKind::Warning, b->Messages()[0].first);
    EXPECT_EQ("w1", b->Messages()[0].second);
    EXPECT_EQ(MessageKind::Error, b->Messages()[1].first);
    EXPECT_EQ("e2", b->Messages()[1].second);
    EXPECT_TRUE(b->HasErrors());
    EXPECT_FALSE(info.Entries()[1]->HasErrors());
}

TEST(ReferenceLoadInfoTest, TryGetInfoAndHasErrors) {
    ReferenceLoadInfo info;
    info.AddMessage("A", MessageKind::Error, "e1");
    const auto* found = info.TryGetInfo("A");
    ASSERT_NE(nullptr, found);
    EXPECT_EQ("A", found->FullName());
    EXPECT_TRUE(found->HasErrors());
    EXPECT_EQ(nullptr, info.TryGetInfo("D"));
    EXPECT_TRUE(info.HasErrors());
    ReferenceLoadInfo empty;
    EXPECT_FALSE(empty.HasErrors());
    EXPECT_TRUE(empty.Entries().empty());
}

// ---------------------------------------------------------------------------
// DotNetCorePathFinder (the C and R gold sections).

class DotNetCorePathFinderTest : public ::testing::Test {
protected:
    void SetUp() override {
#if defined(_WIN32)
        if (!fs::exists(fs::path("C:\\temp-probe"))) {
            GTEST_SKIP() << "fixture root not creatable on this host";
        }
#endif
        // The layout is deterministic and no test mutates it, so it is
        // built ONCE per process (a per-test rebuild churns ~45 files per
        // SetUp, which the machine's real-time antivirus filter throttles
        // after a few hundred create/delete cycles -- an environmental
        // constraint, not a port behavior).
        static bool layoutBuilt = false;
        if (!layoutBuilt) {
            BuildLayout();
            layoutBuilt = true;
        }
        SetNuGetPackages((std::string(WorkRoot()) + "\\nuget").c_str());
    }
};

TEST_F(DotNetCorePathFinderTest, MissingDepsWarningAndSearchPathResolution) {
    ReferenceLoadInfo loadInfo;
    DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\app\\app.dll",
        ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0), &loadInfo);
    // C|warn|app|Warning|app.deps.json could not be found!
    ASSERT_EQ(1u, loadInfo.Entries().size());
    const auto* entry = loadInfo.Entries()[0];
    EXPECT_EQ("app", entry->FullName());
    ASSERT_EQ(1u, entry->Messages().size());
    EXPECT_EQ(MessageKind::Warning, entry->Messages()[0].first);
    EXPECT_EQ("app.deps.json could not be found!", entry->Messages()[0].second);

    AssemblyNameReference sibling = AssemblyNameReference::Parse("Sibling");
    auto dll = finder.TryResolveDotNetCore(sibling);
    ASSERT_TRUE(dll.has_value());
    EXPECT_EQ(std::string(WorkRoot()) + "\\app\\Sibling.dll", *dll);
    AssemblyNameReference exeSibling = AssemblyNameReference::Parse("ExeSibling");
    auto exe = finder.TryResolveDotNetCore(exeSibling);
    ASSERT_TRUE(exe.has_value());
    EXPECT_EQ(std::string(WorkRoot()) + "\\app\\ExeSibling.exe", *exe);
    EXPECT_FALSE(finder.TryResolveDotNetCore(
        AssemblyNameReference::Parse("NotThere")).has_value());
}

TEST_F(DotNetCorePathFinderTest, AddRemoveSearchDirectoryAndNullSearch) {
    DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\app\\app.dll",
        ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
    finder.AddSearchDirectory(std::string(WorkRoot()) + "\\versions\\1.0");
    auto resolved = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("x"));
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::string(WorkRoot()) + "\\versions\\1.0\\x.dll", *resolved);
    finder.RemoveSearchDirectory(std::string(WorkRoot()) + "\\versions\\1.0");
    EXPECT_FALSE(finder.TryResolveDotNetCore(
        AssemblyNameReference::Parse("x")).has_value());
    // A null search path: the probe's Path.Combine throws
    // ArgumentNullException('path1').
    finder.AddSearchDirectory(std::nullopt);
    EXPECT_THROW(finder.TryResolveDotNetCore(
        AssemblyNameReference::Parse("NotThere")), std::invalid_argument);
}

TEST_F(DotNetCorePathFinderTest, DepsJsonHappyPaths) {
    struct Case {
        const char* tag;
        const char* name;
        std::optional<std::string> expected;  // relative to the work root
    };
    // C|s1|first / C|s1|second / C|s11|first / C|s16|deep (all lowercased --
    // the package base paths carry ToLowerInvariant).
    const std::string root = "c:\\temp-probe\\dncpfprobe\\work";
    {
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s1\\s1.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        auto first = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("First.Library"));
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(root + "\\nuget\\first\\1.0.0\\lib\\net6.0\\First.Library.dll", *first);
        auto second = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("Second"));
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(root + "\\nuget\\second\\2.0.0\\runtimes\\win-x64\\lib\\netstandard2.0\\Second.dll",
            *second);
        // The unreferenced packages never contribute base paths, and the
        // shared fallback still resolves System.Runtime.
        EXPECT_FALSE(finder.TryResolveDotNetCore(
            AssemblyNameReference::Parse("ExeOnly")).has_value());
        auto shared = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("System.Runtime"));
        ASSERT_TRUE(shared.has_value());
        EXPECT_EQ("C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
            *shared);
    }
    {
        // s11: the leading comment, the trailing block comment and garbage.
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s11\\s11.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        auto first = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("First.Library"));
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(root + "\\nuget\\first\\1.0.0\\lib\\net6.0\\First.Library.dll", *first);
    }
    {
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s16\\s16.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        auto deep = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("Deep.Lib"));
        ASSERT_TRUE(deep.has_value());
        EXPECT_EQ(root + "\\nuget\\deep\\1.2.3\\lib\\net8.0\\Deep.Lib.dll", *deep);
    }
    {
        // s12: only the good package contributes a base path (the string
        // runtime, the missing runtime, and the not-in-targets library all
        // yield the empty component set).
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s12\\s12.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        auto empty = finder.TryResolveDotNetCore(AssemblyNameReference::Parse("Empty"));
        ASSERT_TRUE(empty.has_value());
        EXPECT_EQ(root + "\\nuget\\good\\1.0.0\\lib\\net6.0\\Empty.dll", *empty);
    }
}

TEST_F(DotNetCorePathFinderTest, DepsJsonEmptyPackageArms) {
    // s2/s5/s6/s18: no throw, no package base paths (the miss falls through
    // to the shared resolution).
    for (const char* tag : {"s2", "s5", "s6", "s18"}) {
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\" + tag + "\\" + tag + ".dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        EXPECT_FALSE(finder.TryResolveDotNetCore(
            AssemblyNameReference::Parse("First.Library")).has_value()) << tag;
        auto shared = finder.TryResolveDotNetCore(
            AssemblyNameReference::Parse("System.Runtime"));
        ASSERT_TRUE(shared.has_value()) << tag;
        EXPECT_EQ("C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
            *shared) << tag;
    }
}

TEST_F(DotNetCorePathFinderTest, DepsJsonFailureArms) {
    struct Case {
        const char* tag;
        const char* message;
    };
    const Case cases[] = {
        // s3/s4/s15: the JsonValue indexer over a non-object (a missing
        // "targets", an array root, the null literal).
        {"s3", "This value does not represent a JsonObject."},
        {"s4", "This value does not represent a JsonObject."},
        {"s7", "This value does not represent a JsonObject."},
        {"s15", "This value does not represent a JsonObject."},
        // s8: the malformed JSON.
        {"s8", "The parser encountered an invalid or unexpected character."},
        // s9: the duplicate object key.
        {"s9", "The parser encountered a JsonObject with duplicate keys."},
        // s10: the empty file (the value-start EOF).
        {"s10", "The string ended before a value could be parsed."},
        // s17: the truncated structure (the mid-token EOF).
        {"s17", "The string ended before a value could be parsed."},
    };
    for (const Case& c : cases) {
        try {
            DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\" + c.tag + "\\"
                    + c.tag + ".dll",
                ".NETCoreApp,Version=v6.0", std::nullopt,
                TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
            FAIL() << c.tag << ": expected the ctor to throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_EQ(c.message, std::string(ex.what())) << c.tag;
        }
    }
    // s14: the empty runtime component yields a null itemPath, and the
    // 4-argument combine throws with 'path4'.
    try {
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s14\\s14.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        FAIL() << "s14: expected the ctor to throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_EQ("Value cannot be null. (Parameter 'path4')", std::string(ex.what()));
    }
}

TEST_F(DotNetCorePathFinderTest, DepsJsonTrailingCommaDivergence) {
    // The documented LightJson/nlohmann divergence: LightJson's reader
    // accepts a trailing comma in an object (the gold's s13 scenario
    // constructs and resolves fine), while nlohmann rejects the file. The
    // port therefore throws where the real engine accepts -- unreachable
    // through real .deps.json producers (strict JSON).
    try {
        DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s13\\s13.dll",
            ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        FAIL() << "expected the documented trailing-comma divergence throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_EQ("The parser encountered an invalid or unexpected character.",
            std::string(ex.what()));
    }
}

TEST_F(DotNetCorePathFinderTest, RootAndBareParentArms) {
    // A root-form parent path: GetDirectoryName yields null and the ctor's
    // deps.json combine throws ArgumentNullException('path1').
    try {
        DotNetCorePathFinder finder("C:\\", ".NETCoreApp,Version=v6.0", std::nullopt,
            TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
        FAIL() << "expected the ctor to throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_EQ("Value cannot be null. (Parameter 'path1')", std::string(ex.what()));
    }
    // A bare file name: GetDirectoryName yields "" and the deps.json probe
    // is relative to the current directory (missing -> the warning). The
    // gold's second message on the same "app" entry comes from the earlier
    // app-dir construction sharing the loadInfo.
    ReferenceLoadInfo loadInfo;
    DotNetCorePathFinder appDir(std::string(WorkRoot()) + "\\app\\app.dll",
        ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0), &loadInfo);
    DotNetCorePathFinder finder("app.dll", ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0), &loadInfo);
    const auto* entry = loadInfo.TryGetInfo("app");
    ASSERT_NE(nullptr, entry);
    EXPECT_EQ(2u, entry->Messages().size());
    EXPECT_EQ("app.deps.json could not be found!", entry->Messages().back().second);
}

TEST_F(DotNetCorePathFinderTest, NetStandard21RewritesTargetFrameworkVersion) {
    DotNetCorePathFinder std21(TargetFrameworkIdentifier::NETStandard, Version(2, 1, 0),
        std::nullopt);
    EXPECT_EQ(Version(3, 0, 0), std21.TargetFrameworkVersionForTest());
    DotNetCorePathFinder std20(TargetFrameworkIdentifier::NETStandard, Version(2, 0, 0),
        std::nullopt);
    EXPECT_EQ(Version(2, 0, 0), std20.TargetFrameworkVersionForTest());
}

TEST_F(DotNetCorePathFinderTest, NoNuGetPackagesVariableSkipsTheLookup) {
    SetNuGetPackages("");
    DotNetCorePathFinder finder(std::string(WorkRoot()) + "\\apps\\s1\\s1.dll",
        ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
    EXPECT_FALSE(finder.TryResolveDotNetCore(
        AssemblyNameReference::Parse("First.Library")).has_value());
    // The whitespace-only variable is skipped the same way.
    SetNuGetPackages("   ");
    DotNetCorePathFinder whitespace(std::string(WorkRoot()) + "\\apps\\s1\\s1.dll",
        ".NETCoreApp,Version=v6.0", std::nullopt,
        TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0));
    EXPECT_FALSE(whitespace.TryResolveDotNetCore(
        AssemblyNameReference::Parse("First.Library")).has_value());
}

TEST_F(DotNetCorePathFinderTest, FindDotNetExeDirectoryScansPath) {
    // R|dotnetExeDir (the machine's PATH-resolved dotnet directory).
    auto dir = DotNetCorePathFinder::FindDotNetExeDirectory();
    ASSERT_TRUE(dir.has_value());
    EXPECT_EQ("C:\\Program Files\\dotnet", *dir);
}

TEST_F(DotNetCorePathFinderTest, ConvertToVersionMatrix) {
    struct Case {
        const char* name;
        std::optional<Version> version;
    };
    const Case cases[] = {
        {"10.0.8", Version(10, 0, 8)},
        {"1.0-rc1", Version(1, 0)},
        {"-weird", std::nullopt},     // the dash at 0 stays -> the parse fails
        {"abc", std::nullopt},
        {"1.2.3.4", Version(1, 2, 3, 4)},
        {"1", std::nullopt},          // a single component fails
        {"1.2", Version(1, 2)},
        {"3.0-beta.1", Version(3, 0)},
        {" 1.0", Version(1, 0)},      // the per-component whitespace shape
        {"1.0.8-preview.7.32", Version(1, 0, 8)},
        {"v2.0", std::nullopt},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(c.version.has_value(),
            DotNetCorePathFinder::ConvertToVersion(c.name).has_value()) << c.name;
        if (c.version.has_value()) {
            EXPECT_EQ(*c.version, *DotNetCorePathFinder::ConvertToVersion(c.name)) << c.name;
        }
    }
}

TEST_F(DotNetCorePathFinderTest, GetClosestVersionFolderMatrix) {
    const std::string vroot = std::string(WorkRoot()) + "\\versions";
    struct Case {
        const char* requested;
        const char* expected;
    };
    const Case cases[] = {
        {"0.9", "1.0"},
        {"1.5", "2.0"},
        // The 2.5 folder has no dll -> the 3.0-beta1 folder wins.
        {"2.6", "3.0-beta1"},
        {"2.5", "3.0-beta1"},
        {"3.2", "4.0"},   // the nested dll counts (AllDirectories)
        // The 5.0 (y.dlly) and 6.0 (y.dllx) folders do NOT match *.dll
        // (Simple matching, no DOS 8.3 quirk) and 7.0 is empty.
        {"5.0", "10.0.8"},
        {"6.0", "10.0.8"},
        {"7.0", "10.0.8"},
        {"99.0", "99.0"}, // the fallback is version.ToString()
    };
    for (const Case& c : cases) {
        Version requested(0, 0);
        ASSERT_NO_THROW(requested = Version(c.requested)) << c.requested;
        EXPECT_EQ(c.expected,
            DotNetCorePathFinder::GetClosestVersionFolder(vroot, requested)) << c.requested;
    }
    try {
        DotNetCorePathFinder::GetClosestVersionFolder(
            std::string(WorkRoot()) + "\\missing", Version(1, 0, 0));
        FAIL() << "expected the missing-directory throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_EQ("Could not find a part of the path '"
            + std::string(WorkRoot()) + "\\missing'.", std::string(ex.what()));
    }
}

TEST_F(DotNetCorePathFinderTest, SharedFrameworkResolution) {
    auto resolveShared = [](Version targetVersion, std::optional<std::string> preferred,
        const char* name, std::optional<std::string>& runtimePack) {
        DotNetCorePathFinder finder(TargetFrameworkIdentifier::NETCoreApp, targetVersion,
            std::move(preferred));
        return finder.TryResolveDotNetCoreShared(AssemblyNameReference::Parse(name),
            runtimePack);
    };
    {
        std::optional<std::string> pack;
        auto file = resolveShared(Version(6, 0, 0), std::nullopt, "System.Runtime", pack);
        ASSERT_TRUE(file.has_value());
        EXPECT_EQ("C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
            *file);
        EXPECT_EQ("Microsoft.NETCore.App", pack.value_or(""));
    }
    {
        // The preferred pack heads the list (and the builtin packs follow).
        std::optional<std::string> pack;
        auto file = resolveShared(Version(6, 0, 0), "Microsoft.WindowsDesktop.App",
            "PresentationFramework", pack);
        ASSERT_TRUE(file.has_value());
        EXPECT_EQ("C:\\Program Files\\dotnet\\shared\\Microsoft.WindowsDesktop.App\\10.0.8\\PresentationFramework.dll",
            *file);
        EXPECT_EQ("Microsoft.WindowsDesktop.App", pack.value_or(""));
    }
    {
        // A version above every installed runtime: the closest-version
        // fallback misses and the out pack ends null.
        std::optional<std::string> pack = "sentinel";
        auto file = resolveShared(Version(99, 0, 0), std::nullopt, "System.Runtime", pack);
        EXPECT_FALSE(file.has_value());
        EXPECT_FALSE(pack.has_value());
    }
    {
        std::optional<std::string> pack;
        auto file = resolveShared(Version(10, 0, 8), std::nullopt, "Microsoft.CSharp", pack);
        ASSERT_TRUE(file.has_value());
        EXPECT_EQ("C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\Microsoft.CSharp.dll",
            *file);
    }
}

TEST_F(DotNetCorePathFinderTest, GetReferenceAssemblyPathMatrix) {
    auto refAsm = [](const char* tfm) {
        DotNetCorePathFinder finder(TargetFrameworkIdentifier::NETCoreApp, Version(6, 0, 0),
            std::nullopt);
        return finder.GetReferenceAssemblyPath(tfm);
    };
    // ".NETCoreApp,Version=v6.0" parses as NET (>= 5), so the ext is net6.0.
    EXPECT_EQ("C:\\Program Files\\dotnet\\packs\\Microsoft.NETCore.App.Ref\\10.0.8\\ref\\net6.0",
        refAsm(".NETCoreApp,Version=v6.0"));
    EXPECT_EQ("C:\\Program Files\\dotnet\\packs\\Microsoft.NETCore.App.Ref\\10.0.8\\ref\\netcoreapp3.1",
        refAsm(".NETCoreApp,Version=v3.1"));
    // ".NET" is not a recognized identifier head -> NotSupportedException.
    EXPECT_THROW(refAsm(".NET,Version=v5.0"), std::logic_error);
    // The NETStandard pack is not installed -> DirectoryNotFoundException.
    try {
        refAsm(".NETStandard,Version=v2.0");
        FAIL() << "expected the missing-pack throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_EQ("Could not find a part of the path "
            "'C:\\Program Files\\dotnet\\packs\\NETStandard.Library.Ref'.",
            std::string(ex.what()));
    }
    EXPECT_THROW(refAsm("Silverlight,Version=v5.0"), std::logic_error);
    EXPECT_THROW(refAsm(""), std::logic_error);
    // A version above the pack: the fallback folder is the PARSED version's
    // ToString (99.0.0) and the ext the NET arm's net99.0.
    EXPECT_EQ("C:\\Program Files\\dotnet\\packs\\Microsoft.NETCore.App.Ref\\99.0.0\\ref\\net99.0",
        refAsm(".NETCoreApp,Version=v99.0"));
}
