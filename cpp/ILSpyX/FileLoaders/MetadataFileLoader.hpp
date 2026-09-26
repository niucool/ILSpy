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

// Port of ICSharpCode.ILSpyX/FileLoaders/MetadataFileLoader.cs.
//
// Porting decisions:
//  * The C# `MetadataReaderProvider.FromMetadataStream(stream,
//    PrefetchMetadata | LeaveOpen)` reads the raw ECMA-335 metadata
//    stream the loaders' bytes carry; the port hands the bytes to the
//    metadata-stream MetadataFile ctor, which wraps them in a synthetic
//    PE image for the winmd parse (see MetadataFile.hpp's metadata-only
//    ctor note). The C# kind selection (.pdb -> ProgramDebugDatabase,
//    else Metadata) ports verbatim.
//  * The C# catch (BadImageFormatException) -> null declines any stream
    //    FromMetadataStream rejects; the port's gate is the BSJB magic
//    plus the wrapped parse succeeding (IsValid), which reduces to the
//    same claim/decline split.
//  * The resulting MetadataFile is the T3 emission path's input
//    contract: a metadata-only MetadataFile whose tables (Name, the
//    tokens, the rows) work through the established reader surface,
//    with Kind() reporting Metadata/ProgramDebugDatabase and
//    IsMetadataOnly() reporting true -- the `IsMetadataOnly: false`
//    gate in LoadedAssembly.IsLoadedAsValidAssembly consumes the latter.

#pragma once

#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class MetadataFileLoader : IFileLoader`.
class MetadataFileLoader : public IFileLoader {
public:
    std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const override;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
