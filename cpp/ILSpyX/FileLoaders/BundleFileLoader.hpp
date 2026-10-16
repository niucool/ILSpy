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

// Port of ICSharpCode.ILSpyX/FileLoaders/BundleFileLoader.cs: the
// single-file-bundle loader. The C# re-opens the file by NAME (the stream
// is not consulted); a null FromBundle result is the C# null LoadResult.
// Porting decisions on LoadResult.hpp.

#pragma once

#include "ILSpyX/FileLoaders/LoadResult.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class BundleFileLoader : IFileLoader`.
class BundleFileLoader final : public IFileLoader {
public:
    std::optional<LoadResult> Load(const std::string& fileName,
        const std::uint8_t* data, std::size_t size,
        const FileLoadContext& context) const override;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
