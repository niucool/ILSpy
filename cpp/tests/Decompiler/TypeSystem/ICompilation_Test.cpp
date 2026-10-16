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

// Tests for `ICompilation` (cpp/Decompiler/TypeSystem/ICompilation.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/ICompilation.cs) -- the resolved-compilation capstone
// of the cyclic `ICompilation` <-> `ICompilationProvider` <-> `IModule` / `INamespace`
// chain. A compilation bundles the `MainModule` (the assembly being decompiled) with its
// `Modules` / `ReferencedModules`, the merged `RootNamespace`, the extern-alias namespace
// lookup, the `FindType` known-type resolver, the per-language `NameComparer`, the
// per-compilation `CacheManager`, and the `TypeSystemOptions`.
//
// The C# `interface ICompilation` ports to a C++ abstract base with NO base classes (it
// is a root interface, the type `ICompilationProvider::Compilation()` returns). It is a
// leaf TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`). All its deps are now ported: `IModule` (D396),
// `INamespace` (D394), `IType` / `KnownTypeCode` (the D271 minimal port), `StringComparer`
// (D398), `CacheManager` (D397), `TypeSystemOptions` (D376).
//
// The test stubs:
//  - `TestCompilation` derives from the real `ICompilation` and overrides the nine
//    pure-virtuals with meaningful returns: the `MainModule` / `RootNamespace` /
//    `FindType` / `CacheManager` accessors bind to held members (a `TestSupport::TestModule`
//    for the module, a separate `TestSupport::TestNamespace` for the merged root namespace --
//    distinct from the module's own root namespace -- `KnownType` instances for the known
//    types, and a `CacheManager`). `NameComparer` returns `StringComparer::Ordinal()`.
//    `GetNamespaceForExternAlias("")` returns the root namespace (the C# empty-alias ==
//    global-root contract), unknown aliases return null. The shared `TestSupport::TestModule`
//    / `TestNamespace` (from `TestCompilationStubs.hpp`) back the module and namespace
//    members; the dedicated test does NOT need a file-local `ICompilation` stand-in (the
//    real header is included via `TestCompilationStubs.hpp`).

#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// A concrete `ICompilation` for testing the full nine-accessor surface. Unlike the
// trivial `TestCompilation` the reconciled files carry (which only need to compile),
// this one returns MEANINGFUL values so the tests can assert the accessors dispatch and
// return the configured state. `mainModule_` (the `TestSupport::TestModule` shared stub)
// backs `MainModule`; `rootNamespace_` (a SEPARATE `TestSupport::TestNamespace`) backs the
// compilation's `RootNamespace` (the merged root, distinct from the module's own root
// namespace, which is what `mainModule_.RootNamespace()` returns); `knownTypeObject_` /
// `knownTypeString_` / `knownTypeVoid_` back `FindType` for the three sampled codes.
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions opts)
        : opts_(opts),
          mainModule_(*this),
          rootNamespace_(*this),
          knownTypeObject_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object),
          knownTypeString_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String),
          knownTypeVoid_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void) {}

    const ILSpy::Decompiler::TypeSystem::IModule& MainModule() const override
    {
        return mainModule_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> Modules() const override
    {
        return {&mainModule_};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ReferencedModules() const override
    {
        return {};
    }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override
    {
        return rootNamespace_;
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetNamespaceForExternAlias(
        const std::string& alias) const override
    {
        // The C# contract: a null or empty alias yields the global root namespace.
        if (alias.empty()) {
            return &rootNamespace_;
        }
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IType& FindType(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode) const override
    {
        switch (typeCode) {
            case ILSpy::Decompiler::TypeSystem::KnownTypeCode::String:
                return knownTypeString_;
            case ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void:
                return knownTypeVoid_;
            case ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object:
            default:
                return knownTypeObject_;
        }
    }
    const ILSpy::Decompiler::TypeSystem::StringComparer& NameComparer() const override
    {
        return ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override
    {
        return opts_;
    }

private:
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions opts_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestNamespace rootNamespace_;
    ILSpy::Decompiler::TypeSystem::KnownType knownTypeObject_;
    ILSpy::Decompiler::TypeSystem::KnownType knownTypeString_;
    ILSpy::Decompiler::TypeSystem::KnownType knownTypeVoid_;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

} // namespace

// ---------------------------------------------------------------------------
// ICompilation -- the MainModule accessor returns the configured module (non-null
// reference, the `IVariable::Type()` non-null-reference convention). Identity is pinned
// by pointer compare against the first entry of `Modules()` (the main module is the
// first module, per the C# `IReadOnlyList<IModule> Modules` doc comment).
// ---------------------------------------------------------------------------
TEST(ICompilationTest, MainModuleReturnsConfiguredModule)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    const auto* main = &compilation.MainModule();
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
}

TEST(ICompilationTest, ModulesReturnsSnapshotContainingMainModuleFirst)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    auto modules = compilation.Modules();
    EXPECT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0], &compilation.MainModule());
}

TEST(ICompilationTest, ReferencedModulesIsEmptyForASingleModuleCompilation)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_TRUE(compilation.ReferencedModules().empty());
}

// ---------------------------------------------------------------------------
// ICompilation -- the RootNamespace accessor returns the merged root namespace
// (non-null reference). It is DISTINCT from the main module's own root namespace
// (the module's `RootNamespace()` returns the module's sub-namespace tree; the
// compilation's `RootNamespace()` is the merged root of all assemblies).
// ---------------------------------------------------------------------------
TEST(ICompilationTest, RootNamespaceReturnsConfiguredNamespace)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    const auto* root = &compilation.RootNamespace();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace);
    // The compilation's root namespace is distinct from the main module's own root.
    EXPECT_NE(&compilation.RootNamespace(), &compilation.MainModule().RootNamespace());
}

// ---------------------------------------------------------------------------
// ICompilation -- GetNamespaceForExternAlias: an empty alias yields the global root
// namespace (the C# contract: "If alias is null or an empty string, this method returns
// the global root namespace"); an unknown alias yields null.
// ---------------------------------------------------------------------------
TEST(ICompilationTest, GetNamespaceForExternAliasReturnsRootForEmptyAlias)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(compilation.GetNamespaceForExternAlias(""), &compilation.RootNamespace());
}

TEST(ICompilationTest, GetNamespaceForExternAliasReturnsNullForUnknownAlias)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(compilation.GetNamespaceForExternAlias("nonexistent"), nullptr);
}

// ---------------------------------------------------------------------------
// ICompilation -- FindType returns the `IType` for a `KnownTypeCode` (non-null
// reference). Different codes return different `IType` instances; the `KnownType`
// `Kind()` / `Name()` reflect the code's entry in the known-type table.
// ---------------------------------------------------------------------------
TEST(ICompilationTest, FindTypeReturnsKnownTypeForCode)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object).Kind(),
              ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object).Name(),
              "Object");
    EXPECT_EQ(compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String).Name(),
              "String");
    EXPECT_EQ(compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void).Kind(),
              ILSpy::Decompiler::TypeSystem::TypeKind::Void);
    // Distinct codes return distinct IType instances.
    EXPECT_NE(&compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object),
              &compilation.FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String));
}

// ---------------------------------------------------------------------------
// ICompilation -- the NameComparer accessor returns the configured string comparer
// (`SimpleCompilation.NameComparer` returns `StringComparer.Ordinal`). Identity is
// pinned against the `StringComparer::Ordinal()` singleton.
// ---------------------------------------------------------------------------
TEST(ICompilationTest, NameComparerReturnsStringComparerOrdinal)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(&compilation.NameComparer(),
              &ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal());
}

// ---------------------------------------------------------------------------
// ICompilation -- the CacheManager accessor returns the per-compilation cache (non-null
// reference, stable across calls -- the same instance every time).
// ---------------------------------------------------------------------------
TEST(ICompilationTest, CacheManagerReturnsStableInstance)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(&compilation.CacheManager(), &compilation.CacheManager());
}

// ---------------------------------------------------------------------------
// ICompilation -- the TypeSystemOptions accessor returns the configured value by value.
// ---------------------------------------------------------------------------
TEST(ICompilationTest, TypeSystemOptionsReturnsConfiguredValue)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_EQ(compilation.TypeSystemOptions(), ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);

    TestCompilation withDefaults(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default);
    EXPECT_EQ(static_cast<std::uint32_t>(withDefaults.TypeSystemOptions()),
              static_cast<std::uint32_t>(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default));
}

// ---------------------------------------------------------------------------
// ICompilation -- polymorphic dispatch through an `ICompilation*` (the dynamic dispatch
// the type-system paths rely on: `TypeSystemAstBuilder` / `CSharpResolver` hold an
// `ICompilation` and reach the modules / namespaces / known types / name comparer / cache
// through the base pointer).
// ---------------------------------------------------------------------------
TEST(ICompilationTest, DispatchesPolymorphicallyThroughBasePointer)
{
    TestCompilation compilation(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default);
    const ILSpy::Decompiler::TypeSystem::ICompilation* base = &compilation;
    EXPECT_EQ(base->MainModule().SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
    EXPECT_EQ(base->RootNamespace().SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace);
    EXPECT_EQ(&base->NameComparer(), &ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal());
    EXPECT_EQ(base->FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object).Name(), "Object");
    EXPECT_EQ(static_cast<std::uint32_t>(base->TypeSystemOptions()),
              static_cast<std::uint32_t>(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default));
}

// ---------------------------------------------------------------------------
// ICompilation -- the interface is abstract (cannot be instantiated) and has a virtual
// destructor; a concrete subclass that overrides all nine pure-virtuals is instantiable.
// ---------------------------------------------------------------------------
TEST(ICompilationTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ICompilation>,
                  "ICompilation must have a virtual destructor.");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::ICompilation>,
                  "ICompilation must be abstract (nine pure-virtual accessors).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::ICompilation>,
                  "ICompilation must be polymorphic.");
    // A concrete subclass overriding all nine pure-virtuals is instantiable.
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ICompilation> p =
        std::make_unique<TestCompilation>(ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None);
    EXPECT_NE(p, nullptr);
    p.reset();
}
