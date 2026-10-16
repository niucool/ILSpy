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

// Implementation of ILSpyX/FileLoaders/PEFileLoader.hpp (the porting
// decisions are on the header and LoadResult.hpp).

#include "ILSpyX/FileLoaders/PEFileLoader.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

std::optional<LoadResult> PEFileLoader::Load(const std::string& fileName,
    const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context) const
{
    // The C# `if (stream.Length < 2 || stream.ReadByte() != 'M' ||
    // stream.ReadByte() != 'Z') return null;`
    if (size < 2 || data[0] != 'M' || data[1] != 'Z')
        return std::nullopt;
    return LoadPEFile(fileName, data, size, context);
}

LoadResult PEFileLoader::LoadPEFile(const std::string& fileName,
    const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context)
{
    // The C# `stream.Position = 0; new PEFile(fileName, stream,
    // PEStreamOptions.PrefetchEntireImage | PEStreamOptions.LeaveOpen,
    // metadataOptions: options)` -- the port's whole-buffer form needs no
    // rewind, and the options (the WinRT projections flag) have no reader
    // surface (the header's documented divergence).
    (void)context;
    LoadResult result;
    result.MetadataFile = std::make_unique<Decompiler::Metadata::MetadataFile>(
        fileName, std::vector<std::uint8_t>(data, data + size));
    return result;
}

}  // namespace ILSpy::ILSpyX::FileLoaders
