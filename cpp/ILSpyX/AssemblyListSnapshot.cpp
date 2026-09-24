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

#include "ILSpyX/AssemblyListSnapshot.hpp"

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/LoadedAssembly.hpp"
#include "ILSpyX/LoadedPackage.hpp"

#include <cstring>

#include <algorithm>
#include <memory>

namespace ILSpy::ILSpyX {

namespace {

// The C# `entry.Name.EndsWith(".dll", OrdinalIgnoreCase) ||
// entry.Name.EndsWith(".exe", OrdinalIgnoreCase)` (the
// GetAllAssembliesAsync entry filter).
bool EndsWithExecutableExtension(const std::string& name)
{
    const auto endsWith = [&name](const char* suffix) {
        const std::size_t n = std::strlen(suffix);
        if (name.size() < n) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const char c = name[name.size() - n + i];
            const char s = suffix[i];
            const auto lower = [](char ch) {
                return ch >= 'A' && ch <= 'Z'
                    ? static_cast<char>(ch - 'A' + 'a')
                    : ch;
            };
            if (lower(c) != lower(s)) {
                return false;
            }
        }
        return true;
    };
    return endsWith(".dll") || endsWith(".exe");
}

// The C# tfm normalization shared by TryGetModuleAsync and the lookup
// builder: the v4.x Framework identifiers collapse to "v4" so the
// 4.5/4.7.2/4.8 lists cross-match.
std::string NormalizeTargetFrameworkId(std::string tfm)
{
    if (tfm.rfind(".NETFramework,Version=v4.", 0) == 0) {
        return ".NETFramework,Version=v4";
    }
    return tfm;
}

}  // namespace

// The lazily-built lookups, behind one mutex (the C# LazyInit fields the
// resolver threads race to fill).
struct AssemblyListSnapshot::LookupCache {
    std::mutex mutex;
    std::unique_ptr<
        std::map<std::string, const Decompiler::Metadata::MetadataFile*,
            OrdinalIgnoreCaseLess>>
        byFullNameLookup;
    std::unique_ptr<
        std::map<std::string, const Decompiler::Metadata::MetadataFile*,
            OrdinalIgnoreCaseLess>>
        byShortNameLookup;
    std::unique_ptr<
        std::map<std::string, std::vector<AssemblyListSnapshot::ShortNameEntry>,
            OrdinalIgnoreCaseLess>>
        byShortNameGrouped;
};

AssemblyListSnapshot::AssemblyListSnapshot(
    std::vector<LoadedAssembly*> assemblies)
    : assemblies_(std::move(assemblies))
{
}

AssemblyListSnapshot::~AssemblyListSnapshot() = default;

const std::map<std::string, const Decompiler::Metadata::MetadataFile*,
    OrdinalIgnoreCaseLess>&
AssemblyListSnapshot::Lookup(bool shortNames) const
{
    if (cache_ == nullptr) {
        cache_ = std::make_unique<LookupCache>();
    }
    std::lock_guard<std::mutex> lock(cache_->mutex);
    auto& cache = shortNames ? cache_->byShortNameLookup : cache_->byFullNameLookup;
    if (!cache) {
        auto map = std::make_unique<
            std::map<std::string, const Decompiler::Metadata::MetadataFile*,
                OrdinalIgnoreCaseLess>>();
        for (LoadedAssembly* loaded : assemblies_) {
            const auto* module = loaded->GetMetadataFileOrNull();
            if (module == nullptr) {
                continue;
            }
            // The C# `reader == null || !reader.IsAssembly` skip.
            const auto asmDef = module->GetAssemblyDefinition();
            if (!asmDef.has_value()) {
                continue;
            }
            // The per-assembly catch (BadImageFormatException) continue
            // arm: the port's readers surface corrupt images as std
            // exceptions.
            try {
                const std::string tfm = NormalizeTargetFrameworkId(
                    loaded->GetTargetFrameworkId());
                const std::string key = tfm + ";" +
                    (shortNames ? module->Name() : module->FullName());
                // The C# `if (!result.ContainsKey(key)) result.Add(...)`
                // first-wins rule.
                map->emplace(std::move(key), module);
            } catch (const std::exception&) {
                continue;
            }
        }
        cache = std::move(map);
    }
    return *cache;
}

const std::map<std::string, std::vector<AssemblyListSnapshot::ShortNameEntry>,
    OrdinalIgnoreCaseLess>&
AssemblyListSnapshot::ShortNameGroupLookup() const
{
    if (cache_ == nullptr) {
        cache_ = std::make_unique<LookupCache>();
    }
    std::lock_guard<std::mutex> lock(cache_->mutex);
    if (!cache_->byShortNameGrouped) {
        auto map = std::make_unique<
            std::map<std::string, std::vector<ShortNameEntry>,
                OrdinalIgnoreCaseLess>>();
        for (LoadedAssembly* loaded : assemblies_) {
            const auto* module = loaded->GetMetadataFileOrNull();
            if (module == nullptr) {
                continue;
            }
            const auto asmDef = module->GetAssemblyDefinition();
            if (!asmDef.has_value()) {
                continue;
            }
            try {
                ShortNameEntry line;
                line.Module = module;
                line.Version = Decompiler::TypeSystem::Version(
                    asmDef->MajorVersion, asmDef->MinorVersion,
                    asmDef->BuildNumber, asmDef->RevisionNumber);
                auto& existing = (*map)[asmDef->Name];
                // The C# BinarySearch + insert-after rule: equal versions
                // keep insertion order within the group.
                auto it = std::upper_bound(existing.begin(), existing.end(),
                    line.Version,
                    [](const Decompiler::TypeSystem::Version& v,
                        const ShortNameEntry& e) { return v < e.Version; });
                existing.insert(it, line);
            } catch (const std::exception&) {
                continue;
            }
        }
        cache_->byShortNameGrouped = std::move(map);
    }
    return *cache_->byShortNameGrouped;
}

std::vector<LoadedAssembly*> AssemblyListSnapshot::GetAllAssemblies() const
{
    std::vector<LoadedAssembly*> results;
    // The C# local function AddDescendants: the folders first, then the
    // .dll/.exe entries resolved on their containing folder.
    const auto addDescendants = [&results](const PackageFolder&
                                               folder,
                                      const auto& self) -> void {
        for (const auto& subFolder : folder.Folders()) {
            self(*subFolder, self);
        }
        for (const auto& entry : folder.Entries()) {
            if (!EndsWithExecutableExtension(entry->Name())) {
                continue;
            }
            if (LoadedAssembly* asm_ = folder.ResolveFileName(entry->Name())) {
                results.push_back(asm_);
            }
        }
    };

    results.reserve(assemblies_.size());
    for (LoadedAssembly* loaded : assemblies_) {
        try {
            const auto& result = loaded->GetLoadResult();
            if (result.Package != nullptr) {
                // A package wrapper is NOT included; its entries are.
                addDescendants(result.Package->RootFolder(), addDescendants);
            } else if (result.MetadataFile != nullptr) {
                results.push_back(loaded);
            }
        } catch (const std::exception&) {
            // The C# catch arm: a faulted load is added anyway.
            results.push_back(loaded);
        }
    }
    return results;
}

const Decompiler::Metadata::MetadataFile*
AssemblyListSnapshot::TryGetModule(
    const Decompiler::Metadata::IAssemblyReference& reference,
    const std::string& tfm) const
{
    const bool isWinRT = reference.IsWindowsRuntime();
    const std::string normalizedTfm = NormalizeTargetFrameworkId(tfm);
    const std::string key = normalizedTfm + ";" +
        (isWinRT ? reference.Name() : reference.FullName());
    const auto& lookup = Lookup(isWinRT);
    const auto it = lookup.find(key);
    return it != lookup.end() ? it->second : nullptr;
}

const Decompiler::Metadata::MetadataFile*
AssemblyListSnapshot::TryGetSimilarModule(
    const Decompiler::Metadata::IAssemblyReference& reference) const
{
    const auto& lookup = ShortNameGroupLookup();
    const auto it = lookup.find(reference.Name());
    if (it == lookup.end()) {
        return nullptr;
    }
    const auto& candidates = it->second;
    // The C# `candidates.FirstOrDefault(c => c.version >=
    // reference.Version).module ?? candidates.Last().module`: the first
    // candidate covering the reference version, else the last one.
    const auto referenceVersion = reference.Version();
    for (const auto& candidate : candidates) {
        if (referenceVersion.has_value() && candidate.Version < *referenceVersion) {
            continue;
        }
        return candidate.Module;
    }
    return candidates.empty() ? nullptr : candidates.back().Module;
}

}  // namespace ILSpy::ILSpyX
