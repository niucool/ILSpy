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

// Tests for `NestedTypeReference` (cpp/Decompiler/TypeSystem/NestedTypeReference.hpp,
// the port of the `NestedTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/Implementation/NestedTypeReference.cs). It is
// the sixth concrete `ITypeReference` (after `KnownTypeReference` D411,
// `ByReferenceTypeReference` D413, `PointerTypeReference` D414,
// `ArrayTypeReference` D415, `ParameterizedTypeReference` D416), the fifth to
// derive from BOTH `ITypeReference` (D408) and `ISupportsInterning` (D412). A
// `NestedTypeReference` wraps a declaring-type reference, a nested name, and an
// additional type-parameter count; `Resolve` searches the declaring type's
// `NestedTypes` for a match by name + total `TypeParameterCount`, falling back to
// an `UnknownType(null, name, tpc)` when no match is found.

#include "Decompiler/TypeSystem/ArrayTypeReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NestedTypeReference.hpp"
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

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayTypeReference;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
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
using ILSpy::Decompiler::TypeSystem::NestedTypeReference;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using ILSpy::Decompiler::TypeSystem::UnknownType;

// A minimal `ICompilation` backing the `TestResolveContext`: overrides the nine
// `ICompilation` pure-virtuals with trivial returns. `NestedTypeReference::Resolve`
// delegates to the declaring type reference's `Resolve` (which returns a
// `TestTypeDefinition`), so `FindType` is never called -- but the context must
// still be a valid concrete `ITypeResolveContext` (it threads
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

// A trivial concrete `ITypeResolveContext` for `NestedTypeReference::Resolve`:
// overrides all six pure-virtuals with trivial returns backed by the held
// `TestCompilation`. The declaring type reference's `Resolve` ignores the
// context (returns a `TestTypeDefinition`), so the nullable current-module /
// -type / -member slots return null and the `With*` factories return null
// `unique_ptr`s (never called by this test).
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

// A concrete `ITypeReference` backing the `NestedTypeReference`'s declaring type:
// it holds a `shared_ptr<IType>` and `Resolve` returns `*element_` (ignoring the
// context). The element is `shared_ptr`-owned (constructed via `make_shared`), so
// `IType::shared_from_this()` is valid when consumers need an `ITypePtr` to it.
class TestTypeReference : public ITypeReference {
public:
    explicit TestTypeReference(ITypePtr element) : element_(std::move(element)) {}

    const IType& Resolve(const ITypeResolveContext&) const override { return *element_; }

private:
    ITypePtr element_;
};

// A compact concrete `ITypeDefinition` for testing `NestedTypeReference::Resolve`:
// overrides every `ITypeDefinition` / `ITypeDefinitionOrUnknown` / `IEntity` /
// `ICompilationProvider` / `INamedElement` / `ISymbol` pure-virtual with a trivial
// return. `Name()` / `ReflectionName()` / `TypeParameterCount()` derive from a
// stored `TopLevelTypeName` (the minimal-port convention). `NestedTypes()` returns
// a configurable snapshot so the `Resolve` search can be exercised (the
// load-bearing accessor for this test). The return types `SymbolKind` /
// `KnownTypeCode` / `FullTypeName` / `ExtensionInfo` / `Accessibility` /
// `Nullability` are GLOBALLY QUALIFIED because the stub INHERITS the same-named
// member functions, which hide the namespace-scope enums/classes in the stub's
// class body (the D372 cross-scope name-hiding crux).
class TestTypeDefinition : public ITypeDefinition {
public:
    TestTypeDefinition(std::string ns, std::string name, int tpc,
                       const TestCompilation& compilation)
        : topLevel_(std::move(ns), std::move(name), tpc),
          fullTypeName_(topLevel_), compilation_(compilation) {}

    // --- IType (inherited unambiguously; only Name/ReflectionName redeclared in ITypeDefinition) ---
    TypeKind Kind() const override { return TypeKind::Class; }
    std::string Name() const override { return topLevel_.Name(); }
    std::string ReflectionName() const override { return topLevel_.ReflectionName(); }
    int TypeParameterCount() const override { return topLevel_.TypeParameterCount(); }

    // --- ITypeDefinitionOrUnknown ---
    const ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override { return fullTypeName_; }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // --- INamedElement ---
    std::string FullName() const override { return topLevel_.Name(); }
    std::string Namespace() const override { return topLevel_.Namespace(); }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- ITypeDefinition-own ---
    std::vector<const ITypeDefinition*> NestedTypes() const override { return nestedTypes_; }
    std::vector<const IMember*> Members() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> Fields() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> Properties() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> Events() const override
    {
        return {};
    }
    ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override
    {
        return ILSpy::Decompiler::TypeSystem::KnownTypeCode::None;
    }
    ITypePtr EnumUnderlyingType() const override { return {}; }
    bool IsReadOnly() const override { return false; }
    std::string MetadataName() const override { return topLevel_.Name(); }
    bool HasExtensions() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const override
    {
        return ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    bool IsRecord() const override { return false; }

    // Test wiring: set the NestedTypes snapshot after construction.
    void SetNestedTypes(std::vector<const ITypeDefinition*> v) { nestedTypes_ = std::move(v); }

protected:
    bool StructuralEquals(const IType& other) const override
    {
        const auto& o = static_cast<const TestTypeDefinition&>(other);
        return topLevel_ == o.topLevel_;
    }

private:
    TopLevelTypeName topLevel_;
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    const TestCompilation& compilation_;
    std::vector<const ITypeDefinition*> nestedTypes_;
};

} // namespace

// ---------------------------------------------------------------------------
// Constructor / DeclaringTypeReference / Name / AdditionalTypeParameterCount --
// the ctor stores the passed declaring-type reference, name, and
// additionalTypeParameterCount; the accessors return them by
// pointer-identity / value.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ConstructorStoresFields)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Inner", 0);
    EXPECT_EQ(ref.DeclaringTypeReference(), declaringRef);
    EXPECT_EQ(ref.Name(), "Inner");
    EXPECT_EQ(ref.AdditionalTypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: when the declaring type has a nested type whose Name
// matches and whose TypeParameterCount equals the declaring type's tpc plus the
// additionalTypeParameterCount, `Resolve` returns that nested type (by reference,
// owned by the declaring type definition). Pinned by `Kind()`, `Name()`, and
// pointer-identity with the nested type definition.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFindsMatchingNestedType)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto inner = std::make_shared<TestTypeDefinition>("System", "Inner", 0, comp);
    outer->SetNestedTypes({inner.get()});
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Inner", 0);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Class);
    EXPECT_EQ(result.Name(), "Inner");
    EXPECT_EQ(&result, static_cast<const IType*>(inner.get()));
}

// ---------------------------------------------------------------------------
// Resolve -- the match is by BOTH name AND total TypeParameterCount. A nested
// type with the right name but the WRONG TypeParameterCount is NOT matched; the
// search falls through to the UnknownType fallback. Here the declaring type has
// tpc=0 and the nested type has tpc=2, but the reference asks for
// additionalTypeParameterCount=0 (so the expected total is 0+0=0); the nested
// type's tpc=2 does NOT match.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFallsBackWhenTypeParameterCountDoesNotMatch)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto inner = std::make_shared<TestTypeDefinition>("System", "Inner", 2, comp);
    outer->SetNestedTypes({inner.get()});
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Inner", 0);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "Inner");
    EXPECT_EQ(result.TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// Resolve -- the match by total TypeParameterCount: the declaring type has
// tpc=1 and the nested type has tpc=3, the reference asks for
// additionalTypeParameterCount=2 (so the expected total is 1+2=3); the nested
// type's tpc=3 MATCHES.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveMatchesByTotalTypeParameterCount)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 1, comp);
    auto inner = std::make_shared<TestTypeDefinition>("System", "Inner", 3, comp);
    outer->SetNestedTypes({inner.get()});
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Inner", 2);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Name(), "Inner");
    EXPECT_EQ(result.TypeParameterCount(), 3);
    EXPECT_EQ(&result, static_cast<const IType*>(inner.get()));
}

// ---------------------------------------------------------------------------
// Resolve -- when the nested name does NOT match any nested type, `Resolve`
// falls back to an `UnknownType(null, name, additionalTypeParameterCount)`. The
// fallback has Kind=Unknown, the known name, and the known tpc.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFallsBackToUnknownTypeWhenNameNotFound)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto inner = std::make_shared<TestTypeDefinition>("System", "Inner", 0, comp);
    outer->SetNestedTypes({inner.get()});
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Nonexistent", 0);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "Nonexistent");
    EXPECT_EQ(result.TypeParameterCount(), 0);
    EXPECT_EQ(result.ReflectionName(), "?");
}

// ---------------------------------------------------------------------------
// Resolve -- the fallback UnknownType carries the additionalTypeParameterCount
// when the nested type is not found.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFallsBackWithAdditionalTypeParameterCount)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Missing", 2);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "Missing");
    EXPECT_EQ(result.TypeParameterCount(), 2);
}

// ---------------------------------------------------------------------------
// Resolve -- when the declaring type is NOT an ITypeDefinition (the
// dynamic_cast fails), the search is skipped and the UnknownType fallback is
// returned. Here the declaring type reference resolves to a KnownType (not an
// ITypeDefinition), so the dynamic_cast yields null.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFallsBackWhenDeclaringTypeIsNotTypeDefinition)
{
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto declaringRef = std::make_shared<TestTypeReference>(intType);
    NestedTypeReference ref(declaringRef, "Inner", 0);
    TestResolveContext ctx;
    const IType& result = ref.Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "Inner");
    EXPECT_EQ(result.TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// Resolve -- the fallback UnknownType is cached: two `Resolve` calls that reach
// the fallback return the SAME `IType` (pointer-identity). The C# creates a new
// UnknownType each call; the C++ reference-return requires the cache.
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveFallbackIsCachedAcrossCalls)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Missing", 0);
    TestResolveContext ctx;
    const IType& first = ref.Resolve(ctx);
    const IType& second = ref.Resolve(ctx);
    EXPECT_EQ(&first, &second);
}

// ---------------------------------------------------------------------------
// Resolve -- dispatches polymorphically through an `ITypeReference*` (the dynamic
// dispatch the type-system paths rely on).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto inner = std::make_shared<TestTypeDefinition>("System", "Inner", 0, comp);
    outer->SetNestedTypes({inner.get()});
    const ITypeReference* base =
        new NestedTypeReference(std::make_shared<TestTypeReference>(outer), "Inner", 0);
    TestResolveContext ctx;
    EXPECT_EQ(base->Resolve(ctx).Name(), "Inner");
    delete base;
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- the identity hash of the declaring-type reference
// XORed with the string hash of the name and the
// additionalTypeParameterCount (the C# `declaringTypeRef.GetHashCode() ^
// name.GetHashCode() ^ additionalTypeParameterCount`).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, GetHashCodeForInterningXorsAllThree)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref(declaringRef, "Inner", 2);
    const int expected =
        static_cast<int>(
            std::hash<std::shared_ptr<const ITypeReference>>{}(declaringRef))
        ^ static_cast<int>(std::hash<std::string>{}("Inner"))
        ^ 2;
    EXPECT_EQ(ref.GetHashCodeForInterning(), expected);
}

// ---------------------------------------------------------------------------
// GetHashCodeForInterning -- a different name or additionalTypeParameterCount
// produces a different hash (both participate in the hash, so the
// InterningProvider buckets them separately).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, GetHashCodeForInterningDistinguishesNameAndCount)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference ref1(declaringRef, "Inner", 0);
    NestedTypeReference ref2(declaringRef, "Inner", 1);
    NestedTypeReference ref3(declaringRef, "Other", 0);
    EXPECT_NE(ref1.GetHashCodeForInterning(), ref2.GetHashCodeForInterning());
    EXPECT_NE(ref1.GetHashCodeForInterning(), ref3.GetHashCodeForInterning());
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `true` for two references wrapping the SAME
// declaring type, with the SAME name, count, and isReferenceType (reference
// equality on the declaring type, value equality on the name/count/optional).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningReturnsTrueForSameFields)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference a(declaringRef, "Inner", 0);
    NestedTypeReference b(declaringRef, "Inner", 0);
    EXPECT_TRUE(a.EqualsForInterning(b));
    EXPECT_TRUE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references wrapping DIFFERENT
// declaring types (same name and count, distinct declaring-type instances --
// the identity comparison distinguishes them).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentDeclaringType)
{
    TestCompilation comp;
    auto outerA = std::make_shared<TestTypeDefinition>("System", "OuterA", 0, comp);
    auto outerB = std::make_shared<TestTypeDefinition>("System", "OuterB", 0, comp);
    NestedTypeReference a(std::make_shared<TestTypeReference>(outerA), "Inner", 0);
    NestedTypeReference b(std::make_shared<TestTypeReference>(outerB), "Inner", 0);
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_FALSE(b.EqualsForInterning(a));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references with the SAME
// declaring type but DIFFERENT names (the name comparison distinguishes them).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentName)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference a(declaringRef, "Inner", 0);
    NestedTypeReference b(declaringRef, "Other", 0);
    EXPECT_FALSE(a.EqualsForInterning(b));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- returns `false` for two references with the SAME
// declaring type and name but DIFFERENT additionalTypeParameterCount (the count
// comparison distinguishes them).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentCount)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference a(declaringRef, "Inner", 0);
    NestedTypeReference b(declaringRef, "Inner", 1);
    EXPECT_FALSE(a.EqualsForInterning(b));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the crux: returns `false` when `other` is a DIFFERENT
// `ISupportsInterning` subtype. A `NestedTypeReference` compared against an
// `ArrayTypeReference` returns `false` because the `dynamic_cast` to
// `NestedTypeReference` fails (the C# `other as NestedTypeReference`-returns-null
// case).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningReturnsFalseForDifferentSubtype)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference nested(declaringRef, "Inner", 0);
    ArrayTypeReference arr(std::make_shared<TestTypeReference>(
        std::make_shared<KnownType>(KnownTypeCode::Int32)));
    EXPECT_FALSE(nested.EqualsForInterning(arr));
    EXPECT_FALSE(arr.EqualsForInterning(nested));
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- the `isReferenceType` optional participates in the
// equality: two references with the SAME declaring type, name, and count but
// DIFFERENT `isReferenceType` are NOT equal (the C# `isReferenceType ==
// o.isReferenceType`).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningDistinguishesIsReferenceType)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference a(declaringRef, "Inner", 0, std::optional<bool>(true));
    NestedTypeReference b(declaringRef, "Inner", 0, std::optional<bool>(false));
    NestedTypeReference c(declaringRef, "Inner", 0, std::optional<bool>(true));
    NestedTypeReference d(declaringRef, "Inner", 0); // nullopt
    EXPECT_FALSE(a.EqualsForInterning(b));
    EXPECT_TRUE(a.EqualsForInterning(c));
    EXPECT_FALSE(a.EqualsForInterning(d)); // true != nullopt
    EXPECT_FALSE(d.EqualsForInterning(a)); // nullopt != true
}

// ---------------------------------------------------------------------------
// EqualsForInterning -- dispatches polymorphically through an
// `ISupportsInterning*` (the InterningProvider's bucketing comparer holds both
// candidates as `ISupportsInterning*` and calls `EqualsForInterning` through the
// base).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, EqualsForInterningDispatchesPolymorphically)
{
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto declaringRef = std::make_shared<TestTypeReference>(outer);
    NestedTypeReference a(declaringRef, "Inner", 0);
    NestedTypeReference b(declaringRef, "Inner", 0);
    const ISupportsInterning* baseA = &a;
    const ISupportsInterning* baseB = &b;
    EXPECT_TRUE(baseA->EqualsForInterning(*baseB));
}

// ---------------------------------------------------------------------------
// NestedTypeReference is `final` (the C# `sealed`), derives from BOTH
// `ITypeReference` and `ISupportsInterning`, is polymorphic, and has a virtual
// destructor (for abstract-base deletion through either base).
// ---------------------------------------------------------------------------
TEST(NestedTypeReferenceTest, IsFinalAndDerivesFromBothBases)
{
    static_assert(std::is_final_v<NestedTypeReference>,
        "NestedTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, NestedTypeReference>,
        "NestedTypeReference must derive from ITypeReference.");
    static_assert(std::is_base_of_v<ISupportsInterning, NestedTypeReference>,
        "NestedTypeReference must derive from ISupportsInterning.");
    static_assert(std::has_virtual_destructor_v<NestedTypeReference>,
        "NestedTypeReference must have a virtual destructor.");
    static_assert(std::is_polymorphic_v<NestedTypeReference>,
        "NestedTypeReference must be polymorphic.");
    TestCompilation comp;
    auto outer = std::make_shared<TestTypeDefinition>("System", "Outer", 0, comp);
    auto ref = std::make_unique<NestedTypeReference>(
        std::make_shared<TestTypeReference>(outer), "Inner", 0);
    const ITypeReference* asRef = ref.get();
    const ISupportsInterning* asInterning = ref.get();
    EXPECT_NE(asRef, nullptr);
    EXPECT_NE(asInterning, nullptr);
    ref.reset(); // runs the derived destructor through the virtual dtor
    SUCCEED();
}
