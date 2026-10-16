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

// Tests for `PointerTypeReference` (cpp/Decompiler/TypeSystem/PointerTypeReference.hpp,
// the port of the `PointerTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/PointerType.cs). It is the structural twin of
// `ByReferenceTypeReference` (the `*` vs `&` unmanaged-vs-managed-pointer pair): the
// two classes are line-for-line identical except for the produced `IType`
// (`PointerType` vs `ByReferenceType`), the salt constant (`91725812` vs
// `91725814`), and the deferred `ToString` suffix. A `PointerTypeReference` wraps
// an `ITypeReference` element and resolves to a `PointerType` wrapping the element's
// resolved `IType` (the unmanaged `T*` pointer shape).

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

// A minimal `ICompilation` backing the `TestResolveContext` (identical to the
// `ByReferenceTypeReference_Test.cpp` stub -- the element's `Resolve` ignores the
// context, so `FindType` is never called, but the context must still be a valid
// concrete `ITypeResolveContext`).
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

// A trivial concrete `ITypeResolveContext` (identical to the
// `ByReferenceTypeReference_Test.cpp` stub).
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

// A concrete `ITypeReference` backing the `PointerTypeReference` element: holds a
// `shared_ptr<IType>` and `Resolve` returns `*element_` (ignoring the context).
// The element is `shared_ptr`-owned, so `IType::shared_from_this()` is valid when
// `PointerTypeReference::Resolve` obtains an `ITypePtr` to it (the D406
// `enable_shared_from_this<IType>` bridge).
class TestTypeReference : public ITypeReference {
public:
    explicit TestTypeReference(ITypePtr element) : element_(std::move(element)) {}

    const IType& Resolve(const ITypeResolveContext&) const override { return *element_; }

private:
    ITypePtr element_;
};

// The C# `PointerTypeReference` interning salt constant (the
// `elementType.GetHashCode() ^ 91725812` operand).
constexpr int kPointerSalt = 91725812;

} // namespace

// ---------------------------------------------------------------------------
// Constructor / ElementType -- the ctor stores the passed element reference, and
// `ElementType()` returns it (pointer-identity).
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, ConstructorStoresElement)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    PointerTypeReference ref(element);
    EXPECT_EQ(ref.ElementType(), element);
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: `Resolve` produces a `PointerType` wrapping the element's
// resolved `IType`. Pinned by `Kind()` (`Pointer`) and `Name()` (delegated to the
// element `IType`, here `KnownType(Int32)` -> "Int32").
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, ResolveProducesPointerType)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    PointerTypeReference ref(
        std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Pointer);
    EXPECT_EQ(result.Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Resolve -- the C++ port caches the resolved `PointerType` (the returned
// `const IType&` must outlive the call), so two calls return the SAME `IType`
// (pointer-identity).
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, ResolveIsCachedAcrossCalls)
{
    PointerTypeReference ref(
        std::make_shared<TestTypeReference>(
            std::make_shared<KnownType>(KnownTypeCode::Int32)));
    TestResolveContext ctx;
    const IType& first = ref.Resolve(ctx);
    const IType& second = ref.Resolve(ctx);
    EXPECT_EQ(&first, &second);
}

// ---------------------------------------------------------------------------
// Resolve -- dispatches polymorphically through an `ITypeReference*`.
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Object);
    const ITypeReference* base =
        new PointerTypeReference(std::make_shared<TestTypeReference>(intType));
    TestResolveContext ctx;
    EXPECT_EQ(base->Resolve(ctx).Kind(), TypeKind::Pointer);
    EXPECT_EQ(base->Resolve(ctx).Name(), "Object");
    delete base;
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- the identity hash of the element reference XORed with
// the pointer salt.
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, GetHashCodeForInterningXorsIdentityHashWithSalt)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    PointerTypeReference ref(element);
    const int expected =
        std::hash<std::shared_ptr<const ITypeReference>>{}(element) ^ kPointerSalt;
    EXPECT_EQ(ref.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `true` for two references wrapping the SAME
// element (reference equality on the element).
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, EqualsForInterningReturnsTrueForSameElement)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    PointerTypeReference a(element);
    PointerTypeReference b(element);
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping DIFFERENT
// elements (same element TYPE, distinct element INSTANCES).
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentElement)
{
    auto intTypeA = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intTypeB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    PointerTypeReference a(std::make_shared<TestTypeReference>(intTypeA));
    PointerTypeReference b(std::make_shared<TestTypeReference>(intTypeB));
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` when `other` is a DIFFERENT
// `ISupportsInterning` subtype. A `PointerTypeReference` compared against a
// `ByReferenceTypeReference` (the structural twin) returns `false` regardless of
// the element, because the `dynamic_cast` to `PointerTypeReference` fails.
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    PointerTypeReference ptr(element);
    ByReferenceTypeReference byRef(element);
    EXPECT_FALSE(ptr.EqualsForInterning(byRef));
    EXPECT_FALSE(byRef.EqualsForInterning(ptr));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- dispatches polymorphically through an
// `ISupportsInterning*`.
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, EqualsForInterningDispatchesPolymorphically)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto element = std::make_shared<TestTypeReference>(intType);
    PointerTypeReference a(element);
    PointerTypeReference b(element);
    const ISupportsInterning* baseA = &a;
    const ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// PointerTypeReference is `final` (the C# `sealed`), derives from BOTH
// `ITypeReference` and `ISupportsInterning`, is polymorphic, and has a virtual
// destructor.
// ---------------------------------------------------------------------------
TEST(PointerTypeReferenceTest, IsFinalAndDerivesFromBothBases)
{
    static_assert(std::is_final_v<PointerTypeReference>,
        "PointerTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, PointerTypeReference>,
        "PointerTypeReference must derive from ITypeReference.");
    static_assert(std::is_base_of_v<ISupportsInterning, PointerTypeReference>,
        "PointerTypeReference must derive from ISupportsInterning.");
    static_assert(std::has_virtual_destructor_v<PointerTypeReference>,
        "PointerTypeReference must have a virtual destructor.");
    static_assert(std::is_polymorphic_v<PointerTypeReference>,
        "PointerTypeReference must be polymorphic.");
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto ref = std::make_unique<PointerTypeReference>(
        std::make_shared<TestTypeReference>(intType));
    const ITypeReference* asRef = ref.get();
    const ISupportsInterning* asInterning = ref.get();
    EXPECT_NE(asRef, nullptr);
    EXPECT_NE(asInterning, nullptr);
    ref.reset(); // runs the derived destructor through the virtual dtor
    SUCCEED();
}
