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

// Tests for `ICompilationProvider` (cpp/Decompiler/TypeSystem/ICompilationProvider.hpp,
// the D379 port of ICSharpCode.Decompiler/TypeSystem/ICompilation.cs lines 83-91).
// `ICompilationProvider` is the 1-accessor base of `IEntity` / `IModule` / `INamespace`:
// it exposes the parent `ICompilation`. The tests pin the interface contract (a concrete
// subclass overriding the accessor, polymorphic dispatch through an
// `ICompilationProvider*`, and the virtual destructor) via a test stub.
//
// `ICompilation` is only forward-declared in `ICompilationProvider.hpp` (it is not yet
// ported), so this test file provides a minimal complete stand-in `ICompilation` in the
// `ILSpy::Decompiler::TypeSystem` namespace (a virtual destructor only) so the
// `TestCompilationProvider` stub can hold and return a concrete `TestCompilation`. This
// stand-in is a TEST FIXTURE, NOT a faithful port of the full `ICompilation` surface
// (which pulls `IModule` / `INamespace` / `IType` / `KnownTypeCode` / `CacheManager` /
// `TypeSystemOptions` / `StringComparer`); the real `ICompilation` interface lands as a
// separate later leaf, at which point this stand-in is dropped in favour of the real
// header. No other translation unit in the test build defines `ICompilation`, so this
// stand-in is the sole definition (ODR-safe for the test executable).

#include "Decompiler/TypeSystem/ICompilationProvider.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// Minimal test stand-in for `ICompilation` (the parent compilation interface). Only a
// virtual destructor is declared here -- this is the complete type the
// `ICompilationProvider::Compilation()` reference return needs to bind to, NOT a faithful
// port of the full `ICompilation` surface. Replaced by the real `ICompilation.hpp` when
// that lands.
class ICompilation {
public:
    virtual ~ICompilation() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A minimal concrete `ICompilation` for testing: a marker object the
// `TestCompilationProvider` can hold and return (identity-testable via pointer compare).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}

    int id() const { return id_; }

private:
    int id_;
};

// A minimal concrete `ICompilationProvider` for testing: holds a reference to a
// `TestCompilation` and returns it from `Compilation()` (the shape a real entity/module/
// namespace -- a `MetadataTypeDefinition`, `MetadataModule`, `MetadataNamespace`, ... --
// will take: it stores its compilation and returns it). The accessor name `Compilation`
// does not collide with a namespace-scope type in the `TypeSystem` namespace, so no
// name-hiding qualification is needed (the D375 `INamedElement` collision-free-accessor
// convention).
class TestCompilationProvider : public ILSpy::Decompiler::TypeSystem::ICompilationProvider {
public:
    explicit TestCompilationProvider(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation) {}

    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
};

} // namespace

// ---------------------------------------------------------------------------
// ICompilationProvider -- the accessor returns the configured compilation (the shape a
// real entity/module/namespace exposes: the compilation it belongs to). Identity is
// pinned by pointer compare (the C# interface has no equality contract; the reference is
// the same object the provider was constructed with).
// ---------------------------------------------------------------------------
TEST(ICompilationProviderTest, AccessorReturnsConfiguredCompilation)
{
    TestCompilation compilation(42);
    TestCompilationProvider provider(compilation);
    EXPECT_EQ(&provider.Compilation(), &compilation);
    EXPECT_EQ(static_cast<const TestCompilation&>(provider.Compilation()).id(), 42);
}

// ---------------------------------------------------------------------------
// ICompilationProvider -- polymorphic dispatch through an `ICompilationProvider*` (the
// dynamic dispatch the type-system paths rely on: `TypeSystemAstBuilder` and the resolver
// hold an entity as an `IEntity` (which IS-A `ICompilationProvider`) and read
// `Compilation()` through the base to reach the compilation's modules/types).
// ---------------------------------------------------------------------------
TEST(ICompilationProviderTest, DispatchesPolymorphicallyThroughBasePointer)
{
    TestCompilation compilation(7);
    auto owned = std::make_unique<TestCompilationProvider>(compilation);
    ILSpy::Decompiler::TypeSystem::ICompilationProvider* base = owned.get();
    EXPECT_EQ(&base->Compilation(), &compilation);
    EXPECT_EQ(static_cast<const TestCompilation&>(base->Compilation()).id(), 7);
}

// ---------------------------------------------------------------------------
// ICompilationProvider -- the returned reference is never null (the C# doc comment
// "This property never returns null", ported as a non-null reference return). The
// concrete provider holds a reference bound at construction, so `Compilation()` always
// yields a valid object.
// ---------------------------------------------------------------------------
TEST(ICompilationProviderTest, ReturnedReferenceIsNeverNull)
{
    TestCompilation compilation(0);
    TestCompilationProvider provider(compilation);
    const auto& returned = provider.Compilation();
    EXPECT_NE(&returned, nullptr);
    // The reference is usable as an `ICompilation` (the stand-in base): the virtual
    // destructor is part of the interface, exercised by the HasVirtualDestructor test.
    (void)returned;
}

// ---------------------------------------------------------------------------
// ICompilationProvider -- has a virtual destructor (a concrete subclass can be deleted
// through an `ICompilationProvider*` and the derived destructor runs), the established
// abstract-base contract.
// ---------------------------------------------------------------------------
TEST(ICompilationProviderTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ICompilationProvider>,
        "ICompilationProvider must have a virtual destructor for abstract-base deletion");
    TestCompilation compilation(1);
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ICompilationProvider> owned =
        std::make_unique<TestCompilationProvider>(compilation);
    EXPECT_EQ(&owned->Compilation(), &compilation);
    // destroying `owned` runs the `TestCompilationProvider` destructor through the
    // virtual `~ICompilationProvider()`.
    owned.reset();
    SUCCEED();
}
