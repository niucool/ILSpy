// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
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

// Port of `ICSharpCode.Decompiler.Util.LazyInit` (Util/LazyInit.cs) -- the
// thread-safe lazy-initialization helper the type-system caches use to defer
// construction of a shared value until first read, with the FIRST writer winning
// and every subsequent read returning the cached value. `LazyInit` is a leaf
// dependency of the type-system implementation: `KnownTypeCache.FindType`,
// `SimpleCompilation.RootNamespace`, and `MergedNamespace.GetChildNamespaces`
// all hold their lazily-built cache as a plain field touched ONLY through
// `VolatileRead` / `GetOrSet`, so the two helpers are the sole accessors and the
// plain field is the atomic storage.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `static class LazyInit` ports to a namespace of free function
//      templates (no instance state); the two helpers stay `inline` templates in
//      the `ILSpy::Decompiler::Util` namespace, mirroring the C# static-class
//      shape (the `CacheManager::TypeKey<T>()` (D397) header-only-template
//      precedent).
//  (b) The C# `where T : class?` generic constraint (a reference type, which has
//      a null state) ports to a template over `std::shared_ptr<T>`: in the C++
//      port the C# reference-type handle is a `std::shared_ptr<T>` (the
//      `ITypePtr` (D271) / `shared_ptr<INamespace>` reference-handle model), and
//      the empty `shared_ptr` is the C# null. Every consumer (`KnownTypeCache`,
//      `SimpleCompilation`, `MergedNamespace`) lazily initializes a
//      `shared_ptr`-held value, so `shared_ptr<T>` is the faithful `T : class`
//      instantiation.
//  (c) The C# `Volatile.Read(ref location)` (an acquire-fenced read of the
//      reference field) ports to `std::atomic_load` on the `shared_ptr` (the
//      C++17 thread-safe read of a `shared_ptr` field, acquire-ordered). The C#
//      `ref T` (a managed pointer to the field) ports to a `const shared_ptr<T>*`
//      parameter (the caller passes `&field`, the address the `std::atomic_load`
//      free function requires); the field MUST be touched only through these
//      helpers so the plain `shared_ptr` is the atomic storage (the same
//      invariant the C# `Volatile`/`Interlocked` accessors enforce).
//  (d) The C# `Interlocked.CompareExchange(ref target, newValue, null)` returning
//      `oldValue ?? newValue` (atomically: if `target` is null, store `newValue`;
//      return whichever wins) ports to `std::atomic_compare_exchange_strong` with
//      an empty `expected` (the C# `null` comparand). On success the empty
//      `expected` means `target` was null and is now `newValue`; return the new
//      value. On failure `expected` is overwritten with the existing `target`;
//      return the existing value. This is the FIRST-WRITER-WINS crux: the value
//      built by the first `GetOrSet` call wins and every later call returns it
//      (the C# `oldValue ?? newValue`), so an idempotent builder never replaces
//      the cached instance.
//  (e) `std::atomic_load` / `std::atomic_compare_exchange_strong` for
//      `shared_ptr` are the standard C++17 free functions (in `<memory>`); they
//      are the faithful genuinely-thread-safe counterpart of the C#
//      `Volatile.Read` / `Interlocked.CompareExchange` on a reference field, and
//      they are the SOLE accessors of the lazily-initialized `shared_ptr` field
//      (so the plain field is the atomic storage, as in C#). The build is /W3
//      with no -Werror (decision D6), so any advisory deprecation note does not
//      break the build.

#pragma once

#include <memory>
#include <utility>

namespace ILSpy::Decompiler::Util {

// C# `T VolatileRead<T>(ref T location) where T : class?` -- an acquire-fenced
// read of the lazily-initialized reference field. The caller passes the field's
// address (`&field`); the field must be touched only through `VolatileRead` and
// `GetOrSet` so the plain `shared_ptr` is the atomic storage. Returns the
// current value (an empty `shared_ptr` if the field has not been initialized
// yet, the C# null).
template <typename T>
inline std::shared_ptr<T> VolatileRead(const std::shared_ptr<T>* location) noexcept
{
    return std::atomic_load(location);
}

// C# `T? GetOrSet<T>(ref T? target, T? newValue) where T : class` -- atomically:
// if `target` is null, store `newValue` and return it; otherwise return the
// existing `target` (the C# `Interlocked.CompareExchange(ref target, newValue,
// null)` returning `oldValue ?? newValue`). The FIRST writer wins; every later
// call returns the cached instance, so an idempotent builder (the type-system
// caches) never replaces it. The caller passes the field's address (`&field`);
// the field must be touched only through `VolatileRead` and `GetOrSet`.
template <typename T>
inline std::shared_ptr<T> GetOrSet(std::shared_ptr<T>* target, std::shared_ptr<T> newValue)
{
    // The C# `null` comparand: an empty `shared_ptr` matches a not-yet-
    // initialized field. `std::atomic_compare_exchange_strong` takes `desired`
    // by value, so passing the (already by-value) `newValue` parameter copies it
    // into the field on success while leaving the local `newValue` intact, which
    // is returned on the success branch; on failure `expected` is overwritten with
    // the existing field value, which is returned instead.
    std::shared_ptr<T> expected;
    if (std::atomic_compare_exchange_strong(target, &expected, newValue))
        return newValue; // success: field was empty, now holds a copy of newValue
    return expected; // failure: field already held a value, now in `expected`
}

} // namespace ILSpy::Decompiler::Util
