// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
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

// Port of ICSharpCode.Decompiler/TypeSystem/ICompilation.cs -- the `ICompilation`
// interface, the resolved-compilation capstone of the cyclic
// `ICompilation` <-> `ICompilationProvider` <-> `IModule` / `INamespace` chain.
// A compilation bundles the `MainModule` (the assembly being decompiled) with its
// `Modules` / `ReferencedModules`, the merged `RootNamespace` tree, the extern-alias
// namespace lookup, the `FindType` known-type resolver, the per-language
// `NameComparer`, the per-compilation `CacheManager`, and the `TypeSystemOptions`.
//
// The C# `interface ICompilation` ports to a C++ abstract base with NO base classes
// (it is a root interface, unlike `IEntity` / `IModule` / `INamespace` which derive
// `ISymbol` + `ICompilationProvider`; `ICompilation` is the type
// `ICompilationProvider::Compilation()` returns, so it stands alone). It is a leaf
// TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`): `TypeSystemAstBuilder` reads
// `compilation.MainModule` / `compilation.Modules` / `compilation.RootNamespace` /
// `compilation.NameComparer` to emit the assembly `using` / `namespace` structure.
//
// KEY PORT CONVENTIONS:
//  (a) `IModule MainModule` (non-null) -> `const IModule&` (the non-null-reference
//      convention, the `IVariable::Type()` / `ICompilationProvider::Compilation()`
//      precedent); `IModule` is forward-declared (a reference return to an incomplete
//      type needs only a forward declaration).
//  (b) `IReadOnlyList<IModule> Modules` / `ReferencedModules` ->
//      `std::vector<const IModule*>` by value (non-owning snapshots, the
//      `IEntity::GetAttributes` `IEnumerable` -> `std::vector<const T*>` precedent).
//  (c) `INamespace RootNamespace` (non-null) -> `const INamespace&` (forward-declared);
//      `INamespace? GetNamespaceForExternAlias(string?)` -> `const INamespace*
//      GetNamespaceForExternAlias(const std::string&)` (nullable pointer return, the
//      `IEntity::ParentModule` nullable-pointer precedent).
//  (d) `IType FindType(KnownTypeCode)` (non-null) -> `const IType& FindType(KnownTypeCode)`
//      (the non-null-reference convention; `IType` forward-declared; `KnownTypeCode` is
//      the by-value parameter so its header is included).
//  (e) `StringComparer NameComparer` (non-null) -> `const StringComparer&` (the D398
//      Meyers-singleton `StringComparer::Ordinal()` the concrete `SimpleCompilation`
//      returns; `StringComparer` forward-declared).
//  (f) `CacheManager CacheManager` (non-null) -> `const CacheManager&` (the D397
//      concrete `CacheManager`, in `ILSpy::Decompiler::Util`; forward-declared here --
//      the cross-namespace reference-return-to-incomplete-type precedent, the
//      `ICompilationProvider::Compilation` cross-namespace forward-declaration
//      applied to a `Util`-namespace type). The accessor is NAMED `CacheManager()`
//      after its return type (the `IModule::MetadataFile()` getter-named-after-return-
//      type convention), so the return type is fully qualified
//      `const ILSpy::Decompiler::Util::CacheManager&` to resolve the cross-namespace
//      type (the unqualified `CacheManager` is not in the `TypeSystem` namespace).
//  (g) `TypeSystemOptions TypeSystemOptions` -> `TypeSystemOptions` by value (the D376
//      `[Flags]` enum; the by-value return needs the complete type, so
//      `TypeSystemOptions.hpp` is included).

#pragma once

#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <string>
#include <vector>

namespace ILSpy::Decompiler::Util { class CacheManager; }

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the reference / pointer return and snapshot-element types.
// `IModule` (D396), `INamespace` (D394), `IType` (the D271 minimal port),
// `StringComparer` (D398) are all ported; they are forward-declared here (not
// included) because the `ICompilation`-own accessors only mention them as pointer /
// reference types (`const IModule&`, `const INamespace&`, `const IType&`,
// `const StringComparer&`, `std::vector<const IModule*>`), which are complete with the
// pointee incomplete, and keeping the includes minimal preserves include-graph
// isolation (the `IEntity::GetAttributes` forward-declared-`IAttribute` precedent).
// `KnownTypeCode` (the `FindType` by-value parameter) and `TypeSystemOptions` (the
// `TypeSystemOptions` by-value return) are the two by-value types, so their headers
// are included above. `CacheManager` (D397) lives in `ILSpy::Decompiler::Util` and is
// forward-declared above.
class IModule;
class INamespace;
class IType;
class StringComparer;

// A resolved compilation. A concrete compilation (`SimpleCompilation`, not yet ported)
// subclasses `ICompilation` and overrides the nine accessors below. The compilation is
// the root of the resolved type system: `TypeSystemAstBuilder` / `CSharpResolver` hold
// an `ICompilation` and reach the modules / namespaces / known types / name comparer /
// cache through it.
class ICompilation {
public:
    virtual ~ICompilation() = default;

    // The C# `IModule MainModule { get; }` -- the primary module (the assembly being
    // decompiled). Non-null reference return (the `IVariable::Type()` convention);
    // `IModule` is forward-declared.
    virtual const IModule& MainModule() const = 0;

    // The C# `IReadOnlyList<IModule> Modules { get; }` -- all modules in the
    // compilation (the main module first), as a non-owning `const IModule*` snapshot.
    virtual std::vector<const IModule*> Modules() const = 0;

    // The C# `IReadOnlyList<IModule> ReferencedModules { get; }` -- the referenced
    // modules (excludes the main module), as a non-owning `const IModule*` snapshot.
    virtual std::vector<const IModule*> ReferencedModules() const = 0;

    // The C# `INamespace RootNamespace { get; }` -- the merged root namespace of all
    // assemblies (non-null, the nameless namespace). Non-null `const INamespace&`
    // reference return.
    virtual const INamespace& RootNamespace() const = 0;

    // The C# `INamespace? GetNamespaceForExternAlias(string? alias)` -- the root
    // namespace for a given extern alias, or null when the alias does not exist (null
    // or empty alias yields the global root namespace). Nullable pointer return.
    virtual const INamespace* GetNamespaceForExternAlias(const std::string& alias) const = 0;

    // The C# `IType FindType(KnownTypeCode typeCode)` -- the known type for a
    // `KnownTypeCode` (non-null: the concrete `SimpleCompilation` returns the cached
    // `KnownType` for the code, never null). Non-null `const IType&` reference return;
    // `IType` is forward-declared, `KnownTypeCode` is the by-value parameter.
    virtual const IType& FindType(KnownTypeCode typeCode) const = 0;

    // The C# `StringComparer NameComparer { get; }` -- the name comparer for the
    // language (the string comparer `INamespace.GetTypeDefinition` uses). Non-null
    // `const StringComparer&` reference return (`SimpleCompilation.NameComparer` returns
    // `StringComparer::Ordinal()`).
    virtual const StringComparer& NameComparer() const = 0;

    // The C# `CacheManager CacheManager { get; }` -- the per-compilation shared cache.
    // Non-null `const CacheManager&` reference return; `CacheManager` lives in
    // `ILSpy::Decompiler::Util` (D397), so the return type is cross-namespace-qualified.
    // The accessor is named after its return type (the `IModule::MetadataFile()`
    // convention), so there is no name-hiding clash (the `TypeSystem` namespace has no
    // `CacheManager` type).
    virtual const ILSpy::Decompiler::Util::CacheManager& CacheManager() const = 0;

    // The C# `TypeSystemOptions TypeSystemOptions { get; }` -- the type-system options
    // for this compilation, returned by value (the D376 `[Flags]` enum).
    virtual TypeSystemOptions TypeSystemOptions() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
