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

// Tests for `IEntity` (cpp/Decompiler/TypeSystem/IEntity.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IEntity.cs). `IEntity` is the root of the
// resolved-entity hierarchy: every resolved type definition / member / (transitively)
// namespace / parameter / type-parameter is an `IEntity`. It multiply-inherits `ISymbol`
// (D372), `ICompilationProvider` (D379), and `INamedElement` (D375), and adds the
// `MetadataToken` (raw `std::uint32_t`, the EntityHandle design decision), the nullable
// `DeclaringTypeDefinition` / `DeclaringType` / `ParentModule` slots, the attribute
// triple `GetAttributes` / `HasAttribute` / `GetAttribute`, and the `Accessibility` /
// `IsStatic` / `IsAbstract` / `IsSealed` scalars.
//
// The cyclic member types `ITypeDefinition`, `IModule`, and `IAttribute` are only
// forward-declared in `IEntity.hpp` (they are not yet ported), so this test file provides
// minimal complete stand-ins for all three in the `ILSpy::Decompiler::TypeSystem`
// namespace (virtual destructors only) so the `TestEntity` stub can hold and return
// concrete instances. These stand-ins are TEST FIXTURES, NOT faithful ports of the full
// surfaces (which pull the rest of the type system); they are dropped when the real
// headers land. No other translation unit in the test build defines these three, so the
// stand-ins are the sole definitions (ODR-safe for the test executable).

#include "Decompiler/TypeSystem/IEntity.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Minimal test stand-in for `ITypeDefinition` (the resolved type-definition interface).
// Only a virtual destructor is declared here -- this is the complete type the
// `IEntity::DeclaringTypeDefinition()` nullable pointer return needs to point at, NOT a
// faithful port of the full `ITypeDefinition` surface (which pulls `IMember` / `IField` /
// ...). Replaced by the real `ITypeDefinition.hpp` when that lands.
class ITypeDefinition {
public:
    virtual ~ITypeDefinition() = default;
};

// Minimal test stand-in for `IModule` (the metadata-module interface). Only a virtual
// destructor; the real `IModule` pulls `MetadataFile` / `INamespace` / `ITypeDefinition` /
// `IAttribute` / `TopLevelTypeName`.
class IModule {
public:
    virtual ~IModule() = default;
};

// Minimal test stand-in for `IAttribute` (the attribute interface). Only a virtual
// destructor; the real `IAttribute` pulls `IType` / `IMethod` and the
// `CustomAttributeTypedArgument` / `CustomAttributeNamedArgument` value-argument structs.
class IAttribute {
public:
    virtual ~IAttribute() = default;
};

// Minimal test stand-in for `ICompilation` (the parent compilation interface). Only a
// virtual destructor is declared here -- this is the complete type the inherited
// `ICompilationProvider::Compilation()` reference return needs to bind to so the
// `TestEntity` stub can hold and return a concrete `TestCompilation`, NOT a faithful port
// of the full `ICompilation` surface (which pulls `IModule` / `INamespace` / `IType` /
// `KnownTypeCode` / `CacheManager` / `TypeSystemOptions` / `StringComparer`). This stand-in
// is IDENTICAL to the one in `ICompilationProvider_Test.cpp`; the two identical class
// definitions across translation units satisfy the One Definition Rule (a class type may
// be defined identically in multiple TUs, as a header is), so both test files coexist in
// the `ilspy_tests` executable. Replaced by the real `ICompilation.hpp` when that lands.
class ICompilation {
public:
    virtual ~ICompilation() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A minimal concrete `ITypeDefinition` for testing (identity-testable via pointer compare).
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    explicit TestTypeDefinition(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A minimal concrete `IModule` for testing.
class TestModule : public ILSpy::Decompiler::TypeSystem::IModule {
public:
    explicit TestModule(std::string assemblyName) : assemblyName_(std::move(assemblyName)) {}
    const std::string& AssemblyName() const { return assemblyName_; }
private:
    std::string assemblyName_;
};

// A minimal concrete `IAttribute` for testing.
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute kind) : kind_(kind) {}
    ILSpy::Decompiler::TypeSystem::KnownAttribute Kind() const { return kind_; }
private:
    ILSpy::Decompiler::TypeSystem::KnownAttribute kind_;
};

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider`
// base can return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A minimal concrete `IEntity` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataTypeDefinition` /
// `MetadataMethod` / ... takes: it stores its compilation, declaring type, module, and
// attributes and returns them). `Name()` is overridden ONCE and satisfies both the
// `ISymbol::Name()` and `INamedElement::Name()` base contracts (the D374 "inherited
// virtual covers the `new`" precedent applied to the multiple-inheritance diamond).
class TestEntity : public ILSpy::Decompiler::TypeSystem::IEntity {
public:
    TestEntity(std::string name,
               std::string fullName,
               std::string reflectionName,
               std::string ns,
               ILSpy::Decompiler::TypeSystem::SymbolKind kind,
               const TestCompilation& compilation,
               std::uint32_t metadataToken,
               ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
               bool isStatic, bool isAbstract, bool isSealed)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    // The single `Name()` override is the final overrider for both `ISymbol::Name()` and
    // `INamedElement::Name()`.
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return fullName_; }
    std::string ReflectionName() const override { return reflectionName_; }
    std::string Namespace() const override { return namespace_; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return declaringTypeDefinition_;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return declaringType_; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return parentModule_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return attributes_;
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        for (auto* a : attributes_) {
            if (static_cast<const TestAttribute*>(a)->Kind() == attribute) return true;
        }
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        for (auto* a : attributes_) {
            if (static_cast<const TestAttribute*>(a)->Kind() == attribute) return a;
        }
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }

    // Test wiring (set the nullable slots after construction).
    void SetDeclaringTypeDefinition(const ILSpy::Decompiler::TypeSystem::ITypeDefinition* td)
    {
        declaringTypeDefinition_ = td;
    }
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void SetParentModule(const ILSpy::Decompiler::TypeSystem::IModule* m) { parentModule_ = m; }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition_ = nullptr;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
    const ILSpy::Decompiler::TypeSystem::IModule* parentModule_ = nullptr;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
};

} // namespace

// ---------------------------------------------------------------------------
// IEntity -- the scalar accessors return the configured values (the shape a real
// `MetadataTypeDefinition` exposes: its kind, names, metadata token, accessibility, and
// static/abstract/sealed flags).
// ---------------------------------------------------------------------------
TEST(IEntityTest, ScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    TestEntity entity("List", "System.Collections.Generic.List",
                      "System.Collections.Generic.List`1", "System.Collections.Generic",
                      SymbolKind::TypeDefinition, compilation, 0x02000042u,
                      Accessibility::Public, /*isStatic*/ false, /*isAbstract*/ false,
                      /*isSealed*/ true);
    EXPECT_EQ(entity.Name(), "List");
    EXPECT_EQ(entity.FullName(), "System.Collections.Generic.List");
    EXPECT_EQ(entity.ReflectionName(), "System.Collections.Generic.List`1");
    EXPECT_EQ(entity.Namespace(), "System.Collections.Generic");
    EXPECT_EQ(entity.SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(entity.MetadataToken(), 0x02000042u);
    EXPECT_EQ(entity.Accessibility(), Accessibility::Public);
    EXPECT_FALSE(entity.IsStatic());
    EXPECT_FALSE(entity.IsAbstract());
    EXPECT_TRUE(entity.IsSealed());
}

// ---------------------------------------------------------------------------
// IEntity -- the nullable pointer slots return the configured targets (the declaring
// type definition, the declaring type, and the parent module), and nullptr when unset
// (the top-level-entity case the C# doc comments describe).
// ---------------------------------------------------------------------------
TEST(IEntityTest, NullablePointerSlotsReturnConfiguredTargets)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    TestTypeDefinition outer(10);
    TestModule module("mscorlib");
    auto declaringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestEntity entity("Inner", "Outer.Inner", "Outer.Inner", "",
                      SymbolKind::TypeDefinition, compilation, 0x02000099u,
                      Accessibility::Public, false, false, false);
    entity.SetDeclaringTypeDefinition(&outer);
    entity.SetDeclaringType(declaringType);
    entity.SetParentModule(&module);

    EXPECT_EQ(entity.DeclaringTypeDefinition(), &outer);
    EXPECT_EQ(static_cast<const TestTypeDefinition*>(entity.DeclaringTypeDefinition())->id(), 10);
    ASSERT_NE(entity.DeclaringType(), nullptr);
    EXPECT_EQ(entity.DeclaringType()->Kind(), TypeKind::Class);
    EXPECT_EQ(entity.ParentModule(), &module);
    EXPECT_EQ(static_cast<const TestModule*>(entity.ParentModule())->AssemblyName(), "mscorlib");
}

// ---------------------------------------------------------------------------
// IEntity -- the nullable pointer slots return nullptr for a top-level entity (the C#
// `DeclaringTypeDefinition` / `DeclaringType` / `ParentModule` all return null for a
// top-level entity).
// ---------------------------------------------------------------------------
TEST(IEntityTest, NullablePointerSlotsReturnNullForTopLevelEntity)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    TestEntity entity("Program", "Program", "Program", "",
                      SymbolKind::TypeDefinition, compilation, 0u,
                      Accessibility::Internal, false, false, false);
    EXPECT_EQ(entity.DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(entity.DeclaringType(), nullptr);
    EXPECT_EQ(entity.ParentModule(), nullptr);
}

// ---------------------------------------------------------------------------
// IEntity -- the attribute triple: `GetAttributes` returns the configured snapshot,
// `HasAttribute` classifies by known kind, and `GetAttribute` returns the first match or
// null. The snapshot is non-owning (the entity owns its attributes; the caller holds raw
// pointers), matching the AST non-owning model.
// ---------------------------------------------------------------------------
TEST(IEntityTest, AttributeTripleClassifiesByKnownKind)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    TestAttribute compilerGenerated(KnownAttribute::CompilerGenerated);
    TestAttribute extension(KnownAttribute::Extension);
    TestEntity entity("Foo", "Foo", "Foo", "", SymbolKind::Method, compilation, 0x06000001u,
                      Accessibility::Public, true, false, false);
    entity.AddAttribute(&compilerGenerated);
    entity.AddAttribute(&extension);

    const auto attrs = entity.GetAttributes();
    EXPECT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0], &compilerGenerated);
    EXPECT_EQ(attrs[1], &extension);

    EXPECT_TRUE(entity.HasAttribute(KnownAttribute::CompilerGenerated));
    EXPECT_TRUE(entity.HasAttribute(KnownAttribute::Extension));
    EXPECT_FALSE(entity.HasAttribute(KnownAttribute::Obsolete));

    EXPECT_EQ(entity.GetAttribute(KnownAttribute::Extension), &extension);
    EXPECT_EQ(entity.GetAttribute(KnownAttribute::CompilerGenerated), &compilerGenerated);
    EXPECT_EQ(entity.GetAttribute(KnownAttribute::Obsolete), nullptr);
}

// ---------------------------------------------------------------------------
// IEntity -- the single `Name()` override satisfies BOTH the `ISymbol::Name()` and
// `INamedElement::Name()` base contracts (the D374 "inherited virtual covers the `new`"
// precedent applied to the multiple-inheritance diamond): dispatching `Name()` through an
// `ISymbol*`, an `INamedElement*`, and an `IEntity*` all reach the same override.
// ---------------------------------------------------------------------------
TEST(IEntityTest, SingleNameOverrideSatisfiesBothBases)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto owned = std::make_unique<TestEntity>(
        "Resolve", "NS.Resolve", "NS.Resolve", "NS", SymbolKind::Method, compilation,
        0x06000005u, Accessibility::Public, true, false, false);
    TestEntity* entity = owned.get();
    ISymbol* asSymbol = entity;
    INamedElement* asNamed = entity;
    IEntity* asEntity = entity;
    EXPECT_EQ(asSymbol->Name(), "Resolve");
    EXPECT_EQ(asNamed->Name(), "Resolve");
    EXPECT_EQ(asEntity->Name(), "Resolve");
    // All three dispatch the same final overrider.
    EXPECT_EQ(asSymbol->Name(), asNamed->Name());
    EXPECT_EQ(asNamed->Name(), asEntity->Name());
}

// ---------------------------------------------------------------------------
// IEntity -- polymorphic dispatch through an `IEntity*` reaches the concrete accessors of
// every inherited base (ISymbol / ICompilationProvider / INamedElement) AND the IEntity-own
// accessors (the dynamic dispatch the type-system paths rely on: `TypeSystemAstBuilder`
// and the resolver hold an entity as an `IEntity*` and read its names, compilation, kind,
// and attributes through the base).
// ---------------------------------------------------------------------------
TEST(IEntityTest, DispatchesPolymorphicallyThroughIEntityPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    TestEntity entity("Bar", "NS.Bar", "NS.Bar`2", "NS", SymbolKind::TypeDefinition,
                      compilation, 0x02000007u, Accessibility::Public, false, true, true);
    IEntity* base = &entity;
    EXPECT_EQ(base->Name(), "Bar");
    EXPECT_EQ(base->FullName(), "NS.Bar");
    EXPECT_EQ(base->ReflectionName(), "NS.Bar`2");
    EXPECT_EQ(base->Namespace(), "NS");
    EXPECT_EQ(base->SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(&base->Compilation(), &compilation);
    EXPECT_EQ(static_cast<const TestCompilation&>(base->Compilation()).id(), 6);
    EXPECT_EQ(base->MetadataToken(), 0x02000007u);
    EXPECT_EQ(base->Accessibility(), Accessibility::Public);
    EXPECT_TRUE(base->IsAbstract());
    EXPECT_TRUE(base->IsSealed());
    EXPECT_FALSE(base->IsStatic());
}

// ---------------------------------------------------------------------------
// IEntity -- polymorphic dispatch through the `ISymbol*` and `ICompilationProvider*` base
// pointers (the entity IS-A each of its bases, so each base pointer dispatches to the
// concrete override).
// ---------------------------------------------------------------------------
TEST(IEntityTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(8);
    TestEntity entity("Qux", "NS.Qux", "NS.Qux", "NS", SymbolKind::Field, compilation,
                      0x04000001u, Accessibility::Private, true, false, false);
    ISymbol* asSymbol = &entity;
    ICompilationProvider* asProvider = &entity;
    INamedElement* asNamed = &entity;
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(asSymbol->Name(), "Qux");
    EXPECT_EQ(&asProvider->Compilation(), &compilation);
    EXPECT_EQ(asNamed->FullName(), "NS.Qux");
    EXPECT_EQ(asNamed->Namespace(), "NS");
}

// ---------------------------------------------------------------------------
// IEntity -- has a virtual destructor (a concrete subclass can be deleted through an
// `IEntity*` and the derived destructor runs), the established abstract-base contract;
// the multiple-inheritance of `ISymbol` / `ICompilationProvider` / `INamedElement` (each
// with its own virtual destructor) composes correctly.
// ---------------------------------------------------------------------------
TEST(IEntityTest, HasVirtualDestructor)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    static_assert(std::has_virtual_destructor_v<IEntity>,
        "IEntity must have a virtual destructor for abstract-base deletion");
    TestCompilation compilation(9);
    std::unique_ptr<IEntity> owned = std::make_unique<TestEntity>(
        "Dtor", "NS.Dtor", "NS.Dtor", "NS", SymbolKind::TypeDefinition, compilation, 0u,
        Accessibility::Public, false, false, false);
    EXPECT_EQ(owned->Name(), "Dtor");
    // destroying `owned` runs the `TestEntity` destructor through the virtual `~IEntity()`
    // (which chains to `~ISymbol()` / `~ICompilationProvider()` / `~INamedElement()`).
    owned.reset();
    SUCCEED();
}
