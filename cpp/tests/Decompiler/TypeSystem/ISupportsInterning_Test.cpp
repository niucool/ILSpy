// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
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
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ISupportsInterning` (cpp/Decompiler/TypeSystem/ISupportsInterning.hpp,
// the D412 port of ICSharpCode.Decompiler/TypeSystem/ISupportsInterning.cs). The
// interface is the opt-in marker a TypeSystem object implements so the
// `InterningProvider` can deduplicate structurally-equal instances: the provider
// buckets candidate duplicates by `GetHashCodeForInterning` and confirms a bucketed
// pair is structurally equal with `EqualsForInterning`. The tests pin the interface
// contract (a concrete subclass overriding both accessors, polymorphic dispatch
// through an `ISupportsInterning*`, the `dynamic_cast`-on-type-mismatch null case the
// concrete references' `as`-then-null-check relies on, and the virtual destructor) via
// test stubs that mirror the `ByReferenceTypeReference` interning shape (a per-type
// salt XORed with the element's identity hash, and reference equality on the element).
//
// `InterningProvider` is NOT yet ported; this test exercises the interface contract in
// isolation (the shape its `Intern`/bucketing-comparer call path will use), not the
// provider itself. No stand-in is needed (`ISupportsInterning` is a NEW type; no
// existing test file defined a stand-in).

#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <type_traits>

namespace {

// A concrete `ISupportsInterning` mirroring the `ByReferenceTypeReference` interning
// shape: it holds a pointer element (identity-tested by pointer compare, the C#
// reference-equality `this.elementType == brt.elementType`) and a per-type salt, and
// `GetHashCodeForInterning` XORs the element's identity hash with the salt (the C#
// `elementType.GetHashCode() ^ 91725814`). `EqualsForInterning` downcasts `other` via
// `dynamic_cast` (the C# `other as ByReferenceTypeReference`) and returns `false` when
// the cast fails (the C# `brt != null` guard) -- the load-bearing shape the
// type-mismatch test pins.
class TestByRefInterning : public ILSpy::Decompiler::TypeSystem::ISupportsInterning {
public:
    TestByRefInterning(const void* element, int salt) : element_(element), salt_(salt) {}

    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(std::hash<const void*>{}(element_)) ^ salt_;
    }

    bool EqualsForInterning(const ILSpy::Decompiler::TypeSystem::ISupportsInterning& other) const override
    {
        const auto* brt = dynamic_cast<const TestByRefInterning*>(&other);
        return brt != nullptr && this->element_ == brt->element_ && this->salt_ == brt->salt_;
    }

private:
    const void* element_;
    int salt_;
};

// A SECOND concrete `ISupportsInterning` subtype (a different reference kind) so the
// type-mismatch test can exercise the `dynamic_cast`-returns-null path: an
// `TestByRefInterning` compared against an `TestOtherInterning` must return `false`
// regardless of the fields, mirroring the C# `as`-returns-null when the bucketed
// candidate is a different `ISupportsInterning` subtype.
class TestOtherInterning : public ILSpy::Decompiler::TypeSystem::ISupportsInterning {
public:
    int GetHashCodeForInterning() const override { return 0x42424242; }

    bool EqualsForInterning(const ILSpy::Decompiler::TypeSystem::ISupportsInterning& other) const override
    {
        return dynamic_cast<const TestOtherInterning*>(&other) != nullptr;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// ISupportsInterning -- `GetHashCodeForInterning` returns the configured per-type hash
// (the element's identity hash XORed with the salt), the value the `InterningProvider`
// buckets candidate duplicates by. Pinned directly and through the base pointer.
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, GetHashCodeForInterningReturnsConfiguredValue)
{
    int element = 0x12345678;
    TestByRefInterning a(&element, 0x2A2A2A2A);
    const int expected = static_cast<int>(std::hash<const void*>{}(&element)) ^ 0x2A2A2A2A;
    EXPECT_EQ(a.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- `GetHashCodeForInterning` dispatches polymorphically through an
// `ISupportsInterning*` (the dynamic dispatch the `InterningProvider`'s bucketing
// dictionary relies on: it holds the candidate as an `ISupportsInterning*` and calls
// `GetHashCodeForInterning` through the base to reach the concrete override).
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, GetHashCodeForInterningDispatchesPolymorphicallyThroughBasePointer)
{
    int element = 1;
    TestByRefInterning a(&element, 0x11111111);
    ILSpy::Decompiler::TypeSystem::ISupportsInterning* base = &a;
    const int expected = static_cast<int>(std::hash<const void*>{}(&element)) ^ 0x11111111;
    EXPECT_EQ(base->GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- `EqualsForInterning` returns `true` for two stubs with the same
// element and salt (the provider returns the shared instance for the equal pair).
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, EqualsForInterningReturnsTrueForEqualStub)
{
    int element = 2;
    TestByRefInterning a(&element, 0x30303030);
    TestByRefInterning b(&element, 0x30303030);
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- `EqualsForInterning` returns `false` for two stubs with a
// different element (same salt, different identity) -- the provider keeps both as
// distinct instances.
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, EqualsForInterningReturnsFalseForDifferentElement)
{
    int elementA = 3;
    int elementB = 4;
    TestByRefInterning a(&elementA, 0x44444444);
    TestByRefInterning b(&elementB, 0x44444444);
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- the crux: `EqualsForInterning` returns `false` when `other` is a
// DIFFERENT `ISupportsInterning` subtype. The concrete reference downcasts `other` via
// `dynamic_cast<const ConcreteType*>(&other)` which yields `nullptr` on a type mismatch
// (the C# `other as ByReferenceTypeReference`-returns-null case), so the bucketing
// comparer never mistakes a `ByReferenceTypeReference` for an `ArrayTypeReference`. This
// is the load-bearing type-discrimination shape the `InterningProvider`'s correctness
// relies on.
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    int element = 5;
    TestByRefInterning byRef(&element, 0x55555555);
    TestOtherInterning other;
    EXPECT_FALSE(byRef.EqualsForInterning(other));
    EXPECT_FALSE(other.EqualsForInterning(byRef));
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- `EqualsForInterning` dispatches polymorphically through an
// `ISupportsInterning*` (the `InterningProvider`'s bucketing comparer holds both
// candidates as `ISupportsInterning*` and calls `EqualsForInterning` through the base).
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, EqualsForInterningDispatchesPolymorphicallyThroughBasePointer)
{
    int element = 6;
    TestByRefInterning a(&element, 0x66666666);
    TestByRefInterning b(&element, 0x66666666);
    ILSpy::Decompiler::TypeSystem::ISupportsInterning* baseA = &a;
    ILSpy::Decompiler::TypeSystem::ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- `GetHashCodeForInterning` is consistent for the same object
// across calls (the provider may bucket and re-bucket the same candidate; the hash must
// be stable for the object's lifetime, matching the .NET `GetHashCode` contract).
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, GetHashCodeForInterningIsStableAcrossCalls)
{
    int element = 7;
    TestByRefInterning a(&element, 0x77777777);
    const int first = a.GetHashCodeForInterning();
    EXPECT_EQ(a.GetHashCodeForInterning(), first);
    EXPECT_EQ(a.GetHashCodeForInterning(), first);
}

// ---------------------------------------------------------------------------
// ISupportsInterning -- has a virtual destructor (a concrete subclass can be deleted
// through an `ISupportsInterning*` and the derived destructor runs), is abstract (the
// two pure-virtuals must be overridden), and is polymorphic (RTTI/`dynamic_cast` works
// through the base pointer -- the load-bearing `dynamic_cast` the concrete references'
// `EqualsForInterning` relies on). The established abstract-base contract.
// ---------------------------------------------------------------------------
TEST(ISupportsInterningTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ISupportsInterning>,
        "ISupportsInterning must have a virtual destructor for abstract-base deletion");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::ISupportsInterning>,
        "ISupportsInterning must be abstract (two pure-virtuals)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::ISupportsInterning>,
        "ISupportsInterning must be polymorphic for dynamic_cast through the base pointer");
    int element = 8;
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ISupportsInterning> owned =
        std::make_unique<TestByRefInterning>(&element, 0x88888888);
    EXPECT_EQ(owned->GetHashCodeForInterning(),
        static_cast<int>(std::hash<const void*>{}(&element)) ^ 0x88888888);
    owned.reset(); // runs the TestByRefInterning destructor through the virtual dtor
    SUCCEED();
}
