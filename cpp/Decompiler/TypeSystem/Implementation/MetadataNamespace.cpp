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

#include "Decompiler/TypeSystem/Implementation/MetadataNamespace.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

using Metadata::NamespaceDefinitionHandle;

// The C# ctor: `Debug.Assert(module != null); Debug.Assert(fullName != null);`
// are compiled out of the release assembly (the Debug.Assert convention);
// `this.Name = module.GetString(ns.Name)` resolves the node's name handle --
// the port's NamespaceDefinition::Name already stores the resolved simple
// name (the iteration-62 GetSimpleName result), so the copy IS the resolution.
MetadataNamespace::MetadataNamespace(const MetadataModule& module,
                                     const INamespace* parentNamespace,
                                     std::string fullName,
                                     const Metadata::NamespaceDefinition& ns)
    : module_(module),
      parentNamespace_(parentNamespace),
      fullName_(std::move(fullName)),
      name_(ns.Name),
      ns_(ns)
{
}

// --- ISymbol ---

::ILSpy::Decompiler::TypeSystem::SymbolKind MetadataNamespace::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
}

std::string MetadataNamespace::Name() const
{
    return name_;
}

// --- ICompilationProvider ---

const ICompilation& MetadataNamespace::Compilation() const
{
    return module_.Compilation();
}

// --- INamespace ---

std::string MetadataNamespace::ExternAlias() const
{
    return std::string();
}

std::string MetadataNamespace::FullName() const
{
    return fullName_;
}

const INamespace* MetadataNamespace::ParentNamespace() const
{
    return parentNamespace_;
}

// The C# `ChildNamespaces`: `LazyInit.VolatileRead` the cached array, and on a
// miss build one `MetadataNamespace` child per `ns.NamespaceDefinitions` handle
// (the full name through `GetNamespaceString`, the node through
// `GetNamespaceDefinition`) and `LazyInit.GetOrSet` it (first-writer-wins). The
// children are OWNED by the cached list (the C# GC roots them through the array);
// each child's `parentNamespace_` back-pointer points back at THIS node.
std::vector<const INamespace*> MetadataNamespace::ChildNamespaces() const
{
    const std::vector<std::shared_ptr<MetadataNamespace>>& children = EnsureChildren();
    std::vector<const INamespace*> result;
    result.reserve(children.size());
    for (const std::shared_ptr<MetadataNamespace>& child : children)
        result.push_back(child.get());
    return result;
}

// The C# `IEnumerable<ITypeDefinition> INamespace.Types`: every
// `ns.TypeDefinitions` token routed through `module.GetDefinition(typeHandle)`,
// the non-null results yielded (the type-definition entity slice constructs
// each definition through the module's cache).
std::vector<const ITypeDefinition*> MetadataNamespace::Types() const
{
    std::vector<const ITypeDefinition*> result;
    result.reserve(ns_.TypeDefinitions.size());
    for (std::uint32_t token : ns_.TypeDefinitions)
    {
        const ITypeDefinition* definition = module_.GetDefinition(token);
        if (definition != nullptr)
            result.push_back(definition);
    }
    return result;
}

// The C# `IEnumerable<IModule> INamespace.ContributingModules =>
// new[] { module }` -- the single contributing module (this namespace is
// per-module; the merged compilation root merges several of these).
std::vector<const IModule*> MetadataNamespace::ContributingModules() const
{
    return std::vector<const IModule*>{ &module_ };
}

// The C# `INamespace.GetChildNamespace(string name)`: the direct child with the
// matching short name (a plain ordinal `==`, not the compilation comparer).
const INamespace* MetadataNamespace::GetChildNamespace(const std::string& name) const
{
    for (const INamespace* child : ChildNamespaces())
    {
        if (child->Name() == name)
            return child;
    }
    return nullptr;
}

// The C# `ITypeDefinition INamespace.GetTypeDefinition(string name, int
// typeParameterCount) => module.GetTypeDefinition(FullName, name,
// typeParameterCount)` -- the TypeSystemExtensions.cs line 717 extension, which
// composes the `TopLevelTypeName` and calls the module's
// `GetTypeDefinition(TopLevelTypeName)`.
const ITypeDefinition* MetadataNamespace::GetTypeDefinition(const std::string& name,
                                                            int typeParameterCount) const
{
    return module_.GetTypeDefinition(
        TopLevelTypeName(fullName_, name, typeParameterCount));
}

// The lazy child build (convention (d)): `VolatileRead` the cache, on a miss
// construct each child over its handle, `GetOrSet` the built list.
const std::vector<std::shared_ptr<MetadataNamespace>>&
MetadataNamespace::EnsureChildren() const
{
    std::shared_ptr<std::vector<std::shared_ptr<MetadataNamespace>>> children =
        Util::VolatileRead(&childNamespaces_);
    if (!children)
    {
        auto built = std::make_shared<std::vector<std::shared_ptr<MetadataNamespace>>>();
        built->reserve(ns_.NamespaceDefinitions.size());
        const Metadata::MetadataFile* file = module_.MetadataFile();
        for (const NamespaceDefinitionHandle& handle : ns_.NamespaceDefinitions)
        {
            std::string fullName = file->GetNamespaceString(handle);
            built->push_back(std::make_shared<MetadataNamespace>(
                module_, this, std::move(fullName),
                file->GetNamespaceDefinition(handle)));
        }
        children = Util::GetOrSet(&childNamespaces_, std::move(built));
    }
    return *children;
}

} // namespace Implementation
