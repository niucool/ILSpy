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

// Port of ICSharpCode.ILSpyX/LoadedPackage.cs: NuGet package or .NET
// single-file bundle -- the package model the archive and bundle file
// loaders produce (the entries, the folder tree, and the on-demand entry
// streams).
//
// C#-to-C++ porting decisions:
//  * The C# `Resource` / `ByteArrayResource` base classes live in
//    ICSharpCode.Decompiler/Metadata/Resource.cs; their public surface
//    is declared here (below PackageEntry's dependency) because the
//    package model is the only Phase 8 consumer -- when the Decompiler-side
//    file is ported for its other consumers, these declarations move there
//    (the shapes are identical by construction).
//  * The C# `Stream? TryOpenStream()` ports as
//    std::optional<std::vector<std::uint8_t>> (the blob convention; a
//    disengaged optional is the C# null stream). `long? TryGetLength()`
//    ports as std::optional<long long>.
//  * The C# `MemoryMappedFile` view the bundle entries keep alive ports
//    as a shared_ptr to the whole image (the port's read-the-file
//    convention; each BundleEntry shares it).
//  * The C# `ZipFile.OpenRead` / `ZipArchiveEntry` port through miniz
//    (the repo's zip dependency): FromZipFile opens the archive once to
//    enumerate the entries, and every ZipFileEntry re-opens the zip by
//    file name on demand exactly as the C# does. A non-zip file throws
//    the .NET InvalidDataException message ("End of Central Directory
//    record could not be found.") as std::out_of_range (the port's
//    InvalidDataException convention, see IlspyCmdProgram's
//    DumpPackage).
//  * The C# InvalidDataException arms of the bundle decode (the
//    corrupted-entry size mismatch) port as std::out_of_range with the
//    exact .NET message (the same convention).
//  * NOT PORTED (documented deferrals, all of the LoadedAssembly
//    pipeline): the `internal LoadedAssembly LoadedAssembly` property,
//    PackageFolder's IAssemblyResolver surface (Resolve / ResolveModule /
//    ResolveFileName / the async pair) and the assemblies cache -- they
//    build LoadedAssembly instances, which belongs to the GUI tree model
//    (out of the Phase 8 core subset; PORT_PLAN.md's ILSpyX sub-scoping
//    drops it). The folder MODEL (Name, Parent, Folders, Entries) is the
//    part the loaders and the package tree need.
//  * ILSpyXEventSource.Log calls do not port (ETW instrumentation; see
//    the Instrumentation deferral in PORT_LOG_BAML.md).

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/SingleFileBundle.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX {

// The C# `public enum ResourceType` (Resource.cs): the port reuses the
// MetadataFile kind enum, which carries the same members (Linked,
// Embedded, AssemblyLinked).
using ResourceType = Decompiler::Metadata::MetadataFile::ManifestResourceKind;

// The C# `public abstract class Resource` (Resource.cs): the base of every
// package entry. The default members are Embedded / Public (0x01, the
// System.Reflection.ManifestResourceAttributes value).
class Resource {
public:
    virtual ~Resource() = default;

    virtual ResourceType ResourceType() const
    {
        return ILSpy::ILSpyX::ResourceType::Embedded;
    }
    // The raw ManifestResourceAttributes flags (Public = 0x01,
    // Private = 0x02).
    virtual std::uint32_t Attributes() const { return 0x01; }
    virtual std::string Name() const = 0;
    virtual std::optional<std::vector<std::uint8_t>> TryOpenStream() const = 0;
    virtual std::optional<long long> TryGetLength() const = 0;
};

// The C# `public class ByteArrayResource : Resource` (Resource.cs): an
// in-memory resource.
class ByteArrayResource : public Resource {
public:
    ByteArrayResource(std::string name, std::vector<std::uint8_t> data)
        : name_(std::move(name)), data_(std::move(data))
    {
    }

    std::string Name() const override { return name_; }
    std::optional<std::vector<std::uint8_t>> TryOpenStream() const override
    {
        return data_;
    }
    std::optional<long long> TryGetLength() const override
    {
        return static_cast<long long>(data_.size());
    }

private:
    std::string name_;
    std::vector<std::uint8_t> data_;
};

// The C# `public abstract class PackageEntry : Resource`: one entry in a
// package.
class PackageEntry : public Resource {
public:
    // The full file name including the package's own name, prefixed with
    // the scheme (e.g. "zip://..." or "bundle://...").
    virtual std::string PackageQualifiedFileName() const = 0;
    // The entry name relative to the package root (as stored).
    virtual std::string FullName() const = 0;
};

// The C# `public sealed class PackageFolder` (the model half; see the
// header note for the deferred IAssemblyResolver half).
class PackageFolder {
public:
    PackageFolder(std::string name)
        : name_(std::move(name))
    {
    }

    const std::string& Name() const { return name_; }
    const PackageFolder* Parent() const { return parent_; }
    const std::vector<std::shared_ptr<PackageFolder>>& Folders() const
    {
        return folders_;
    }
    const std::vector<std::shared_ptr<PackageEntry>>& Entries() const
    {
        return entries_;
    }

private:
    friend class LoadedPackage;

    std::string name_;
    PackageFolder* parent_ = nullptr;
    std::vector<std::shared_ptr<PackageFolder>> folders_;
    std::vector<std::shared_ptr<PackageEntry>> entries_;
};

// The C# `public class LoadedPackage`.
class LoadedPackage {
public:
    // The C# `public enum PackageKind`.
    enum class PackageKind {
        Zip,
        Bundle,
    };

    // The C# `public LoadedPackage(PackageKind kind,
    // IEnumerable<PackageEntry> entries)`: builds the folder tree from
    // the entry names ('/' and '\\' both separate; an entry with an empty
    // final component -- a directory row -- is skipped).
    LoadedPackage(PackageKind kind,
        std::vector<std::shared_ptr<PackageEntry>> entries);

    PackageKind Kind() const { return kind_; }
    // All entries, including those in sub-directories.
    const std::vector<std::shared_ptr<PackageEntry>>& Entries() const
    {
        return entries_;
    }
    const PackageFolder& RootFolder() const { return *rootFolder_; }
    // The bundle manifest (a value only for PackageKind::Bundle).
    const std::optional<Decompiler::SingleFileBundle::Header>& BundleHeader()
        const
    {
        return bundleHeader_;
    }

    // The C# `public static LoadedPackage FromZipFile(string file)`:
    // opens the zip and takes its entry list. Throws the .NET
    // InvalidDataException (as std::out_of_range, "End of Central
    // Directory record could not be found.") for a file that is not a
    // zip.
    static std::shared_ptr<LoadedPackage> FromZipFile(const std::string& file);

    // The C# `public static LoadedPackage? FromBundle(string fileName)`:
    // null (nullptr) when the file does not carry the bundle signature;
    // the bundle's InvalidDataException (ReadManifest) also yields
    // nullptr.
    static std::shared_ptr<LoadedPackage> FromBundle(
        const std::string& fileName);

private:
    PackageKind kind_;
    std::vector<std::shared_ptr<PackageEntry>> entries_;
    std::shared_ptr<PackageFolder> rootFolder_;
    std::optional<Decompiler::SingleFileBundle::Header> bundleHeader_;
};

}  // namespace ILSpy::ILSpyX
