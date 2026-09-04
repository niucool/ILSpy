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

// The resource-path enumeration implementation (ResourceExtensions.hpp has
// the full contract): the embedded-resource walk with the .resources
// container entry expansion and its parse-failure fallback.

#include "ILSpyCmd/ResourceExtensions.hpp"

#include "Decompiler/Util/ResourcesFile.hpp"

#include <cstddef>

namespace ILSpy::ILSpyCmd {

namespace {

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
        Decompiler::Util::ResourcesFile resourcesFile(
            data->data(), data->size());
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

}  // namespace ILSpy::ILSpyCmd
