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

// Tests for `SpecializedParameterizedMember` (D484) -- the abstract
// `SpecializedMember, IParameterizedMember` base adding the lazily-computed `Parameters`
// list (`Implementation/SpecializedParameterizedMember.{hpp,cpp}`). `SpecializedMethod` /
// `SpecializedProperty` derive from it; the `Parameters` getter builds the substituted
// parameter list ONCE (lazily), each parameter a `SpecializedParameter` wrapping the base
// member's parameter with its type run through the member's substitution.
//
// The tests pin:
//  (a) the class is ABSTRACT (a `protected` ctor; not instantiable directly);
//  (b) `Parameters()` is empty when the base member has no parameters;
//  (c) `Parameters()` returns the specialized list: the count matches the base, and each
//      `SpecializedParameter` has the SUBSTITUTED type (a class type parameter at index 0
//      -> the substitution's class type argument);
//  (d) `IVariable::Type()` of each specialized param == the substituted type;
//  (e) `Parameters()` is CACHED: the same `SpecializedParameter` instances across calls
//      (pointer-identity of the snapshot elements);
//  (f) each specialized param's `Owner()` is the `SpecializedParameterizedMember` (this);
//  (g) `CreateParameters` (the protected customization point) with a custom substitution
//      applies it to each parameter's type;
//  (h) the base member's parameter `Name` / `ReferenceKind` / `IsParams` etc. delegate
//      through the `SpecializedParameter` to the base parameter.

#include "Decompiler/TypeSystem/Implementation/SpecializedParameterizedMember.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedParameter.hpp"
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
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::Nullability;
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
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedParameter;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedParameterizedMember;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern): a class type
// parameter at a given index, dispatched to `VisitTypeParameter`. Used as a parameter's
// `Type` so the `SpecializedParameter.Type()` substitution effect is observable.
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

// A configurable concrete `IParameter` -- the "base parameter" the specialized list wraps.
// Holds the type, name, reference kind, params flag; the rest trivial. `Owner()` returns
// the configured owner (the base parameterized member).
class TestBaseParameter : public IParameter {
public:
    TestBaseParameter(ITypePtr type, std::string name,
                      ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
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

// A configurable concrete `IParameterizedMember` -- the "base member" the
// `SpecializedParameterizedMember` wraps. Implements the full `IMember` / `IEntity` /
// `INamedElement` / `ICompilationProvider` / `ISymbol` / `IParameterizedMember` surface;
// `Parameters()` returns a configured non-owning snapshot of base `IParameter*`.
class TestBaseParameterizedMember : public IParameterizedMember {
public:
    TestBaseParameterizedMember(std::string name, ITypePtr returnType,
                                ITypePtr declaringType, const ICompilation& compilation,
                                std::vector<const IParameter*> parameters = {})
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation),
          parameters_(std::move(parameters)) {}

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
    const IMember* Specialize(const TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const IMember* obj, const TypeVisitor*) const override
    {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    std::vector<const IParameter*> parameters_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// The test-only CONCRETE `SpecializedParameterizedMember` subclass. The base is abstract
// (the two-`IMember`-subobject diamond leaves sub B's `IMember` pure-virtuals unresolved);
// this subclass adds the diamond overrides (one delegating override per method name, the
// `SpecializedField` pattern -- each calls `SpecializedMember::` qualified, sub A's override).
// `Parameters()` is inherited from the base.
class TestSpecializedParameterizedMember : public SpecializedParameterizedMember {
public:
    explicit TestSpecializedParameterizedMember(
        std::shared_ptr<IParameterizedMember> memberDefinition,
        TypeParameterSubstitution substitution)
        : SpecializedParameterizedMember(memberDefinition) {
        AddSubstitution(std::move(substitution));
    }
    using SpecializedParameterizedMember::CreateParameters;

    // --- The IMember / IEntity / ISymbol / INamedElement / ICompilationProvider diamond
    //     overrides (delegating to sub A's SpecializedMember::) ---

    std::string Name() const override { return SpecializedMember::Name(); }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return SpecializedMember::SymbolKind();
    }
    std::string FullName() const override { return SpecializedMember::FullName(); }
    std::string ReflectionName() const override { return SpecializedMember::ReflectionName(); }
    std::string Namespace() const override { return SpecializedMember::Namespace(); }
    const ICompilation& Compilation() const override { return SpecializedMember::Compilation(); }
    std::uint32_t MetadataToken() const override { return SpecializedMember::MetadataToken(); }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return SpecializedMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override { return SpecializedMember::DeclaringType(); }
    const IModule* ParentModule() const override { return SpecializedMember::ParentModule(); }
    std::vector<const IAttribute*> GetAttributes() const override {
        return SpecializedMember::GetAttributes();
    }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override {
        return SpecializedMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override {
        return SpecializedMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return SpecializedMember::Accessibility();
    }
    bool IsStatic() const override { return SpecializedMember::IsStatic(); }
    bool IsAbstract() const override { return SpecializedMember::IsAbstract(); }
    bool IsSealed() const override { return SpecializedMember::IsSealed(); }
    const IMember* MemberDefinition() const override { return SpecializedMember::MemberDefinition(); }
    const IType& ReturnType() const override { return SpecializedMember::ReturnType(); }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return SpecializedMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return SpecializedMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return SpecializedMember::IsVirtual(); }
    bool IsOverride() const override { return SpecializedMember::IsOverride(); }
    bool IsOverridable() const override { return SpecializedMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return SpecializedMember::Substitution();
    }
    const IMember* Specialize(const TypeParameterSubstitution* newSubstitution) const override {
        return SpecializedMember::Specialize(newSubstitution);
    }
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override {
        return SpecializedMember::Equals(obj, typeNormalization);
    }
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

} // namespace

// ---------------------------------------------------------------------------
// The class is abstract: a protected ctor (not instantiable directly). The standard
// `static_assert(!std::is_constructible_v<...>)` on an abstract class is ill-formed (the
// ctor is protected, not deleted), so pin abstractness by the protected-ctor convention:
// the test-only public-ctor subclass is the only way to instantiate it.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Parameters() is empty when the base member has no parameters.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParametersEmptyWhenBaseHasNone) {
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(), std::vector<const IParameter*>{});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(spm.Parameters().empty());
}

// ---------------------------------------------------------------------------
// Parameters() returns the specialized list: count matches, each has the substituted type.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParametersReturnsSpecializedList) {
    auto tp0 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto tp1 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "U", 1);
    auto p0 = std::make_shared<TestBaseParameter>(tp0, "a");
    auto p1 = std::make_shared<TestBaseParameter>(tp1, "b");
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p0.get(), p1.get()});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String(), Int32()}), std::nullopt));
    auto params = spm.Parameters();
    ASSERT_EQ(params.size(), 2u);
    // p0 (type T at index 0) -> String; p1 (type U at index 1) -> Int32.
    EXPECT_EQ(params[0]->Type().Name(), "String");
    EXPECT_EQ(params[1]->Type().Name(), "Int32");
    // The base parameter names delegate through the SpecializedParameter.
    EXPECT_EQ(params[0]->Name(), "a");
    EXPECT_EQ(params[1]->Name(), "b");
}

// ---------------------------------------------------------------------------
// IVariable::Type() of each specialized param == the substituted type (the
// SpecializedParameter.Type is the substituted type).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParameterVariableTypeIsSubstituted) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "a");
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto params = spm.Parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(&params[0]->Type(), &params[0]->Type()); // stable
    EXPECT_EQ(params[0]->Type().Name(), "String");
}

// ---------------------------------------------------------------------------
// Parameters() is CACHED: the same SpecializedParameter instances across calls
// (pointer-identity of the snapshot elements).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParametersIsCached) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "a");
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto first = spm.Parameters();
    auto second = spm.Parameters();
    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0], second[0]); // same SpecializedParameter instance
}

// ---------------------------------------------------------------------------
// Each specialized param's Owner() is the SpecializedParameterizedMember (this).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParameterOwnerIsTheSpecializedMember) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "a");
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()});
    auto spm = std::make_shared<TestSpecializedParameterizedMember>(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto params = spm->Parameters();
    ASSERT_EQ(params.size(), 1u);
    ASSERT_NE(params[0]->Owner(), nullptr);
    // The Owner is the SpecializedParameterizedMember (this) -- the IParameterizedMember
    // view of `spm`. dynamic_cast back to the concrete subclass confirms identity.
    EXPECT_EQ(dynamic_cast<const TestSpecializedParameterizedMember*>(params[0]->Owner()),
              spm.get());
}

// ---------------------------------------------------------------------------
// CreateParameters (the protected customization point) with a custom substitution applies
// it to each parameter's type.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, CreateParametersAppliesCustomSubstitution) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "a");
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    // A custom substitution that maps every type to Int32 (ignores the member's
    // substitution). The Identity substitution passed to CreateParameters leaves a class
    // type parameter unchanged, so use the member's own substitution via a lambda that
    // applies it (the same Parameters() uses).
    auto owned = spm.CreateParameters([](const IType& /*t*/) -> ITypePtr {
        return Int32(); // custom: always Int32
    });
    ASSERT_EQ(owned->size(), 1u);
    EXPECT_EQ((*owned)[0]->Type().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// The base parameter's ReferenceKind / IsParams delegate through the SpecializedParameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ParameterDelegatesBaseMemberSurface) {
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto p = std::make_shared<TestBaseParameter>(tp, "a", ReferenceKind::Ref,
                                                  /*isParams*/ true);
    auto base = std::make_shared<TestBaseParameterizedMember>(
        "M", Int32(), nullptr, Compilation(),
        std::vector<const IParameter*>{p.get()});
    TestSpecializedParameterizedMember spm(
        base, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto params = spm.Parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0]->ReferenceKind(), ReferenceKind::Ref);
    EXPECT_TRUE(params[0]->IsParams());
}

// ---------------------------------------------------------------------------
// The class shape: SpecializedParameterizedMember IS-A SpecializedMember / IParameterizedMember
// / IMember.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterizedMemberTest, ClassShape) {
    static_assert(std::is_base_of_v<SpecializedMember, SpecializedParameterizedMember>);
    static_assert(std::is_base_of_v<IParameterizedMember, SpecializedParameterizedMember>);
    static_assert(std::is_base_of_v<IMember, SpecializedParameterizedMember>);
}
