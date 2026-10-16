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

// Implementation of ILSpyX/FileLoaders/BundleFileLoader.hpp (the porting
// decisions are on the header and LoadResult.hpp).

#include "ILSpyX/FileLoaders/BundleFileLoader.hpp"

namespace ILSpy::ILSpyX::FileLoaders {

std::optional<LoadResult> BundleFileLoader::Load(const std::string& fileName,
    const std::uint8_t* data, std::size_t size,
    const FileLoadContext& context) const
{
    // The C# `if (settings.ParentBundle != null) return null;` -- entries
    // inside a bundle are not re-interpreted as bundles.
    if (context.ParentBundle != nullptr)
        return std::nullopt;
    // The C# re-opens by file name (the stream is never consulted).
    (void)data;
    (void)size;
    std::shared_ptr<LoadedPackage> bundle = LoadedPackage::FromBundle(fileName);
    if (bundle == nullptr)
        return std::nullopt;
    LoadResult result;
    result.Package = std::move(bundle);
    return result;
}

}  // namespace ILSpy::ILSpyX::FileLoaders
