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

// Port of ICSharpCode.ILSpyX/AssemblyList.cs: the list of assemblies (the
// open-by-path deduplication, the FindAssembly lookup, and the list
// surface).
//
// C#-to-C++ porting decisions:
//  * The C# ObservableCollection<LoadedAssembly> ports as a vector of
//    shared_ptr (the C# "removed but NOT disposed" notes -- HotReplace /
//    Unload / Clear drop the instance and let the GC reclaim it once
//    nothing holds it; shared ownership is the port's stand-in). The
//    byFilename dictionary is keyed case-insensitively (the C#
//    StringComparer.OrdinalIgnoreCase).
//  * The C# `Path.GetFullPath` normalization ports as
//    std::filesystem::absolute + lexically_normal (the IlspyCmdProgram
//    shape).
//  * The C# thread-affinity machinery (ownerThread / VerifyAccess /
//    SynchronizationContext / BeginInvoke) does not port: the port has no
//    thread-affinity notion, and the BeginInvoke dispatch (the
//    non-UI-thread add path) runs inline. The list is still internally
//    locked for concurrent read access.
//  * The C# ctor pair (the parameterless testing-only ctor and the
//    (AssemblyListManager, listName) ctor) ports as the default ctor for
//    now; the manager-driven ctor and the CollectionChanged /
//    RefreshSave / SaveAsXml machinery arrive with the
//    AssemblyListManager slice. ApplyWinRTProjections / UseDebugSymbols /
//    LoaderRegistry come from the manager there too.
//  * The C# `OpenAssembly(string file, Stream? stream, ...)` overload
//    takes the stream-provider convention (see LoadedAssembly::Options):
//    the PackageFolder.ResolveFileName path passes a deferred provider,
//    exactly like the C# `stream: Task.Run(entry.TryOpenStream)` arm
//    constructing LoadedAssembly directly.
//  * DEFERRED: the GetAllAssemblies recursion (it builds LoadedAssembly
//    instances through PackageFolder.ResolveFileName -- the resolver
//    slice); GetSnapshot is present (the resolver takes it).

#pragma once

#include "ILSpyX/LoadedAssembly.hpp"

#include "ILSpyX/AssemblyListSnapshot.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX {

class AssemblyListManager;

// The C# `public sealed class AssemblyList`.
class AssemblyList {
public:
    // The C# internal parameterless ctor ("exists for testing only";
    // ListName "Testing Only"). The manager stays null, so the list has
    // no save wiring (the C# testing ctor skips the CollectionChanged
    // subscription for the same reason).
    AssemblyList()
        : listName_("Testing Only")
    {
    }

    // The C# internal ctor (AssemblyListManager manager, string listName):
    // the manager carries the save wiring and the loader/flag defaults.
    AssemblyList(AssemblyListManager& manager, std::string listName);

    // The C# internal ctor (AssemblyListManager manager, XElement
    // listElement): loads the stored assemblies (OpenAssembly + the
    // TargetFramework attribute) and resets the dirty flag afterwards.
    AssemblyList(AssemblyListManager& manager,
        const Decompiler::Xml::XElement& listElement);

    // The C# `public LoadedAssembly[] GetAssemblies()`: thread-safe
    // snapshot of the list.
    std::vector<LoadedAssembly*> GetAssemblies() const;

    // The C# `public int Count`.
    int Count() const;

    // The C# `public string ListName { get; }`.
    const std::string& ListName() const { return listName_; }

    // The C# `internal AssemblyListSnapshot GetSnapshot()`: the
    // point-in-time view the resolver resolves against.
    AssemblyListSnapshot GetSnapshot() const;

    // The C# `public LoadedAssembly? FindAssembly(string file)` -- the
    // byFilename lookup after Path.GetFullPath.

    // --- the mutation surface ---

    // The C# `public void Unload(LoadedAssembly assembly)`: removed from
    // the list and the byFilename map, NOT disposed (the C# comment: open
    // tabs may still hold the metadata; shared ownership stands in for the
    // GC).
    void Unload(LoadedAssembly& assembly);

    // The C# `public void Clear()`.
    void Clear();

    // The C# `public void Move(LoadedAssembly[] assembliesToMove, int
    // index)`: removes the assemblies, then re-inserts them at the
    // adjusted index (the nodeIndex < index decrement rule).
    void Move(const std::vector<LoadedAssembly*>& assembliesToMove,
        int index);

    // The C# `public void Sort(IComparer<LoadedAssembly> comparer)` /
    // `Sort(int index, int count, comparer)`. The comparer is a
    // two-argument compare function (negative/zero/positive).
    void Sort(int index, int count,
        const std::function<int(const LoadedAssembly&,
            const LoadedAssembly&)>& comparer);
    void Sort(const std::function<int(const LoadedAssembly&,
        const LoadedAssembly&)>& comparer)
    {
        Sort(0, std::numeric_limits<int>::max(), comparer);
    }

    // The C# `public LoadedAssembly? ReloadAssembly(string file)` / the
    // LoadedAssembly overload: a fresh instance at the same position,
    // carrying PdbFileName, the auto-loaded flag, and the framework
    // override; the old instance is dropped (not disposed -- the C#
    // comment).
    LoadedAssembly* ReloadAssembly(const std::string& file);
    LoadedAssembly* ReloadAssembly(LoadedAssembly& target);

    // The C# `public LoadedAssembly? HotReplaceAssembly(string file,
    // Stream stream)`: swaps the object model from a crafted stream
    // without disk I/O; null when the file is not loaded.
    LoadedAssembly* HotReplaceAssembly(const std::string& file,
        std::function<std::optional<std::vector<std::uint8_t>>()> stream);

    // The C# `internal XElement SaveAsXml()`: the <List> element with the
    // non-auto-loaded assemblies (the TargetFramework attribute carried).
    std::shared_ptr<Decompiler::Xml::XElement> SaveAsXml() const;

    // The C# `public void RefreshSave()`: marks the list dirty and saves
    // it through the manager (inline -- the port has no
    // SynchronizationContext; the C# BeginInvoke deferral collapses).
    void RefreshSave();

    // The flags the manager seeded (the C# properties).
    bool ApplyWinRTProjections() const { return applyWinRTProjections_; }
    bool UseDebugSymbols() const { return useDebugSymbols_; }

    LoadedAssembly* FindAssembly(const std::string& file) const;

    // The C# `public LoadedAssembly Open(string assemblyUri, bool
    // isAutoLoaded = false)`.
    LoadedAssembly& Open(const std::string& assemblyUri,
        bool isAutoLoaded = false);

    // The C# `public LoadedAssembly OpenAssembly(string file, bool
    // isAutoLoaded = false)`: opens an assembly from disk; returns the
    // existing node when already loaded.
    LoadedAssembly& OpenAssembly(const std::string& file,
        bool isAutoLoaded = false);

    // The C# `public LoadedAssembly OpenAssembly(string file, Stream?
    // stream, bool isAutoLoaded = false)`: opens an assembly from a
    // stream; a disengaged optional (the C# null stream) falls back to
    // the file.
    LoadedAssembly& OpenAssembly(const std::string& file,
        std::function<std::optional<std::vector<std::uint8_t>>()> stream,
        bool isAutoLoaded = false);

private:
    LoadedAssembly& OpenAssembly(const std::string& fullPath,
        const std::function<std::unique_ptr<LoadedAssembly>()>& load);

    // The C# `AssemblyList(AssemblyList list, string newName)` body: the
    // source's assemblies are adopted SHARED (the same instances); the
    // byFilename map is NOT copied (the C# copies only the assemblies
    // collection -- FindAssembly on a cloned list misses).
    void AdoptAssembliesFrom(AssemblyList& source);

    // The C# `CollectionChangeHasEffectOnSave` filter: an Add/Remove only
    // marks the list dirty when the touched assembly is not auto-loaded.
    void OnCollectionChanged(LoadedAssembly& touched, bool added);

    std::string listName_;
    // The C# `ApplyWinRTProjections` / `UseDebugSymbols` flags arrive with
    // the manager-driven ctor; the testing-only ctor leaves them false
    // (the C# does too).
    bool applyWinRTProjections_ = false;
    bool useDebugSymbols_ = false;
    // The owning manager (null for the testing-only list; the save wiring
    // exists only when set). The manager holds the list, so the pointer is
    // non-owning.
    AssemblyListManager* manager_ = nullptr;
    // The C# dirty flag (RefreshSave).
    bool dirty_ = false;
    FileLoaders::FileLoaderRegistry* loaderRegistry_ = nullptr;

    friend class AssemblyListManager;
    // The assemblies (shared ownership -- the C# GC stand-in for the
    // "dropped but not disposed" removals), guarded by lockObj_.
    mutable std::mutex lockObj_;
    std::vector<std::shared_ptr<LoadedAssembly>> assemblies_;
    std::map<std::string, std::shared_ptr<LoadedAssembly>,
        OrdinalIgnoreCaseLess>
        byFilename_;
};

}  // namespace ILSpy::ILSpyX
