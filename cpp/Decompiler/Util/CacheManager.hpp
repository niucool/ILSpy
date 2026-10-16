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

// Port of `ICSharpCode.Decompiler.Util.CacheManager` (the per-compilation shared
// cache). `CacheManager` is the type `ICompilation.CacheManager` returns (a leaf
// dependency of `ICompilation`, toward `TypeSystemAstBuilder` / `CSharpAmbience`):
// a thread-safe keyed cache the resolver/round-trip paths hang shared singleton
// values off (`CSharpConversions` / `CSharpOperators` cache their per-compilation
// instance under the `typeof(CSharpConversions)` / `typeof(CSharpOperators)` key
// via `GetOrAddShared`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class CacheManager` (a reference type) ports to a C++ class
//      with a virtual destructor disabled (it is `sealed` -- no derivation); the
//      C++ port stays a concrete (non-polymorphic) class because the C# class is
//      `sealed` and has no virtual members. `ICompilation.CacheManager` returns it
//      by reference in the port (the C# `CacheManager` property returns the
//      reference type by reference), so the cache instance is owned by the
//      compilation and returned as `const CacheManager&`.
//  (b) The C# `ConcurrentDictionary<object, object>` keyed by `ReferenceComparer`
//      ports to a `std::unordered_map<const void*, std::any>` guarded by a
//      `std::mutex`. The `ReferenceComparer` semantics are `Equals(x, y)` is
//      `x == y` (reference equality) and `GetHashCode(obj)` is
//      `RuntimeHelpers.GetHashCode` (the identity hash); a `const void*` key
//      reproduces both exactly -- pointer `==` is the reference equality and
//      `std::hash<const void*>` hashes the pointer value (the identity hash), so
//      no separate comparer type is needed. The mutex makes the four operations
//      thread-safe (the C# "This class is thread-safe" doc comment).
//  (c) The C# `object` key ports to `const void*` (the reference identity). The
//      C# consumers key on `typeof(T)`, which is a reference-equal-per-type
//      `System.Type` object; the C++ counterpart is the `TypeKey<T>()` helper
//      (below), which yields a stable per-type address (one `static` per
//      instantiation), so `TypeKey<T>() == TypeKey<U>()` (pointer identity) iff
//      `T` and `U` are the same type, matching `typeof` reference identity.
//  (d) The C# `object?` value ports to `std::any` (the D374
//      `IVariable::GetConstantValue` precedent -- `std::any` is the type-erased
//      boxed value, empty for `null`). `GetShared` returns an empty `std::any`
//      for an absent key (the C# `object?` null case); consumers test
//      `has_value` (the C# `null` test) and retrieve with `std::any_cast` (the
//      C# `object` cast). A `std::any` holds any copy-constructible value, so it
//      models the full BCL `object?` range the cache stores -- a primitive, a
//      `std::string`, an `ITypePtr`, or a resolver singleton pointer.

#pragma once

#include <any>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ILSpy::Decompiler::Util {

// The per-compilation shared cache: a thread-safe keyed map of arbitrary boxed
// values, keyed by pointer identity (the C# `ReferenceComparer` semantics).
// Concrete compilations (the unported `SimpleCompilation`) hold one by value and
// expose it via `ICompilation::CacheManager()`; the resolver/round-trip paths
// read/write per-compilation singletons through `GetShared` / `GetOrAddShared` /
// `SetShared`.
class CacheManager {
public:
    CacheManager() = default;

    // Not copyable (a per-compilation cache is a single shared instance, the C#
    // `readonly` field initialized once on `SimpleCompilation` construction);
    // moving would race with concurrent access, so the type is non-copyable and
    // non-movable (the cache is held by reference by the compilation).
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;
    CacheManager(CacheManager&&) = delete;
    CacheManager& operator=(CacheManager&&) = delete;

    // C# `object? GetShared(object key)` -- returns the cached value, or an empty
    // `std::any` when the key is absent (the `object?` null case). The C#
    // `TryGetValue` miss leaves the out-param `null`, which ports to a default
    // (empty) `std::any`.
    std::any GetShared(const void* key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sharedDict_.find(key);
        if (it == sharedDict_.end())
            return std::any();
        return it->second;
    }

    // C# `object GetOrAddShared(object key, Func<object, object> valueFactory)` --
    // returns the existing value, or computes it via `valueFactory(key)` and
    // stores it. The factory is called at most once (under the lock); the C#
    // `ConcurrentDictionary.GetOrAdd` may call the factory speculatively under
    // contention, but the single stored value is the same, so the locked
    // at-most-once call is a faithful (and tighter) port.
    //
    // The C# `Func<object, object>` is a distinct delegate type, so overload
    // resolution cleanly separates it from the `object` value overload. In C++
    // a `std::function<...>` and a `std::any` both have greedy templated
    // constructors, so a lambda would convert to BOTH (ambiguous). The faithful
    // port therefore takes the factory as an SFINAE-constrained template that
    // accepts only callables invocable with a `const void*` returning something
    // convertible to `std::any` (`std::is_invocable_r_v<std::any, F, const void*>`)
    // -- a non-callable argument (a `std::any` value, an `int`, a string) does
    // not match the template, so it falls through to the `std::any` value
    // overload, and a callable does not match `std::any` (a callable is rarely a
    // `std::any`), so the two overloads never compete for the same argument.
    template <typename F,
              std::enable_if_t<std::is_invocable_r_v<std::any, F, const void*>, int> = 0>
    std::any GetOrAddShared(const void* key, F&& valueFactory) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sharedDict_.find(key);
        if (it != sharedDict_.end())
            return it->second;
        std::any value = std::forward<F>(valueFactory)(key);
        sharedDict_.emplace(key, value);
        return value;
    }

    // C# `object GetOrAddShared(object key, object value)` -- returns the existing
    // value, or stores `value` and returns it. The C# overload taking a pre-built
    // value (used by the resolver paths that build the singleton unconditionally
    // and let the cache dedupe).
    std::any GetOrAddShared(const void* key, std::any value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sharedDict_.find(key);
        if (it != sharedDict_.end())
            return it->second;
        sharedDict_.emplace(key, value);
        return value;
    }

    // C# `void SetShared(object key, object value)` -- overwrites (or inserts) the
    // value for the key (the C# `sharedDict[key] = value` indexer assign).
    void SetShared(const void* key, std::any value) {
        std::lock_guard<std::mutex> lock(mutex_);
        sharedDict_[key] = std::move(value);
    }

private:
    // The mutex guards all four operations (the C# `ConcurrentDictionary` is
    // itself thread-safe); `mutable` so `GetShared` (logically const, the C#
    // non-mutating `TryGetValue`) can lock.
    mutable std::mutex mutex_;
    std::unordered_map<const void*, std::any> sharedDict_;
};

// `TypeKey<T>()` returns a stable per-type address -- the C++ counterpart of the
// C# `typeof(T)` reference the `CSharpConversions` / `CSharpOperators` consumers
// use as a `CacheManager` key. The C# `typeof(T)` returns the same `System.Type`
// reference for a given type (reference-equal per type, the `ReferenceComparer`
// semantics); each instantiation of `TypeKey<T>()` has its own `static` `tag`, so
// `TypeKey<T>()` and `TypeKey<U>()` compare equal (pointer identity) iff `T` and
// `U` are the same type. Pass the result to `CacheManager` as the `const void*`
// key.
template <typename T>
inline const void* TypeKey() {
    static const char tag = 0;
    return &tag;
}

} // namespace ILSpy::Decompiler::Util
