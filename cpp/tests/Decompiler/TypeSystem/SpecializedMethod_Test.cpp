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

// Tests for `SpecializedMethod` (D488) -- the concrete `IMethod` a `GetMembersHelper.
// GetMethodsImpl` builds for a method on a parameterized type
// (`Implementation/SpecializedMethod.{hpp,cpp}`). It derives `SpecializedParameterizedMember,
// IMethod`: the base supplies the substituted `Parameters` / `ReturnType` / `DeclaringType` /
// `Substitution`; this class adds the `IMethod`-own surface + the method-type-parameter
// specialization machinery (a per-base-type-parameter `SpecializedTypeParameter` array +
// `substitutionWithoutSpecializedTypeParameters`).
//
// The tests pin:
//  (a) the ctor wires the substitution (`Substitution()` is the composed substitution, NOT
//      `Identity`);
//  (b) `Name` / `SymbolKind` / `MemberDefinition` delegate;
//  (c) the trivial `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` delegations;
//  (d) `ReturnType` / `DeclaringType` are the SUBSTITUTED values;
//  (e) `Parameters` (inherited) are substituted;
//  (f) the `IMethod`-own bools (`IsExtensionMethod` / `IsConstructor` / ...) delegate to the
//      method definition;
//  (g) `TypeParameters` for a generic base method returns the SPECIALIZED type parameters
//      (count matches; each is a `SpecializedTypeParameter` whose `Owner` is the
//      `SpecializedMethod`); `TypeArguments` returns the substitution's method type args;
//  (h) the `SpecializedTypeParameter`'s `TypeConstraints` are substituted (a base type param
//      with a constraint T -> the substituted type);
//  (i) `ReducedFrom` -> null; `AccessorOwner` deferred (returns base);
//  (j) `Specialize` (covariant `IMethod*`) delegates;
//  (k) `Equals` / `GetHashCode` (same base + same substitution => equal / same hash;
//      different substitution => not);
//  (l) the three-`IMember`-subobject diamond: dispatch through `IMethod*` / `IMember*` (via
//      `SpecializedMember*`) all reach the single override;
//  (m) the class shape.

#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"

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
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::Implementation::AbstractTypeParameter;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMember;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMethod;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedTypeParameter;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern) -- a class type
// parameter at a given index, dispatched to `VisitTypeParameter`.
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

// A `TestTypeParameter` for the base method's OWN type parameters -- a direct `ITypeParameter`
// stub with a configurable `TypeConstraints` (so the `SpecializedTypeParameter.TypeConstraints`
// substitution is observable). The constraint type is a class type parameter T at index 0.
class TestBaseMethodTypeParameter : public ITypeParameter {
public:
    explicit TestBaseMethodTypeParameter(std::string name, int index,
                                          std::vector<TypeConstraint> constraints = {})
        : name_(std::move(name)), index_(index), constraints_(std::move(constraints)) {}

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
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
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
    std::vector<TypeConstraint> TypeConstraints() const override { return constraints_; }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::string name_;
    int index_;
    std::vector<TypeConstraint> constraints_;
};

// A configurable concrete `IParameter` (the `TestBaseParameter` pattern).
class TestBaseParameter : public IParameter {
public:
    TestBaseParameter(ITypePtr type, std::string name,
                      ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind =
                          ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                      const IParameterizedMember* owner = nullptr)
        : type_(std::move(type)), name_(std::move(name)),
          referenceKind_(referenceKind), owner_(owner) {}

    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return referenceKind_;
    }
    LifetimeAnnotation Lifetime() const override { return LifetimeAnnotation{}; }
    bool IsParams() const override { return false; }
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
    const IParameterizedMember* owner_;
};

// A configurable concrete `IMethod` -- the "method definition" the `SpecializedMethod` wraps.
// Implements the full `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
// `ISymbol` / `IParameterizedMember` / `IMethod` surface. Holds the name, return type, declaring
// type, the base type parameters, the parameters, and the IMethod-own flags.
class TestBaseMethod : public IMethod {
public:
    TestBaseMethod(std::string name, ITypePtr returnType, ITypePtr declaringType,
                   const ICompilation& compilation,
                   std::vector<const ITypeParameter*> typeParameters = {},
                   std::vector<const IParameter*> parameters = {},
                   bool isExtensionMethod = false, bool isConstructor = false,
                   bool isOperator = false, bool hasBody = true, bool isAccessor = false,
                   const IMember* accessorOwner = nullptr,
                   MethodSemanticsAttributes accessorKind = MethodSemanticsAttributes::None)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation),
          typeParameters_(std::move(typeParameters)), parameters_(std::move(parameters)),
          isExtensionMethod_(isExtensionMethod), isConstructor_(isConstructor),
          isOperator_(isOperator), hasBody_(hasBody), isAccessor_(isAccessor),
          accessorOwner_(accessorOwner), accessorKind_(accessorKind) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
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
    const IMethod* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IMethod-own ---
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override { return {}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return isExtensionMethod_; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return isConstructor_; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return isOperator_; }
    bool HasBody() const override { return hasBody_; }
    bool IsAccessor() const override { return isAccessor_; }
    const IMember* AccessorOwner() const override { return accessorOwner_; }
    MethodSemanticsAttributes AccessorKind() const override { return accessorKind_; }
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    std::vector<const ITypeParameter*> typeParameters_;
    std::vector<const IParameter*> parameters_;
    bool isExtensionMethod_;
    bool isConstructor_;
    bool isOperator_;
    bool hasBody_;
    bool isAccessor_;
    const IMember* accessorOwner_;
    MethodSemanticsAttributes accessorKind_;
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

// A non-generic base method (no type parameters) -- the common case.
std::shared_ptr<TestBaseMethod> NonGenericMethod() {
    return std::make_shared<TestBaseMethod>("M", Int32(), nullptr, Compilation());
}

// A generic base method with ONE type parameter T (whose TypeConstraints include a constraint
// of type T -- a self-referential constraint that exercises the SpecializedTypeParameter
// substitution: the constraint T is substituted to the class type argument).
std::shared_ptr<TestBaseMethod> GenericMethod(std::shared_ptr<TestBaseMethodTypeParameter> tp) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    return std::make_shared<TestBaseMethod>(
        "M", returnType, nullptr, Compilation(),
        std::vector<const ITypeParameter*>{tp.get()});
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor wires the substitution (non-Identity).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, CtorWiresSubstitution) {
    auto m = NonGenericMethod();
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_FALSE(sm.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
}

// ---------------------------------------------------------------------------
// Name / SymbolKind / MemberDefinition delegate.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, NameSymbolKindMemberDefinitionDelegate) {
    auto m = NonGenericMethod();
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sm.Name(), "M");
    EXPECT_EQ(sm.SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(sm.MemberDefinition(), m.get());
}

// ---------------------------------------------------------------------------
// The trivial IEntity / INamedElement / ICompilationProvider delegations forward.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, TrivialEntitySurfaceDelegates) {
    auto m = NonGenericMethod();
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sm.FullName(), "M");
    EXPECT_EQ(sm.ReflectionName(), "M");
    EXPECT_EQ(sm.Namespace(), "");
    EXPECT_EQ(&sm.Compilation(), &Compilation());
    EXPECT_EQ(sm.ParentModule(), nullptr);
    EXPECT_EQ(sm.MetadataToken(), 0u);
    EXPECT_EQ(sm.DeclaringTypeDefinition(), nullptr);
    EXPECT_FALSE(sm.IsStatic());
    EXPECT_FALSE(sm.IsAbstract());
    EXPECT_FALSE(sm.IsSealed());
    EXPECT_EQ(sm.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(sm.GetAttributes().empty());
    EXPECT_FALSE(sm.IsExplicitInterfaceImplementation());
    EXPECT_FALSE(sm.IsVirtual());
    EXPECT_FALSE(sm.IsOverride());
    EXPECT_FALSE(sm.IsOverridable());
}

// ---------------------------------------------------------------------------
// ReturnType applies the substitution (class type param T at index 0 -> the class arg).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, ReturnTypeAppliesSubstitution) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto m = std::make_shared<TestBaseMethod>("M", returnType, nullptr, Compilation());
    SpecializedMethod sm(m, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_EQ(sm.ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// DeclaringType applies the substitution.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, DeclaringTypeAppliesSubstitution) {
    auto def = GenericDef();
    auto m = std::make_shared<TestBaseMethod>("M", Int32(), def, Compilation());
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto dt = sm.DeclaringType();
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->GetDefinition(), def.get());
    auto pt = std::dynamic_pointer_cast<ParameterizedType>(dt);
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), "String");
}

// ---------------------------------------------------------------------------
// Parameters (inherited from SpecializedParameterizedMember) are substituted.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, ParametersAreSubstituted) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "x");
    auto m = std::make_shared<TestBaseMethod>(
        "M", Int32(), nullptr, Compilation(), std::vector<const ITypeParameter*>{},
        std::vector<const IParameter*>{p.get()});
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto params = sm.Parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0]->Type().Name(), "String");
}

// ---------------------------------------------------------------------------
// The IMethod-own bools delegate to the method definition.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, IMethodBoolsDelegate) {
    auto m = std::make_shared<TestBaseMethod>(
        "op_Add", Int32(), nullptr, Compilation(),
        std::vector<const ITypeParameter*>{}, std::vector<const IParameter*>{},
        /*isExtensionMethod*/ false, /*isConstructor*/ false, /*isOperator*/ true,
        /*hasBody*/ true);
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm.IsOperator());
    EXPECT_TRUE(sm.HasBody());
    EXPECT_FALSE(sm.IsExtensionMethod());
    EXPECT_FALSE(sm.IsConstructor());
    EXPECT_FALSE(sm.IsLocalFunction());
    EXPECT_FALSE(sm.IsDestructor());
    EXPECT_FALSE(sm.IsInitOnly());
    EXPECT_FALSE(sm.ReturnTypeIsRefReadOnly());
    EXPECT_FALSE(sm.ThisIsRefReadOnly());
    EXPECT_TRUE(sm.GetReturnTypeAttributes().empty());
    EXPECT_EQ(sm.AccessorKind(), MethodSemanticsAttributes::None);
}

// ---------------------------------------------------------------------------
// TypeParameters for a generic base method returns the SPECIALIZED type parameters (count
// matches; each is a SpecializedTypeParameter whose Owner is the SpecializedMethod).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, TypeParametersAreSpecializedForGenericMethod) {
    auto baseTp = std::make_shared<TestBaseMethodTypeParameter>("T", 0);
    auto m = GenericMethod(baseTp);
    auto sm = std::make_shared<SpecializedMethod>(
        m, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto tps = sm->TypeParameters();
    ASSERT_EQ(tps.size(), 1u);
    // The specialized type parameter is a SpecializedTypeParameter whose Owner is the
    // SpecializedMethod (not the base method).
    EXPECT_NE(dynamic_cast<const SpecializedTypeParameter*>(tps[0]), nullptr);
    EXPECT_EQ(tps[0]->Owner(), static_cast<const IMethod*>(sm.get()));
    // The count matches the base method's type-parameter count.
    EXPECT_EQ(sm->TypeParameters().size(), m->TypeParameters().size());
}

// ---------------------------------------------------------------------------
// TypeParameters for a non-generic base method returns the base method's (empty).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, TypeParametersEmptyForNonGenericMethod) {
    auto m = NonGenericMethod();
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm.TypeParameters().empty());
}

// ---------------------------------------------------------------------------
// TypeArguments returns the substitution's method type args (or empty).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, TypeArgumentsFromSubstitution) {
    auto m = NonGenericMethod();
    // No method type args -> empty.
    SpecializedMethod sm1(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm1.TypeArguments().empty());
    // With method type args -> the args.
    SpecializedMethod sm2(m, TypeParameterSubstitution(std::nullopt, List({Int32(), String()})));
    auto ta = sm2.TypeArguments();
    ASSERT_EQ(ta.size(), 2u);
    EXPECT_EQ(ta[0]->Name(), "Int32");
    EXPECT_EQ(ta[1]->Name(), "String");
}

// ---------------------------------------------------------------------------
// The SpecializedTypeParameter's TypeConstraints are substituted: a base type param with a
// constraint of type T (a class type parameter at index 0) -> the substituted type (String).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, SpecializedTypeParameterConstraintsSubstituted) {
    // The base type parameter T has a TypeConstraint whose Type is a class type parameter at
    // index 0 (so the SpecializedTypeParameter.TypeConstraints substitutes it to the class arg).
    auto constraintType = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    std::vector<TypeConstraint> constraints;
    constraints.emplace_back(constraintType, std::vector<const IAttribute*>{});
    auto baseTp = std::make_shared<TestBaseMethodTypeParameter>("T", 0, std::move(constraints));
    auto m = GenericMethod(baseTp);
    auto sm = std::make_shared<SpecializedMethod>(
        m, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto tps = sm->TypeParameters();
    ASSERT_EQ(tps.size(), 1u);
    auto stpConstraints = tps[0]->TypeConstraints();
    ASSERT_EQ(stpConstraints.size(), 1u);
    // The constraint type (T at index 0) is substituted to String (the class type argument).
    EXPECT_EQ(stpConstraints[0].Type()->Name(), "String");
}

// ---------------------------------------------------------------------------
// ReducedFrom -> null; AccessorOwner deferred (returns the base method's AccessorOwner).
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, ReducedFromAndAccessorOwner) {
    auto accessorOwner = reinterpret_cast<const IMember*>(0x100);
    auto m = std::make_shared<TestBaseMethod>(
        "get_X", Int32(), nullptr, Compilation(),
        std::vector<const ITypeParameter*>{}, std::vector<const IParameter*>{},
        false, false, false, true, /*isAccessor*/ true, accessorOwner);
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(sm.ReducedFrom(), nullptr);
    EXPECT_EQ(sm.AccessorOwner(), accessorOwner);  // deferred -- returns the base's
}

// ---------------------------------------------------------------------------
// Specialize (covariant IMethod*) delegates to the base method's Specialize.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, SpecializeDelegates) {
    auto m = NonGenericMethod();
    SpecializedMethod sm(m, TypeParameterSubstitution(List({String()}), std::nullopt));
    TypeParameterSubstitution s(List({Int32()}), std::nullopt);
    const IMethod* baseResult = m->Specialize(&s);
    const IMethod* smResult = sm.Specialize(&s);
    EXPECT_EQ(smResult, baseResult);
}

// ---------------------------------------------------------------------------
// Equals / GetHashCode: same base + same substitution => equal / same hash; different => not.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, EqualsAndHash) {
    auto m = NonGenericMethod();
    auto str = String();
    SpecializedMethod sm1(m, TypeParameterSubstitution(List({str}), std::nullopt));
    SpecializedMethod sm2(m, TypeParameterSubstitution(List({str}), std::nullopt));
    EXPECT_TRUE(sm1.Equals(static_cast<const SpecializedMember*>(&sm2), nullptr));
    EXPECT_EQ(sm1.GetHashCode(), sm2.GetHashCode());
    // Different substitution => not equal / different hash.
    SpecializedMethod sm3(m, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(sm1.Equals(static_cast<const SpecializedMember*>(&sm3), nullptr));
    EXPECT_NE(sm1.GetHashCode(), sm3.GetHashCode());
}

// ---------------------------------------------------------------------------
// The three-IMember-subobject diamond: dispatch through IMethod* / IMember* (via
// SpecializedMember*) all reach the single override.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, DiamondDispatchThroughAllBases) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto m = std::make_shared<TestBaseMethod>("M", returnType, nullptr, Compilation());
    auto sm = std::make_shared<SpecializedMethod>(
        m, TypeParameterSubstitution(List({Int32()}), std::nullopt));

    // Through IMethod* (the full surface).
    IMethod* asMethod = sm.get();
    EXPECT_EQ(asMethod->Name(), "M");
    EXPECT_EQ(asMethod->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(asMethod->ReturnType().Name(), "Int32");

    // Through IMember* -- the SpecializedMethod* -> IMember* upcast is AMBIGUOUS (three IMember
    // subobjects), so upcast through the unambiguous SpecializedMember* (one IMember).
    IMember* asMember = static_cast<SpecializedMember*>(sm.get());
    EXPECT_EQ(asMember->Name(), "M");
    EXPECT_EQ(asMember->ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// The class shape.
// ---------------------------------------------------------------------------
TEST(SpecializedMethodTest, ClassShape) {
    static_assert(std::is_base_of_v<IMethod, SpecializedMethod>);
    static_assert(std::is_base_of_v<IParameterizedMember, SpecializedMethod>);
    static_assert(std::is_base_of_v<IMember, SpecializedMethod>);
    static_assert(std::is_base_of_v<SpecializedMember, SpecializedMethod>);
    // NOT final: the C# `SpecializedMethod` is unsealed, and the C# `sealed class
    // LiftedUserDefinedOperator : SpecializedMethod, ILiftedOperator` (CSharpOperators.cs line
    // 1129) derives from it -- the port's `LiftedUserDefinedOperator` (CSharpOperators.hpp)
    // subclasses it the same way.
    static_assert(!std::is_final_v<SpecializedMethod>);
    static_assert(std::is_base_of_v<AbstractTypeParameter, SpecializedTypeParameter>);
    static_assert(std::is_final_v<SpecializedTypeParameter>);
}
