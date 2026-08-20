// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
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

// Tests for `IModule` (cpp/Decompiler/TypeSystem/IModule.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IAssembly.cs -- the `IModule` interface defined
// alongside `IModuleReference`). `IModule : ISymbol, ICompilationProvider` represents a
// resolved metadata module; it exposes the nullable `MetadataFile`, the `IsMainModule`
// flag, the assembly `AssemblyName` / `AssemblyVersion` / `FullAssemblyName`, the
// `GetAssemblyAttributes` / `GetModuleAttributes` snapshots, the `InternalsVisibleTo`
// query, the `RootNamespace`, and the `GetTypeDefinition` / `TopLevelTypeDefinitions` /
// `TypeDefinitions` lookups. It does NOT derive from `IEntity` / `INamedElement` (unlike
// the member family), so it carries NO inherited attribute family -- only the inherited
// `ISymbol::SymbolKind` / `ISymbol::Name` and `ICompilationProvider::Compilation`.
//
// `IModule` is the structural twin of `INamespace` (D394): both are
// `ISymbol, ICompilationProvider` with a single `ISymbol` subobject, so `Name` AND
// `SymbolKind` are unambiguous through the interface pointer with NO redeclaration (the
// D394 single-ISymbol-subobject precedent). The concrete `TestModule` overrides `Name()`
// once and dispatch through `IModule*` / `ISymbol*` both reach it.
//
// The test stubs:
//  - `TestModule` derives from the real `IModule` and overrides the inherited `ISymbol` /
//    `ICompilationProvider` surface plus the twelve `IModule`-own accessors (the shape a
//    real `MetadataModule` / `SimpleModule` takes). It holds a `TestNamespace` (for the
//    non-null `RootNamespace` reference return) and a `TestCompilation` (for the
//    `Compilation` reference return and to back the namespace).
//  - `TestNamespace` is a COMPACT concrete `INamespace` (every pure-virtual overridden with
//    a trivial return) used ONLY so `TestModule::RootNamespace()` can return a valid
//    `const INamespace&` -- the IModule tests never read an `INamespace` accessor through
//    it. It lives in the anonymous namespace (file-local), so it does not ODR-conflict with
//    the richer `TestNamespace` in `INamespace_Test.cpp`.
//  - `TestTypeDefinition` is a COMPACT concrete `ITypeDefinition` (every pure-virtual
//    overridden with a trivial return, only `Name` / `ReflectionName` /
//    `TypeParameterCount` derive from a stored `FullTypeName`) used ONLY for pointer
//    identity in the `GetTypeDefinition` / `TopLevelTypeDefinitions` / `TypeDefinitions`
//    snapshots -- the IModule tests read only `Name()` through the snapshot pointers (the
//    `GetTypeDefinition` lookup compares `TopLevelTypeName`s, which carry the name).
//  - `TestAttribute` derives from the real `IAttribute` (D386) and implements every
//    pure-virtual with simple defaults; its `Kind()` accessor backs the
//    `GetAssemblyAttributes` / `GetModuleAttributes` pointer-identity tests.
//  - `TestCompilation` is the minimal concrete `ICompilation` stand-in (the D379 pattern);
//    the `ICompilation` stand-in is defined IDENTICALLY to the other TypeSystem test files
//    (ODR-safe across translation units).

#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Minimal test stand-in for `ICompilation` (the parent compilation interface). IDENTICAL to
// the stand-in in the other TypeSystem test files (a virtual destructor only); the
// identical class definitions across translation units satisfy the One Definition Rule.
// Replaced by the real `ICompilation.hpp` when that lands.
class ICompilation {
public:
    virtual ~ICompilation() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider`
// base can return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A COMPACT concrete `ITypeDefinition` for testing: used ONLY for pointer identity in the
// `IModule::GetTypeDefinition` / `TopLevelTypeDefinitions` / `TypeDefinitions` snapshots.
// It overrides every pure-virtual with a trivial return, except `Name` / `ReflectionName`
// / `TypeParameterCount` which derive from the stored `FullTypeName`. The return types
// `SymbolKind` / `KnownTypeCode` / `FullTypeName` / `ExtensionInfo` / `Accessibility` /
// `Nullability` / `TypeKind` are GLOBALLY QUALIFIED because the stub INHERITS the
// same-named member functions (the D372 cross-scope name-hiding crux). It lives in the
// anonymous namespace (file-local), so it does not ODR-conflict with the
// `TestTypeDefinition` in other test files.
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    explicit TestTypeDefinition(std::string reflectionName)
        : fullTypeName_(reflectionName), metadataName_(std::move(reflectionName)) {}

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
    // --- ICompilationProvider (declared here, defined out-of-line below after
    // `TestCompilation` is complete; returns a reference to a function-local singleton) ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;
    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0u; }
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
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
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
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType&) const override { return false; }

private:
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    std::string metadataName_;
};

// Out-of-line `TestTypeDefinition::Compilation()` (declared above): returns a reference to a
// function-local `TestCompilation` singleton. Defined here (after `TestCompilation` is
// complete) so the `TestCompilation` -> `const ICompilation&` derived-to-base conversion
// resolves. The IModule tests never read `Compilation()` through a `TestTypeDefinition`, so
// a shared singleton is harmless.
const ILSpy::Decompiler::TypeSystem::ICompilation& TestTypeDefinition::Compilation() const
{
    static TestCompilation s_compilation(0);
    return s_compilation;
}

// A COMPACT concrete `INamespace` for testing: used ONLY so `TestModule::RootNamespace()`
// can return a valid `const INamespace&` (the IModule tests never read an `INamespace`
// accessor through it -- they only check reference identity). It overrides every
// `INamespace` / `ISymbol` / `ICompilationProvider` pure-virtual with a trivial return. It
// lives in the anonymous namespace (file-local), so it does not ODR-conflict with the
// richer `TestNamespace` in `INamespace_Test.cpp`.
class TestNamespace : public ILSpy::Decompiler::TypeSystem::INamespace {
public:
    explicit TestNamespace(const TestCompilation& compilation) : compilation_(compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override { return {}; }
    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }
    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::INamespace* ParentNamespace() const override
    {
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> ChildNamespaces() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> Types() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules() const override
    {
        return {};
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetChildNamespace(
        const std::string&) const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const std::string&, int) const override
    {
        return nullptr;
    }

private:
    const TestCompilation& compilation_;
};

// A minimal concrete `IAttribute` for testing (the D386 pattern): `AttributeType` returns a
// `KnownType(Object)` by reference, `Constructor` is null, `HasDecodeErrors` is false, the
// argument vectors are empty. The test-specific `Kind()` accessor and `kind_` member back
// the `GetAssemblyAttributes` / `GetModuleAttributes` pointer-identity tests.
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute kind)
        : kind_(kind), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    ILSpy::Decompiler::TypeSystem::KnownAttribute Kind() const { return kind_; }

    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override
    {
        return {};
    }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override
    {
        return {};
    }

private:
    ILSpy::Decompiler::TypeSystem::KnownAttribute kind_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `IModule` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataModule` / `SimpleModule`
// takes). `Name()` is overridden ONCE and satisfies the inherited `ISymbol::Name()`
// contract (single inheritance -- `ICompilationProvider` declares no `Name`, so the
// inherited `ISymbol::Name()` is the only `Name`, the D374 precedent). `RootNamespace()`
// returns a reference to the held `TestNamespace` (non-null, the `IVariable::Type()`
// non-null-reference convention). `GetTypeDefinition` does a linear search over the
// configured `TopLevelTypeDefinitions` by `TopLevelTypeName::operator==` (the C# uses
// ordinal name comparison; a plain `==` suffices for the tests).
class TestModule : public ILSpy::Decompiler::TypeSystem::IModule {
public:
    TestModule(std::string assemblyName,
               std::string fullAssemblyName,
               bool isMainModule,
               const TestCompilation& compilation)
        : assemblyName_(std::move(assemblyName)),
          fullAssemblyName_(std::move(fullAssemblyName)),
          isMainModule_(isMainModule),
          compilation_(compilation),
          rootNamespace_(compilation_) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
    }
    std::string Name() const override { return assemblyName_; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IModule ---
    const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override
    {
        return metadataFile_;
    }
    bool IsMainModule() const override { return isMainModule_; }
    std::string AssemblyName() const override { return assemblyName_; }
    ILSpy::Decompiler::TypeSystem::Version AssemblyVersion() const override { return assemblyVersion_; }
    std::string FullAssemblyName() const override { return fullAssemblyName_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAssemblyAttributes() const override
    {
        return assemblyAttributes_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetModuleAttributes() const override
    {
        return moduleAttributes_;
    }
    bool InternalsVisibleTo(const ILSpy::Decompiler::TypeSystem::IModule& module) const override
    {
        for (auto* m : internalsVisibleTo_) {
            if (m == &module) return true;
        }
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override
    {
        return rootNamespace_;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName& topLevelTypeName) const override
    {
        for (auto* td : topLevelTypeDefinitions_) {
            if (td->FullTypeName().GetTopLevelTypeName() == topLevelTypeName) return td;
        }
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TopLevelTypeDefinitions() const override
    {
        return topLevelTypeDefinitions_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TypeDefinitions() const override
    {
        return typeDefinitions_;
    }

    // Test wiring (set the nullable / collection slots after construction).
    void SetMetadataFile(const ILSpy::Decompiler::Metadata::MetadataFile* mf) { metadataFile_ = mf; }
    void SetAssemblyVersion(ILSpy::Decompiler::TypeSystem::Version v) { assemblyVersion_ = v; }
    void AddAssemblyAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a)
    {
        assemblyAttributes_.push_back(a);
    }
    void AddModuleAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a)
    {
        moduleAttributes_.push_back(a);
    }
    void AddInternalsVisibleTo(const ILSpy::Decompiler::TypeSystem::IModule* m)
    {
        internalsVisibleTo_.push_back(m);
    }
    void SetTopLevelTypeDefinitions(
        std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> v)
    {
        topLevelTypeDefinitions_ = std::move(v);
    }
    void SetTypeDefinitions(std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> v)
    {
        typeDefinitions_ = std::move(v);
    }

private:
    std::string assemblyName_, fullAssemblyName_;
    bool isMainModule_;
    const TestCompilation& compilation_;
    TestNamespace rootNamespace_;
    const ILSpy::Decompiler::Metadata::MetadataFile* metadataFile_ = nullptr;
    ILSpy::Decompiler::TypeSystem::Version assemblyVersion_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> assemblyAttributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> moduleAttributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> internalsVisibleTo_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> topLevelTypeDefinitions_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> typeDefinitions_;
};

} // namespace

// ---------------------------------------------------------------------------
// IModule -- the scalar accessors return the configured values: `MetadataFile` (null by
// default -- a module may not originate from a file), `IsMainModule`, `AssemblyName`, and
// `FullAssemblyName`.
// ---------------------------------------------------------------------------
TEST(IModuleTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    TestModule module("mscorlib", "mscorlib, Version=4.0.0.0", true, compilation);

    EXPECT_EQ(module.MetadataFile(), nullptr);
    EXPECT_TRUE(module.IsMainModule());
    EXPECT_EQ(module.AssemblyName(), "mscorlib");
    EXPECT_EQ(module.FullAssemblyName(), "mscorlib, Version=4.0.0.0");
}

// ---------------------------------------------------------------------------
// IModule -- `AssemblyVersion` returns the configured `Version` by value (the C# property
// returns the `System.Version` value type). A round-trip through `ToString` confirms the
// full four-component form is preserved.
// ---------------------------------------------------------------------------
TEST(IModuleTest, AssemblyVersionReturnsConfiguredVersion)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    TestModule module("System", "System", false, compilation);
    module.SetAssemblyVersion(Version(4, 0, 0, 0));

    EXPECT_EQ(module.AssemblyVersion(), Version(4, 0, 0, 0));
    EXPECT_EQ(module.AssemblyVersion().ToString(), "4.0.0.0");
}

// ---------------------------------------------------------------------------
// IModule -- `GetAssemblyAttributes` / `GetModuleAttributes` return the configured
// non-owning `const IAttribute*` snapshots (pointer identity preserved).
// ---------------------------------------------------------------------------
TEST(IModuleTest, AttributeSnapshotsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    TestModule module("mscorlib", "mscorlib", false, compilation);
    TestAttribute asmAttr(KnownAttribute::AssemblyVersion);
    TestAttribute modAttr(KnownAttribute::NonSerialized);
    module.AddAssemblyAttribute(&asmAttr);
    module.AddModuleAttribute(&modAttr);

    const auto assemblyAttrs = module.GetAssemblyAttributes();
    ASSERT_EQ(assemblyAttrs.size(), 1u);
    EXPECT_EQ(assemblyAttrs[0], &asmAttr);
    EXPECT_EQ(static_cast<const TestAttribute*>(assemblyAttrs[0])->Kind(),
              KnownAttribute::AssemblyVersion);

    const auto moduleAttrs = module.GetModuleAttributes();
    ASSERT_EQ(moduleAttrs.size(), 1u);
    EXPECT_EQ(moduleAttrs[0], &modAttr);
    EXPECT_EQ(static_cast<const TestAttribute*>(moduleAttrs[0])->Kind(),
              KnownAttribute::NonSerialized);
}

// ---------------------------------------------------------------------------
// IModule -- `InternalsVisibleTo(module)` returns true when the specified module is in the
// configured list, false otherwise.
// ---------------------------------------------------------------------------
TEST(IModuleTest, InternalsVisibleToQueriesConfiguredModule)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    TestModule self("Self", "Self", true, compilation);
    TestModule friend1("Friend1", "Friend1", false, compilation);
    TestModule friend2("Friend2", "Friend2", false, compilation);
    TestModule stranger("Stranger", "Stranger", false, compilation);
    self.AddInternalsVisibleTo(&friend1);
    self.AddInternalsVisibleTo(&friend2);

    EXPECT_TRUE(self.InternalsVisibleTo(friend1));
    EXPECT_TRUE(self.InternalsVisibleTo(friend2));
    EXPECT_FALSE(self.InternalsVisibleTo(stranger));
}

// ---------------------------------------------------------------------------
// IModule -- `RootNamespace` returns a non-null `const INamespace&` (reference identity:
// the same `INamespace` is returned across calls). The C# property is non-null (always the
// nameless root namespace for this module).
// ---------------------------------------------------------------------------
TEST(IModuleTest, RootNamespaceReturnsConfiguredNamespace)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    TestModule module("mscorlib", "mscorlib", false, compilation);

    const INamespace& root1 = module.RootNamespace();
    const INamespace& root2 = module.RootNamespace();
    EXPECT_EQ(&root1, &root2);
    EXPECT_EQ(root1.SymbolKind(), SymbolKind::Namespace);
}

// ---------------------------------------------------------------------------
// IModule -- `GetTypeDefinition(TopLevelTypeName)` finds the top-level type by its full
// name (namespace + name + type-parameter count) and returns null when not found.
// ---------------------------------------------------------------------------
TEST(IModuleTest, GetTypeDefinitionFindsByTopLevelTypeName)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    TestModule module("mscorlib", "mscorlib", false, compilation);
    TestTypeDefinition list("System.Collections.Generic.List`1");
    TestTypeDefinition dictionary("System.Collections.Generic.Dictionary`2");
    module.SetTopLevelTypeDefinitions({&list, &dictionary});

    EXPECT_EQ(module.GetTypeDefinition(TopLevelTypeName("System.Collections.Generic", "List", 1)), &list);
    EXPECT_EQ(module.GetTypeDefinition(
                  TopLevelTypeName("System.Collections.Generic", "Dictionary", 2)),
              &dictionary);
    EXPECT_EQ(module.GetTypeDefinition(
                  TopLevelTypeName("System.Collections.Generic", "Queue", 1)),
              nullptr);
}

// ---------------------------------------------------------------------------
// IModule -- `TopLevelTypeDefinitions` / `TypeDefinitions` return the configured
// non-owning `const ITypeDefinition*` snapshots (pointer identity preserved). They are
// independent collections: `TypeDefinitions` may include nested types that
// `TopLevelTypeDefinitions` omits.
// ---------------------------------------------------------------------------
TEST(IModuleTest, TopLevelTypeDefinitionsAndTypeDefinitionsReturnSnapshots)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(7);
    TestModule module("mscorlib", "mscorlib", false, compilation);
    TestTypeDefinition list("System.Collections.Generic.List`1");
    TestTypeDefinition enumerator("System.Collections.Generic.List`1+Enumerator");
    module.SetTopLevelTypeDefinitions({&list});
    module.SetTypeDefinitions({&list, &enumerator});

    const auto topLevel = module.TopLevelTypeDefinitions();
    ASSERT_EQ(topLevel.size(), 1u);
    EXPECT_EQ(topLevel[0], &list);

    const auto all = module.TypeDefinitions();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0], &list);
    EXPECT_EQ(all[1], &enumerator);
}

// ---------------------------------------------------------------------------
// IModule -- the inherited `ISymbol` / `ICompilationProvider` accessors dispatch through the
// `IModule*`: `SymbolKind` (Module), the single `Name`, and `Compilation` (the parent
// compilation). `IModule : ISymbol, ICompilationProvider` (NOT `IEntity`), so there is NO
// attribute family on the interface itself (only the `IModule`-own
// `GetAssemblyAttributes` / `GetModuleAttributes`).
// ---------------------------------------------------------------------------
TEST(IModuleTest, InheritedAccessorsDispatchThroughIModulePointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(8);
    TestModule module("mscorlib", "mscorlib, Version=4.0.0.0", true, compilation);

    EXPECT_EQ(module.SymbolKind(), SymbolKind::Module);
    EXPECT_EQ(module.Name(), "mscorlib");
    EXPECT_EQ(&module.Compilation(), &compilation);
}

// ---------------------------------------------------------------------------
// IModule -- every IModule-own + inherited accessor dispatches polymorphically through the
// base pointers `ISymbol*` and `ICompilationProvider*` (single `ISymbol` subobject, so the
// upcasts are unambiguous, the D394 single-ISymbol-subobject precedent). The IModule-own
// accessors (`AssemblyName` / `IsMainModule`) are reachable through `IModule*` but NOT
// through `ISymbol*` / `ICompilationProvider*`.
// ---------------------------------------------------------------------------
TEST(IModuleTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(9);
    TestModule module("System", "System", false, compilation);

    const ISymbol* asSymbol = &module;
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Module);
    EXPECT_EQ(asSymbol->Name(), "System");

    const ICompilationProvider* asProvider = &module;
    EXPECT_EQ(&asProvider->Compilation(), &compilation);

    // IModule-own accessors are reachable through IModule* only.
    EXPECT_EQ(module.AssemblyName(), "System");
    EXPECT_FALSE(module.IsMainModule());
}

// ---------------------------------------------------------------------------
// IModule -- `static_assert`s on the virtual destructor / abstract / polymorphic traits,
// and a `unique_ptr` reset confirms the destructor is virtual (no slicing / no UB when
// deleted through a base pointer).
// ---------------------------------------------------------------------------
TEST(IModuleTest, HasVirtualDestructorAndIsAbstract)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    static_assert(std::has_virtual_destructor_v<IModule>, "IModule must have a virtual destructor");
    static_assert(std::is_abstract_v<IModule>, "IModule must be abstract");
    static_assert(std::is_polymorphic_v<IModule>, "IModule must be polymorphic");

    TestCompilation compilation(10);
    auto owned = std::make_unique<TestModule>("mscorlib", "mscorlib", true, compilation);
    std::unique_ptr<IModule> base = std::move(owned);
    base.reset();
    SUCCEED();
}
