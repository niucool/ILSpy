// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
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
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ByReferenceTypeReference` (cpp/Decompiler/TypeSystem/
// ByReferenceTypeReference.hpp, the port of the `ByReferenceTypeReference` class
// from ICSharpCode.Decompiler/TypeSystem/ByReferenceType.cs). It is the second
// concrete `ITypeReference` (after `KnownTypeReference` D411), the first to derive
// from BOTH `ITypeReference` (D408) and `ISupportsInterning` (D412), and the
// structural twin of `PointerTypeReference` (the `&` vs `*` managed-vs-unmanaged-
// pointer pair). A `ByReferenceTypeReference` wraps an `ITypeReference` element and
// resolves to a `ByReferenceType` wrapping the element's resolved `IType` (the
// `ref T` / `out T` managed-pointer shape).

#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/PointerTypeReference.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::ByReferenceTypeReference;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeReference;
using ILSpy::Decompiler::TypeSystem::ITypeResolveContext;
using ILSpy::Decompiler::TypeSystem::ISupportsInterning;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::PointerTypeReference;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A minimal `ICompilation` backing the `TestResolveContext`: overrides the nine
// `ICompilation` pure-virtuals with trivial returns. `ByReferenceTypeReference::
// Resolve` delegates to the element's `Resolve`, which (for the test's
// `TestTypeReference`) IGNORES the context, so `FindType` is never called -- but
// the context must still be a valid concrete `ITypeResolveContext` (it threads
// `ICompilationProvider::Compilation`), hence the trivial stub.
class TestCompilation : public ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

    const IModule& MainModule() const override { return mainModule_; }
    std::vector<const IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const IModule*> ReferencedModules() const override { return {}; }
    const INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const IType& FindType(KnownTypeCode) const override
    {
        throw std::logic_error("TestCompilation::FindType should not be called");
    }
    const StringComparer& NameComparer() const override { return StringComparer::Ordinal(); }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
    }

private:
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A trivial concrete `ITypeResolveContext` for `ByReferenceTypeReference::Resolve`:
// overrides all six pure-virtuals with trivial returns backed by the held
// `TestCompilation`. The element's `Resolve` ignores the context, so the nullable
// current-module / -type / -member slots return null and the `With*` factories
// return null `unique_ptr`s (never called by this test).
class TestResolveContext : public ITypeResolveContext {
public:
    TestResolveContext() = default;

    const ICompilation& Compilation() const override { return compilation_; }
    const IModule* CurrentModule() const override { return nullptr; }
    const ITypeDefinition* CurrentTypeDefinition() const override { return nullptr; }
    const IMember* CurrentMember() const override { return nullptr; }
    std::unique_ptr<ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition*) const override
    {
        return {};
    }
    std::unique_ptr<ITypeResolveContext> WithCurrentMember(const IMember*) const override
    {
        return {};
    }

private:
    TestCompilation compilation_;
};

// A concrete `ITypeReference` backing the `ByReferenceTypeReference` element: it
// holds a `shared_ptr<IType>` and `Resolve` returns `*element_` (ignoring the
// context). The element is `shared_ptr`-owned (constructed via `make_shared`), so
// `IType::shared_from_this()` is valid when `ByReferenceTypeReference::Resolve`
// obtains an `ITypePtr` to it -- the load-bearing shape the `Resolve` test relies
// on (the D406 `enable_shared_from_this<IType>` bridge).
class TestTypeReference : public ITypeReference {
public:
    explicit TestTypeReference(ITypePtr element) : element_(std::move(element)) {}

    const IType& Resolve(const ITypeResolveContext&) const override { return *element_; }

private:
    ITypePtr element_;
};

// The C# `ByReferenceTypeReference` interning salt constant (the
// `elementType.GetHashCode() ^ 91725814` operand).
constexpr int kByRefSalt = 91725814;

} // namespace

// ---------------------------------------------------------------------------
// Constructor / ElementType -- the ctor stores the passed element reference, and
// `ElementType()` returns it (pointer-identity).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, ConstructorStoresElement)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ByReferenceTypeReference ref(element);
    EXPECT_EQ(ref.ElementType(), element);
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: `Resolve` produces a `ByReferenceType` wrapping the
// element's resolved `IType`. Pinned by `Kind()` (`ByReference`) and `Name()`
// (delegated to the element `IType`, here `KnownType(Int32)` -> "Int32").
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, ResolveProducesByReferenceType)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ByReferenceTypeReference ref(
        std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::ByReference);
    EXPECT_EQ(result.Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Resolve -- the C++ port caches the resolved `ByReferenceType` (the returned
// `const IType&` must outlive the call), so two calls return the SAME `IType`
// (pointer-identity). This verifies the caching is in place (the C# creates a new
// `ByReferenceType` each call; the C++ reference-return requires the cache).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, ResolveIsCachedAcrossCalls)
{
    ByReferenceTypeReference ref(
        std::make_shared<TestTypeReference>(
            std::make_shared<KnownType>(KnownTypeCode::Int32)));
    TestResolveContext ctx;
    const IType& first = ref.Resolve(ctx);
    const IType& second = ref.Resolve(ctx);
    EXPECT_EQ(&first, &second);
}

// ---------------------------------------------------------------------------
// Resolve -- dispatches polymorphically through an `ITypeReference*` (the dynamic
// dispatch the type-system paths rely on: a type reference is held as an
// `ITypeReference` and the resolved `IType` reached through the base pointer).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Object);
    const ITypeReference* base =
        new ByReferenceTypeReference(std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    EXPECT_EQ(base->Resolve(ctx).Kind(), TypeKind::ByReference);
    EXPECT_EQ(base->Resolve(ctx).Name(), "Object");
    delete base;
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- the identity hash of the element reference XORed with
// the by-reference salt. `std::hash<shared_ptr<T>>` hashes the stored pointer (the
// identity hash), faithful to the C# default reference-type `GetHashCode`.
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, GetHashCodeForInterningXorsIdentityHashWithSalt)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ByReferenceTypeReference ref(element);
    const int expected =
        std::hash<std::shared_ptr<const ITypeReference>>{}(element) ^ kByRefSalt;
    EXPECT_EQ(ref.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `true` for two references wrapping the SAME
// element (reference equality on the element, the C# `this.elementType ==
// brt.elementType`).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, EqualsForInterningReturnsTrueForSameElement)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ByReferenceTypeReference a(element);
    ByReferenceTypeReference b(element);
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping DIFFERENT
// elements (same element TYPE, distinct element INSTANCES -- the identity
// comparison distinguishes them, the `InterningProvider` keeps both).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentElement)
{
    auto intTypeA = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intTypeB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ByReferenceTypeReference a(std::make_shared<TestTypeReference>(intTypeA));
    ByReferenceTypeReference b(std::make_shared<TestTypeReference>(intTypeB));
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` when `other` is a DIFFERENT
// `ISupportsInterning` subtype. A `ByReferenceTypeReference` compared against a
// `PointerTypeReference` (the structural twin) returns `false` regardless of the
// element, because the `dynamic_cast` to `ByReferenceTypeReference` fails (the C#
// `other as ByReferenceTypeReference`-returns-null case). The load-bearing
// type-discrimination the `InterningProvider`'s correctness relies on.
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ByReferenceTypeReference byRef(element);
    PointerTypeReference ptr(element);
    EXPECT_FALSE(byRef.EqualsForInterning(ptr));
    EXPECT_FALSE(ptr.EqualsForInterning(byRef));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- dispatches polymorphically through an
// `ISupportsInterning*` (the `InterningProvider`'s bucketing comparer holds both
// candidates as `ISupportsInterning*` and calls `EqualsForInterning` through the
// base).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, EqualsForInterningDispatchesPolymorphically)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ByReferenceTypeReference a(element);
    ByReferenceTypeReference b(element);
    const ISupportsInterning* baseA = &a;
    const ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// ByReferenceTypeReference is `final` (the C# `sealed`), derives from BOTH
// `ITypeReference` and `ISupportsInterning`, is polymorphic, and has a virtual
// destructor (for abstract-base deletion through either base).
// ---------------------------------------------------------------------------
TEST(ByReferenceTypeReferenceTest, IsFinalAndDerivesFromBothBases)
{
    static_assert(std::is_final_v<ByReferenceTypeReference>,
        "ByReferenceTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, ByReferenceTypeReference>,
        "ByReferenceTypeReference must derive from ITypeReference.");
    static_assert(std::is_base_of_v<ISupportsInterning, ByReferenceTypeReference>,
        "ByReferenceTypeReference must derive from ISupportsInterning.");
    static_assert(std::has_virtual_destructor_v<ByReferenceTypeReference>,
        "ByReferenceTypeReference must have a virtual destructor.");
    static_assert(std::is_polymorphic_v<ByReferenceTypeReference>,
        "ByReferenceTypeReference must be polymorphic.");
    // Upcasts to both bases.
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto ref = std::make_unique<ByReferenceTypeReference>(
        std::make_shared<TestTypeReference>(intType));
    const ITypeReference* asRef = ref.get();
    const ISupportsInterning* asInterning = ref.get();
    EXPECT_NE(asRef, nullptr);
    EXPECT_NE(asInterning, nullptr);
    ref.reset(); // runs the derived destructor through the virtual dtor
    SUCCEED();
}
