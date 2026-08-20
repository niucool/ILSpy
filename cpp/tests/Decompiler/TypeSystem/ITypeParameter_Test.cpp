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

// Tests for `ITypeParameter` (cpp/Decompiler/TypeSystem/ITypeParameter.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/ITypeParameter.cs) and its co-located `TypeConstraint`
// value struct (cpp/Decompiler/TypeSystem/TypeConstraint.hpp). `ITypeParameter : IType, ISymbol`
// is the interface for a generic type parameter (the `T` in `class Foo<T>` or `void M<T>()`):
// it adds the owner kind / owner / index, the variance, the effective base class / interface
// set, the constraint flags (`new()` / `class` / `struct` / `unmanaged` / `allows ref struct`),
// the nullability constraint, the attributes, and the type constraints.
//
// The `Name()` redeclaration disambiguates the `IType` + `ISymbol` multiple-inheritance diamond
// (the D381 `IEntity` precedent): a concrete type parameter overrides `Name()` once and dispatch
// through `IType*` / `ISymbol*` / `ITypeParameter*` all reach it.
//
// The `Owner` accessor returns `const IEntity*` (a nullable raw pointer), so the test provides a
// minimal concrete `IEntity` stand-in (`TestEntity`) to point at. The `IModule` / `ICompilation` stand-ins are IDENTICAL to those in `IEntity_Test.cpp` (ODR-safe
// across translation units); `ITypeDefinition` is the real port (D393) and `IAttribute` the real port
// (D386), both included above.

#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// `ITypeDefinition` is now the real port (cpp/Decompiler/TypeSystem/ITypeDefinition.hpp, D393),
// included above; the `TestTypeDefinition` stub below derives from it (it is never instantiated,
// so it stays abstract and overrides nothing).

// Minimal test stand-in for `IModule` -- IDENTICAL to the stand-in in `IEntity_Test.cpp`.
class IModule {
public:
    virtual ~IModule() = default;
};

// `IAttribute` is now the real port (cpp/Decompiler/TypeSystem/IAttribute.hpp, D386);
// it is included above rather than forward-declared as a stand-in. `IMethod` (its
// `Constructor` slot return type) remains forward-declared inside `IAttribute.hpp`.

// Minimal test stand-in for `ICompilation` -- IDENTICAL to the stand-in in `IEntity_Test.cpp`
// (ODR-safe across translation units).
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

// A minimal concrete `IAttribute` for testing (identity-testable via pointer compare).
// Derives from the real `IAttribute` (D386) and implements every pure-virtual with simple
// defaults (`AttributeType` returns a `KnownType(Object)` by reference, `Constructor` is
// null, `HasDecodeErrors` is false, the argument vectors are empty); the test-specific `id()`
// accessor and `id_` member are kept so the existing `GetAttributes` pointer-identity tests
// continue to work.
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(int id) : id_(id), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    int id() const { return id_; }

    // --- IAttribute ---
    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    int id_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base can
// return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A minimal concrete `IEntity` for testing the `Owner` slot: holds the configured state and
// returns it from every accessor. `Name()` is overridden ONCE and satisfies both the
// `ISymbol::Name()` and `INamedElement::Name()` base contracts (the D374 "inherited virtual
// covers the `new`" precedent applied to the multiple-inheritance diamond). Adapted from the
// `TestEntity` in `IEntity_Test.cpp`.
class TestEntity : public ILSpy::Decompiler::TypeSystem::IEntity {
public:
    TestEntity(std::string name,
               ILSpy::Decompiler::TypeSystem::SymbolKind kind,
               const TestCompilation& compilation,
               ILSpy::Decompiler::TypeSystem::Accessibility accessibility)
        : name_(std::move(name)), kind_(kind), compilation_(compilation),
          accessibility_(accessibility) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

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
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
};

// A minimal concrete `ITypeParameter` for testing: holds the configured state and returns it
// from every accessor (the shape a real `MetadataTypeParameter` / `DefaultTypeParameter`
// takes). The `SymbolKind()` / `OwnerType()` return types are qualified because the inherited
// `ISymbol::SymbolKind()` member function hides the namespace-scope `SymbolKind` enum in this
// derived class (the D372 cross-scope name-hiding crux). `Name()` is overridden once and is the
// final overrider for `IType::Name`, `ISymbol::Name`, and `ITypeParameter::Name` (the D381
// multiple-inheritance-diamond disambiguation).
class TestTypeParameter : public ILSpy::Decompiler::TypeSystem::ITypeParameter {
public:
    TestTypeParameter(
        ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
        std::string name, int index,
        ILSpy::Decompiler::TypeSystem::VarianceModifier variance,
        ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint,
        bool hasDefaultCtor, bool hasRefType, bool hasValueType,
        bool hasUnmanaged, bool allowsRefLike)
        : ownerType_(ownerType), name_(std::move(name)), index_(index),
          variance_(variance), nullabilityConstraint_(nullabilityConstraint),
          hasDefaultCtor_(hasDefaultCtor), hasRefType_(hasRefType),
          hasValueType_(hasValueType), hasUnmanaged_(hasUnmanaged),
          allowsRefLike_(allowsRefLike) {}

    // --- IType ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeKind::TypeParameter;
    }
    // The single `Name()` override is the final overrider for `IType::Name`, `ISymbol::Name`,
    // and `ITypeParameter::Name` (the D381 diamond disambiguation).
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }

    // --- ITypeParameter ---
    ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerType_; }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return owner_; }
    int Index() const override { return index_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return attributes_;
    }
    ILSpy::Decompiler::TypeSystem::VarianceModifier Variance() const override { return variance_; }
    ILSpy::Decompiler::TypeSystem::ITypePtr EffectiveBaseClass() const override
    {
        return effectiveBaseClass_;
    }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> EffectiveInterfaceSet() const override
    {
        return effectiveInterfaceSet_;
    }
    bool HasDefaultConstructorConstraint() const override { return hasDefaultCtor_; }
    bool HasReferenceTypeConstraint() const override { return hasRefType_; }
    bool HasValueTypeConstraint() const override { return hasValueType_; }
    bool HasUnmanagedConstraint() const override { return hasUnmanaged_; }
    bool AllowsRefLikeType() const override { return allowsRefLike_; }
    ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return nullabilityConstraint_;
    }
    std::vector<ILSpy::Decompiler::TypeSystem::TypeConstraint> TypeConstraints() const override
    {
        return typeConstraints_;
    }

protected:
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType& other) const override
    {
        return this == &other; // identity equality for the test stub
    }

    // Test wiring (set the nullable / collection slots after construction).
public:
    void SetOwner(const ILSpy::Decompiler::TypeSystem::IEntity* owner) { owner_ = owner; }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void SetEffectiveBaseClass(ILSpy::Decompiler::TypeSystem::ITypePtr t)
    {
        effectiveBaseClass_ = std::move(t);
    }
    void AddEffectiveInterface(ILSpy::Decompiler::TypeSystem::ITypePtr t)
    {
        effectiveInterfaceSet_.push_back(std::move(t));
    }
    void AddTypeConstraint(ILSpy::Decompiler::TypeSystem::TypeConstraint c)
    {
        typeConstraints_.push_back(std::move(c));
    }

private:
    ILSpy::Decompiler::TypeSystem::SymbolKind ownerType_;
    std::string name_;
    int index_;
    ILSpy::Decompiler::TypeSystem::VarianceModifier variance_;
    ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint_;
    bool hasDefaultCtor_, hasRefType_, hasValueType_, hasUnmanaged_, allowsRefLike_;
    const ILSpy::Decompiler::TypeSystem::IEntity* owner_ = nullptr;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    ILSpy::Decompiler::TypeSystem::ITypePtr effectiveBaseClass_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> effectiveInterfaceSet_;
    std::vector<ILSpy::Decompiler::TypeSystem::TypeConstraint> typeConstraints_;
};

} // namespace

// ---------------------------------------------------------------------------
// TypeConstraint -- the computed `SymbolKind` property always returns `SymbolKind::Constraint`,
// the `Type` accessor returns the configured non-null `ITypePtr`, and the `Attributes` accessor
// returns the configured (possibly empty) non-owning snapshot. The constructor asserts non-null
// type.
// ---------------------------------------------------------------------------
TEST(TypeConstraintTest, ComputedSymbolKindTypeAndAttributes)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    auto baseType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TypeConstraint constraint(baseType);
    EXPECT_EQ(constraint.SymbolKind(), SymbolKind::Constraint);
    ASSERT_NE(constraint.Type(), nullptr);
    EXPECT_EQ(constraint.Type()->Kind(), TypeKind::Class);
    EXPECT_TRUE(constraint.Attributes().empty());

    // With attributes.
    TestAttribute attr1(1);
    TestAttribute attr2(2);
    std::vector<const IAttribute*> attrs{&attr1, &attr2};
    TypeConstraint constraintWithAttrs(baseType, attrs);
    EXPECT_EQ(constraintWithAttrs.Attributes().size(), 2u);
    EXPECT_EQ(constraintWithAttrs.Attributes()[0], &attr1);
    EXPECT_EQ(constraintWithAttrs.Attributes()[1], &attr2);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- every scalar accessor returns the configured value (the shape a real
// `MetadataTypeParameter` exposes: its owner kind, name, index, variance, constraint flags,
// and nullability constraint).
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, ScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 0,
        VarianceModifier::Covariant, Nullability::NotNullable,
        /*hasDefaultCtor*/ true, /*hasRefType*/ false, /*hasValueType*/ true,
        /*hasUnmanaged*/ false, /*allowsRefLike*/ true);
    EXPECT_EQ(tp.OwnerType(), SymbolKind::TypeDefinition);
    EXPECT_EQ(tp.Name(), "T");
    EXPECT_EQ(tp.Index(), 0);
    EXPECT_EQ(tp.Variance(), VarianceModifier::Covariant);
    EXPECT_EQ(tp.NullabilityConstraint(), Nullability::NotNullable);
    EXPECT_TRUE(tp.HasDefaultConstructorConstraint());
    EXPECT_FALSE(tp.HasReferenceTypeConstraint());
    EXPECT_TRUE(tp.HasValueTypeConstraint());
    EXPECT_FALSE(tp.HasUnmanagedConstraint());
    EXPECT_TRUE(tp.AllowsRefLikeType());
}

// ---------------------------------------------------------------------------
// ITypeParameter -- the single `Name()` override satisfies BOTH the `IType::Name()` and
// `ISymbol::Name()` base contracts (the D381 multiple-inheritance-diamond disambiguation):
// dispatching `Name()` through an `IType*`, an `ISymbol*`, and an `ITypeParameter*` all reach
// the same override. The `Kind()` / `ReflectionName()` / `TypeParameterCount()` (from `IType`)
// and `SymbolKind()` (from `ISymbol`) also dispatch through the base pointers.
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, SingleNameOverrideSatisfiesBothBases)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    auto owned = std::make_unique<TestTypeParameter>(
        SymbolKind::Method, "U", 1, VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    TestTypeParameter* tp = owned.get();
    IType* asType = tp;
    ISymbol* asSymbol = tp;
    ITypeParameter* asTp = tp;
    EXPECT_EQ(asType->Name(), "U");
    EXPECT_EQ(asSymbol->Name(), "U");
    EXPECT_EQ(asTp->Name(), "U");
    // All three dispatch the same final overrider.
    EXPECT_EQ(asType->Name(), asSymbol->Name());
    EXPECT_EQ(asSymbol->Name(), asTp->Name());
    // IType / ISymbol base accessors also dispatch.
    EXPECT_EQ(asType->Kind(), TypeKind::TypeParameter);
    EXPECT_EQ(asType->TypeParameterCount(), 0);
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::TypeParameter);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- `Owner` returns the configured entity, or nullptr for the dummy type
// parameters used by `NormalizeTypeVisitor.ReplaceMethodTypeParametersWithDummy` (the C#
// `May return null` doc comment). A nullable raw pointer return (the `IEntity::ParentModule`
// precedent).
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, OwnerReturnsConfiguredEntityOrNull)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    TestEntity entity("Foo", SymbolKind::TypeDefinition, compilation, Accessibility::Public);
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 0,
        VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    tp.SetOwner(&entity);
    EXPECT_EQ(tp.Owner(), &entity);

    // The null case: a dummy type parameter has no owner.
    TestTypeParameter dummyTp(SymbolKind::Method, "!!0", 0,
        VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    EXPECT_EQ(dummyTp.Owner(), nullptr);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- `GetAttributes` returns the configured non-owning snapshot (the type
// parameter owns its attributes; the caller holds raw pointers), matching the AST non-owning
// model and the `IEntity::GetAttributes` / `IParameter::GetAttributes` precedent.
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, GetAttributesReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 0,
        VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    TestAttribute attr1(1);
    TestAttribute attr2(2);
    tp.AddAttribute(&attr1);
    tp.AddAttribute(&attr2);
    const auto attrs = tp.GetAttributes();
    EXPECT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0], &attr1);
    EXPECT_EQ(attrs[1], &attr2);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- `EffectiveBaseClass` and `EffectiveInterfaceSet` return the configured
// shared `ITypePtr` (the single effective base class) and the configured vector of shared
// `ITypePtr`s (the effective interface set). These are computed by the type system and held
// as shared, cached `IType`s.
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, EffectiveBaseClassAndInterfaceSetReturnConfiguredTypes)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 0,
        VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    auto baseClass = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto iface1 = std::make_shared<KnownType>(KnownTypeCode::IDisposable);
    auto iface2 = std::make_shared<KnownType>(KnownTypeCode::IEnumerable);
    tp.SetEffectiveBaseClass(baseClass);
    tp.AddEffectiveInterface(iface1);
    tp.AddEffectiveInterface(iface2);

    ASSERT_NE(tp.EffectiveBaseClass(), nullptr);
    EXPECT_EQ(tp.EffectiveBaseClass()->Kind(), TypeKind::Class);
    EXPECT_EQ(tp.EffectiveBaseClass(), baseClass); // shared-identity

    const auto ifaces = tp.EffectiveInterfaceSet();
    EXPECT_EQ(ifaces.size(), 2u);
    EXPECT_EQ(ifaces[0], iface1);
    EXPECT_EQ(ifaces[1], iface2);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- `TypeConstraints` returns the configured `TypeConstraint` value vector
// (the `Base` / `IInterface` in `where T : Base, IInterface`). Each `TypeConstraint` carries
// its own `SymbolKind` (always `Constraint`), `Type`, and `Attributes`.
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, TypeConstraintsReturnConfiguredConstraints)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 0,
        VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    auto baseType = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto ifaceType = std::make_shared<KnownType>(KnownTypeCode::IDisposable);
    tp.AddTypeConstraint(TypeConstraint(baseType));
    tp.AddTypeConstraint(TypeConstraint(ifaceType));

    const auto constraints = tp.TypeConstraints();
    EXPECT_EQ(constraints.size(), 2u);
    EXPECT_EQ(constraints[0].SymbolKind(), SymbolKind::Constraint);
    ASSERT_NE(constraints[0].Type(), nullptr);
    EXPECT_EQ(constraints[0].Type(), baseType);
    EXPECT_EQ(constraints[1].SymbolKind(), SymbolKind::Constraint);
    EXPECT_EQ(constraints[1].Type(), ifaceType);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- polymorphic dispatch through an `ITypeParameter*` reaches the concrete
// accessors of every inherited base (IType / ISymbol) AND the ITypeParameter-own accessors
// (the dynamic dispatch the type-system paths rely on: `TypeSystemAstBuilder` and the resolver
// hold a type parameter as an `ITypeParameter*` and read its variance, name, constraints, and
// owner through the base).
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, DispatchesPolymorphicallyThroughITypeParameterPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestTypeParameter tp(SymbolKind::TypeDefinition, "T", 2,
        VarianceModifier::Contravariant, Nullability::Nullable,
        true, true, false, false, false);
    ITypeParameter* base = &tp;
    EXPECT_EQ(base->Name(), "T");
    EXPECT_EQ(base->OwnerType(), SymbolKind::TypeDefinition);
    EXPECT_EQ(base->Index(), 2);
    EXPECT_EQ(base->Variance(), VarianceModifier::Contravariant);
    EXPECT_EQ(base->NullabilityConstraint(), Nullability::Nullable);
    EXPECT_TRUE(base->HasDefaultConstructorConstraint());
    EXPECT_TRUE(base->HasReferenceTypeConstraint());
    EXPECT_EQ(base->SymbolKind(), SymbolKind::TypeParameter);
    EXPECT_EQ(base->Kind(), TypeKind::TypeParameter);
}

// ---------------------------------------------------------------------------
// ITypeParameter -- polymorphic dispatch through the `IType*` and `ISymbol*` base pointers (an
// `ITypeParameter` IS-A each of its bases, so each base pointer dispatches to the concrete
// override). `IType` IS-A `IType` (the `Kind` / `Name` / `ReflectionName` /
// `TypeParameterCount` contract); `ISymbol` IS-A `ISymbol` (the `SymbolKind` / `Name` contract).
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, DispatchesPolymorphicallyThroughITypeAndISymbolPointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    auto owned = std::make_unique<TestTypeParameter>(
        SymbolKind::Method, "V", 3, VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    IType* asType = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asType->Name(), "V");
    EXPECT_EQ(asType->Kind(), TypeKind::TypeParameter);
    EXPECT_EQ(asType->ReflectionName(), "V");
    EXPECT_EQ(asType->TypeParameterCount(), 0);
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::TypeParameter);
    EXPECT_EQ(asSymbol->Name(), "V");
    // destroying `owned` runs the `TestTypeParameter` destructor through the virtual
    // `~ITypeParameter()` (which chains to `~IType()` / `~ISymbol()`).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// ITypeParameter -- has a virtual destructor (a concrete subclass can be deleted through an
// `ITypeParameter*` / `IType*` / `ISymbol*` and the derived destructor runs), the established
// abstract-base contract; the multiple-inheritance of `IType` + `ISymbol` (each with its own
// virtual destructor) composes correctly.
// ---------------------------------------------------------------------------
TEST(ITypeParameterTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeParameter>,
        "ITypeParameter must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IType>,
        "IType (the base of ITypeParameter) must have a virtual destructor");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ISymbol>,
        "ISymbol (the base of ITypeParameter) must have a virtual destructor");
    using namespace ILSpy::Decompiler::TypeSystem;
    std::unique_ptr<ITypeParameter> owned = std::make_unique<TestTypeParameter>(
        SymbolKind::TypeDefinition, "W", 0, VarianceModifier::Invariant, Nullability::Oblivious,
        false, false, false, false, false);
    EXPECT_EQ(owned->Name(), "W");
    // destroying `owned` runs the `TestTypeParameter` destructor through the virtual
    // `~ITypeParameter()` (which chains to `~IType()` / `~ISymbol()`).
    owned.reset();
    SUCCEED();
}
