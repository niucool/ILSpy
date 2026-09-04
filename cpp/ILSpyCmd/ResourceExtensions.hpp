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

// Port of ICSharpCode.ILSpyCmd/ResourceExtensions.cs -- the resource
// surface the CLI's --list-resources and --resource paths consume: the
// resource-path enumeration (EnumerateResourcePaths) and the resource
// lookup (TryGetResource over the ResourcesFile value decode).
// DecompileBaml (the BamlDecompiler bridge) stays deferred with the
// Phase-9 BamlDecompiler.

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ILSpy::ILSpyCmd {

// The C# `ResourceExtensions.EnumerateResourcePaths(MetadataFile module)`:
// one path per embedded manifest resource -- the resource's own name, or,
// when the name ends with ".resources" (case-insensitively) and the blob
// parses as a .resources container, one "<container>/<entry>" path per
// entry (in the container's row order). A .resources-suffixed blob that
// does not parse (the ResourcesFile constructor rejects it -- a garbage or
// truncated container) falls back to the raw resource name; linked and
// assembly-linked rows are never listed. The C# yields the paths lazily;
// the port returns the vector (the two CLI consumers enumerate it whole).
std::vector<std::string> EnumerateResourcePaths(
    const Decompiler::Metadata::MetadataFile& module);

// The C# `bool TryGetResource(MetadataFile module, string resourcePath, out
// object value)`: the resource lookup by path. The whole-path arm matches an
// embedded resource's own name (case-insensitive) and yields its whole blob
// as a ByteArray; the entry arm matches a "<container>/<entry>" path into a
// .resources container (the container's name ends with ".resources",
// case-insensitively) and yields the entry's decoded value, with the
// serialized user types and the Stream values reduced to their byte arrays
// (the C# GetBytes()/stream copy in TryReadResourcesEntry). Nullopt when no
// resource matches (the C# false with value=null); an exception from the
// entry's value decode propagates out (the C# TryReadResourcesEntry catch
// covers only the container construction).
std::optional<Decompiler::Util::ResourceValue> TryGetResource(
    const Decompiler::Metadata::MetadataFile& module,
    const std::string& resourcePath);

}  // namespace ILSpy::ILSpyCmd
