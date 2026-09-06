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
// resource-path enumeration (EnumerateResourcePaths), the resource
// lookup (TryGetResource over the ResourcesFile value decode), and the
// DecompileBaml BamlDecompiler bridge.

#pragma once

#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Xml {
class XDocument;
}

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
} // namespace ILSpy::Decompiler::Metadata

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

// The C# `XDocument DecompileBaml(MetadataFile module, IAssemblyResolver
// resolver, Stream bamlStream, BamlDecompilerSettings settings,
// CancellationToken cancellationToken)` -- the BamlDecompiler bridge the
// CLI's --resource .baml arm drives: a BamlDecompilerTypeSystem over the
// caller's module and resolver, an XamlDecompiler with the caller's
// settings, and Decompile over the BAML stream bytes returning the result's
// Xaml document. The C# CancellationToken property is the documented
// XamlDecompiler deferral (no ported consumer cancels); the C# Stream
// parameter carries the resource's whole blob, which the port takes as the
// byte span (the Decompile byte-span convention). A BamlReader rejection
// (an invalid BAML signature length and friends) propagates out -- the C#
// InvalidDataException escapes ExtractResource to the global catch.
std::shared_ptr<Decompiler::Xml::XDocument> DecompileBaml(
    const Decompiler::Metadata::MetadataFile& module,
    const Decompiler::Metadata::IAssemblyResolver& resolver,
    const std::uint8_t* bamlStream, std::size_t bamlStreamSize,
    const BamlDecompiler::BamlDecompilerSettings& settings);

}  // namespace ILSpy::ILSpyCmd
