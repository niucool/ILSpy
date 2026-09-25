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

#include "ILSpyX/LoadedAssembly.hpp"

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"
#include "ILSpyX/FileLoaders/PEFileLoader.hpp"
#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"
#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"

#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"
#include "ILSpyX/FileLoaders/PEFileLoader.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace ILSpy::ILSpyX {

namespace {

// The C# `internal static readonly ConditionalWeakTable<MetadataFile,
// LoadedAssembly> loadedAssemblies` -- the successfully loaded modules
// mapped back to the wrapper that loaded them. The C# table's keys are
// weak (an entry dies with its MetadataFile); the port erases the entries
// in ~LoadedAssembly, which is exactly when the module dies (the module
// is owned by the wrapper's load result).
std::mutex gLoadedAssembliesMutex;
std::unordered_map<const Decompiler::Metadata::MetadataFile*,
    const LoadedAssembly*>
    gLoadedAssemblies;

// The C# `Path.GetFileNameWithoutExtension` (the IlspyCmdProgram shape):
// the file-name component after the last separator, minus everything from
// the last dot.
std::string GetFileNameWithoutExtension(const std::string& path)
{
    std::size_t sep = path.find_last_of("\\/");
    std::string name = sep == std::string::npos ? path : path.substr(sep + 1);
    std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// The C# BadImageFormatException the port's never-throwing MetadataFile
// stands in for: the PEReader ctor's message ladder, re-derived from the
// image bytes in PEReader's stage order. The probed messages (ilspycmd
// 11.0.0.9335-rc, see PORT_LOG_BAML.md): a non-PE blob yields
// "Unknown file format."; an MZ blob whose DOS header or e_lfanew cannot
// be honored yields "Image is either too small or contains an invalid
// byte offset or count."; an in-range e_lfanew without the PE signature
// yields "Invalid PE signature."; a well-formed PE header whose CLR
// metadata does not parse yields the MetadataFileNotSupportedException
// default "PE file does not contain any managed metadata." (the corrupt-
// metadata stage merges into the last one -- the port cannot distinguish
// a missing CLR directory from corrupt metadata bytes).
std::string InvalidImageMessage(
    const std::optional<std::vector<std::uint8_t>>& image)
{
    const auto& bytes = *image;
    if (bytes.size() < 2 || bytes[0] != 'M' || bytes[1] != 'Z') {
        return "Unknown file format.";
    }
    if (bytes.size() < 64) {
        return "Image is either too small or contains an invalid byte "
               "offset or count.";
    }
    const auto eLfanew =
        static_cast<std::size_t>(bytes[60]) |
        (static_cast<std::size_t>(bytes[61]) << 8) |
        (static_cast<std::size_t>(bytes[62]) << 16) |
        (static_cast<std::size_t>(bytes[63]) << 24);
    if (eLfanew < 64 || eLfanew + 4 > bytes.size()) {
        return "Image is either too small or contains an invalid byte "
               "offset or count.";
    }
    if (bytes[eLfanew] != 'P' || bytes[eLfanew + 1] != 'E' ||
        bytes[eLfanew + 2] != 0 || bytes[eLfanew + 3] != 0) {
        return "Invalid PE signature.";
    }
    return "PE file does not contain any managed metadata.";
}

// The C# `string.Replace(string, string)` -- all occurrences, scanning
// for the next match after each replacement.
std::string ReplaceAll(std::string text, const std::string& from,
    const std::string& to)
{
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

}  // namespace

// The C# `internal static ConditionalWeakTable<MetadataFile,
// LoadedAssembly> loadedAssemblies` lookup arm (the
// LoadedAssemblyExtensions backend).
const LoadedAssembly* FindLoadedAssembly(
    const Decompiler::Metadata::MetadataFile& file)
{
    std::lock_guard<std::mutex> lock(gLoadedAssembliesMutex);
    const auto it = gLoadedAssemblies.find(&file);
    return it != gLoadedAssemblies.end() ? it->second : nullptr;
}

LoadedAssembly::LoadedAssembly(AssemblyList& assemblyList,
    std::string fileName)
    : LoadedAssembly(assemblyList, std::move(fileName), Options())
{
}

LoadedAssembly::LoadedAssembly(AssemblyList& assemblyList,
    std::string fileName, Options options)
    : assemblyList_(&assemblyList), fileName_(std::move(fileName)),
      options_(std::move(options))
{
    shortName_ = GetFileNameWithoutExtension(fileName_);
    pdbFileName_ = options_.PdbFileName;
}

LoadedAssembly::LoadedAssembly(LoadedAssembly& bundle, std::string fileName)
    : LoadedAssembly(bundle, std::move(fileName), Options())
{
}

LoadedAssembly::LoadedAssembly(LoadedAssembly& bundle, std::string fileName,
    Options options)
    : LoadedAssembly(bundle.GetAssemblyList(), std::move(fileName),
          std::move(options))
{
    parentBundle_ = &bundle;
}

LoadedAssembly::~LoadedAssembly()
{
    // Join the load worker first (the bennu rule: join on the normal
    // path): the worker touches this object's state and the registry
    // below, so the object must not die under a live load. Detach is a
    // host concern (a CLI may _Exit on a deadline); the library never
    // detaches.
    if (loadThread_ && loadThread_->joinable()) {
        loadThread_->join();
    }
    std::lock_guard<std::mutex> lock(gLoadedAssembliesMutex);
    for (const auto* file : loadedFiles_) {
        gLoadedAssemblies.erase(file);
    }
}

void LoadedAssembly::AddLoadedListener(std::function<void()> listener)
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    // The C# event fires once when the load task completes; subscribing
    // after completion is not retroactive.
    if (!loadedFired_) {
        loadedListeners_.push_back(std::move(listener));
    }
}

std::shared_future<void> LoadedAssembly::StartLoad() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    if (!loadStarted_) {
        loadStarted_ = true;
        // The promise is captured by the worker; the mutex guards only
        // this transition (LoadCore itself runs unlocked, so a load that
        // recursively demands another assembly's load cannot
        // self-deadlock, and status polls proceed while the load runs).
        auto done = std::make_shared<std::promise<void>>();
        loadDone_ = done->get_future().share();
        loadThread_ = std::make_unique<std::thread>(
            [this, done] { RunLoad(done); });
    }
    return loadDone_;
}

void LoadedAssembly::EnsureLoaded() const
{
    // Every demander (including the one that started the work) waits on
    // the shared future -- the C# `await loadingTask`.
    StartLoad().wait();
}

bool LoadedAssembly::WaitForLoaded(
    std::chrono::milliseconds timeout) const
{
    // The demand plus the bounded wait (the bennu wait_for arm): the
    // deadline arm stops here -- the library never abandons a load; a
    // host applying detach must _Exit the process (session.cpp) or keep
    // the object alive until the load lands (the destructor joins).
    return StartLoad().wait_for(timeout) == std::future_status::ready;
}

void LoadedAssembly::RunLoad(
    std::shared_ptr<std::promise<void>> done) const
{
    std::vector<std::function<void()>> listeners;
    try {
        FileLoaders::LoadResult result = LoadCore();
        std::lock_guard<std::mutex> lock(loadMutex_);
        loadResult_ = std::move(result);
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(loadMutex_);
        faulted_ = true;
        faultMessage_ = ex.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(loadMutex_);
        faulted_ = true;
        faultMessage_ = "Exception of type 'System.Exception' was thrown.";
    }
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        // The C# `task.ContinueWith(... RaiseLoaded())`: fires on success
        // and failure alike, exactly once.
        loadedFired_ = true;
        listeners = std::move(loadedListeners_);
        loadedListeners_.clear();
    }
    // Always satisfy the promise (bennu rule 3): the failure was already
    // recorded in the fault slots, so the future carries completion, not
    // the exception. A waiter waking here observes the landed state.
    done->set_value();
    // Fired with every lock released (a listener may re-enter the status
    // surface), on the worker thread -- the C# continuation's pool
    // thread.
    for (auto& listener : listeners) {
        listener();
    }
}

FileLoaders::LoadResult LoadedAssembly::LoadCore() const
{
    // The C# `PrepareStream`: the pre-crafted stream, or the file off
    // disk. A stream that cannot seek ports as materialized bytes either
    // way (the C# copies it into a MemoryStream).
    std::optional<std::vector<std::uint8_t>> image;
    if (options_.Stream) {
        image = options_.Stream();
    }
    if (!image.has_value()) {
        std::ifstream in(fileName_, std::ios::binary);
        if (!in) {
            // The C# FileStream ctor's FileNotFoundException message.
            throw std::runtime_error(
                "Could not find file '" + fileName_ + "'.");
        }
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        image = std::move(bytes);
    }

    FileLoaders::FileLoadContext context;
    context.ApplyWinRTProjections = options_.ApplyWinRTProjections;
    context.ParentBundle = parentBundle_;

    // The C# loader loop: each loader may decline (null), succeed (break),
    // or fail (its error replaces any previous error). A loader that
    // throws behaves exactly like one that reports a failure.
    std::optional<FileLoaders::LoadResult> result;
    const auto consider = [&](std::optional<FileLoaders::LoadResult> next) {
        if (next.has_value()) {
            if (next->IsSuccess() && next->MetadataFile != nullptr &&
                !next->MetadataFile->IsValid()) {
                // The never-throwing MetadataFile divergence shim: the
                // C# PEFile ctor threw before any result existed; map
                // the invalid image back to the .NET exception message
                // and continue the loop (the C# catch arm does not
                // break either).
                FileLoaders::LoadResult failure;
                failure.FileLoadException = InvalidImageMessage(image);
                result = std::move(failure);
                return;
            }
            result = std::move(next);
        }
    };

    if (options_.FileLoaders != nullptr) {
        for (const auto& loader : options_.FileLoaders->RegisteredLoaders()) {
            try {
                consider(loader->Load(fileName_, image->data(), image->size(),
                    context));
                if (result.has_value() && result->IsSuccess()) {
                    break;
                }
            } catch (const std::exception& ex) {
                FileLoaders::LoadResult failure;
                failure.FileLoadException = ex.what();
                result = std::move(failure);
            }
        }
    }

    // The C# fallback: the direct PEFileLoader.LoadPEFile call when no
    // registry loader produced a success.
    if (!result.has_value() || !result->IsSuccess()) {
        try {
            consider(FileLoaders::PEFileLoader::LoadPEFile(fileName_,
                image->data(), image->size(), context));
        } catch (const std::exception& ex) {
            FileLoaders::LoadResult failure;
            failure.FileLoadException = ex.what();
            result = std::move(failure);
        }
    }

    // The C# final arms: register a loaded module, stamp a package, or
    // rethrow the failure (the C# `throw result.FileLoadException;`).
    if (result->MetadataFile != nullptr) {
        // The C# `if (result.MetadataFile is PEFile module)
        // debugInfoProvider = LoadDebugInfo(module);` -- the provider is
        // only attempted for a successfully parsed module.
        debugInfoProvider_ = LoadDebugInfo(result->MetadataFile.get());
        std::lock_guard<std::mutex> registryLock(gLoadedAssembliesMutex);
        // The C# ConditionalWeakTable.Add throws on a duplicate key; a
        // duplicate is unreachable -- each load constructs a fresh
        // MetadataFile.
        gLoadedAssemblies.emplace(result->MetadataFile.get(), this);
        loadedFiles_.push_back(result->MetadataFile.get());
    } else if (result->Package != nullptr) {
        result->Package->SetLoadedAssembly(*this);
    } else if (result->FileLoadException.has_value()) {
        throw std::runtime_error(*result->FileLoadException);
    }
    return std::move(*result);
}

const FileLoaders::LoadResult& LoadedAssembly::GetLoadResult() const
{
    EnsureLoaded();
    if (faulted_) {
        // The C# `await loadingTask` rethrowing the faulted task's
        // exception.
        throw std::runtime_error(faultMessage_);
    }
    return *loadResult_;
}

const Decompiler::Metadata::MetadataFile& LoadedAssembly::GetMetadataFile()
    const
{
    const auto& result = GetLoadResult();
    if (result.MetadataFile != nullptr) {
        return *result.MetadataFile;
    }
    // The C# `throw loadResult.FileLoadException ??
    // new MetadataFileNotSupportedException()`: reached when the load
    // completed without an exception but also without a module (a
    // package-only result). The C# parameterless
    // MetadataFileNotSupportedException carries the runtime's default
    // Exception message.
    if (result.FileLoadException.has_value()) {
        throw std::runtime_error(*result.FileLoadException);
    }
    throw std::runtime_error(
        "Exception of type 'System.Exception' was thrown.");
}

const Decompiler::Metadata::MetadataFile*
LoadedAssembly::GetMetadataFileOrNull() const
{
    try {
        const auto& result = GetLoadResult();
        return result.MetadataFile.get();
    } catch (const std::exception&) {
        // The C# Trace.TraceError(ex.ToString()); the port has no trace
        // sink.
        return nullptr;
    }
}

bool LoadedAssembly::IsLoaded() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    // The C# `IsValueCreated && Value.IsCompleted` window: demanded AND
    // completed -- false while the load runs.
    return loadStarted_ && loadDone_.valid() &&
           loadDone_.wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready;
}

bool LoadedAssembly::IsLoadedAsValidAssembly() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    return loadStarted_ && loadDone_.valid() &&
           loadDone_.wait_for(std::chrono::seconds(0)) ==
               std::future_status::ready &&
           !faulted_ && loadResult_.has_value() &&
           loadResult_->MetadataFile != nullptr;
}

bool LoadedAssembly::HasLoadError() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    // The C# `IsFaulted`: false while the load runs.
    return loadStarted_ && loadDone_.valid() &&
           loadDone_.wait_for(std::chrono::seconds(0)) ==
               std::future_status::ready &&
           faulted_;
}

std::string LoadedAssembly::GetTargetFrameworkId() const
{
    // An explicit override wins over (and bypasses) detection, even once
    // detection has already cached a value.
    if (targetFrameworkIdOverride_.has_value()) {
        return *targetFrameworkIdOverride_;
    }
    return GetDetectedTargetFrameworkId();
}

std::string LoadedAssembly::GetDetectedTargetFrameworkId() const
{
    if (!targetFrameworkId_.has_value()) {
        const auto& assembly = GetMetadataFile();
        // The C# `assembly.DetectTargetFrameworkId() ?? string.Empty`.
        targetFrameworkId_ =
            Decompiler::Metadata::DetectTargetFrameworkId(assembly)
                .value_or("");
    }
    return *targetFrameworkId_;
}

std::string LoadedAssembly::GetRuntimePack() const
{
    if (!runtimePack_.has_value()) {
        const auto& assembly = GetMetadataFile();
        // The C# `assembly.DetectRuntimePack() ?? string.Empty`; the
        // port's DetectRuntimePack already returns the empty string for
        // the no-attribute case.
        runtimePack_ = Decompiler::Metadata::DetectRuntimePack(assembly);
    }
    return *runtimePack_;
}

void LoadedAssembly::SetTargetFrameworkIdOverride(
    std::optional<std::string> value)
{
    // The C# setter: trim incidental whitespace; blank values normalize
    // to no override.
    if (value.has_value()) {
        const auto first = value->find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            value.reset();
        } else {
            const auto last = value->find_last_not_of(" \t\r\n");
            *value = value->substr(first, last - first + 1);
        }
    }
    targetFrameworkIdOverride_ = std::move(value);
}

void LoadedAssembly::SetPdbFileName(std::optional<std::string> value)
{
    pdbFileName_ = std::move(value);
}

std::string LoadedAssembly::Text() const
{
    if (cachedText_.has_value()) {
        return *cachedText_;
    }
    if (IsLoaded() && !HasLoadError()) {
        const auto& result = GetLoadResult();
        if (result.MetadataFile != nullptr) {
            const auto& module = *result.MetadataFile;
            std::string versionOrInfo;
            // The C# `metadata.IsAssembly` + the assembly version.
            if (auto asmDef = module.GetAssemblyDefinition();
                asmDef.has_value()) {
                versionOrInfo = Decompiler::TypeSystem::Version(
                    asmDef->MajorVersion, asmDef->MinorVersion,
                    asmDef->BuildNumber, asmDef->RevisionNumber)
                                    .ToString();
                std::string tfId = GetTargetFrameworkId();
                if (!tfId.empty()) {
                    versionOrInfo +=
                        ", " + ReplaceAll(tfId, "Version=", " ");
                }
            } else {
                versionOrInfo = ".netmodule";
            }
            cachedText_ = ShortName() + " (" + versionOrInfo + ")";
            return *cachedText_;
        }
    }
    return ShortName();
}

std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
LoadedAssembly::LoadDebugInfo(const Decompiler::Metadata::MetadataFile* module)
    const
{
    if (module == nullptr || !options_.UseDebugSymbols) {
        return LoadDebugInfoCore(module);
    }
    // The C# wraps the core in the DebugInfoLoadStart/Stop ETW logging
    // (not ported).
    return LoadDebugInfoCore(module);
}

std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
LoadedAssembly::LoadDebugInfoCore(
    const Decompiler::Metadata::MetadataFile* module) const
{
    if (module == nullptr) {
        return nullptr;
    }
    if (options_.UseDebugSymbols) {
        try {
            // The C# `(PdbFileName != null ? FromFile(module,
            // PdbFileName) : null) ?? LoadSymbols(module)`.
            if (pdbFileName_.has_value()) {
                if (auto provider = PdbProvider::FromFile(*module,
                        *pdbFileName_)) {
                    return provider;
                }
            }
            return PdbProvider::LoadSymbols(*module);
        } catch (const std::exception&) {
            // The C# catches IOException / UnauthorizedAccessException /
            // InvalidOperationException (any error during symbol
            // loading); the port's DebugInfoUtils throws the std family.
            return nullptr;
        }
    }
    return nullptr;
}

std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
LoadedAssembly::GetDebugInfoOrNull() const
{
    if (GetMetadataFileOrNull() == nullptr) {
        return nullptr;
    }
    return debugInfoProvider_;
}

std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider>
LoadedAssembly::LoadDebugInfo(std::string fileName)
{
    SetPdbFileName(std::move(fileName));
    const auto& assembly = GetMetadataFile();
    // The C# recomputes the provider over the loaded module.
    debugInfoProvider_ = LoadDebugInfo(&assembly);
    return debugInfoProvider_;
}

std::shared_ptr<Decompiler::TypeSystem::ICompilation>
LoadedAssembly::GetTypeSystemOrNull()
    const
{
    // The C# `TypeSystemOptions.Default | Uncached | KeepModifiers`.
    return GetTypeSystemOrNull(Decompiler::TypeSystem::TypeSystemOptions::Default |
        Decompiler::TypeSystem::TypeSystemOptions::Uncached |
        Decompiler::TypeSystem::TypeSystemOptions::KeepModifiers);
}

std::shared_ptr<Decompiler::TypeSystem::ICompilation>
LoadedAssembly::GetTypeSystemOrNull(
    Decompiler::TypeSystem::TypeSystemOptions options) const
{
    std::lock_guard<std::mutex> lock(typeSystemMutex_);
    if (typeSystemWithOptions_.has_value() &&
        options == currentTypeSystemOptions_) {
        return typeSystemWithOptions_->compilation;
    }
    const auto* module = GetMetadataFileOrNull();
    // The C# `module == null || module.IsMetadataOnly` guard: the port's
    // MetadataFile is always the full-PE shape (IsMetadataOnly false).
    if (module == nullptr) {
        return nullptr;
    }
    // The module reference owns the deferred-resolution view (the C# GC
    // owns it; the cache holds it beside the compilation).
    auto moduleReference = module->WithOptions(options |
        Decompiler::TypeSystem::TypeSystemOptions::Uncached |
        Decompiler::TypeSystem::TypeSystemOptions::KeepModifiers);
    std::vector<const Decompiler::TypeSystem::IModuleReference*> references {
        &Decompiler::TypeSystem::Implementation::MinimalCorlib::Instance()};
    auto compilation =
        std::make_shared<Decompiler::TypeSystem::SimpleCompilation>(
        *moduleReference, std::move(references));
    typeSystemWithOptions_ = CachedTypeSystem{std::move(moduleReference),
        compilation};
    currentTypeSystemOptions_ = options;
    return compilation;
}

// ---------------------------------------------------------------------------
// The resolver integration: MyAssemblyResolver (the C# private nested
// class), the lazy universal resolver, and the GetAssemblyResolver /
// GetAssemblyReferenceClassifier factories.

namespace {

// The C# `Path.GetDirectoryName` (null for a bare file name; the drive
// root keeps its separator).
std::optional<std::string> GetDirectoryName(const std::string& path)
{
    const std::size_t sep = path.find_last_of("\\/");
    if (sep == std::string::npos) {
        return std::nullopt;
    }
    if (sep == 0) {
        return std::string(1, path[0]);
    }
    return path.substr(0, sep);
}

// The C# `Path.Combine(directory, moduleName)`: no separator when the
// directory already ends with one.
std::string CombinePath(const std::string& directory,
    const std::string& name)
{
    if (!directory.empty() && (directory.back() == '/' ||
                                  directory.back() == '\\')) {
        return directory + name;
    }
    return directory + "/" + name;
}

}  // namespace

// The C# `sealed class MyAssemblyResolver : IAssemblyResolver` -- the
// ResolveCoreAsync step order over the parent, a snapshot, the
// load-on-demand flag, and the winrt flag.
class LoadedAssembly::MyAssemblyResolver final :
    public Decompiler::Metadata::IAssemblyResolver {
public:
    MyAssemblyResolver(const LoadedAssembly& parent,
        AssemblyListSnapshot snapshot, bool loadOnDemand,
        bool applyWinRTProjections)
        : parent_(parent), snapshot_(std::move(snapshot)),
          loadOnDemand_(loadOnDemand),
          applyWinRTProjections_(applyWinRTProjections)
    {
    }

    // The C# `public MetadataFile? Resolve(IAssemblyReference reference)`.
    const Decompiler::Metadata::MetadataFile* Resolve(
        const Decompiler::Metadata::IAssemblyReference& reference)
        const override
    {
        // 0) if we're inside a package, look for filename.dll in the
        // parent directories (the providedAssemblyResolver arm).
        if (parent_.options_.AssemblyResolver != nullptr) {
            if (const auto* module =
                    parent_.options_.AssemblyResolver->Resolve(reference)) {
                return module;
            }
        }

        // The C# captures the TFM task at resolver construction (the
        // tfmTask); the port's synchronous load runs on first demand
        // here.
        const std::string tfm = parent_.GetTargetFrameworkId();

        // 1) try to find an exact match by tfm + full asm name in the
        // loaded assemblies.
        if (const auto* module = snapshot_.TryGetModule(reference, tfm)) {
            parent_.loadedAssemblyReferencesInfo_.AddMessageOnce(
                reference.FullName(), Decompiler::Metadata::MessageKind::Info,
                "Success - Found in Assembly List");
            return module;
        }

        // 2) try to find a match in the search paths (the universal
        // resolver: the GAC / framework / deps.json machinery).
        const auto file =
            parent_.GetUniversalResolver(applyWinRTProjections_)
                .FindAssemblyFile(reference);
        if (file.has_value()) {
            // Load the assembly from disk.
            LoadedAssembly* asm_ = nullptr;
            if (loadOnDemand_) {
                asm_ = &parent_.assemblyList_->OpenAssembly(*file, true);
            } else {
                asm_ = parent_.assemblyList_->FindAssembly(*file);
            }
            if (asm_ != nullptr) {
                parent_.loadedAssemblyReferencesInfo_.AddMessage(
                    reference.FullName(),
                    Decompiler::Metadata::MessageKind::Info,
                    "Success - Loading from: " + *file);
                return asm_->GetMetadataFileOrNull();
            }
            return nullptr;
        }

        // 9) try to find a match by asm name (no tfm/version) in the
        // loaded assemblies.
        if (const auto* module = snapshot_.TryGetSimilarModule(reference)) {
            parent_.loadedAssemblyReferencesInfo_.AddMessageOnce(
                reference.FullName(), Decompiler::Metadata::MessageKind::Info,
                "Success - Found in Assembly List with different TFM or "
                "version: " +
                    module->FileName());
            return module;
        }
        parent_.loadedAssemblyReferencesInfo_.AddMessageOnce(
            reference.FullName(), Decompiler::Metadata::MessageKind::Error,
            "Could not find reference: " + reference.FullName());
        return nullptr;
    }

    // The C# `public MetadataFile? ResolveModule(MetadataFile mainModule,
    // string moduleName)`.
    const Decompiler::Metadata::MetadataFile* ResolveModule(
        const Decompiler::Metadata::MetadataFile& mainModule,
        const std::string& moduleName) const override
    {
        if (parent_.options_.AssemblyResolver != nullptr) {
            if (const auto* module =
                    parent_.options_.AssemblyResolver->ResolveModule(
                        mainModule, moduleName)) {
                return module;
            }
        }

        // Load the module from the main module's directory.
        if (const auto directory = GetDirectoryName(mainModule.FileName());
            directory.has_value()) {
            const std::string file = CombinePath(*directory, moduleName);
            std::error_code ec;
            if (std::filesystem::exists(file, ec)) {
                LoadedAssembly* asm_ = nullptr;
                if (loadOnDemand_) {
                    asm_ = &parent_.assemblyList_->OpenAssembly(file, true);
                } else {
                    asm_ = parent_.assemblyList_->FindAssembly(file);
                }
                if (asm_ != nullptr) {
                    return asm_->GetMetadataFileOrNull();
                }
            }
        }

        // The module does not exist on disk; look for one with a matching
        // name in the assembly list (a non-assembly module only).
        for (LoadedAssembly* loaded : snapshot_.Assemblies()) {
            const auto* module = loaded->GetMetadataFileOrNull();
            if (module == nullptr ||
                module->GetAssemblyDefinition().has_value()) {
                continue;
            }
            const auto moduleDef = module->GetModuleDefinition();
            if (!moduleDef.has_value()) {
                continue;
            }
            if (OrdinalIgnoreCaseEquals(moduleName, moduleDef->Name)) {
                parent_.loadedAssemblyReferencesInfo_.AddMessageOnce(
                    moduleName, Decompiler::Metadata::MessageKind::Info,
                    "Success - Found in Assembly List");
                return module;
            }
        }

        return nullptr;
    }

private:
    // The C# string.Equals(..., StringComparison.OrdinalIgnoreCase).
    static bool OrdinalIgnoreCaseEquals(const std::string& a,
        const std::string& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto lower = [](char c) {
                return c >= 'A' && c <= 'Z'
                    ? static_cast<char>(c - 'A' + 'a')
                    : c;
            };
            if (lower(a[i]) != lower(b[i])) {
                return false;
            }
        }
        return true;
    }

    const LoadedAssembly& parent_;
    AssemblyListSnapshot snapshot_;
    bool loadOnDemand_;
    bool applyWinRTProjections_;
};

std::unique_ptr<Decompiler::Metadata::IAssemblyResolver>
LoadedAssembly::GetAssemblyResolver(bool loadOnDemand,
    bool applyWinRTProjections) const
{
    return std::make_unique<MyAssemblyResolver>(*this,
        assemblyList_->GetSnapshot(), loadOnDemand, applyWinRTProjections);
}

std::unique_ptr<Decompiler::Metadata::IAssemblyResolver>
LoadedAssembly::GetAssemblyResolver(const AssemblyListSnapshot& snapshot,
    bool loadOnDemand, bool applyWinRTProjections) const
{
    return std::make_unique<MyAssemblyResolver>(*this, snapshot, loadOnDemand,
        applyWinRTProjections);
}

const Decompiler::Metadata::UniversalAssemblyResolver&
LoadedAssembly::GetUniversalResolver(bool applyWinRTProjections) const
{
    // The C# LazyInitializer.EnsureInitialized: one resolver per
    // LoadedAssembly, created with the options of the FIRST call (later
    // calls with a different winrt flag return the cached one).
    std::lock_guard<std::mutex> lock(universalResolverMutex_);
    if (universalResolver_ == nullptr) {
        const std::string targetFramework = GetTargetFrameworkId();
        const std::string runtimePack = GetRuntimePack();
        // The C# `Path.IsPathRooted(this.FileName) ? this.FileName : null`
        // (the POSIX arm: absolute paths only).
        std::optional<std::string> rootedPath;
        {
            std::error_code ec;
            if (std::filesystem::path(fileName_).is_absolute()) {
                rootedPath = fileName_;
            }
        }
        universalResolver_ =
            std::make_unique<Decompiler::Metadata::UniversalAssemblyResolver>(
                rootedPath, false, targetFramework, runtimePack,
                Decompiler::Metadata::PEStreamOptions::PrefetchEntireImage,
                applyWinRTProjections
                    ? Decompiler::Metadata::MetadataReaderOptions::
                          ApplyWindowsRuntimeProjections
                    : Decompiler::Metadata::MetadataReaderOptions::None);
    }
    return *universalResolver_;
}

const Decompiler::Metadata::IAssemblyReferenceClassifier&
LoadedAssembly::GetAssemblyReferenceClassifier(
    bool applyWinRTProjections) const
{
    return GetUniversalResolver(applyWinRTProjections);
}

}  // namespace ILSpy::ILSpyX
