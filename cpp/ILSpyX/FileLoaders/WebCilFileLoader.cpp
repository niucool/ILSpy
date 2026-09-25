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

// Implementation of ILSpyX/FileLoaders/WebCilFileLoader.hpp (the porting
// decisions are on the header).

#include "ILSpyX/FileLoaders/WebCilFileLoader.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/WebCilFile.hpp"

#include <fstream>
#include <iterator>
#include <vector>

namespace ILSpy::ILSpyX::FileLoaders {

std::optional<LoadResult> WebCilFileLoader::Load(const std::string& fileName,
    const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context) const
{
    // The C# `if (settings.ParentBundle != null) return null;` -- entries
    // of a bundle are never WebCIL containers.
    if (context.ParentBundle != nullptr) {
        return std::nullopt;
    }

    // The C# `var wasm = WebCilFile.FromFile(fileName, options);` -- the
    // file is re-read from disk; the passed bytes are unused.
    (void)data;
    (void)size;
    auto parsed = Decompiler::Metadata::WebCilFile::TryParse(fileName);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    std::ifstream in(fileName, std::ios::binary);
    if (!in) {
        // Unreachable in practice (the parse just read the same path),
        // but a vanished file is "not claimed", not a load error.
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    LoadResult result;
    result.MetadataFile =
        std::make_unique<Decompiler::Metadata::MetadataFile>(fileName,
            Decompiler::Metadata::WebCilFile::BuildPeImage(bytes, *parsed));
    return result;
}

}  // namespace ILSpy::ILSpyX::FileLoaders
