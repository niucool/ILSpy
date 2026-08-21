// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for `ITypeResolveContext` (cpp/Decompiler/TypeSystem/ITypeResolveContext.hpp, the
// port of ICSharpCode.Decompiler/TypeSystem/ITypeReference.cs lines 51-73). The resolution
// context a type reference resolves itself against: it bundles the parent `ICompilation`
// (inherited from `ICompilationProvider` D379) with the current `IModule` /
// `ITypeDefinition` / `IMember` slots and the two `With*` factories that return a NEW
// context with one slot replaced. It is the natural next increment after the D408
// `ITypeReference` port (which forward-declared `ITypeResolveContext` and provided a
// dtor-only test stand-in that this real header now replaces -- the established
// stand-in-reconciliation step, applied to `ITypeReference_Test.cpp` in this same
// iteration). It is a leaf TypeSystem dependency toward `KnownTypeReference` (the first
// concrete `ITypeReference`, which resolves via `context.Compilation.FindType`),
// `SimpleTypeResolveContext` (the default impl), and `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// The `TestResolveContext` (the SUT stub) is a MEANINGFUL concrete `ITypeResolveContext`
// holding the four slots (`compilation` + three nullable pointers) and implementing the
// `With*` factories as `make_unique<TestResolveContext>(compilation_, <carried>, <replaced>,
// <carried>)` -- the faithful shape of the C# `SimpleTypeResolveContext` private ctor + the
// two `With*` methods. The three nullable slots are backed by compact concrete stubs:
// `TestSupport::TestModule` (the shared stub from `TestCompilationStubs.hpp`, for
// `CurrentModule`), a minimal `TestTypeDefinition` (for `CurrentTypeDefinition`), and a
// minimal `TestMember` (for `CurrentMember`); the tests use POINTER-IDENTITY (configure the
// context with a slot, verify the same pointer comes back) so the stubs only need to be
// CONCRETE (instantiable), not behaviourful -- each override is a trivial one-liner.

#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A compact `ICompilation` backing the `TestResolveContext` (the `TestCompilation`
// reconciliation pattern, D399): overrides the nine `ICompilation` pure-virtuals with
// trivial returns backed by a `TestSupport::TestModule` (the shared stub from
// `TestCompilationStubs.hpp`). `mainModule_(*this)` binds the `TestModule` to the
// already-constructed `ICompilation` base. The tests read `Compilation()` for
// pointer-identity, so the module / namespace members are live (not function-local
// statics).
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

// A minimal concrete `ITypeDefinition` for `CurrentTypeDefinition` pointer-identity. Each
// override is a trivial one-liner; the tests only use the instance's ADDRESS (never call
// its accessors), so the stub needs only to be INSTANTIABLE. `Compilation()` returns the
// held compilation reference (constructed with one); `FullTypeName()` returns a stable
// reference to the default-constructed `FullTypeName` member (the `FullTypeName() = default`
// ctor). The protected `StructuralEquals` override matches the `IType` protected virtual.
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    explicit TestTypeDefinition(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation) {}

    // --- IType (inherited unambiguously; Name/ReflectionName redeclared in ITypeDefinition) ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeKind::Class;
    }
    std::string Name() const override { return {}; }
    std::string ReflectionName() const override { return {}; }
    int TypeParameterCount() const override { return 0; }

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
    std::string FullName() const override { return {}; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return nullptr;
    }
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
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> NestedTypes() const override
    {
        return {};
    }
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
    std::string MetadataName() const override { return {}; }
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
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType&) const override
    {
        return false;
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
};

// A minimal concrete `IMember` for `CurrentMember` pointer-identity. Each override is a
// trivial one-liner; the tests only use the instance's ADDRESS. `ReturnType()` returns a
// reference to the held `KnownType` member (the non-null `IVariable::Type()` convention);
// the three long-pole-dep members (`Substitution` / `Specialize` / `Equals`) use the
// degenerate `nullptr` / `this` / identity stand-ins (the D387 IMember stub precedent).
class TestMember : public ILSpy::Decompiler::TypeSystem::IMember {
public:
    explicit TestMember(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation),
          returnType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return {}; }

    // --- INamedElement ---
    std::string FullName() const override { return {}; }
    std::string ReflectionName() const override { return {}; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return nullptr;
    }
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

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override
    {
        return this;
    }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override
    {
        return returnType_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::KnownType returnType_;
};

// A MEANINGFUL concrete `ITypeResolveContext` (the SUT stub): holds the four slots
// (`compilation` + three nullable pointers) and implements the `With*` factories as
// `make_unique<TestResolveContext>(compilation_, <carried>, <replaced>, <carried>)` -- the
// faithful shape of the C# `SimpleTypeResolveContext` private ctor + the two `With*`
// methods (one slot replaced, the other two carried over, the compilation always carried).
// The inherited `Compilation()` returns the held compilation reference (non-null, the
// `ICompilationProvider` contract).
class TestResolveContext : public ILSpy::Decompiler::TypeSystem::ITypeResolveContext {
public:
    TestResolveContext(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                       const ILSpy::Decompiler::TypeSystem::IModule* currentModule,
                       const ILSpy::Decompiler::TypeSystem::ITypeDefinition* currentTypeDefinition,
                       const ILSpy::Decompiler::TypeSystem::IMember* currentMember)
        : compilation_(compilation),
          currentModule_(currentModule),
          currentTypeDefinition_(currentTypeDefinition),
          currentMember_(currentMember) {}

    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }
    const ILSpy::Decompiler::TypeSystem::IModule* CurrentModule() const override
    {
        return currentModule_;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const override
    {
        return currentTypeDefinition_;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const override
    {
        return currentMember_;
    }

    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
    WithCurrentTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const override
    {
        return std::make_unique<TestResolveContext>(
            compilation_, currentModule_, typeDefinition, currentMember_);
    }

    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
    WithCurrentMember(const ILSpy::Decompiler::TypeSystem::IMember* member) const override
    {
        return std::make_unique<TestResolveContext>(
            compilation_, currentModule_, currentTypeDefinition_, member);
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    const ILSpy::Decompiler::TypeSystem::IModule* currentModule_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* currentTypeDefinition_;
    const ILSpy::Decompiler::TypeSystem::IMember* currentMember_;
};

} // namespace

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the three nullable accessors return null by default (a context
// constructed with no current module / type definition / member). The C# `null` for each
// nullable slot ports to a null pointer.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, NullableAccessorsReturnNullByDefault)
{
    TestCompilation compilation;
    TestResolveContext ctx(compilation, nullptr, nullptr, nullptr);
    EXPECT_EQ(ctx.CurrentModule(), nullptr);
    EXPECT_EQ(ctx.CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(ctx.CurrentMember(), nullptr);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the three nullable accessors return the CONFIGURED pointers
// (pointer-identity: configure the context with a slot, verify the same pointer comes
// back). The crux: the accessors dispatch to the override and return the held slot.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, CurrentModuleReturnsConfiguredModule)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);
    EXPECT_EQ(ctx.CurrentModule(), &module);
}

TEST(ITypeResolveContextTest, CurrentTypeDefinitionReturnsConfiguredTypeDefinition)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);
    EXPECT_EQ(ctx.CurrentTypeDefinition(), &td);
}

TEST(ITypeResolveContextTest, CurrentMemberReturnsConfiguredMember)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);
    EXPECT_EQ(ctx.CurrentMember(), &member);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- `WithCurrentTypeDefinition` returns a NEW context with the
// current-type-definition slot REPLACED and the other slots (module / member /
// compilation) CARRIED OVER. The crux: the factory builds a new context from the current
// slots with exactly one replaced. Pinned by pointer-identity on all three slots.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithCurrentTypeDefinitionReplacesSlotAndPreservesOthers)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition tdA(compilation);
    TestTypeDefinition tdB(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &tdA, &member);

    auto next = ctx.WithCurrentTypeDefinition(&tdB);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), &tdB);   // replaced
    EXPECT_EQ(next->CurrentModule(), &module);         // carried over
    EXPECT_EQ(next->CurrentMember(), &member);         // carried over
    // The new context is a DISTINCT instance (a new heap allocation, not `this`).
    EXPECT_NE(next.get(), &ctx);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- `WithCurrentTypeDefinition(nullptr)` clears the slot (the C#
// nullable parameter: pass null to clear) while carrying the others over.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithCurrentTypeDefinitionWithNullClearsSlot)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);

    auto next = ctx.WithCurrentTypeDefinition(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), nullptr); // cleared
    EXPECT_EQ(next->CurrentModule(), &module);          // carried over
    EXPECT_EQ(next->CurrentMember(), &member);          // carried over
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- `WithCurrentMember` returns a NEW context with the current-member
// slot REPLACED and the others carried over. The twin of the type-definition factory.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithCurrentMemberReplacesSlotAndPreservesOthers)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember memberA(compilation);
    TestMember memberB(compilation);
    TestResolveContext ctx(compilation, &module, &td, &memberA);

    auto next = ctx.WithCurrentMember(&memberB);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentMember(), &memberB);   // replaced
    EXPECT_EQ(next->CurrentModule(), &module);     // carried over
    EXPECT_EQ(next->CurrentTypeDefinition(), &td);  // carried over
    EXPECT_NE(next.get(), &ctx);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- `WithCurrentMember(nullptr)` clears the slot while carrying the
// others over.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithCurrentMemberWithNullClearsSlot)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);

    auto next = ctx.WithCurrentMember(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentMember(), nullptr);  // cleared
    EXPECT_EQ(next->CurrentModule(), &module);    // carried over
    EXPECT_EQ(next->CurrentTypeDefinition(), &td); // carried over
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the `With*` factories return a `std::unique_ptr` (the faithful C++
// counterpart of the C# `new SimpleTypeResolveContext(...)` heap allocation transferred to
// the caller). The caller owns the new context (unique ownership, verified by the
// `EXPECT_NE(next.get(), &ctx)` distinct-instance checks above).
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithFactoriesReturnUniquePtrTransferringOwnership)
{
    TestCompilation compilation;
    TestResolveContext ctx(compilation, nullptr, nullptr, nullptr);
    auto byTd = ctx.WithCurrentTypeDefinition(nullptr);
    auto byMember = ctx.WithCurrentMember(nullptr);
    static_assert(
        std::is_same_v<decltype(byTd), std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>>,
        "WithCurrentTypeDefinition must return std::unique_ptr<ITypeResolveContext>.");
    static_assert(
        std::is_same_v<decltype(byMember), std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>>,
        "WithCurrentMember must return std::unique_ptr<ITypeResolveContext>.");
    ASSERT_NE(byTd, nullptr);
    ASSERT_NE(byMember, nullptr);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- `WithCurrentTypeDefinition` / `WithCurrentMember` are `const`-
// callable (the factory reads `this` without modifying it; the C# instance method ports to
// a `const` member). A `const ITypeResolveContext&` can call them.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, WithFactoriesAreConstCallable)
{
    TestCompilation compilation;
    TestTypeDefinition td(compilation);
    const TestResolveContext ctx(compilation, nullptr, &td, nullptr);
    const ILSpy::Decompiler::TypeSystem::ITypeResolveContext& base = ctx;
    auto next = base.WithCurrentTypeDefinition(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(next->CurrentMember(), nullptr);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the inherited `Compilation()` (from `ICompilationProvider` D379)
// dispatches through an `ITypeResolveContext*` and returns the configured compilation
// (pointer-identity). Single inheritance means no `Name` / `SymbolKind` redeclaration and
// `Compilation()` is inherited unchanged (the D374 precedent).
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, InheritedCompilationDispatchesThroughInterfacePointer)
{
    TestCompilation compilation;
    TestResolveContext ctx(compilation, nullptr, nullptr, nullptr);
    const ILSpy::Decompiler::TypeSystem::ITypeResolveContext* base = &ctx;
    EXPECT_EQ(&base->Compilation(), &compilation);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the three nullable accessors dispatch polymorphically through an
// `ITypeResolveContext*` (the dynamic dispatch the resolution paths rely on: a context is
// held as an `ITypeResolveContext` and the current module / type definition / member reached
// through the base pointer, without knowing the concrete `SimpleTypeResolveContext` kind).
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, NullableAccessorsDispatchPolymorphicallyThroughBasePointer)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    TestResolveContext ctx(compilation, &module, &td, &member);
    const ILSpy::Decompiler::TypeSystem::ITypeResolveContext* base = &ctx;
    EXPECT_EQ(base->CurrentModule(), &module);
    EXPECT_EQ(base->CurrentTypeDefinition(), &td);
    EXPECT_EQ(base->CurrentMember(), &member);
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the interface is abstract (cannot be instantiated) and has a
// virtual destructor; a concrete subclass overriding the six pure-virtuals is
// instantiable. The `IModule` / `ITypeDefinition` / `IMember` parameter / return types are
// forward-declared in the header, so the interface compiles with all three incomplete.
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>,
                  "ITypeResolveContext must have a virtual destructor for abstract-base deletion.");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>,
                  "ITypeResolveContext must be abstract (six pure-virtuals).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>,
                  "ITypeResolveContext must be polymorphic.");
    // A concrete subclass overriding the six pure-virtuals is instantiable.
    TestCompilation compilation;
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext> p =
        std::make_unique<TestResolveContext>(compilation, nullptr, nullptr, nullptr);
    EXPECT_NE(p, nullptr);
    p.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// ITypeResolveContext -- the interface IS-A `ICompilationProvider` (single inheritance);
// an `ITypeResolveContext*` upcasts to an `ICompilationProvider*` unambiguously, and the
// inherited `Compilation()` is reached through the base (the D374 single-inheritance
// "inherited virtual covers the `new`" precedent -- no redeclaration needed).
// ---------------------------------------------------------------------------
TEST(ITypeResolveContextTest, IsACompilationProvider)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::TypeSystem::ICompilationProvider,
                                    ILSpy::Decompiler::TypeSystem::ITypeResolveContext>,
                  "ITypeResolveContext must derive from ICompilationProvider.");
    TestCompilation compilation;
    TestResolveContext ctx(compilation, nullptr, nullptr, nullptr);
    const ILSpy::Decompiler::TypeSystem::ICompilationProvider* asProvider = &ctx;
    EXPECT_EQ(&asProvider->Compilation(), &compilation);
}
