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

// Port of ICSharpCode.ILSpyX/FileLoaders/FileLoaderRegistry.cs: the
// registration list and its default population. The C# registers
// XamarinCompressed, WebCil, Metadata, Bundle, PE, then Archive; the port
// registers XamarinCompressed, Bundle, PE, Archive -- WebCil and Metadata
// are documented deferrals (LoadResult.hpp), so files only those loaders
// claim fall through to the remaining order (the precedence between the
// four ported loaders is unchanged).

#pragma once

#include "ILSpyX/FileLoaders/LoadResult.hpp"

#include <memory>
#include <vector>

namespace ILSpy::ILSpyX::FileLoaders {

// The C# `public sealed class FileLoaderRegistry`.
class FileLoaderRegistry {
public:
    FileLoaderRegistry();

    const std::vector<std::unique_ptr<IFileLoader>>& RegisteredLoaders() const
    {
        return registeredLoaders_;
    }

    // The C# `public void Register(IFileLoader loader)`: appends; a null
    // loader throws the ArgumentNullException
    // ("Value cannot be null. (Parameter 'loader')") as
    // std::invalid_argument.
    void Register(std::unique_ptr<IFileLoader> loader);

private:
    std::vector<std::unique_ptr<IFileLoader>> registeredLoaders_;
};

}  // namespace ILSpy::ILSpyX::FileLoaders
