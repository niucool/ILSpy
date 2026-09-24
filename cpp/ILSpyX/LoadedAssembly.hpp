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

// Port of ICSharpCode.ILSpyX/LoadedAssembly.cs: a file loaded into ILSpy
// -- the lazy load pipeline through the FileLoaderRegistry and the
// PEFileLoader fallback, the load-status surface, and the display text.
//
// C#-to-C++ porting decisions:
//  * The C# lazy load (`Lazy<Task<LoadResult>>` + `Task.Run(LoadAsync)`)
//    ports SYNCHRONOUSLY: the port has no Task analogue (the
//    AssemblyNameReference.hpp precedent), so the load runs on the first
//    demand (GetLoadResult / GetMetadataFile / ...), guarded once under a
//    mutex. The async status arms collapse accordingly: IsLoaded is "the
//    load has run" (no in-flight window), HasLoadError is "the load
//    threw", and polling never triggers the load (the C#
//    Lazy.IsValueCreated contract).
//  * The C# `Exception? FileLoadException` slots carry the message (the
//    LoadResult.hpp convention); the final `throw result.FileLoadException`
//    and the GetMetadataFileAsync rethrow port as std::runtime_error with
//    that message.
//  * The C# BadImageFormatException ladder out of the PEFile ctor (the
//    port's MetadataFile never throws and reports IsValid() == false --
//    the established port divergence) maps back inside the load core:
//    the .NET messages are re-derived from the image bytes in
//    PEReader's stage order ("Unknown file format." / "Image is either
//    too small or contains an invalid byte offset or count." /
//    "Invalid PE signature." / the MetadataFileNotSupportedException
//    default "PE file does not contain any managed metadata." -- the
//    metadata-corruption stage merges into the last one).
//  * The C# ConditionalWeakTable<MetadataFile, LoadedAssembly> reverse
//    map ports as a process-wide registry keyed by const MetadataFile*;
//    ~LoadedAssembly erases its entries (the C# weak keys die with the
//    MetadataFile -- the port erases them with the owner).
//  * The C# `event Action? Loaded` ports as AddLoadedListener: fires once
//    when the load completes (success or failure); subscribing after
//    completion is not retroactive (the C# doc).
//  * The C# `string? TargetFrameworkIdOverride` setter's trim/blank
//    normalization is preserved: blank or whitespace-only values
//    normalize to no override, so the override never suppresses detection
//    with an empty effective TFM.
//  * The C# `Stream?` parameters port as the stream-provider convention:
//    `Options.Stream` is a function invoked at load time (the C#
//    `Task.Run(entry.TryOpenStream)` deferral); an empty function or a
//    nullopt result is the C# null stream (read the file from disk).
//  * IDisposable does not port (RAII: the MetadataFile dies with the
//    LoadedAssembly's lazy state, so the Dispose ordering comment -- flag
//    before unmapping -- has no C++ analogue, and the Text getter's
//    disposed arm is unreachable).
//  * The ILSpyXEventSource ETW instrumentation and the
//    ConditionalWeakTable.Add duplicate-key guard (unreachable: each load
//    constructs a fresh MetadataFile) do not port.
//  * DEFERRED to the resolver slice: GetAssemblyResolver /
//    GetUniversalResolver / GetAssemblyReferenceClassifier (the
//    MyAssemblyResolver nested class); to the debug-info slice:
//    LoadDebugInfo/GetDebugInfoOrNull; and GetTypeSystemOrNull (the
//    SimpleCompilation over MinimalCorlib).

#pragma once

#include "ILSpyX/FileLoaders/LoadResult.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
}  // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::ILSpyX {

class AssemblyList;

namespace FileLoaders {
class FileLoaderRegistry;
}  // namespace ILSpy::ILSpyX::FileLoaders

// The C# `public sealed class LoadedAssembly : IDisposable`.
class LoadedAssembly {
public:
    // The C# optional constructor parameters
    // (`Task<Stream?>? stream, FileLoaderRegistry? fileLoaders,
    // IAssemblyResolver? assemblyResolver, string? pdbFileName,
    // bool applyWinRTProjections, bool useDebugSymbols`).
    struct Options {
        // The C# `Task<Stream?>? stream`: invoked at load time (the C#
        // defers the stream task the same way); an empty function or a
        // nullopt result is the C# null stream (read the file from disk).
        std::function<std::optional<std::vector<std::uint8_t>>()> Stream;
        // The C# `FileLoaderRegistry? fileLoaders` (null = the
        // PEFileLoader fallback only).
        FileLoaders::FileLoaderRegistry* FileLoaders = nullptr;
        // The C# `IAssemblyResolver? assemblyResolver`.
        const Decompiler::Metadata::IAssemblyResolver* AssemblyResolver =
            nullptr;
        std::optional<std::string> PdbFileName;
        bool ApplyWinRTProjections = false;
        bool UseDebugSymbols = false;
    };

    // The C# `LoadedAssembly(AssemblyList, string fileName, ...)` -- the
    // ArgumentNullExceptions are N/A (reference parameters are non-null
    // by C++ semantics). The default-argument form is split into an
    // overload pair (a default argument over the aggregate's default
    // member initializers cannot be evaluated inside the enclosing-class
    // definition).
    LoadedAssembly(AssemblyList& assemblyList, std::string fileName);
    LoadedAssembly(AssemblyList& assemblyList, std::string fileName,
        Options options);

    // The C# `LoadedAssembly(LoadedAssembly bundle, string fileName, ...)`
    // -- the bundle-entry form (ParentBundle = the bundle wrapper).
    LoadedAssembly(LoadedAssembly& bundle, std::string fileName);
    LoadedAssembly(LoadedAssembly& bundle, std::string fileName,
        Options options);

    ~LoadedAssembly();

    LoadedAssembly(const LoadedAssembly&) = delete;
    LoadedAssembly& operator=(const LoadedAssembly&) = delete;

    // The C# `public AssemblyList AssemblyList { get; }` -- renamed (a
    // member named exactly like the type it returns).
    AssemblyList& GetAssemblyList() const { return *assemblyList_; }
    // The C# `public string FileName { get; }` / `ShortName`.
    const std::string& FileName() const { return fileName_; }
    const std::string& ShortName() const { return shortName_; }

    // The C# `public LoadedAssembly? ParentBundle { get; }` (null outside
    // a bundle).
    const LoadedAssembly* ParentBundle() const { return parentBundle_; }

    // The C# `public event Action? Loaded`: fires once when the load
    // completes (success or failure). Subscribing after completion is not
    // retroactive -- check IsLoaded() after subscribing (the C# doc).
    void AddLoadedListener(std::function<void()> listener);

    // --- the load demand and its status ---

    // The C# `public Task<LoadResult> GetLoadResultAsync()`: triggers the
    // load; rethrows the stored failure as std::runtime_error (the C#
    // await rethrow).
    const FileLoaders::LoadResult& GetLoadResult() const;

    // The C# `public async Task<MetadataFile> GetMetadataFileAsync()`:
    // throws the failure message, or the C# default
    // MetadataFileNotSupportedException message when the load succeeded
    // without a module (a package-only result).
    const Decompiler::Metadata::MetadataFile& GetMetadataFile() const;

    // The C# `public MetadataFile? GetMetadataFileOrNull()`: nullptr in
    // case of load errors (the C# Trace.TraceError of the exception does
    // not port).
    const Decompiler::Metadata::MetadataFile* GetMetadataFileOrNull() const;

    // The C# `public bool IsLoaded`: the load has run (triggered and
    // completed -- the port's synchronous load has no in-flight window).
    // Reading it must NOT trigger the load.
    bool IsLoaded() const;

    // The C# `public bool IsLoadedAsValidAssembly`: loaded successfully
    // as an assembly (not as a bundle). The port's PE-only MetadataFile
    // is always the full-PE shape (the C# `IsMetadataOnly: false` arm),
    // so the check reduces to a non-null module.
    bool IsLoadedAsValidAssembly() const;

    // The C# `public bool HasLoadError`: the load threw (the C# faulted
    // task).
    bool HasLoadError() const;

    // The C# `public bool IsAutoLoaded { get; set; }`.
    bool IsAutoLoaded() const { return isAutoLoaded_; }
    void SetIsAutoLoaded(bool value) { isAutoLoaded_ = value; }

    // The C# `public string? PdbFileName { get; private set; }`.
    const std::optional<std::string>& PdbFileName() const
    {
        return pdbFileName_;
    }

    // --- the target framework surface ---

    // The C# `public string GetTargetFrameworkIdAsync()`: the effective
    // '<framework>,Version=v<version>' identifier; an explicit override
    // wins over (and bypasses) the detection, even once detection has
    // cached a value.
    std::string GetTargetFrameworkId() const;

    // The C# `public string GetDetectedTargetFrameworkIdAsync()`: the
    // detected identifier, or "" when no TargetFrameworkAttribute was
    // found; throws when the file has no .NET metadata.
    std::string GetDetectedTargetFrameworkId() const;

    // The C# `public string GetRuntimePackAsync()`.
    std::string GetRuntimePack() const;

    // The C# `public string? TargetFrameworkIdOverride` with the
    // trim/blank normalization (a non-null override is always a usable
    // TFM).
    const std::optional<std::string>& TargetFrameworkIdOverride() const
    {
        return targetFrameworkIdOverride_;
    }
    void SetTargetFrameworkIdOverride(std::optional<std::string> value);

    // --- the display surface ---

    // The C# `public string Text`: "ShortName (version[, TFM])" for an
    // assembly, "ShortName (.netmodule)" for a netmodule, the bare
    // ShortName before a completed load or after a failure. The
    // ProgramDebugDatabase / Metadata kind arms do not port (the port's
    // MetadataFile is the PE shape only).
    std::string Text() const;

private:
    void EnsureLoaded() const;
    FileLoaders::LoadResult LoadCore() const;
    void SetPdbFileName(std::optional<std::string> value);

    AssemblyList* assemblyList_;
    std::string fileName_;
    std::string shortName_;
    Options options_;
    LoadedAssembly* parentBundle_ = nullptr;

    // The load state (the C# Lazy<Task<LoadResult>>), guarded by
    // loadMutex_. loadTriggered_ is the C# Lazy.IsValueCreated (set
    // before the work runs, so the status members can poll it without
    // triggering); faulted_/faultMessage_ the C# faulted-task shape.
    mutable std::mutex loadMutex_;
    mutable bool loadTriggered_ = false;
    mutable bool faulted_ = false;
    mutable std::string faultMessage_;
    mutable std::optional<FileLoaders::LoadResult> loadResult_;
    // The Loaded event state: listeners queued before completion fire
    // once when it completes; later ones are dropped (not retroactive).
    mutable std::vector<std::function<void()>> loadedListeners_;
    mutable bool loadedFired_ = false;
    // The reverse-map registrations this instance owns (erased in the
    // destructor -- the C# weak-key cleanup).
    mutable std::vector<const Decompiler::Metadata::MetadataFile*>
        loadedFiles_;

    // The C# cached display text (computed once; the disposed arm of the
    // C# cache comment is unreachable in the port).
    mutable std::optional<std::string> cachedText_;
    // The C# LazyInit targetFrameworkId / runtimePair caches.
    mutable std::optional<std::string> targetFrameworkId_;
    mutable std::optional<std::string> runtimePack_;
    std::optional<std::string> targetFrameworkIdOverride_;
    std::optional<std::string> pdbFileName_;
    bool isAutoLoaded_ = false;
};

}  // namespace ILSpy::ILSpyX
