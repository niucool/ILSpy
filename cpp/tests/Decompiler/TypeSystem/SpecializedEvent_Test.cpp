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

// Tests for `SpecializedEvent` (D485) -- the concrete `IEvent` a
// `GetMembersHelper.GetEventsImpl` builds for an event on a parameterized type
// (`Implementation/SpecializedEvent.hpp`). It derives `SpecializedMember, IEvent`: the
// `SpecializedMember` base supplies the substituted `ReturnType` / `DeclaringType` /
// `Substitution` + the delegated `IMember` surface; this class adds the `IEvent`-own
// surface (`CanAdd` / `CanRemove` / `CanInvoke` + the accessors), delegating to the wrapped
// `eventDefinition`.
//
// The tests pin:
//  (a) the ctor wires the substitution (`Substitution()` is the composed substitution, NOT
//      `Identity`);
//  (b) `Substitution()` / `MemberDefinition()` / `Specialize()` delegate to the
//      `SpecializedMember` base;
//  (c) the trivial `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
//      `ISymbol` delegations forward to the base member (Name / SymbolKind / IsVirtual /
//      MetadataToken / DeclaringTypeDefinition / Compilation / etc.);
//  (d) `ReturnType()` / `DeclaringType()` are the SUBSTITUTED values (a class type
//      parameter at index 0 -> the substitution's class type argument);
//  (e) the `IEvent`-own bools (`CanAdd` / `CanRemove` / `CanInvoke`) delegate to the event
//      definition;
//  (f) the accessors (`AddAccessor` / `RemoveAccessor` / `InvokeAccessor`) delegate to the
//      event definition (DEFERRED un-specialized -- the `WrapAccessor` owning-`Specialize`
//      design is not yet in place; the base accessor is returned, a documented divergence);
//  (g) the two-`IMember`-subobject diamond: `SpecializedEvent` IS-A `IEvent` AND `IMember`,
//      dispatch through `IEvent*` / `IMember*` (via `SpecializedMember*`) all reach the single
//      override;
//  (h) `Equals` / `GetHashCode` (inherited from `SpecializedMember`).

#include "Decompiler/TypeSystem/Implementation/SpecializedEvent.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

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
using ILSpy::Decompiler::TypeSystem::IEvent;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedEvent;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMember;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern): a class type
// parameter at a given index. Used as the event's `ReturnType` so the
// `SpecializedEvent.ReturnType()` substitution effect is observable.
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

// A minimal concrete `IMethod` stand-in for the accessor properties (the
// `SpecializedEvent.AddAccessor()` DEFERRED override returns the base accessor). Reuses the
// `LookupMethod` stub from `LookupStubs.hpp` (a full `IMethod` impl); only `Name()` is
// exercised (the accessor's identity).

// A configurable concrete `IEvent` -- the "event definition" the `SpecializedEvent` wraps.
// Implements the whole `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
// `ISymbol` / `IEvent` surface. Holds the name, return type, declaring type, the CanAdd /
// CanRemove / CanInvoke flags, and the three accessor methods. `Substitution()` returns
// `Identity`; `MemberDefinition()` returns `this`; `Specialize()` returns `this`; `Equals()`
// is identity.
class TestBaseEvent : public IEvent {
public:
    TestBaseEvent(std::string name, ITypePtr returnType, ITypePtr declaringType,
                  const ICompilation& compilation,
                  bool canAdd = true, bool canRemove = true, bool canInvoke = false,
                  const IMethod* addAccessor = nullptr, const IMethod* removeAccessor = nullptr,
                  const IMethod* invokeAccessor = nullptr)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation),
          canAdd_(canAdd), canRemove_(canRemove), canInvoke_(canInvoke),
          addAccessor_(addAccessor), removeAccessor_(removeAccessor),
          invokeAccessor_(invokeAccessor) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event;
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

    // --- IEvent ---
    bool CanAdd() const override { return canAdd_; }
    bool CanRemove() const override { return canRemove_; }
    bool CanInvoke() const override { return canInvoke_; }
    const IMethod* AddAccessor() const override { return addAccessor_; }
    const IMethod* RemoveAccessor() const override { return removeAccessor_; }
    const IMethod* InvokeAccessor() const override { return invokeAccessor_; }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    bool canAdd_;
    bool canRemove_;
    bool canInvoke_;
    const IMethod* addAccessor_;
    const IMethod* removeAccessor_;
    const IMethod* invokeAccessor_;
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
// The ctor wires the substitution: Substitution() is the composed substitution, not Identity.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, CtorWiresSubstitution) {
    auto ev = std::make_shared<TestBaseEvent>("x", Int32(), nullptr, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_FALSE(se.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
    EXPECT_TRUE(se.Substitution()->Equals(
        &TypeParameterSubstitution(List({String()}), std::nullopt)));
}

// ---------------------------------------------------------------------------
// MemberDefinition() / Name() / SymbolKind() delegate to the base.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, MemberDefinitionAndNameAndSymbolKindDelegate) {
    auto ev = std::make_shared<TestBaseEvent>("myEvent", Int32(), nullptr, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(se.MemberDefinition(), ev.get());
    EXPECT_EQ(se.Name(), "myEvent");
    EXPECT_EQ(se.SymbolKind(), SymbolKind::Event);
}

// ---------------------------------------------------------------------------
// The trivial IEntity / INamedElement / ICompilationProvider delegations forward.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, TrivialEntitySurfaceDelegates) {
    auto ev = std::make_shared<TestBaseEvent>("x", Int32(), nullptr, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(se.FullName(), "x");
    EXPECT_EQ(se.ReflectionName(), "x");
    EXPECT_EQ(se.Namespace(), "");
    EXPECT_EQ(&se.Compilation(), &Compilation());
    EXPECT_EQ(se.ParentModule(), nullptr);
    EXPECT_EQ(se.MetadataToken(), 0u);
    EXPECT_EQ(se.DeclaringTypeDefinition(), nullptr);
    EXPECT_FALSE(se.IsStatic());
    EXPECT_FALSE(se.IsAbstract());
    EXPECT_FALSE(se.IsSealed());
    EXPECT_EQ(se.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(se.GetAttributes().empty());
    EXPECT_FALSE(se.IsExplicitInterfaceImplementation());
    EXPECT_FALSE(se.IsVirtual());
    EXPECT_FALSE(se.IsOverride());
    EXPECT_FALSE(se.IsOverridable());
}

// ---------------------------------------------------------------------------
// ReturnType() applies the substitution: a class type parameter at index 0 -> the class arg.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, ReturnTypeAppliesSubstitution) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto ev = std::make_shared<TestBaseEvent>("x", returnType, nullptr, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_EQ(se.ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// DeclaringType() applies the substitution: a generic ITypeDefinition with matching class
// args -> new ParameterizedType(def, classArgs).
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, DeclaringTypeAppliesSubstitution) {
    auto def = GenericDef();
    auto ev = std::make_shared<TestBaseEvent>("x", Int32(), def, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    auto dt = se.DeclaringType();
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->GetDefinition(), def.get());
    auto pt = std::dynamic_pointer_cast<ParameterizedType>(dt);
    ASSERT_NE(pt, nullptr);
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), "String");
}

// ---------------------------------------------------------------------------
// The IEvent-own bools (CanAdd / CanRemove / CanInvoke) delegate to the event definition.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, IEventBoolsDelegate) {
    auto ev = std::make_shared<TestBaseEvent>(
        "x", Int32(), nullptr, Compilation(),
        /*canAdd*/ true, /*canRemove*/ false, /*canInvoke*/ true);
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(se.CanAdd());
    EXPECT_FALSE(se.CanRemove());
    EXPECT_TRUE(se.CanInvoke());
}

// ---------------------------------------------------------------------------
// The accessors (AddAccessor / RemoveAccessor / InvokeAccessor) delegate to the event
// definition (DEFERRED un-specialized -- the base accessor is returned).
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, AccessorsDelegateToBase) {
    auto addAcc = std::make_shared<LookupMethod>("add_x", Compilation());
    auto removeAcc = std::make_shared<LookupMethod>("remove_x", Compilation());
    auto invokeAcc = std::make_shared<LookupMethod>("raise_x", Compilation());
    auto ev = std::make_shared<TestBaseEvent>(
        "x", Int32(), nullptr, Compilation(),
        true, true, true, addAcc.get(), removeAcc.get(), invokeAcc.get());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_EQ(se.AddAccessor(), addAcc.get());
    EXPECT_EQ(se.RemoveAccessor(), removeAcc.get());
    EXPECT_EQ(se.InvokeAccessor(), invokeAcc.get());
}

// ---------------------------------------------------------------------------
// The two-IMember-subobject diamond: dispatch through IEvent* / IMember* (via
// SpecializedMember*) all reach the single override.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, DiamondDispatchThroughAllBases) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto ev = std::make_shared<TestBaseEvent>("x", returnType, nullptr, Compilation());
    auto se = std::make_shared<SpecializedEvent>(
        ev, TypeParameterSubstitution(List({Int32()}), std::nullopt));

    // Through IEvent* (the full surface).
    IEvent* asEvent = se.get();
    EXPECT_EQ(asEvent->Name(), "x");
    EXPECT_EQ(asEvent->SymbolKind(), SymbolKind::Event);
    EXPECT_EQ(asEvent->ReturnType().Name(), "Int32");

    // Through IMember* -- the SpecializedEvent* -> IMember* upcast is AMBIGUOUS (two IMember
    // subobjects), so upcast through the unambiguous SpecializedMember* (one IMember).
    IMember* asMember = static_cast<SpecializedMember*>(se.get());
    EXPECT_EQ(asMember->Name(), "x");
    EXPECT_EQ(asMember->ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Specialize() delegates to the SpecializedMember base (the base member's Specialize).
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, SpecializeDelegates) {
    auto ev = std::make_shared<TestBaseEvent>("x", Int32(), nullptr, Compilation());
    SpecializedEvent se(ev, TypeParameterSubstitution(List({String()}), std::nullopt));
    TypeParameterSubstitution s(List({Int32()}), std::nullopt);
    const IMember* baseResult = ev->Specialize(&s);
    const IMember* seResult = se.Specialize(&s);
    EXPECT_EQ(seResult, baseResult);
}

// ---------------------------------------------------------------------------
// Equals / GetHashCode (inherited from SpecializedMember): same base + same substitution
// => equal / same hash.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, EqualsAndHashInheritedFromSpecializedMember) {
    auto ev = std::make_shared<TestBaseEvent>("x", Int32(), nullptr, Compilation());
    auto str = String();
    SpecializedEvent se1(ev, TypeParameterSubstitution(List({str}), std::nullopt));
    SpecializedEvent se2(ev, TypeParameterSubstitution(List({str}), std::nullopt));
    EXPECT_TRUE(se1.Equals(static_cast<const SpecializedMember*>(&se2), nullptr));
    EXPECT_EQ(se1.GetHashCode(), se2.GetHashCode());
    // Different substitution => not equal / different hash.
    SpecializedEvent se3(ev, TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(se1.Equals(static_cast<const SpecializedMember*>(&se3), nullptr));
    EXPECT_NE(se1.GetHashCode(), se3.GetHashCode());
}

// ---------------------------------------------------------------------------
// The class shape: SpecializedEvent IS-A IEvent / IMember / SpecializedMember, and is final.
// ---------------------------------------------------------------------------
TEST(SpecializedEventTest, ClassShape) {
    static_assert(std::is_base_of_v<IEvent, SpecializedEvent>);
    static_assert(std::is_base_of_v<IMember, SpecializedEvent>);
    static_assert(std::is_base_of_v<SpecializedMember, SpecializedEvent>);
    static_assert(std::is_final_v<SpecializedEvent>);
}
