// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Implementation of `MergedNamespace` (see MergedNamespace.hpp).

#include "Decompiler/TypeSystem/MergedNamespace.hpp"

#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The lazily-built child-namespace map (convention (c)). A flat list of
// `(name, shared_ptr<MergedNamespace>)` pairs keyed by the child's short name.
// The `StringComparer`-based equality (the compilation's `NameComparer`) is
// applied in the grouping (`GetChildNamespaces`) and the lookup
// (`GetChildNamespace`) via a linear search -- the `StringComparer` is polymorphic
// and the number of child namespaces is small, so a linear search over a flat list
// is the faithful minimal-port substitute for the C# `Dictionary`'s hash-based
// O(1) lookup (the observable behavior is identical).
struct MergedNamespace::ChildMap {
    std::vector<std::pair<std::string, std::shared_ptr<MergedNamespace>>> entries;
};

// The C# root ctor -- binds the compilation, stores the namespaces snapshot and the
// extern alias, and leaves the parent null (the root has no parent). `namespaces`
// is asserted non-empty (the C# delegates `FullName` / `Name` to `namespaces[0]`,
// which throws `IndexOutOfRangeException` on an empty array -- the C# precondition;
// the C++ port asserts the same precondition as a debug check).
MergedNamespace::MergedNamespace(const ICompilation& compilation,
                                 std::vector<const INamespace*> namespaces,
                                 std::string externAlias)
    : externAlias_(std::move(externAlias)),
      compilation_(compilation),
      namespaces_(std::move(namespaces))
{
    assert(!namespaces_.empty());
}

// The C# child ctor -- threads `parentNamespace.Compilation` / `parentNamespace.ExternAlias`
// into the child, stores the parent and the namespaces snapshot. `namespaces` is
// asserted non-empty (the C# precondition).
MergedNamespace::MergedNamespace(const INamespace* parentNamespace,
                                 std::vector<const INamespace*> namespaces)
    : externAlias_(parentNamespace->ExternAlias()),
      compilation_(parentNamespace->Compilation()),
      parentNamespace_(parentNamespace),
      namespaces_(std::move(namespaces))
{
    assert(!namespaces_.empty());
}

MergedNamespace::~MergedNamespace() = default;

// --- ISymbol ---

// The C# `SymbolKind => SymbolKind.Namespace`.
::ILSpy::Decompiler::TypeSystem::SymbolKind MergedNamespace::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
}

// The C# `Name => namespaces[0].Name`.
std::string MergedNamespace::Name() const
{
    return namespaces_[0]->Name();
}

// --- ICompilationProvider ---

// The C# `Compilation => compilation`.
const ICompilation& MergedNamespace::Compilation() const
{
    return compilation_;
}

// --- INamespace ---

// The C# `ExternAlias => externAlias`.
std::string MergedNamespace::ExternAlias() const
{
    return externAlias_;
}

// The C# `FullName => namespaces[0].FullName`.
std::string MergedNamespace::FullName() const
{
    return namespaces_[0]->FullName();
}

// The C# `ParentNamespace => parentNamespace` (null for the root).
const INamespace* MergedNamespace::ParentNamespace() const
{
    return parentNamespace_;
}

// The C# `IEnumerable<ITypeDefinition> Types => namespaces.SelectMany(ns => ns.Types)`
// -- flattens the `Types` of all underlying namespaces into a single non-owning
// `const ITypeDefinition*` snapshot.
std::vector<const ITypeDefinition*> MergedNamespace::Types() const
{
    std::vector<const ITypeDefinition*> result;
    for (const INamespace* ns : namespaces_) {
        auto nsTypes = ns->Types();
        result.insert(result.end(), nsTypes.begin(), nsTypes.end());
    }
    return result;
}

// The C# `IEnumerable<IModule> ContributingModules => namespaces.SelectMany(ns =>
// ns.ContributingModules)` -- flattens the `ContributingModules` of all underlying
// namespaces into a single non-owning `const IModule*` snapshot.
std::vector<const IModule*> MergedNamespace::ContributingModules() const
{
    std::vector<const IModule*> result;
    for (const INamespace* ns : namespaces_) {
        auto nsModules = ns->ContributingModules();
        result.insert(result.end(), nsModules.begin(), nsModules.end());
    }
    return result;
}

// The C# `IEnumerable<INamespace> ChildNamespaces => GetChildNamespaces().Values` --
// the values of the lazily-built child map, as a non-owning `const INamespace*`
// snapshot.
std::vector<const INamespace*> MergedNamespace::ChildNamespaces() const
{
    auto map = GetChildNamespaces();
    std::vector<const INamespace*> result;
    result.reserve(map->entries.size());
    for (const auto& [name, child] : map->entries) {
        result.push_back(child.get());
    }
    return result;
}

// The C# `INamespace GetChildNamespace(string name)` -- `GetChildNamespaces().TryGetValue(name,
// out ns) ? ns : null`. The C++ port does a linear search over the flat child map
// using the compilation's `NameComparer` (convention (c)).
const INamespace* MergedNamespace::GetChildNamespace(const std::string& name) const
{
    auto map = GetChildNamespaces();
    const StringComparer& comparer = compilation_.NameComparer();
    for (const auto& [childName, child] : map->entries) {
        if (comparer.Equals(childName, name)) {
            return child.get();
        }
    }
    return nullptr;
}

// The C# `ITypeDefinition GetTypeDefinition(string name, int typeParameterCount)`:
//   iterate the underlying namespaces, call each one's `GetTypeDefinition`, and
//   return the FIRST `Public` type (the "prefer accessible types" rule), or the
//   last non-null type if none are `Public`. The C++ port mirrors this verbatim.
const ITypeDefinition* MergedNamespace::GetTypeDefinition(const std::string& name,
                                                          int typeParameterCount) const
{
    const ITypeDefinition* anyTypeDef = nullptr;
    for (const INamespace* ns : namespaces_) {
        const ITypeDefinition* typeDef = ns->GetTypeDefinition(name, typeParameterCount);
        if (typeDef != nullptr) {
            if (typeDef->Accessibility() == Accessibility::Public) {
                // Prefer accessible types over non-accessible types.
                return typeDef;
            }
            anyTypeDef = typeDef;
        }
    }
    return anyTypeDef;
}

// The C# `Dictionary<string, INamespace> GetChildNamespaces()`:
//   `var result = LazyInit.VolatileRead(ref this.childNamespaces);`
//   `if (result != null) return result;`
//   `result = new Dictionary<string, INamespace>(compilation.NameComparer);`
//   `foreach (var g in namespaces.SelectMany(ns => ns.ChildNamespaces)`
//   `          .GroupBy(ns => ns.Name, compilation.NameComparer))`
//   `    result.Add(g.Key, new MergedNamespace(this, g.ToArray()));`
//   `return LazyInit.GetOrSet(ref this.childNamespaces, result);`
// The C++ port mirrors the four steps: read the cached `shared_ptr<ChildMap>` via
// `LazyInit::VolatileRead` (an empty `shared_ptr` is the C# `null`), return on a
// hit, else build the merged child map (group the underlying namespaces'
// `ChildNamespaces` by short name using the compilation's `NameComparer`, create a
// `MergedNamespace` child per group), and `LazyInit::GetOrSet` it (first-writer-
// wins). The grouping is a two-phase build (collect the groups into a temporary
// flat list keyed by name, then create one `MergedNamespace` child per group with
// the group's members) because the `MergedNamespace` child's `namespaces_` is fixed
// at construction (it cannot be grown after the fact, so the group must be
// complete before the child is created).
std::shared_ptr<MergedNamespace::ChildMap> MergedNamespace::GetChildNamespaces() const
{
    std::shared_ptr<ChildMap> result = Util::VolatileRead(&childNamespaces_);
    if (result) {
        return result;
    }
    result = std::make_shared<ChildMap>();
    const StringComparer& comparer = compilation_.NameComparer();
    // Phase 1: collect the underlying namespaces' `ChildNamespaces` and group them
    // by short name (using the compilation's `NameComparer`).
    struct Group {
        std::string name;
        std::vector<const INamespace*> members;
    };
    std::vector<Group> groups;
    for (const INamespace* ns : namespaces_) {
        for (const INamespace* child : ns->ChildNamespaces()) {
            const std::string childName = child->Name();
            // Find an existing group with the same name (under the comparer).
            bool found = false;
            for (Group& g : groups) {
                if (comparer.Equals(g.name, childName)) {
                    g.members.push_back(child);
                    found = true;
                    break;
                }
            }
            if (!found) {
                groups.push_back(Group{childName, {child}});
            }
        }
    }
    // Phase 2: create a `MergedNamespace` child per group, keyed by the group's
    // name. The child is owned by the `ChildMap` via the `shared_ptr<MergedNamespace>`
    // value (the C# `Dictionary` GC-owns the children).
    for (Group& g : groups) {
        auto merged = std::make_shared<MergedNamespace>(this, std::move(g.members));
        result->entries.emplace_back(std::move(g.name), std::move(merged));
    }
    return Util::GetOrSet(&childNamespaces_, result);
}

// The C# `override string ToString()`:
//   `string.Format(CultureInfo.InvariantCulture, "[MergedNamespace {0}{1} (from {2} assemblies)]",`
//   `             externAlias != null ? externAlias + "::" : null, this.FullName, this.namespaces.Length)`
// The C++ port mirrors the format: an empty `externAlias_` (the C# `null`) omits the
// `{externAlias}::` prefix; a non-empty one prepends it. `namespaces_.size()` is
// the `Length`.
std::string MergedNamespace::ToString() const
{
    std::string result = "[MergedNamespace ";
    if (!externAlias_.empty()) {
        result += externAlias_;
        result += "::";
    }
    result += FullName();
    result += " (from ";
    result += std::to_string(namespaces_.size());
    result += " assemblies)]";
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem
