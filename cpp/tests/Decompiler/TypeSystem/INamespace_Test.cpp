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

// Tests for `INamespace` (cpp/Decompiler/TypeSystem/INamespace.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/INamespace.cs). `INamespace : ISymbol,
// ICompilationProvider` represents a resolved namespace; it exposes the `ExternAlias`,
// the dotted `FullName` and short `Name`, the nullable `ParentNamespace`, the
// `ChildNamespaces` / `Types` / `ContributingModules` collections, and the
// `GetChildNamespace` / `GetTypeDefinition` lookup helpers. It does NOT derive from
// `IEntity` / `INamedElement` (unlike the member family), so it carries NO attribute
// family -- only the inherited `ISymbol::SymbolKind` / `ISymbol::Name` and
// `ICompilationProvider::Compilation`.
//
// The C# `new string Name` re-hides `ISymbol.Name` with SINGLE inheritance (only
// `ISymbol` declares `Name`; `ICompilationProvider` does not), so the inherited
// `ISymbol::Name()` virtual covers it with NO C++ redeclaration (the D374 single-
// inheritance "inherited virtual covers the `new`" precedent): the concrete
// `TestNamespace` overrides `Name()` once and dispatch through `INamespace*` /
// `ISymbol*` both reach it. This is the structural distinction from the
// `IEntity` (D381) / `ITypeParameter` (D383) / `IField` (D391) multiple-inheritance
// diamonds where `Name` was inherited via two independent paths and REQUIRED a
// redeclaration.
//
// The test stubs:
//  - `TestNamespace` derives from the real `INamespace` and overrides the inherited
//    `ISymbol` / `ICompilationProvider` surface plus the eight `INamespace`-own
//    accessors (the shape a real `MetadataNamespace` / `SimpleNamespace` takes).
//  - `TestTypeDefinition` is a COMPACT concrete `ITypeDefinition` (every pure-virtual
//    overridden with a trivial return, only `Name` / `ReflectionName` /
//    `TypeParameterCount` derive from a stored `FullTypeName`) used ONLY for pointer
//    identity in the `Types` / `GetTypeDefinition` snapshots -- the INamespace tests
//    never read an `ITypeDefinition` accessor through the snapshot pointers beyond
//    `Name()` / `TypeParameterCount()` (the `GetTypeDefinition` lookup keys). It lives
//    in the anonymous namespace (file-local), so it does not ODR-conflict with the
//    richer `TestTypeDefinition` in `ITypeDefinition_Test.cpp`.
//  - `TestCompilation` is the minimal concrete `ICompilation` stand-in (the D379
//    pattern). The `ICompilation` stand-in is defined IDENTICALLY to the other
//    TypeSystem test files (virtual destructor only); the identical class definitions
//    across translation units satisfy the One Definition Rule.
//  - `TestModule` is a COMPACT concrete `IModule` (the real port, D396) used ONLY for
//    pointer identity in the `ContributingModules` snapshot (the INamespace tests never
//    read an `IModule` accessor through the snapshot pointers). It lives in the anonymous
//    namespace (file-local), so it does not ODR-conflict with the `TestModule` in other
//    test files.

#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Minimal test stand-in for `ICompilation` (the parent compilation interface). IDENTICAL to
// the stand-in in the other TypeSystem test files (a virtual destructor only); the identical
// class definitions across translation units satisfy the One Definition Rule. Replaced by the
// real `ICompilation.hpp` when that lands.
class ICompilation {
public:
    virtual ~ICompilation() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// can return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A COMPACT concrete `ITypeDefinition` for testing: used ONLY for pointer identity in the
// `INamespace::Types` / `GetTypeDefinition` snapshots (the INamespace tests read only `Name()`
// / `TypeParameterCount()` through the snapshot pointers -- the `GetTypeDefinition` lookup
// keys). It overrides every `ITypeDefinition` / `ITypeDefinitionOrUnknown` / `IType` /
// `IEntity` / `ICompilationProvider` / `INamedElement` / `ISymbol` pure-virtual with a trivial
// return, except `Name` / `ReflectionName` / `TypeParameterCount` which derive from the stored
// `FullTypeName` (the `ITypeDefinitionOrUnknown_Test` convention). The single `Name()` /
// `ReflectionName()` overrides are the final overrider for the `IType`-vs-`IEntity` diamond
// (the `ITypeDefinition` redeclarations disambiguate lookup). The return types `SymbolKind` /
// `KnownTypeCode` / `FullTypeName` / `ExtensionInfo` / `Accessibility` / `Nullability` /
// `TypeKind` are GLOBALLY QUALIFIED because the stub INHERITS the same-named member functions
// (via `ISymbol` / `ITypeDefinition` / `ITypeDefinitionOrUnknown` / `IEntity`), which hide the
// namespace-scope enums/classes in the stub's class body (the D372 cross-scope name-hiding
// crux). It lives in the anonymous namespace (file-local), so it does not ODR-conflict with
// the richer `TestTypeDefinition` in `ITypeDefinition_Test.cpp`.
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    TestTypeDefinition(std::string reflectionName,
                       const TestCompilation& compilation,
                       std::uint32_t metadataToken)
        : fullTypeName_(reflectionName), metadataName_(std::move(reflectionName)),
          compilation_(compilation), metadataToken_(metadataToken) {}

    // --- IType (inherited unambiguously; only Name/ReflectionName are redeclared in ITypeDefinition) ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeKind::Class;
    }
    std::string Name() const override { return fullTypeName_.Name(); }
    std::string ReflectionName() const override { return fullTypeName_.ReflectionName(); }
    int TypeParameterCount() const override { return fullTypeName_.TypeParameterCount(); }

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
    std::string FullName() const override { return fullTypeName_.FullName(); }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/) const override
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
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> NestedTypes() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> Members() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> Fields() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> Properties() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> Events() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override
    {
        return ILSpy::Decompiler::TypeSystem::KnownTypeCode::None;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr EnumUnderlyingType() const override { return {}; }
    bool IsReadOnly() const override { return false; }
    std::string MetadataName() const override { return metadataName_; }
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
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType& /*other*/) const override
    {
        return false;
    }

private:
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    std::string metadataName_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
};

// A minimal concrete `INamespace` for testing: holds the configured state and returns it from
// every accessor (the shape a real `MetadataNamespace` / `SimpleNamespace` takes). `Name()` is
// overridden ONCE and satisfies the inherited `ISymbol::Name()` contract (single inheritance --
// `ICompilationProvider` declares no `Name`, so the inherited `ISymbol::Name()` is the only
// `Name` and the C# `new string Name` is covered with no redeclaration, the D374 precedent).
// `GetChildNamespace` / `GetTypeDefinition` do a linear search over the configured
// `ChildNamespaces` / `Types` snapshots by `Name()` / (`Name()`, `TypeParameterCount()`) -- the
// C# uses the compilation's string comparer, but a plain `==` suffices for the tests.
class TestNamespace : public ILSpy::Decompiler::TypeSystem::INamespace {
public:
    TestNamespace(std::string name,
                  std::string fullName,
                  std::string externAlias,
                  const TestCompilation& compilation)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          externAlias_(std::move(externAlias)), compilation_(compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- INamespace ---
    std::string ExternAlias() const override { return externAlias_; }
    std::string FullName() const override { return fullName_; }
    const ILSpy::Decompiler::TypeSystem::INamespace* ParentNamespace() const override
    {
        return parentNamespace_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> ChildNamespaces() const override
    {
        return childNamespaces_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> Types() const override
    {
        return types_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules() const override
    {
        return contributingModules_;
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetChildNamespace(
        const std::string& name) const override
    {
        for (auto* child : childNamespaces_) {
            if (child->Name() == name) return child;
        }
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const std::string& name, int typeParameterCount) const override
    {
        for (auto* td : types_) {
            if (td->Name() == name && td->TypeParameterCount() == typeParameterCount) return td;
        }
        return nullptr;
    }

    // Test wiring (set the nullable / collection slots after construction).
    void SetParentNamespace(const ILSpy::Decompiler::TypeSystem::INamespace* parent)
    {
        parentNamespace_ = parent;
    }
    void SetChildNamespaces(std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> v)
    {
        childNamespaces_ = std::move(v);
    }
    void SetTypes(std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> v)
    {
        types_ = std::move(v);
    }
    void SetContributingModules(std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> v)
    {
        contributingModules_ = std::move(v);
    }

private:
    std::string name_, fullName_, externAlias_;
    const TestCompilation& compilation_;
    const ILSpy::Decompiler::TypeSystem::INamespace* parentNamespace_ = nullptr;
    std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> childNamespaces_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> types_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> contributingModules_;
};

// A COMPACT concrete `IModule` for testing: used ONLY for pointer identity in the
// `INamespace::ContributingModules` snapshot (the INamespace tests never read an `IModule`
// accessor through the snapshot pointers). `IModule` is now the real port (D396), so the
// snapshot element type is the abstract interface and a concrete stub backs the instances.
// It overrides every `IModule` / `ISymbol` / `ICompilationProvider` pure-virtual with a
// trivial return; `RootNamespace()` returns a reference to the held `TestNamespace` (the
// non-null `const INamespace&` return requires a concrete `INamespace`, which `TestNamespace`
// provides). It lives in the anonymous namespace (file-local), so it does not ODR-conflict
// with the `TestModule` in other test files.
class TestModule : public ILSpy::Decompiler::TypeSystem::IModule {
public:
    explicit TestModule(const TestCompilation& compilation) : rootNamespace_("", "", "", compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
    }
    std::string Name() const override { return {}; }
    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return rootNamespace_.Compilation();
    }
    // --- IModule ---
    const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override { return nullptr; }
    bool IsMainModule() const override { return false; }
    std::string AssemblyName() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::Version AssemblyVersion() const override { return {}; }
    std::string FullAssemblyName() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAssemblyAttributes() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetModuleAttributes() const override
    {
        return {};
    }
    bool InternalsVisibleTo(const ILSpy::Decompiler::TypeSystem::IModule&) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override
    {
        return rootNamespace_;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName&) const override
    {
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TopLevelTypeDefinitions() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TypeDefinitions() const override
    {
        return {};
    }

private:
    TestNamespace rootNamespace_;
};

} // namespace

// ---------------------------------------------------------------------------
// INamespace -- the scalar/flag accessors return the configured values: the short `Name`,
// the dotted `FullName`, and the `ExternAlias` (empty for a normal namespace, non-empty for
// an extern-aliased namespace).
// ---------------------------------------------------------------------------
TEST(INamespaceTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    TestNamespace ns("Collections", "System.Collections", "", compilation);

    EXPECT_EQ(ns.Name(), "Collections");
    EXPECT_EQ(ns.FullName(), "System.Collections");
    EXPECT_EQ(ns.ExternAlias(), "");

    TestNamespace aliased("Data", "Alias::Data", "Alias", compilation);
    EXPECT_EQ(aliased.ExternAlias(), "Alias");
    EXPECT_EQ(aliased.FullName(), "Alias::Data");
}

// ---------------------------------------------------------------------------
// INamespace -- `ParentNamespace` returns the configured nullable pointer (null for the root
// namespace, a pointer to the parent for a child namespace).
// ---------------------------------------------------------------------------
TEST(INamespaceTest, ParentNamespaceReturnsConfiguredNullablePointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    TestNamespace root("", "", "", compilation);
    TestNamespace child("Collections", "System.Collections", "", compilation);
    child.SetParentNamespace(&root);

    EXPECT_EQ(root.ParentNamespace(), nullptr);
    EXPECT_EQ(child.ParentNamespace(), &root);
}

// ---------------------------------------------------------------------------
// INamespace -- `ChildNamespaces` returns the configured non-owning snapshot (pointer identity
// preserved; the namespace owns its children, the caller holds raw pointers).
// ---------------------------------------------------------------------------
TEST(INamespaceTest, ChildNamespacesReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    TestNamespace system("System", "System", "", compilation);
    TestNamespace collections("Collections", "System.Collections", "", compilation);
    TestNamespace io("IO", "System.IO", "", compilation);
    system.SetChildNamespaces({&collections, &io});

    const auto children = system.ChildNamespaces();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], &collections);
    EXPECT_EQ(children[1], &io);
}

// ---------------------------------------------------------------------------
// INamespace -- `Types` returns the configured non-owning `const ITypeDefinition*` snapshot
// (pointer identity preserved), and `GetTypeDefinition(name, count)` looks a type up by its
// short name and type parameter count (returning the configured pointer, or null when not
// found). `ITypeDefinition` is the real port (D393); the compact `TestTypeDefinition` stands in
// for the concrete type definitions.
// ---------------------------------------------------------------------------
TEST(INamespaceTest, TypesAndGetTypeDefinitionReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    TestTypeDefinition list("System.Collections.Generic.List`1", compilation, 0x02000001u);
    TestTypeDefinition dict("System.Collections.Generic.Dictionary`2", compilation, 0x02000002u);
    TestNamespace generic("Generic", "System.Collections.Generic", "", compilation);
    generic.SetTypes({&list, &dict});

    const auto types = generic.Types();
    ASSERT_EQ(types.size(), 2u);
    EXPECT_EQ(types[0], &list);
    EXPECT_EQ(types[1], &dict);
    EXPECT_EQ(types[0]->Name(), "List");
    EXPECT_EQ(types[1]->TypeParameterCount(), 2);

    // `GetTypeDefinition` finds by (short name, type parameter count).
    EXPECT_EQ(generic.GetTypeDefinition("List", 1), &list);
    EXPECT_EQ(generic.GetTypeDefinition("Dictionary", 2), &dict);
    // A generic arity mismatch misses.
    EXPECT_EQ(generic.GetTypeDefinition("List", 0), nullptr);
    // A name mismatch misses.
    EXPECT_EQ(generic.GetTypeDefinition("Queue", 1), nullptr);
}

// ---------------------------------------------------------------------------
// INamespace -- `ContributingModules` returns the configured non-owning `const IModule*`
// snapshot (pointer identity preserved). `IModule` is now the real port (D396, an abstract
// interface), so the snapshot element type is the abstract `IModule` and the test uses a
// concrete `TestModule` stub for the instances.
// ---------------------------------------------------------------------------
TEST(INamespaceTest, ContributingModulesReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    TestModule moduleA(compilation);
    TestModule moduleB(compilation);
    TestNamespace system("System", "System", "", compilation);
    system.SetContributingModules({&moduleA, &moduleB});

    const auto modules = system.ContributingModules();
    ASSERT_EQ(modules.size(), 2u);
    EXPECT_EQ(modules[0], &moduleA);
    EXPECT_EQ(modules[1], &moduleB);
}

// ---------------------------------------------------------------------------
// INamespace -- `GetChildNamespace(name)` finds a direct child by its short name (returning
// the configured pointer, or null when not found).
// ---------------------------------------------------------------------------
TEST(INamespaceTest, GetChildNamespaceFindsByShortName)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    TestNamespace system("System", "System", "", compilation);
    TestNamespace collections("Collections", "System.Collections", "", compilation);
    TestNamespace io("IO", "System.IO", "", compilation);
    system.SetChildNamespaces({&collections, &io});

    EXPECT_EQ(system.GetChildNamespace("Collections"), &collections);
    EXPECT_EQ(system.GetChildNamespace("IO"), &io);
    EXPECT_EQ(system.GetChildNamespace("Threading"), nullptr);
}

// ---------------------------------------------------------------------------
// INamespace -- the inherited `ISymbol` / `ICompilationProvider` accessors dispatch through the
// `INamespace*`: `SymbolKind` (Namespace), the single `Name`, and `Compilation` (the parent
// compilation). `INamespace : ISymbol, ICompilationProvider` (NOT `IEntity`), so there is NO
// attribute family here -- only the two inherited-base surfaces plus the eight own accessors.
// ---------------------------------------------------------------------------
TEST(INamespaceTest, InheritedAccessorsDispatchThroughINamespacePointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(7);
    TestNamespace system("System", "System", "", compilation);
    INamespace* ns = &system;

    EXPECT_EQ(ns->SymbolKind(), SymbolKind::Namespace);
    EXPECT_EQ(ns->Name(), "System");
    EXPECT_EQ(&ns->Compilation(), &compilation);
}

// ---------------------------------------------------------------------------
// INamespace -- polymorphic dispatch through the `ISymbol*` / `ICompilationProvider*` base
// pointers (an `INamespace` IS-A each of its bases, so each base pointer dispatches to the
// concrete override, including the single `Name()` that is the final overrider for the
// inherited `ISymbol::Name()`).
// ---------------------------------------------------------------------------
TEST(INamespaceTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(8);
    auto owned = std::make_unique<TestNamespace>(
        "System", "System", "", compilation);
    ISymbol* asSymbol = owned.get();
    ICompilationProvider* asProvider = owned.get();
    EXPECT_EQ(asSymbol->Name(), "System");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Namespace);
    EXPECT_EQ(&asProvider->Compilation(), &compilation);
    // destroying `owned` runs the `TestNamespace` destructor through the virtual `~INamespace()`
    // (which chains to `~ISymbol()` / `~ICompilationProvider()`).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// INamespace -- has a virtual destructor (a concrete subclass can be deleted through an
// `INamespace*` / `ISymbol*` / `ICompilationProvider*` and the derived destructor runs), the
// established abstract-base contract; it is abstract (every accessor is pure-virtual) and
// polymorphic. The multiple-inheritance `INamespace : ISymbol, ICompilationProvider` composes
// correctly with NO shared-base diamond (only `ISymbol` declares `Name`; `ICompilationProvider`
// does not derive from `ISymbol`), so no `Name` / `SymbolKind` redeclaration is needed.
// ---------------------------------------------------------------------------
TEST(INamespaceTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::INamespace>,
        "INamespace must have a virtual destructor for abstract-base deletion");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::INamespace>,
        "INamespace must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::INamespace>,
        "INamespace must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(9);
    std::unique_ptr<INamespace> owned = std::make_unique<TestNamespace>(
        "System", "System", "", compilation);
    EXPECT_EQ(owned->Name(), "System");
    EXPECT_EQ(owned->SymbolKind(), SymbolKind::Namespace);
    // destroying `owned` runs the `TestNamespace` destructor through the virtual `~INamespace()`.
    owned.reset();
    SUCCEED();
}
