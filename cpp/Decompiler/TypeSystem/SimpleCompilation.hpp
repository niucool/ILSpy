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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.TypeSystem.Implementation.SimpleCompilation`
// (TypeSystem/Implementation/SimpleCompilation.cs) -- the concrete `ICompilation`
// (D399). A `SimpleCompilation` resolves a main module plus its referenced modules
// (via `IModuleReference.Resolve` against a `SimpleTypeResolveContext` over `*this`),
// exposes them as `Modules` (the main first) / `ReferencedModules` (the referenced
// only), builds a merged `RootNamespace` (a `MergedNamespace` over the root
// namespaces of the main module and the referenced modules, lazily via `LazyInit`),
// forwards `FindType` to a per-compilation `KnownTypeCache`, and returns the
// `StringComparer::Ordinal()` name comparer, the per-compilation `CacheManager`, and
// the `TypeSystemOptions::Default` options.
//
// It is a leaf TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker of `CSharpAmbience`): `TypeSystemAstBuilder` reads
// `compilation.MainModule` / `compilation.Modules` / `compilation.RootNamespace` /
// `compilation.NameComparer` to emit the assembly `using` / `namespace` structure.
// All its deps are now ported: `ICompilation` (D399, the base), `IModule` (D396),
// `INamespace` (D394), `IType` / `KnownTypeCode` (the D271 minimal port),
// `StringComparer` (D398), `CacheManager` (D397), `TypeSystemOptions` (D376),
// `IModuleReference` (D418), `SimpleTypeResolveContext` (D410), `KnownTypeCache`
// (D421), `MergedNamespace` (D422), and `LazyInit` (D420).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly CacheManager cacheManager = new CacheManager();` (a field
//      initializer) ports to a `CacheManager` by-value member default-constructed
//      (the D397 concrete `CacheManager`, in `ILSpy::Decompiler::Util`). The header
//      includes `CacheManager.hpp` so the by-value member is a complete type.
//  (b) The C# `KnownTypeCache knownTypeCache` field (assigned `new KnownTypeCache(this)`
//      in `Init`) ports to a `KnownTypeCache` by-value member bound to `*this` in
//      EVERY ctor's member-init list (`knownTypeCache_(*this)`). The `KnownTypeCache`
//      ctor only stores the `const ICompilation&` reference (the cache slots start
//      empty), so binding it at construction (rather than in `Init`) is behaviorally
//      equivalent to the C# `Init` assignment: the slots start empty either way, and
//      the only divergence is the impl-detail timing of the `const ICompilation&`
//      binding (the C# leaves it `null` until `Init`; the C++ binds it at
//      construction -- but `FindType` is never read before `Init` sets `initialized_`
//      in valid usage, and the cache slots start empty). A by-value member requires
//      the complete type, so `KnownTypeCache.hpp` is included. A reference member
//      (`const ICompilation&` inside `KnownTypeCache`) cannot be re-assigned, so the
//      cache is bound ONCE at construction (calling `Init` twice is not a supported
//      usage -- the C# `Init` is meant to be called once).
//  (c) The C# `IModule mainModule` / `IReadOnlyList<IModule> assemblies` /
//      `referencedAssemblies` (non-null reference-type fields) port to a non-owning
//      `const IModule*` (`mainModule_`) and two `std::vector<const IModule*>`
//      snapshots (`assemblies_` / `referencedAssemblies_`). The resolved modules are
//      NON-OWNING: in the C# they are GC-owned; in the C++ the caller (the test, or
//      the future `CecilLoader` integration) owns them and they must outlive the
//      compilation. This is the non-owning-snapshot convention the rest of the
//      TypeSystem surface uses (`IEntity::GetAttributes`, `IModule` snapshots); the
//      `IModuleReference::Resolve` (D418) returns `const IModule*` (nullable raw
//      pointer, non-owning), so the compilation stores the resolved pointers without
//      taking ownership.
//  (d) The C# `bool initialized` gate (the `MainModule` / `Modules` /
//      `ReferencedModules` getters throw `InvalidOperationException` when `!initialized`)
//      ports to a `bool initialized_` member (default `false`, set `true` at the end
//      of `Init`); the three getters throw `std::runtime_error` (the C#
//      `InvalidOperationException` counterpart, a runtime operational error) when
//      `!initialized_`. The public ctors call `Init` in the body, so a
//      publicly-constructed `SimpleCompilation` is initialized before it is usable;
//      the `protected` default ctor (for subclasses) leaves it uninitialized until
//      the subclass calls `Init`, so the gate is load-bearing for the subclass path.
//  (e) The C# `INamespace rootNamespace` (lazily built via
//      `LazyInit.VolatileRead` / `GetOrSet`) ports to a `mutable std::shared_ptr<INamespace>`
//      (`rootNamespace_`) touched ONLY through the `LazyInit` helpers (D420,
//      first-writer-wins) so the plain `shared_ptr` is the atomic storage. The
//      `MergedNamespace` built by `CreateRootNamespace` is OWNED by the compilation
//      via the `shared_ptr` (the C# GC owns it). `mutable` so the const `RootNamespace`
//      can lazily write it (the D416 / D422 const-lazy-write precedent).
//  (f) `CreateRootNamespace()` is `protected virtual` and `const`: it builds a
//      `MergedNamespace` over the root namespaces of the main module and the
//      referenced modules (the C# `INamespace[] namespaces` array: `mainModule.RootNamespace`
//      first, then each referenced module's `RootNamespace`). It is `const` because it
//      is a factory that reads `mainModule_` / `referencedAssemblies_` without
//      modifying the compilation (the C# instance method is non-const by C#-has-no-const
//      default; the C++ port makes it `const` for const-correctness so the const
//      `RootNamespace` can call it -- a documented C++-vs-C# const addition). A
//      subclass overrides it to change the global namespace (the C# `SimpleCompilation`
//      does not support extern aliases, but derived classes might).
//  (g) The C# `string.IsNullOrEmpty(alias)` test in `GetNamespaceForExternAlias`
//      (empty / null alias yields the global root namespace) ports to
//      `alias.empty()` (a `std::string` has no null state, so empty == the C# null-or-
//      empty); unknown aliases return `nullptr` (the C# `null`). `virtual` so a
//      subclass can support extern aliases (the C# is `virtual`).
//  (h) The C# `NameComparer => StringComparer.Ordinal` ports to
//      `StringComparer::Ordinal()` (the D398 Meyers singleton, returned by `const
//      StringComparer&`). The C# `virtual TypeSystemOptions TypeSystemOptions =>
//      TypeSystemOptions.Default` ports to `TypeSystemOptions::Default` (the D376
//      `[Flags]` enum composite); the accessor's return type is GLOBALLY QUALIFIED
//      (`::ILSpy::Decompiler::TypeSystem::TypeSystemOptions`) because the inherited
//      `ICompilation::TypeSystemOptions()` member function hides the namespace-scope
//      `TypeSystemOptions` enum in the class body (the D372 / D421 cross-scope
//      name-hiding crux), and the body value `TypeSystemOptions::Default` likewise
//      needs the full qualification.
//  (i) `ToString()` is a NON-VIRTUAL accessor (the D419 `DefaultAssemblyReference` /
//      D422 `MergedNamespace` convention -- `ICompilation` / its bases declare no
//      `ToString` virtual, so the C# `override string ToString()` has no C++ virtual
//      counterpart and ports as a non-virtual member), mirroring the C#
//      `"[" + GetType().Name + " " + mainModule.AssemblyName + "]"` format
//      (`GetType().Name` for `SimpleCompilation` is the literal `"SimpleCompilation"`).

#pragma once

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/KnownTypeCache.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IModule` (the D396 port, `MainModule` return /
// `Modules` element -- a reference / pointer return and a pointer vector element
// are complete with the pointee incomplete), `INamespace` (the D394 port, the
// `RootNamespace` / `GetNamespaceForExternAlias` return and the `rootNamespace_`
// `shared_ptr` member -- a `shared_ptr` and a reference / pointer are complete with
// the pointee incomplete), `IModuleReference` (the D418 port, the ctor / `Init`
// parameter -- a reference / pointer parameter is complete with the pointee
// incomplete), `IType` (the D271 minimal port, the `FindType` return -- a reference
// return is complete with the pointee incomplete), `StringComparer` (the D398
// port, the `NameComparer` return -- a reference return is complete with the
// pointee incomplete). The `Init` / `CreateRootNamespace` / accessor bodies are
// out-of-line in the `.cpp` where the complete-type headers are included.
class IModule;
class INamespace;
class IModuleReference;
class IType;
class StringComparer;

// The concrete `ICompilation` -- resolves a main module plus its referenced modules,
// exposes them as `Modules` / `ReferencedModules`, builds a merged `RootNamespace`,
// forwards `FindType` to a `KnownTypeCache`, and returns the `StringComparer::Ordinal`
// name comparer, the `CacheManager`, and the `TypeSystemOptions::Default` options.
// The class is NOT `final` (the C# `SimpleCompilation` is unsealed: `MinimalCorlib`,
// `LazyCxc` and other derived compilations subclass it and override
// `CreateRootNamespace` / `GetNamespaceForExternAlias` / `TypeSystemOptions`).
class SimpleCompilation : public ICompilation {
public:
    // The C# `SimpleCompilation(IModuleReference mainAssembly, params IModuleReference[]
    // assemblyReferences)` / `SimpleCompilation(IModuleReference mainAssembly,
    // IEnumerable<IModuleReference> assemblyReferences)` -- the public ctor: resolves
    // the main module and the referenced modules via `Init`. The C# `params` array and
    // `IEnumerable` forms collapse to a single C++ ctor taking a `std::vector` (the
    // `IEnumerable` form). The C# `ArgumentNullException` on a null `mainAssembly` /
    // `assemblyReferences` is N/A in C++ (a reference parameter is non-null by C++
    // semantics; the vector is non-null). `knownTypeCache_` is bound to `*this` in the
    // member-init list (convention (b)); then `Init` runs in the body.
    SimpleCompilation(const IModuleReference& mainAssembly,
                      std::vector<const IModuleReference*> assemblyReferences);

    // The C# `protected SimpleCompilation()` -- the protected default ctor for
    // subclasses: does NOT call `Init` (the compilation is uninitialized until the
    // subclass calls `Init`). `knownTypeCache_` is bound to `*this` in the member-init
    // list (convention (b)) -- harmless before `Init` since the cache slots start
    // empty and `FindType` is not read before `initialized_` is set.
protected:
    SimpleCompilation();

    // The C# `protected void Init(IModuleReference mainAssembly,
    // IEnumerable<IModuleReference> assemblyReferences)` -- resolves the main module
    // and the referenced modules against a `SimpleTypeResolveContext(*this)`, dedups
    // them by reference equality, populates `assemblies_` (the main first, then the
    // unique resolved refs) / `referencedAssemblies_` (the unique resolved refs), and
    // sets `initialized_`. Out-of-line in the `.cpp` (needs `SimpleTypeResolveContext`
    // / `IModuleReference` / `IModule` complete). Protected so a subclass (using the
    // default ctor) can call it.
    void Init(const IModuleReference& mainAssembly,
              std::vector<const IModuleReference*> assemblyReferences);

    // The C# `protected virtual INamespace CreateRootNamespace()` -- builds the merged
    // root namespace: a `MergedNamespace` over the root namespaces of the main module
    // (first) and the referenced modules. `const` (convention (f)). Out-of-line (needs
    // `MergedNamespace` / `IModule` / `INamespace` complete). Returns a
    // `shared_ptr<INamespace>` so the compilation can own it via the `rootNamespace_`
    // cache (the C# returns a GC-owned `INamespace`).
    virtual std::shared_ptr<INamespace> CreateRootNamespace() const;

public:
    // --- ICompilation ---
    const IModule& MainModule() const override;
    std::vector<const IModule*> Modules() const override;
    std::vector<const IModule*> ReferencedModules() const override;
    const INamespace& RootNamespace() const override;
    const INamespace* GetNamespaceForExternAlias(const std::string& alias) const override;
    const IType& FindType(KnownTypeCode typeCode) const override;
    const StringComparer& NameComparer() const override;
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override;
    // Return type GLOBALLY QUALIFIED (convention (h), the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override;

    // The C# `override string ToString()` -- a non-virtual diagnostic accessor
    // (convention (i)). Out-of-line (reads `mainModule_->AssemblyName()`, needs
    // `IModule` complete).
    std::string ToString() const;

protected:
    // The per-compilation shared cache (convention (a)). Default-constructed.
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    // The resolved main module (non-owning, convention (c)). Null before `Init`.
    const IModule* mainModule_ = nullptr;
    // The per-compilation known-type cache (convention (b)). Bound to `*this` at
    // construction (in every ctor's member-init list).
    KnownTypeCache knownTypeCache_;
    // The resolved modules (the main first, then the unique referenced), non-owning
    // (convention (c)). Empty before `Init`.
    std::vector<const IModule*> assemblies_;
    // The unique referenced modules (excludes the main unless a ref resolves to it),
    // non-owning (convention (c)). Empty before `Init`.
    std::vector<const IModule*> referencedAssemblies_;
    // The `initialized` gate (convention (d)). False until `Init` completes.
    bool initialized_ = false;
    // The lazily-built merged root namespace (convention (e)). `mutable` so the const
    // `RootNamespace` can lazily write it via `LazyInit`.
    mutable std::shared_ptr<INamespace> rootNamespace_;
};

} // namespace ILSpy::Decompiler::TypeSystem
