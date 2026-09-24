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

// Port of ICSharpCode.ILSpyX/AssemblyListSnapshot.cs: the point-in-time
// view of an AssemblyList the resolver resolves against (the
// tfm+full-name exact-match lookup and the short-name version groups).
//
// C#-to-C++ porting decisions:
//  * The C# async pipeline ports synchronously (no Task analogue); the
//    LazyInit.VolatileRead/GetOrSet lookups port as mutex-guarded
//    lazily-built maps.
//  * The C# Dictionary(StringComparer.OrdinalIgnoreCase) keys port as
//    case-insensitively ordered maps (the AssemblyList comparator).
//  * The per-assembly catch (BadImageFormatException) continue arm ports
//    as catch (const std::exception&) -- the port's readers surface
//    corrupt images as std exceptions.
//  * `reader.IsAssembly` ports as GetAssemblyDefinition().has_value()
//    (the Assembly-table row presence).
//  * The ILSpyXEventSource ETW instrumentation does not port.
//  * DEFERRED to the assembly-recursion slice: GetAllAssembliesAsync.

#pragma once

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX {

// The StringComparer.OrdinalIgnoreCase key shape shared by the
// assembly-list lookup tables: ASCII A-Z lowercased on both sides (the
// TypeSystem StringComparer convention), then lexicographic.
struct OrdinalIgnoreCaseLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

class LoadedAssembly;

// The C# `class AssemblyListSnapshot` (internal).
class AssemblyListSnapshot {
public:
    // The C# `AssemblyListSnapshot(ImmutableArray<LoadedAssembly>
    // assemblies)` -- the port snapshots the pointers (the list owns the
    // instances).
    explicit AssemblyListSnapshot(
        std::vector<LoadedAssembly*> assemblies);
    // The pimpl dtor is defined out-of-line (the LookupCache is complete
    // there; the defaulted-in-class form would need the complete type at
    // every include site). Copies share the lazily-built lookups (the C#
    // snapshot is a reference type).
    ~AssemblyListSnapshot();
    AssemblyListSnapshot(const AssemblyListSnapshot&) = default;
    AssemblyListSnapshot& operator=(const AssemblyListSnapshot&) = default;

    // The C# `ImmutableArray<LoadedAssembly> Assemblies { get; }`.
    const std::vector<LoadedAssembly*>& Assemblies() const
    {
        return assemblies_;
    }

    // The C# `Task<MetadataFile?> TryGetModuleAsync(IAssemblyReference
    // reference, string tfm)`: the exact match by (normalized) tfm +
    // full assembly name; a winrt reference matches by short name.
    const Decompiler::Metadata::MetadataFile* TryGetModule(
        const Decompiler::Metadata::IAssemblyReference& reference,
        const std::string& tfm) const;

    // The C# `Task<MetadataFile?> TryGetSimilarModuleAsync(
    // IAssemblyReference reference)`: the short-name group lookup -- the
    // first candidate whose version covers the reference's, else the
    // last one.
    const Decompiler::Metadata::MetadataFile* TryGetSimilarModule(
        const Decompiler::Metadata::IAssemblyReference& reference) const;

private:
    // One candidate of a short-name group: the C#
    // `(MetadataFile module, Version version)` tuple.
    struct ShortNameEntry {
        const Decompiler::Metadata::MetadataFile* Module = nullptr;
        Decompiler::TypeSystem::Version Version;
    };

    // The lazily-built lookup maps + their guard, heap-allocated so the
    // snapshot stays movable (the C# snapshot is an immutable record; the
    // lookups fill lazily and the LazyInit fields live behind it).
    struct LookupCache;

    // The C# `CreateLoadedAssemblyLookupAsync(bool shortNames)`: the
    // tfm;name maps, built on first demand (the C# LazyInit cache).
    const std::map<std::string, const Decompiler::Metadata::MetadataFile*,
        OrdinalIgnoreCaseLess>&
    Lookup(bool shortNames) const;

    // The C# `CreateLoadedAssemblyShortNameGroupLookupAsync()`.
    const std::map<std::string, std::vector<ShortNameEntry>,
        OrdinalIgnoreCaseLess>&
    ShortNameGroupLookup() const;

    std::vector<LoadedAssembly*> assemblies_;
    mutable std::shared_ptr<LookupCache> cache_;
};

}  // namespace ILSpy::ILSpyX
