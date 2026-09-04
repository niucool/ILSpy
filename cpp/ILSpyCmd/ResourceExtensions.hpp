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

// Port of ICSharpCode.ILSpyCmd/ResourceExtensions.cs -- the resource-path
// enumeration the CLI's --list-resources flag renders (and that the
// --resource extraction lists as its available-resources error). This
// slice ports EnumerateResourcePaths; TryGetResource (the entry lookup with
// the ResourcesFile value decode) and DecompileBaml (the BamlDecompiler
// bridge) are deferred to their own slices -- the C#
// --decompile-baml/--resource paths that consume them.

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"

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

}  // namespace ILSpy::ILSpyCmd
