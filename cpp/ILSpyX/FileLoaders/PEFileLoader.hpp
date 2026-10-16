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

// Port of ICSharpCode.ILSpyX/FileLoaders/PEFileLoader.cs: the MZ-gated PE
// loader. C#-to-C++ decisions on LoadResult.hpp; this file adds: the
// MetadataReaderOptions (ApplyWindowsRuntimeProjections) the C# passes the
// PEFile ctor has no port analogue (the winmd reader has no options
// surface), so the context flag rides FileLoadContext untouched and the
// open itself is options-free -- a documented divergence.

#pragma once

#include "ILSpyX/FileLoaders/LoadResult.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class PEFileLoader : IFileLoader`.
class PEFileLoader final : public IFileLoader {
public:
    // The C# `Load`: null unless the stream starts with 'MZ'.
    std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const override;

    // The C# `public static Task<LoadResult> LoadPEFile(string fileName,
    // Stream stream, FileLoadContext context)`: the PE open over the image
    // bytes. The port's never-throwing MetadataFile reports an unparseable
    // image through IsValid() (the established port divergence; the C#
    // throws BadImageFormatException out of the PEFile ctor).
    static LoadResult LoadPEFile(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context);
};

}  // namespace ILSpy::ILSpyX::FileLoaders
