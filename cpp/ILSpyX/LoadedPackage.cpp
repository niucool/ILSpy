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

// Implementation of ILSpyX/LoadedPackage.hpp (the porting decisions are
// documented on the header; the entry classes mirror the C# nested
// FolderEntry / ZipFileEntry / BundleEntry).

#include "ILSpyX/LoadedPackage.hpp"

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/LoadedAssemblyExtensions.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

#include <miniz/miniz.h>
#include <miniz/miniz_zip.h>

namespace ILSpy::ILSpyX {

namespace Sfb = ILSpy::Decompiler::SingleFileBundle;

namespace {

// The C# string.Equals(..., StringComparison.OrdinalIgnoreCase).
bool OrdinalIgnoreCaseEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

// The C# `SplitName`: the (directory, file-name) split on the LAST
// separator of either kind.
std::pair<std::string, std::string> SplitName(const std::string& filename)
{
    std::size_t pos = filename.find_last_of("/\\");
    if (pos == std::string::npos)
        return { "", filename };  // file in root
    return { filename.substr(0, pos), filename.substr(pos + 1) };
}

// Reads the whole file (the port's image convention; the C# maps the file
// and the entries share the view).
std::vector<std::uint8_t> ReadFileBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Could not find file '" + path + "'.");
    return std::vector<std::uint8_t>(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The miniz RAII wrapper over mz_zip_reader_init_file /
// mz_zip_reader_end (the C# `using var archive`).
class ZipReader {
public:
    explicit ZipReader(const std::string& file)
    {
        if (!mz_zip_reader_init_file(&zip_, file.c_str(), 0))
            throw std::out_of_range(
                "End of Central Directory record could not be found.");
    }
    ~ZipReader() { mz_zip_reader_end(&zip_); }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    mz_zip_archive& get() { return zip_; }

private:
    mz_zip_archive zip_ = {};
};

// The C# `sealed class FolderEntry : PackageEntry`: an entry inside a
// package folder, effectively renamed.
class FolderEntry final : public PackageEntry {
public:
    FolderEntry(std::string name, std::shared_ptr<PackageEntry> originalEntry)
        : name_(std::move(name)), originalEntry_(std::move(originalEntry))
    {
    }

    std::string Name() const override { return name_; }
    std::string FullName() const override { return originalEntry_->Name(); }
    std::string PackageQualifiedFileName() const override
    {
        return originalEntry_->PackageQualifiedFileName();
    }
    // The return type is qualified: the inherited member name shadows the
    // namespace alias during return-type lookup (the C# property/type
    // name collision the C++ port spells around).
    ILSpy::ILSpyX::ResourceType ResourceType() const override
    {
        return originalEntry_->ResourceType();
    }
    std::uint32_t Attributes() const override
    {
        return originalEntry_->Attributes();
    }
    std::optional<std::vector<std::uint8_t>> TryOpenStream() const override
    {
        return originalEntry_->TryOpenStream();
    }
    std::optional<long long> TryGetLength() const override
    {
        return originalEntry_->TryGetLength();
    }

private:
    std::string name_;
    std::shared_ptr<PackageEntry> originalEntry_;
};

// The C# `sealed class ZipFileEntry : PackageEntry`: one member of a zip
// archive, re-opened from the zip file on demand.
class ZipFileEntry final : public PackageEntry {
public:
    ZipFileEntry(std::string zipFile, std::string name)
        : zipFile_(std::move(zipFile)), name_(std::move(name))
    {
    }

    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string PackageQualifiedFileName() const override
    {
        return "zip://" + zipFile_ + ";" + name_;
    }

    std::optional<std::vector<std::uint8_t>> TryOpenStream() const override
    {
        ZipReader reader(zipFile_);
        int index = mz_zip_reader_locate_file(&reader.get(), name_.c_str(),
            nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        if (index < 0)
            return std::nullopt;
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&reader.get(),
            static_cast<mz_uint>(index), &size, 0);
        if (data == nullptr) {
            // The C# entry.Open() throws InvalidDataException for an
            // undecodable member; a failed heap extract is the same
            // corrupt-archive class.
            throw std::out_of_range(
                "The archive entry was compressed using an unsupported "
                "compression method.");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::uint8_t*>(data),
            static_cast<std::uint8_t*>(data) + size);
        mz_free(data);
        return bytes;
    }

    std::optional<long long> TryGetLength() const override
    {
        ZipReader reader(zipFile_);
        int index = mz_zip_reader_locate_file(&reader.get(), name_.c_str(),
            nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        if (index < 0)
            return std::nullopt;
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader.get(),
                static_cast<mz_uint>(index), &stat))
            return std::nullopt;
        return static_cast<long long>(stat.m_uncomp_size);
    }

private:
    std::string zipFile_;
    std::string name_;
};

// Raw-deflate inflate with the C# DeflateStream semantics the compressed
// bundle entries need (the same implementation IlspyCmdProgram's
// DumpPackage carries, mirrored file-locally here -- see its comment for
// the partial-production and corrupt-stream behaviors).
std::vector<std::uint8_t> InflateRawDeflate(const std::uint8_t* src,
    std::size_t srcSize, long long declaredSize)
{
    std::int32_t preSize = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(declaredSize)));
    if (preSize < 0)
        throw std::out_of_range("Negative MemoryStream capacity.");
    std::size_t capacity = preSize > 0 ? static_cast<std::size_t>(preSize) : 1;
    for (;;) {
        std::vector<std::uint8_t> out(capacity);
        size_t inSize = srcSize;
        size_t outSize = capacity;
        tinfl_decompressor decomp;
        tinfl_init(&decomp);
        tinfl_status status = tinfl_decompress(
            &decomp, src, &inSize, out.data(), out.data(), &outSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status == TINFL_STATUS_DONE) {
            out.resize(outSize);
            return out;
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            capacity *= 2;
            continue;
        }
        if (status == TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS) {
            out.resize(outSize);
            return out;
        }
        throw std::out_of_range(
            "The archive entry was compressed using an unsupported "
            "compression method.");
    }
}

// The C# `sealed class BundleEntry : PackageEntry`: one member of a
// single-file bundle, reading through the shared image.
class BundleEntry final : public PackageEntry {
public:
    BundleEntry(std::string bundleFile,
        std::shared_ptr<const std::vector<std::uint8_t>> image,
        Decompiler::SingleFileBundle::Entry entry)
        : bundleFile_(std::move(bundleFile)), image_(std::move(image)),
          entry_(std::move(entry))
    {
    }

    std::string Name() const override { return entry_.RelativePath; }
    std::string FullName() const override { return entry_.RelativePath; }
    std::string PackageQualifiedFileName() const override
    {
        return "bundle://" + bundleFile_ + ";" + entry_.RelativePath;
    }

    std::optional<std::vector<std::uint8_t>> TryOpenStream() const override
    {
        const long long imageSize = static_cast<long long>(image_->size());
        if (entry_.CompressedSize == 0) {
            // The C# UnmanagedMemoryStream does not validate the extent;
            // the port refuses an out-of-image entry instead of reading
            // undefined bytes (the DumpPackage determinism divergence).
            if (entry_.Offset < 0 || entry_.Size < 0
                || entry_.Offset + entry_.Size > imageSize)
                throw std::out_of_range("Bundle entry '" + entry_.RelativePath
                    + "' lies outside the package image.");
            return std::vector<std::uint8_t>(
                image_->begin() + entry_.Offset,
                image_->begin() + entry_.Offset + entry_.Size);
        }
        if (entry_.Offset < 0 || entry_.CompressedSize < 0
            || entry_.Offset + entry_.CompressedSize > imageSize)
            throw std::out_of_range("Bundle entry '" + entry_.RelativePath
                + "' lies outside the package image.");
        std::vector<std::uint8_t> decompressed = InflateRawDeflate(
            image_->data() + entry_.Offset,
            static_cast<std::size_t>(entry_.CompressedSize), entry_.Size);
        if (static_cast<long long>(decompressed.size()) != entry_.Size) {
            throw std::out_of_range(
                "Corrupted single-file entry '" + entry_.RelativePath
                + "'. Declared decompressed size '"
                + std::to_string(entry_.Size)
                + "' is not the same as actual decompressed size '"
                + std::to_string(decompressed.size()) + "'.");
        }
        return decompressed;
    }

    std::optional<long long> TryGetLength() const override
    {
        return entry_.Size;
    }

private:
    std::string bundleFile_;
    std::shared_ptr<const std::vector<std::uint8_t>> image_;
    Decompiler::SingleFileBundle::Entry entry_;
};

}  // namespace

// The C# `public LoadedPackage(PackageKind kind, IEnumerable<PackageEntry>
// entries)`: the folder tree over the entry names.
LoadedPackage::LoadedPackage(PackageKind kind,
    std::vector<std::shared_ptr<PackageEntry>> entries)
    : kind_(kind), entries_(std::move(entries))
{
    auto rootFolder = std::make_shared<PackageFolder>(*this, nullptr, "");
    std::map<std::string, PackageFolder*> folders;
    folders.emplace("", rootFolder.get());

    // The C# local function GetFolder(name): recursively materialize the
    // parent chain, then register the folder.
    auto getFolder = [this, &folders](const std::string& name,
                             const auto& self) -> PackageFolder* {
        auto it = folders.find(name);
        if (it != folders.end())
            return it->second;
        auto [dirname, basename] = SplitName(name);
        PackageFolder* parent = self(dirname, self);
        auto folder = std::make_shared<PackageFolder>(*this, parent, basename);
        PackageFolder* result = folder.get();
        parent->folders_.push_back(std::move(folder));
        folders.emplace(name, result);
        return result;
    };

    for (const std::shared_ptr<PackageEntry>& entry : entries_) {
        if (!entry)
            continue;
        auto [dirname, filename] = SplitName(entry->Name());
        if (!filename.empty()) {
            getFolder(dirname, getFolder)->entries_.push_back(
                std::make_shared<FolderEntry>(filename, entry));
        }
    }
    rootFolder_ = std::move(rootFolder);
}

std::shared_ptr<LoadedPackage> LoadedPackage::FromZipFile(const std::string& file)
{
    ZipReader reader(file);
    mz_uint count = mz_zip_reader_get_num_files(&reader.get());
    std::vector<std::shared_ptr<PackageEntry>> entries;
    entries.reserve(count);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader.get(), i, &stat))
            continue;
        entries.push_back(std::make_shared<ZipFileEntry>(file, stat.m_filename));
    }
    return std::make_shared<LoadedPackage>(PackageKind::Zip, std::move(entries));
}

std::shared_ptr<LoadedPackage> LoadedPackage::FromBundle(
    const std::string& fileName)
{
    std::vector<std::uint8_t> image;
    try {
        image = ReadFileBytes(fileName);
    } catch (const std::exception&) {
        // The C# MemoryMappedFile.CreateFromFile throws for a missing
        // file; the caller (BundleFileLoader) only feeds existing files,
        // so the observable path is the invalid-data null below.
        return nullptr;
    }
    long long bundleHeaderOffset = 0;
    if (!Sfb::IsBundle(image.data(), static_cast<long long>(image.size()),
            bundleHeaderOffset))
        return nullptr;
    try {
        Sfb::Header manifest = Sfb::ReadManifest(image.data(),
            static_cast<long long>(image.size()), bundleHeaderOffset);
        auto sharedImage =
            std::make_shared<const std::vector<std::uint8_t>>(std::move(image));
        std::vector<std::shared_ptr<PackageEntry>> entries;
        entries.reserve(manifest.Entries.size());
        for (Sfb::Entry& e : manifest.Entries) {
            entries.push_back(std::make_shared<BundleEntry>(
                fileName, sharedImage, std::move(e)));
        }
        auto result = std::make_shared<LoadedPackage>(
            PackageKind::Bundle, std::move(entries));
        result->bundleHeader_ = std::move(manifest);
        return result;
    } catch (const std::out_of_range&) {
        // The C# catch (InvalidDataException) -> null.
        return nullptr;
    } catch (const std::runtime_error&) {
        // ReadManifest's InvalidDataException messages are
        // std::runtime_error (see SingleFileBundle.hpp); the same catch.
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// The PackageFolder IAssemblyResolver half (the C# PackageFolder's
// Resolve / ResolveModule / ResolveFileName).

PackageFolder::PackageFolder(LoadedPackage& package, PackageFolder* parent,
    std::string name)
    : name_(std::move(name)), parent_(parent), package_(&package)
{
}

// The out-of-line dtor: the entry cache owns LoadedAssembly instances
// (complete only here).
PackageFolder::~PackageFolder() = default;

const Decompiler::Metadata::MetadataFile* PackageFolder::Resolve(
    const Decompiler::Metadata::IAssemblyReference& reference) const
{
    if (LoadedAssembly* asm_ = ResolveFileName(reference.Name() + ".dll")) {
        return asm_->GetMetadataFileOrNull();
    }
    return parent_ != nullptr ? parent_->Resolve(reference) : nullptr;
}

const Decompiler::Metadata::MetadataFile* PackageFolder::ResolveModule(
    const Decompiler::Metadata::MetadataFile& mainModule,
    const std::string& moduleName) const
{
    (void)mainModule;
    if (LoadedAssembly* asm_ = ResolveFileName(moduleName + ".dll")) {
        return asm_->GetMetadataFileOrNull();
    }
    return parent_ != nullptr ? parent_->ResolveModule(mainModule, moduleName)
                              : nullptr;
}

LoadedAssembly* PackageFolder::ResolveFileName(const std::string& name) const
{
    if (package_->GetLoadedAssembly() == nullptr) {
        return nullptr;
    }
    const LoadedAssembly& wrapper = *package_->GetLoadedAssembly();
    std::lock_guard<std::mutex> lock(assembliesMutex_);
    // The C# cache stores the misses too (the null values are cached).
    const auto cached = resolvedAssemblies_.find(name);
    if (cached != resolvedAssemblies_.end()) {
        return cached->second.get();
    }
    LoadedAssembly* result = nullptr;
    for (const auto& entry : entries_) {
        // The C# Entries.FirstOrDefault(OrdinalIgnoreCase name match).
        if (OrdinalIgnoreCaseEquals(name, entry->Name())) {
            // The C# constructs the wrapper with the entry's stream (the
            // deferred TryOpenStream task), the list's loader registry,
            // this folder as the resolver, and the list flags.
            LoadedAssembly::Options options;
            options.FileLoaders = wrapper.GetAssemblyList().LoaderRegistry();
            options.AssemblyResolver = this;
            options.Stream = [entry] { return entry->TryOpenStream(); };
            options.ApplyWinRTProjections =
                wrapper.GetAssemblyList().ApplyWinRTProjections();
            options.UseDebugSymbols =
                wrapper.GetAssemblyList().UseDebugSymbols();
            // The C# constructs through the LoadedAssembly bundle ctor
            // (`new LoadedAssembly(package.LoadedAssembly, entry.Name,
            // ...)`); the instance is NOT added to the list (the C#
            // constructs it directly, not through OpenAssembly). The
            // cache owns it -- the C# GC does.
            auto owned = std::make_shared<LoadedAssembly>(
                const_cast<LoadedAssembly&>(wrapper), entry->Name(),
                std::move(options));
            result = owned.get();
            resolvedAssemblies_.emplace(name, std::move(owned));
            break;
        }
    }
    if (result == nullptr) {
        resolvedAssemblies_.emplace(name, nullptr);
    }
    return result;
}

}  // namespace ILSpy::ILSpyX
