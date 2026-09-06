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

// Port of ICSharpCode.Decompiler/Metadata/UniversalAssemblyResolver.cs -- in
// TWO sub-slices. The FIRST (landed with the `DotNetCorePathFinder` slice):
// the three enums the file declares and the `internal static
// ParseTargetFramework` classifier, both `DotNetCorePathFinder
// .GetReferenceAssemblyPath` and the resolver ctor consume -- landed as free
// functions at the mirror-layout positions so the class's future instance
// surface can call them unchanged.
//
// The SECOND sub-slice (landed with the GAC static machinery): the class's
// STATIC half -- the `#region .NET / mono GAC handling` machinery
// (`GetGacPaths` / `GetAssemblyInGac` / `EnumerateGac` / `IsZeroOrAllOnes` /
// `IsSpecialVersionOrRetargetable` / `GetAssemblyFile` and the `gac_paths`
// static field) plus the `AssemblyReferenceClassifier` base it derives. Every
// member is gold-pinned against the real installed resolver over this
// machine's real .NET Framework GAC.
//
// The THIRD sub-slice (this slice): the class's INSTANCE surface -- the ctor
// (`mainAssemblyFileName`/`throwOnError`/`streamOptions`/`metadataOptions`,
// the `Lazy<DotNetCorePathFinder>` wiring), the search-directory trio
// (`AddSearchDirectory`/`RemoveSearchDirectory`/`GetSearchDirectories`),
// `IsSharedAssembly` (the override resolving through the lazy finder),
// `FindAssemblyFile`/`FindAssemblyFileCore` (the target-framework dispatch),
// `FindWindowsMetadataFile`/`FindWindowsMetadataInSystemDirectory`,
// `ResolveSilverlight`, `FindClosestVersionDirectory`, `ResolveInternal`,
// `SearchDirectory` (both overloads), and `GetCorlib`/
// `GetMscorlibBasePath` -- everything gold-pinned against the real
// engine's drives over this machine's framework directories, GAC, Windows
// Kits references, and .NET 10 shared-framework install. Every helper is
// public per the standing internal-to-public convention (the C# members are
// `private`); `FindClosestVersionDirectory` is a direct-drive test target
// (the C# probe invokes it by reflection over a crafted version-folder
// layout).
//
// The `Resolve`/`ResolveModule`/`CreatePEFileFromFileName` members (the
// `IAssemblyResolver` file-loading half) stay a documented deferral: they
// construct the port's `MetadataFile` over an opened stream, a separate
// verifiable unit that lands with the `IAssemblyResolver` derivation of the
// class.
//
// The Mono arms (`GetDefaultMonoGacPaths`/`GetCurrentMonoGac`/
// `GetAssemblyInMonoGac`/`GetMonoMscorlibBasePath` and the Mono static ctor
// detection) read the decompiler HOST's own Mono runtime module layout
// (`typeof(object).Module.FullyQualifiedName`) and are reachable only
// through `decompilerRuntime == Mono` -- the port pins the host statically
// as `kDecompilerRuntime = NETCoreApp` (the ilspycmd 11.0 build this port is
// gold-pinned against runs on .NET 10), so every Mono arm is unreachable in
// the shipped-tool shape and stays unported. The C#
// `ResolveInternal`'s NETFramework/`goto default` host arms (the decompiler
// host's own runtime directory as the framework search path) are likewise
// unreachable: the pinned NETCoreApp host always takes the
// `<windir>\Microsoft.NET\Framework64\v4.0.30319` arm.

#pragma once

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The lazy path finder the resolver owns -- forward-declared here (the C#
// file pair resolves the circular reference across the two files the same
// way `AssemblyReferenceClassifier::IsGacAssembly` does).
class DotNetCorePathFinder;

// The C# `public enum TargetRuntime` -- the classic .NET runtime versions the
// legacy API surface names (the port has no consumer yet; the resolver slice
// carries it as part of the file surface).
enum class TargetRuntime {
    Unknown,
    Net_1_0,
    Net_1_1,
    Net_2_0,
    Net_4_0,
};

// The C# `public enum TargetFrameworkIdentifier` -- the target-framework
// family `ParseTargetFramework` classifies a
// `System.Runtime.Versioning.TargetFrameworkAttribute` value into.
enum class TargetFrameworkIdentifier {
    NETFramework,
    NETCoreApp,
    NETStandard,
    Silverlight,
    NET,
};

// The C# `enum DecompilerRuntime` (internal) -- the runtime hosting the
// decompiler itself, which picks `UniversalAssemblyResolver.ResolveInternal`'s
// fallback framework directory. The port pins the real tool's host shape
// statically: the ilspycmd build this port is gold-pinned against runs on
// .NET 10 (`typeof(object).Assembly` is System.Private.CoreLib, the
// `NETCoreApp` arm of the C# static ctor before any Mono/Unix fallback), and
// the C# detection (`Type.GetType("Mono.Runtime")` /
// `typeof(object).Module.FullyQualifiedName`) has no native analogue.
enum class DecompilerRuntime {
    NETFramework,
    NETCoreApp,
    Mono,
};

// The port's `DecompilerRuntime` value (the `NETCoreApp` arm above).
constexpr DecompilerRuntime kDecompilerRuntime = DecompilerRuntime::NETCoreApp;

// The C# `System.Reflection.PortableExecutable.PEStreamOptions` -- the
// stream options the resolver ctor carries for the `PEReader` it creates
// (the port's `MetadataFile` opens files eagerly, so the options are carried
// verbatim; the `Resolve` deferral note above). The member tables are
// gold-pinned from the .NET 10 runtime (the I1 enum drives).
enum class PEStreamOptions {
    Default = 0,
    LeaveOpen = 1,
    PrefetchMetadata = 2,
    PrefetchEntireImage = 4,
    IsLoadedImage = 8,
};

// The C# `System.Reflection.Metadata.MetadataReaderOptions` (Default and
// ApplyWindowsRuntimeProjections alias the same value -- the I1 gold table
// pins both names at 1).
enum class MetadataReaderOptions {
    None = 0,
    Default = 1,
    ApplyWindowsRuntimeProjections = 1,
};

// The `internal static (TargetFrameworkIdentifier, Version)
// ParseTargetFramework(string targetFramework)` result pair.
struct ParsedTargetFramework {
    TargetFrameworkIdentifier Identifier;
    TypeSystem::Version ParsedVersion;
};

// The C# `internal static (TargetFrameworkIdentifier, Version)
// ParseTargetFramework(string targetFramework)`:
//   * null/empty input -> (NETFramework, 0.0.0.0);
//   * the FIRST comma-separated token, Unicode-trimmed and
//     invariant-uppercased, picks the identifier (".NETCOREAPP" ->
//     NETCoreApp, ".NETSTANDARD" -> NETStandard, "SILVERLIGHT" ->
//     Silverlight, anything else -> NETFramework);
//   * the later tokens are `key=value` pairs (a token without '=' is
//     skipped); a "Version" key (case-insensitive, both sides trimmed)
//     strips the leading 'v'/'V'/' '/'\t' run and `Version.TryParse`s the
//     remainder -- a parse failure leaves the version null; a success
//     re-wraps as (Major, Minor, Build < 0 ? 0 : Build) (the revision is
//     dropped);
//   * a parsed Major >= 5 under NETCoreApp re-classifies the identifier as
//     NET (".NET 5 or greater still use .NETCOREAPP as
//     TargetFrameworkAttribute value");
//   * the result version defaults to ZeroVersion when no "Version" pair
//     parsed.
//
// Divergences: the case fold is ASCII-only plus the one non-ASCII unit that
// folds into the matching set (U+017F long s -> 'S', the only BMP unit whose
// invariant uppercase is an ASCII letter, so ".NETſTANDARD" still matches
// ".NETSTANDARD"); `Version.TryParse` routes through the ported string ctor's
// exception arms.
ParsedTargetFramework ParseTargetFramework(const std::string& targetFramework);

// The C# `internal static Version ZeroVersion = new Version(0, 0, 0, 0)` --
// the null-version stand-in both the classifier and the resolver's
// `IsZeroOrAllOnes` gate use.
const TypeSystem::Version& ZeroVersion();

// The C# `public class UniversalAssemblyResolver : AssemblyReferenceClassifier,
// IAssemblyResolver` (UniversalAssemblyResolver.cs lines 76-813) -- the
// assembly resolver over the .NET Framework GAC, the .deps.json-driven
// `DotNetCorePathFinder`, Silverlight, and winmd layouts. This slice is the
// STATIC half (the header comment above); the `IAssemblyResolver` base lands
// with the instance surface.
class UniversalAssemblyResolver : public AssemblyReferenceClassifier {
public:
    // The C# `public static List<string> GetGacPaths()` -- the GAC root
    // directories: `<windir>\assembly` (the pre-v4 GAC) and
    // `<windir>\Microsoft.NET\assembly` (the v4 GAC). The Mono arm
    // (`GetDefaultMonoGacPaths`) is unreachable under the pinned `NETCoreApp`
    // host.
    static std::vector<std::string> GetGacPaths();

    // The C# `public static string? GetAssemblyInGac(IAssemblyReference
    // reference)`: a null or EMPTY public key token is never in the GAC
    // (the guard); otherwise the `.NET` GAC walk (`GetAssemblyInNetGac`, the
    // Mono arm unreachable under the pinned host).
    static std::optional<std::string> GetAssemblyInGac(
        const IAssemblyReference& reference);

    // The C# `public static IEnumerable<AssemblyNameReference> EnumerateGac()`
    // -- every assembly of the machine GAC, parsed from the version-folder
    // names (`{assemblyName}\{v4.0_?}{version}__{culture-or-empty}_{publicKey}`
    // under each `GAC_MSIL`/`GAC_32`/`GAC_64`/`GAC` root). The C# lazy sequence
    // ports eagerly (no observable side effect interleaves the enumeration).
    static std::vector<AssemblyNameReference> EnumerateGac();

    // The C# private `static bool IsZeroOrAllOnes(Version? version)` -- the
    // `0.0.0.0` / `65535.65535.65535.65535` wildcard versions (a null version
    // included). NOTE the quirk: `new Version(0, 0, 0)` is NOT all-zeros --
    // its `Revision` is -1 (unspecified), so only a FOUR-component 0.0.0.0
    // matches (gold-pinned).
    static bool IsZeroOrAllOnes(const std::optional<TypeSystem::Version>& version);

    // The C# private `static bool IsSpecialVersionOrRetargetable(
    // IAssemblyReference reference)` -- `IsZeroOrAllOnes` or the retargetable
    // flag: the references `ResolveInternal` resolves through the framework
    // directories unversioned.
    static bool IsSpecialVersionOrRetargetable(const IAssemblyReference& reference);

    // The C# private `static string GetAssemblyFile(IAssemblyReference
    // reference, string prefix, string gac)` -- the pure folder-name
    // composition: `Path.Combine(gac, name, prefix + version + "__" +
    // publicKeyTokenHex, name + ".dll")`. `prefix` is the `v4.0_`
    // v4-GAC-root prefix; a NULL reference version renders EMPTY (the C#
    // `StringBuilder.Append` null arm -- gold-pinned:
    // `foo` with no version composes `v4.0___<hex>`).
    static std::string GetAssemblyFile(const IAssemblyReference& reference,
        const std::string& prefix, const std::string& gac);

    // The C# `public UniversalAssemblyResolver(string? mainAssemblyFileName,
    // bool throwOnError, string? targetFramework, string? runtimePack = null,
    // PEStreamOptions streamOptions = PEStreamOptions.Default,
    // MetadataReaderOptions metadataOptions = MetadataReaderOptions.Default)`.
    // `targetFramework` null means the empty string, `runtimePack` null
    // "Microsoft.NETCore.App"; the pair is classified through
    // `ParseTargetFramework`; a non-null `mainAssemblyFileName` derives the
    // base directory (the `Path.GetDirectoryName` null/whitespace rule
    // falling back to Environment.CurrentDirectory) and adds it as the first
    // search directory. The C# `Lazy<DotNetCorePathFinder>` is a lazily
    // built unique_ptr (the `Finder()` accessor materializes it on first
    // use).
    explicit UniversalAssemblyResolver(
        std::optional<std::string> mainAssemblyFileName, bool throwOnError,
        std::optional<std::string> targetFramework,
        std::optional<std::string> runtimePack = std::nullopt,
        PEStreamOptions streamOptions = PEStreamOptions::Default,
        MetadataReaderOptions metadataOptions = MetadataReaderOptions::Default);
    ~UniversalAssemblyResolver();

    // The C# `public void AddSearchDirectory(string? directory)` -- a null
    // entry is stored (the C# `List<string?>`), and the lazy finder
    // receives it only once materialized.
    void AddSearchDirectory(std::optional<std::string> directory);

    // The C# `public void RemoveSearchDirectory(string? directory)` -- the
    // FIRST matching entry (a null removes the first null).
    void RemoveSearchDirectory(std::optional<std::string> directory);

    // The C# `public string?[] GetSearchDirectories()`.
    std::vector<std::optional<std::string>> GetSearchDirectories() const;

    // The C# `public override bool IsSharedAssembly(...)` -- through the lazy
    // `DotNetCorePathFinder.TryResolveDotNetCoreShared` (forcing the lazy).
    bool IsSharedAssembly(const IAssemblyReference& reference,
        std::optional<std::string>& runtimePack) const override;

    // The C# `public string? FindAssemblyFile(IAssemblyReference name)` (the
    // Instrumentation event-source logging of the C# VSADDIN-off build has
    // no observable effect outside the ETW log and does not port).
    std::optional<std::string> FindAssemblyFile(const IAssemblyReference& name) const;

    // The C# `string? FindClosestVersionDirectory(string basePath,
    // Version? version)` -- the closest-version-folder picker over the
    // `ConvertToVersion`-parsed subdirectory names (descending order, the
    // `path == null || version == null || folder >= version` walk), with the
    // `version?.ToString() ?? "."` fallback. Public per the
    // internal-to-public convention (the C# probe drives it by reflection).
    std::string FindClosestVersionDirectory(const std::string& basePath,
        const std::optional<TypeSystem::Version>& version) const;

private:
    // The C# `static readonly List<string> gac_paths = GetGacPaths()` -- the
    // process-lifetime singleton (a function-local static; `EnumerateGac`
    // calls `GetGacPaths()` fresh instead, exactly as the C# does).
    static const std::vector<std::string>& GacPaths();

    // The C# private `static string? GetAssemblyInNetGac(IAssemblyReference
    // reference)` -- the `GAC_MSIL`/`GAC_32`/`GAC_64`/`GAC` walk over the two
    // roots, the root index picking the `""`/`"v4.0_"` prefix.
    static std::optional<std::string> GetAssemblyInNetGac(
        const IAssemblyReference& reference);

    // The C# `string? FindAssemblyFileCore(IAssemblyReference name)` -- the
    // target-framework dispatch: the winmd arm, the shared-framework arms
    // (gated on `IsZeroOrAllOnes(targetFrameworkVersion)`), the Silverlight
    // arm, and the `ResolveInternal` default.
    std::optional<std::string> FindAssemblyFileCore(const IAssemblyReference& name) const;

    // The C# `DotNetCorePathFinder InitDotNetCorePathFinder()` -- the lazy
    // factory (the two ctor shapes + the search-directory replay).
    std::unique_ptr<DotNetCorePathFinder> InitDotNetCorePathFinder() const;

    // The C# `dotNetCorePathFinder.Value` Lazy read -- the first access
    // builds through `InitDotNetCorePathFinder` (the C# Lazy thread-safety
    // ports as a non-const mutable field; the C# semantics are
    // observable-order identical for the single-threaded resolver callers).
    DotNetCorePathFinder& Finder() const;

    // The C# `string? FindWindowsMetadataFile(...)` (the Windows Kits
    // References layout) and `...InSystemDirectory` (the system32\WinMetadata
    // fallback).
    std::optional<std::string> FindWindowsMetadataFile(const IAssemblyReference& name) const;
    std::optional<std::string> FindWindowsMetadataInSystemDirectory(
        const IAssemblyReference& name) const;

    // The C# `string? ResolveSilverlight(...)` -- the two ProgramFiles
    // search roots.
    std::optional<std::string> ResolveSilverlight(const IAssemblyReference& name,
        const std::optional<TypeSystem::Version>& version) const;

    // The C# `string? ResolveInternal(IAssemblyReference name)` -- the
    // search-directory walk, the special-version framework-directory arm,
    // the corlib arm, the GAC arm, the <= 4.0 framework-directory fallback,
    // and the shared-runtime last resort, then the `throwOnError` throw.
    std::optional<std::string> ResolveInternal(const IAssemblyReference& name) const;

    // The C# `SearchDirectory` pair -- the multi-directory walk (null
    // entries skipped) and the two-extension probe (`.winmd`/`.dll` for a
    // Windows-Runtime reference, `.dll`/`.exe` otherwise).
    std::optional<std::string> SearchDirectory(const IAssemblyReference& name,
        const std::vector<std::optional<std::string>>& directories) const;
    std::optional<std::string> SearchDirectory(const IAssemblyReference& name,
        const std::string& directory) const;

    // The C# `string? GetCorlib(...)` + `string? GetMscorlibBasePath(...)`
    // -- the corlib identity arm (the pinned host skips the
    // `decompilerRuntime != NETCoreApp` shortcut) and the Major/
    // MajorRevision subfolder table with the CompactFramework arm.
    std::optional<std::string> GetCorlib(const IAssemblyReference& reference) const;
    std::optional<std::string> GetMscorlibBasePath(const TypeSystem::Version& version,
        const std::optional<std::string>& publicKeyToken) const;

    // The C# readonly fields (the C# `baseDirectory` is write-only --
    // assigned in the ctor, never read -- and the port carries it for
    // fidelity).
    std::optional<std::string> mainAssemblyFileName_;
    bool throwOnError_ = false;
    PEStreamOptions streamOptions_ = PEStreamOptions::Default;
    MetadataReaderOptions metadataOptions_ = MetadataReaderOptions::Default;
    std::string targetFramework_;
    std::string runtimePack_;
    TargetFrameworkIdentifier targetFrameworkIdentifier_ =
        TargetFrameworkIdentifier::NETFramework;
    TypeSystem::Version targetFrameworkVersion_;
    std::optional<std::string> baseDirectory_;
    std::vector<std::optional<std::string>> directories_;
    mutable std::unique_ptr<DotNetCorePathFinder> dotNetCorePathFinder_;
};

// The port's hand-rolled equivalent of `EnumerateGac`'s
// `Regex.Match("(v4.0_)?(?<version>[^_]+)_(?<culture>[^_]+)?_(?<publicKey>[^_]+)")`
// folder-name matcher: the capture groups of the first-position match (an
// UNMATCHED culture group renders "" -- `EnumerateGac` maps that to
// "neutral", exactly the C# `IsNullOrEmpty` gate). The matcher is
// port-internal machinery; exposed here for the crafted-matrix tests (the
// standing internal-to-public convention).
struct GacFolderNameMatch {
    std::string Version;
    std::string Culture;
    std::string PublicKey;
};

bool TryMatchGacFolderName(
    std::string_view folderName, GacFolderNameMatch& match);

}  // namespace ILSpy::Decompiler::Metadata
