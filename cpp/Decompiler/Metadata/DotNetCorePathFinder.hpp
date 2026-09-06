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

// Port of ICSharpCode.Decompiler/Metadata/DotNetCorePathFinder.cs: the
// .NET Core assembly directory finder `UniversalAssemblyResolver` composes
// (the per-app .deps.json package-base-path discovery, the shared-framework
// "dotnet\shared\<pack>\<version>" resolution, the reference-assembly pack
// path, and the PATH-scan for the dotnet executable).
//
// The .deps.json read (`LoadPackageInfos`) routes through nlohmann-json's
// SAX parser instead of a LightJson port (the PORT_PLAN.md 5.1 decision);
// the LightJson surface it reproduces:
//   * object properties keep their document (insertion) order -- the
//     `libraries` walk and the runtime-component list are order-observable
//     through `packageBasePaths`' probing order;
//   * a duplicate object key is REJECTED (LightJson's JsonParseException
//     "The parser encountered a JsonObject with duplicate keys.");
//   * `//` and `/* */` comments are accepted (LightJson's TextScanner
//     skips them; nlohmann's ignore_comments matches);
//   * trailing content after the root value is ignored (LightJson's
//     Parse reads one value; nlohmann's strict=false matches).
// Documented divergences from LightJson's reader: a JSON TRAILING COMMA
// (LightJson accepts one, nlohmann rejects the file -- real .deps.json
// producers emit strict JSON), the number-token spellings LightJson's
// lenient ReadNumber accepts ("01", trailing '.'), lone-surrogate \uXXXX
// escapes, and non-UTF-8 file bytes (File.ReadAllText's U+FFFD replacement
// vs nlohmann's parse error) -- none reachable through a real .deps.json.
//
// The C# `internal` members (`GetReferenceAssemblyPath`, `ConvertToVersion`,
// `GetClosestVersionFolder`) port as public members (the standing
// internal-to-public test-access convention).

#pragma once

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class DotNetCorePathFinder {
public:
    // The C# base ctor -- the one `UniversalAssemblyResolver.InitDotNetCorePathFinder`
    // uses when there is no main assembly file. `DotNetCorePathFinder(parentAssemblyFileName, ...)`
    // delegates here. The .NET Standard 2.1 special case rewrites the version
    // to 3.0.0 (".NET Standard 2.1 is implemented by .NET Core 3.0 or
    // higher").
    DotNetCorePathFinder(TargetFrameworkIdentifier targetFramework,
        TypeSystem::Version targetFrameworkVersion,
        std::optional<std::string> preferredRuntimePack);

    // The C# main ctor: probes `<dir>\<name>.deps.json` beside the parent
    // assembly (the base search path is the parent's directory), loads the
    // package infos for the target framework, and derives the NuGet
    // package base paths (each runtime component's directory under the
    // NUGET_PACKAGES / %USERPROFILE%\.nuget\packages lookup paths, lowercased).
    // When the .deps.json is missing, the loadInfo (if any) gets the
    // warning "<name>.deps.json could not be found!".
    //
    // Divergences: the C# static LookupPaths read `NUGET_PACKAGES` once per
    // process (static field initializer); the port reads it per construction
    // -- no caller mutates the variable mid-process, so the observable
    // behavior is identical for every real process shape.
    DotNetCorePathFinder(const std::string& parentAssemblyFileName,
        const std::string& targetFrameworkIdString,
        std::optional<std::string> preferredRuntimePack,
        TargetFrameworkIdentifier targetFramework,
        TypeSystem::Version targetFrameworkVersion,
        ReferenceLoadInfo* loadInfo = nullptr);

    DotNetCorePathFinder(const DotNetCorePathFinder&) = delete;
    DotNetCorePathFinder& operator=(const DotNetCorePathFinder&) = delete;

    // The C# `AddSearchDirectory(string path)` -- nulls flow in through
    // `UniversalAssemblyResolver.AddSearchDirectory`'s nullable parameter
    // and stay in the list (a later `Path.Combine` over a null search path
    // throws ArgumentNullException, reproduced by TryResolveDotNetCore).
    void AddSearchDirectory(std::optional<std::string> path);

    // The C# `RemoveSearchDirectory(string path)` -- removes the FIRST
    // occurrence (List.Remove).
    void RemoveSearchDirectory(std::optional<std::string> path);

    // The C# `string TryResolveDotNetCore(IAssemblyReference name)`: every
    // search path then every package base path, probing `<name>.dll` then
    // `<name>.exe`; falls through to the shared-framework resolution.
    std::optional<std::string> TryResolveDotNetCore(const IAssemblyReference& name);

    // The C# `string TryResolveDotNetCoreShared(IAssemblyReference name, out
    // string runtimePack)`: the preferred runtime pack (when set) ahead of
    // the four builtin packs, each probed as
    // `<dotnetBasePath>\shared\<pack>\<closest version>` -- the lowest
    // installed version folder at or above the target framework version
    // that recursively contains a .dll file. The out parameter ends as the
    // LAST probed pack on a miss (null) and the matching pack on a hit.
    std::optional<std::string> TryResolveDotNetCoreShared(const IAssemblyReference& name,
        std::optional<std::string>& runtimePack);

    // The C# `internal string GetReferenceAssemblyPath(string targetFramework)`:
    // the packs\{identifier}.Ref\<closest>\ref\{ext} path for the reference-assembly
    // pack matching the parsed target framework. Throws NotSupportedException
    // for non-.NET identifiers and propagates `GetClosestVersionFolder`'s
    // DirectoryNotFoundException when the pack folder does not exist.
    std::string GetReferenceAssemblyPath(const std::string& targetFramework);

    // The C# `public static string FindDotNetExeDirectory()` -- the PATH
    // scan for dotnet.exe (dotnet on Unix), returning its directory or
    // null. Divergences: a null PATH maps to null (the C# NullReferenceException
    // arm is unreachable -- PATH is always set in practice), and the Unix
    // reparse-point arm resolves through realpath(3).
    static std::optional<std::string> FindDotNetExeDirectory();

    // The C# `internal static (Version version, DirectoryInfo directory)
    // ConvertToVersion(DirectoryInfo directory)` -- the observable core is
    // the name parse: everything before the FIRST '-' (a dash at position 0
    // stays), then `new Version(...)`; any parse failure is null (the C#
    // catches the exception, Trace-warns, and returns the null tuple).
    static std::optional<TypeSystem::Version> ConvertToVersion(
        const std::string& directoryName);

    // The C# `static string GetClosestVersionFolder(string basePath,
    // Version version)`: the subdirectories whose names parse as versions,
    // ordered ascending (stable), returning the first whose version is
    // >= the requested one and that recursively contains a .dll file;
    // `version.ToString()` when none qualifies. Throws
    // DirectoryNotFoundException (std::runtime_error) when basePath does
    // not exist.
    static std::string GetClosestVersionFolder(const std::string& basePath,
        const TypeSystem::Version& version);

    // The private `readonly Version targetFrameworkVersion` field (the
    // .NET Standard 2.1 -> 3.0 rewrite) -- the test seam mirroring the
    // reflection gold dump.
    const TypeSystem::Version& TargetFrameworkVersionForTest() const {
        return targetFrameworkVersion_;
    }

private:
    // The C# private nested `DotNetCorePackageInfo` -- one `libraries`
    // entry. `Type`/`Path` carry the AsString conversions (null when the
    // value is not a string) but are never read downstream; `Version` is
    // the part of the library key after '/' or "<UNKNOWN>".
    struct DotNetCorePackageInfo {
        std::string Name;
        std::string Version;
        std::optional<std::string> Type;
        std::optional<std::string> Path;
        std::vector<std::string> RuntimeComponents;
    };

    // The C# `static IEnumerable<DotNetCorePackageInfo>
    // LoadPackageInfos(string depsJsonFileName, string targetFramework)`:
    // the .deps.json read (see the file header for the JSON-engine notes).
    static std::vector<DotNetCorePackageInfo> LoadPackageInfos(
        const std::string& depsJsonFileName, const std::string& targetFramework);

    // The C# `readonly DotNetCorePackageInfo[] packages` -- only the ctor
    // reads it (the C# keeps it as a field; the package base paths are
    // precomputed there).
    std::vector<DotNetCorePackageInfo> packages_;
    // The C# `readonly List<string> searchPaths` (nulls observable -- see
    // AddSearchDirectory).
    std::vector<std::optional<std::string>> searchPaths_;
    // The C# `readonly List<string> packageBasePaths`.
    std::vector<std::string> packageBasePaths_;
    TypeSystem::Version targetFrameworkVersion_;
    // The C# `readonly string dotnetBasePath = FindDotNetExeDirectory()`.
    std::optional<std::string> dotnetBasePath_;
    // The C# `readonly string preferredRuntimePack` (nullable).
    std::optional<std::string> preferredRuntimePack_;
};

}  // namespace ILSpy::Decompiler::Metadata
