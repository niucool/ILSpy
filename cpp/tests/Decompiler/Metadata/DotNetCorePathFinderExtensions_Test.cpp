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

// Tests for the DotNetCorePathFinderExtensions port (the
// DetectTargetFrameworkId / IsReferenceAssembly / DetectRuntimePack family
// the CLI passes to the UniversalAssemblyResolver ctor).
//
// Every expected value is gold-dumped from the REAL ICSharpCode.Decompiler
// 11.0 over the identical fixtures by the DtfProbe gold probe
// (C:/temp-probe/DtfProbe): twelve real local assemblies, the twenty-three
// crafted manifests (TestFixtures/DtfFixtures.hpp -- one per detect arm no
// real assembly carries), the synthetic-path matrix that drives the
// path-pattern fallback through the MetadataReader overload, and the
// null-path arms.

#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "TestFixtures/DtfFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ILSpy::Decompiler::Metadata::DetectRuntimePack;
using ILSpy::Decompiler::Metadata::DetectTargetFrameworkId;
using ILSpy::Decompiler::Metadata::GetDotNetCoreVersion;
using ILSpy::Decompiler::Metadata::IsReferenceAssembly;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::Version;

namespace {

// The probe's Show(): "<null>" for the C# null, "[...]" for a string.
std::string Show(const std::optional<std::string>& s) {
    return s ? "[" + *s + "]" : "<null>";
}

std::string CoreLibPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
}

// The section-A real-assembly paths (the probe's RealFiles order).
std::vector<std::pair<const char*, std::string>> RealFiles() {
    std::vector<std::pair<const char*, std::string>> files = {
        {"mscorlib",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"},
        {"System",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll"},
        {"System.Core",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.Core.dll"},
        {"facade",
         "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
         "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll"},
        {"corelib", CoreLibPath()},
        {"tiny", WriteTinyNetModule()},
        {"PresentationFramework",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\WPF\\"
         "PresentationFramework.dll"},
        {"WindowsBase",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\WPF\\WindowsBase.dll"},
        {"PresentationCore",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\WPF\\"
         "PresentationCore.dll"},
        {"System.Xaml",
         "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.Xaml.dll"},
        {"netstandard",
         "C:\\Program Files (x86)\\Reference Assemblies\\Microsoft\\Framework\\"
         ".NETFramework\\v4.7.2\\Facades\\netstandard.dll"},
        {"roslyn",
         "C:\\Program Files\\dotnet\\sdk\\10.0.204\\Roslyn\\bincore\\"
         "Microsoft.CodeAnalysis.dll"},
    };
    return files;
}

// The crafted manifests: (tag, hex array) in the fixture's registry order.
std::vector<std::pair<const char*, std::pair<const std::uint8_t*, std::size_t>>>
Manifests() {
    return {
        {"dtfAttr", {ILSpy::Tests::kDtfAttrBytes, sizeof(ILSpy::Tests::kDtfAttrBytes)}},
        {"dtfSpaces", {ILSpy::Tests::kDtfSpacesBytes, sizeof(ILSpy::Tests::kDtfSpacesBytes)}},
        {"dtfNull", {ILSpy::Tests::kDtfNullBytes, sizeof(ILSpy::Tests::kDtfNullBytes)}},
        {"dtfProlog", {ILSpy::Tests::kDtfPrologBytes, sizeof(ILSpy::Tests::kDtfPrologBytes)}},
        {"dtfMalformed", {ILSpy::Tests::kDtfMalformedBytes, sizeof(ILSpy::Tests::kDtfMalformedBytes)}},
        {"dtfMscorlib", {ILSpy::Tests::kDtfMscorlibBytes, sizeof(ILSpy::Tests::kDtfMscorlibBytes)}},
        {"dtfNetstandard", {ILSpy::Tests::kDtfNetstandardBytes, sizeof(ILSpy::Tests::kDtfNetstandardBytes)}},
        {"dtfSysRuntime41", {ILSpy::Tests::kDtfSysRuntime41Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime41Bytes)}},
        {"dtfSysRuntime420", {ILSpy::Tests::kDtfSysRuntime420Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime420Bytes)}},
        {"dtfSysRuntime421", {ILSpy::Tests::kDtfSysRuntime421Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime421Bytes)}},
        {"dtfSysRuntime422", {ILSpy::Tests::kDtfSysRuntime422Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime422Bytes)}},
        {"dtfSysRuntime59", {ILSpy::Tests::kDtfSysRuntime59Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime59Bytes)}},
        {"dtfSysRuntime40", {ILSpy::Tests::kDtfSysRuntime40Bytes, sizeof(ILSpy::Tests::kDtfSysRuntime40Bytes)}},
        {"dtfCoreLib", {ILSpy::Tests::kDtfCoreLibBytes, sizeof(ILSpy::Tests::kDtfCoreLibBytes)}},
        {"dtfRefMscorlib", {ILSpy::Tests::kDtfRefMscorlibBytes, sizeof(ILSpy::Tests::kDtfRefMscorlibBytes)}},
        {"dtfRefSysRuntime", {ILSpy::Tests::kDtfRefSysRuntimeBytes, sizeof(ILSpy::Tests::kDtfRefSysRuntimeBytes)}},
        {"dtfRefNilKey", {ILSpy::Tests::kDtfRefNilKeyBytes, sizeof(ILSpy::Tests::kDtfRefNilKeyBytes)}},
        {"dtfRefNetstandardNil",
         {ILSpy::Tests::kDtfRefNetstandardNilBytes, sizeof(ILSpy::Tests::kDtfRefNetstandardNilBytes)}},
        {"dtfNilValue", {ILSpy::Tests::kDtfNilValueBytes, sizeof(ILSpy::Tests::kDtfNilValueBytes)}},
        {"dtfNoRefs", {ILSpy::Tests::kDtfNoRefsBytes, sizeof(ILSpy::Tests::kDtfNoRefsBytes)}},
        {"dtfRefAssembly", {ILSpy::Tests::kDtfRefAssemblyBytes, sizeof(ILSpy::Tests::kDtfRefAssemblyBytes)}},
        {"dtfDesktop", {ILSpy::Tests::kDtfDesktopBytes, sizeof(ILSpy::Tests::kDtfDesktopBytes)}},
        {"dtfDesktopNil", {ILSpy::Tests::kDtfDesktopNilBytes, sizeof(ILSpy::Tests::kDtfDesktopNilBytes)}},
    };
}

}  // namespace

TEST(DotNetCorePathFinderExtensionsTest, GetDotNetCoreVersionMatrix)
{
    // The (Major, Minor, Build) tuple switch.
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 1, 0, 0)), std::optional<std::string>("1.1"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 2, 0, 0)), std::optional<std::string>("2.0"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 2, 1, 0)), std::optional<std::string>("3.0"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 2, 2, 0)), std::optional<std::string>("3.1"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(5, 0, 0, 0)), std::optional<std::string>("5.0"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(5, 9, 2, 4)), std::optional<std::string>("5.9"));
    EXPECT_EQ(GetDotNetCoreVersion(Version(10, 0, 8, 0)), std::optional<std::string>("10.0"));
    // Everything else maps to null (the break out of the name switch).
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 0, 0, 0)), std::nullopt);
    EXPECT_EQ(GetDotNetCoreVersion(Version(4, 3, 0, 0)), std::nullopt);
    EXPECT_EQ(GetDotNetCoreVersion(Version(3, 5, 0, 0)), std::nullopt);
    EXPECT_EQ(GetDotNetCoreVersion(Version(2, 0, 5, 0)), std::nullopt);
    EXPECT_EQ(GetDotNetCoreVersion(Version()), std::nullopt);
}

TEST(DotNetCorePathFinderExtensionsTest, RealAssemblies)
{
    std::vector<std::string> got;
    for (auto& [tag, path] : RealFiles()) {
        if (path.empty())
            continue;  // not installed (the globbed CoreLib)
        MetadataFile file(path);
        if (!file.IsValid()) {
            ADD_FAILURE() << "cannot open " << path;
            continue;
        }
        got.push_back("A|" + std::string(tag)
            + "|tfm=" + Show(DetectTargetFrameworkId(file))
            + "|refAsm=" + (IsReferenceAssembly(file) ? "True" : "False")
            + "|pack=" + DetectRuntimePack(file)
            + "|mdVer=" + file.MetadataVersion());
    }
    ASSERT_EQ(got.size(), 12u);
    for (std::size_t i = 0; i < got.size(); i++) {
        EXPECT_EQ(got[i], ILSpy::Tests::kDtfGoldReal[i]) << "real fixture " << i;
    }
}

TEST(DotNetCorePathFinderExtensionsTest, CraftedManifests)
{
    std::vector<std::string> got;
    for (auto& [tag, bytes] : Manifests()) {
        std::string path = ILSpy::Tests::WriteDtfDll(tag, bytes.first, bytes.second);
        MetadataFile file(path);
        ASSERT_TRUE(file.IsValid()) << tag;
        got.push_back("M|" + std::string(tag)
            + "|tfm=" + Show(DetectTargetFrameworkId(file))
            + "|refAsm=" + (IsReferenceAssembly(file) ? "True" : "False")
            + "|pack=" + DetectRuntimePack(file));
    }
    ASSERT_EQ(got.size(), 23u);
    for (std::size_t i = 0; i < got.size(); i++) {
        EXPECT_EQ(got[i], ILSpy::Tests::kDtfGoldManifests[i]) << "manifest " << i;
    }
}

TEST(DotNetCorePathFinderExtensionsTest, PathMatrix)
{
    // The MetadataReader overload with explicit paths over the
    // no-attribute/no-refs manifest (the path arm is the only one left).
    // kDtfPaths and kDtfGoldPaths are in the same order (the generator
    // asserts the gold row count and tags).
    std::string path = ILSpy::Tests::WriteDtfDll(
        "dtfNoRefs", ILSpy::Tests::kDtfNoRefsBytes, sizeof(ILSpy::Tests::kDtfNoRefsBytes));
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    constexpr std::size_t kCount = sizeof(ILSpy::Tests::kDtfPaths)
        / sizeof(ILSpy::Tests::kDtfPaths[0]);
    ASSERT_EQ(sizeof(ILSpy::Tests::kDtfGoldPaths)
              / sizeof(ILSpy::Tests::kDtfGoldPaths[0]), kCount);
    for (std::size_t i = 0; i < kCount; i++) {
        const auto& [tag, drivePath] = ILSpy::Tests::kDtfPaths[i];
        SCOPED_TRACE(tag);
        std::string p(drivePath);
        std::string line = "P|" + std::string(tag)
            + "|tfm=" + Show(DetectTargetFrameworkId(file, p))
            + "|refAsm=" + (IsReferenceAssembly(file, p) ? "True" : "False");
        EXPECT_EQ(line, std::string(ILSpy::Tests::kDtfGoldPaths[i]));
    }
}

TEST(DotNetCorePathFinderExtensionsTest, NullPathArms)
{
    // The reader overload with a null path: the path fallback is skipped and
    // the result is the empty string (NOT the no-match MetadataVersion arm).
    std::string path = ILSpy::Tests::WriteDtfDll(
        "dtfNoRefs", ILSpy::Tests::kDtfNoRefsBytes, sizeof(ILSpy::Tests::kDtfNoRefsBytes));
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DetectTargetFrameworkId(file, std::nullopt),
              std::optional<std::string>(""));

    // The C# Regex.Match(null, ...) ArgumentNullException.
    try {
        IsReferenceAssembly(file, std::nullopt);
        ADD_FAILURE() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'input')");
    }

    // The MetadataFile overload passes FileName (non-null), so the crafted
    // manifest with nothing else matching takes the no-match arm.
    EXPECT_EQ(DetectTargetFrameworkId(file),
              std::optional<std::string>(".NETFramework,Version=v4.0"));
    EXPECT_FALSE(IsReferenceAssembly(file));
}

TEST(DotNetCorePathFinderExtensionsTest, MetadataVersionAccessor)
{
    // The metadata root's version string (every local real file and every
    // MetadataBuilder manifest carries v4.0.30319).
    {
        MetadataFile mscorlib(
            "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll");
        if (mscorlib.IsValid())
            EXPECT_EQ(mscorlib.MetadataVersion(), "v4.0.30319");
    }
    {
        MetadataFile invalid("does-not-exist.dll");
        EXPECT_FALSE(invalid.IsValid());
        EXPECT_EQ(invalid.MetadataVersion(), "");
    }
}
