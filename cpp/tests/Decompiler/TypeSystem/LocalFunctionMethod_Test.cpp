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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `LocalFunctionMethod` -- the port of the C#
// `TypeSystem/Implementation/LocalFunctionMethod.cs` wrapper the decompiler puts around
// the metadata method when it detects a local function
// (`cpp/Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp`). It derives
// `IMethod` directly and forwards nearly the whole surface to the base method; the
// load-bearing divergences from a plain forwarder are:
//  (a) `IsLocalFunction` is unconditionally TRUE (the only ported implementation that
//      reports true);
//  (b) `IsStatic` is unconditionally TRUE -- "local functions do not have a 'this
//      parameter'; even local functions in instance methods capture this" -- EVEN when
//      the base method is an instance method and even when the wrapper was constructed
//      with `isStaticLocalFunction == false`;
//  (c) `ReducedFrom` returns THE BASE METHOD ITSELF (not whatever the base is itself
//      reduced from);
//  (d) `Parameters` / `TypeParameters` / `TypeArguments` hide the compiler-generated
//      tails (`SkipLast` semantics: a count at or above the size yields empty);
//  (e) `Name` / `FullName` are the wrapper's OWN name (the local function's source
//      name; `ReflectionName` / `Namespace` forward);
//  (f) `MemberDefinition` is `this` when the base is its own definition, else a fresh
//      wrapper over the base's definition; a degenerate null definition passes through
//      (the documented safe fallback for the C# hard-cast throw);
//  (g) `Specialize` builds a FRESH wrapper per call over the specialized base (kept
//      alive through the source wrapper; the results are `Equals`-equal);
//  (h) `Equals` compares the base (under the passed normalization) AND the two
//      generated counts AND the `IsStaticLocalFunction` flag;
//  (i) the `internal` accessors (`IsStaticLocalFunction` /
//      `NumberOfCompilerGeneratedParameters` / `NumberOfCompilerGeneratedTypeParameters`)
//      are widened to public for direct TDD.
//
// The base-method stub is the shared `LookupMethod` (already configurable for
// `Parameters` / `TypeParameters` / `TypeArguments` / `IsStatic` / `Accessibility` /
// `DeclaringTypeDefinition` / ...) extended by the local `ConfigurableMethod`, which
// adds configurable `MemberDefinition` / `ReducedFrom` / `Substitution` /
// `AccessorOwner` / `AccessorKind` slots, the `LookupMethod`-hardcoded bools, and
// RECORDING `Specialize` / `Equals` overrides (the substitution / normalization
// pointer pass-throughs).

#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ICompilationProvider.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::ICompilationProvider;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::INamedElement;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::ISymbol;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
using ILSpy::Decompiler::TypeSystem::Implementation::LocalFunctionMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The shared test compilation (the `LookupMethod` ctor needs an `ICompilation&`).
LookupCompilation& Compilation()
{
    static LookupCompilation c;
    return c;
}

// A configurable base method for the wrapper tests: derives the shared `LookupMethod`
// stub (already configurable for `IsStatic` / `IsOperator` / `IsExtensionMethod` /
// `ReturnTypeIsRefReadOnly` / `IsOverridable` / `Accessibility` /
// `DeclaringTypeDefinition` / `Parameters` / `TypeParameters` / `TypeArguments`) and
// adds the pieces `LookupMethod` hardcodes: a configurable `MemberDefinition` (the
// default `this` models the base-is-own-definition shape), a `ReducedFrom`, a
// `Substitution` slot, the `LookupMethod`-false bools, and RECORDING `Specialize` /
// `Equals` overrides (the substitution / normalization pointer pass-throughs).
class ConfigurableMethod : public LookupMethod {
public:
    ConfigurableMethod(std::string name, const ICompilation& compilation)
        : LookupMethod(std::move(name), compilation), memberDefinition_(this) {}

    void SetMemberDefinition(const IMethod* d) { memberDefinition_ = d; }
    void SetReducedFrom(const IMethod* r) { reducedFrom_ = r; }
    void SetSubstitution(const TypeParameterSubstitution* s) { substitution_ = s; }
    void SetAbstract(bool v) { isAbstract_ = v; }
    void SetSealed(bool v) { isSealed_ = v; }
    void SetVirtual(bool v) { isVirtual_ = v; }
    void SetOverride(bool v) { isOverride_ = v; }
    void SetExplicitInterfaceImplementation(bool v) { isExplicitInterfaceImplementation_ = v; }
    void SetInitOnly(bool v) { isInitOnly_ = v; }
    void SetThisIsRefReadOnly(bool v) { thisIsRefReadOnly_ = v; }
    void SetConstructor(bool v) { isConstructor_ = v; }
    void SetDestructor(bool v) { isDestructor_ = v; }
    void SetHasBody(bool v) { hasBody_ = v; }
    void SetAccessor(bool v) { isAccessor_ = v; }
    void SetAccessorOwner(const IMember* o) { accessorOwner_ = o; }
    void SetAccessorKind(MethodSemanticsAttributes k) { accessorKind_ = k; }

    const TypeParameterSubstitution* LastSpecializeSubstitution() const
    {
        return lastSpecializeSubstitution_;
    }

    const TypeVisitor* LastEqualsNormalization() const { return lastEqualsNormalization_; }

    // The configurable `MemberDefinition` (the default `this` models an unspecialized
    // base method, which IS its own definition).
    const IMember* MemberDefinition() const override { return memberDefinition_; }

    // A configurable `ReducedFrom` distinct from the stub itself (the
    // wrapper-ReducedFrom-is-the-base crux needs the base's OWN value to be non-null).
    const IMethod* ReducedFrom() const override { return reducedFrom_; }

    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsExplicitInterfaceImplementation() const override
    {
        return isExplicitInterfaceImplementation_;
    }

    const TypeParameterSubstitution* Substitution() const override { return substitution_; }

    bool IsInitOnly() const override { return isInitOnly_; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    bool IsConstructor() const override { return isConstructor_; }
    bool IsDestructor() const override { return isDestructor_; }
    bool HasBody() const override { return hasBody_; }
    bool IsAccessor() const override { return isAccessor_; }
    const IMember* AccessorOwner() const override { return accessorOwner_; }
    MethodSemanticsAttributes AccessorKind() const override { return accessorKind_; }

    // Records the received substitution pointer, then returns `this` (the
    // `LookupMethod::Specialize` behavior, made observable).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override
    {
        lastSpecializeSubstitution_ = substitution;
        return this;
    }

    // Records the received normalization pointer, then the identity comparison (the
    // `LookupMethod::Equals` behavior, made observable).
    bool Equals(const IMember* obj, const TypeVisitor* normalization) const override
    {
        lastEqualsNormalization_ = normalization;
        return obj == this;
    }

private:
    const IMethod* memberDefinition_;
    const IMethod* reducedFrom_ = nullptr;
    const TypeParameterSubstitution* substitution_ = nullptr;
    const IMember* accessorOwner_ = nullptr;
    MethodSemanticsAttributes accessorKind_ = MethodSemanticsAttributes::None;
    bool isAbstract_ = false;
    bool isSealed_ = false;
    bool isVirtual_ = false;
    bool isOverride_ = false;
    bool isExplicitInterfaceImplementation_ = false;
    bool isInitOnly_ = false;
    bool thisIsRefReadOnly_ = false;
    bool isConstructor_ = false;
    bool isDestructor_ = false;
    bool hasBody_ = false;
    bool isAccessor_ = false;
    mutable const TypeParameterSubstitution* lastSpecializeSubstitution_ = nullptr;
    mutable const TypeVisitor* lastEqualsNormalization_ = nullptr;
};

std::shared_ptr<ConfigurableMethod> MakeBase(std::string name = "M")
{
    return std::make_shared<ConfigurableMethod>(std::move(name), Compilation());
}

std::shared_ptr<LocalFunctionMethod> MakeWrapper(std::shared_ptr<IMethod> base,
                                                 std::string name = "<M>g__Local|0_1",
                                                 bool isStaticLocalFunction = false,
                                                 int generatedParameters = 0,
                                                 int generatedTypeParameters = 0)
{
    return std::make_shared<LocalFunctionMethod>(std::move(base), std::move(name),
        isStaticLocalFunction, generatedParameters, generatedTypeParameters);
}

std::shared_ptr<LookupTypeDefinition> MakeHost()
{
    return std::make_shared<LookupTypeDefinition>("Host", "",
        FullTypeName(TopLevelTypeName("", "Host", 0)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

TEST(LocalFunctionMethodTest, CtorStoresTheOwnSurface)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", /*isStaticLocalFunction*/ true,
        /*generatedParameters*/ 3, /*generatedTypeParameters*/ 2);

    EXPECT_EQ(wrapper->Name(), "<M>g__F|0_1");
    EXPECT_EQ(wrapper->FullName(), "<M>g__F|0_1");
    EXPECT_TRUE(wrapper->IsStaticLocalFunction());
    EXPECT_EQ(wrapper->NumberOfCompilerGeneratedParameters(), 3);
    EXPECT_EQ(wrapper->NumberOfCompilerGeneratedTypeParameters(), 2);

    wrapper->SetName("<M>g__G|0_2");
    EXPECT_EQ(wrapper->Name(), "<M>g__G|0_2");
    EXPECT_EQ(wrapper->FullName(), "<M>g__G|0_2");
}

TEST(LocalFunctionMethodTest, CtorThrowsOnNullBaseMethod)
{
    auto makeNullBaseWrapper = []() {
        std::shared_ptr<IMethod> nullBase;
        return std::make_shared<LocalFunctionMethod>(nullBase, "n", false, 0, 0);
    };
    EXPECT_THROW(makeNullBaseWrapper(), std::invalid_argument);
}

TEST(LocalFunctionMethodTest, IsLocalFunctionIsAlwaysTrue)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base);

    // The contrast pin: the BASE method is not a local function.
    EXPECT_FALSE(base->IsLocalFunction());
    EXPECT_TRUE(wrapper->IsLocalFunction());

    // Dispatch through the IMethod base pointer.
    const IMethod* asMethod = wrapper.get();
    EXPECT_TRUE(asMethod->IsLocalFunction());
}

TEST(LocalFunctionMethodTest, IsStaticIsAlwaysTrueEvenForInstanceBase)
{
    auto base = MakeBase();
    base->SetStatic(false);
    // isStaticLocalFunction == false too: the wrapper still reports IsStatic.
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", false, 0, 0);

    EXPECT_FALSE(base->IsStatic());
    EXPECT_TRUE(wrapper->IsStatic());

    // Dispatch through the IMember base pointer (the IMethod surface does not carry
    // IsStatic).
    const IMember* asMember = wrapper.get();
    EXPECT_TRUE(asMember->IsStatic());
}

TEST(LocalFunctionMethodTest, ReducedFromReturnsTheBaseMethodItself)
{
    auto base = MakeBase();
    auto otherBase = MakeBase("Other");
    // The base's OWN ReducedFrom is non-null and distinct from the base itself, so the
    // assert distinguishes returning the base from forwarding to the base's value.
    base->SetReducedFrom(otherBase.get());

    auto wrapper = MakeWrapper(base);

    EXPECT_EQ(wrapper->ReducedFrom(), base.get());
    EXPECT_NE(wrapper->ReducedFrom(), otherBase.get());
    EXPECT_NE(wrapper->ReducedFrom(), nullptr);
}

TEST(LocalFunctionMethodTest, NameAndFullNameAreTheOwnNotTheBase)
{
    auto base = MakeBase("M");
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1");

    EXPECT_EQ(base->Name(), "M");
    EXPECT_EQ(wrapper->Name(), "<M>g__F|0_1");
    EXPECT_EQ(wrapper->FullName(), "<M>g__F|0_1");

    // ReflectionName / Namespace forward to the base (the same test pins the split).
    EXPECT_EQ(wrapper->ReflectionName(), base->ReflectionName());
    EXPECT_EQ(wrapper->Namespace(), base->Namespace());
}

TEST(LocalFunctionMethodTest, CompilationForwardsToBase)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base);

    EXPECT_EQ(&wrapper->Compilation(), &base->Compilation());
    EXPECT_EQ(&wrapper->Compilation(), &Compilation());
}

TEST(LocalFunctionMethodTest, EntitySurfaceForwardsToBase)
{
    auto base = MakeBase();
    auto host = MakeHost();
    base->SetDeclaringTypeDefinition(host.get());
    base->SetAccessibility(Accessibility::Internal);
    base->SetAbstract(true);
    base->SetSealed(true);
    auto wrapper = MakeWrapper(base);

    EXPECT_EQ(wrapper->SymbolKind(), base->SymbolKind());
    EXPECT_EQ(wrapper->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(wrapper->MetadataToken(), base->MetadataToken());
    EXPECT_EQ(wrapper->DeclaringTypeDefinition(), host.get());
    EXPECT_EQ(wrapper->ParentModule(), base->ParentModule());
    EXPECT_TRUE(wrapper->GetAttributes().empty());
    EXPECT_FALSE(wrapper->HasAttribute(KnownAttribute::Obsolete));
    EXPECT_EQ(wrapper->GetAttribute(KnownAttribute::Obsolete), nullptr);
    EXPECT_EQ(wrapper->Accessibility(), Accessibility::Internal);
    EXPECT_TRUE(wrapper->IsAbstract());
    EXPECT_TRUE(wrapper->IsSealed());
    // DeclaringType forwards the base's handle (both the null default here).
    EXPECT_EQ(wrapper->DeclaringType().get(), base->DeclaringType().get());
}

TEST(LocalFunctionMethodTest, MemberSurfaceForwardsToBase)
{
    auto base = MakeBase();
    base->SetVirtual(true);
    base->SetOverride(true);
    base->SetIsOverridable(true);
    base->SetExplicitInterfaceImplementation(true);
    base->SetReturnTypeIsRefReadOnly(true);
    TypeParameterSubstitution substitution(std::nullopt, std::nullopt);
    base->SetSubstitution(&substitution);
    auto wrapper = MakeWrapper(base);

    // ReturnType / Substitution forward by reference / pointer identity.
    EXPECT_EQ(&wrapper->ReturnType(), &base->ReturnType());
    EXPECT_EQ(wrapper->Substitution(), &substitution);
    EXPECT_TRUE(wrapper->IsVirtual());
    EXPECT_TRUE(wrapper->IsOverride());
    EXPECT_TRUE(wrapper->IsOverridable());
    EXPECT_TRUE(wrapper->IsExplicitInterfaceImplementation());
    EXPECT_TRUE(wrapper->ReturnTypeIsRefReadOnly());
    EXPECT_TRUE(wrapper->ExplicitlyImplementedInterfaceMembers().empty());
}

TEST(LocalFunctionMethodTest, IMethodSurfaceForwardsToBase)
{
    auto base = MakeBase();
    base->SetIsOperator(true);
    base->SetIsExtensionMethod(true);
    base->SetInitOnly(true);
    base->SetThisIsRefReadOnly(true);
    base->SetConstructor(true);
    base->SetDestructor(true);
    base->SetHasBody(true);
    base->SetAccessor(true);
    auto owner = MakeBase("P");
    base->SetAccessorOwner(owner.get());
    base->SetAccessorKind(MethodSemanticsAttributes::Adder);
    auto wrapper = MakeWrapper(base);

    EXPECT_TRUE(wrapper->GetReturnTypeAttributes().empty());
    EXPECT_TRUE(wrapper->IsInitOnly());
    EXPECT_TRUE(wrapper->ThisIsRefReadOnly());
    EXPECT_TRUE(wrapper->IsExtensionMethod());
    EXPECT_TRUE(wrapper->IsConstructor());
    EXPECT_TRUE(wrapper->IsDestructor());
    EXPECT_TRUE(wrapper->IsOperator());
    EXPECT_TRUE(wrapper->HasBody());
    EXPECT_TRUE(wrapper->IsAccessor());
    EXPECT_EQ(wrapper->AccessorOwner(), owner.get());
    EXPECT_EQ(wrapper->AccessorKind(), MethodSemanticsAttributes::Adder);
}

TEST(LocalFunctionMethodTest, ParametersHideTheCompilerGeneratedTail)
{
    auto p1 = std::make_shared<DefaultParameter>(
        std::make_shared<KnownType>(KnownTypeCode::Int32), "x");
    auto p2 = std::make_shared<DefaultParameter>(
        std::make_shared<KnownType>(KnownTypeCode::Int32), "y");
    auto p3 = std::make_shared<DefaultParameter>(
        std::make_shared<KnownType>(KnownTypeCode::Int32), "closure");
    auto base = MakeBase();
    base->SetParameters({p1.get(), p2.get(), p3.get()});

    // Two generated parameters: only the first (source-level) parameter shows.
    auto trimmed = MakeWrapper(base, "<M>g__F|0_1", false, 2, 0);
    ASSERT_EQ(trimmed->Parameters().size(), 1u);
    EXPECT_EQ(trimmed->Parameters()[0], p1.get());

    // Zero generated: the full list (with the tail) shows.
    auto full = MakeWrapper(base, "<M>g__F|0_1", false, 0, 0);
    ASSERT_EQ(full->Parameters().size(), 3u);
    EXPECT_EQ(full->Parameters()[0], p1.get());
    EXPECT_EQ(full->Parameters()[1], p2.get());
    EXPECT_EQ(full->Parameters()[2], p3.get());

    // More generated than present: empty (the SkipLast overflow shape).
    auto overflow = MakeWrapper(base, "<M>g__F|0_1", false, 5, 0);
    EXPECT_TRUE(overflow->Parameters().empty());
}

TEST(LocalFunctionMethodTest, TypeParametersHideTheCompilerGeneratedTail)
{
    auto tp1 = std::make_shared<LookupTypeParameter>("T");
    auto tp2 = std::make_shared<LookupTypeParameter>("U");
    auto tp3 = std::make_shared<LookupTypeParameter>("TCached");
    auto base = MakeBase();
    base->SetTypeParameters({tp1.get(), tp2.get(), tp3.get()});

    auto trimmed = MakeWrapper(base, "<M>g__F|0_1", false, 0, 2);
    ASSERT_EQ(trimmed->TypeParameters().size(), 1u);
    EXPECT_EQ(trimmed->TypeParameters()[0], tp1.get());

    auto full = MakeWrapper(base, "<M>g__F|0_1", false, 0, 0);
    ASSERT_EQ(full->TypeParameters().size(), 3u);
    EXPECT_EQ(full->TypeParameters()[2], tp3.get());

    auto overflow = MakeWrapper(base, "<M>g__F|0_1", false, 0, 3);
    EXPECT_TRUE(overflow->TypeParameters().empty());
}

TEST(LocalFunctionMethodTest, TypeArgumentsHideTheCompilerGeneratedTail)
{
    auto t1 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto t2 = std::make_shared<KnownType>(KnownTypeCode::String);
    auto t3 = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto base = MakeBase();
    base->SetTypeArguments({t1, t2, t3});

    auto trimmed = MakeWrapper(base, "<M>g__F|0_1", false, 0, 2);
    ASSERT_EQ(trimmed->TypeArguments().size(), 1u);
    EXPECT_EQ(trimmed->TypeArguments()[0].get(), t1.get());

    auto full = MakeWrapper(base, "<M>g__F|0_1", false, 0, 0);
    ASSERT_EQ(full->TypeArguments().size(), 3u);
    EXPECT_EQ(full->TypeArguments()[2].get(), t3.get());

    auto overflow = MakeWrapper(base, "<M>g__F|0_1", false, 0, 4);
    EXPECT_TRUE(overflow->TypeArguments().empty());
}

TEST(LocalFunctionMethodTest, MemberDefinitionReturnsThisWhenBaseIsItsOwnDefinition)
{
    auto base = MakeBase();
    // The ConfigurableMethod default: the base IS its own definition.
    ASSERT_EQ(base->MemberDefinition(), base.get());
    auto wrapper = MakeWrapper(base);

    EXPECT_EQ(wrapper->MemberDefinition(), wrapper.get());
}

TEST(LocalFunctionMethodTest, MemberDefinitionRewrapsWhenBaseIsSpecialized)
{
    auto base = MakeBase();
    auto definition = MakeBase("M`definition");
    base->SetMemberDefinition(definition.get());
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", /*isStaticLocalFunction*/ true,
        /*generatedParameters*/ 3, /*generatedTypeParameters*/ 1);

    const IMember* memberDefinition = wrapper->MemberDefinition();
    ASSERT_NE(memberDefinition, wrapper.get());
    const auto* rewrap = dynamic_cast<const LocalFunctionMethod*>(memberDefinition);
    ASSERT_NE(rewrap, nullptr);
    EXPECT_TRUE(rewrap->IsLocalFunction());
    EXPECT_EQ(rewrap->Name(), "<M>g__F|0_1");
    EXPECT_EQ(rewrap->NumberOfCompilerGeneratedParameters(), 3);
    // The rewrap wraps the base's DEFINITION (observable through ReducedFrom).
    EXPECT_EQ(rewrap->ReducedFrom(), definition.get());
}

TEST(LocalFunctionMethodTest, MemberDefinitionPassesTheDegenerateNullThrough)
{
    auto base = MakeBase();
    base->SetMemberDefinition(nullptr);
    auto wrapper = MakeWrapper(base);

    EXPECT_EQ(wrapper->MemberDefinition(), nullptr);
}

TEST(LocalFunctionMethodTest, SpecializeWrapsTheSpecializedBase)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", /*isStaticLocalFunction*/ true,
        /*generatedParameters*/ 3, /*generatedTypeParameters*/ 1);
    TypeParameterSubstitution substitution(std::nullopt, std::nullopt);

    const IMethod* result = wrapper->Specialize(&substitution);

    // The base received the substitution pointer unchanged (the passthrough pin).
    EXPECT_EQ(base->LastSpecializeSubstitution(), &substitution);
    // The result is a fresh local-function wrapper over the specialized base (the stub
    // specializes to itself, so the rewrap's ReducedFrom is the stub).
    ASSERT_NE(result, nullptr);
    const auto* rewrap = dynamic_cast<const LocalFunctionMethod*>(result);
    ASSERT_NE(rewrap, nullptr);
    EXPECT_TRUE(rewrap->IsLocalFunction());
    EXPECT_TRUE(rewrap->IsStatic());
    EXPECT_EQ(rewrap->Name(), "<M>g__F|0_1");
    EXPECT_EQ(rewrap->NumberOfCompilerGeneratedParameters(), 3);
    EXPECT_EQ(rewrap->NumberOfCompilerGeneratedTypeParameters(), 1);
    EXPECT_EQ(rewrap->ReducedFrom(), base.get());
}

TEST(LocalFunctionMethodTest, SpecializeProducesFreshWrappersPerCall)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", true, 2, 1);
    TypeParameterSubstitution substitution(std::nullopt, std::nullopt);

    const IMethod* first = wrapper->Specialize(&substitution);
    const IMethod* second = wrapper->Specialize(&substitution);

    // Fresh per call (the C# allocates a new wrapper each time)...
    EXPECT_NE(first, second);
    // ...and the two results are Equals-equal (same base + same fields).
    EXPECT_TRUE(first->Equals(second, nullptr));
    EXPECT_TRUE(second->Equals(first, nullptr));
}

TEST(LocalFunctionMethodTest, EqualsRequiresTheLocalFunctionMethodRtti)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base);

    // The raw base method is not a LocalFunctionMethod.
    EXPECT_FALSE(wrapper->Equals(base.get(), nullptr));
    EXPECT_FALSE(wrapper->Equals(nullptr, nullptr));
}

TEST(LocalFunctionMethodTest, EqualsComparesBaseCountsAndFlagUnderNormalization)
{
    auto base = MakeBase();
    auto a = MakeWrapper(base, "<M>g__F|0_1", true, 2, 1);
    auto b = MakeWrapper(base, "<M>g__F|0_1", true, 2, 1);
    TypeVisitor normalizationA;
    TypeVisitor normalizationB;

    EXPECT_TRUE(a->Equals(b.get(), &normalizationA));
    // The base's Equals received the normalization pointer (the pass-through pin); the
    // assert runs BEFORE the reverse call, which records its own normalization.
    EXPECT_EQ(base->LastEqualsNormalization(), &normalizationA);
    EXPECT_TRUE(b->Equals(a.get(), &normalizationB));
    EXPECT_EQ(base->LastEqualsNormalization(), &normalizationB);
}

TEST(LocalFunctionMethodTest, EqualsDistinguishesTheGeneratedCountsAndTheStaticFlag)
{
    auto base = MakeBase();
    auto a = MakeWrapper(base, "<M>g__F|0_1", true, 2, 1);
    auto otherParameterCount = MakeWrapper(base, "<M>g__F|0_1", true, 3, 1);
    auto otherTypeParameterCount = MakeWrapper(base, "<M>g__F|0_1", true, 2, 2);
    auto otherStaticFlag = MakeWrapper(base, "<M>g__F|0_1", false, 2, 1);

    EXPECT_FALSE(a->Equals(otherParameterCount.get(), nullptr));
    EXPECT_FALSE(a->Equals(otherTypeParameterCount.get(), nullptr));
    EXPECT_FALSE(a->Equals(otherStaticFlag.get(), nullptr));
}

TEST(LocalFunctionMethodTest, EqualsDistinguishesTheBaseMethod)
{
    auto baseA = MakeBase();
    auto baseB = MakeBase();
    auto a = MakeWrapper(baseA);
    auto b = MakeWrapper(baseB);

    // The stubs compare by identity, so distinct bases are unequal.
    EXPECT_FALSE(a->Equals(b.get(), nullptr));
}

TEST(LocalFunctionMethodTest, GetHashCodeMatchesForTheSameBase)
{
    auto base = MakeBase();
    auto otherBase = MakeBase();
    auto a = MakeWrapper(base);
    auto b = MakeWrapper(base);
    auto c = MakeWrapper(otherBase);

    EXPECT_EQ(a->GetHashCode(), b->GetHashCode());
    EXPECT_NE(a->GetHashCode(), c->GetHashCode());
}

TEST(LocalFunctionMethodTest, DispatchesThroughTheDiamondBasePointers)
{
    auto base = MakeBase();
    auto wrapper = MakeWrapper(base, "<M>g__F|0_1", true, 1, 1);

    const ISymbol* asSymbol = wrapper.get();
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(asSymbol->Name(), "<M>g__F|0_1");

    const INamedElement* asNamedElement = wrapper.get();
    EXPECT_EQ(asNamedElement->FullName(), "<M>g__F|0_1");
    EXPECT_EQ(asNamedElement->ReflectionName(), base->ReflectionName());

    const ICompilationProvider* asProvider = wrapper.get();
    EXPECT_EQ(&asProvider->Compilation(), &Compilation());

    const IEntity* asEntity = wrapper.get();
    EXPECT_TRUE(asEntity->IsStatic());

    const IMember* asMember = wrapper.get();
    EXPECT_EQ(asMember->Name(), "<M>g__F|0_1");

    const IMethod* asMethod = wrapper.get();
    EXPECT_TRUE(asMethod->IsLocalFunction());
}

TEST(LocalFunctionMethodTest, ClassShapeIsNotFinalAndDerivesTheInterfaces)
{
    static_assert(!std::is_final_v<LocalFunctionMethod>,
        "the C# class is unsealed");
    static_assert(std::is_base_of_v<IMethod, LocalFunctionMethod>, "");
    static_assert(std::is_base_of_v<IMember, LocalFunctionMethod>, "");
    static_assert(std::is_base_of_v<IEntity, LocalFunctionMethod>, "");
    static_assert(std::is_base_of_v<ISymbol, LocalFunctionMethod>, "");
    static_assert(std::has_virtual_destructor_v<LocalFunctionMethod>, "");
    static_assert(std::is_polymorphic_v<LocalFunctionMethod>, "");
    SUCCEED();
}

} // namespace
