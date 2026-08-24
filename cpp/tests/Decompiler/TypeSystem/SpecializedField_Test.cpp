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

// Tests for `SpecializedField` (D483) -- the concrete `IField` a
// `GetMembersHelper.GetFieldsImpl` builds for a field on a parameterized type
// (`Implementation/SpecializedField.hpp`). It derives `SpecializedMember, IField`: the
// `SpecializedMember` base supplies the substituted `ReturnType` / `DeclaringType` /
// `Substitution` + the delegated `IMember` surface; this class adds the `IField`-own
// surface + the `IVariable` surface, delegating to the wrapped `fieldDefinition`.
//
// The tests pin:
//  (a) the ctor wires the substitution (`Substitution()` is the composed substitution, NOT
//      `Identity`);
//  (b) `Substitution()` / `MemberDefinition()` / `Specialize()` delegate to the
//      `SpecializedMember` base (the field IS a specialized member);
//  (c) the trivial `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
//      `ISymbol` delegations forward to the base member (Name / SymbolKind / IsVirtual /
//      MetadataToken / DeclaringTypeDefinition / Compilation / etc.);
//  (d) `ReturnType()` / `DeclaringType()` are the SUBSTITUTED values (a class type
//      parameter at index 0 -> the substitution's class type argument);
//  (e) `IVariable::Type()` returns the SAME substituted `IType` as `ReturnType()` (the C#
//      `IVariable.Type => this.ReturnType`);
//  (f) the `IField`-own surface delegates to the field definition (`IsReadOnly` /
//      `ReturnTypeIsRefReadOnly` / `IsVolatile`) and the `IVariable` surface (`IsConst` /
//      `GetConstantValue`);
//  (g) the two-`IMember`-subobject diamond: `SpecializedField` IS-A `IField` AND `IMember`,
//      dispatch through `IField*` / `IMember*` / `IVariable*` all reach the single override;
//  (h) `Equals` / `GetHashCode` (inherited from `SpecializedMember`).

#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

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
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedField;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMember;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern): holds the
// owner kind + index, dispatches `AcceptVisitor` to `visitor.VisitTypeParameter(*this)`.
// Used as the field's `ReturnType` so the `SpecializedField.ReturnType()` substitution
// effect is observable (a class type parameter at index 0 -> the class type argument).
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

// A configurable concrete `IField` -- the "field definition" the `SpecializedField` wraps.
// Implements the whole `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
// `ISymbol` / `IVariable` / `IField` surface (the `IField` shared-ISymbol-base diamond
// requires `Name` / `SymbolKind` overrides; the rest is single-inheritance). Holds the
// name, return type, declaring type, the IField-own flags, the IVariable flags + constant
// value. `Substitution()` returns `Identity`; `MemberDefinition()` returns `this`;
// `Specialize()` returns a non-owning pointer to a lazily-built owned copy; `Equals()` is
// identity.
class TestBaseField : public IField {
public:
    TestBaseField(std::string name, ITypePtr returnType, ITypePtr declaringType,
                  const ICompilation& compilation,
                  bool isReadOnly = false, bool isVolatile = false, bool isConst = false,
                  bool returnTypeIsRefReadOnly = false, std::any constantValue = std::any())
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation),
          isReadOnly_(isReadOnly), isVolatile_(isVolatile), isConst_(isConst),
          returnTypeIsRefReadOnly_(returnTypeIsRefReadOnly),
          constantValue_(std::move(constantValue)) {}

    // --- ISymbol (redeclared by IField for the shared-ISymbol-base diamond) ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
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
    const IMember* Specialize(const TypeParameterSubstitution* /*substitution*/) const override
    {
        if (!specialized_) {
            specialized_ = std::make_shared<TestBaseField>(*this);
        }
        return specialized_.get();
    }
    bool Equals(const IMember* obj, const TypeVisitor* /*typeNormalization*/) const override
    {
        return obj == this; // identity equality for the stub
    }

    // --- IVariable ---
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/) const override
    {
        return constantValue_;
    }

    // --- IField ---
    bool IsReadOnly() const override { return isReadOnly_; }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsVolatile() const override { return isVolatile_; }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    bool isReadOnly_;
    bool isVolatile_;
    bool isConst_;
    bool returnTypeIsRefReadOnly_;
    std::any constantValue_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
    mutable std::shared_ptr<IMember> specialized_;
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
// The ctor wires the substitution: Substitution() is the composed substitution, not Identity.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, CtorWiresSubstitution) {
    auto field = std::make_shared<TestBaseField>("x", Int32(), nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_FALSE(sf.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
    EXPECT_TRUE(sf.Substitution()->Equals(
        &TypeParameterSubstitution(List({String()}), std::nullopt)));
}

// ---------------------------------------------------------------------------
// MemberDefinition() delegates to the SpecializedMember base (the field returns its
// definition).
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, MemberDefinitionDelegates) {
    auto field = std::make_shared<TestBaseField>("x", Int32(), nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sf.MemberDefinition(), field.get());
}

// ---------------------------------------------------------------------------
// Name() / SymbolKind() delegate to the base member.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, NameAndSymbolKindDelegate) {
    auto field = std::make_shared<TestBaseField>("myField", Int32(), nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sf.Name(), "myField");
    EXPECT_EQ(sf.SymbolKind(), SymbolKind::Field);
}

// ---------------------------------------------------------------------------
// The trivial IEntity / INamedElement / ICompilationProvider delegations forward.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, TrivialEntitySurfaceDelegates) {
    auto field = std::make_shared<TestBaseField>("x", Int32(), nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sf.FullName(), "x");
    EXPECT_EQ(sf.ReflectionName(), "x");
    EXPECT_EQ(sf.Namespace(), "");
    EXPECT_EQ(&sf.Compilation(), &Compilation());
    EXPECT_EQ(sf.ParentModule(), nullptr);
    EXPECT_EQ(sf.MetadataToken(), 0u);
    EXPECT_EQ(sf.DeclaringTypeDefinition(), nullptr);
    EXPECT_FALSE(sf.IsStatic());
    EXPECT_FALSE(sf.IsAbstract());
    EXPECT_FALSE(sf.IsSealed());
    EXPECT_EQ(sf.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(sf.GetAttributes().empty());
    EXPECT_FALSE(sf.IsExplicitInterfaceImplementation());
    EXPECT_FALSE(sf.IsVirtual());
    EXPECT_FALSE(sf.IsOverride());
    EXPECT_FALSE(sf.IsOverridable());
}

// ---------------------------------------------------------------------------
// ReturnType() applies the substitution: a class type parameter at index 0 -> the class arg.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, ReturnTypeAppliesSubstitution) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto field = std::make_shared<TestBaseField>("x", returnType, nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_EQ(sf.ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// IVariable::Type() returns the SAME substituted IType as ReturnType().
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, VariableTypeEqualsReturnType) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto field = std::make_shared<TestBaseField>("x", returnType, nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({Int32()}), std::nullopt));
    // Both denote the field's (substituted) type; the C# `IVariable.Type => this.ReturnType`.
    EXPECT_EQ(&sf.Type(), &sf.ReturnType());
    EXPECT_EQ(sf.Type().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// DeclaringType() applies the substitution: a generic ITypeDefinition with matching class
// args -> new ParameterizedType(def, classArgs).
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, DeclaringTypeAppliesSubstitution) {
    auto def = GenericDef();
    auto field = std::make_shared<TestBaseField>("x", Int32(), def, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    auto dt = sf.DeclaringType();
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->GetDefinition(), def.get());
    auto pt = std::dynamic_pointer_cast<ParameterizedType>(dt);
    ASSERT_NE(pt, nullptr);
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), "String");
}

// ---------------------------------------------------------------------------
// The IField-own surface delegates to the field definition.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, IFieldSurfaceDelegates) {
    auto field = std::make_shared<TestBaseField>(
        "x", Int32(), nullptr, Compilation(),
        /*isReadOnly*/ true, /*isVolatile*/ true, /*isConst*/ false,
        /*returnTypeIsRefReadOnly*/ true);
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sf.IsReadOnly());
    EXPECT_TRUE(sf.IsVolatile());
    EXPECT_TRUE(sf.ReturnTypeIsRefReadOnly());
    // A non-readonly field forwards false.
    auto plainField = std::make_shared<TestBaseField>("y", Int32(), nullptr, Compilation());
    SpecializedField sf2(plainField,
                         TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_FALSE(sf2.IsReadOnly());
    EXPECT_FALSE(sf2.IsVolatile());
    EXPECT_FALSE(sf2.ReturnTypeIsRefReadOnly());
}

// ---------------------------------------------------------------------------
// The IVariable surface (IsConst / GetConstantValue) delegates to the field definition.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, IVariableSurfaceDelegates) {
    auto field = std::make_shared<TestBaseField>(
        "x", Int32(), nullptr, Compilation(),
        false, false, /*isConst*/ true, false, std::any(42));
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sf.IsConst());
    auto val = sf.GetConstantValue(false);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::any_cast<int>(val), 42);
}

// ---------------------------------------------------------------------------
// The two-IMember-subobject diamond: dispatch through IField* / IMember* / IVariable*
// all reach the single override.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, DiamondDispatchThroughAllBases) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto field = std::make_shared<TestBaseField>("x", returnType, nullptr, Compilation());
    auto sf = std::make_shared<SpecializedField>(
        field, TypeParameterSubstitution(List({Int32()}), std::nullopt));

    // Through IField* (the full surface).
    IField* asField = sf.get();
    EXPECT_EQ(asField->Name(), "x");
    EXPECT_EQ(asField->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(asField->ReturnType().Name(), "Int32");
    EXPECT_EQ(asField->Type().Name(), "Int32");

    // Through IMember* (the IMember surface -- resolves the two-IMember-subobject diamond;
    // dispatch reaches the single SpecializedField override). The `SpecializedField*` ->
    // `IMember*` upcast is AMBIGUOUS (two IMember subobjects: SpecializedMember's + IField's),
    // so upcast through the unambiguous `SpecializedMember*` (one IMember) -- the IField
    // header-comment diamond-upcast convention.
    IMember* asMember = static_cast<SpecializedMember*>(sf.get());
    EXPECT_EQ(asMember->Name(), "x");
    EXPECT_EQ(asMember->ReturnType().Name(), "Int32");

    // Through IVariable* (the IVariable surface).
    IVariable* asVariable = sf.get();
    EXPECT_EQ(asVariable->Type().Name(), "Int32");
    EXPECT_TRUE(asVariable->IsConst() == field->IsConst()); // both false here
}

// ---------------------------------------------------------------------------
// Specialize() delegates to the SpecializedMember base (the base member's Specialize).
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, SpecializeDelegates) {
    auto field = std::make_shared<TestBaseField>("x", Int32(), nullptr, Compilation());
    SpecializedField sf(field,
                        TypeParameterSubstitution(List({String()}), std::nullopt));
    TypeParameterSubstitution s(List({Int32()}), std::nullopt);
    const IMember* baseResult = field->Specialize(&s);
    const IMember* sfResult = sf.Specialize(&s);
    EXPECT_EQ(sfResult, baseResult);
}

// ---------------------------------------------------------------------------
// Equals / GetHashCode (inherited from SpecializedMember): same base + same substitution
// => equal / same hash.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, EqualsAndHashInheritedFromSpecializedMember) {
    auto field = std::make_shared<TestBaseField>("x", Int32(), nullptr, Compilation());
    auto str = String();
    SpecializedField sf1(field, TypeParameterSubstitution(List({str}), std::nullopt));
    SpecializedField sf2(field, TypeParameterSubstitution(List({str}), std::nullopt));
    EXPECT_TRUE(sf1.Equals(static_cast<const SpecializedMember*>(&sf2), nullptr));
    EXPECT_EQ(sf1.GetHashCode(), sf2.GetHashCode());
    // Different substitution => not equal / different hash.
    SpecializedField sf3(field, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(sf1.Equals(static_cast<const SpecializedMember*>(&sf3), nullptr));
    EXPECT_NE(sf1.GetHashCode(), sf3.GetHashCode());
}

// ---------------------------------------------------------------------------
// The class shape: SpecializedField IS-A IField / IMember / IVariable / SpecializedMember,
// and is final.
// ---------------------------------------------------------------------------
TEST(SpecializedFieldTest, ClassShape) {
    static_assert(std::is_base_of_v<IField, SpecializedField>);
    static_assert(std::is_base_of_v<IMember, SpecializedField>);
    static_assert(std::is_base_of_v<IVariable, SpecializedField>);
    static_assert(std::is_final_v<SpecializedField>);
}
