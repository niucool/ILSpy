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

// Tests for `CacheManager` (cpp/Decompiler/Util/CacheManager.hpp, the port of
// `ICSharpCode.Decompiler.Util.CacheManager`). `CacheManager` is the per-compilation
// shared cache `ICompilation.CacheManager` returns -- a leaf dependency of
// `ICompilation` (toward `TypeSystemAstBuilder` / `CSharpAmbience`). The tests pin
// the four operations (`GetShared` / `GetOrAddShared` factory/value / `SetShared`),
// the pointer-identity keying (the `ReferenceComparer` semantics), the `TypeKey<T>()`
// per-type-stable-key helper (the `typeof(T)` counterpart), and the `std::any`
// value round-trip (the `object?` -> `std::any` D374 convention).

#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <any>
#include <string>
#include <utility>

namespace Util_ = ILSpy::Decompiler::Util;

// Two distinct stable keys for the non-templated tests (addresses of file-local
// statics, the same pointer-identity shape `TypeKey<T>()` yields). Each is stable
// for the lifetime of the program and distinct from every `TypeKey<T>()`.
namespace {
const char kKeyA = 0;
const char kKeyB = 0;
const void* KeyA() { return &kKeyA; }
const void* KeyB() { return &kKeyB; }
} // namespace

// ---------------------------------------------------------------------------
// `GetShared` on a fresh cache returns an empty `std::any` (the C# `object?`
// null case): the key is absent, the `TryGetValue` miss leaves the out-param
// null, ported to a default-constructed `std::any`.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, GetSharedReturnsEmptyForAbsentKey)
{
    Util_::CacheManager cache;
    EXPECT_FALSE(cache.GetShared(KeyA()).has_value());
}

// ---------------------------------------------------------------------------
// `SetShared` stores a value and `GetShared` retrieves it: the basic write/read
// round-trip. `any_cast<int>` recovers the boxed primitive (the C# `(int) value`
// cast), pinning the `std::any` value-boxing round-trip for a primitive.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, SetSharedStoresValueAndGetSharedRetrievesIt)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), 42);
    std::any retrieved = cache.GetShared(KeyA());
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(std::any_cast<int>(retrieved), 42);
}

// ---------------------------------------------------------------------------
// `SetShared` overwrites the value for an existing key (the C#
// `sharedDict[key] = value` indexer assign replaces the prior value).
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, SetSharedOverwritesExistingValue)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), 1);
    cache.SetShared(KeyA(), 2);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyA())), 2);
}

// ---------------------------------------------------------------------------
// `GetOrAddShared(key, value)` inserts the value when the key is absent and
// returns it (the C# overload taking a pre-built value). A subsequent `GetShared`
// confirms the value was stored, not just returned.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, GetOrAddSharedWithValueInsertsWhenAbsent)
{
    Util_::CacheManager cache;
    std::any result = cache.GetOrAddShared(KeyA(), std::any(42));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<int>(result), 42);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyA())), 42);
}

// ---------------------------------------------------------------------------
// `GetOrAddShared(key, value)` returns the EXISTING value when the key is
// present and does NOT replace it (the C# `GetOrAdd` dedupe: the pre-built
// `value` is discarded if a value is already cached). The cache keeps the
// first-stored value.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, GetOrAddSharedWithValueReturnsExistingWhenPresent)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), 1);
    std::any result = cache.GetOrAddShared(KeyA(), std::any(2));
    EXPECT_EQ(std::any_cast<int>(result), 1);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyA())), 1);
}

// ---------------------------------------------------------------------------
// `GetOrAddShared(key, valueFactory)` invokes the factory when the key is
// absent, stores its result, and returns it (the C# overload taking a
// `Func<object, object>`). The factory receives the key and its result is
// cached: a later `GetShared` retrieves the factory-built value.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, GetOrAddSharedWithFactoryInvokesFactoryWhenAbsent)
{
    Util_::CacheManager cache;
    bool factoryCalled = false;
    std::any result = cache.GetOrAddShared(KeyA(),
        [&](const void* key) -> std::any {
            factoryCalled = true;
            EXPECT_EQ(key, KeyA());
            return 99;
        });
    EXPECT_TRUE(factoryCalled);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<int>(result), 99);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyA())), 99);
}

// ---------------------------------------------------------------------------
// `GetOrAddShared(key, valueFactory)` does NOT invoke the factory when the key
// is already present -- it returns the existing value directly (the C#
// `GetOrAdd` short-circuit). The factory is never called; the existing value
// is returned unchanged.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, GetOrAddSharedWithFactorySkipsFactoryWhenPresent)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), 1);
    bool factoryCalled = false;
    std::any result = cache.GetOrAddShared(KeyA(),
        [&](const void*) -> std::any {
            factoryCalled = true;
            return 99;
        });
    EXPECT_FALSE(factoryCalled);
    EXPECT_EQ(std::any_cast<int>(result), 1);
}

// ---------------------------------------------------------------------------
// `TypeKey<T>()` is stable across calls for the same type: each call returns
// the same address (the single `static` tag per instantiation), matching the
// C# `typeof(T)` reference-equality-per-type semantics. This is the invariant
// that makes `TypeKey<T>()` usable as a `CacheManager` key.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, TypeKeyIsStableAcrossCallsForSameType)
{
    EXPECT_EQ(Util_::TypeKey<int>(), Util_::TypeKey<int>());
    EXPECT_EQ(Util_::TypeKey<std::string>(), Util_::TypeKey<std::string>());
    // The same type yields the same key, so a value stored under
    // `TypeKey<int>()` is retrievable under a second `TypeKey<int>()` call.
    Util_::CacheManager cache;
    cache.SetShared(Util_::TypeKey<int>(), 7);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(Util_::TypeKey<int>())), 7);
}

// ---------------------------------------------------------------------------
// `TypeKey<T>()` distinguishes distinct types: `TypeKey<int>()` and
// `TypeKey<std::string>()` are different addresses (different instantiations,
// different `static` tags), matching `typeof(int) != typeof(string)`.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, TypeKeyDistinguishesDistinctTypes)
{
    EXPECT_NE(Util_::TypeKey<int>(), Util_::TypeKey<std::string>());
    EXPECT_NE(Util_::TypeKey<int>(), Util_::TypeKey<long>());
}

// ---------------------------------------------------------------------------
// Cache entries are independent per `TypeKey<T>()`: storing under
// `TypeKey<int>()` and `TypeKey<std::string>()` keeps the two values separate
// (the keys are distinct addresses). This mirrors the C# resolver pattern of
// caching distinct singletons under distinct `typeof(T)` keys.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, CacheEntriesAreIndependentPerTypeKey)
{
    Util_::CacheManager cache;
    cache.GetOrAddShared(Util_::TypeKey<int>(), std::any(1));
    cache.GetOrAddShared(Util_::TypeKey<std::string>(), std::any(std::string("two")));
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(Util_::TypeKey<int>())), 1);
    EXPECT_EQ(std::any_cast<std::string>(cache.GetShared(Util_::TypeKey<std::string>())), "two");
}

// ---------------------------------------------------------------------------
// Distinct raw keys keep distinct values: `KeyA()` and `KeyB()` are different
// addresses, so entries stored under each are independent -- the pointer-identity
// keying the `ReferenceComparer` semantics rely on.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, DistinctRawKeysKeepDistinctValues)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), 10);
    cache.SetShared(KeyB(), 20);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyA())), 10);
    EXPECT_EQ(std::any_cast<int>(cache.GetShared(KeyB())), 20);
    EXPECT_FALSE(cache.GetShared(Util_::TypeKey<int>()).has_value());
}

// ---------------------------------------------------------------------------
// The `std::any` value round-trips both a `std::string` and a primitive through
// `SetShared` / `GetShared` via `any_cast` (the C# `object` cast), pinning the
// `object?` -> `std::any` D374 convention for the value range the cache stores.
// ---------------------------------------------------------------------------
TEST(Util_CacheManager, AnyValueRoundTripsStringAndPrimitive)
{
    Util_::CacheManager cache;
    cache.SetShared(KeyA(), std::string("hello"));
    cache.SetShared(KeyB(), 3.14);
    EXPECT_EQ(std::any_cast<std::string>(cache.GetShared(KeyA())), "hello");
    EXPECT_DOUBLE_EQ(std::any_cast<double>(cache.GetShared(KeyB())), 3.14);
}
