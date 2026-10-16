// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `SpecializedProperty` (D486) -- the concrete `IProperty` a
// `GetMembersHelper.GetPropertiesImpl` builds for a property on a parameterized type
// (`Implementation/SpecializedProperty.hpp`). It derives `SpecializedParameterizedMember,
// IProperty`: the `SpecializedParameterizedMember` base supplies the substituted `Parameters`
// list (lazily) + the substituted `ReturnType` / `DeclaringType` / `Substitution` + the
// delegated `IMember` surface; this class adds the `IProperty`-own surface, delegating to
// the wrapped `propertyDefinition`.
//
// The tests pin:
//  (a) the ctor wires the substitution (`Substitution()` is the composed substitution, NOT
//      `Identity`);
//  (b) `Substitution()` / `MemberDefinition()` / `Specialize()` delegate to the
//      `SpecializedMember` base;
//  (c) the trivial `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
//      `ISymbol` delegations forward to the base member;
//  (d) `ReturnType()` / `DeclaringType()` are the SUBSTITUTED values (a class type parameter
//      at index 0 -> the substitution's class type argument);
//  (e) `Parameters()` (inherited from `SpecializedParameterizedMember`) is the SUBSTITUTED
//      list: a parameter whose type is a class type parameter at index 0 -> the substituted
//      type;
//  (f) the `IProperty`-own bools (`CanGet` / `CanSet` / `IsIndexer` /
//      `ReturnTypeIsRefReadOnly`) delegate to the property definition;
//  (g) the accessors (`Getter` / `Setter`) delegate to the property definition (DEFERRED
//      un-specialized);
//  (h) the three-`IMember`-subobject diamond: `SpecializedProperty` IS-A `IProperty` AND
//      `IMember`, dispatch through `IProperty*` / `IMember*` (via `SpecializedMember*`) all
//      reach the single override;
//  (i) `Equals` / `GetHashCode` (inherited from `SpecializedMember`).

#include "Decompiler/TypeSystem/Implementation/SpecializedProperty.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMember;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedProperty;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern).
class TestSubstTypeParameter : public ITypeParameter {
public:
    TestSubstTypeParameter(::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
                           std::string name, int index)
        : ownerType_(ownerType), name_(std::move(name)), index_(index) {}

    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerType_; }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    VarianceModifier Variance() const override { return VarianceModifier::Invariant; }
    ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    std::vector<TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType_;
    std::string name_;
    int index_;
};

// A configurable concrete `IParameter` (the `TestBaseParameter` pattern) -- the base
// property's parameter.
class TestBaseParameter : public IParameter {
public:
    TestBaseParameter(ITypePtr type, std::string name,
                      ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind =
                          ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                      bool isParams = false,
                      const IParameterizedMember* owner = nullptr)
        : type_(std::move(type)), name_(std::move(name)),
          referenceKind_(referenceKind), isParams_(isParams), owner_(owner) {}

    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return referenceKind_;
    }
    LifetimeAnnotation Lifetime() const override { return LifetimeAnnotation{}; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return owner_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::string Name() const override { return name_; }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }

private:
    ITypePtr type_;
    std::string name_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    bool isParams_;
    const IParameterizedMember* owner_;
};

// A configurable concrete `IProperty` -- the "property definition" the `SpecializedProperty`
// wraps. Implements the full `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider`
// / `ISymbol` / `IParameterizedMember` / `IProperty` surface. Holds the name, return type,
// declaring type, the CanGet / CanSet / IsIndexer / ReturnTypeIsRefReadOnly flags, the
// parameters, and the Getter / Setter accessors.
class TestBaseProperty : public IProperty {
public:
    TestBaseProperty(std::string name, ITypePtr returnType, ITypePtr declaringType,
                     const ICompilation& compilation,
                     std::vector<const IParameter*> parameters = {},
                     bool canGet = true, bool canSet = true, bool isIndexer = false,
                     bool returnTypeIsRefReadOnly = false,
                     const IMethod* getter = nullptr, const IMethod* setter = nullptr)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation),
          parameters_(std::move(parameters)), canGet_(canGet), canSet_(canSet),
          isIndexer_(isIndexer), returnTypeIsRefReadOnly_(returnTypeIsRefReadOnly),
          getter_(getter), setter_(setter) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IProperty ---
    bool CanGet() const override { return canGet_; }
    bool CanSet() const override { return canSet_; }
    bool IsIndexer() const override { return isIndexer_; }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    const IMethod* Getter() const override { return getter_; }
    const IMethod* Setter() const override { return setter_; }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    std::vector<const IParameter*> parameters_;
    bool canGet_;
    bool canSet_;
    bool isIndexer_;
    bool returnTypeIsRefReadOnly_;
    const IMethod* getter_;
    const IMethod* setter_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// Build a `std::optional<std::vector<ITypePtr>>` holding the given args (a present list).
std::optional<std::vector<ITypePtr>> List(std::vector<ITypePtr> args) {
    return std::optional<std::vector<ITypePtr>>(std::move(args));
}

// The shared `ICompilation` for the stubs.
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A generic `ITypeDefinition` (TypeParameterCount == 1) for the `DeclaringType` arm.
std::shared_ptr<LookupTypeDefinition> GenericDef() {
    return std::make_shared<LookupTypeDefinition>(
        "Foo", "", FullTypeName("Foo`1"), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor wires the substitution.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, CtorWiresSubstitution) {
    auto prop = std::make_shared<TestBaseProperty>("x", Int32(), nullptr, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_FALSE(sp.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
    const TypeParameterSubstitution sameArguments(
        List({String()}), std::nullopt);
    EXPECT_TRUE(sp.Substitution()->Equals(&sameArguments));
}

// ---------------------------------------------------------------------------
// MemberDefinition / Name / SymbolKind delegate to the base.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, MemberDefinitionAndNameAndSymbolKindDelegate) {
    auto prop = std::make_shared<TestBaseProperty>("myProp", Int32(), nullptr, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sp.MemberDefinition(), prop.get());
    EXPECT_EQ(sp.Name(), "myProp");
    EXPECT_EQ(sp.SymbolKind(), SymbolKind::Property);
}

// ---------------------------------------------------------------------------
// The trivial IEntity / INamedElement / ICompilationProvider delegations forward.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, TrivialEntitySurfaceDelegates) {
    auto prop = std::make_shared<TestBaseProperty>("x", Int32(), nullptr, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sp.FullName(), "x");
    EXPECT_EQ(sp.ReflectionName(), "x");
    EXPECT_EQ(sp.Namespace(), "");
    EXPECT_EQ(&sp.Compilation(), &Compilation());
    EXPECT_EQ(sp.ParentModule(), nullptr);
    EXPECT_EQ(sp.MetadataToken(), 0u);
    EXPECT_EQ(sp.DeclaringTypeDefinition(), nullptr);
    EXPECT_FALSE(sp.IsStatic());
    EXPECT_FALSE(sp.IsAbstract());
    EXPECT_FALSE(sp.IsSealed());
    EXPECT_EQ(sp.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(sp.GetAttributes().empty());
    EXPECT_FALSE(sp.IsExplicitInterfaceImplementation());
    EXPECT_FALSE(sp.IsVirtual());
    EXPECT_FALSE(sp.IsOverride());
    EXPECT_FALSE(sp.IsOverridable());
}

// ---------------------------------------------------------------------------
// ReturnType() applies the substitution.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, ReturnTypeAppliesSubstitution) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto prop = std::make_shared<TestBaseProperty>("x", returnType, nullptr, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_EQ(sp.ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// DeclaringType() applies the substitution.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, DeclaringTypeAppliesSubstitution) {
    auto def = GenericDef();
    auto prop = std::make_shared<TestBaseProperty>("x", Int32(), def, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto dt = sp.DeclaringType();
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->GetDefinition(), def.get());
    auto pt = std::dynamic_pointer_cast<ParameterizedType>(dt);
    ASSERT_NE(pt, nullptr);
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), "String");
}

// ---------------------------------------------------------------------------
// Parameters() (inherited from SpecializedParameterizedMember) is the SUBSTITUTED list: a
// parameter whose type is a class type parameter at index 0 -> the substituted type.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, ParametersAreSubstituted) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "index");
    auto prop = std::make_shared<TestBaseProperty>(
        "Item", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()},
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ true);
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto params = sp.Parameters();
    ASSERT_EQ(params.size(), 1u);
    // The parameter's type (T at index 0) is substituted to String.
    EXPECT_EQ(params[0]->Type().Name(), "String");
    // The base parameter name delegates through the SpecializedParameter.
    EXPECT_EQ(params[0]->Name(), "index");
}

// ---------------------------------------------------------------------------
// The IProperty-own bools delegate to the property definition.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, IPropertyBoolsDelegate) {
    auto prop = std::make_shared<TestBaseProperty>(
        "x", Int32(), nullptr, Compilation(), std::vector<const IParameter*>{},
        /*canGet*/ true, /*canSet*/ false, /*isIndexer*/ true,
        /*returnTypeIsRefReadOnly*/ true);
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sp.CanGet());
    EXPECT_FALSE(sp.CanSet());
    EXPECT_TRUE(sp.IsIndexer());
    EXPECT_TRUE(sp.ReturnTypeIsRefReadOnly());
}

// ---------------------------------------------------------------------------
// The accessors (Getter / Setter) delegate to the property definition (DEFERRED
// un-specialized -- the base accessor is returned).
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, AccessorsDelegateToBase) {
    auto getter = std::make_shared<LookupMethod>("get_x", Compilation());
    auto setter = std::make_shared<LookupMethod>("set_x", Compilation());
    auto prop = std::make_shared<TestBaseProperty>(
        "x", Int32(), nullptr, Compilation(), std::vector<const IParameter*>{},
        true, true, false, false, getter.get(), setter.get());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sp.Getter(), getter.get());
    EXPECT_EQ(sp.Setter(), setter.get());
}

// ---------------------------------------------------------------------------
// The three-IMember-subobject diamond: dispatch through IProperty* / IMember* (via
// SpecializedMember*) all reach the single override.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, DiamondDispatchThroughAllBases) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto prop = std::make_shared<TestBaseProperty>("x", returnType, nullptr, Compilation());
    auto sp = std::make_shared<SpecializedProperty>(
        prop, TypeParameterSubstitution(List({Int32()}), std::nullopt));

    // Through IProperty* (the full surface).
    IProperty* asProperty = sp.get();
    EXPECT_EQ(asProperty->Name(), "x");
    EXPECT_EQ(asProperty->SymbolKind(), SymbolKind::Property);
    EXPECT_EQ(asProperty->ReturnType().Name(), "Int32");

    // Through IMember* -- the SpecializedProperty* -> IMember* upcast is AMBIGUOUS (three
    // IMember subobjects), so upcast through the unambiguous SpecializedMember* (one IMember).
    IMember* asMember = static_cast<SpecializedMember*>(sp.get());
    EXPECT_EQ(asMember->Name(), "x");
    EXPECT_EQ(asMember->ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Specialize() delegates to the SpecializedMember base.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, SpecializeDelegates) {
    auto prop = std::make_shared<TestBaseProperty>("x", Int32(), nullptr, Compilation());
    SpecializedProperty sp(prop, TypeParameterSubstitution(List({String()}), std::nullopt));
    TypeParameterSubstitution s(List({Int32()}), std::nullopt);
    const IMember* baseResult = prop->Specialize(&s);
    const IMember* spResult = sp.Specialize(&s);
    EXPECT_EQ(spResult, baseResult);
}

// ---------------------------------------------------------------------------
// Equals / GetHashCode (inherited from SpecializedMember): same base + same substitution.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, EqualsAndHashInheritedFromSpecializedMember) {
    auto prop = std::make_shared<TestBaseProperty>("x", Int32(), nullptr, Compilation());
    auto str = String();
    SpecializedProperty sp1(prop, TypeParameterSubstitution(List({str}), std::nullopt));
    SpecializedProperty sp2(prop, TypeParameterSubstitution(List({str}), std::nullopt));
    EXPECT_TRUE(sp1.Equals(static_cast<const SpecializedMember*>(&sp2), nullptr));
    EXPECT_EQ(sp1.GetHashCode(), sp2.GetHashCode());
    // Different substitution => not equal / different hash.
    SpecializedProperty sp3(prop, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(sp1.Equals(static_cast<const SpecializedMember*>(&sp3), nullptr));
    EXPECT_NE(sp1.GetHashCode(), sp3.GetHashCode());
}

// ---------------------------------------------------------------------------
// The class shape: SpecializedProperty IS-A IProperty / IMember / IParameterizedMember /
// SpecializedParameterizedMember / SpecializedMember, and is final.
// ---------------------------------------------------------------------------
TEST(SpecializedPropertyTest, ClassShape) {
    static_assert(std::is_base_of_v<IProperty, SpecializedProperty>);
    static_assert(std::is_base_of_v<IParameterizedMember, SpecializedProperty>);
    static_assert(std::is_base_of_v<IMember, SpecializedProperty>);
    static_assert(std::is_base_of_v<SpecializedMember, SpecializedProperty>);
    static_assert(std::is_final_v<SpecializedProperty>);
}
