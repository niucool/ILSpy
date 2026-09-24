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

#include "ILSpyX/AssemblyList.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace ILSpy::ILSpyX {

namespace {

// The C# `Path.GetFullPath(file)` (the IlspyCmdProgram shape): resolve
// against the current directory and normalize the '.'/'..' components.
std::string GetFullPath(const std::string& path)
{
    namespace fs = std::filesystem;
    return fs::absolute(fs::path(path)).lexically_normal().string();
}

}  // namespace

bool OrdinalIgnoreCaseLess::operator()(
    const std::string& a, const std::string& b) const
{
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char ca = lower(a[i]);
        const char cb = lower(b[i]);
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

std::vector<LoadedAssembly*> AssemblyList::GetAssemblies() const
{
    std::lock_guard<std::mutex> lock(lockObj_);
    std::vector<LoadedAssembly*> result;
    result.reserve(assemblies_.size());
    for (const auto& asm_ : assemblies_) {
        result.push_back(asm_.get());
    }
    return result;
}

int AssemblyList::Count() const
{
    std::lock_guard<std::mutex> lock(lockObj_);
    return static_cast<int>(assemblies_.size());
}

LoadedAssembly* AssemblyList::FindAssembly(const std::string& file) const
{
    const std::string fullPath = GetFullPath(file);
    std::lock_guard<std::mutex> lock(lockObj_);
    const auto it = byFilename_.find(fullPath);
    return it != byFilename_.end() ? it->second.get() : nullptr;
}

LoadedAssembly& AssemblyList::Open(const std::string& assemblyUri,
    bool isAutoLoaded)
{
    return OpenAssembly(assemblyUri, isAutoLoaded);
}

LoadedAssembly& AssemblyList::OpenAssembly(const std::string& file,
    bool isAutoLoaded)
{
    // The C# factory closure reads the already-normalized path.
    const std::string fullPath = GetFullPath(file);
    return OpenAssembly(fullPath, [this, fullPath, isAutoLoaded]() {
        LoadedAssembly::Options options;
        // The manager-driven ctor (a later slice) passes the manager's
        // loader registry and the list flags here; the testing-only list
        // loads through the PEFileLoader fallback, exactly like the C#
        // testing ctor's null manager.
        auto loaded = std::make_unique<LoadedAssembly>(*this, fullPath,
            options);
        loaded->SetIsAutoLoaded(isAutoLoaded);
        return loaded;
    });
}

LoadedAssembly& AssemblyList::OpenAssembly(const std::string& file,
    std::function<std::optional<std::vector<std::uint8_t>>()> stream,
    bool isAutoLoaded)
{
    const std::string fullPath = GetFullPath(file);
    return OpenAssembly(fullPath, [this, fullPath,
                                      stream = std::move(stream),
                                      isAutoLoaded]() {
        LoadedAssembly::Options options;
        options.Stream = stream;
        auto loaded = std::make_unique<LoadedAssembly>(*this, fullPath,
            options);
        loaded->SetIsAutoLoaded(isAutoLoaded);
        return loaded;
    });
}

LoadedAssembly& AssemblyList::OpenAssembly(const std::string& fullPath,
    const std::function<std::unique_ptr<LoadedAssembly>()>& load)
{
    std::lock_guard<std::mutex> lock(lockObj_);
    const auto it = byFilename_.find(fullPath);
    if (it != byFilename_.end()) {
        return *it->second;
    }
    std::unique_ptr<LoadedAssembly> asm_ = load();
    // The C# Debug.Assert(asm.FileName == file): the factory receives the
    // already-normalized path.
    LoadedAssembly& ref = *asm_;
    byFilename_.emplace(ref.FileName(), std::move(asm_));
    assemblies_.push_back(byFilename_.at(ref.FileName()));
    return ref;
}

AssemblyListSnapshot AssemblyList::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(lockObj_);
    std::vector<LoadedAssembly*> snapshot;
    snapshot.reserve(assemblies_.size());
    for (const auto& asm_ : assemblies_) {
        snapshot.push_back(asm_.get());
    }
    return AssemblyListSnapshot(std::move(snapshot));
}

}  // namespace ILSpy::ILSpyX
