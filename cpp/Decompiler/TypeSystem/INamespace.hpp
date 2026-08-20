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

// Port of ICSharpCode.Decompiler/TypeSystem/INamespace.cs -- the `INamespace`
// interface, a resolved namespace. The C# `interface INamespace : ISymbol,
// ICompilationProvider` ports to a C++ abstract base multiply-inheriting those two
// independent abstract bases (the `ISymbol` D372 / `ICompilationProvider` D379
// precedents). A namespace exposes its `ExternAlias` (empty for normal namespaces),
// the dotted `FullName` (e.g. "System.Collections") and short `Name` (e.g.
// "Collections"), the nullable `ParentNamespace` (null for the root namespace), the
// `ChildNamespaces` / `Types` / `ContributingModules` collections, and the
// `GetChildNamespace` / `GetTypeDefinition` lookup helpers. There is no pointer back
// to an unresolved namespace: multiple unresolved namespaces (from different
// assemblies) merge into one `INamespace`.
//
// It is a leaf TypeSystem dependency toward `ICompilation` (which holds the root
// `INamespace`) and toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`): `TypeSystemAstBuilder` walks the namespace
// tree (`ns.FullName` / `ns.Name` / `ns.ParentNamespace` / `ns.ChildNamespaces` /
// `ns.Types`) to emit `namespace` declarations, and `IModule.RootNamespace` (the
// `IModule` leaf after this one) returns an `INamespace`. `ITypeDefinition` (D393, the
// `Types` / `GetTypeDefinition` element type) and `IModule` (not yet ported, the
// `ContributingModules` element type) are FORWARD-DECLARED here: a pointer element
// type is complete with the pointee incomplete, so the snapshot declarations compile
// with both incomplete, and keeping the includes minimal preserves include-graph
// isolation (the D388 forward-declared-already-ported-leaf precedent, here applied to
// `ITypeDefinition` which IS already ported, and to `IModule` which is not).

#pragma once

#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/ICompilationProvider.hpp"

#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the snapshot element types. `ITypeDefinition` (D393) is
// already ported; `IModule` is not yet ported. Both are forward-declared here (not
// included) because the `Types` / `GetTypeDefinition` / `ContributingModules` accessors
// only mention them as pointer element types (`const ITypeDefinition*` /
// `std::vector<const IModule*>`), which are complete with the pointee incomplete, and
// keeping the includes minimal preserves include-graph isolation (the
// `IEntity::GetAttributes` forward-declared-`IAttribute` precedent).
class ITypeDefinition;
class IModule;

// A resolved namespace. A concrete namespace subclasses `INamespace` and overrides
// the inherited `ISymbol::SymbolKind()` / `ISymbol::Name()` (reporting
// `SymbolKind::Namespace` and the short name), the inherited
// `ICompilationProvider::Compilation()` (the parent compilation), and the eight
// `INamespace`-own accessors declared below.
//
// The C# `new string Name` re-hides `ISymbol.Name` (single inheritance -- only
// `ISymbol` declares `Name` here; `ICompilationProvider` does not), so the inherited
// `ISymbol::Name()` virtual covers it with NO C++ redeclaration (the D374 single-
// inheritance "inherited virtual covers the `new`" precedent): a concrete namespace
// overrides `Name()` once and dispatch through `INamespace*` / `ISymbol*` both reach
// it. This is the structural distinction from the `IEntity` (D381) /
// `ITypeParameter` (D383) / `IField` (D391) multiple-inheritance-diamond cases where
// `Name` was inherited via two independent paths and REQUIRED a redeclaration to
// disambiguate lookup; here `Name` lives on only one base, so the inherited virtual
// suffices.
class INamespace : public ISymbol, public ICompilationProvider {
public:
    // The C# `string ExternAlias { get; }` -- the extern alias for this namespace.
    // Empty for normal namespaces.
    virtual std::string ExternAlias() const = 0;

    // The C# `string FullName { get; }` -- the dotted full name (e.g.
    // "System.Collections"). (Also declared by `INamedElement`, but `INamespace`
    // does NOT derive from `INamedElement`, so this is an `INamespace`-own accessor
    // with no collision.)
    virtual std::string FullName() const = 0;

    // The C# `INamespace? ParentNamespace { get; }` -- the parent namespace, or null
    // for the root namespace. Self-referential nullable pointer (the
    // `ITypeDefinition::NestedTypes` self-referential-pointer precedent).
    virtual const INamespace* ParentNamespace() const = 0;

    // The C# `IEnumerable<INamespace> ChildNamespaces { get; }` -- the child
    // namespaces, as a non-owning pointer snapshot (the `IEntity::GetAttributes`
    // `IEnumerable` -> `std::vector<const T*>` by-value precedent). Self-referential.
    virtual std::vector<const INamespace*> ChildNamespaces() const = 0;

    // The C# `IEnumerable<ITypeDefinition> Types { get; }` -- the types in this
    // namespace, as a non-owning `const ITypeDefinition*` snapshot (`ITypeDefinition`
    // forward-declared above).
    virtual std::vector<const ITypeDefinition*> Types() const = 0;

    // The C# `IEnumerable<IModule> ContributingModules { get; }` -- the modules that
    // contribute types to this namespace (or to child namespaces), as a non-owning
    // `const IModule*` snapshot (`IModule` forward-declared above).
    virtual std::vector<const IModule*> ContributingModules() const = 0;

    // The C# `INamespace? GetChildNamespace(string name)` -- a direct child namespace
    // by its short name, or null when not found (uses the compilation's string
    // comparer).
    virtual const INamespace* GetChildNamespace(const std::string& name) const = 0;

    // The C# `ITypeDefinition? GetTypeDefinition(string name, int typeParameterCount)`
    // -- the type with the specified short name and type parameter count, or null
    // when not found (uses the compilation's string comparer).
    virtual const ITypeDefinition* GetTypeDefinition(const std::string& name,
                                                    int typeParameterCount) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
