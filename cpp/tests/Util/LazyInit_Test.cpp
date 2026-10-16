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

// Tests for `LazyInit` (cpp/Decompiler/Util/LazyInit.hpp, the port of
// `ICSharpCode.Decompiler.Util.LazyInit`). `LazyInit` is the thread-safe
// lazy-initialization helper the type-system caches use to defer construction
// of a shared value until first read, with the FIRST writer winning -- a leaf
// dependency of `KnownTypeCache` / `SimpleCompilation` / `MergedNamespace`
// (toward `SimpleCompilation` / `TypeSystemAstBuilder` / `CSharpAmbience`). The
// tests pin the acquire-fenced `VolatileRead`, the first-writer-wins `GetOrSet`
// crux (the C# `Interlocked.CompareExchange(ref target, newValue, null)` returning
// `oldValue ?? newValue`), the shared-ownership of the returned `shared_ptr`, and
// the idempotent-builder never-replaces-cached-instance invariant.

#include "Decompiler/Util/LazyInit.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace Util_ = ILSpy::Decompiler::Util;

namespace {

// A trivial value type the lazily-initialized `shared_ptr` holds; the `value`
// member lets the tests distinguish the cached instance from a later candidate.
struct LazyValue {
    int value;
};

} // namespace

// ---------------------------------------------------------------------------
// `VolatileRead` of a not-yet-initialized field returns an empty `shared_ptr`
// (the C# null): the field has never been written, so the acquire-fenced read
// observes the empty state.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, VolatileReadReturnsEmptyForUninitializedField)
{
    std::shared_ptr<LazyValue> field;
    EXPECT_EQ(Util_::VolatileRead(&field), nullptr);
}

// ---------------------------------------------------------------------------
// `VolatileRead` of an initialized field returns the stored value, sharing
// ownership with the field (the returned `shared_ptr` and the field point at
// the same object -- the acquire-fenced read does not detach or copy the value).
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, VolatileReadReturnsStoredValue)
{
    std::shared_ptr<LazyValue> field = std::make_shared<LazyValue>(LazyValue{11});
    std::shared_ptr<LazyValue> read = Util_::VolatileRead(&field);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->value, 11);
    EXPECT_EQ(read.get(), field.get()); // shares ownership (same object)
    EXPECT_EQ(read.use_count(), 2);     // field + the returned copy
}

// ---------------------------------------------------------------------------
// `GetOrSet` on an empty field stores the new value and returns it: the C#
// `Interlocked.CompareExchange(ref target, newValue, null)` finds `target` null,
// stores `newValue`, and returns `oldValue ?? newValue` = `newValue`.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, GetOrSetOnEmptyFieldStoresAndReturnsValue)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> a = std::make_shared<LazyValue>(LazyValue{1});
    std::shared_ptr<LazyValue> result = Util_::GetOrSet(&field, a);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->value, 1);
    EXPECT_EQ(result.get(), a.get());          // returned the new value
    EXPECT_EQ(field.get(), a.get());          // field now holds the new value
    EXPECT_EQ(Util_::VolatileRead(&field).get(), a.get());
}

// ---------------------------------------------------------------------------
// FIRST-WRITER-WINS crux: `GetOrSet` on an already-set field returns the
// EXISTING value, NOT the candidate. The C# `oldValue ?? newValue` resolves to
// the non-null `oldValue` (the value the first `GetOrSet` stored); the candidate
// is discarded. This is the load-bearing invariant an idempotent builder relies
// on -- the cached instance is never replaced by a later, value-equal rebuild.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, GetOrSetOnAlreadySetFieldReturnsExistingValue)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> a = std::make_shared<LazyValue>(LazyValue{1});
    std::shared_ptr<LazyValue> b = std::make_shared<LazyValue>(LazyValue{2});

    ASSERT_EQ(Util_::GetOrSet(&field, a).get(), a.get()); // first call stores a
    std::shared_ptr<LazyValue> second = Util_::GetOrSet(&field, b); // field not empty
    EXPECT_EQ(second.get(), a.get()); // crux: returns the existing a, NOT b
    EXPECT_NE(second.get(), b.get());
}

// ---------------------------------------------------------------------------
// The first-writer-wins contract also means the field is NOT overwritten by a
// later `GetOrSet`: after storing `a` then attempting `b`, a `VolatileRead`
// still observes `a` (the first value), never `b`. The candidate `b` is dropped.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, GetOrSetDoesNotOverwriteExistingValue)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> a = std::make_shared<LazyValue>(LazyValue{1});
    std::shared_ptr<LazyValue> b = std::make_shared<LazyValue>(LazyValue{2});

    Util_::GetOrSet(&field, a);
    Util_::GetOrSet(&field, b);
    EXPECT_EQ(Util_::VolatileRead(&field).get(), a.get()); // still a, not b
}

// ---------------------------------------------------------------------------
// An idempotent builder (the type-system caches) calling `GetOrSet` twice with
// value-equal but DISTINCT instances gets the SAME (first) instance back both
// times -- the cache never swaps to the second instance even though it is
// value-equal. The two returned `shared_ptr`s share ownership of the first.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, GetOrSetWithIdempotentBuilderReturnsSameInstance)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> first = std::make_shared<LazyValue>(LazyValue{42});
    std::shared_ptr<LazyValue> second = std::make_shared<LazyValue>(LazyValue{42});

    std::shared_ptr<LazyValue> r1 = Util_::GetOrSet(&field, first);
    std::shared_ptr<LazyValue> r2 = Util_::GetOrSet(&field, second);
    EXPECT_EQ(r1.get(), first.get());
    EXPECT_EQ(r2.get(), first.get()); // same instance as r1, NOT second
    EXPECT_NE(r2.get(), second.get());
    EXPECT_EQ(r1.get(), r2.get());
}

// ---------------------------------------------------------------------------
// `VolatileRead` after a `GetOrSet` reflects the stored value: the acquire-fenced
// read observes the value the first `GetOrSet` wrote. Combined with the no-
// overwrite invariant, this pins the full read/write contract the type-system
// caches rely on (read-after-write consistency under the single-accessor model).
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, VolatileReadReflectsValueAfterGetOrSet)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> a = std::make_shared<LazyValue>(LazyValue{7});
    Util_::GetOrSet(&field, a);
    std::shared_ptr<LazyValue> read = Util_::VolatileRead(&field);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->value, 7);
    EXPECT_EQ(read.get(), a.get());
}

// ---------------------------------------------------------------------------
// The returned `shared_ptr` shares ownership with the field: after `GetOrSet`
// the field, the returned `shared_ptr`, and a subsequent `VolatileRead` all
// refer to the same object (the `use_count` reflects the live references). This
// is the faithful C# GC-reference-wrap -- the cached value is shared, not
// deep-copied, by the lazy-init helper.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, ReturnedSharedPtrSharesOwnershipWithField)
{
    std::shared_ptr<LazyValue> field;
    std::shared_ptr<LazyValue> a = std::make_shared<LazyValue>(LazyValue{3});
    std::shared_ptr<LazyValue> stored = Util_::GetOrSet(&field, a);
    std::shared_ptr<LazyValue> read = Util_::VolatileRead(&field);
    EXPECT_EQ(stored.get(), a.get());
    EXPECT_EQ(read.get(), a.get());
    // field + the `a` local + `stored` + `read` all reference the same object.
    EXPECT_EQ(a.use_count(), 4);
}

// ---------------------------------------------------------------------------
// `VolatileRead` is callable on a `const` field address (the acquire-fenced read
// does not mutate the field): the C# `Volatile.Read` is a non-mutating read, and
// the C++ `VolatileRead` takes a `const shared_ptr<T>*` so a `const` field's
// address binds. This mirrors the read-only access the type-system caches do
// once the value is initialized.
// ---------------------------------------------------------------------------
TEST(Util_LazyInit, VolatileReadAcceptsConstFieldAddress)
{
    std::shared_ptr<LazyValue> field = std::make_shared<LazyValue>(LazyValue{9});
    const std::shared_ptr<LazyValue>& constField = field;
    std::shared_ptr<LazyValue> read = Util_::VolatileRead(&constField);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->value, 9);
}
