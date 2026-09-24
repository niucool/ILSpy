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
//  * DEFERRED to their own slices: the Reload / HotReplace / Move / Sort /
//    Unload / Clear mutators, GetSnapshot + AssemblyListSnapshot, and the
//    GetAllAssemblies recursion.

#pragma once

#include "ILSpyX/LoadedAssembly.hpp"

#include "ILSpyX/AssemblyListSnapshot.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX {

// The C# `public sealed class AssemblyList`.
class AssemblyList {
public:
    // The C# internal parameterless ctor ("exists for testing only";
    // ListName "Testing Only").
    AssemblyList()
        : listName_("Testing Only")
    {
    }

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

    std::string listName_;
    // The C# `ApplyWinRTProjections` / `UseDebugSymbols` flags arrive with
    // the manager-driven ctor; the testing-only ctor leaves them false
    // (the C# does too).
    bool applyWinRTProjections_ = false;
    bool useDebugSymbols_ = false;
    // The assemblies (shared ownership -- the C# GC stand-in for the
    // "dropped but not disposed" removals), guarded by lockObj_.
    mutable std::mutex lockObj_;
    std::vector<std::shared_ptr<LoadedAssembly>> assemblies_;
    std::map<std::string, std::shared_ptr<LoadedAssembly>,
        OrdinalIgnoreCaseLess>
        byFilename_;
};

}  // namespace ILSpy::ILSpyX
