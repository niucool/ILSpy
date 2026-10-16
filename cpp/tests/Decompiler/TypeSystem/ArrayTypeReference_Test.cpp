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

// Tests for `ArrayTypeReference` (cpp/Decompiler/TypeSystem/ArrayTypeReference.hpp,
// the port of the `ArrayTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/ArrayType.cs). It is the fourth concrete
// `ITypeReference` (after `KnownTypeReference` D411, `ByReferenceTypeReference`
// D413, `PointerTypeReference` D414), the third to derive from BOTH `ITypeReference`
// (D408) and `ISupportsInterning` (D412). An `ArrayTypeReference` wraps an
// `ITypeReference` element plus a rank (dimensions) and resolves to an `ArrayType`
// of that rank wrapping the element's resolved `IType` (the `T[]` / `T[,]` /
// `T[,,]` array shape). It is structurally the D413/D414 siblings plus a
// `dimensions` int field: the interning hash XORs the element's identity hash
// with the dimensions (NOT a fixed salt), and the interning equality compares
// both the element and the dimensions.

#include "Decompiler/TypeSystem/ArrayTypeReference.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
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

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ArrayTypeReference;
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
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A minimal `ICompilation` backing the `TestResolveContext`: overrides the nine
// `ICompilation` pure-virtuals with trivial returns. `ArrayTypeReference::Resolve`
// delegates to the element's `Resolve`, which (for the test's
// `TestTypeReference`) IGNORES the context, so `FindType` is never called -- but
// the context must still be a valid concrete `ITypeResolveContext` (it threads
// `ICompilationProvider::Compilation`).
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

// A trivial concrete `ITypeResolveContext` for `ArrayTypeReference::Resolve`:
// overrides all six pure-virtuals with trivial returns backed by the held
// `TestCompilation`. The element's `Resolve` ignores the context, so the
// nullable current-module / -type / -member slots return null and the `With*`
// factories return null `unique_ptr`s (never called by this test).
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

// A concrete `ITypeReference` backing the `ArrayTypeReference` element: it holds
// a `shared_ptr<IType>` and `Resolve` returns `*element_` (ignoring the context).
// The element is `shared_ptr`-owned (constructed via `make_shared`), so
// `IType::shared_from_this()` is valid when `ArrayTypeReference::Resolve` obtains
// an `ITypePtr` to it -- the load-bearing shape the `Resolve` test relies on (the
// D406 `enable_shared_from_this<IType>` bridge).
class TestTypeReference : public ITypeReference {
public:
    explicit TestTypeReference(ITypePtr element) : element_(std::move(element)) {}

    const IType& Resolve(const ITypeResolveContext&) const override { return *element_; }

private:
    ITypePtr element_;
};

} // namespace

// ---------------------------------------------------------------------------
// Constructor / ElementType / Dimensions -- the ctor stores the passed element
// reference and dimensions, `ElementType()` returns the element (pointer-identity),
// and `Dimensions()` returns the rank. The default rank is 1 (the C# default).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, ConstructorStoresElementAndDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference ref(element);
    EXPECT_EQ(ref.ElementType(), element);
    EXPECT_EQ(ref.Dimensions(), 1);
}

// ---------------------------------------------------------------------------
// Constructor / Dimensions -- an explicit rank is stored verbatim.
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, ConstructorStoresExplicitDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ArrayTypeReference ref(std::make_shared<TestTypeReference>(intType), 3);
    EXPECT_EQ(ref.Dimensions(), 3);
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: `Resolve` produces an `ArrayType` of the given rank
// wrapping the element's resolved `IType`. Pinned by `Kind()` (`Array`),
// `Dimensions()` (the rank, via a downcast), and `Name()` (delegated to the
// element `IType`, here `KnownType(Int32)` -> "Int32").
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, ResolveProducesArrayType)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ArrayTypeReference ref(std::make_shared<TestTypeReference>(intType), 2);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Array);
    const auto* array = dynamic_cast<const ArrayType*>(&result);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->Rank(), 2);
    EXPECT_EQ(result.Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Resolve -- a 1-D reference (the default rank) produces an `ArrayType` with
// rank 1.
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, ResolveProducesOneDimensionalArray)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ArrayTypeReference ref(std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Array);
    const auto* array = dynamic_cast<const ArrayType*>(&result);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->Rank(), 1);
}

// ---------------------------------------------------------------------------
// Resolve -- the C++ port caches the resolved `ArrayType` (the returned
// `const IType&` must outlive the call), so two calls return the SAME `IType`
// (pointer-identity). This verifies the caching is in place (the C# creates a
// new `ArrayType` each call; the C++ reference-return requires the cache).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, ResolveIsCachedAcrossCalls)
{
    ArrayTypeReference ref(std::make_shared<TestTypeReference>(
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
TEST(ArrayTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Object);
    const ITypeReference* base =
        new ArrayTypeReference(std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    EXPECT_EQ(base->Resolve(ctx).Kind(), TypeKind::Array);
    EXPECT_EQ(base->Resolve(ctx).Name(), "Object");
    delete base;
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- the identity hash of the element reference XORed
// with the dimensions. `std::hash<shared_ptr<T>>` hashes the stored pointer (the
// identity hash), faithful to the C# default reference-type `GetHashCode`. The
// dimensions (NOT a fixed salt) is the structural distinction from the D413/D414
// twins.
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, GetHashCodeForInterningXorsIdentityHashWithDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference ref(element, 2);
    const int expected =
        std::hash<std::shared_ptr<const ITypeReference>>{}(element) ^ 2;
    EXPECT_EQ(ref.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- a different rank produces a different hash (the
// dimensions participates in the hash, so the `InterningProvider` buckets same-
// rank references together and separates different-rank references).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, GetHashCodeForInterningDistinguishesDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference ref1(element, 1);
    ArrayTypeReference ref2(element, 2);
    EXPECT_NE(ref1.GetHashCodeForInterning(), ref2.GetHashCodeForInterning());
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `true` for two references wrapping the SAME
// element with the SAME rank (reference equality on the element, value equality
// on the dimensions, the C# `elementType == o.elementType && dimensions ==
// o.dimensions`).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, EqualsForInterningReturnsTrueForSameElementAndDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference a(element, 2);
    ArrayTypeReference b(element, 2);
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping DIFFERENT
// elements (same element TYPE, distinct element INSTANCES, same rank -- the
// identity comparison distinguishes them).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentElement)
{
    auto intTypeA = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intTypeB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ArrayTypeReference a(std::make_shared<TestTypeReference>(intTypeA), 1);
    ArrayTypeReference b(std::make_shared<TestTypeReference>(intTypeB), 1);
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping the SAME
// element with DIFFERENT ranks (the dimensions comparison distinguishes them;
// this is the load-bearing `dimensions`-driven distinction the D413/D414 twins
// do not have).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentDimensions)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference a(element, 1);
    ArrayTypeReference b(element, 2);
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` when `other` is a DIFFERENT
// `ISupportsInterning` subtype. An `ArrayTypeReference` compared against a
// `ByReferenceTypeReference` returns `false` regardless of the element, because
// the `dynamic_cast` to `ArrayTypeReference` fails (the C# `other as
// ArrayTypeReference`-returns-null case). The load-bearing type-discrimination
// the `InterningProvider`'s correctness relies on.
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference arr(element, 1);
    ByReferenceTypeReference byRef(element);
    EXPECT_FALSE(arr.EqualsForInterning(byRef));
    EXPECT_FALSE(byRef.EqualsForInterning(arr));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- dispatches polymorphically through an
// `ISupportsInterning*` (the `InterningProvider`'s bucketing comparer holds both
// candidates as `ISupportsInterning*` and calls `EqualsForInterning` through the
// base).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, EqualsForInterningDispatchesPolymorphically)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    ArrayTypeReference a(element, 2);
    ArrayTypeReference b(element, 2);
    const ISupportsInterning* baseA = &a;
    const ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// ArrayTypeReference is `final` (the C# `sealed`), derives from BOTH
// `ITypeReference` and `ISupportsInterning`, is polymorphic, and has a virtual
// destructor (for abstract-base deletion through either base).
// ---------------------------------------------------------------------------
TEST(ArrayTypeReferenceTest, IsFinalAndDerivesFromBothBases)
{
    static_assert(std::is_final_v<ArrayTypeReference>,
        "ArrayTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, ArrayTypeReference>,
        "ArrayTypeReference must derive from ITypeReference.");
    static_assert(std::is_base_of_v<ISupportsInterning, ArrayTypeReference>,
        "ArrayTypeReference must derive from ISupportsInterning.");
    static_assert(std::has_virtual_destructor_v<ArrayTypeReference>,
        "ArrayTypeReference must have a virtual destructor.");
    static_assert(std::is_polymorphic_v<ArrayTypeReference>,
        "ArrayTypeReference must be polymorphic.");
    // Upcasts to both bases.
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto ref = std::make_unique<ArrayTypeReference>(
        std::make_shared<TestTypeReference>(intType));
    const ITypeReference* asRef = ref.get();
    const ISupportsInterning* asInterning = ref.get();
    EXPECT_NE(asRef, nullptr);
    EXPECT_NE(asInterning, nullptr);
    ref.reset(); // runs the derived destructor through the virtual dtor
    SUCCEED();
}
