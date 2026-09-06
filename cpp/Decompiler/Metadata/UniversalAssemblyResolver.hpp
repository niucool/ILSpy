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
// The SECOND (this slice): the class's STATIC half -- the `#region .NET /
// mono GAC handling` machinery (`GetGacPaths` / `GetAssemblyInGac` /
// `EnumerateGac` / `IsZeroOrAllOnes` / `IsSpecialVersionOrRetargetable` /
// `GetAssemblyFile` and the `gac_paths` static field) plus the
// `AssemblyReferenceClassifier` base it derives. Every member is
// gold-pinned against the real installed resolver over this machine's
// real .NET Framework GAC.
//
// The class's INSTANCE surface stays a documented deferral until the
// following slices (each is additive to the class below):
//   * the `IAssemblyResolver` base itself (the interface's two members are
//     the `Resolve`/`ResolveModule` pair) plus the ctor
//     (`mainAssemblyFileName`/`throwOnError`/`streamOptions`/
//     `metadataOptions`, the `Lazy<DotNetCorePathFinder>` wiring) and
//     `AddSearchDirectory`/`RemoveSearchDirectory`/
//     `GetSearchDirectories`;
//   * `IsSharedAssembly` (the override resolving through the
//     `dotNetCorePathFinder` Lazy) and `FindAssemblyFile`/
//     `FindAssemblyFileCore` (the target-framework dispatch, consuming
//     `TypeReferenceMetadata`/`ExportedTypeMetadata`);
//   * `FindWindowsMetadataFile`/`FindWindowsMetadataInSystemDirectory`,
//     `ResolveSilverlight`, `FindClosestVersionDirectory`,
//     `ResolveInternal`, `GetCorlib`/`GetMscorlibBasePath`, and
//     `CreatePEFileFromFileName`.
//
// The Mono arms (`GetDefaultMonoGacPaths`/`GetCurrentMonoGac`/
// `GetAssemblyInMonoGac`/`GetMonoMscorlibBasePath` and the Mono static ctor
// detection) read the decompiler HOST's own Mono runtime module layout
// (`typeof(object).Module.FullyQualifiedName`) and are reachable only
// through `decompilerRuntime == Mono` -- the port pins the host statically
// as `kDecompilerRuntime = NETCoreApp` (the ilspycmd 11.0 build this port is
// gold-pinned against runs on .NET 10), so every Mono arm is unreachable in
// the shipped-tool shape and stays unported.
//
// The C# members are `internal`/`private`; the port exposes the statics
// publicly (the standing internal-to-public convention for test access).

#pragma once

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

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
