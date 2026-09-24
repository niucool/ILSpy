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

#include "ILSpyX/AssemblyListManager.hpp"

#include <algorithm>
#include <cstdio>
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
        options.FileLoaders = loaderRegistry_;
        options.ApplyWinRTProjections = applyWinRTProjections_;
        options.UseDebugSymbols = useDebugSymbols_;
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
    bool refresh = false;
    LoadedAssembly* opened = nullptr;
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        const auto it = byFilename_.find(fullPath);
        if (it != byFilename_.end()) {
            return *it->second;
        }
        std::unique_ptr<LoadedAssembly> asm_ = load();
        // The C# Debug.Assert(asm.FileName == file): the factory receives
        // the already-normalized path.
        LoadedAssembly& ref = *asm_;
        const bool isAutoLoaded = ref.IsAutoLoaded();
        byFilename_.emplace(ref.FileName(), std::move(asm_));
        assemblies_.push_back(byFilename_.at(ref.FileName()));
        // The C# ObservableCollection.CollectionChanged fires here (the UI
        // thread add path); only a non-auto-loaded assembly marks the list
        // dirty. The save itself runs AFTER the lock scope: the C#
        // BeginInvoke deferral keeps RefreshSave off the caller's stack,
        // and SaveAsXml takes this same non-reentrant lock.
        refresh = !isAutoLoaded;
        opened = &ref;
    }
    if (refresh) {
        RefreshSave();
    }
    return *opened;
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

// The C# string.Equals(..., StringComparison.OrdinalIgnoreCase).
static bool OrdinalIgnoreCaseEquals(const std::string& a,
    const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

// The C# internal ctor (AssemblyListManager manager, string listName): the
// manager carries the save wiring, the loader registry, and the flag
// defaults. (The C# `manager ?? throw ArgumentNullException` is N/A.)
AssemblyList::AssemblyList(AssemblyListManager& manager,
    std::string listName)
    : listName_(std::move(listName)), manager_(&manager),
      applyWinRTProjections_(manager.ApplyWinRTProjections()),
      useDebugSymbols_(manager.UseDebugSymbols()),
      loaderRegistry_(&manager.LoaderRegistry())
{
}

void AssemblyList::Unload(LoadedAssembly& assembly)
{
    // Read the flag BEFORE the erase: Unload may drop the last owning
    // reference (the C# GC keeps the object alive through the handler; the
    // port must not touch it afterwards).
    const bool isAutoLoaded = assembly.IsAutoLoaded();
    const std::string fileName = assembly.FileName();
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        const auto it = std::find_if(assemblies_.begin(), assemblies_.end(),
            [&assembly](const auto& entry) {
                return entry.get() == &assembly;
            });
        if (it != assemblies_.end()) {
            assemblies_.erase(it);
        }
        byFilename_.erase(fileName);
    }
    // The C# fires CollectionChanged(Remove) inside the mutation; only a
    // non-auto-loaded assembly marks the list dirty.
    if (!isAutoLoaded) {
        RefreshSave();
    }
}

void AssemblyList::Clear()
{
    std::lock_guard<std::mutex> lock(lockObj_);
    assemblies_.clear();
    byFilename_.clear();
    // The C# fires CollectionChanged(Reset); the save filter's default arm
    // treats it as effective.
    RefreshSave();
}

void AssemblyList::Move(
    const std::vector<LoadedAssembly*>& assembliesToMove, int index)
{
    std::lock_guard<std::mutex> lock(lockObj_);
    // The C# removes each node first (decrementing the target index when
    // the node sat before it), then re-inserts in reverse order at the
    // (possibly adjusted) target index.
    for (LoadedAssembly* asm_ : assembliesToMove) {
        const auto it = std::find_if(assemblies_.begin(), assemblies_.end(),
            [asm_](const auto& entry) { return entry.get() == asm_; });
        if (it == assemblies_.end()) {
            continue;  // the C# Debug.Assert(nodeIndex >= 0)
        }
        const auto nodeIndex = static_cast<int>(it - assemblies_.begin());
        if (nodeIndex < index) {
            index--;
        }
        assemblies_.erase(it);
    }
    for (auto it = assembliesToMove.rbegin(); it != assembliesToMove.rend();
         ++it) {
        const auto slot = static_cast<std::size_t>(
            std::max(index, 0));
        assemblies_.insert(assemblies_.begin() + slot,
            byFilename_.at((*it)->FileName()));
    }
}

void AssemblyList::Sort(int index, int count,
    const std::function<int(const LoadedAssembly&, const LoadedAssembly&)>&
        comparer)
{
    std::lock_guard<std::mutex> lock(lockObj_);
    // The C# copies the list, sorts the requested range, and rebuilds.
    const int size = static_cast<int>(assemblies_.size());
    const int begin = std::max(index, 0);
    const int end = std::min(index + count, size);
    if (begin >= end) {
        return;
    }
    std::sort(assemblies_.begin() + begin, assemblies_.begin() + end,
        [&comparer](const auto& a, const auto& b) {
            return comparer(*a, *b) < 0;
        });
}

LoadedAssembly* AssemblyList::ReloadAssembly(const std::string& file)
{
    // The C# compares the file names OrdinalIgnoreCase.
    const std::string fullPath = GetFullPath(file);
    LoadedAssembly* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        for (const auto& asm_ : assemblies_) {
            if (OrdinalIgnoreCaseEquals(asm_->FileName(), fullPath)) {
                target = asm_.get();
                break;
            }
        }
    }
    if (target == nullptr) {
        return nullptr;
    }
    return ReloadAssembly(*target);
}

LoadedAssembly* AssemblyList::ReloadAssembly(LoadedAssembly& target)
{
    const std::optional<std::string> pdbFileName = target.PdbFileName();
    const bool isAutoLoaded = target.IsAutoLoaded();
    const std::optional<std::string> tfmOverride =
        target.TargetFrameworkIdOverride();
    LoadedAssembly* fresh = nullptr;
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        const auto it = std::find_if(assemblies_.begin(), assemblies_.end(),
            [&target](const auto& entry) {
                return entry.get() == &target;
            });
        if (it == assemblies_.end()) {
            return nullptr;
        }
        // A fresh instance at the same position, carrying the flags (the
        // C# ctor arguments: pdbFileName, the loader registry, the flags).
        LoadedAssembly::Options options;
        options.FileLoaders = loaderRegistry_;
        options.PdbFileName = pdbFileName;
        options.ApplyWinRTProjections = applyWinRTProjections_;
        options.UseDebugSymbols = useDebugSymbols_;
        auto newAsm = std::make_unique<LoadedAssembly>(*this,
            target.FileName(), options);
        newAsm->SetIsAutoLoaded(isAutoLoaded);
        newAsm->SetTargetFrameworkIdOverride(tfmOverride);
        // The old instance is dropped, not disposed (the C# comment: open
        // tabs may still hold its metadata; the GC -- here shared
        // ownership -- reclaims it once nothing holds it).
        fresh = newAsm.get();
        byFilename_[fresh->FileName()] = std::move(newAsm);
        *it = byFilename_.at(fresh->FileName());
        if (!isAutoLoaded) {
            RefreshSave();
        }
    }
    return fresh;
}

LoadedAssembly* AssemblyList::HotReplaceAssembly(const std::string& file,
    std::function<std::optional<std::vector<std::uint8_t>>()> stream)
{
    const std::string fullPath = GetFullPath(file);
    LoadedAssembly* replaced = nullptr;
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        const auto byIt = byFilename_.find(fullPath);
        if (byIt == byFilename_.end()) {
            return nullptr;
        }
        const auto it = std::find_if(assemblies_.begin(), assemblies_.end(),
            [&byIt](const auto& entry) {
                return entry.get() == byIt->second.get();
            });
        if (it == assemblies_.end()) {
            return nullptr;
        }
        const bool isAutoLoaded = (*it)->IsAutoLoaded();
        LoadedAssembly::Options options;
        options.FileLoaders = loaderRegistry_;
        options.Stream = std::move(stream);
        options.ApplyWinRTProjections = applyWinRTProjections_;
        options.UseDebugSymbols = useDebugSymbols_;
        auto newAsm = std::make_unique<LoadedAssembly>(*this, fullPath,
            std::move(options));
        newAsm->SetIsAutoLoaded(isAutoLoaded);
        replaced = newAsm.get();
        byFilename_[fullPath] = std::move(newAsm);
        *it = byFilename_.at(fullPath);
    }
    return replaced;
}

std::shared_ptr<Decompiler::Xml::XElement> AssemblyList::SaveAsXml() const
{
    namespace Xml = ILSpy::Decompiler::Xml;
    std::vector<LoadedAssembly*> snapshot;
    {
        std::lock_guard<std::mutex> lock(lockObj_);
        for (const auto& asm_ : assemblies_) {
            if (!asm_->IsAutoLoaded()) {
                snapshot.push_back(asm_.get());
            }
        }
    }
    auto element = std::make_shared<Xml::XElement>(Xml::XName("List"));
    element->SetAttributeValue("name", listName_);
    for (LoadedAssembly* asm_ : snapshot) {
        auto assembly = std::make_shared<Xml::XElement>(Xml::XName("Assembly"));
        assembly->Add(asm_->FileName());
        if (asm_->TargetFrameworkIdOverride().has_value()) {
            assembly->SetAttributeValue("TargetFramework",
                *asm_->TargetFrameworkIdOverride());
        }
        element->Add(std::move(assembly));
    }
    return element;
}

void AssemblyList::AdoptAssembliesFrom(AssemblyList& source)
{
    std::lock_guard<std::mutex> lockA(lockObj_);
    std::lock_guard<std::mutex> lockB(source.lockObj_);
    // The C# copy ctor: `this.assemblies.AddRange(list.assemblies)` --
    // the same instances, shared ownership. byFilename is deliberately
    // not copied (the C# quirk).
    for (const auto& asm_ : source.assemblies_) {
        assemblies_.push_back(asm_);
    }
}

void AssemblyList::RefreshSave()
{
    // Whenever the list is modified, mark it dirty and save it (the C#
    // BeginInvoke deferral collapses inline -- the port has no
    // SynchronizationContext).
    if (!dirty_) {
        dirty_ = true;
        if (dirty_) {
            dirty_ = false;
            if (manager_ != nullptr) {
                manager_->SaveList(*this);
            }
        }
    }
}

AssemblyList::AssemblyList(AssemblyListManager& manager,
    const Decompiler::Xml::XElement& listElement)
    : AssemblyList(manager,
          [&listElement]() {
              // The C# `(string?)listElement.Attribute("name") ??
              // DefaultListName` (the XElement deep-copy keeps the
              // non-const iteration surface).
              Decompiler::Xml::XElement copy(listElement);
              const auto* name = copy.Attribute("name");
              return name != nullptr
                  ? name->Value()
                  : std::string(AssemblyListManager::DefaultListName);
          }())
{
    // The C# ctor body: OpenAssembly for each stored path, seeding the
    // framework override, then reset the dirty flag (OpenAssembly sets
    // it). The C# iterates Elements("Assembly"); the port skips other
    // child shapes explicitly.
    Decompiler::Xml::XElement copy(listElement);
    for (const auto& asm_ : copy.Elements()) {
        if (asm_->Name().LocalName() != "Assembly") {
            continue;
        }
        LoadedAssembly& loaded = OpenAssembly(asm_->Value());
        const auto* tfm = asm_->Attribute("TargetFramework");
        loaded.SetTargetFrameworkIdOverride(
            tfm != nullptr ? std::optional<std::string>(tfm->Value())
                           : std::optional<std::string>());
    }
    dirty_ = false;
}

}  // namespace ILSpy::ILSpyX
