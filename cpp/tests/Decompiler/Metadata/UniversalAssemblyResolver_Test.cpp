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

// The UniversalAssemblyResolver GAC-machinery slice tests (the class-body
// STATIC half: the enums' gold member tables, IsZeroOrAllOnes,
// IsSpecialVersionOrRetargetable, GetGacPaths, GetAssemblyInGac,
// GetAssemblyFile, EnumerateGac, the hand-rolled folder-name regex, the
// AssemblyReferenceClassifier base virtuals, and ResolutionException) --
// every expectation dumped from the real installed ICSharpCode.Decompiler
// 11.0 (the C:/temp-probe/UarProbe gold probe) over THIS machine's real .NET
// Framework 4.8 GAC:
//   * the E/T/S/G/A/C/R curated drives (the enum values, the wildcard-version
//   quirk, the machine GAC roots, the real resolved GAC paths, the classifier
//   base virtuals, and both ResolutionException message renders with their
//   property shapes and the null-reference guard);
//   * the N EnumerateGac snapshot (608 entries, every full name round-tripping
//   through Parse, the FNV-1a-64 over the sorted names, and the
//   mscorlib/System/System.Core known subset);
//   * the X crafted folder-name regex matrix (the unanchored scan, the greedy
//   optional prefix and its fallback, and the forced group lengths).

#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "TestFixtures/GacGold.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <cstdio>
#include <filesystem>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;
using ILSpy::Tests::kGacEntryCount;
using ILSpy::Tests::kGacFolderNameExpected;
using ILSpy::Tests::kGacFolderNameInputs;
using ILSpy::Tests::kGacKnownEntries;
using ILSpy::Tests::kGacRoundTripBad;
using ILSpy::Tests::kGacRoundTripOk;
using ILSpy::Tests::kGacSortedNamesDigest;

// The probe's FNV-1a-64 (each line feeds the bytes, then 0xff, then the
// prime multiply -- the MethodSemanticsLookup convention).
class Fnv64 {
public:
    void Add(const std::string& s) {
        for (char ch : s) {
            fnv_ ^= static_cast<std::uint8_t>(ch);
            fnv_ *= 0x100000001b3ULL;
        }
        fnv_ ^= 0xff;
        fnv_ *= 0x100000001b3ULL;
    }
    std::uint64_t Digest() const { return fnv_; }

private:
    std::uint64_t fnv_ = 0xcbf29ce484222325ULL;
};

// The probe's StubRef equivalent: an IAssemblyReference the retargetable
// flag drives (AssemblyNameReference.Parse never sets it).
class StubReference : public TM::IAssemblyReference {
public:
    std::string Name() const override { return "r"; }
    std::string FullName() const override { return "r"; }
    std::optional<TS::Version> Version() const override { return version; }
    std::optional<std::string> Culture() const override { return std::nullopt; }
    std::optional<std::vector<std::uint8_t>> PublicKeyToken() const override {
        return std::nullopt;
    }
    bool IsWindowsRuntime() const override { return false; }
    bool IsRetargetable() const override { return isRetargetable; }

    std::optional<TS::Version> version = TS::Version(1, 0);
    bool isRetargetable = false;
};

TEST(UniversalAssemblyResolverTest, EnumValuesMatchTheGoldMemberTables) {
    EXPECT_EQ(static_cast<int>(TM::TargetRuntime::Unknown), 0);
    EXPECT_EQ(static_cast<int>(TM::TargetRuntime::Net_1_0), 1);
    EXPECT_EQ(static_cast<int>(TM::TargetRuntime::Net_1_1), 2);
    EXPECT_EQ(static_cast<int>(TM::TargetRuntime::Net_2_0), 3);
    EXPECT_EQ(static_cast<int>(TM::TargetRuntime::Net_4_0), 4);

    EXPECT_EQ(static_cast<int>(TM::TargetFrameworkIdentifier::NETFramework), 0);
    EXPECT_EQ(static_cast<int>(TM::TargetFrameworkIdentifier::NETCoreApp), 1);
    EXPECT_EQ(static_cast<int>(TM::TargetFrameworkIdentifier::NETStandard), 2);
    EXPECT_EQ(static_cast<int>(TM::TargetFrameworkIdentifier::Silverlight), 3);
    EXPECT_EQ(static_cast<int>(TM::TargetFrameworkIdentifier::NET), 4);

    // The E|DecompilerRuntime member values and the host pin
    // (E|decompilerRuntime=NETCoreApp).
    EXPECT_EQ(static_cast<int>(TM::DecompilerRuntime::NETFramework), 0);
    EXPECT_EQ(static_cast<int>(TM::DecompilerRuntime::NETCoreApp), 1);
    EXPECT_EQ(static_cast<int>(TM::DecompilerRuntime::Mono), 2);
    EXPECT_EQ(TM::kDecompilerRuntime, TM::DecompilerRuntime::NETCoreApp);

    // E|ZeroVersion=0.0.0.0
    EXPECT_EQ(TM::ZeroVersion().ToString(), "0.0.0.0");
}

TEST(UniversalAssemblyResolverTest, IsZeroOrAllOnesMatchesTheGoldMatrix) {
    // T|[<null>]|True -- a null version is a wildcard.
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(std::nullopt));
    // T|[0.0.0.0]|True / T|[65535.65535.65535.65535]|True
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(0, 0, 0, 0)));
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(
        TS::Version(65535, 65535, 65535, 65535)));
    // T|[0.0.0]|False / T|[0.0]|False -- an UNSPECIFIED component is -1:
    // only a FOUR-component 0.0.0.0 is all-zeros.
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(0, 0, 0)));
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(0, 0)));
    // T|[0.0.1]|False / T|[1.0.0.0]|False / T|[65535.0.0.0]|False
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(0, 0, 1)));
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(1, 0, 0, 0)));
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsZeroOrAllOnes(TS::Version(65535, 0, 0, 0)));
}

TEST(UniversalAssemblyResolverTest, IsSpecialVersionOrRetargetableMatchesTheGoldMatrix) {
    // S|[mscorlib, Version=4.0.0.0, ...b77a...] ret=False ver=4.0.0.0|False
    TM::AssemblyNameReference mscorlib = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(mscorlib));
    // S|[foo, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null]
    // ret=False ver=<null>|True -- the null-version wildcard.
    TM::AssemblyNameReference foo = TM::AssemblyNameReference::Parse("foo");
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(foo));
    // The explicit 0.0.0.0 and 65535.65535.65535.65535 wildcards.
    TM::AssemblyNameReference zero = TM::AssemblyNameReference::Parse(
        "foo, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(zero));
    TM::AssemblyNameReference ones = TM::AssemblyNameReference::Parse(
        "foo, Version=65535.65535.65535.65535, Culture=neutral, PublicKeyToken=null");
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(ones));
    // S|[r] ret=True ver=1.0|True / S|[r] ret=False ver=1.0|False -- the
    // retargetable flag half (the stub shape).
    StubReference retargetable;
    retargetable.isRetargetable = true;
    EXPECT_TRUE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(retargetable));
    StubReference plain;
    EXPECT_FALSE(TM::UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(plain));
}

TEST(UniversalAssemblyResolverTest, GetGacPathsReturnsTheMachineGacRoots) {
    // G|[0]|C:\WINDOWS\assembly / G|[1]|C:\WINDOWS\Microsoft.NET\assembly --
    // the machine-pinned roots (the GetWindowsDirectory value).
    std::vector<std::string> paths = TM::UniversalAssemblyResolver::GetGacPaths();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "C:\\WINDOWS\\assembly");
    EXPECT_EQ(paths[1], "C:\\WINDOWS\\Microsoft.NET\\assembly");
}

TEST(UniversalAssemblyResolverTest, GetAssemblyInGacMatchesTheMachineGac) {
    // A|mscorlib4: the first hit across the roots and the
    // GAC_MSIL/GAC_32/GAC_64/GAC order (the v4 root's GAC_32).
    TM::AssemblyNameReference mscorlib4 = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(mscorlib4),
        std::optional<std::string>(
            "C:\\WINDOWS\\Microsoft.NET\\assembly\\GAC_32\\mscorlib\\"
            "v4.0_4.0.0.0__b77a5c561934e089\\mscorlib.dll"));

    // A|mscorlib2|<null> -- no root carries a 2.0 mscorlib folder.
    TM::AssemblyNameReference mscorlib2 = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=2.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(mscorlib2), std::nullopt);

    // A|system4 / A|sysCore: the v4 root's GAC_MSIL.
    TM::AssemblyNameReference system4 = TM::AssemblyNameReference::Parse(
        "System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(system4),
        std::optional<std::string>(
            "C:\\WINDOWS\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
            "v4.0_4.0.0.0__b77a5c561934e089\\System.dll"));
    TM::AssemblyNameReference sysCore = TM::AssemblyNameReference::Parse(
        "System.Core, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(sysCore),
        std::optional<std::string>(
            "C:\\WINDOWS\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Core\\"
            "v4.0_4.0.0.0__b77a5c561934e089\\System.Core.dll"));

    // A|nonexistent|<null>
    TM::AssemblyNameReference nonexistent = TM::AssemblyNameReference::Parse(
        "NoSuchAssemblyExists, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(nonexistent), std::nullopt);

    // A|noToken|<null> (no PublicKeyToken at all) and A|nullToken|<null>
    // (the 'null' spelling): the null/empty-token guard.
    TM::AssemblyNameReference noToken = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(noToken), std::nullopt);
    TM::AssemblyNameReference nullToken = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=null");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(nullToken), std::nullopt);

    // A|winrtFlag|<null> -- a WindowsRuntime reference with a null token.
    TM::AssemblyNameReference winrt = TM::AssemblyNameReference::Parse(
        "Windows.Foundation, Version=255.255.255.255, ContentType=WindowsRuntime");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyInGac(winrt), std::nullopt);
}

TEST(UniversalAssemblyResolverTest, GetAssemblyFileComposesTheGacFolderNames) {
    // F|netgac4: the v4.0_ prefix + the version render + '__' + the
    // lowercase-hex token + the four-part combine.
    TM::AssemblyNameReference mscorlib = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyFile(mscorlib, "v4.0_", "C:\\gac"),
        "C:\\gac\\mscorlib\\v4.0_4.0.0.0__b77a5c561934e089\\mscorlib.dll");
    // F|netgac0: the plain (no-prefix) legacy-root folder shape.
    TM::AssemblyNameReference system = TM::AssemblyNameReference::Parse(
        "System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyFile(system, "", "C:\\gac"),
        "C:\\gac\\System\\4.0.0.0__b77a5c561934e089\\System.dll");
    // F|noversion: a NULL reference version renders EMPTY -- the folder
    // name carries the prefix, then '__' straight after ('v4.0___<hex>').
    TM::AssemblyNameReference noVersion = TM::AssemblyNameReference::Parse(
        "foo, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(TM::UniversalAssemblyResolver::GetAssemblyFile(noVersion, "v4.0_", "C:\\gac"),
        "C:\\gac\\foo\\v4.0___b77a5c561934e089\\foo.dll");
}

TEST(AssemblyReferenceClassifierTest, BaseVirtualsMatchTheGold) {
    TM::AssemblyReferenceClassifier classifier;
    // C|mscorlib4|gac=True|shared=False|pack=<null>
    TM::AssemblyNameReference mscorlib = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    std::optional<std::string> pack;
    EXPECT_TRUE(classifier.IsGacAssembly(mscorlib));
    EXPECT_FALSE(classifier.IsSharedAssembly(mscorlib, pack));
    EXPECT_EQ(pack, std::nullopt);
    // C|nonexistent / C|noToken
    TM::AssemblyNameReference nonexistent = TM::AssemblyNameReference::Parse(
        "NoSuchAssemblyExists, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    EXPECT_FALSE(classifier.IsGacAssembly(nonexistent));
    EXPECT_FALSE(classifier.IsSharedAssembly(nonexistent, pack));
    EXPECT_EQ(pack, std::nullopt);
    TM::AssemblyNameReference noToken = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral");
    EXPECT_FALSE(classifier.IsGacAssembly(noToken));
    EXPECT_FALSE(classifier.IsSharedAssembly(noToken, pack));
    EXPECT_EQ(pack, std::nullopt);
}

TEST(ResolutionExceptionTest, MessagesAndPropertiesMatchTheGold) {
    TM::AssemblyNameReference reference = TM::AssemblyNameReference::Parse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");

    // R|1: the null resolvedPath renders '<not found>'; the message carries
    // Environment.NewLine (CRLF).
    TM::ResolutionException notFound(&reference, std::nullopt);
    EXPECT_STREQ(notFound.what(),
        "Failed to resolve assembly: 'mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089'\r\nResolve result: <not found>");
    EXPECT_EQ(notFound.Reference(), &reference);
    EXPECT_EQ(notFound.ResolvedFullPath(), std::nullopt);
    EXPECT_EQ(notFound.ModuleName(), std::nullopt);
    EXPECT_EQ(notFound.MainModuleFullPath(), std::nullopt);

    // R|2: a resolved path renders verbatim.
    TM::ResolutionException resolved(&reference, std::optional<std::string>("C:\\some\\path.dll"));
    EXPECT_STREQ(resolved.what(),
        "Failed to resolve assembly: 'mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089'\r\nResolve result: C:\\some\\path.dll");
    EXPECT_EQ(resolved.ResolvedFullPath(), std::optional<std::string>("C:\\some\\path.dll"));

    // R|3/R|4: the module-resolution ctor's message and properties.
    TM::ResolutionException module("C:\\main.dll", "mymodule.netmodule", std::nullopt);
    EXPECT_STREQ(module.what(),
        "Failed to resolve module: 'mymodule.netmodule of C:\\main.dll'\r\n"
        "Resolve result: <not found>");
    EXPECT_EQ(module.Reference(), nullptr);
    EXPECT_EQ(module.ModuleName(), std::optional<std::string>("mymodule.netmodule"));
    EXPECT_EQ(module.MainModuleFullPath(), std::optional<std::string>("C:\\main.dll"));
    EXPECT_EQ(module.ResolvedFullPath(), std::nullopt);
    TM::ResolutionException moduleResolved(
        "C:\\main.dll", "mymodule.netmodule", std::optional<std::string>("C:\\mymodule.netmodule"));
    EXPECT_STREQ(moduleResolved.what(),
        "Failed to resolve module: 'mymodule.netmodule of C:\\main.dll'\r\n"
        "Resolve result: C:\\mymodule.netmodule");

    // R|5: the null-reference guard (the C# ArgumentNullException thrown
    // after the base ctor renders -- never observable).
    EXPECT_THROW(
        TM::ResolutionException(nullptr, std::nullopt), std::invalid_argument);
    try {
        TM::ResolutionException(nullptr, std::nullopt);
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'reference')");
    }
}

TEST(UniversalAssemblyResolverTest, EnumerateGacMatchesTheMachineGacSnapshot) {
    std::vector<TM::AssemblyNameReference> entries =
        TM::UniversalAssemblyResolver::EnumerateGac();
    ASSERT_EQ(entries.size(), kGacEntryCount);

    // The machine-independent invariant: every full name round-trips through
    // Parse (N|roundtrip ok=608 bad=0).
    std::size_t roundTripOk = 0;
    std::size_t roundTripBad = 0;
    for (const TM::AssemblyNameReference& entry : entries) {
        if (TM::AssemblyNameReference::Parse(entry.FullName()).FullName()
            == entry.FullName()) {
            roundTripOk++;
        } else {
            roundTripBad++;
        }
    }
    EXPECT_EQ(roundTripOk, kGacRoundTripOk);
    EXPECT_EQ(roundTripBad, kGacRoundTripBad);

    // The snapshot digest: the FNV-1a-64 over the probe's own line set
    // (count, roundtrip, and the 608 ordinal-sorted names -- byte-ordinal
    // for the all-ASCII snapshot).
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const TM::AssemblyNameReference& entry : entries) {
        names.push_back(entry.FullName());
    }
    std::sort(names.begin(), names.end());
    Fnv64 fnv;
    fnv.Add("N|count=" + std::to_string(entries.size()));
    fnv.Add("N|roundtrip ok=" + std::to_string(roundTripOk)
        + " bad=" + std::to_string(roundTripBad));
    for (const std::string& name : names) {
        fnv.Add("N|" + name);
    }
    EXPECT_EQ(fnv.Digest(), kGacSortedNamesDigest);

    // The K known subset (mscorlib/System/System.Core, ordinal sorted).
    std::vector<std::string> known;
    for (const TM::AssemblyNameReference& entry : entries) {
        if (entry.Name() == "mscorlib" || entry.Name() == "System"
            || entry.Name() == "System.Core") {
            known.push_back(entry.FullName());
        }
    }
    std::sort(known.begin(), known.end());
    ASSERT_EQ(known.size(), kGacKnownEntries.size());
    for (std::size_t i = 0; i < known.size(); i++) {
        EXPECT_EQ(known[i], kGacKnownEntries[i]) << "known[" << i << "]";
    }
}

TEST(GacFolderNameRegexTest, CraftedMatrixMatchesTheDotNetRegex) {
    ASSERT_EQ(kGacFolderNameInputs.size(), kGacFolderNameExpected.size());
    for (std::size_t i = 0; i < kGacFolderNameInputs.size(); i++) {
        std::string_view folderName = kGacFolderNameInputs[i];
        std::string_view expected = kGacFolderNameExpected[i];
        TM::GacFolderNameMatch match;
        std::string rendered;
        if (TM::TryMatchGacFolderName(folderName, match)) {
            rendered = "OK|v=" + match.Version + "|c=" + match.Culture
                + "|p=" + match.PublicKey;
        } else {
            rendered = "FAIL";
        }
        EXPECT_EQ(rendered, expected) << "folder name: " << folderName;
    }
}

// A name-parameterizable reference (the file-local StubReference above has a
// fixed name; this one drives the search-directory machinery).
class NamedReference : public TM::IAssemblyReference {
public:
    explicit NamedReference(std::string name) : name_(std::move(name)) {}

    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::optional<TS::Version> Version() const override
    {
        return TS::Version(1, 0);
    }
    std::optional<std::string> Culture() const override { return std::nullopt; }
    std::optional<std::vector<std::uint8_t>> PublicKeyToken() const override
    {
        return std::nullopt;
    }
    bool IsWindowsRuntime() const override { return false; }
    bool IsRetargetable() const override { return false; }

private:
    std::string name_;
};

// A POSIX-shaped main-assembly path (a .NETCoreApp-targeting resolver on a
// Linux host): the DotNetCorePathFinder ctor takes Path.GetDirectoryName of
// the main file and Path.Combine's the .deps.json probe against it, so a
// leading-'/' path must parse as a rooted path with a real directory
// name -- not as a UNC-style whole-path root, whose null directory name
// made the ctor throw the Path.Combine ArgumentNullException
// ("Value cannot be null. (Parameter 'path1')") on every
// UniversalAssemblyResolver-driven BAML decompilation.
TEST(UniversalAssemblyResolverTest, PosixMainPathDrivesTheNetCoreFinderWithoutThrowing) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "ilspy_uar_posix";
    std::error_code ec;
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << "cannot create " << dir;
    const std::string main = (dir / "main.dll").string();
    const std::string sideBySide = (dir / "SideBySide.dll").string();
    for (const std::string& file : { main, sideBySide }) {
        std::FILE* out = std::fopen(file.c_str(), "wb");
        ASSERT_NE(out, nullptr) << "cannot write " << file;
        std::fputs("MZ placeholder bytes", out);
        std::fclose(out);
    }

    TM::UniversalAssemblyResolver resolver(
        main, /*throwOnError=*/false, ".NETCoreApp,Version=v8.0");

    // Resolving an assembly that exists nowhere must come back empty, not
    // throw from the finder construction (the deps.json probe arm).
    EXPECT_EQ(resolver.FindAssemblyFile(NamedReference("NowhereToBeFound")),
        std::nullopt);

    // A side-by-side file must be located through the POSIX join: the
    // composed candidate path is the main file's directory + '/' + the
    // assembly name (a '\\' join does not name a file on a POSIX host).
    EXPECT_EQ(resolver.FindAssemblyFile(NamedReference("SideBySide")),
        std::optional<std::string>(sideBySide));
}

}  // namespace
