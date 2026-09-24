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

// Port of ICSharpCode.ILSpyX/LoadedAssemblyExtensions.cs: the extension
// methods over a MetadataFile that reach back to the LoadedAssembly that
// loaded it (the C# static class ports as free functions in the ILSpyX
// namespace -- the SRMExtensions free-function convention).
//
// C#-to-C++ porting decisions:
//  * `this MetadataFile` extension methods port as free functions taking
//    the file first.
//  * The C# `CreateCecilObjectModel` does not port (the Mono.Cecil bridge
//    the plan drops).
//  * `GetTypeSystemWithDecompilerSettingsOrNull` is deferred: it needs
//    DecompilerTypeSystem.GetOptions (the Phase 5 CSharpDecompiler
//    settings mapping, not yet ported).

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"

#include "ILSpyX/LoadedAssembly.hpp"

#include <memory>

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
}  // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::Decompiler::DebugInfo {
class IDebugInfoProvider;
}  // namespace ILSpy::Decompiler::DebugInfo

#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

namespace ILSpy::ILSpyX {

class LoadedAssembly;
class AssemblyListSnapshot;

// The C# `public static LoadedAssembly GetLoadedAssembly(this MetadataFile
// file)`: throws std::invalid_argument with the C# ArgumentException
// message for a file no LoadedAssembly loaded.
LoadedAssembly& GetLoadedAssembly(
    const Decompiler::Metadata::MetadataFile& file);

// The C# `GetAssemblyResolver(this MetadataFile, bool loadOnDemand = true)`
// / the snapshot overload.
std::unique_ptr<Decompiler::Metadata::IAssemblyResolver> GetAssemblyResolver(
    const Decompiler::Metadata::MetadataFile& file,
    bool loadOnDemand = true);
std::unique_ptr<Decompiler::Metadata::IAssemblyResolver> GetAssemblyResolver(
    const Decompiler::Metadata::MetadataFile& file,
    const AssemblyListSnapshot& snapshot, bool loadOnDemand = true);

// The C# `public static IDebugInfoProvider? GetDebugInfoOrNull(this
// MetadataFile file)`.
std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider> GetDebugInfoOrNull(
    const Decompiler::Metadata::MetadataFile& file);

// The C# `public static ICompilation? GetTypeSystemOrNull(this MetadataFile
// file)`.
std::shared_ptr<Decompiler::TypeSystem::ICompilation> GetTypeSystemOrNull(
    const Decompiler::Metadata::MetadataFile& file);

}  // namespace ILSpy::ILSpyX
