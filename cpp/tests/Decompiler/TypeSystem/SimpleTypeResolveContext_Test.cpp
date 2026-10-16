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

// Tests for `SimpleTypeResolveContext` (cpp/Decompiler/TypeSystem/SimpleTypeResolveContext.hpp,
// the port of ICSharpCode.Decompiler/TypeSystem/SimpleTypeResolveContext.cs) -- the default
// `ITypeResolveContext` implementation (D409). It bundles the parent `ICompilation` (inherited
// from `ICompilationProvider`) with the three nullable current-module / current-type-definition
// / current-member slots and provides the two `With*` factory methods that return a NEW context
// with one slot replaced. It is the concrete `ITypeResolveContext` the resolution paths construct
// (`KnownTypeReference.Resolve` receives one and reaches `context.Compilation.FindType`).
//
// The `TestCompilation` / `TestTypeDefinition` / `TestMember` stubs back the three public ctors
// (the `ICompilation` / `IModule` / `IEntity` ctors). The `TestTypeDefinition` stub IS-A
// `ITypeDefinition` (and IS-A `IEntity`), so the `IEntity` ctor's `dynamic_cast<const
// ITypeDefinition*>` succeeds and `currentTypeDefinition` is the entity itself; the `TestMember`
// stub IS-A `IMember` (and IS-A `IEntity`) but NOT `ITypeDefinition`, so the `dynamic_cast<const
// ITypeDefinition*>` fails and `currentTypeDefinition` falls to `entity.DeclaringTypeDefinition()`,
// while `dynamic_cast<const IMember*>` succeeds and `currentMember` is the entity itself. The
// `ParentModule()` / `DeclaringTypeDefinition()` accessors are configurable (held pointer
// members) so the entity-ctor tests can pin pointer-identity on all four slots.

#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A compact `ICompilation` backing the stubs (the D399 `TestCompilation` reconciliation
// pattern): overrides the nine `ICompilation` pure-virtuals with trivial returns backed by
// a `TestSupport::TestModule` (the shared stub from `TestCompilationStubs.hpp`).
// `mainModule_(*this)` binds the `TestModule` to the already-constructed `ICompilation` base.
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

    // Expose the main module so the entity-ctor tests can pin pointer-identity on
    // `CurrentModule()` (set from `entity.ParentModule()` which the stubs configure).
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule& Module() { return mainModule_; }

private:
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `ITypeDefinition` for the `IEntity` ctor test (the entity IS-A
// `ITypeDefinition` case). `ParentModule()` / `DeclaringTypeDefinition()` are configurable
// (held pointer members) so the entity-ctor tests can pin pointer-identity. Each other
// override is a trivial one-liner; the tests only use the instance's ADDRESS plus the
// two configurable accessors.
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    explicit TestTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
        const ILSpy::Decompiler::TypeSystem::IModule* parentModule = nullptr,
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition = nullptr)
        : compilation_(compilation),
          parentModule_(parentModule),
          declaringTypeDefinition_(declaringTypeDefinition) {}

    // --- IType (Name/ReflectionName redeclared in ITypeDefinition) ---
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
        return declaringTypeDefinition_;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return parentModule_;
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
    const ILSpy::Decompiler::TypeSystem::IModule* parentModule_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition_;
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
};

// A minimal concrete `IMember` for the `IEntity` ctor test (the entity IS-A `IMember` but
// NOT `ITypeDefinition` case). `ParentModule()` / `DeclaringTypeDefinition()` are
// configurable (held pointer members) so the entity-ctor tests can pin pointer-identity.
// The three long-pole-dep members use the degenerate `nullptr` / `this` / identity stand-ins
// (the D387 IMember stub precedent).
class TestMember : public ILSpy::Decompiler::TypeSystem::IMember {
public:
    explicit TestMember(
        const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
        const ILSpy::Decompiler::TypeSystem::IModule* parentModule = nullptr,
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition = nullptr)
        : compilation_(compilation),
          parentModule_(parentModule),
          declaringTypeDefinition_(declaringTypeDefinition),
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
        return declaringTypeDefinition_;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return parentModule_;
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
    const ILSpy::Decompiler::TypeSystem::IModule* parentModule_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition_;
    ILSpy::Decompiler::TypeSystem::KnownType returnType_;
};

} // namespace

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `ICompilation` ctor holds the compilation and leaves the
// three nullable slots null. The C# `ArgumentNullException` on a null compilation is N/A
// in C++ (a reference parameter is non-null by C++ semantics).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, CompilationCtorHoldsCompilationAndLeavesSlotsNull)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::ICompilation&>(compilation));
    EXPECT_EQ(&ctx.Compilation(), &compilation);
    EXPECT_EQ(ctx.CurrentModule(), nullptr);
    EXPECT_EQ(ctx.CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(ctx.CurrentMember(), nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `IModule` ctor holds the module's compilation
// (`module.Compilation()`, the inherited `ICompilationProvider::Compilation`) and the
// module itself as the current module. The other two slots are null.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, ModuleCtorHoldsModuleAndItsCompilation)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    EXPECT_EQ(&ctx.Compilation(), &module.Compilation());
    EXPECT_EQ(&module.Compilation(), &compilation);
    EXPECT_EQ(ctx.CurrentModule(), &module);
    EXPECT_EQ(ctx.CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(ctx.CurrentMember(), nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `IEntity` ctor with an entity that IS-A `ITypeDefinition`
// (a `TestTypeDefinition`): the `dynamic_cast<const ITypeDefinition*>` succeeds so
// `CurrentTypeDefinition` is the entity itself (pointer-identity), and the
// `dynamic_cast<const IMember*>` fails (a type definition is NOT a member) so
// `CurrentMember` is null. The compilation is `entity.Compilation()` and the current
// module is `entity.ParentModule()` (pointer-identity on the configured parent module).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, EntityCtorWithTypeDefinitionEntityUsesEntityAsTypeDefinition)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation, &module, nullptr);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IEntity&>(td));
    EXPECT_EQ(&ctx.Compilation(), &td.Compilation());
    EXPECT_EQ(&td.Compilation(), &compilation);
    EXPECT_EQ(ctx.CurrentModule(), &module);            // entity.ParentModule()
    EXPECT_EQ(ctx.CurrentTypeDefinition(), &td);        // dynamic_cast succeeded
    EXPECT_EQ(ctx.CurrentMember(), nullptr);            // not an IMember
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `IEntity` ctor with an entity that IS-A `IMember` (a
// `TestMember`) but NOT `ITypeDefinition`: the `dynamic_cast<const ITypeDefinition*>` fails
// so `CurrentTypeDefinition` falls to `entity.DeclaringTypeDefinition()` (the configured
// pointer), and the `dynamic_cast<const IMember*>` succeeds so `CurrentMember` is the
// entity itself (pointer-identity). The compilation is `entity.Compilation()` and the
// current module is `entity.ParentModule()`.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, EntityCtorWithMemberEntityUsesDeclaringTypeDefinition)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition declaringTd(compilation);
    TestMember member(compilation, &module, &declaringTd);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IEntity&>(member));
    EXPECT_EQ(&ctx.Compilation(), &member.Compilation());
    EXPECT_EQ(&member.Compilation(), &compilation);
    EXPECT_EQ(ctx.CurrentModule(), &module);             // entity.ParentModule()
    EXPECT_EQ(ctx.CurrentTypeDefinition(), &declaringTd); // entity.DeclaringTypeDefinition()
    EXPECT_EQ(ctx.CurrentMember(), &member);             // dynamic_cast succeeded
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `IEntity` ctor with a member entity whose
// `DeclaringTypeDefinition()` is null (a top-level entity): `CurrentTypeDefinition` falls
// to null (the `dynamic_cast<const ITypeDefinition*>` failed and the fallback is null).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, EntityCtorWithMemberEntityAndNullDeclaringTypeDefinition)
{
    TestCompilation compilation;
    TestMember member(compilation, nullptr, nullptr);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IEntity&>(member));
    EXPECT_EQ(ctx.CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(ctx.CurrentMember(), &member);
    EXPECT_EQ(ctx.CurrentModule(), nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- `WithCurrentTypeDefinition` returns a NEW context with the
// current-type-definition slot REPLACED and the other slots (module / member / compilation)
// CARRIED OVER. Pinned by pointer-identity on all four slots.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithCurrentTypeDefinitionReplacesSlotAndPreservesOthers)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition tdA(compilation);
    TestTypeDefinition tdB(compilation);
    TestMember member(compilation);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::ICompilation&>(compilation));
    // Start with no slots; set the module and member via the entity-ctor path is not
    // straightforward, so build the context with all four slots via the ICompilation ctor
    // and use the With* to set the remaining slots. Actually, use the IModule ctor to get
    // a context with a module, then With* the type definition and member.

    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctxWithModule(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    auto withTd = ctxWithModule.WithCurrentTypeDefinition(&tdA);
    ASSERT_NE(withTd, nullptr);
    auto withAll = withTd->WithCurrentMember(&member);
    ASSERT_NE(withAll, nullptr);

    // Now replace the type definition slot.
    auto next = withAll->WithCurrentTypeDefinition(&tdB);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), &tdB);  // replaced
    EXPECT_EQ(next->CurrentModule(), &module);      // carried over
    EXPECT_EQ(next->CurrentMember(), &member);       // carried over
    EXPECT_EQ(&next->Compilation(), &compilation);   // carried over
    EXPECT_NE(next.get(), withAll.get());            // a NEW context
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- `WithCurrentTypeDefinition(nullptr)` clears the slot while
// carrying the others over.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithCurrentTypeDefinitionWithNullClearsSlot)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctxWithModule(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    auto withTd = ctxWithModule.WithCurrentTypeDefinition(&td);
    auto withAll = withTd->WithCurrentMember(&member);

    auto next = withAll->WithCurrentTypeDefinition(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), nullptr);  // cleared
    EXPECT_EQ(next->CurrentModule(), &module);          // carried over
    EXPECT_EQ(next->CurrentMember(), &member);           // carried over
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- `WithCurrentMember` returns a NEW context with the current-
// member slot REPLACED and the others carried over. The twin of the type-definition factory.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithCurrentMemberReplacesSlotAndPreservesOthers)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember memberA(compilation);
    TestMember memberB(compilation);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctxWithModule(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    auto withTd = ctxWithModule.WithCurrentTypeDefinition(&td);
    auto withAll = withTd->WithCurrentMember(&memberA);

    auto next = withAll->WithCurrentMember(&memberB);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentMember(), &memberB);     // replaced
    EXPECT_EQ(next->CurrentModule(), &module);     // carried over
    EXPECT_EQ(next->CurrentTypeDefinition(), &td);  // carried over
    EXPECT_EQ(&next->Compilation(), &compilation);  // carried over
    EXPECT_NE(next.get(), withAll.get());          // a NEW context
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- `WithCurrentMember(nullptr)` clears the slot while carrying
// the others over.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithCurrentMemberWithNullClearsSlot)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctxWithModule(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    auto withTd = ctxWithModule.WithCurrentTypeDefinition(&td);
    auto withAll = withTd->WithCurrentMember(&member);

    auto next = withAll->WithCurrentMember(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentMember(), nullptr);     // cleared
    EXPECT_EQ(next->CurrentModule(), &module);     // carried over
    EXPECT_EQ(next->CurrentTypeDefinition(), &td); // carried over
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `With*` factories return `std::unique_ptr<ITypeResolveContext>`
// (the D409 convention -- the faithful C++ counterpart of the C# `new SimpleTypeResolveContext(...)`
// heap allocation transferred to the caller).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithFactoriesReturnUniquePtrTransferringOwnership)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::ICompilation&>(compilation));
    auto byTd = ctx.WithCurrentTypeDefinition(nullptr);
    auto byMember = ctx.WithCurrentMember(nullptr);
    static_assert(
        std::is_same_v<decltype(byTd),
                        std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>>,
        "WithCurrentTypeDefinition must return std::unique_ptr<ITypeResolveContext>.");
    static_assert(
        std::is_same_v<decltype(byMember),
                        std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>>,
        "WithCurrentMember must return std::unique_ptr<ITypeResolveContext>.");
    ASSERT_NE(byTd, nullptr);
    ASSERT_NE(byMember, nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `With*` factories are `const`-callable (the factory reads
// `this` without modifying it; the C# instance method ports to a `const` member). A
// `const SimpleTypeResolveContext&` can call them.
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithFactoriesAreConstCallable)
{
    TestCompilation compilation;
    const ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::ICompilation&>(compilation));
    auto next = ctx.WithCurrentTypeDefinition(nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->CurrentTypeDefinition(), nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the class IS-A `ITypeResolveContext`; an instance dispatches
// through the base pointer (the dynamic dispatch the resolution paths rely on: a context is
// held as an `ITypeResolveContext` and the slots reached through the base pointer without
// knowing the concrete `SimpleTypeResolveContext` kind).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, DispatchesPolymorphicallyThroughBasePointer)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation, &module, nullptr);
    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IEntity&>(td));
    const ILSpy::Decompiler::TypeSystem::ITypeResolveContext* base = &ctx;
    EXPECT_EQ(&base->Compilation(), &compilation);
    EXPECT_EQ(base->CurrentModule(), &module);
    EXPECT_EQ(base->CurrentTypeDefinition(), &td);
    EXPECT_EQ(base->CurrentMember(), nullptr);
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the class is `final` (the C# `sealed`) and IS-A
// `ITypeResolveContext` (single inheritance, no diamond); an instance is instantiable via
// any of the three public ctors and has a virtual destructor (inherited from
// `ITypeResolveContext`).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, IsFinalAndIsAnITypeResolveContext)
{
    static_assert(std::is_final_v<ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext>,
                  "SimpleTypeResolveContext must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ILSpy::Decompiler::TypeSystem::ITypeResolveContext,
                                      ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext>,
                  "SimpleTypeResolveContext must derive from ITypeResolveContext.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext>,
                  "SimpleTypeResolveContext must have a virtual destructor (inherited).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext>,
                  "SimpleTypeResolveContext must be polymorphic.");

    TestCompilation compilation;
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext> p =
        std::make_unique<ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext>(
            static_cast<const ILSpy::Decompiler::TypeSystem::ICompilation&>(compilation));
    EXPECT_NE(p, nullptr);
    p.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SimpleTypeResolveContext -- the `With*` factories compose: calling `WithCurrentTypeDefinition`
// then `WithCurrentMember` on the result builds a context with both slots set (the others
// carried over from the original).
// ---------------------------------------------------------------------------
TEST(SimpleTypeResolveContextTest, WithFactoriesComposeToSetAllSlots)
{
    TestCompilation compilation;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule module(compilation);
    TestTypeDefinition td(compilation);
    TestMember member(compilation);

    ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext ctx(
        static_cast<const ILSpy::Decompiler::TypeSystem::IModule&>(module));
    auto withTd = ctx.WithCurrentTypeDefinition(&td);
    auto withAll = withTd->WithCurrentMember(&member);
    ASSERT_NE(withAll, nullptr);
    EXPECT_EQ(&withAll->Compilation(), &compilation);
    EXPECT_EQ(withAll->CurrentModule(), &module);
    EXPECT_EQ(withAll->CurrentTypeDefinition(), &td);
    EXPECT_EQ(withAll->CurrentMember(), &member);
}
