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

// Port of ICSharpCode.ILSpyX/FileLoaders/WebCilFileLoader.cs.
//
// Porting decisions:
//  * The C# `WebCilFile.FromFile(fileName, options)` re-opens the file from
//    disk (the passed stream is unused) and returns null on any parse
//    failure; the port mirrors both: the structural parse (TryParse) runs
//    over the file path, and every rejection is the nullopt "not claimed".
//  * The C# `MetadataReaderOptions` (ApplyWindowsRuntimeProjections) has
//    no reader surface in the port (the PEFileLoader precedent).
//  * The C# returns a WebCilFile (a MetadataFile subclass backed by the
//    metadata stream); the port returns the PE-shaped MetadataFile over
//    WebCilFile::BuildPeImage's adapted image (see the adapter's note),
//    which preserves the metadata tables and the method bodies verbatim.

#pragma once

#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class WebCilFileLoader : IFileLoader`.
class WebCilFileLoader : public IFileLoader {
public:
    std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const override;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
