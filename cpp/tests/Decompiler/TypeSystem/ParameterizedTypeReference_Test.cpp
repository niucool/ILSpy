// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including, without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Tests for `ParameterizedTypeReference` (cpp/Decompiler/TypeSystem/
// ParameterizedTypeReference.hpp, the port of the `ParameterizedTypeReference`
// class from ICSharpCode.Decompiler/TypeSystem/ParameterizedType.cs). It is the
// fifth concrete `ITypeReference` (after `KnownTypeReference` D411,
// `ByReferenceTypeReference` D413, `PointerTypeReference` D414,
// `ArrayTypeReference` D415), the fourth to derive from BOTH `ITypeReference`
// (D408) and `ISupportsInterning` (D412). A `ParameterizedTypeReference` wraps a
// generic-type reference plus a type-argument-reference vector and resolves to a
// `ParameterizedType` of the resolved base type with the resolved type arguments
// (the `List<string>` reference shape). Its load-bearing cruxes (absent from the
// single-element D413/D414/D415 references) are: the `tpc == 0` early return (a
// non-generic base type is returned unchanged, NOT wrapped); the
// `UnknownType` fill for missing arguments past the reference's own list; and the
// per-element reference-inequality in the interning equality.

#include "Decompiler/TypeSystem/ArrayTypeReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"
#include "Decompiler/TypeSystem/ParameterizedTypeReference.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
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

using ILSpy::Decompiler::TypeSystem::ArrayTypeReference;
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
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedTypeReference;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A minimal `ICompilation` backing the `TestResolveContext`: overrides the nine
// `ICompilation` pure-virtuals with trivial returns. `ParameterizedTypeReference::
// Resolve` delegates to the generic-type and type-argument references' `Resolve`,
// which (for the test's `TestTypeReference`) IGNORES the context, so `FindType` is
// never called -- but the context must still be a valid concrete
// `ITypeResolveContext` (it threads `ICompilationProvider::Compilation`).
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

// A trivial concrete `ITypeResolveContext` for `ParameterizedTypeReference::
// Resolve`: overrides all six pure-virtuals with trivial returns backed by the
// held `TestCompilation`. The references' `Resolve` ignore the context, so the
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

// A concrete `ITypeReference` backing the `ParameterizedTypeReference`
// generic-type and type-argument slots: it holds an `ITypePtr` and `Resolve`
// returns `*element_` (ignoring the context). The element is `shared_ptr`-owned
// (constructed via `make_shared`), so `IType::shared_from_this()` is valid when
// `ParameterizedTypeReference::Resolve` obtains an `ITypePtr` to it -- the
// load-bearing shape the `Resolve` test relies on (the D406
// `enable_shared_from_this<IType>` bridge).
class TestTypeReference : public ITypeReference {
public:
    explicit TestTypeReference(ITypePtr element) : element_(std::move(element)) {}

    const IType& Resolve(const ITypeResolveContext&) const override { return *element_; }

private:
    ITypePtr element_;
};

// Builds a `SimpleType` with the given fully-qualified name and type-parameter
// count -- the minimal-port `IType` whose `TypeParameterCount()` returns the
// configured count, used to exercise the `tpc > 0` paths (the `KnownType` table
// has no 2-arity entry, so a `SimpleType` is needed for the `tpc == 2` fill
// test).
std::shared_ptr<SimpleType> MakeGenericType(const std::string& ns,
                                            const std::string& name, int tpc)
{
    return std::make_shared<SimpleType>(TopLevelTypeName(ns, name, tpc));
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / GenericType / TypeArguments -- the ctor stores the passed
// generic-type reference and type-argument vector, `GenericType()` returns the
// generic (pointer-identity), and `TypeArguments()` returns the vector.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ConstructorStoresGenericAndTypeArguments)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    std::vector<std::shared_ptr<const ITypeReference>> args = {
        std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32))
    };
    ParameterizedTypeReference ref(generic, args);
    EXPECT_EQ(ref.GenericType(), generic);
    EXPECT_EQ(ref.TypeArguments().size(), 1u);
    EXPECT_EQ(ref.TypeArguments()[0], args[0]);
}

// ---------------------------------------------------------------------------
// Constructor -- an empty type-argument vector is allowed (the resolved base
// type with `tpc > 0` then fills every slot with `UnknownType`).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ConstructorAllowsEmptyTypeArguments)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    ParameterizedTypeReference ref(generic, {});
    EXPECT_TRUE(ref.TypeArguments().empty());
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: when the resolved base type has `TypeParameterCount ==
// 0`, the bare base type is returned unchanged (NOT wrapped in a
// `ParameterizedType`). Pinned by `dynamic_cast` to `ParameterizedType`
// returning null and pointer-identity with the generic's resolved `IType`.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveReturnsBaseTypeWhenTypeParameterCountIsZero)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto generic = std::make_shared<TestTypeReference>(intType);
    ParameterizedTypeReference ref(generic, {});
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(&result, intType.get());
    EXPECT_EQ(dynamic_cast<const ParameterizedType*>(&result), nullptr);
    EXPECT_EQ(result.TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// Resolve -- when the resolved base type has `TypeParameterCount > 0`, a
// `ParameterizedType` is constructed. Pinned by the downcast succeeding,
// `GenericType()` pointer-identity with the resolved base type, `TypeArguments`
// count matching `tpc`, and `Kind()`/`Name()` delegated to the base type.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveProducesParameterizedType)
{
    auto taskType = std::make_shared<KnownType>(KnownTypeCode::TaskOfT);
    auto generic = std::make_shared<TestTypeReference>(taskType);
    std::vector<std::shared_ptr<const ITypeReference>> args = {
        std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32))
    };
    ParameterizedTypeReference ref(generic, args);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    const auto* param = dynamic_cast<const ParameterizedType*>(&result);
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->GenericType().get(), taskType.get());
    EXPECT_EQ(param->TypeArguments().size(), 1u);
    EXPECT_EQ(param->TypeParameterCount(), 1);
    EXPECT_EQ(result.Kind(), TypeKind::Class);
    EXPECT_EQ(result.Name(), "Task");
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: when the reference's own type-argument list is shorter
// than the base type's `TypeParameterCount`, the missing slots are filled with
// `UnknownType` (the C# `SpecialType.UnknownType`). Pinned by the count matching
// `tpc` and the trailing slot's `Kind()` being `Unknown`.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveFillsMissingTypeArgumentsWithUnknownType)
{
    auto dictType = MakeGenericType("System.Collections.Generic", "Dictionary", 2);
    auto generic = std::make_shared<TestTypeReference>(dictType);
    std::vector<std::shared_ptr<const ITypeReference>> args = {
        std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32))
    };
    ParameterizedTypeReference ref(generic, args);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    const auto* param = dynamic_cast<const ParameterizedType*>(&result);
    ASSERT_NE(param, nullptr);
    ASSERT_EQ(param->TypeArguments().size(), 2u);
    EXPECT_EQ(param->TypeArguments()[0]->Kind(), TypeKind::Struct);
    EXPECT_EQ(param->TypeArguments()[1]->Kind(), TypeKind::Unknown);
}

// ---------------------------------------------------------------------------
// Resolve -- when the reference's own type-argument list is LONGER than the
// base type's `TypeParameterCount`, the extra arguments are dropped (the C#
// `resolvedTypes` array is `tpc` long, so the loop stops at `tpc`). Pinned by
// the `TypeArguments` count matching `tpc`, not the reference's list length.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveIgnoresExtraTypeArguments)
{
    auto taskType = std::make_shared<KnownType>(KnownTypeCode::TaskOfT);
    auto generic = std::make_shared<TestTypeReference>(taskType);
    std::vector<std::shared_ptr<const ITypeReference>> args = {
        std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32)),
        std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Object))
    };
    ParameterizedTypeReference ref(generic, args);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    const auto* param = dynamic_cast<const ParameterizedType*>(&result);
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->TypeArguments().size(), 1u);
}

// ---------------------------------------------------------------------------
// Resolve -- the C++ port caches the resolved `IType` (the returned
// `const IType&` must outlive the call), so two calls return the SAME `IType`
// (pointer-identity). This verifies the caching is in place (the C# resolves
// the base type and constructs the `ParameterizedType` each call; the C++
// reference-return requires the cache).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveIsCachedAcrossCalls)
{
    auto taskType = std::make_shared<KnownType>(KnownTypeCode::TaskOfT);
    ParameterizedTypeReference ref(
        std::make_shared<TestTypeReference>(taskType),
        {std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32))});
    TestResolveContext ctx;
    const IType& first = ref.Resolve(ctx);
    const IType& second = ref.Resolve(ctx);
    EXPECT_EQ(&first, &second);
}

// ---------------------------------------------------------------------------
// Resolve -- dispatches polymorphically through an `ITypeReference*` (the
// dynamic dispatch the type-system paths rely on: a type reference is held as
// an `ITypeReference` and the resolved `IType` reached through the base
// pointer).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    auto taskType = std::make_shared<KnownType>(KnownTypeCode::TaskOfT);
    const ITypeReference* base = new ParameterizedTypeReference(
        std::make_shared<TestTypeReference>(taskType),
        {std::make_shared<TestTypeReference>(std::make_shared<KnownType>(KnownTypeCode::Int32))});
    TestResolveContext ctx;
    const IType& result = base->Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Class);
    EXPECT_EQ(result.Name(), "Task");
    delete base;
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- the identity hash of the generic-type reference
// seeded, then `*27 +` each type-argument's identity hash (the C#
// `hashCode *= 27; hashCode += t.GetHashCode()` formula). `std::hash<shared_ptr<T>>`
// hashes the stored pointer (the identity hash), faithful to the C# default
// reference-type `GetHashCode`.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, GetHashCodeForInterningMatchesCSharpFormula)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto argA = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto argB = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Object));
    ParameterizedTypeReference ref(generic, {argA, argB});
    int expected = static_cast<int>(
        std::hash<std::shared_ptr<const ITypeReference>>{}(generic));
    expected = expected * 27 + static_cast<int>(
        std::hash<std::shared_ptr<const ITypeReference>>{}(argA));
    expected = expected * 27 + static_cast<int>(
        std::hash<std::shared_ptr<const ITypeReference>>{}(argB));
    EXPECT_EQ(ref.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- a different type-argument vector produces a
// different hash (the type arguments participate in the hash, so the
// `InterningProvider` buckets references with the same generic + arguments
// together and separates those with different arguments).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, GetHashCodeForInterningDistinguishesTypeArguments)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto argA = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto argB = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Object));
    ParameterizedTypeReference refA(generic, {argA});
    ParameterizedTypeReference refB(generic, {argB});
    EXPECT_NE(refA.GetHashCodeForInterning(), refB.GetHashCodeForInterning());
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `true` for two references wrapping the SAME
// generic-type instance with the SAME (reference-identical) type-argument
// instances (the C# `genericType == o.genericType` and the per-element
// `typeArguments[i] == o.typeArguments[i]` reference equality).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, EqualsForInterningReturnsTrueForSameGenericAndTypeArgs)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto arg = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    ParameterizedTypeReference a(generic, {arg});
    ParameterizedTypeReference b(generic, {arg});
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping DIFFERENT
// generic-type instances (the same generic TYPE, distinct INSTANCES -- the
// identity comparison on the generic distinguishes them).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentGeneric)
{
    auto genericA = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto genericB = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto arg = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    ParameterizedTypeReference a(genericA, {arg});
    ParameterizedTypeReference b(genericB, {arg});
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` for two references wrapping
// the SAME generic with DIFFERENT (distinct-instance) type arguments, AND for
// a different argument COUNT. This is the load-bearing per-element
// reference-inequality loop the single-element D413/D414/D415 references do not
// have; without it the interning would collapse distinct argument lists.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentTypeArguments)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto argA = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto argB = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    // Same generic, distinct-instance arguments -> false.
    ParameterizedTypeReference a(generic, {argA});
    ParameterizedTypeReference b(generic, {argB});
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
    // Same generic, different count -> false.
    ParameterizedTypeReference c(generic, {argA, argB});
    EXPECT_FALSE(a.EqualsForInterning(c));
    EXPECT_FALSE(c.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` when `other` is a DIFFERENT
// `ISupportsInterning` subtype. A `ParameterizedTypeReference` compared against
// an `ArrayTypeReference` returns `false` regardless of the contents, because
// the `dynamic_cast` to `ParameterizedTypeReference` fails (the C# `other as
// ParameterizedTypeReference`-returns-null case). The load-bearing
// type-discrimination the `InterningProvider`'s correctness relies on.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto arg = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    ParameterizedTypeReference param(generic, {arg});
    ArrayTypeReference arr(std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32)));
    EXPECT_FALSE(param.EqualsForInterning(arr));
    EXPECT_FALSE(arr.EqualsForInterning(param));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- dispatches polymorphically through an
// `ISupportsInterning*` (the `InterningProvider`'s bucketing comparer holds both
// candidates as `ISupportsInterning*` and calls `EqualsForInterning` through the
// base).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, EqualsForInterningDispatchesPolymorphically)
{
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto arg = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    ParameterizedTypeReference a(generic, {arg});
    ParameterizedTypeReference b(generic, {arg});
    const ISupportsInterning* baseA = &a;
    const ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// ParameterizedTypeReference is `final` (the C# `sealed`), derives from BOTH
// `ITypeReference` and `ISupportsInterning`, is polymorphic, and has a virtual
// destructor (for abstract-base deletion through either base).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReferenceTest, IsFinalAndDerivesFromBothBases)
{
    static_assert(std::is_final_v<ParameterizedTypeReference>,
        "ParameterizedTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, ParameterizedTypeReference>,
        "ParameterizedTypeReference must derive from ITypeReference.");
    static_assert(std::is_base_of_v<ISupportsInterning, ParameterizedTypeReference>,
        "ParameterizedTypeReference must derive from ISupportsInterning.");
    static_assert(std::has_virtual_destructor_v<ParameterizedTypeReference>,
        "ParameterizedTypeReference must have a virtual destructor.");
    static_assert(std::is_polymorphic_v<ParameterizedTypeReference>,
        "ParameterizedTypeReference must be polymorphic.");
    // Upcasts to both bases.
    auto generic = std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::TaskOfT));
    auto ref = std::make_unique<ParameterizedTypeReference>(generic,
        std::vector<std::shared_ptr<const ITypeReference>>{});
    const ITypeReference* asRef = ref.get();
    const ISupportsInterning* asInterning = ref.get();
    EXPECT_NE(asRef, nullptr);
    EXPECT_NE(asInterning, nullptr);
    ref.reset(); // runs the derived destructor through the virtual dtor
    SUCCEED();
}
