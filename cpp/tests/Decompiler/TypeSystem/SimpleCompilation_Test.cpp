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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `SimpleCompilation` (cpp/Decompiler/TypeSystem/SimpleCompilation.hpp, the
// port of `ICSharpCode.Decompiler.TypeSystem.Implementation.SimpleCompilation`) -- the
// concrete `ICompilation` (D399). A `SimpleCompilation` resolves a main module plus its
// referenced modules (via `IModuleReference.Resolve`), exposes them as `Modules` (the
// main first) / `ReferencedModules`, builds a merged `RootNamespace` (a `MergedNamespace`
// over the root namespaces of the main and referenced modules, lazily via `LazyInit`),
// forwards `FindType` to a `KnownTypeCache`, and returns the `StringComparer::Ordinal`
// name comparer, the `CacheManager`, and the `TypeSystemOptions::Default` options.
//
// The test stubs:
//  - `TestSimpleCompilation` derives from `SimpleCompilation` and exposes the
//    `protected` default ctor + `Init` as `public` (via `using` declarations) so the
//    tests can use the two-phase construction (default ctor, then `Init`), which avoids
//    the chicken-and-egg of the module references needing an `ICompilation` before the
//    compilation exists: the default-constructed `TestSimpleCompilation` exists as an
//    `ICompilation` (its `knownTypeCache_` is bound to `*this`), so the test modules
//    can bind to it, and then `Init` resolves them. `SimpleCompilation`'s own logic
//    never reads `module->Compilation()` (the modules' `Compilation` is dead in the
//    tested paths), so binding the test modules to the not-yet-initialized compilation
//    is safe.
//  - `TestModule` derives from `TestSupport::TestModule` (the shared stub) and overrides
//    `AssemblyName` / `RootNamespace` with configurable values (the base's trivial
//    overrides cover the rest of the `IModule` surface).
//  - `TestModuleRef` is a concrete `IModuleReference` holding a nullable `const IModule*`
//    it returns from `Resolve` (ignoring the context) -- the D418 test-stub pattern.
//  - `TestThrowingModuleRef` throws `std::runtime_error` from `Resolve` (for the
//    `Init` exception-wrapping test).
//  - `TestNamespace2` derives from `TestSupport::TestNamespace` and overrides `Name` /
//    `FullName` / `ChildNamespaces` with configurable values (for the merged-root test).

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
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

using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IModuleReference;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SimpleCompilation;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using TestSupportModule = ILSpy::Decompiler::TypeSystem::TestSupport::TestModule;
using TestSupportNamespace = ILSpy::Decompiler::TypeSystem::TestSupport::TestNamespace;

// A `SimpleCompilation` subclass that exposes the `protected` default ctor + `Init` as
// `public` so the tests can use the two-phase construction (default ctor, then `Init`).
// Inheriting the base ctors (`using SimpleCompilation::SimpleCompilation;`) brings the
// public ctor (and the protected default ctor) into the public section; `using
// SimpleCompilation::Init;` brings `Init` into the public section.
class TestSimpleCompilation : public SimpleCompilation {
public:
    using SimpleCompilation::SimpleCompilation;
    using SimpleCompilation::Init;
};

// A concrete `IModule` for the resolved modules: derives from the shared
// `TestSupport::TestModule` (which handles the `Compilation` binding and the trivial
// overrides) and overrides `AssemblyName` / `RootNamespace` with configurable values.
class TestModule : public TestSupportModule {
public:
    TestModule(std::string assemblyName, const INamespace& rootNamespace,
               const ICompilation& compilation)
        : TestSupportModule(compilation),
          assemblyName_(std::move(assemblyName)),
          rootNamespaceRef_(rootNamespace) {}

    std::string AssemblyName() const override { return assemblyName_; }
    const INamespace& RootNamespace() const override { return rootNamespaceRef_; }

private:
    std::string assemblyName_;
    const INamespace& rootNamespaceRef_;
};

// A concrete `IModuleReference` holding a nullable `const IModule*` it returns from
// `Resolve` (ignoring the context) -- the D418 test-stub pattern. `nullptr` models the
// C# `IModule?` "module not found" case.
class TestModuleRef : public IModuleReference {
public:
    explicit TestModuleRef(const IModule* module) : module_(module) {}
    const IModule* Resolve(const ILSpy::Decompiler::TypeSystem::ITypeResolveContext&) const override
    {
        return module_;
    }

private:
    const IModule* module_;
};

// A concrete `IModuleReference` whose `Resolve` throws `std::runtime_error` (for the
// `Init` exception-wrapping test -- the C# `catch (InvalidOperationException)` path).
class TestThrowingModuleRef : public IModuleReference {
public:
    const IModule* Resolve(const ILSpy::Decompiler::TypeSystem::ITypeResolveContext&) const override
    {
        throw std::runtime_error("invalid reference");
    }
};

// A compact `INamespace` for the modules' `RootNamespace`: derives from the shared
// `TestSupport::TestNamespace` and overrides `Name` / `FullName` / `ChildNamespaces`
// with configurable values (for the merged-root test -- the root namespace's children
// are flattened across the main and referenced modules).
class TestNamespace2 : public TestSupportNamespace {
public:
    TestNamespace2(std::string name, std::vector<const INamespace*> children,
                   const ICompilation& compilation)
        : TestSupportNamespace(compilation),
          name_(std::move(name)),
          children_(std::move(children)) {}

    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::vector<const INamespace*> ChildNamespaces() const override { return children_; }

private:
    std::string name_;
    std::vector<const INamespace*> children_;
};

} // namespace

// ---------------------------------------------------------------------------
// `Init` resolves the main module and the referenced modules, dedups them by reference
// equality, and populates `Modules` (the main first, then the unique referenced) /
// `ReferencedModules` (the unique referenced, excluding the main unless a ref resolves
// to it). `MainModule` returns the resolved main module.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, CtorResolvesMainAndReferences)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModule ref1Mod("System", rootNs, comp);
    TestModule ref2Mod("System.Core", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    TestModuleRef ref1(&ref1Mod);
    TestModuleRef ref2(&ref2Mod);
    comp.Init(mainRef, {&ref1, &ref2});

    EXPECT_EQ(comp.MainModule().AssemblyName(), "mscorlib");
    const auto modules = comp.Modules();
    ASSERT_EQ(modules.size(), 3u);
    EXPECT_EQ(modules[0], &mainMod);
    EXPECT_EQ(modules[1], &ref1Mod);
    EXPECT_EQ(modules[2], &ref2Mod);
    const auto referenced = comp.ReferencedModules();
    ASSERT_EQ(referenced.size(), 2u);
    EXPECT_EQ(referenced[0], &ref1Mod);
    EXPECT_EQ(referenced[1], &ref2Mod);
}

// ---------------------------------------------------------------------------
// `Init` dedups duplicate references: a reference resolving to an already-included
// module is not added again (reference equality, the C# `List.Contains`).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, CtorDedupsDuplicateReferences)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModule refMod("System", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    TestModuleRef ref(&refMod);
    comp.Init(mainRef, {&ref, &ref});

    const auto modules = comp.Modules();
    ASSERT_EQ(modules.size(), 2u);
    EXPECT_EQ(modules[0], &mainMod);
    EXPECT_EQ(modules[1], &refMod);
    const auto referenced = comp.ReferencedModules();
    ASSERT_EQ(referenced.size(), 1u);
    EXPECT_EQ(referenced[0], &refMod);
}

// ---------------------------------------------------------------------------
// Faithful C# crux: a reference resolving to the MAIN module is NOT added to `Modules`
// (it is already there as the main) but IS added to `ReferencedModules` (the C#
// `referencedAssemblies` dedups against itself only, not against `assemblies`, so the
// main is added when a ref resolves to it). This pins the faithful (slightly
// surprising) C# behavior -- a "fixed" port that excluded the main from
// `ReferencedModules` would fail this test.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, ReferenceResolvingToMainIsAddedToReferencedModules)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    // A reference that resolves to the main module itself.
    TestModuleRef refToMain(&mainMod);
    comp.Init(mainRef, {&refToMain});

    const auto modules = comp.Modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0], &mainMod);
    // The faithful C# behavior: the main IS in ReferencedModules when a ref resolves
    // to it (the `referencedAssemblies` list dedups against itself only).
    const auto referenced = comp.ReferencedModules();
    ASSERT_EQ(referenced.size(), 1u);
    EXPECT_EQ(referenced[0], &mainMod);
}

// ---------------------------------------------------------------------------
// `MainModule` returns the resolved main module.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, MainModuleReturnsResolvedMain)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(&comp.MainModule(), &mainMod);
}

// ---------------------------------------------------------------------------
// `FindType` forwards to the `KnownTypeCache`: for a known type not present in any
// module (the `TestModule`'s `GetTypeDefinition` returns null), the cache returns the
// `UnknownType` fallback carrying the type's name. `Object` resolves to an
// `UnknownType` with `Name == "Object"` and `Kind == Unknown`.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, FindTypeReturnsUnknownTypeFallbackForMissingType)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    const IType& t = comp.FindType(KnownTypeCode::Object);
    EXPECT_EQ(t.Kind(), TypeKind::Unknown);
    EXPECT_EQ(t.Name(), "Object");
}

// ---------------------------------------------------------------------------
// `FindType(None)` returns the `SpecialType.UnknownType` null object (no
// `KnownTypeReference` for `None`): `Kind == Unknown`, `Name == "?"` (the
// `SpecialType(TypeKind::Unknown)` name).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, FindTypeReturnsSpecialTypeForNone)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    const IType& t = comp.FindType(KnownTypeCode::None);
    EXPECT_EQ(t.Kind(), TypeKind::Unknown);
    EXPECT_EQ(t.Name(), "?");
}

// ---------------------------------------------------------------------------
// `RootNamespace` is a `MergedNamespace` over the root namespaces of the main module
// (first) and the referenced modules: the merged root's `ChildNamespaces` flattens the
// children of all underlying root namespaces. The main module's root has a "System"
// child; the referenced module's root has a "Collections" child; the merged root has
// both.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, RootNamespaceMergesMainAndReferences)
{
    TestSimpleCompilation comp;
    TestNamespace2 mainChild("System", {}, comp);
    TestNamespace2 mainRoot("", {&mainChild}, comp);
    TestNamespace2 refChild("Collections", {}, comp);
    TestNamespace2 refRoot("", {&refChild}, comp);
    TestModule mainMod("mscorlib", mainRoot, comp);
    TestModule refMod("System", refRoot, comp);
    TestModuleRef mainRef(&mainMod);
    TestModuleRef refRef(&refMod);
    comp.Init(mainRef, {&refRef});

    const INamespace& root = comp.RootNamespace();
    const auto children = root.ChildNamespaces();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_NE(root.GetChildNamespace("System"), nullptr);
    EXPECT_NE(root.GetChildNamespace("Collections"), nullptr);
}

// ---------------------------------------------------------------------------
// `RootNamespace` is lazily built and cached (first-writer-wins via `LazyInit`): a
// second call returns the SAME `INamespace*` (pointer-identity), proving the
// `LazyInit` cache holds the first result rather than rebuilding it.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, RootNamespaceIsCached)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    const INamespace* first = &comp.RootNamespace();
    const INamespace* second = &comp.RootNamespace();
    EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// `GetNamespaceForExternAlias("")` returns the global root namespace (the C#
// `string.IsNullOrEmpty(alias)` -> root contract).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, GetNamespaceForExternAliasReturnsRootForEmptyAlias)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(comp.GetNamespaceForExternAlias(""), &comp.RootNamespace());
}

// ---------------------------------------------------------------------------
// `GetNamespaceForExternAlias` returns null for an unknown alias (`SimpleCompilation`
// does not support extern aliases).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, GetNamespaceForExternAliasReturnsNullForUnknownAlias)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(comp.GetNamespaceForExternAlias("Alias"), nullptr);
}

// ---------------------------------------------------------------------------
// `NameComparer` returns `StringComparer::Ordinal` (the singleton identity).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, NameComparerIsOrdinal)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(&comp.NameComparer(), &StringComparer::Ordinal());
}

// ---------------------------------------------------------------------------
// `TypeSystemOptions` returns `TypeSystemOptions::Default` (the D376 composite).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, TypeSystemOptionsIsDefault)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(comp.TypeSystemOptions(), TypeSystemOptions::Default);
}

// ---------------------------------------------------------------------------
// `CacheManager` returns a stable per-compilation instance (the `cacheManager_` member,
// not a temporary): two calls return the same reference.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, CacheManagerReturnsStableInstance)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(&comp.CacheManager(), &comp.CacheManager());
}

// ---------------------------------------------------------------------------
// `Init` wraps a `std::runtime_error` thrown by a module reference's `Resolve` (the C#
// `catch (InvalidOperationException)` -> rethrow with a helpful message): the rethrown
// message contains the "Tried to initialize compilation" text.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, InitThrowsWrappedOnResolveException)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    // The throwing ref is a REFERENCED module (the C# try/catch wraps the referenced
    // module resolves, NOT the main `mainAssembly.Resolve` which is outside the
    // try/catch -- faithful to the C# source).
    TestThrowingModuleRef badRef;
    try {
        comp.Init(mainRef, {&badRef});
        FAIL() << "Expected Init to throw std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("Tried to initialize compilation"),
                  std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// The `!initialized` gate: a `SimpleCompilation` constructed via the `protected`
// default ctor (no `Init` call) throws on `MainModule` / `Modules` /
// `ReferencedModules` (the C# `InvalidOperationException`). The gate is load-bearing
// for the subclass path (the public ctors call `Init` in the body, so a
// publicly-constructed compilation is always initialized).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, UninitializedCompilationThrowsOnModuleAccessors)
{
    TestSimpleCompilation comp; // default ctor, no Init -> uninitialized
    EXPECT_THROW(comp.MainModule(), std::runtime_error);
    EXPECT_THROW(comp.Modules(), std::runtime_error);
    EXPECT_THROW(comp.ReferencedModules(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// The public ctor (the inherited `SimpleCompilation(const IModuleReference&,
// std::vector<const IModuleReference*>)`) initializes the compilation: it calls `Init`
// in its body. The test modules bind to a placeholder `ICompilation` (a
// not-yet-initialized `TestSimpleCompilation`); `SimpleCompilation`'s logic never reads
// `module->Compilation()`, so the placeholder is safe.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, PublicCtorInitializesCompilation)
{
    TestSimpleCompilation placeholder; // exists as an ICompilation for the modules
    TestNamespace2 rootNs("", {}, placeholder);
    TestModule mainMod("mscorlib", rootNs, placeholder);
    TestModuleRef mainRef(&mainMod);
    // The inherited public ctor calls Init in its body.
    TestSimpleCompilation comp(mainRef, {});

    EXPECT_EQ(comp.MainModule().AssemblyName(), "mscorlib");
    EXPECT_EQ(comp.Modules().size(), 1u);
}

// ---------------------------------------------------------------------------
// `ToString` mirrors the C# `"[" + GetType().Name + " " + mainModule.AssemblyName + "]"`
// format (`GetType().Name` for `SimpleCompilation` is the literal
// `"SimpleCompilation"`).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, ToStringMirrorsCSharpFormat)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    EXPECT_EQ(comp.ToString(), "[SimpleCompilation mscorlib]");
}

// ---------------------------------------------------------------------------
// Polymorphic dispatch through the `ICompilation*` base pointer (a `SimpleCompilation`
// IS-A `ICompilation`, so the base pointer dispatches to the concrete overrides).
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, DispatchesPolymorphicallyThroughICompilationPointer)
{
    TestSimpleCompilation comp;
    TestNamespace2 rootNs("", {}, comp);
    TestModule mainMod("mscorlib", rootNs, comp);
    TestModuleRef mainRef(&mainMod);
    comp.Init(mainRef, {});

    ICompilation* asCompilation = &comp;
    EXPECT_EQ(asCompilation->MainModule().AssemblyName(), "mscorlib");
    EXPECT_EQ(&asCompilation->NameComparer(), &StringComparer::Ordinal());
    EXPECT_EQ(asCompilation->TypeSystemOptions(), TypeSystemOptions::Default);
}

// ---------------------------------------------------------------------------
// Has a virtual destructor (a `SimpleCompilation` subclass can be deleted through an
// `ICompilation*` and the derived destructor runs), is polymorphic, and is an
// `ICompilation`.
// ---------------------------------------------------------------------------
TEST(SimpleCompilationTest, HasVirtualDestructorAndIsPolymorphic)
{
    static_assert(std::has_virtual_destructor_v<SimpleCompilation>,
        "SimpleCompilation must have a virtual destructor for abstract-base deletion");
    static_assert(std::is_polymorphic_v<SimpleCompilation>,
        "SimpleCompilation must be polymorphic");
    static_assert(std::is_base_of_v<ICompilation, SimpleCompilation>,
        "SimpleCompilation must derive from ICompilation");

    // The modules must outlive the compilation (the non-owning model: the compilation
    // holds `const IModule*` pointers, not owning `shared_ptr`s). The test modules bind
    // to a placeholder `ICompilation` (a not-yet-initialized `TestSimpleCompilation`);
    // `SimpleCompilation`'s logic never reads `module->Compilation()`, so the
    // placeholder is safe. The real compilation is heap-allocated and owned by a
    // `unique_ptr<ICompilation>` so `reset()` deletes it through the `ICompilation*`
    // base pointer (the virtual destructor runs).
    TestSimpleCompilation placeholder;
    TestNamespace2 rootNs("", {}, placeholder);
    TestModule mainMod("mscorlib", rootNs, placeholder);
    TestModuleRef mainRef(&mainMod);
    std::unique_ptr<ICompilation> owned =
        std::make_unique<TestSimpleCompilation>(mainRef, std::vector<const IModuleReference*>{});
    EXPECT_EQ(owned->MainModule().AssemblyName(), "mscorlib");
    owned.reset(); // delete TestSimpleCompilation through ICompilation* -> virtual dtor
    SUCCEED();
}
