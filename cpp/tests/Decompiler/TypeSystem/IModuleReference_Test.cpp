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

// Tests for `IModuleReference` (cpp/Decompiler/TypeSystem/IModuleReference.hpp, the port
// of ICSharpCode.Decompiler/TypeSystem/IAssembly.cs lines 41-48). `IModuleReference` is
// the 1-method interface that a module reference implements to resolve itself to an
// `IModule` against an `ITypeResolveContext` -- the module-reference counterpart of
// `ITypeReference` (D408), but returning a NULLABLE `IModule?` (unlike the non-null
// `IType` the `ITypeReference.Resolve` contract guarantees). It is the base of
// `DefaultAssemblyReference` (the assembly-by-name reference, not yet ported) and a
// leaf TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker of `CSharpAmbience`).
//
// The test's `TestModuleReference` IGNORES the context (`Resolve` returns its configured
// `IModule*`, never reading the context), so the concrete `TestResolveContext` below
// (a trivial override of all six `ITypeResolveContext` pure-virtuals backed by a compact
// `TestCompilation`) is sufficient for the `const ITypeResolveContext&` parameter to bind
// to a complete concrete type -- the D408 `ITypeReference` test-stub pattern applied to
// the module-reference interface. The production `DefaultAssemblyReference::Resolve`
// (not yet ported) WILL read the context (`context.CurrentModule` /
// `context.Compilation.Modules`) against the real `ITypeResolveContext` surface -- the
// deferred-consumer shape the stand-in-reconciliation handles.

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A compact `ICompilation` backing the `TestResolveContext` (the `TestCompilation`
// reconciliation pattern, D399): overrides the nine `ICompilation` pure-virtuals with
// trivial returns backed by a `TestSupport::TestModule` (the shared stub from
// `TestCompilationStubs.hpp`). Default-constructible so `TestResolveContext` can hold
// one by value with no wiring. `mainModule_(*this)` binds the `TestModule` to the
// already-constructed `ICompilation` base (bases are constructed before members). The
// `TestModule` is the `IModule` the `TestModuleReference` resolves to -- accessed via
// `compilation.MainModule()`.
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

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
        return mainModule_.RootNamespace();
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetNamespaceForExternAlias(
        const std::string&) const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IType& FindType(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode) const override
    {
        return knownType_;
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
        return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
    }

private:
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A trivial concrete `ITypeResolveContext` for the `TestModuleReference::Resolve`
// parameter (which IGNORES the context): overrides all six pure-virtuals (the inherited
// `Compilation` plus the three nullable accessors and the two `With*` factories) with
// trivial returns backed by the held `TestCompilation`. The `With*` factories return
// null `unique_ptr`s (this test never calls them); the nullable accessors return null.
// Default-constructible so the tests can write `TestResolveContext standIn;`.
class TestResolveContext : public ILSpy::Decompiler::TypeSystem::ITypeResolveContext {
public:
    TestResolveContext() = default;

    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }
    const ILSpy::Decompiler::TypeSystem::IModule* CurrentModule() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const override
    {
        return nullptr;
    }
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
    WithCurrentTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition*) const override
    {
        return {};
    }
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
    WithCurrentMember(const ILSpy::Decompiler::TypeSystem::IMember*) const override
    {
        return {};
    }

    // Expose the held compilation so tests can reach the `TestModule` to pass to the
    // `TestModuleReference` ctor (the `IModule` the reference resolves to).
    const TestCompilation& CompilationAsTestCompilation() const { return compilation_; }

private:
    TestCompilation compilation_;
};


// A concrete `IModuleReference` for testing: holds a nullable `const IModule*` and
// returns it from `Resolve`, ignoring the context. This is the shape a real
// `DefaultAssemblyReference` will take (it resolves by matching the short name against
// `context.CurrentModule` / `context.Compilation.Modules`, but the test stub
// short-circuits to a pre-configured `IModule*` so it needs no `ICompilation` wiring).
// A null `const IModule*` ctor arg models the "module not found" case (the C# nullable
// `IModule? Resolve` return).
class TestModuleReference : public ILSpy::Decompiler::TypeSystem::IModuleReference {
public:
    explicit TestModuleReference(const ILSpy::Decompiler::TypeSystem::IModule* module)
        : module_(module) {}

    const ILSpy::Decompiler::TypeSystem::IModule* Resolve(
        const ILSpy::Decompiler::TypeSystem::ITypeResolveContext& /*context*/) const override
    {
        return module_;
    }

private:
    const ILSpy::Decompiler::TypeSystem::IModule* module_;
};

} // namespace

// ---------------------------------------------------------------------------
// IModuleReference -- `Resolve` returns the configured `IModule` (the crux: the
// reference's `Resolve` dispatches to the override and returns the `IModule` the
// reference was constructed to resolve to). Pinned by the `SymbolKind` the
// `TestSupport::TestModule` reports (`SymbolKind::Module`).
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, ResolveReturnsConfiguredModule)
{
    TestResolveContext context;
    const auto* expectedModule = &context.CompilationAsTestCompilation().MainModule();
    TestModuleReference ref(expectedModule);
    EXPECT_EQ(ref.Resolve(context), expectedModule);
    EXPECT_EQ(ref.Resolve(context)->SymbolKind(),
              ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
}

// ---------------------------------------------------------------------------
// IModuleReference -- `Resolve` returns null when the module is not configured (the C#
// `IModule?` nullable return, distinct from the non-null `IType` the
// `ITypeReference.Resolve` contract guarantees -- a module reference may fail to
// resolve and return null). A `TestModuleReference` constructed with `nullptr` models
// the "module not found" case.
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, ResolveReturnsNullWhenNotConfigured)
{
    TestModuleReference ref(nullptr);
    TestResolveContext context;
    EXPECT_EQ(ref.Resolve(context), nullptr);
}

// ---------------------------------------------------------------------------
// IModuleReference -- `Resolve` dispatches polymorphically through an
// `IModuleReference*` (the dynamic dispatch the compilation-construction paths rely on:
// the compilation constructor holds module references as `IModuleReference` and reaches
// the resolved `IModule` through the base pointer, without knowing the concrete
// `DefaultAssemblyReference` kind).
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, ResolveDispatchesPolymorphicallyThroughBasePointer)
{
    TestResolveContext context;
    const auto* expectedModule = &context.CompilationAsTestCompilation().MainModule();
    TestModuleReference ref(expectedModule);
    const ILSpy::Decompiler::TypeSystem::IModuleReference* base = &ref;
    EXPECT_EQ(base->Resolve(context), expectedModule);
    EXPECT_EQ(base->Resolve(context)->SymbolKind(),
              ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
}

// ---------------------------------------------------------------------------
// IModuleReference -- `Resolve` is `const`-callable (the override reads the reference
// without modifying it; the C# instance method ports to a `const` member). A
// `const IModuleReference&` dispatches to the `const Resolve` overload.
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, ResolveIsCallableOnConstReference)
{
    TestResolveContext context;
    const auto* expectedModule = &context.CompilationAsTestCompilation().MainModule();
    const TestModuleReference ref(expectedModule);
    const ILSpy::Decompiler::TypeSystem::IModuleReference& base = ref;
    EXPECT_EQ(base.Resolve(context), expectedModule);
}

// ---------------------------------------------------------------------------
// IModuleReference -- two references constructed with distinct `IModule`s resolve to
// distinct modules (the references are independent; each carries its own resolved
// module). Pinned by pointer-identity of each reference's resolved `IModule`.
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, TwoDistinctReferencesReturnDistinctModules)
{
    TestResolveContext contextA;
    TestResolveContext contextB;
    const auto* moduleA = &contextA.CompilationAsTestCompilation().MainModule();
    const auto* moduleB = &contextB.CompilationAsTestCompilation().MainModule();
    TestModuleReference refA(moduleA);
    TestModuleReference refB(moduleB);
    EXPECT_NE(refA.Resolve(contextA), refB.Resolve(contextB));
}

// ---------------------------------------------------------------------------
// IModuleReference -- the interface is abstract (cannot be instantiated) and has a
// virtual destructor; a concrete subclass overriding the one pure-virtual is
// instantiable. The `ITypeResolveContext` parameter type is forward-declared in the
// header, so the interface compiles with the context incomplete.
// ---------------------------------------------------------------------------
TEST(IModuleReferenceTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IModuleReference>,
                  "IModuleReference must have a virtual destructor for abstract-base deletion.");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IModuleReference>,
                  "IModuleReference must be abstract (one pure-virtual Resolve accessor).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IModuleReference>,
                  "IModuleReference must be polymorphic.");
    // A concrete subclass overriding the one pure-virtual is instantiable.
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::IModuleReference> p =
        std::make_unique<TestModuleReference>(nullptr);
    EXPECT_NE(p, nullptr);
    p.reset();
    SUCCEED();
}
