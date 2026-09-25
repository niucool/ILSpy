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
//    ports as the promise/thread pattern from the design doc in
//    PORT_LOG_BAML.md (the bennu session.cpp mapping, commit 0dedade):
//    the first demand starts one worker thread; the load state mutex
//    guards only the state transition (the started flag + the future
//    slot) -- LoadCore() runs unlocked on the worker, so a load that
//    recursively demands another assembly's load cannot self-deadlock,
//    and status polls proceed while a load is in flight. Concurrent
//    demanders share the one in-flight load through the future (the C#
//    in-flight task dedup). The worker catches everything and always
//    satisfies the promise (bennu rule 3), storing the failure into the
//    fault slots instead of the promise state (bennu swallows; here the
//    C# faulted-task surface IS the result channel). The Loaded event
//    fires on the worker thread after the promise is satisfied (the C#
//    ContinueWith continuation on TaskScheduler.Default). The library
//    always joins (the destructor waits for an in-flight load); the
//    bennu detach-on-deadline arm belongs to the HOST (a CLI may
//    _Exit the process on a deadline -- the detached worker dies with
//    it), never to this class.
//  * The async status arms: IsLoaded is "demanded AND completed" (the
//    C# `Lazy.IsValueCreated && Value.IsCompleted` window -- false while
//    the load runs), HasLoadError the C# `IsFaulted` (false while the
//    load runs), and polling still never triggers the load.
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

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"

#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"
#include "ILSpyX/FileLoaders/LoadResult.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
class IAssemblyReferenceClassifier;
class UniversalAssemblyResolver;
}  // namespace ILSpy::Decompiler::Metadata

#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

namespace ILSpy::Decompiler::TypeSystem {
class ICompilation;
class IModuleReference;
}  // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::ILSpyX {

class AssemblyList;

class AssemblyListSnapshot;

namespace FileLoaders {
class FileLoaderRegistry;
struct LoadResult;
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
    // completed). Reading it must NOT trigger the load.
    bool IsLoaded() const;

    // The C# `public bool IsLoadedAsValidAssembly`: loaded successfully
    // as an assembly (not as a bundle). The port's PE-only MetadataFile
    // is always the full-PE shape (the C# `IsMetadataOnly: false` arm),
    // so the check reduces to a non-null module.
    bool IsLoadedAsValidAssembly() const;

    // The C# `public bool HasLoadError`: the load threw (the C# faulted
    // task).
    bool HasLoadError() const;

    // The bounded wait (the bennu host-edge primitive; no C#
    // analogue -- the C# host would Task.Wait(timeout)). A demand: it
    // starts the load if it has not started, then waits at most the
    // deadline. True when the load completed within the deadline; false
    // while it is still in flight (the caller must keep the object
    // alive until the load completes -- the destructor joins). The
    // deadline arm stops here: detach belongs to the host, which may
    // _Exit the process on a hard deadline (the bennu session.cpp
    // pattern) -- the library never abandons a load.
    bool WaitForLoaded(std::chrono::milliseconds timeout) const;

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

    // --- the debug-info and type-system surface (the LoadDebugInfo half
    // of the C# class) ---

    // The C# `public IDebugInfoProvider? GetDebugInfoOrNull()`: null on
    // load errors or when no debug info is available.
    std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
    GetDebugInfoOrNull() const;

    // The C# `public async Task<IDebugInfoProvider?> LoadDebugInfo(string
    // fileName)`: sets the PDB file name and (re)loads the provider.
    std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider> LoadDebugInfo(
        std::string fileName);

    // The C# `public ICompilation? GetTypeSystemOrNull()`: the uncached
    // compilation over the module plus the minimal corlib; null on load
    // errors or for a metadata-only module. Cached (the C# LazyInit).
    std::shared_ptr<Decompiler::TypeSystem::ICompilation> GetTypeSystemOrNull()
        const;

    // The C# `public ICompilation? GetTypeSystemOrNull(TypeSystemOptions
    // options)`: the options-keyed variant (rebuilt when the options
    // change; the C# lock + currentOptions pair).
    std::shared_ptr<Decompiler::TypeSystem::ICompilation> GetTypeSystemOrNull(
        Decompiler::TypeSystem::TypeSystemOptions options) const;

    // --- the resolver integration (the MyAssemblyResolver half) ---

    // The C# `public IAssemblyResolver GetAssemblyResolver(bool
    // loadOnDemand = true, bool applyWinRTProjections = false)`: the
    // MyAssemblyResolver over a fresh AssemblyList snapshot. Caller-owned
    // (the C# GC owns the resolver).
    std::unique_ptr<Decompiler::Metadata::IAssemblyResolver>
    GetAssemblyResolver(bool loadOnDemand = true,
        bool applyWinRTProjections = false) const;

    // The C# `internal IAssemblyResolver GetAssemblyResolver(
    // AssemblyListSnapshot snapshot, bool loadOnDemand = true, bool
    // applyWinRTProjections = false)`.
    std::unique_ptr<Decompiler::Metadata::IAssemblyResolver>
    GetAssemblyResolver(const class AssemblyListSnapshot& snapshot,
        bool loadOnDemand = true, bool applyWinRTProjections = false) const;

    // The C# `public AssemblyReferenceClassifier
    // GetAssemblyReferenceClassifier(bool applyWinRTProjections)`: the
    // lazy universal resolver (created on first demand with the flag the
    // FIRST call passes -- the C# LazyInitializer caches regardless of
    // later flags). Non-owning; the LoadedAssembly outlives it.
    const Decompiler::Metadata::IAssemblyReferenceClassifier&
    GetAssemblyReferenceClassifier(bool applyWinRTProjections = false) const;

    // The C# `public ReferenceLoadInfo LoadedAssemblyReferencesInfo { get;
    // }` -- the resolver's per-reference diagnostics. The accessor is
    // const and returns a mutable reference (the C# property hands out
    // the shared mutable object; the member is mutable to match).
    Decompiler::Metadata::ReferenceLoadInfo& LoadedAssemblyReferencesInfo()
        const
    {
        return loadedAssemblyReferencesInfo_;
    }

    // --- the display surface ---

    // The C# `public string Text`: "ShortName (version[, TFM])" for an
    // assembly, "ShortName (.netmodule)" for a netmodule, the bare
    // ShortName before a completed load or after a failure. The
    // ProgramDebugDatabase / Metadata kind arms do not port (the port's
    // MetadataFile is the PE shape only).
    std::string Text() const;

private:
    // The start-once step both EnsureLoaded and WaitForLoaded share:
    // starts the worker on the first call, returns the shared future
    // (the mutex guards only this transition).
    std::shared_future<void> StartLoad() const;
    void EnsureLoaded() const;
    // The worker body: runs LoadCore() with no lock held, records the
    // outcome in the fault slots, then satisfies the promise and fires
    // the Loaded listeners (in that order -- a waiter waking on the
    // promise observes the landed state).
    void RunLoad(std::shared_ptr<std::promise<void>> done) const;
    FileLoaders::LoadResult LoadCore() const;
    void SetPdbFileName(std::optional<std::string> value);

    // The C# `IDebugInfoProvider? LoadDebugInfo(PEFile? module)` / the
    // LoadDebugInfoCore core: the FromFile-then-LoadSymbols chain when
    // useDebugSymbols is set, null otherwise. The exceptions the port's
    // DebugInfoUtils throws (the std family) collapse into the null
    // result, like the C# catch set.
    std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider> LoadDebugInfo(
        const Decompiler::Metadata::MetadataFile* module) const;
    std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
    LoadDebugInfoCore(const Decompiler::Metadata::MetadataFile* module) const;

    // The C# `private UniversalAssemblyResolver GetUniversalResolver(bool
    // applyWinRTProjections)` -- the lazy resolver construction (the
    // rooted-file-name / TFM / runtime-pack / reader-options capture).
    const Decompiler::Metadata::UniversalAssemblyResolver&
    GetUniversalResolver(bool applyWinRTProjections) const;

    // The C# private nested `sealed class MyAssemblyResolver :
    // IAssemblyResolver` -- the resolution step order over the parent, a
    // snapshot, the load-on-demand flag, and the winrt flag. Declared
    // here (the nested-classes-public convention); defined in the .cpp.
    class MyAssemblyResolver;

    AssemblyList* assemblyList_;
    std::string fileName_;
    std::string shortName_;
    Options options_;
    LoadedAssembly* parentBundle_ = nullptr;

    // The load state (the C# Lazy<Task<LoadResult>>), guarded by
    // loadMutex_, which protects only the state transition -- never the
    // load itself (LoadCore runs on the worker with no lock held). The
    // promise/thread machinery follows the bennu pattern (the design
    // doc in PORT_LOG_BAML.md): the shared future lets concurrent
    // demanders await the one in-flight load; the worker satisfies it
    // unconditionally after recording the outcome. loadStarted_ is the
    // C# Lazy.IsValueCreated; loadDone_'s readiness is the C#
    // Task.IsCompleted (the IsLoaded in-flight window); faulted_/
    // faultMessage_ the C# faulted-task shape.
    mutable std::mutex loadMutex_;
    mutable bool loadStarted_ = false;
    mutable std::shared_future<void> loadDone_;
    mutable std::unique_ptr<std::thread> loadThread_;
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
    // The C# `ReferenceLoadInfo LoadedAssemblyReferencesInfo { get; }`
    // (mutable: the const accessors hand out the shared mutable object,
    // the C# shared-mutable-state convention).
    mutable Decompiler::Metadata::ReferenceLoadInfo
        loadedAssemblyReferencesInfo_;
    // The C# `UniversalAssemblyResolver? universalResolver` lazy (created
    // on first demand; mutable -- the accessor is const, the C#
    // LazyInitializer is thread-safe).
    mutable std::mutex universalResolverMutex_;
    mutable std::unique_ptr<Decompiler::Metadata::UniversalAssemblyResolver>
        universalResolver_;
    // The debug-info provider (the C# `IDebugInfoProvider?
    // debugInfoProvider` field; filled during the load when
    // useDebugSymbols is set, or by the LoadDebugInfo(fileName) call).
    mutable std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
        debugInfoProvider_;
    // The C# `ICompilation? typeSystem` cache (the default-options
    // variant) and the options-keyed variant (the C#
    // typeSystemWithOptions / currentTypeSystemOptions pair under
    // typeSystemWithOptionsLockObj). Each cache owns the WithOptions
    // module reference the compilation was built over (the C# GC owns
    // it).
    struct CachedTypeSystem {
        std::unique_ptr<Decompiler::TypeSystem::IModuleReference>
            moduleReference;
        std::shared_ptr<Decompiler::TypeSystem::ICompilation> compilation;
    };
    mutable std::mutex typeSystemMutex_;
    mutable std::optional<CachedTypeSystem> typeSystem_;
    mutable std::optional<CachedTypeSystem> typeSystemWithOptions_;
    mutable std::optional<Decompiler::TypeSystem::TypeSystemOptions>
        currentTypeSystemOptions_;
};

}  // namespace ILSpy::ILSpyX
