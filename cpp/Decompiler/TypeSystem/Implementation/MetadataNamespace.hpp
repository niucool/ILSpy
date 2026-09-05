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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataNamespace.cs --
// the `sealed class MetadataNamespace : INamespace`, the per-module namespace node
// the `MetadataModule` (the sibling skeleton in TypeSystem/MetadataModule.hpp)
// builds its `RootNamespace` tree from. Each node wraps one frozen
// `ILSpy::Decompiler::Metadata::NamespaceDefinition` (the iteration-62 SRM
// namespace-tree node): `FullName` is the ctor parameter, `Name` the node's
// resolved last segment (`module.GetString(ns.Name)` in the C# -- the port's
// NamespaceDefinition::Name already stores the resolved simple name), and the
// children are built lazily from `ns.NamespaceDefinitions` (the SRM tree's
// insertion order) through the MetadataFile surface
// (`GetNamespaceString` / `GetNamespaceDefinition`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class MetadataNamespace : INamespace` ports to a C++ `final`
//      class deriving `INamespace`. The C# `public string Name { get; }` implicitly
//      implements `ISymbol.Name` (single inheritance -- only `ISymbol` declares
//      `Name`), so one `Name()` override covers both dispatch paths (the D394
//      single-ISymbol-subobject convention).
//  (b) The C# `readonly MetadataModule module` field (the CONCRETE module type)
//      ports to a `const MetadataModule&` reference member -- `MetadataModule` is
//      forward-declared in this header (the reference member is complete with the
//      pointee incomplete, the `MergedNamespace::compilation_` precedent), and this
//      header is included by `MetadataModule.cpp` where the module is complete.
//      The `module.GetDefinition(typeHandle)` / `module.GetTypeDefinition(...)`
//      calls the namespace routes through need the concrete module member surface
//      (`GetDefinition` is a MetadataModule-own member, not an `IModule` virtual),
//      which is why the field keeps the concrete type rather than `const IModule&`.
//  (c) The C# `readonly NamespaceDefinition ns` (the SRM struct, a thin wrapper
//      over the frozen NamespaceData) ports to a BY-VALUE
//      `ILSpy::Decompiler::Metadata::NamespaceDefinition` member: the port's node
//      is a plain value type (resolved strings + handle/token vectors), so the
//      copy duplicates the node's payload once at namespace construction (the C#
//      copies the struct handle; both stay alive as long as they are read -- the
//      port's cache hangs off the MetadataFile, which the module contract keeps
//      alive behind the module).
//  (d) The C# `INamespace[] childNamespaces` lazy cache (`LazyInit.VolatileRead` /
//      `GetOrSet` over the children array) ports to a
//      `mutable std::shared_ptr<std::vector<std::shared_ptr<MetadataNamespace>>>`
//      touched only through the `Util::LazyInit` helpers (the `MergedNamespace`
//      ChildMap precedent, here over the owning child list): the first
//      `ChildNamespaces` / `GetChildNamespace` call builds every child once (each
//      child resolving its own full name + node through the MetadataFile surface),
//      and later calls return the same child instances (the C# reference-identity
//      of the cached array -- pinned by the repeated-call identity test). `mutable`
//      so the const accessors can lazily write the cache (the const-lazy-write
//      precedent).
//  (e) The C# `string INamespace.ExternAlias => string.Empty` ports to
//      `return std::string()` (an empty string; MetadataNamespace never carries an
//      extern alias -- only the deferred MergedNamespace forms do).
//  (f) `Types` routes each `ns.TypeDefinitions` token through
//      `module.GetDefinition(token)` -- the type-definition entity slice landed,
//      so every token resolves to the real `MetadataTypeDefinition` (a namespace
//      with direct types enumerates them; the SRM-synthesized virtual
//      intermediates like mscorlib's "Microsoft" or "Windows" carry no direct
//      types and yield the empty snapshot -- the gold-pinned virtual-namespace
//      arm). The `if (def != null)` filter is the C# shape.
//  (g) `GetTypeDefinition(name, typeParameterCount)` routes through the extension
//      `module.GetTypeDefinition(FullName, name, tpc)` -- literally
//      `module.GetTypeDefinition(new TopLevelTypeName(FullName, name, tpc))`
//      (TypeSystemExtensions.cs line 717), so the port composes the
//      `TopLevelTypeName` and calls the `IModule::GetTypeDefinition` virtual
//      through the concrete module reference.

#pragma once

#include "Decompiler/Metadata/NamespaceDefinition.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration: the concrete module the namespace routes through (the
// sibling skeleton in TypeSystem/MetadataModule.hpp). The reference member below
// is complete with the pointee incomplete.
class MetadataModule;

namespace Implementation {

// A per-module namespace node. Constructed by `MetadataModule` (the root, over
// the tree's root node) and by this class's own lazy child build (each child,
// over the child's node). Not constructible outside the module surface in the
// C# (the ctor is implicit); the port keeps the ctor public so the tests can pin
// the node surface directly (the reflection-probe-equivalent access divergence).
class MetadataNamespace final : public INamespace {
public:
    // The C# `MetadataNamespace(MetadataModule module, INamespace parent,
    // string fullName, NamespaceDefinition ns)` -- `parent` is null for the root.
    // The node copy is taken by value (convention (c)); `ns.Name` is resolved
    // into `name_` at construction (the C# `this.Name = module.GetString(ns.Name)`
    // -- the port's node Name is the already-resolved simple name).
    MetadataNamespace(const MetadataModule& module, const INamespace* parentNamespace,
                      std::string fullName, const Metadata::NamespaceDefinition& ns);

    // --- ISymbol ---
    // `SymbolKind` return type GLOBALLY QUALIFIED (the D372 name-hiding crux: the
    // accessor name hides the namespace-scope `SymbolKind` enum for the rest of
    // the class body).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override;

    // --- INamespace ---
    std::string ExternAlias() const override;
    std::string FullName() const override;
    const INamespace* ParentNamespace() const override;
    std::vector<const INamespace*> ChildNamespaces() const override;
    std::vector<const ITypeDefinition*> Types() const override;
    std::vector<const IModule*> ContributingModules() const override;
    const INamespace* GetChildNamespace(const std::string& name) const override;
    const ITypeDefinition* GetTypeDefinition(const std::string& name,
                                              int typeParameterCount) const override;

private:
    // The lazily-built child list (convention (d)): builds every child once on
    // the first call and caches it; the shared_ptr is the atomic storage the
    // `Util::LazyInit` helpers read/write (the `MergedNamespace::GetChildNamespaces`
    // precedent).
    const std::vector<std::shared_ptr<MetadataNamespace>>& EnsureChildren() const;

    const MetadataModule& module_;
    const INamespace* parentNamespace_;
    std::string fullName_;
    std::string name_;
    Metadata::NamespaceDefinition ns_;
    mutable std::shared_ptr<std::vector<std::shared_ptr<MetadataNamespace>>> childNamespaces_;
};

} // namespace Implementation
} // namespace ILSpy::Decompiler::TypeSystem
