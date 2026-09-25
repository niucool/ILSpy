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

// Implementation of ILSpyX/FileLoaders/FileLoaderRegistry.hpp (the porting
// decisions are on the header).

#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"

#include "ILSpyX/FileLoaders/ArchiveFileLoader.hpp"
#include "ILSpyX/FileLoaders/BundleFileLoader.hpp"
#include "ILSpyX/FileLoaders/PEFileLoader.hpp"
#include "ILSpyX/FileLoaders/WebCilFileLoader.hpp"
#include "ILSpyX/FileLoaders/XamarinCompressedFileLoader.hpp"

#include <stdexcept>

namespace ILSpy::ILSpyX::FileLoaders {

FileLoaderRegistry::FileLoaderRegistry()
{
    // The C# registration order, minus the one remaining deferral
    // (MetadataFileLoader -- see LoadResult.hpp):
    // Register(new XamarinCompressedFileLoader());
    // Register(new MetadataFileLoader());     -- deferred
    // Register(new BundleFileLoader()); // bundles are PE files with a special signature, prefer over normal PE files
    // Register(new PEFileLoader()); // prefer PE format over archives, because ZIP has no fixed header
    // Register(new ArchiveFileLoader());
    Register(std::make_unique<XamarinCompressedFileLoader>());
    Register(std::make_unique<WebCilFileLoader>());
    Register(std::make_unique<BundleFileLoader>());
    Register(std::make_unique<PEFileLoader>());
    Register(std::make_unique<ArchiveFileLoader>());
}

void FileLoaderRegistry::Register(std::unique_ptr<IFileLoader> loader)
{
    if (loader == nullptr) {
        throw std::invalid_argument(
            "Value cannot be null. (Parameter 'loader')");
    }
    registeredLoaders_.push_back(std::move(loader));
}

}  // namespace ILSpy::ILSpyX::FileLoaders
