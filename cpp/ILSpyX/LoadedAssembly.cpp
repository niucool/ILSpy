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
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"

#include <algorithm>
#include <cstdio>
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

void LoadedAssembly::EnsureLoaded() const
{
    std::vector<std::function<void()>> listeners;
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        if (loadTriggered_) {
            return;
        }
        // Set the flag BEFORE the work (the C# Lazy.IsValueCreated
        // contract: a status poll must see "started" while the load
        // runs).
        loadTriggered_ = true;
        try {
            loadResult_ = LoadCore();
        } catch (const std::exception& ex) {
            faulted_ = true;
            faultMessage_ = ex.what();
        } catch (...) {
            faulted_ = true;
            faultMessage_ = "Exception of type 'System.Exception' was thrown.";
        }
        // The C# `task.ContinueWith(... RaiseLoaded())`: fires on success
        // and failure alike, exactly once.
        loadedFired_ = true;
        listeners = std::move(loadedListeners_);
        loadedListeners_.clear();
    }
    // Fired with the load mutex released (a listener may re-enter the
    // status surface).
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
    return loadTriggered_;
}

bool LoadedAssembly::IsLoadedAsValidAssembly() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    return loadTriggered_ && !faulted_ &&
           loadResult_.has_value() && loadResult_->MetadataFile != nullptr;
}

bool LoadedAssembly::HasLoadError() const
{
    std::lock_guard<std::mutex> lock(loadMutex_);
    return loadTriggered_ && faulted_;
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

}  // namespace ILSpy::ILSpyX
