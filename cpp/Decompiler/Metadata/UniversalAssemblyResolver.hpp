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

// Port of ICSharpCode.Decompiler/Metadata/UniversalAssemblyResolver.cs -- the
// FIRST sub-slice of the resolver: the three enums the file declares and the
// `internal static ParseTargetFramework` classifier both
// `DotNetCorePathFinder.GetReferenceAssemblyPath` and the resolver ctor
// consume. The `UniversalAssemblyResolver` class body itself (the
// `AssemblyReferenceClassifier` inheritance, the GAC handling, and the
// `Resolve`/`FindAssemblyFile` surface) stays a documented deferral until
// the following slice; `ParseTargetFramework` lands as a free function at
// the mirror-layout position so the class body can adopt it unchanged.
//
// The C# member is `internal static`; the port exposes it publicly (the
// standing internal-to-public convention for test access).

#pragma once

#include "Decompiler/TypeSystem/Version.hpp"

#include <string>

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

}  // namespace ILSpy::Decompiler::Metadata
