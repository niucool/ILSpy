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

// Implementation of ILSpyX/FileLoaders/MetadataFileLoader.hpp (the
// porting decisions are on the header).

#include "ILSpyX/FileLoaders/MetadataFileLoader.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <cstring>

namespace ILSpy::ILSpyX::FileLoaders {

std::optional<LoadResult> MetadataFileLoader::Load(
    const std::string& fileName, const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context) const
{
    (void)context;  // the C# reads only the stream and the file name
    // The C# kind selection: ".pdb" (StringComparison.OrdinalIgnoreCase)
    // -> ProgramDebugDatabase, else Metadata. The fold mirrors
    // OrdinalIgnoreCase (the ASCII letters only); the file itself is
    // opened by the path's exact casing.
    const std::size_t nameLength = fileName.size();
    bool isPdb = false;
    if (nameLength >= 4) {
        const std::string_view extension =
            std::string_view(fileName).substr(nameLength - 4);
        isPdb = extension[0] == '.' &&
            (extension[1] | 0x20) == 'p' && (extension[2] | 0x20) == 'd' &&
            (extension[3] | 0x20) == 'b';
    }
    const Decompiler::Metadata::MetadataFile::MetadataFileKind kind =
        isPdb ? Decompiler::Metadata::MetadataFile::MetadataFileKind::
                    ProgramDebugDatabase
              : Decompiler::Metadata::MetadataFile::MetadataFileKind::
                    Metadata;

    // The C# `MetadataReaderProvider.FromMetadataStream(stream,
    // PrefetchMetadata | LeaveOpen)`: the bytes the loaders carry ARE
    // the stream. The port's ctor validates the BSJB magic and parses;
    // an invalid stream is the decline (the C# catch's null).
    std::vector<std::uint8_t> bytes(data, data + size);
    auto metadataFile = std::make_unique<Decompiler::Metadata::MetadataFile>(
        fileName, kind, std::move(bytes));
    if (!metadataFile->IsValid()) {
        return std::nullopt;
    }
    LoadResult result;
    result.MetadataFile = std::move(metadataFile);
    return result;
}

}  // namespace ILSpy::ILSpyX::FileLoaders
