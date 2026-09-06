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

// The UniversalAssemblyResolver instance-surface tests (the ctor's directory
// bookkeeping, the search-directory trio, IsSharedAssembly through the lazy
// DotNetCorePathFinder, the FindAssemblyFile target-framework dispatch with
// the full ResolveInternal chain, the throwOnError arms, the winmd arms, and
// the FindClosestVersionDirectory picker) -- every expectation dumped from
// the real installed ICSharpCode.Decompiler 11.0 (the C:/temp-probe/UarProbe
// gold probe) over THIS machine's framework directories, GAC, Windows Kits
// references, and .NET 10 shared-framework install:
//   * the I1 option-enum member tables (PEStreamOptions/MetadataReaderOptions)
//   and the Version nullable-comparison quirks the chain consumes;
//   * the I2 ctor/search-directory drives (the cwd-dependent arms the test
//   derives from its own current directory -- the same
//   Path.GetDirectoryName null/whitespace -> CurrentDirectory relation);
//   * the I3 IsSharedAssembly drives and the lazy-add plumbing;
//   * the I4 FindAssemblyFile drives over the .NET Framework target and the
//   throwOnError arms;
//   * the I5 target-framework dispatch arms;
//   * the I6 winmd resolution arms (through the IsWindowsRuntime stub the
//   C# probe uses);
//   * the I7 FindClosestVersionDirectory crafted matrix over the version
//   folder layout the test builds.

#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "TestFixtures/UarInstanceGold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;
using ILSpy::Tests::kUarAfterLazyDirsGold;
using ILSpy::Tests::kUarClosestVersionGold;
using ILSpy::Tests::kUarDirsGold;
using ILSpy::Tests::kUarDispatchGold;
using ILSpy::Tests::kUarFrameworkChainGold;
using ILSpy::Tests::kUarOptionEnumGold;
using ILSpy::Tests::kUarSharedGold;
using ILSpy::Tests::kUarThrowGold;
using ILSpy::Tests::kUarWinmdGold;

// The machine fixture roots (the probe's own layout paths -- the standing
// C:\temp-probe scratch area the machine's gold probes share).
constexpr const char* kInstanceDir = "C:\\temp-probe\\uar_instance";
constexpr const char* kVersionsDir = "C:\\temp-probe\\uar_versions";

std::string JoinPath(const std::string& first, const char* second) {
    std::string result = first;
    if (!result.empty() && result.back() != '\\') result += '\\';
    result += second;
    return result;
}

void WriteAllText(const std::string& path, const char* text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

// The .NET Path.GetDirectoryName over the fs path (the tests' own
// expectation derivation for the cwd-dependent ctor arms -- the same
// relation the implementation pins over the probe's cwd).
std::string CurrentDirectory() {
    return fs::current_path().string();
}

// Builds the instance fixture directory once: Bar.dll / Baz.exe beside the
// main.dll the resolver ctors take, plus the added/ directory the lazy-add
// drive resolves LATER.dll through.
void BuildInstanceLayout() {
    std::error_code ec;
    fs::remove_all(fs::path(kInstanceDir), ec);
    fs::create_directories(fs::path(kInstanceDir), ec);
    WriteAllText(JoinPath(kInstanceDir, "Bar.dll"), "x");
    WriteAllText(JoinPath(kInstanceDir, "Baz.exe"), "x");
    WriteAllText(JoinPath(kInstanceDir, "main.dll"), "x");
    std::string added = JoinPath(kInstanceDir, "added");
    fs::create_directories(fs::path(added), ec);
    WriteAllText(JoinPath(added, "LATER.dll"), "x");
}

// Builds the version-folder layout the FindClosestVersionDirectory matrix
// probes: three plain versions, a suffix folder ConvertToVersion accepts
// ("1.5-beta" parses to 1.5), a non-version folder, a negative version, and
// an empty subdirectory.
void BuildVersionsLayout() {
    std::error_code ec;
    fs::remove_all(fs::path(kVersionsDir), ec);
    fs::create_directories(fs::path(kVersionsDir), ec);
    for (const char* name :
        {"1.0.0.0", "2.0.0.0", "3.0.0.0", "1.5-beta", "notaversion", "-2.0"}) {
        fs::create_directories(fs::path(JoinPath(kVersionsDir, name)), ec);
    }
    fs::create_directories(
        fs::path(JoinPath(kVersionsDir, "empty")), ec);
}

// The C# probe's `dumpDirs` render: "[a, b, <null>]".
std::string DirsToString(
    const std::vector<std::optional<std::string>>& dirs) {
    std::string joined;
    bool first = true;
    for (const auto& directory : dirs) {
        if (!first) joined += ", ";
        first = false;
        joined += directory ? *directory : std::string("<null>");
    }
    return "[" + joined + "]";
}

// The probe's StubRef equivalent for the winmd drives: a reference whose
// identity fields alias a parsed AssemblyNameReference with
// IsWindowsRuntime forced true.
class WinmdStubRef : public TM::IAssemblyReference {
public:
    explicit WinmdStubRef(const TM::AssemblyNameReference& parsed)
        : parsed_(parsed) {}

    std::string Name() const override { return parsed_.Name(); }
    std::string FullName() const override { return parsed_.FullName(); }
    std::optional<TS::Version> Version() const override {
        return parsed_.Version();
    }
    std::optional<std::string> Culture() const override {
        return parsed_.Culture();
    }
    std::optional<std::vector<std::uint8_t>> PublicKeyToken() const override {
        return parsed_.PublicKeyToken();
    }
    bool IsWindowsRuntime() const override { return true; }
    bool IsRetargetable() const override { return false; }

private:
    TM::AssemblyNameReference parsed_;
};

// The probe's tag|full drive rows (the I4 framework chain).
struct RefCase {
    const char* tag;
    const char* full;
};

// The exception-message render the probe uses (the CR/LF escapes).
std::string EscapeMessage(std::string message) {
    std::string out;
    for (char c : message) {
        if (c == '\r') {
            out += "\\r";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

class UniversalAssemblyResolverInstanceTest : public ::testing::Test {
protected:
    void SetUp() override {
#if defined(_WIN32)
        std::error_code ec;
        if (!fs::exists(fs::path("C:\\temp-probe"))) {
            GTEST_SKIP() << "fixture root not creatable on this host";
        }
        // The layouts are deterministic and no test mutates them, so they
        // are built ONCE per process (a per-test rebuild churns files per
        // SetUp, which the machine's real-time antivirus filter throttles
        // after a few hundred create/delete cycles -- the DotNetCorePathFinder
        // suite's standing environmental constraint).
        static bool layoutBuilt = false;
        if (!layoutBuilt) {
            BuildInstanceLayout();
            BuildVersionsLayout();
            layoutBuilt = true;
        }
#else
        GTEST_SKIP() << "Windows-only";
#endif
    }
};

// The I1 option-enum member tables (the ctor's PEStreamOptions and
// MetadataReaderOptions parameters) in the probe's render shape.
TEST_F(UniversalAssemblyResolverInstanceTest, OptionEnumTablesMatchTheGold) {
    // The gold lines carry the member NAMES; the drive renders every member
    // spelling with its value.
    std::vector<std::pair<const char*, int>> streamMembers = {
        {"Default", 0}, {"LeaveOpen", 1}, {"PrefetchMetadata", 2},
        {"PrefetchEntireImage", 4}, {"IsLoadedImage", 8}};
    std::vector<std::string> rendered;
    for (const auto& [name, value] : streamMembers) {
        rendered.push_back("I1|PEStreamOptions|" + std::string(name) + "=" +
            std::to_string(value));
    }
    std::vector<std::pair<const char*, int>> readerMembers = {
        {"None", 0}, {"Default", 1}, {"ApplyWindowsRuntimeProjections", 1}};
    for (const auto& [name, value] : readerMembers) {
        rendered.push_back("I1|MetadataReaderOptions|" + std::string(name) +
            "=" + std::to_string(value));
    }
    ASSERT_EQ(rendered.size(), kUarOptionEnumGold.size());
    for (std::size_t i = 0; i < rendered.size(); i++) {
        EXPECT_STREQ(rendered[i].c_str(),
            std::string(kUarOptionEnumGold[i]).c_str());
    }
    // The enum values back the rendered tables.
    EXPECT_EQ(static_cast<int>(TM::PEStreamOptions::Default), 0);
    EXPECT_EQ(static_cast<int>(TM::PEStreamOptions::LeaveOpen), 1);
    EXPECT_EQ(static_cast<int>(TM::PEStreamOptions::PrefetchMetadata), 2);
    EXPECT_EQ(static_cast<int>(TM::PEStreamOptions::PrefetchEntireImage), 4);
    EXPECT_EQ(static_cast<int>(TM::PEStreamOptions::IsLoadedImage), 8);
    EXPECT_EQ(static_cast<int>(TM::MetadataReaderOptions::None), 0);
    EXPECT_EQ(static_cast<int>(TM::MetadataReaderOptions::Default), 1);
    EXPECT_EQ(static_cast<int>(TM::MetadataReaderOptions::ApplyWindowsRuntimeProjections), 1);
}

// The I1 Version nullable-comparison quirks the ResolveInternal chain
// consumes (the C# nullable operator: a null version is less than every
// version).
TEST_F(UniversalAssemblyResolverInstanceTest, VersionNullableComparisonQuirks) {
    std::optional<TS::Version> nullVersion;
    EXPECT_TRUE(nullVersion <= TS::Version(4, 0, 0, 0));
    EXPECT_TRUE(nullVersion <= nullVersion);
    EXPECT_FALSE(TS::Version(4, 0, 0, 0) <= nullVersion);
    EXPECT_TRUE(TS::Version(4, 0, 0, 0) <= TS::Version(4, 0, 0, 0));
    // The I1 MajorRevision facts: an unspecified revision is -1, and the
    // 1.0.3300.216268800 revision's high word is 3300.
    EXPECT_EQ(TS::Version(1, 0).Revision, -1);
    EXPECT_EQ(TS::Version(1, 0, 0).Revision, -1);
    EXPECT_EQ(TS::Version(1, 0, 0, 216268800).Revision >> 16, 3300);
    EXPECT_EQ(TS::Version(1, 0, 0, 0).Revision >> 16, 0);
}

// The I2 ctor bookkeeping and the search-directory trio: the fixture lines
// byte-for-byte plus the cwd-dependent arms derived from the test's own
// current directory.
TEST_F(UniversalAssemblyResolverInstanceTest, CtorAndSearchDirectories) {
    const std::string cwd = CurrentDirectory();
    std::vector<std::string> observed;
    // I2|null-main|[] -- a null main assembly file leaves no search dir.
    TM::UniversalAssemblyResolver r0(std::nullopt, false, ".NETFramework,Version=v4.0");
    observed.push_back("I2|null-main|" + DirsToString(r0.GetSearchDirectories()));
    // I2|abs-main|[C:\some] -- the main file's directory.
    TM::UniversalAssemblyResolver r1(std::string("C:\\some\\main.dll"), false,
        ".NETFramework,Version=v4.0");
    observed.push_back("I2|abs-main|" + DirsToString(r1.GetSearchDirectories()));
    // I2|cwd -- the probe's current-directory marker line.
    observed.push_back("I2|cwd=" + cwd);
    // I2|rel-main/[root|ws]-main|[<cwd>] -- GetDirectoryName null/empty/
    // whitespace-only all fall back to Environment.CurrentDirectory.
    TM::UniversalAssemblyResolver r2(std::string("main.dll"), false,
        ".NETFramework,Version=v4.0");
    observed.push_back("I2|rel-main|" + DirsToString(r2.GetSearchDirectories()));
    TM::UniversalAssemblyResolver r3(std::string("C:\\"), false,
        ".NETFramework,Version=v4.0");
    observed.push_back("I2|root-main|" + DirsToString(r3.GetSearchDirectories()));
    TM::UniversalAssemblyResolver r4(std::string("   "), false,
        ".NETFramework,Version=v4.0");
    observed.push_back("I2|ws-main|" + DirsToString(r4.GetSearchDirectories()));
    // I2|unc-main|[\\server\share] -- the incomplete UNC path's directory.
    TM::UniversalAssemblyResolver r5(
        std::string("\\\\server\\\\share\\\\main.dll"), false,
        ".NETFramework,Version=v4.0");
    observed.push_back("I2|unc-main|" + DirsToString(r5.GetSearchDirectories()));
    // The Add/Remove sequence over the absolute-main resolver: a null entry
    // is stored, Remove takes the FIRST match (a null removes the first
    // null), and a missing entry is a no-op.
    r1.AddSearchDirectory(std::string("D1"));
    r1.AddSearchDirectory(std::nullopt);
    r1.RemoveSearchDirectory(std::string("C:\\some"));
    r1.RemoveSearchDirectory(std::string("never-was"));
    observed.push_back("I2|mutated|" + DirsToString(r1.GetSearchDirectories()));
    r1.RemoveSearchDirectory(std::nullopt);
    observed.push_back("I2|removed-null|" + DirsToString(r1.GetSearchDirectories()));

    // The five fixed gold lines in drive order (the fixture excludes the
    // three cwd-derived arms, which the mapping below skips).
    const std::size_t fixtureIndices[] = {0, 1, 6, 7, 8};
    ASSERT_EQ(observed.size(), 9u);
    for (std::size_t i = 0; i < kUarDirsGold.size(); i++) {
        EXPECT_STREQ(observed[fixtureIndices[i]].c_str(),
            std::string(kUarDirsGold[i]).c_str())
            << "line " << i;
    }
    // The three cwd-derived lines the fixture excludes.
    EXPECT_EQ(observed[3], "I2|rel-main|[" + cwd + "]");
    EXPECT_EQ(observed[4], "I2|root-main|[" + cwd + "]");
    EXPECT_EQ(observed[5], "I2|ws-main|[" + cwd + "]");
}

// The I3 IsSharedAssembly drives (through the lazy DotNetCorePathFinder,
// forcing it) and the lazy-add plumbing.
TEST_F(UniversalAssemblyResolverInstanceTest, IsSharedAssemblyAndLazyAdd) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    TM::UniversalAssemblyResolver rn(mainFile, false, ".NETCoreApp,Version=v10.0");
    std::vector<std::string> observed;
    const RefCase sharedRefs[] = {
        {"sysruntime",
            "System.Runtime, Version=10.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a"},
        {"mscorlib",
            "mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"bogus", "NoSuchAssemblyAnywhere, Version=1.0.0.0"},
    };
    for (const RefCase& refCase : sharedRefs) {
        TM::AssemblyNameReference parsed =
            TM::AssemblyNameReference::Parse(refCase.full);
        std::optional<std::string> pack;
        bool shared = rn.IsSharedAssembly(parsed, pack);
        observed.push_back(std::string("I3|") + refCase.tag + "|shared=" +
            (shared ? "True" : "False") + "|pack=" + (pack ? *pack : "<null>"));
    }
    // I2|after-lazy -- the resolver's OWN directories are unchanged by the
    // lazy materialization (the search-dir replay lives in the finder).
    observed.push_back("I2|after-lazy|" + DirsToString(rn.GetSearchDirectories()));
    // The lazy-add plumbing: a search directory added AFTER the finder was
    // materialized still reaches it.
    const std::string addDir = JoinPath(kInstanceDir, "added");
    rn.AddSearchDirectory(addDir);
    auto laterFile = rn.FindAssemblyFile(
        TM::AssemblyNameReference::Parse("LATER, Version=1.0.0.0"));
    observed.push_back("I3|lazy-add|" + (laterFile ? *laterFile : std::string("<null>")));

    // The three shared lines, then the after-lazy, then the lazy-add (the
    // fixture's shared array carries all four I3 lines).
    ASSERT_EQ(observed.size(), 5u);
    for (std::size_t i = 0; i < 3; i++) {
        EXPECT_STREQ(observed[i].c_str(),
            std::string(kUarSharedGold[i]).c_str())
            << "line " << i;
    }
    EXPECT_STREQ(observed[3].c_str(),
        std::string(kUarAfterLazyDirsGold[0]).c_str());
    EXPECT_STREQ(observed[4].c_str(),
        std::string(kUarSharedGold[3]).c_str());
}

// The I4 FindAssemblyFile drives over the .NET Framework target (the full
// ResolveInternal chain: the search directories, the special-version
// framework arm, the corlib arm, the GAC arm, the <= 4.0 fallback, and the
// shared-runtime last resort).
TEST_F(UniversalAssemblyResolverInstanceTest, FindAssemblyFileFrameworkChain) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    TM::UniversalAssemblyResolver rf(mainFile, false, ".NETFramework,Version=v4.0");
    const RefCase fwRefs[] = {
        {"inDir", "Bar, Version=1.0.0.0"},
        {"inDirExe", "Baz, Version=1.0.0.0"},
        {"msc4",
            "mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"msc2",
            "mscorlib, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"msc1-rev3300",
            "mscorlib, Version=1.0.3300.216268800, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"msc1",
            "mscorlib, Version=1.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"mscNoVer",
            "mscorlib, Culture=neutral, PublicKeyToken=b77a5c561934e089"},
        {"gac",
            "System.Configuration, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a"},
        {"sysruntime8",
            "System.Runtime, Version=8.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a"},
        {"sys4",
            "System, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"mscCF",
            "mscorlib, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=969db8053d3322ac"},
        {"msc99",
            "mscorlib, Version=99.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089"},
        {"missing", "NoSuchAssemblyAnywhere, Version=1.0.0.0"},
    };
    std::vector<std::string> observed;
    for (const RefCase& refCase : fwRefs) {
        auto file = rf.FindAssemblyFile(
            TM::AssemblyNameReference::Parse(refCase.full));
        observed.push_back(std::string("I4|") + refCase.tag + "|" +
            (file ? *file : std::string("<null>")));
    }
    ASSERT_EQ(observed.size(), kUarFrameworkChainGold.size());
    for (std::size_t i = 0; i < observed.size(); i++) {
        EXPECT_STREQ(observed[i].c_str(),
            std::string(kUarFrameworkChainGold[i]).c_str())
            << "line " << i;
    }
}

// The I4 throwOnError arms: the miss throws the ResolutionException message,
// the unsupported corlib versions throw the NotSupportedException renders
// (the port's runtime_error mapping of the C# type -- the message is what
// the drive pins).
TEST_F(UniversalAssemblyResolverInstanceTest, ThrowOnErrorArms) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    TM::UniversalAssemblyResolver rfT(mainFile, true, ".NETFramework,Version=v4.0");
    std::vector<std::string> observed;
    auto drive = [&](const char* tag, const char* full) {
        try {
            rfT.FindAssemblyFile(TM::AssemblyNameReference::Parse(full));
            observed.push_back(std::string("I4|") + tag + "|no-throw");
        } catch (const std::exception& ex) {
            observed.push_back(std::string("I4|") + tag + "|" +
                EscapeMessage(ex.what()));
        }
    };
    drive("throw-missing", "NoSuchAssemblyAnywhere, Version=1.0.0.0");
    drive("throw-99",
        "mscorlib, Version=99.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    drive("throw-cf",
        "mscorlib, Version=2.0.0.0, Culture=neutral, "
        "PublicKeyToken=969db8053d3322ac");
    ASSERT_EQ(observed.size(), kUarThrowGold.size());
    for (std::size_t i = 0; i < observed.size(); i++) {
        // The gold carries the C# exception TYPE NAME between the tag and
        // the message; the port's mapping of that type is std::runtime_error
        // (the NotSupportedException/ResolutionException messages are what
        // the drives pin), so the fixture's middle segment asserts the
        // mapping while the message text pins the render.
        std::string gold = std::string(kUarThrowGold[i]);
        const std::size_t first = gold.find('|');
        const std::size_t second = gold.find('|', first + 1);
        const std::size_t third = gold.find('|', second + 1);
        ASSERT_NE(third, std::string::npos) << gold;
        const std::string tagName = gold.substr(first + 1, second - first - 1);
        const std::string typeName =
            gold.substr(second + 1, third - second - 1);
        const std::string message = gold.substr(third + 1);
        const std::string observedTag = observed[i].substr(
            0, observed[i].find('|', 3) + 1);
        EXPECT_EQ("I4|" + tagName + "|", observedTag) << observed[i];
        // The ResolutionException/NotSupportedException type mapping.
        EXPECT_TRUE(typeName == "ResolutionException" ||
            typeName == "NotSupportedException")
            << typeName;
        EXPECT_EQ(message, observed[i].substr(observedTag.size()))
            << observed[i];
    }
}

// The I5 target-framework dispatch arms.
TEST_F(UniversalAssemblyResolverInstanceTest, TargetFrameworkDispatch) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    std::vector<std::string> observed;
    TM::UniversalAssemblyResolver rc(mainFile, false, ".NETCoreApp,Version=v10.0");
    auto drive = [&](const char* tag, const char* full) {
        auto file = rc.FindAssemblyFile(TM::AssemblyNameReference::Parse(full));
        observed.push_back(std::string("I5|") + tag + "|" +
            (file ? *file : std::string("<null>")));
    };
    drive("core-sysruntime",
        "System.Runtime, Version=10.0.0.0, Culture=neutral, "
        "PublicKeyToken=b03f5f7f11d50a3a");
    drive("core-msc2",
        "mscorlib, Version=2.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    TM::UniversalAssemblyResolver rz(mainFile, false, ".NETCoreApp,Version=v0.0");
    auto driveZ = [&](const char* tag, const char* full) {
        auto file = rz.FindAssemblyFile(TM::AssemblyNameReference::Parse(full));
        observed.push_back(std::string("I5|") + tag + "|" +
            (file ? *file : std::string("<null>")));
    };
    driveZ("gate-sysruntime",
        "System.Runtime, Version=10.0.0.0, Culture=neutral, "
        "PublicKeyToken=b03f5f7f11d50a3a");
    TM::UniversalAssemblyResolver rs(mainFile, false, "Silverlight,Version=v5.0");
    auto file = rs.FindAssemblyFile(TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089"));
    observed.push_back("I5|sl-msc4|" +
        (file ? *file : std::string("<null>")));
    ASSERT_EQ(observed.size(), kUarDispatchGold.size());
    for (std::size_t i = 0; i < observed.size(); i++) {
        EXPECT_STREQ(observed[i].c_str(),
            std::string(kUarDispatchGold[i]).c_str())
            << "line " << i;
    }
}

// The I6 winmd resolution arms (through the IsWindowsRuntime stub).
TEST_F(UniversalAssemblyResolverInstanceTest, WinmdResolutionArms) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    TM::UniversalAssemblyResolver rw(mainFile, false, ".NETFramework,Version=v4.0");
    const RefCase winmdRefs[] = {
        {"contract", "Windows.Foundation.UniversalApiContract, Version=19.0.0.0"},
        {"contract99", "Windows.Foundation.UniversalApiContract, Version=99.0.0.0"},
        {"contractNoVer", "Windows.Foundation.UniversalApiContract"},
        {"sysdir", "Windows.Foundation, Version=1.0.0.0"},
        {"bogusWinmd", "NoSuchContractAnywhere, Version=1.0.0.0"},
    };
    std::vector<std::string> observed;
    for (const RefCase& refCase : winmdRefs) {
        WinmdStubRef stub(TM::AssemblyNameReference::Parse(refCase.full));
        auto file = rw.FindAssemblyFile(stub);
        observed.push_back(std::string("I6|") + refCase.tag + "|" +
            (file ? *file : std::string("<null>")));
    }
    ASSERT_EQ(observed.size(), kUarWinmdGold.size());
    for (std::size_t i = 0; i < observed.size(); i++) {
        EXPECT_STREQ(observed[i].c_str(),
            std::string(kUarWinmdGold[i]).c_str())
            << "line " << i;
    }
}

// The I7 FindClosestVersionDirectory crafted matrix over the version-folder
// layout.
TEST_F(UniversalAssemblyResolverInstanceTest, FindClosestVersionMatrix) {
    const std::string mainFile = JoinPath(kInstanceDir, "main.dll");
    TM::UniversalAssemblyResolver rf(mainFile, false, ".NETFramework,Version=v4.0");
    std::vector<std::string> observed;
    const std::vector<std::pair<std::string, std::optional<TS::Version>>> cases = {
        {std::string(kVersionsDir), TS::Version(2, 5)},
        {std::string(kVersionsDir), TS::Version(1, 5)},
        {std::string(kVersionsDir), std::nullopt},
        {std::string(kVersionsDir), TS::Version(0, 5)},
        {std::string(kVersionsDir), TS::Version(9, 9)},
        {std::string(kVersionsDir), TS::Version(3, 0)},
        {JoinPath(kVersionsDir, "empty"), TS::Version(2, 5)},
        {JoinPath(kVersionsDir, "empty"), std::nullopt},
    };
    for (const auto& [basePath, version] : cases) {
        std::string result = rf.FindClosestVersionDirectory(basePath, version);
        observed.push_back("I7|" + (version ? version->ToString() : std::string("<null>")) +
            "|" + result);
    }
    ASSERT_EQ(observed.size(), kUarClosestVersionGold.size());
    for (std::size_t i = 0; i < observed.size(); i++) {
        EXPECT_STREQ(observed[i].c_str(),
            std::string(kUarClosestVersionGold[i]).c_str())
            << "line " << i;
    }
}

}  // namespace
