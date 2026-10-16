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

// Port of ICSharpCode.Decompiler/Metadata/DotNetCorePathFinderExtensions.cs:
// the target-framework detection family the CLI passes to the
// UniversalAssemblyResolver ctor (`new UniversalAssemblyResolver(fileName,
// false, module.Metadata.DetectTargetFrameworkId())`) and the runtime-pack /
// reference-assembly classifiers beside it. The port models the MetadataReader
// as the MetadataFile (the MetadataGenericContext convention), so the two C#
// overloads per member become:
//   * DetectTargetFrameworkId(const MetadataFile&) -- the MetadataFile
//     extension (uses FileName() as the assembly path);
//   * DetectTargetFrameworkId(const MetadataFile&, path) -- the
//     MetadataReader overload with the explicit (nullable) path; a nullopt
//     path skips the path-pattern fallback entirely (the C# `assemblyPath ==
//     null` guard), returning the empty string where a non-matching path
//     would have taken the MetadataVersion no-match arm.
//
// The C# string returns are nullable (the attribute arm's
// `ReadSerializedString()?.Replace(" ", "")` propagates the SerString 0xFF
// null form), so the port returns std::optional<std::string>: nullopt is the
// C# null, an engaged empty string is the C# "".

#pragma once

#include "Decompiler/TypeSystem/Version.hpp"

#include <optional>
#include <string>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

// The C# `internal static string GetDotNetCoreVersion(Version
// assemblyVersion)` -- the System.Runtime/System.Private.CoreLib
// assembly-version to .NET Core mapping:
//   4.1.0 -> "1.1" (Core 1.0/1.1), 4.2.0 -> "2.0", 4.2.1 -> "3.0",
//   4.2.2 -> "3.1", 5.0.0+ -> ToString(2), anything else -> null.
std::optional<std::string> GetDotNetCoreVersion(const TypeSystem::Version& version);

// The C# `public static string DetectTargetFrameworkId(this MetadataFile
// assembly)` -- the core detection over the assembly's own FileName.
std::optional<std::string> DetectTargetFrameworkId(const MetadataFile& assembly);

// The C# `public static string DetectTargetFrameworkId(this MetadataReader
// metadata, string assemblyPath = null)` -- the core detection:
//   * the TargetFrameworkAttribute walk over the assembly-definition
//     custom-attribute rows (the attribute type's full name must equal
//     "System.Runtime.Versioning.TargetFrameworkAttribute"; the value blob's
//     prolog must be 0x0001 and the fixed string arg is returned with every
//     space removed; a malformed attribute is skipped by the
//     BadImageFormatException catch, and a wrong prolog continues the walk);
//   * the assembly-name arm (an assembly manifest named mscorlib /
//     netstandard / System.Runtime / System.Private.CoreLib);
//   * the two AssemblyReference passes (mscorlib /
//     System.Runtime / System.Private.CoreLib refs with a non-nil
//     PublicKeyOrToken, then the netstandard pass);
//   * the assembly-path regex fallback (the six alternatives of the C#
//     PathPattern) and the MetadataVersion no-match arm; a nullopt path
//     skips the fallback and returns "".
std::optional<std::string> DetectTargetFrameworkId(
    const MetadataFile& metadata, const std::optional<std::string>& assemblyPath);

// The C# `public static bool IsReferenceAssembly(this MetadataFile assembly)`
// -- the [ReferenceAssembly] marker over the assembly-definition custom
// attributes, else the RefPathPattern path check.
bool IsReferenceAssembly(const MetadataFile& assembly);

// The C# `public static bool IsReferenceAssembly(this MetadataReader
// metadata, string assemblyPath)` -- the same check with the explicit path.
// The C# Regex.Match(null, ...) ArgumentNullException maps to
// std::invalid_argument carrying the .NET message (the path is never null
// through the MetadataFile overload -- the C# FileName is never null).
bool IsReferenceAssembly(const MetadataFile& metadata,
                         const std::optional<std::string>& assemblyPath);

// The C# `public static string DetectRuntimePack(this MetadataFile assembly)`
// -- the AssemblyReference scan returning "Microsoft.WindowsDesktop.App" when
// a non-nil-key reference is named WindowsBase / PresentationFramework /
// PresentationCore (the SRM StringComparer.Equals(handle, value) default is
// case-SENSITIVE, so a differently-cased name never matches), else
// "Microsoft.NETCore.App".
std::string DetectRuntimePack(const MetadataFile& assembly);

}  // namespace ILSpy::Decompiler::Metadata
