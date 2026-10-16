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

// Tests for `MergedNamespace` (cpp/Decompiler/TypeSystem/MergedNamespace.hpp, the
// port of `ICSharpCode.Decompiler.TypeSystem.Implementation.MergedNamespace`).
// `MergedNamespace` presents multiple per-module `INamespace`s (the same namespace
// from different assemblies) as a single `INamespace`: it delegates `Name` /
// `FullName` to the first underlying namespace, flattens `Types` /
// `ContributingModules` across them, merges their `ChildNamespaces` into merged
// grandchildren (grouped by short name using the compilation's `NameComparer`), and
// resolves `GetTypeDefinition` by preferring the first `Public` type over
// non-accessible types.
//
// The test stubs:
//  - `TestCompilation` is a compact `ICompilation` with a configurable `NameComparer`
//    (so the case-insensitive grouping can be tested) and a configurable `Modules`
//    list (unused by the MergedNamespace tests but required by the `ICompilation`
//    contract). `FindType` is dead code returning a held `KnownType(Object)`.
//  - `TestTypeDefinition2` is a compact `ITypeDefinition` stub with a configurable
//    `Name`, `TypeParameterCount`, and `Accessibility` (for the
//    `GetTypeDefinition` prefer-public test), every other pure-virtual overridden
//    with a trivial return.
//  - `TestLeafNamespace` is a compact `INamespace` stub (a leaf namespace -- no
//    merged children of its own) with a configurable `Name`, `FullName`,
//    `ExternAlias`, `ChildNamespaces`, `Types`, `ContributingModules`, and a
//    `GetTypeDefinition` that linear-searches its `Types` by
//    (`Name()`, `TypeParameterCount()`). The shape a real per-module
//    `MetadataNamespace` / `SimpleNamespace` takes (the individual namespaces a
//    `MergedNamespace` merges).

#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MergedNamespace.hpp"
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
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MergedNamespace;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A compact `ICompilation` with a configurable `NameComparer` (so the
// case-insensitive child grouping can be tested) and a configurable `Modules` list
// (unused by the MergedNamespace tests but required by the `ICompilation`
// contract; `MainModule` returns a `TestSupport::TestModule`). `FindType` is dead
// code returning a held `KnownType(Object)` to satisfy the non-null reference
// contract.
class TestCompilation : public ICompilation {
public:
    explicit TestCompilation(const StringComparer& nameComparer)
        : nameComparer_(nameComparer), mainModule_(*this) {}

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
    const StringComparer& NameComparer() const override { return nameComparer_; }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
    }

private:
    const StringComparer& nameComparer_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    std::vector<const IModule*> modules_;
    KnownType knownType_{KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A compact `ITypeDefinition` stub with a configurable `Name`,
// `TypeParameterCount`, and `Accessibility` (for the `GetTypeDefinition`
// prefer-public test). Every other pure-virtual is overridden with a trivial
// return. The return types `SymbolKind` / `KnownTypeCode` / `FullTypeName` /
// `ExtensionInfo` / `Accessibility` / `Nullability` are GLOBALLY QUALIFIED because
// the stub INHERITS the same-named member functions (the D372 / D393 cross-scope
// name-hiding crux). Used for pointer identity in the `Types` snapshot AND for the
// `Accessibility()` value the `MergedNamespace.GetTypeDefinition` prefer-public
// logic reads.
class TestTypeDefinition2 : public ITypeDefinition {
public:
    TestTypeDefinition2(std::string reflectionName, int typeParameterCount,
                        ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                        const ICompilation& compilation)
        : fullTypeName_(TopLevelTypeName(std::move(reflectionName))),
          typeParameterCount_(typeParameterCount),
          accessibility_(accessibility),
          compilation_(compilation) {}

    // --- IType ---
    TypeKind Kind() const override { return TypeKind::Class; }
    std::string Name() const override { return fullTypeName_.GetTopLevelTypeName().Name(); }
    std::string ReflectionName() const override
    {
        return fullTypeName_.GetTopLevelTypeName().ReflectionName();
    }
    int TypeParameterCount() const override { return typeParameterCount_; }

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
    std::string Namespace() const override { return {}; }

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
        return accessibility_;
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
        return typeParameterCount_
            == static_cast<const TestTypeDefinition2&>(other).typeParameterCount_;
    }

private:
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    int typeParameterCount_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    const ICompilation& compilation_;
};

// A compact `INamespace` stub (a leaf namespace -- no merged children of its own):
// holds the configured `Name` / `FullName` / `ExternAlias`, `ChildNamespaces` /
// `Types` / `ContributingModules` snapshots, and a `GetTypeDefinition` that
// linear-searches its `Types` by (`Name()`, `TypeParameterCount()`). The shape a
// real per-module `MetadataNamespace` / `SimpleNamespace` takes (the individual
// namespaces a `MergedNamespace` merges). `Name()` is overridden ONCE and satisfies
// the inherited `ISymbol::Name()` contract (single inheritance -- the D374
// precedent).
class TestLeafNamespace : public INamespace {
public:
    TestLeafNamespace(std::string name, std::string fullName, std::string externAlias,
                      const ICompilation& compilation)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          externAlias_(std::move(externAlias)), compilation_(compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return externAlias_; }
    std::string FullName() const override { return fullName_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return childNamespaces_; }
    std::vector<const ITypeDefinition*> Types() const override
    {
        std::vector<const ITypeDefinition*> result;
        result.reserve(types_.size());
        for (auto* td : types_)
            result.push_back(td);
        return result;
    }
    std::vector<const IModule*> ContributingModules() const override
    {
        return contributingModules_;
    }
    const INamespace* GetChildNamespace(const std::string& name) const override
    {
        for (auto* child : childNamespaces_) {
            if (child->Name() == name) return child;
        }
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string& name,
                                              int typeParameterCount) const override
    {
        for (auto* td : types_) {
            if (td->Name() == name && td->TypeParameterCount() == typeParameterCount)
                return td;
        }
        return nullptr;
    }

    // Test wiring.
    void SetChildNamespaces(std::vector<const INamespace*> v) { childNamespaces_ = std::move(v); }
    void SetTypes(std::vector<TestTypeDefinition2*> v) { types_ = std::move(v); }
    void SetContributingModules(std::vector<const IModule*> v)
    {
        contributingModules_ = std::move(v);
    }

private:
    std::string name_, fullName_, externAlias_;
    const ICompilation& compilation_;
    std::vector<const INamespace*> childNamespaces_;
    std::vector<TestTypeDefinition2*> types_;
    std::vector<const IModule*> contributingModules_;
};

} // namespace

// ---------------------------------------------------------------------------
// Root ctor -- delegates `Name` / `FullName` to the first underlying namespace,
// stores the `externAlias`, the `compilation`, and a null `ParentNamespace` (the
// root has no parent).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, RootCtorDelegatesNameAndFullNameToFirstNamespace)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace ns2("System", "System", "Alias", compilation);
    MergedNamespace root(compilation, {&ns1, &ns2});

    EXPECT_EQ(root.Name(), "System");
    EXPECT_EQ(root.FullName(), "System");
    EXPECT_EQ(root.ExternAlias(), "");
    EXPECT_EQ(root.ParentNamespace(), nullptr);
    EXPECT_EQ(&root.Compilation(), &compilation);
}

// ---------------------------------------------------------------------------
// Root ctor -- the `externAlias` is stored (non-empty for an extern-aliased
// namespace).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, RootCtorStoresExternAlias)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    MergedNamespace root(compilation, {&ns1}, "Alias");

    EXPECT_EQ(root.ExternAlias(), "Alias");
}

// ---------------------------------------------------------------------------
// Child ctor -- threads `Compilation` and `ExternAlias` from the parent, stores
// the parent pointer.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, ChildCtorThreadsCompilationAndExternAliasFromParent)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    MergedNamespace parent(compilation, {&ns1}, "Alias");

    TestLeafNamespace childNs1("Collections", "System.Collections", "", compilation);
    MergedNamespace child(&parent, {&childNs1});

    EXPECT_EQ(&child.Compilation(), &compilation);
    EXPECT_EQ(child.ExternAlias(), "Alias");
    EXPECT_EQ(child.ParentNamespace(), &parent);
}

// ---------------------------------------------------------------------------
// `SymbolKind` is `Namespace` (the `ISymbol` override).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, SymbolKindIsNamespace)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    MergedNamespace root(compilation, {&ns1});

    EXPECT_EQ(root.SymbolKind(), SymbolKind::Namespace);
}

// ---------------------------------------------------------------------------
// `Types` flattens the `Types` of all underlying namespaces (the C# `SelectMany`).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, TypesFlattensAcrossAllNamespaces)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestTypeDefinition2 list("List`1", 1, Accessibility::Public, compilation);
    TestTypeDefinition2 queue("Queue`1", 1, Accessibility::Public, compilation);
    TestTypeDefinition2 dict("Dictionary`2", 2, Accessibility::Public, compilation);
    TestLeafNamespace ns1("Collections", "System.Collections", "", compilation);
    TestLeafNamespace ns2("Collections", "System.Collections", "", compilation);
    ns1.SetTypes({&list, &queue});
    ns2.SetTypes({&dict});

    MergedNamespace root(compilation, {&ns1, &ns2});
    const auto types = root.Types();
    ASSERT_EQ(types.size(), 3u);
    EXPECT_EQ(types[0], static_cast<const ITypeDefinition*>(&list));
    EXPECT_EQ(types[1], static_cast<const ITypeDefinition*>(&queue));
    EXPECT_EQ(types[2], static_cast<const ITypeDefinition*>(&dict));
}

// ---------------------------------------------------------------------------
// `ContributingModules` flattens the `ContributingModules` of all underlying
// namespaces (the C# `SelectMany`).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, ContributingModulesFlattensAcrossAllNamespaces)
{
    TestCompilation compilation(StringComparer::Ordinal());
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule moduleA(compilation);
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule moduleB(compilation);
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace ns2("System", "System", "", compilation);
    ns1.SetContributingModules({&moduleA});
    ns2.SetContributingModules({&moduleB});

    MergedNamespace root(compilation, {&ns1, &ns2});
    const auto modules = root.ContributingModules();
    ASSERT_EQ(modules.size(), 2u);
    EXPECT_EQ(modules[0], &moduleA);
    EXPECT_EQ(modules[1], &moduleB);
}

// ---------------------------------------------------------------------------
// `ChildNamespaces` groups the underlying namespaces' `ChildNamespaces` by short
// name: two underlying namespaces each contributing a "Collections" child are merged
// into ONE child `MergedNamespace` (whose `Types` flattens both underlying
// children's `Types`), while a uniquely-named "IO" child stays separate.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, ChildNamespacesGroupsByShortName)
{
    TestCompilation compilation(StringComparer::Ordinal());
    // The two underlying "System" namespaces, each with a "Collections" child and
    // (the first) an "IO" child.
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace ns2("System", "System", "", compilation);
    TestLeafNamespace collections1("Collections", "System.Collections", "", compilation);
    TestLeafNamespace collections2("Collections", "System.Collections", "", compilation);
    TestLeafNamespace io("IO", "System.IO", "", compilation);
    TestTypeDefinition2 list("List`1", 1, Accessibility::Public, compilation);
    TestTypeDefinition2 dict("Dictionary`2", 2, Accessibility::Public, compilation);
    collections1.SetTypes({&list});
    collections2.SetTypes({&dict});
    ns1.SetChildNamespaces({&collections1, &io});
    ns2.SetChildNamespaces({&collections2});

    MergedNamespace root(compilation, {&ns1, &ns2});
    const auto children = root.ChildNamespaces();
    ASSERT_EQ(children.size(), 2u);
    // The merged "Collections" child flattens both underlying children's Types.
    const INamespace* mergedCollections = root.GetChildNamespace("Collections");
    ASSERT_NE(mergedCollections, nullptr);
    const auto mergedTypes = mergedCollections->Types();
    ASSERT_EQ(mergedTypes.size(), 2u);
    EXPECT_EQ(mergedTypes[0], static_cast<const ITypeDefinition*>(&list));
    EXPECT_EQ(mergedTypes[1], static_cast<const ITypeDefinition*>(&dict));
    // The "IO" child is separate (only from ns1).
    const INamespace* mergedIO = root.GetChildNamespace("IO");
    ASSERT_NE(mergedIO, nullptr);
    EXPECT_EQ(mergedIO->Types().size(), 0u);
}

// ---------------------------------------------------------------------------
// `GetChildNamespace` finds a child by its short name, or returns null when not
// found.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, GetChildNamespaceFindsByShortNameOrReturnsNull)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace collections("Collections", "System.Collections", "", compilation);
    TestLeafNamespace io("IO", "System.IO", "", compilation);
    ns1.SetChildNamespaces({&collections, &io});

    MergedNamespace root(compilation, {&ns1});
    const auto children = root.ChildNamespaces();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(root.GetChildNamespace("Collections"), children[0]);
    EXPECT_EQ(root.GetChildNamespace("IO"), children[1]);
    EXPECT_EQ(root.GetChildNamespace("Threading"), nullptr);
}

// ---------------------------------------------------------------------------
// `GetChildNamespace` uses the compilation's `NameComparer` for the lookup: with
// `OrdinalIgnoreCase`, a lookup by a different-cased name finds the child. The
// grouping also uses the `NameComparer`, so two children whose names differ only
// by case are merged into one.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, GetChildNamespaceUsesNameComparerForCaseInsensitive)
{
    TestCompilation compilation(StringComparer::OrdinalIgnoreCase());
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace ns2("System", "System", "", compilation);
    TestLeafNamespace collections("Collections", "System.Collections", "", compilation);
    TestLeafNamespace collectionsUpper("COLLECTIONS", "System.COLLECTIONS", "", compilation);
    TestTypeDefinition2 list("List`1", 1, Accessibility::Public, compilation);
    TestTypeDefinition2 dict("Dictionary`2", 2, Accessibility::Public, compilation);
    collections.SetTypes({&list});
    collectionsUpper.SetTypes({&dict});
    ns1.SetChildNamespaces({&collections});
    ns2.SetChildNamespaces({&collectionsUpper});

    MergedNamespace root(compilation, {&ns1, &ns2});
    // The two case-variant children are merged into ONE (OrdinalIgnoreCase).
    const auto children = root.ChildNamespaces();
    ASSERT_EQ(children.size(), 1u);
    // A case-variant lookup finds the merged child.
    EXPECT_NE(root.GetChildNamespace("COLLECTIONS"), nullptr);
    EXPECT_EQ(root.GetChildNamespace("COLLECTIONS"), children[0]);
    // The merged child flattens both underlying children's Types.
    EXPECT_EQ(children[0]->Types().size(), 2u);
}

// ---------------------------------------------------------------------------
// `GetTypeDefinition` prefers the FIRST `Public` type (the C# "prefer accessible
// types" rule): when the first namespace has a non-Public type and the second has a
// Public type with the same name, the Public one is returned.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, GetTypeDefinitionPrefersPublicType)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestTypeDefinition2 internalList("List`1", 1, Accessibility::Internal, compilation);
    TestTypeDefinition2 publicList("List`1", 1, Accessibility::Public, compilation);
    TestLeafNamespace ns1("Collections", "System.Collections", "", compilation);
    TestLeafNamespace ns2("Collections", "System.Collections", "", compilation);
    ns1.SetTypes({&internalList});
    ns2.SetTypes({&publicList});

    MergedNamespace root(compilation, {&ns1, &ns2});
    EXPECT_EQ(root.GetTypeDefinition("List", 1), static_cast<const ITypeDefinition*>(&publicList));
}

// ---------------------------------------------------------------------------
// `GetTypeDefinition` returns the last non-null type when none are `Public` (the
// C# fallback: `anyTypeDef` holds the last non-null, returned if no `Public` is
// found).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, GetTypeDefinitionReturnsLastNonPublicWhenNonePublic)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestTypeDefinition2 internalList("List`1", 1, Accessibility::Internal, compilation);
    TestTypeDefinition2 protectedList("List`1", 1, Accessibility::Protected, compilation);
    TestLeafNamespace ns1("Collections", "System.Collections", "", compilation);
    TestLeafNamespace ns2("Collections", "System.Collections", "", compilation);
    ns1.SetTypes({&internalList});
    ns2.SetTypes({&protectedList});

    MergedNamespace root(compilation, {&ns1, &ns2});
    // Neither is Public -> the last non-null (from ns2) is returned.
    EXPECT_EQ(root.GetTypeDefinition("List", 1),
              static_cast<const ITypeDefinition*>(&protectedList));
}

// ---------------------------------------------------------------------------
// `GetTypeDefinition` returns null when no underlying namespace has the type.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, GetTypeDefinitionReturnsNullWhenNotFound)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("Collections", "System.Collections", "", compilation);
    MergedNamespace root(compilation, {&ns1});
    EXPECT_EQ(root.GetTypeDefinition("Queue", 1), nullptr);
}

// ---------------------------------------------------------------------------
// The child-namespace map is lazily built and cached (first-writer-wins): a second
// `ChildNamespaces` call returns the SAME child pointers (pointer-identity), proving
// the `LazyInit` cache holds the first result rather than rebuilding it.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, ChildNamespaceMapIsCached)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace collections("Collections", "System.Collections", "", compilation);
    TestLeafNamespace io("IO", "System.IO", "", compilation);
    ns1.SetChildNamespaces({&collections, &io});

    MergedNamespace root(compilation, {&ns1});
    const auto first = root.ChildNamespaces();
    const auto second = root.ChildNamespaces();
    ASSERT_EQ(first.size(), second.size());
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0], second[0]);
    EXPECT_EQ(first[1], second[1]);
}

// ---------------------------------------------------------------------------
// `ToString` mirrors the C# diagnostic format: `[MergedNamespace {FullName} (from
// {count} assemblies)]` for a normal namespace, `[MergedNamespace {alias}::{FullName}
// (from {count} assemblies)]` for an extern-aliased namespace.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, ToStringMirrorsCSharpDiagnosticFormat)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    TestLeafNamespace ns2("System", "System", "", compilation);
    MergedNamespace root(compilation, {&ns1, &ns2});
    EXPECT_EQ(root.ToString(), "[MergedNamespace System (from 2 assemblies)]");

    MergedNamespace aliased(compilation, {&ns1}, "Alias");
    EXPECT_EQ(aliased.ToString(), "[MergedNamespace Alias::System (from 1 assemblies)]");
}

// ---------------------------------------------------------------------------
// Polymorphic dispatch through the `INamespace*` / `ISymbol*` /
// `ICompilationProvider*` base pointers (a `MergedNamespace` IS-A each of its
// bases, so each base pointer dispatches to the concrete override, including the
// single `Name()` that is the final overrider for the inherited `ISymbol::Name()`).
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, DispatchesPolymorphicallyThroughBasePointers)
{
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    auto owned = std::make_unique<MergedNamespace>(compilation, std::vector<const INamespace*>{&ns1});
    INamespace* asNamespace = owned.get();
    ILSpy::Decompiler::TypeSystem::ISymbol* asSymbol = owned.get();
    ILSpy::Decompiler::TypeSystem::ICompilationProvider* asProvider = owned.get();
    EXPECT_EQ(asNamespace->Name(), "System");
    EXPECT_EQ(asSymbol->Name(), "System");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Namespace);
    EXPECT_EQ(&asProvider->Compilation(), &compilation);
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Has a virtual destructor (a concrete subclass can be deleted through an
// `INamespace*` and the derived destructor runs), is `final`, and is polymorphic.
// ---------------------------------------------------------------------------
TEST(MergedNamespaceTest, HasVirtualDestructorAndIsFinalAndPolymorphic)
{
    static_assert(std::has_virtual_destructor_v<MergedNamespace>,
        "MergedNamespace must have a virtual destructor for abstract-base deletion");
    static_assert(std::is_final_v<MergedNamespace>,
        "MergedNamespace must be final (the C# sealed class)");
    static_assert(std::is_polymorphic_v<MergedNamespace>,
        "MergedNamespace must be polymorphic");
    TestCompilation compilation(StringComparer::Ordinal());
    TestLeafNamespace ns1("System", "System", "", compilation);
    std::unique_ptr<INamespace> owned =
        std::make_unique<MergedNamespace>(compilation, std::vector<const INamespace*>{&ns1});
    EXPECT_EQ(owned->Name(), "System");
    owned.reset();
    SUCCEED();
}
