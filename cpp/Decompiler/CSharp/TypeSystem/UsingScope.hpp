// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/TypeSystem/UsingScope.cs -- a scope that
// contains "using" statements: either the module (compilation) itself, or a
// namespace declaration. The resolver creates one UsingScope per namespace
// declaration level (chained through the parent context's CurrentUsingScope) and
// memoizes per-identifier name-resolution results in the ResolveCache the
// CSharpResolver.ResolveSimpleName arm consults (the `scope.ResolveCache.TryGetValue
// / TryAdd` pair), plus the lazily-built extension-method group list
// (`LazyInit.VolatileRead` / `GetOrSet` over `AllExtensionMethods`) the resolver's
// `GetAllExtensionMethods` populates. The class is the direct prerequisite pair
// member of `CSharpTypeResolveContext` (the other half of the
// CSharp/TypeSystem pair the `CSharpResolver` skeleton holds): the context holds a
// UsingScope in its `CurrentUsingScope` slot, and every UsingScope holds the
// `CSharpTypeResolveContext` it was created against (the `Parent` chain reads the
// parent context's `CurrentUsingScope`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public class UsingScope` is unsealed -> the C++ class is NOT `final`
//      (no derivation is currently planned, but the C# shape is preserved).
//  (b) The C# `readonly CSharpTypeResolveContext parentContext` ctor-guarded
//      reference ports to an owning `std::shared_ptr<CSharpTypeResolveContext>`: the
//      context is a heap-allocated immutable object shared by the resolver and by
//      every scope created against it (the `CSharpTypeResolveContext::WithUsingScope`
//      factory allocates a fresh context per call, so the C#-reachable construction
//      orders never form an ownership cycle: a scope's parentContext always points
//      at a context whose `CurrentUsingScope` is the scope's PARENT scope, never at
//      a context holding the scope itself). The C# `context ?? throw
//      ArgumentNullException` guard ports to a null `shared_ptr` check throwing
//      `std::invalid_argument` (the DefaultParameter null-type convention; a
//      `shared_ptr` parameter CAN be null, so the guard is meaningful).
//  (c) The C# `INamespace Namespace` ctor-guarded reference ports to a non-owning
//      `const INamespace*` (the compilation owns real namespaces -- the
//      `SimpleCompilation::rootNamespace_` shared ownership -- while the scope only
//      references one). The internally-created `DummyNamespace` (the
//      `WithNestedNamespace` fallback for a namespace declaration that has no
//      corresponding metadata namespace) is an exception the scope itself must keep
//      alive: the port adds the private `ownedNamespace_` `shared_ptr` slot (set only
//      by the private ctor the `WithNestedNamespace` path takes) -- the "own what you
//      create, borrow what the type system owns" convention (the
//      `MemberListWithDeclaringType` DeclaringType/Methods split). The C# null guard
//      on the ctor parameter is structurally unreachable through the `const
//      INamespace&` reference parameter (the D374 non-null-reference convention) and
//      is documented rather than thrown.
//  (d) The C# `ImmutableArray<INamespace> Usings` ports to a `std::vector<const
//      INamespace*>` snapshot (non-owning; the compilation owns the namespaces).
//  (e) The C# `internal` members port to PUBLIC members (the internal->public
//      convention): `ResolveCache` is a public data member of the nested
//      `ResolveCacheMap` type (a mutex-guarded `unordered_map` exposing the two
//      `ConcurrentDictionary` operations the resolver uses -- `TryGetValue` /
//      `TryAdd` -- the CacheManager locked-map convention; the map type is
//      non-copyable/non-movable, so the C# `readonly` field cannot be reassigned,
//      only mutated through its operations), and `AllExtensionMethods` is a public
//      nullable `std::shared_ptr` data member (the `LazyInit` `VolatileRead` /
//      `GetOrSet` contract: the field must be touched only through the two helpers,
//      so it must be a plain addressable member).
//  (f) `WithNestedNamespace` needs an owning `std::shared_ptr` handle to `*this` for
//      the `parentContext.WithUsingScope(this)` call, so the class derives
//      `std::enable_shared_from_this<UsingScope>` (the IType D406 convention): the
//      const `shared_from_this()` yields a `shared_ptr<const UsingScope>` which the
//      implementation `const_pointer_cast`s back to the mutable handle (the
//      underlying object is mutable; the const is the accessor contract, the D515
//      precedent). Every C#-reachable construction site creates the scope
//      heap-allocated (the GC reference semantics), so `shared_from_this` is always
//      valid in practice.
//  (g) The `Parent` property reads the PARENT CONTEXT's `CurrentUsingScope` (not a
//      stored parent pointer): a scope created directly against a context whose
//      `CurrentUsingScope` slot is null reports `Parent == null`, while the
//      `WithNestedNamespace` child (created against `parentContext.WithUsingScope
//      (thisScope)`) reports the enclosing scope -- the `Parent` chain is the
//      context chain, not a sibling-pointer chain. The C# nullable `UsingScope`
//      return ports to a nullable `std::shared_ptr<UsingScope>`.

#pragma once

#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (the ResolveCache value)

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward declarations of the interface types the UsingScope surface references (the
// Namespace slot via reference/pointer, the AllExtensionMethods group element via pointer
// -- neither needs a complete type in this header; the .cpp includes the full interfaces
// for the DummyNamespace overrides and the GetChildNamespace call). The qualified block
// lives at GLOBAL scope: a `namespace A::B { }` reopening written INSIDE another
// namespace is resolved by MSVC against the enclosing namespace's own members (it
// declares a fresh `A::B` chain under the enclosing namespace, shadowing the global
// `A` for every later qualified reference in the file) -- the
// CSharpConversions.hpp global-scope forward-declaration convention.
namespace ILSpy::Decompiler::TypeSystem {
class INamespace;
class IMethod;
}

namespace ILSpy::Decompiler::CSharp::TypeSystem {

class CSharpTypeResolveContext;  // the parent context (fwd: held via shared_ptr, defined below)

// The `TS` alias for the sibling TypeSystem namespace (the SIBLING namespace is not
// searched from inside this one, so every reference below goes through the alias).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The C# `public class UsingScope` -- a scope that contains "using" statements (the
// compilation root, or a namespace declaration). Unsealed (not `final`).
class UsingScope : public std::enable_shared_from_this<UsingScope> {
public:
    // The C# `internal readonly ConcurrentDictionary<string, ResolveResult> ResolveCache`
    // -- the per-scope identifier->result memoization cache the resolver's
    // ResolveSimpleName arm consults. The C# `ConcurrentDictionary` is itself
    // thread-safe; the faithful C++ counterpart is this small mutex-guarded map
    // exposing exactly the two operations the resolver uses (the CacheManager
    // locked-map convention). Non-copyable/non-movable so a data member of this type
    // cannot be reassigned (the C# `readonly` field semantics) -- only mutated
    // through its operations.
    class ResolveCacheMap {
    public:
        ResolveCacheMap() = default;
        ResolveCacheMap(const ResolveCacheMap&) = delete;
        ResolveCacheMap& operator=(const ResolveCacheMap&) = delete;
        ResolveCacheMap(ResolveCacheMap&&) = delete;
        ResolveCacheMap& operator=(ResolveCacheMap&&) = delete;

        // The C# `ConcurrentDictionary.TryGetValue(string key, out ResolveResult value)`
        // -- returns false and leaves `value` DEFAULT (an empty `shared_ptr`, the C#
        // out-param default) when the key is absent. `const` (a logically non-mutating
        // read; the mutex is `mutable`, the CacheManager GetShared precedent).
        bool TryGetValue(const std::string& key,
                         std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& value) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = map_.find(key);
            if (it == map_.end()) {
                value = nullptr;
                return false;
            }
            value = it->second;
            return true;
        }

        // The C# `ConcurrentDictionary.TryAdd(string key, ResolveResult value)` -- adds
        // only when the key is absent (the first writer wins; a later `TryAdd` on the
        // same key returns false and does NOT overwrite the stored value).
        bool TryAdd(std::string key,
                    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return map_.emplace(std::move(key), std::move(value)).second;
        }

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::string,
                           std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> map_;
    };

    // The C# public ctor `UsingScope(CSharpTypeResolveContext context, INamespace
    // @namespace, ImmutableArray<INamespace> usings)` -- both C# null guards are
    // meaningful here: the `context` arrives as a nullable `shared_ptr` (the null
    // throws `std::invalid_argument`, the ArgumentNullException analog), while the
    // `@namespace` reference parameter cannot be null (the guard is structurally
    // unreachable, the D374 convention).
    UsingScope(std::shared_ptr<CSharpTypeResolveContext> context,
               const TS::INamespace& namespace_,
               std::vector<const TS::INamespace*> usings);

    // The C# `public INamespace Namespace { get; }` -- the namespace this scope
    // declares (non-null; a `const INamespace&` return, the non-null-reference
    // convention). The referenced namespace is owned by the compilation (or, for the
    // internally-created fallback, by this scope -- the `ownedNamespace_` slot).
    const TS::INamespace& Namespace() const { return *namespace_; }

    // The C# `public UsingScope Parent { get { return parentContext.CurrentUsingScope; } }`
    // -- the enclosing using scope, read THROUGH the parent context's slot (the
    // context-chain shape; see the header convention (g)). Nullable (the empty
    // `shared_ptr` is the C# null).
    std::shared_ptr<UsingScope> Parent() const;

    // The C# `public ImmutableArray<INamespace> Usings { get; }` -- the namespaces
    // imported by this scope's `using` declarations, as a non-owning pointer snapshot
    // (the compilation owns the namespaces).
    const std::vector<const TS::INamespace*>& Usings() const { return usings_; }

    // The C# `public IReadOnlyList<KeyValuePair<string, ResolveResult>> UsingAliases
    // => [];` -- the using-alias declarations of this scope. The C# expression-bodied
    // property always yields the empty list in this decompiler-side port (the parser
    // tracks aliases elsewhere); the faithful port returns the empty vector.
    std::vector<std::pair<std::string,
                          std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>>
    UsingAliases() const
    {
        return {};
    }

    // The C# `public IReadOnlyList<string> ExternAliases => [];` -- always empty
    // (the same expression-bodied shape as `UsingAliases`).
    std::vector<std::string> ExternAliases() const { return {}; }

    // The C# `public bool HasAlias(string identifier) => false;` -- this port never
    // tracks aliases, so no identifier has one.
    bool HasAlias(const std::string& identifier) const
    {
        (void)identifier;
        return false;
    }

    // The C# `internal UsingScope WithNestedNamespace(string simpleName)` -- the
    // nested-namespace scope factory: resolves the metadata child namespace by its
    // simple name, falling back to the internal `DummyNamespace` (an empty namespace
    // with the given simple name -- a namespace declaration that has no corresponding
    // metadata namespace), and creates the child scope against
    // `parentContext.WithUsingScope(this)` (so the child's `Parent` is this scope).
    // `internal` -> public (the convention). Returns an owning `shared_ptr` (the C#
    // heap allocation transferred to the caller). `const` because it reads `this`
    // (the namespace, the parent context, and the `shared_from_this` handle) without
    // modifying it.
    std::shared_ptr<UsingScope> WithNestedNamespace(const std::string& simpleName) const;

    // ---- The C# `internal` mutable state (public in the port, the internal->public
    // convention) -----------------------------------------------------------------
    // The C# `internal readonly ConcurrentDictionary<string, ResolveResult>
    // ResolveCache` -- the identifier->result memoization the resolver's
    // ResolveSimpleName arm reads and fills. A public data member of the
    // non-copyable/non-movable `ResolveCacheMap` type (the `readonly` semantics).
    ResolveCacheMap ResolveCache;

    // The C# `internal List<List<IMethod>>? AllExtensionMethods` -- the lazily-built
    // extension-method group list the resolver's `GetAllExtensionMethods` computes
    // (the `LazyInit.VolatileRead` / `GetOrSet` first-writer-wins pattern). A public
    // nullable `std::shared_ptr` data member (the `LazyInit` helpers take the field's
    // address, so the field must be a plain addressable member; the empty
    // `shared_ptr` is the C# null). The group element is a non-owning
    // `const IMethod*` list (the declaring types own the methods, the
    // `MemberListWithDeclaringType` convention).
    std::shared_ptr<std::vector<std::vector<const TS::IMethod*>>> AllExtensionMethods;

private:
    // The private ctor the `WithNestedNamespace` path takes: additionally carries
    // the owning handle that keeps an internally-created `DummyNamespace` alive for
    // the scope's lifetime (the "own what you create" convention, header note (c)).
    UsingScope(std::shared_ptr<CSharpTypeResolveContext> context,
               const TS::INamespace& namespace_,
               std::vector<const TS::INamespace*> usings,
               std::shared_ptr<const TS::INamespace> ownedNamespace);

    // The C# `sealed class DummyNamespace : INamespace` -- the empty fallback
    // namespace a nested namespace declaration resolves to when the metadata has no
    // corresponding child. Declared here (private nested, the C# shape), defined
    // out-of-line in the .cpp so the ~12-interface-override body stays out of this
    // header.
    class DummyNamespace;

    std::shared_ptr<CSharpTypeResolveContext> parentContext_;
    const TS::INamespace* namespace_;
    std::shared_ptr<const TS::INamespace> ownedNamespace_;  // set only for the DummyNamespace
    std::vector<const TS::INamespace*> usings_;
};

} // namespace ILSpy::Decompiler::CSharp::TypeSystem
