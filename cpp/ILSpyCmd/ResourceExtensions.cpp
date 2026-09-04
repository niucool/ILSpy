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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The resource-path enumeration and lookup implementation
// (ResourceExtensions.hpp has the full contract): the embedded-resource
// walk with the .resources container entry expansion and its
// parse-failure fallback, and the resource-path lookup with the value
// decode.

#include "ILSpyCmd/ResourceExtensions.hpp"

#include "Decompiler/Util/ResourcesFile.hpp"

#include <cstddef>
#include <memory>
#include <optional>

namespace ILSpy::ILSpyCmd {

namespace {

using ILSpy::Decompiler::Util::ResourceValue;
using ILSpy::Decompiler::Util::ResourcesFile;

// The C# `EndsWith(string, StringComparison.OrdinalIgnoreCase)` over the
// ".resources" suffix (the port's StringComparers carry no EndsWith; the
// fixed 10-char suffix makes the case fold direct).
bool EndsWithResourcesCaseInsensitive(const std::string& name) {
    static constexpr const char* kSuffix = ".resources";
    constexpr std::size_t kSuffixLen = 10;
    if (name.size() < kSuffixLen) return false;
    for (std::size_t i = 0; i < kSuffixLen; i++) {
        char c = name[name.size() - kSuffixLen + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kSuffix[i]) return false;
    }
    return true;
}

// The C# `string.Equals(string, string, StringComparison.OrdinalIgnoreCase)`
// and `StartsWith`/`EndsWith` with the same comparison: the ordinal
// case-insensitive fold (the ASCII letters fold; nothing else does) over
// the byte sequences.
bool EqualsOrdinalIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

// The C# `TryReadResourcesEntryNames(Resource r, out IReadOnlyList<string>
// names)`: the resource's blob parsed as a .resources container, with its
// entry names in row order. The C# catches BadImageFormatException and
// EndOfStreamException out of the ResourcesFile construction; the port's
// reader throws std::out_of_range for both (the ResourcesFile contract),
// so that is the rejected-container shape.
bool TryReadResourcesEntryNames(
    const Decompiler::Metadata::MetadataFile& module,
    const Decompiler::Metadata::MetadataFile::ManifestResourceInfo& resource,
    std::vector<std::string>& names) {
    auto data = module.TryGetManifestResourceData(resource.Token);
    if (!data) {
        names.clear();
        return false;
    }
    try {
        ResourcesFile resourcesFile(data->data(), data->size());
        names.clear();
        names.reserve(static_cast<std::size_t>(
            resourcesFile.ResourceCount()));
        for (int i = 0; i < resourcesFile.ResourceCount(); i++)
            names.push_back(resourcesFile.GetResourceName(i));
        return true;
    } catch (const std::out_of_range&) {
        names.clear();
        return false;
    }
}

// The C# `static bool TryReadResourcesEntry(Resource r, string entryName,
// out object value)`: the container's entry with the given name (the
// case-insensitive first match in the container's row order), with the
// serialized user types and the Stream values reduced to byte arrays (the
// C# GetBytes()/stream copy). The construction catch covers ONLY the
// ResourcesFile ctor (the C# BadImageFormatException /
// EndOfStreamException filter -- the value decode runs outside it and its
// exceptions propagate to the caller exactly as the C#'s do).
bool TryReadResourcesEntry(
    const Decompiler::Metadata::MetadataFile& module,
    const Decompiler::Metadata::MetadataFile::ManifestResourceInfo& resource,
    const std::string& entryName, ResourceValue& value) {
    auto data = module.TryGetManifestResourceData(resource.Token);
    if (!data)
        return false;  // the C# stream == null
    std::unique_ptr<ResourcesFile> resourcesFile;
    try {
        resourcesFile = std::make_unique<ResourcesFile>(
            data->data(), data->size());
    } catch (const std::out_of_range&) {
        return false;
    }
    for (int i = 0; i < resourcesFile->ResourceCount(); i++) {
        if (!EqualsOrdinalIgnoreCase(resourcesFile->GetResourceName(i), entryName))
            continue;
        ResourceValue entryValue = resourcesFile->GetResourceValue(i);
        if (entryValue.kind == ResourceValue::Kind::SerializedObject) {
            // The C# `value = serialized.GetBytes()`.
            entryValue.kind = ResourceValue::Kind::ByteArray;
        } else if (entryValue.kind == ResourceValue::Kind::Stream) {
            // The C# stream copy to a MemoryStream's array.
            entryValue.kind = ResourceValue::Kind::ByteArray;
        }
        value = std::move(entryValue);
        return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> EnumerateResourcePaths(
    const Decompiler::Metadata::MetadataFile& module) {
    std::vector<std::string> paths;
    for (const auto& r : module.GetManifestResources()) {
        // The C# `module.Resources.Where(r => r.ResourceType ==
        // ResourceType.Embedded)` filter: linked and assembly-linked rows
        // are not listed.
        if (r.Kind
            != Decompiler::Metadata::MetadataFile::ManifestResourceKind::
                Embedded)
            continue;
        std::vector<std::string> entries;
        if (EndsWithResourcesCaseInsensitive(r.Name)
            && TryReadResourcesEntryNames(module, r, entries)) {
            for (const auto& name : entries)
                paths.push_back(r.Name + "/" + name);
        } else {
            paths.push_back(r.Name);
        }
    }
    return paths;
}

std::optional<ResourceValue> TryGetResource(
    const Decompiler::Metadata::MetadataFile& module,
    const std::string& resourcePath) {
    for (const auto& r : module.GetManifestResources()) {
        // The C# `module.Resources.Where(r => r.ResourceType ==
        // ResourceType.Embedded)` filter.
        if (r.Kind
            != Decompiler::Metadata::MetadataFile::ManifestResourceKind::
                Embedded)
            continue;
        if (EqualsOrdinalIgnoreCase(resourcePath, r.Name)) {
            // The whole-path arm: the resource's whole blob (the C#
            // TryOpenStream + CopyTo -- the bytes after the length prefix,
            // exactly TryGetManifestResourceData's blob).
            auto data = module.TryGetManifestResourceData(r.Token);
            if (!data)
                return std::nullopt;  // the C# stream == null
            ResourceValue value;
            value.kind = ResourceValue::Kind::ByteArray;
            value.bytes = std::move(*data);
            return value;
        }

        std::string prefix = r.Name + "/";
        if (EndsWithResourcesCaseInsensitive(r.Name)
            && resourcePath.size() >= prefix.size()
            && EqualsOrdinalIgnoreCase(
                resourcePath.substr(0, prefix.size()), prefix)) {
            // The entry arm: the path's remainder after the container
            // prefix (the C# StartsWith + Substring).
            std::string entryName = resourcePath.substr(prefix.size());
            ResourceValue value;
            if (TryReadResourcesEntry(module, r, entryName, value))
                return value;
        }
    }
    return std::nullopt;
}

}  // namespace ILSpy::ILSpyCmd
