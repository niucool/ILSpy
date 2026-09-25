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

// Port of ICSharpCode.ILSpyX/FileLoaders/LoadResult.cs: the load contract
// shared by every file loader -- the result (a module, a package, or a
// captured load failure), the per-load context, and the loader interface.
//
// C#-to-C++ porting decisions:
//  * The C# `MetadataFile?` / `LoadedPackage?` result slots port as
//    unique_ptr / shared_ptr (null = absent); the port's MetadataFile is
//    move-only, so the result is move-only.
//  * The C# `Exception? FileLoadException` ports as the exception MESSAGE
//    string (the port carries no exception object graph; IsSuccess reads
//    the slot exactly as the C# does).
//  * The C# `Task<LoadResult?>` ports as the synchronous
//    std::optional<LoadResult>: a disengaged optional is the C# null (the
//    loader does not claim the file).
//  * The C# `Stream` parameter ports as the (data, size) byte view (the
//    repo's blob convention); the loaders that re-open by file NAME (the
//    bundle and archive loaders) read the name, exactly as the C# does
//    with its streams.
//  * The C# `FileLoadContext.ParentBundle` carries the LoadedAssembly
//    wrapping the bundle; the port carries the same wrapper (raw pointer
//    -- the loaders only read its nullness, and the wrapper is owned by
//    the caller's AssemblyList).
//  * DEFERRED: MetadataFileLoader (MetadataReaderProvider
//    .FromMetadataStream -- the metadata-only MetadataFile shape the
//    port's PE-only reader does not construct; see MetadataFile::Name()'s
//    note). It is not registered in FileLoaderRegistry and is recorded in
//    PORT_LOG_BAML.md. The WebCilFileLoader deferral has since been
//    filled (the container reader in Metadata/WebCilFile.hpp; the loader
//    presents the container as the PE-shaped MetadataFile over the
//    adapted image).

#pragma once

#include "ILSpyX/LoadedPackage.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ILSpy::ILSpyX {

// The C# `LoadedAssembly` (LoadedAssembly.hpp) -- forward-declared: the
// load context carries the wrapper the bundle entry loads belong to.
class LoadedAssembly;

}  // namespace ILSpy::ILSpyX

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class LoadResult`.
struct LoadResult {
    std::unique_ptr<Decompiler::Metadata::MetadataFile> MetadataFile;
    // The C# `Exception? FileLoadException` -- the port carries the
    // message; a disengaged optional is the C# null.
    std::optional<std::string> FileLoadException;
    std::shared_ptr<LoadedPackage> Package;

    // The C# `public bool IsSuccess => FileLoadException == null`.
    bool IsSuccess() const { return !FileLoadException.has_value(); }
};

// The C# `public record FileLoadContext(bool ApplyWinRTProjections,
// LoadedAssembly? ParentBundle)`.
struct FileLoadContext {
    bool ApplyWinRTProjections = false;
    // The bundle wrapper (null outside a bundle); the loaders read the
    // nullness only. Raw pointer: the wrapper is owned by the caller's
    // AssemblyList, which outlives every load it drives.
    const LoadedAssembly* ParentBundle = nullptr;
};

// The C# `public interface IFileLoader`.
class IFileLoader {
public:
    virtual ~IFileLoader() = default;

    // The C# `Task<LoadResult?> Load(string fileName, Stream stream,
    // FileLoadContext context)`: nullopt when the loader does not claim
    // the file.
    virtual std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const = 0;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
