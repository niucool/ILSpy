// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
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

// Tests for `KnownTypeCache` (cpp/Decompiler/TypeSystem/KnownTypeCache.hpp, the
// port of `ICSharpCode.Decompiler.TypeSystem.Implementation.KnownTypeCache`).
// `KnownTypeCache` is the per-compilation cache backing
// `ICompilation.FindType(KnownTypeCode)`: it lazily resolves a code to its `IType`
// on first use -- the `ITypeDefinition` found by searching the compilation's
// `Modules` (the happy path, a NON-OWNING `shared_ptr` alias of the module's
// `ITypeDefinition`), an `UnknownType` fallback (no module has the type), or the
// `SpecialType.UnknownType` null object for `None` -- and caches the result with
// first-writer-wins semantics via `LazyInit` (D420).
//
// The test stubs: `TestTypeDefinition` is a compact `ITypeDefinition` stub (the
// ~34 pure-virtuals overridden with trivial returns, except `Kind` / `Name` /
// `ReflectionName` / `TypeParameterCount` / `FullTypeName` derived from a
// configured `TopLevelTypeName`); `TestModuleWithTypeDef` extends the shared
// `TestSupport::TestModule` (D399) and overrides `GetTypeDefinition` to return a
// configured `ITypeDefinition*` when the lookup key matches; `TestCompilation`
// is a compact `ICompilation` with a configurable `Modules()` list (the
// collection `SearchType` walks). The modules / type definitions are constructed
// in the test body and wired into the compilation via `SetModules` after
// construction (the `TestCompilation` is declared first so it outlives the
// modules / type definitions that reference it).

#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCache.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCache;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A compact `ICompilation` with a configurable `Modules()` list -- the
// collection `KnownTypeCache::SearchType` walks to find a type definition.
// `MainModule` returns a `TestSupport::TestModule` (the shared stub, D399, which
// returns `nullptr` from `GetTypeDefinition`). `FindType` is dead code for these
// tests (the `KnownTypeCache` is what implements `FindType`; the test's
// `TestCompilation.FindType` is never reached through the cache), so it returns a
// held `KnownType(Object)` to satisfy the non-null reference contract.
class TestCompilation : public ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

    void SetModules(std::vector<const IModule*> modules) { modules_ = std::move(modules); }

    const IModule& MainModule() const override { return mainModule_; }
    std::vector<const IModule*> Modules() const override { return modules_; }
    std::vector<const IModule*> ReferencedModules() const override { return {}; }
    const INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const IType& FindType(KnownTypeCode) const override { return knownType_; }
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
    std::vector<const IModule*> modules_;
    KnownType knownType_{KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A compact `ITypeDefinition` stub for the type-found happy path: overrides every
// pure-virtual with a trivial return, except `Kind` (a configured `TypeKind`) and
// `Name` / `ReflectionName` / `TypeParameterCount` / `FullTypeName` (derived from
// a configured `TopLevelTypeName`). The single `Name` / `ReflectionName` overrides
// are the final overrider for the `IType`-vs-`IEntity` diamond (the `ITypeDefinition`
// redeclarations disambiguate lookup; a single override covers both paths, D393).
// The return types `SymbolKind` / `KnownTypeCode` / `FullTypeName` /
// `ExtensionInfo` / `Accessibility` / `Nullability` are GLOBALLY QUALIFIED because
// the stub INHERITS the same-named member functions (the D372 / D393 cross-scope
// name-hiding crux). The accessors the `KnownTypeCache` test never reads
// (`NestedTypes` / `Members` / `GetAttributes` / ...) are dead code that merely
// needs to compile -- trivial empty / null / false returns.
class TestTypeDefinition : public ITypeDefinition {
public:
    TestTypeDefinition(TopLevelTypeName topLevelName, TypeKind typeKind,
                       const ICompilation& compilation)
        : fullTypeName_(std::move(topLevelName)),
          typeKind_(typeKind),
          compilation_(compilation) {}

    // --- IType (inherited unambiguously via the single IType subobject; only
    //     Name / ReflectionName are redeclared in ITypeDefinition) ---
    TypeKind Kind() const override { return typeKind_; }
    std::string Name() const override { return fullTypeName_.GetTopLevelTypeName().Name(); }
    std::string ReflectionName() const override
    {
        return fullTypeName_.GetTopLevelTypeName().ReflectionName();
    }
    int TypeParameterCount() const override
    {
        return fullTypeName_.GetTopLevelTypeName().TypeParameterCount();
    }

    // --- ITypeDefinitionOrUnknown ---
    const ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override
    {
        return fullTypeName_;
    }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // --- INamedElement ---
    std::string FullName() const override
    {
        return fullTypeName_.GetTopLevelTypeName().ReflectionName();
    }
    std::string Namespace() const override
    {
        return fullTypeName_.GetTopLevelTypeName().Namespace();
    }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
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
    std::vector<const ITypeDefinition*> NestedTypes() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> Members() const override
    {
        return {};
    }
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
    ILSpy::Decompiler::TypeSystem::ITypePtr EnumUnderlyingType() const override { return {}; }
    bool IsReadOnly() const override { return false; }
    std::string MetadataName() const override
    {
        return fullTypeName_.GetTopLevelTypeName().Name();
    }
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

protected:
    bool StructuralEquals(const IType& other) const override
    {
        return typeKind_ == static_cast<const TestTypeDefinition&>(other).typeKind_;
    }

private:
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    TypeKind typeKind_;
    const ICompilation& compilation_;
};

// A compact `IModule` that returns a configured `ITypeDefinition*` from
// `GetTypeDefinition` when the lookup key matches the type definition's
// `TopLevelTypeName`, otherwise `nullptr`. It extends the shared
// `TestSupport::TestModule` (D399) so the inherited `ISymbol` /
// `ICompilationProvider` / `IModule` surface stays trivial; only
// `GetTypeDefinition` is overridden. The held type definition is non-owning
// (the test owns it; the module returns a pointer to it).
class TestModuleWithTypeDef : public ILSpy::Decompiler::TypeSystem::TestSupport::TestModule {
public:
    TestModuleWithTypeDef(const ICompilation& compilation, const ITypeDefinition* typeDef)
        : ILSpy::Decompiler::TypeSystem::TestSupport::TestModule(compilation),
          typeDef_(typeDef) {}

    const ITypeDefinition* GetTypeDefinition(
        const TopLevelTypeName& topLevelTypeName) const override
    {
        if (typeDef_ != nullptr
            && topLevelTypeName == typeDef_->FullTypeName().GetTopLevelTypeName())
        {
            return typeDef_;
        }
        return nullptr;
    }

private:
    const ITypeDefinition* typeDef_;
};

} // namespace

// ---------------------------------------------------------------------------
// FindType -- `KnownTypeCode.None` has no `KnownTypeReference` (`Get` returns
// `nullptr`), so `SearchType` returns the `SpecialType.UnknownType` null object
// (the `UnknownType()` convenience function). Pinned by `Kind() == Unknown`.
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeReturnsUnknownNullObjectForNone)
{
    TestCompilation compilation;
    KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::None);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
}

// ---------------------------------------------------------------------------
// FindType -- a code whose `KnownTypeReference` exists but no module has the
// type definition -> the `UnknownType` fallback (`namespaceKnown = true`,
// carrying the metadata name / namespace / arity). Pinned by `Kind() == Unknown`
// and the metadata `Name()` / `TypeParameterCount()` for `Object` (System.Object,
// arity 0). Uses the shared `TestSupport::TestModule` (returns `nullptr` from
// `GetTypeDefinition`, so no module has the type).
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeReturnsUnknownTypeFallbackWhenNotInModules)
{
    TestCompilation compilation;
    KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "Object");
    EXPECT_EQ(result.TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// FindType -- the `UnknownType` fallback carries the arity for a generic known
// type. `IEnumerableOfT` (System.Collections.Generic.IEnumerable, arity 1) is not
// in any module -> the fallback's `TypeParameterCount() == 1` and `Name() ==
// "IEnumerable"`.
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeFallbackCarriesArityForGenericKnownType)
{
    TestCompilation compilation;
    KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::IEnumerableOfT);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
    EXPECT_EQ(result.Name(), "IEnumerable");
    EXPECT_EQ(result.TypeParameterCount(), 1);
}

// ---------------------------------------------------------------------------
// FindType -- the happy path: a module's `GetTypeDefinition` returns the type
// definition -> `SearchType` returns a NON-OWNING alias of the module's
// `ITypeDefinition` (convention (d)), so `FindType` returns that exact type
// definition (the `IType` subobject). Pinned by `Kind()` / `Name()` from the
// stub AND pointer-identity: the address of the returned `IType&` equals the
// `IType` subobject of the stub type definition (the module-owned object is
// aliased, not copied).
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeReturnsTypeDefinitionWhenFoundInModule)
{
    TestCompilation compilation;
    TestTypeDefinition typeDef(TopLevelTypeName("System", "Object", 0), TypeKind::Class,
                               compilation);
    TestModuleWithTypeDef module(compilation, &typeDef);
    compilation.SetModules({&module});

    KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(result.Kind(), TypeKind::Class);
    EXPECT_EQ(result.Name(), "Object");
    // Pointer-identity: the cache aliases the module's type definition (the
    // non-owning shared_ptr holds the IType subobject of `typeDef`).
    EXPECT_EQ(&result, static_cast<const IType*>(&typeDef));
}

// ---------------------------------------------------------------------------
// FindType -- the result is cached: a second `FindType` for the same code
// returns the SAME `IType` (pointer-identity of the cached slot), proving the
// first-writer-wins `LazyInit` cache holds the first result rather than
// rebuilding it.
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeCachesResult)
{
    TestCompilation compilation;
    KnownTypeCache cache(compilation);
    const IType& first = cache.FindType(KnownTypeCode::Object);
    const IType& second = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(&first, &second);
}

// ---------------------------------------------------------------------------
// FindType -- `SearchType` walks EVERY module in `compilation.Modules` and
// returns the FIRST non-null `GetTypeDefinition` result. Pinned by a
// compilation with two modules: the first (the shared `TestSupport::TestModule`)
// returns `nullptr` for the type, the second (`TestModuleWithTypeDef`) has the
// type definition -> the second's type definition is returned.
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeSearchesAllModules)
{
    TestCompilation compilation;
    TestTypeDefinition typeDef(TopLevelTypeName("System", "Object", 0), TypeKind::Class,
                               compilation);
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule firstModule(compilation);
    TestModuleWithTypeDef secondModule(compilation, &typeDef);
    compilation.SetModules({&firstModule, &secondModule});

    KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(&result, static_cast<const IType*>(&typeDef));
}

// ---------------------------------------------------------------------------
// FindType -- the type-found result is cached too (pointer-identity across two
// calls), proving the non-owning alias is stored in the slot and returned on
// the second call (the `VolatileRead` early-return path).
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeCachesTypeDefinitionResult)
{
    TestCompilation compilation;
    TestTypeDefinition typeDef(TopLevelTypeName("System", "Object", 0), TypeKind::Class,
                               compilation);
    TestModuleWithTypeDef module(compilation, &typeDef);
    compilation.SetModules({&module});

    KnownTypeCache cache(compilation);
    const IType& first = cache.FindType(KnownTypeCode::Object);
    const IType& second = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(&first, static_cast<const IType*>(&typeDef));
}

// ---------------------------------------------------------------------------
// FindType is callable on a `const KnownTypeCache` (the `ICompilation::FindType`
// contract is a const read; the lazy write is hidden behind `mutable`).
// ---------------------------------------------------------------------------
TEST(KnownTypeCacheTest, FindTypeIsConstCallable)
{
    TestCompilation compilation;
    const KnownTypeCache cache(compilation);
    const IType& result = cache.FindType(KnownTypeCode::Object);
    EXPECT_EQ(result.Kind(), TypeKind::Unknown);
}
