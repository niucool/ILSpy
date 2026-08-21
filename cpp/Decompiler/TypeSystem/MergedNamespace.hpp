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

// Port of `ICSharpCode.Decompiler.TypeSystem.Implementation.MergedNamespace`
// (TypeSystem/Implementation/MergedNamespace.cs) -- a merged namespace. A
// `MergedNamespace` presents multiple per-module `INamespace`s (the same namespace
// from different assemblies) as a single `INamespace`: it delegates `Name` /
// `FullName` to the first underlying namespace, flattens `Types` /
// `ContributingModules` across them, merges their `ChildNamespaces` into merged
// grandchildren (grouped by short name using the compilation's `NameComparer`), and
// resolves `GetTypeDefinition` by preferring the first `Public` type over
// non-accessible types (the C# "prefer accessible types" rule).
//
// It is a leaf TypeSystem dependency toward `SimpleCompilation` (the concrete
// `ICompilation`, not yet ported, whose `RootNamespace` is a `MergedNamespace`
// over the root namespaces of its `Modules`) and onward to
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`). All its deps are now ported: `INamespace` (D394, the base),
// `ICompilation` (D399, `NameComparer` for the child grouping), `ITypeDefinition`
// (D393, the `Types` / `GetTypeDefinition` element), `IModule` (D396, the
// `ContributingModules` element), `StringComparer` (D398, the child-name grouping
// comparer), and `LazyInit` (D420, the first-writer-wins lazy build of the child
// map).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class MergedNamespace : INamespace` ports to a C++ `final`
//      class deriving from the real `INamespace` (D394), overriding the inherited
//      `ISymbol::SymbolKind()` (reporting `SymbolKind::Namespace`) and
//      `ISymbol::Name()` (the short name, delegated to `namespaces_[0]`), the
//      inherited `ICompilationProvider::Compilation()`, and the eight
//      `INamespace`-own accessors. The `SymbolKind()` return type is GLOBALLY
//      QUALIFIED because the inherited `ISymbol::SymbolKind()` member function
//      hides the namespace-scope `SymbolKind` enum in the class body (the D372 /
//      D391 cross-scope name-hiding crux); the `Name()` return type (`std::string`)
//      and the other accessors' return types are not shadowed (no same-named
//      inherited member function / no same-named type collision).
//  (b) The C# `INamespace[] namespaces` (an array of the individual namespaces
//      being merged) ports to a `std::vector<const INamespace*>` -- a non-owning
//      snapshot of the per-module namespaces (the modules own them for the
//      compilation's lifetime). The C# `namespaces[0].FullName` / `namespaces[0].Name`
//      delegate to the first element; the C++ port mirrors this (an empty
//      `namespaces_` is a C# precondition violation -- the ctors assert non-empty
//      rather than guarding, faithful to the C# which throws `IndexOutOfRangeException`
//      on `namespaces[0]` for an empty array).
//  (c) The C# `Dictionary<string, INamespace> childNamespaces` (a lazily-built
//      child map keyed by the child's short name, using the compilation's
//      `NameComparer`) ports to a `std::shared_ptr<ChildMap>` where `ChildMap` is a
//      nested struct holding a flat list of `(name, shared_ptr<MergedNamespace>)`
//      pairs. The map is built on first use via `LazyInit` (D420, first-writer-wins)
//      and the `shared_ptr` field is `mutable` so the const `GetChildNamespace` /
//      `ChildNamespaces` can lazily write it. The `StringComparer`-based equality
//      (the compilation's `NameComparer`) is preserved in the grouping and the
//      lookup (a linear search, since the `StringComparer` is polymorphic and the
//      number of child namespaces is small -- a documented deviation from the C#
//      `Dictionary`'s hash-based O(1) lookup; the observable behavior is
//      identical). The `MergedNamespace` children are OWNED by the `ChildMap`
//      (held via `shared_ptr<MergedNamespace>` values), mirroring the C# where the
//      `Dictionary` GC-owns the children.
//  (d) The C# child ctor `MergedNamespace(INamespace parentNamespace, INamespace[]
//      namespaces)` threads `parentNamespace.Compilation` / `parentNamespace.ExternAlias`
//      into the child's `compilation` / `externAlias`; the C++ port binds the
//      `const ICompilation&` member to `parentNamespace->Compilation()` and copies
//      `parentNamespace->ExternAlias()`. The `parentNamespace_` is a raw
//      `const INamespace*` (the C# nullable `INamespace?`, null for the root
//      namespace); the root ctor sets it to `nullptr` (the root has no parent).
//  (e) `ToString()` is a NON-VIRTUAL accessor (the D419 `DefaultAssemblyReference`
//      convention -- `INamespace` / its bases declare no `ToString` virtual, so the
//      C# `override string ToString()` has no C++ virtual counterpart and ports as a
//      non-virtual member), mirroring the C# `string.Format` diagnostic format.

#pragma once

#include "Decompiler/TypeSystem/INamespace.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration: the compilation whose `NameComparer` the child grouping
// uses (D399, already ported). A reference member (`const ICompilation&`) is
// complete with the pointee incomplete.
class ICompilation;

// A merged namespace -- presents multiple per-module `INamespace`s (the same
// namespace from different assemblies) as a single `INamespace`.
class MergedNamespace final : public INamespace {
public:
    // The C# root ctor `MergedNamespace(ICompilation compilation, INamespace[]
    // namespaces, string externAlias = null)` -- creates a merged ROOT namespace
    // (no parent). `compilation` is the parent compilation; `namespaces` are the
    // individual per-module namespaces being merged (non-empty -- the C# delegates
    // `FullName` / `Name` to `namespaces[0]`); `externAlias` is the extern alias
    // (empty for a normal namespace).
    MergedNamespace(const ICompilation& compilation,
                    std::vector<const INamespace*> namespaces,
                    std::string externAlias = {});

    // The C# child ctor `MergedNamespace(INamespace parentNamespace, INamespace[]
    // namespaces)` -- creates a merged CHILD namespace. `compilation` /
    // `externAlias` are threaded from the parent; `parentNamespace` is the parent
    // merged namespace; `namespaces` are the individual per-module child namespaces
    // being merged.
    MergedNamespace(const INamespace* parentNamespace,
                    std::vector<const INamespace*> namespaces);

    // Out-of-line destructor: the `shared_ptr<ChildMap>` member's deleter needs
    // `ChildMap` complete (the PImpl-with-`shared_ptr` best practice, even though
    // `shared_ptr` works with incomplete types at the destructor).
    ~MergedNamespace();

    // --- ISymbol ---
    // `SymbolKind` return type GLOBALLY QUALIFIED (convention (a), the D372 name-
    // hiding crux).
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

    // The C# `override string ToString()` -- a non-virtual diagnostic accessor
    // (convention (e)).
    std::string ToString() const;

private:
    // The lazily-built child-namespace map (convention (c)). A nested struct
    // (forward-declared here, defined in the `.cpp` where `MergedNamespace` is
    // complete) holding the flat list of `(name, shared_ptr<MergedNamespace>)` pairs.
    // The `shared_ptr<ChildMap>` member is valid with `ChildMap` incomplete
    // (`shared_ptr` works with incomplete types); the out-of-line destructor ensures
    // the deleter sees `ChildMap` complete.
    struct ChildMap;

    // Lazily builds and returns the child-namespace map (convention (c)): reads the
    // cached `shared_ptr<ChildMap>` via `LazyInit::VolatileRead`, and on a miss
    // groups the merged namespaces' `ChildNamespaces` by short name (using the
    // compilation's `NameComparer`), creates a `MergedNamespace` child per group,
    // and `LazyInit::GetOrSet`s the result (first-writer-wins). `const` with a
    // `mutable` field so the const `GetChildNamespace` / `ChildNamespaces` can
    // lazily write the cache.
    std::shared_ptr<ChildMap> GetChildNamespaces() const;

    std::string externAlias_;
    const ICompilation& compilation_;
    const INamespace* parentNamespace_ = nullptr;
    std::vector<const INamespace*> namespaces_;
    mutable std::shared_ptr<ChildMap> childNamespaces_;
};

} // namespace ILSpy::Decompiler::TypeSystem
