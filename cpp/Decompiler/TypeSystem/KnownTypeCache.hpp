// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.TypeSystem.Implementation.KnownTypeCache`
// (TypeSystem/Implementation/KnownTypeCache.cs) -- the per-compilation cache that
// backs `ICompilation.FindType(KnownTypeCode)`. A `KnownTypeCache` holds one lazily-
// initialized slot per `KnownTypeCode` and resolves a code to its `IType` on first
// use: the `ITypeDefinition` found by searching the compilation's `Modules` (the
// happy path), or an `UnknownType` fallback (the type reference was not found in any
// module), or the `SpecialType.UnknownType` null object for `KnownTypeCode.None`
// (which has no `KnownTypeReference`). The slot is touched ONLY through the
// `LazyInit` helpers (D420) so the plain `shared_ptr` field is the atomic storage
// and the FIRST writer wins (a later `FindType` for the same code returns the
// cached instance rather than rebuilding it).
//
// It is a leaf TypeSystem dependency toward `SimpleCompilation` (the concrete
// `ICompilation`, not yet ported, which holds a `KnownTypeCache` and forwards
// `FindType` to it) and onward to `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker of `CSharpAmbience`). All its deps are now ported:
// `ICompilation` (D399, the `Modules` collection), `IModule` (D396,
// `GetTypeDefinition`), `ITypeDefinition` (D393, the found type), `KnownTypeReference`
// (D411, `Get` / the metadata accessors / `KnownTypeCodeCount`), `TopLevelTypeName`
// (D271, the lookup key), `UnknownType` (D417, the fallback), and `LazyInit` (D420,
// the thread-safe lazy init). `KnownTypeCache` is `sealed` in C# and is a concrete
// leaf here (no subclasses); it is held by value as a member of `SimpleCompilation`.

// KEY PORT CONVENTIONS:
//  (a) The C# `IType[] knownTypes = new IType[KnownTypeReference.KnownTypeCodeCount]`
//      (a reference array, each slot `null` until first use) ports to a
//      `std::array<std::shared_ptr<const IType>, KnownTypeCodeCount>` -- one slot
//      per code, each an empty `shared_ptr` (the C# `null`) until first use. The
//      slots are `shared_ptr<const IType>` (NOT `ITypePtr` = `shared_ptr<IType>`)
//      because the cached result may be a MODULE-OWNED `ITypeDefinition` (a
//      `const ITypeDefinition*` the module owns for the compilation's lifetime),
//      which is wrapped in a NON-OWNING `shared_ptr` (a no-op deleter, convention
//      (d)); `shared_ptr<const IType>` binds a `const IType*` directly with no
//      `const_cast`, while `shared_ptr<IType>` would require one. The built results
//      (the `UnknownType` fallback and the `SpecialType.UnknownType` null object)
//      are owned `shared_ptr<IType>` that convert to `shared_ptr<const IType>`
//      implicitly (the shared_ptr const-converting constructor). This is the
//      polymorphic-`IType`-cache convention the `ParameterizedTypeReference` (D416)
//      `mutable std::shared_ptr<const IType> resolved_` established.
//  (b) The C# `IType FindType(KnownTypeCode typeCode)` returns an `IType` reference
//      (non-null); the C++ port returns `const IType&` (the `ICompilation::FindType`
//      D399 non-null-reference convention) -- `SimpleCompilation.FindType` will
//      forward to this and return the reference. The body reads the cached slot via
//      `LazyInit::VolatileRead`; on a hit it returns `*cached`; on a miss it calls
//      `SearchType` (which builds the `IType`) and `LazyInit::GetOrSet`s it
//      (first-writer-wins), then returns `*cached`.
//  (c) The C# `IType SearchType(KnownTypeCode typeCode)` returns the built `IType`
//      (non-null); the C++ port returns `std::shared_ptr<const IType>` (the slot
//      type, the value `GetOrSet` caches). The three branches: `KnownTypeReference::Get`
//      returns `nullptr` for `None` -> the `UnknownType()` convenience null object
//      (an OWNED `SpecialType(TypeKind::Unknown)`, the `UnknownType()` function NOT
//      the `class UnknownType` -- the two share the name via the C++ tag-vs-ordinary-
//      namespace distinction, D417); a module's `GetTypeDefinition` returns non-null
//      -> a NON-OWNING `shared_ptr<const IType>` aliasing the module's `ITypeDefinition`
//      (convention (d)); no module has the type -> an OWNED `UnknownType(ns, name, tpc)`
//      built via `new class UnknownType(...)` (the elaborated-type-specifier dodges
//      the MSVC `make_shared<UnknownType>`-resolves-to-the-function quirk, D417).
//  (d) The MODULE-OWNED `ITypeDefinition` NON-OWNING ALIAS is the key C++-vs-C#
//      divergence: in C# the `ITypeDefinition` is GC-owned, so storing it in the
//      `IType[]` cache is trivial (the GC owns it). In C++ the `ITypeDefinition` is
//      owned by the module (a raw `const ITypeDefinition*`), and the cache slot is a
//      `shared_ptr<const IType>` -- so the found `ITypeDefinition` is wrapped in a
//      `shared_ptr<const IType>` with a NO-OP DELETER (`[](const IType*){}`), which
//      aliases the module's `ITypeDefinition` for the compilation's lifetime WITHOUT
//      taking ownership (the `shared_ptr` does not delete it when the cache is
//      cleared). `static_cast<const IType*>(typeDef)` upcasts (the `ITypeDefinition`
//      IS-A `IType` via `ITypeDefinitionOrUnknown`, D393); no `const_cast` is needed
//      because the slot is `shared_ptr<const IType>` (a `const IType*` binds
//      directly). This is the FIRST ported TypeSystem cache that stores a
//      module-owned interface pointer as a non-owning `shared_ptr` alias.
//  (e) The cache array is `mutable` because `FindType` is `const` (the
//      `ICompilation::FindType` contract is a const read) but lazily WRITES the
//      cache via `LazyInit::GetOrSet` (the `ParameterizedTypeReference` D416
//      `mutable shared_ptr<const IType> resolved_` const-lazy-write precedent). The
//      `LazyInit` helpers take `shared_ptr<T>*` (a non-const pointer-to-`shared_ptr`),
//      so `&knownTypes_[i]` must be a non-const `shared_ptr*` -- the `mutable`
//      specifier on the array makes `knownTypes_[i]` a non-const `shared_ptr` even
//      in a const method, so `&knownTypes_[i]` binds to `GetOrSet`'s
//      `shared_ptr<T>*` parameter.

#pragma once

#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp" // KnownTypeCodeCount

#include <array>
#include <cstddef>
#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `ICompilation` (D399, the `Modules` collection the search
// walks) and `IType` (the D271 minimal port, the cached slot / `FindType` return).
// Both are incomplete here -- the cache array's `shared_ptr<const IType>` element
// has a type-erased deleter, so `std::array` instantiates with `IType` incomplete,
// and the `const ICompilation&` member is a reference (complete with the pointee
// incomplete). The `FindType` / `SearchType` bodies are out-of-line in the `.cpp`
// where the complete-type headers are included.
class ICompilation;
class IType;

// Cache for `KnownTypeReference`s: lazily resolves a `KnownTypeCode` to its `IType`
// (the `ITypeDefinition` found in a module, an `UnknownType` fallback, or the
// `SpecialType.UnknownType` null object for `None`), caching the result per code
// with first-writer-wins semantics via `LazyInit`. The C# `sealed class` is a
// concrete leaf here; it is held by value as a member of `SimpleCompilation`.
class KnownTypeCache {
public:
    // The C# `KnownTypeCache(ICompilation compilation)` -- binds the compilation
    // whose `Modules` the search walks. The cache slots start empty (the C# `null`).
    explicit KnownTypeCache(const ICompilation& compilation);

    // The C# `IType FindType(KnownTypeCode typeCode)` -- the cached `IType` for the
    // code (non-null). Returns `const IType&` (the `ICompilation::FindType`
    // non-null-reference convention); lazily builds and caches the result on first
    // use via `LazyInit` (first-writer-wins).
    const IType& FindType(KnownTypeCode typeCode) const;

private:
    // The C# `IType SearchType(KnownTypeCode typeCode)` -- builds the `IType` for
    // the code: `SpecialType.UnknownType` for `None` (no `KnownTypeReference`), the
    // `ITypeDefinition` found in a module (a non-owning alias, convention (d)), or an
    // `UnknownType` fallback (no module has the type). Returns
    // `shared_ptr<const IType>` (the slot type); out-of-line in the `.cpp`.
    std::shared_ptr<const IType> SearchType(KnownTypeCode typeCode) const;

    const ICompilation& compilation_;
    // The C# `IType[] knownTypes` -- one lazily-initialized slot per `KnownTypeCode`.
    // `shared_ptr<const IType>` per slot (convention (a)); an empty `shared_ptr` is
    // the C# `null`. `mutable` so the const `FindType` can lazily write it via
    // `LazyInit` (convention (e)).
    mutable std::array<std::shared_ptr<const IType>, KnownTypeCodeCount> knownTypes_;
};

} // namespace ILSpy::Decompiler::TypeSystem
