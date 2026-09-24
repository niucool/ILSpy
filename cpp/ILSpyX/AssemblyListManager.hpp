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

// Port of ICSharpCode.ILSpyX/AssemblyListManager.cs: the registry of
// assembly-list names over the ISettingsProvider, with the load/save and
// create/delete operations.
//
// C#-to-C++ porting decisions:
//  * The C# `ObservableCollection<string> AssemblyLists { get; }` ports as
//    a vector<string> with the Contains/Add operations the manager uses.
//  * The C# `FileLoaderRegistry LoaderRegistry { get; } = new()` ports as
//    an owned registry (the manager outlives the lists it hands out).
//  * The settings access goes through ISettingsProvider's Section/Update
//    (the C# indexer + Update closure).
//  * The C# `CreateDefaultList`'s GAC seeding runs through
//    UniversalAssemblyResolver.GetAssemblyInGac, which finds nothing
//    without a machine GAC (the port's POSIX host) -- the arms port
//    unchanged and no-op there.
//  * The C# `IsIncludedFrameworkFile` reads `fileName[0]` unguarded (an
//    empty name would throw); the port guards the empty case to false --
//    GetFiles never yields an empty name, so the guard is unreachable.
//  * The ILSpyXEventSource ETW instrumentation does not port.

#pragma once

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"
#include "ILSpyX/Settings/ISettingsProvider.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX {

// The C# `public sealed class AssemblyListManager`.
class AssemblyListManager {
public:
    static constexpr const char* DotNet4List = ".NET 4 (WPF)";
    static constexpr const char* DotNet35List = ".NET 3.5";
    static constexpr const char* ASPDotNetMVC3List = "ASP.NET (MVC3)";
    static constexpr const char* DefaultListName = "(Default)";

    // The C# `public AssemblyListManager(ISettingsProvider
    // settingsProvider)`: reads the stored list names.
    explicit AssemblyListManager(
        std::shared_ptr<Settings::ISettingsProvider> settingsProvider);

    // The C# `public bool ApplyWinRTProjections { get; set; }` /
    // `UseDebugSymbols` -- the flags new AssemblyLists seed from.
    bool ApplyWinRTProjections() const { return applyWinRTProjections_; }
    void SetApplyWinRTProjections(bool value)
    {
        applyWinRTProjections_ = value;
    }
    bool UseDebugSymbols() const { return useDebugSymbols_; }
    void SetUseDebugSymbols(bool value) { useDebugSymbols_ = value; }

    // The C# `public ObservableCollection<string> AssemblyLists { get; }`
    // (read-only surface here; the manager mutates it internally).
    const std::vector<std::string>& AssemblyLists() const
    {
        return assemblyLists_;
    }
    bool ContainsList(const std::string& name) const
    {
        return std::find(assemblyLists_.begin(), assemblyLists_.end(), name)
            != assemblyLists_.end();
    }

    // The C# `public FileLoaderRegistry LoaderRegistry { get; }`.
    FileLoaders::FileLoaderRegistry& LoaderRegistry()
    {
        return loaderRegistry_;
    }

    // The C# `public AssemblyList LoadList(string listName)`: loads the
    // named list (a fresh empty one when absent) and registers its name.
    // Caller-owned (the C# GC owns the list).
    std::unique_ptr<AssemblyList> LoadList(const std::string& listName);

    // The C# `public void SaveList(AssemblyList list)`.
    void SaveList(AssemblyList& list);

    // The C# `public bool AddListIfNotExists(AssemblyList list)`.
    bool AddListIfNotExists(AssemblyList& list);

    // The C# `public bool DeleteList(string Name)`.
    bool DeleteList(const std::string& name);

    // The C# `public void ClearAll()`.
    void ClearAll();

    // The C# `public bool CloneList(string selectedAssemblyList, string
    // newListName)` / `RenameList(...)` / `CreateList(string name)`.
    bool CloneList(const std::string& selectedAssemblyList,
        const std::string& newListName);
    bool RenameList(const std::string& selectedAssemblyList,
        const std::string& newListName);
    std::unique_ptr<AssemblyList> CreateList(const std::string& name);

    // The C# `public void CreateDefaultAssemblyLists()`.
    void CreateDefaultAssemblyLists();

    // The C# `public AssemblyList CreateDefaultList(string name, string?
    // path = null, string? newName = null)`.
    std::unique_ptr<AssemblyList> CreateDefaultList(const std::string& name,
        const std::string* path = nullptr,
        const std::string* newName = nullptr);

    // The C# `public void AddFrameworkAssembliesFromDirectory(AssemblyList
    // list, string directory)`.
    void AddFrameworkAssembliesFromDirectory(AssemblyList& list,
        const std::string& directory);

    // The C# `static bool IsIncludedFrameworkFile(string fileName)`.
    static bool IsIncludedFrameworkFile(const std::string& fileName);

private:
    // The C# `AssemblyList DoLoadList(string? listName)`.
    std::unique_ptr<AssemblyList> DoLoadList(
        const std::string* listName);

    // The C# `void AddToListFromGAC(AssemblyList list, string fullName)`
    // (the CreateDefaultList local function).
    static void AddToListFromGac(AssemblyList& list,
        const std::string& fullName);

    Settings::ISettingsProvider& settings_;
    bool applyWinRTProjections_ = false;
    bool useDebugSymbols_ = false;
    std::vector<std::string> assemblyLists_;
    FileLoaders::FileLoaderRegistry loaderRegistry_;
};

}  // namespace ILSpy::ILSpyX
