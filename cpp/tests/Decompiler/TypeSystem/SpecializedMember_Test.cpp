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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `SpecializedMember` (D482) -- the abstract `IMember` base a specialized
// method / property / field / event derives from (Implementation/SpecializedMember.{hpp,cpp}).
// It wraps a base (unspecialized) member and a `TypeParameterSubstitution`, delegating the
// whole `IMember` surface to the base EXCEPT `ReturnType` / `DeclaringType` / `Substitution`
// (re-computed through the substitution). The test exercises it through a test-only
// public-ctor subclass `TestSpecializedMember`.
//
// The tests pin:
//  (a) the ctor rejects a null base and a `SpecializedMember` base (the C# `throw new
//      ArgumentNullException` / `ArgumentException`);
//  (b) `Substitution()` is `Identity` (value-equal) before any `AddSubstitution`, and
//      `AddSubstitution` composes (the new substitution applied after the existing);
//  (c) the trivial delegations forward to the base (`Name`, `SymbolKind`, `IsVirtual` /
//      `IsOverride` / `IsOverridable`, `MetadataToken`, `DeclaringTypeDefinition`,
//      `MemberDefinition`, the `IEntity` / `INamedElement` / `ICompilationProvider` /
//      `ISymbol` surface);
//  (d) `ReturnType()` lazily applies the substitution to the base return type (a class
//      type parameter -> the substitution's class type argument);
//  (e) `DeclaringType()` -- the three reachable arms: a non-`ITypeDefinition` declaring
//      type (e.g. `ParameterizedType`) -> `AcceptVisitor`; a generic `ITypeDefinition` with
//      MATCHING class args -> `new ParameterizedType(def, classArgs)`; a null declaring
//      type -> null. Plus the DEFERRED arm (a generic `ITypeDefinition` with non-matching
//      args -> falls through to `AcceptVisitor`); the Identity substitution leaves the
//      `ITypeDefinition` unchanged (`AcceptVisitor` returns the same instance);
//  (f) `Specialize()` delegates to the base member's `Specialize`;
//  (g) `ExplicitlyImplementedInterfaceMembers()` forwards the base list (DEFERRED
//      un-specialized -- the common empty case is faithful);
//  (h) `Equals(IMember*, TypeVisitor*)` / `Equals(const SpecializedMember*)` -- same base +
//      same substitution => equal; different base / different substitution / null => not;
//  (i) `GetHashCode()` is consistent (same base + same substitution => same hash, stable).

#include "Decompiler/TypeSystem/Implementation/SpecializedMember.hpp"
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

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
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
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMember;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` (the `TestSubstTypeParameter` pattern from
// `TypeParameterSubstitution_Test.cpp`): holds the owner kind + index, dispatches
// `AcceptVisitor` to `visitor.VisitTypeParameter(*this)` (the C#
// `AbstractTypeParameter.AcceptVisitor` bridge). `make_shared`-managed so the base
// `VisitChildren -> shared_from_this` identity fallback is sound. Used as the base
// member's `ReturnType` so the `SpecializedMember.ReturnType()` substitution effect is
// observable (a class type parameter at index 0 -> the substitution's class type argument).
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

// A configurable concrete `IMember` -- the "base member" the `SpecializedMember` wraps.
// Holds the name, return type (an owning `ITypePtr`, returned as `const IType&`), the
// declaring type (an owning `ITypePtr`), the SymbolKind, and a shared `ICompilation`.
// `Substitution()` returns a held `Identity` substitution; `Specialize()` returns a
// non-owning pointer to a lazily-built owned copy (the `IMember` "type system owns"
// convention; the test keeps the stub alive); `MemberDefinition()` returns `this`;
// `Equals()` is identity. The `mutable` lazy fields mirror the `SpecializedMember` lazy
// pattern (set in `const` accessors).
class TestBaseMember : public IMember {
public:
    TestBaseMember(std::string name, ITypePtr returnType, ITypePtr declaringType,
                   const ICompilation& compilation,
                   ::ILSpy::Decompiler::TypeSystem::SymbolKind kind =
                       ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          declaringType_(std::move(declaringType)), compilation_(compilation), kind_(kind) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
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
        // The stub "owns" the specialized result (held in specialized_) and returns a
        // non-owning pointer (the IMember "type system owns" convention). Built lazily.
        if (!specialized_) {
            specialized_ = std::make_shared<TestBaseMember>(*this);
        }
        return specialized_.get();
    }
    bool Equals(const IMember* obj, const TypeVisitor* /*typeNormalization*/) const override
    {
        return obj == this; // identity equality for the stub
    }

private:
    std::string name_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
    ::ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
    mutable std::shared_ptr<IMember> specialized_;
};

// The test-only public-ctor `SpecializedMember` subclass. Exposes the protected
// `AddSubstitution` and `baseMember_` (the latter for the same-base `Equals` check).
class TestSpecializedMember : public SpecializedMember {
public:
    explicit TestSpecializedMember(std::shared_ptr<IMember> baseMember)
        : SpecializedMember(std::move(baseMember)) {}
    using SpecializedMember::AddSubstitution;
    using SpecializedMember::baseMember_;
};

// Build a `std::optional<std::vector<ITypePtr>>` holding the given args (a present list).
std::optional<std::vector<ITypePtr>> List(std::vector<ITypePtr> args) {
    return std::optional<std::vector<ITypePtr>>(std::move(args));
}

// The shared `ICompilation` for the stubs (the `LookupCompilation` default ctor).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A generic `ITypeDefinition` (TypeParameterCount == 1) for the `DeclaringType` arms.
std::shared_ptr<LookupTypeDefinition> GenericDef() {
    return std::make_shared<LookupTypeDefinition>(
        "Foo", "", FullTypeName("Foo`1"), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor rejects a null base (the C# throw new ArgumentNullException).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, CtorRejectsNullBase) {
    EXPECT_THROW(TestSpecializedMember(nullptr), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// The ctor rejects a SpecializedMember base (the C# throw new ArgumentException).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, CtorRejectsSpecializedMemberBase) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    auto alreadySpecialized = std::make_shared<TestSpecializedMember>(base);
    EXPECT_THROW(TestSpecializedMember{alreadySpecialized}, std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Substitution() is Identity (value-equal) before any AddSubstitution.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, SubstitutionIsIdentityByDefault) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_TRUE(sm.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
}

// ---------------------------------------------------------------------------
// AddSubstitution composes: the new substitution applied after the existing.
// With no prior substitution, AddSubstitution(s) leaves Substitution() value-equal to s.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, AddSubstitutionComposes) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    TypeParameterSubstitution s(List({String()}), std::nullopt);
    sm.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm.Substitution()->Equals(&s));
    EXPECT_FALSE(sm.Substitution()->Equals(&TypeParameterSubstitution::Identity()));
}

// ---------------------------------------------------------------------------
// Name() delegates to the base member.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, NameDelegates) {
    auto base = std::make_shared<TestBaseMember>("field", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.Name(), "field");
}

// ---------------------------------------------------------------------------
// SymbolKind() delegates to the base member.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, SymbolKindDelegates) {
    auto base = std::make_shared<TestBaseMember>(
        "field", Int32(), nullptr, Compilation(), SymbolKind::Field);
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.SymbolKind(), SymbolKind::Field);
}

// ---------------------------------------------------------------------------
// IsVirtual / IsOverride / IsOverridable delegate (the stub defaults false).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, ModifierFlagsDelegate) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_FALSE(sm.IsVirtual());
    EXPECT_FALSE(sm.IsOverride());
    EXPECT_FALSE(sm.IsOverridable());
}

// ---------------------------------------------------------------------------
// MetadataToken / DeclaringTypeDefinition delegate (the stub defaults 0 / null).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, MetadataTokenAndDeclaringTypeDefinitionDelegate) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.MetadataToken(), 0u);
    EXPECT_EQ(sm.DeclaringTypeDefinition(), nullptr);
}

// ---------------------------------------------------------------------------
// MemberDefinition() delegates to the base (the stub returns `this`).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, MemberDefinitionDelegates) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.MemberDefinition(), base.get());
}

// ---------------------------------------------------------------------------
// The trivial IEntity / INamedElement / ICompilationProvider delegations forward.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, TrivialEntitySurfaceDelegates) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.FullName(), "x");
    EXPECT_EQ(sm.ReflectionName(), "x");
    EXPECT_EQ(sm.Namespace(), "");
    EXPECT_EQ(&sm.Compilation(), &Compilation());
    EXPECT_EQ(sm.ParentModule(), nullptr);
    EXPECT_FALSE(sm.IsStatic());
    EXPECT_FALSE(sm.IsAbstract());
    EXPECT_FALSE(sm.IsSealed());
    EXPECT_EQ(sm.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(sm.GetAttributes().empty());
    EXPECT_FALSE(sm.HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute::Obsolete));
    EXPECT_EQ(sm.GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute::Obsolete), nullptr);
    EXPECT_FALSE(sm.IsExplicitInterfaceImplementation());
}

// ---------------------------------------------------------------------------
// ReturnType() lazily applies the substitution: a class type parameter at index 0
// -> the substitution's class type argument (Int32).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, ReturnTypeAppliesSubstitution) {
    auto returnType = std::make_shared<TestSubstTypeParameter>(
        SymbolKind::TypeDefinition, "T", 0);
    auto base = std::make_shared<TestBaseMember>("x", returnType, nullptr, Compilation());
    TestSpecializedMember sm(base);
    sm.AddSubstitution(TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_EQ(sm.ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// ReturnType() with an Identity substitution returns the base return type unchanged
// (a concrete KnownType is not a type parameter, AcceptVisitor leaves it).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, ReturnTypeIdentityLeavesConcreteType) {
    auto base = std::make_shared<TestBaseMember>("x", String(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.ReturnType().Name(), "String");
}

// ---------------------------------------------------------------------------
// DeclaringType() arm 3: a non-ITypeDefinition declaring type (a ParameterizedType)
// -> AcceptVisitor. With an Identity substitution, AcceptVisitor returns the SAME instance.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, DeclaringTypeNonDefinitionAppliesSubstitution) {
    auto baseDecl = std::make_shared<ParameterizedType>(Object(), std::vector<ITypePtr>{String()});
    auto base = std::make_shared<TestBaseMember>("x", Int32(), baseDecl, Compilation());
    TestSpecializedMember sm(base);
    // Identity substitution -> AcceptVisitor(Identity) returns the same ParameterizedType.
    EXPECT_EQ(sm.DeclaringType().get(), baseDecl.get());
}

// ---------------------------------------------------------------------------
// DeclaringType() arm 1: a generic ITypeDefinition with MATCHING class args
// -> new ParameterizedType(def, classArgs). The result's type argument is the class arg.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, DeclaringTypeGenericDefinitionMatchingClassArgs) {
    auto def = GenericDef();
    auto base = std::make_shared<TestBaseMember>("x", Int32(), def, Compilation());
    TestSpecializedMember sm(base);
    sm.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    auto dt = sm.DeclaringType();
    ASSERT_NE(dt, nullptr);
    // The result is a ParameterizedType over `def` with the class arg [String].
    EXPECT_EQ(dt->GetDefinition(), def.get());
    auto pt = std::dynamic_pointer_cast<ParameterizedType>(dt);
    ASSERT_NE(pt, nullptr);
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), "String");
}

// ---------------------------------------------------------------------------
// DeclaringType() DEFERRED arm: a generic ITypeDefinition with NON-matching class args
// (the Identity substitution, no class args) -> falls through to AcceptVisitor.
// With an Identity substitution, AcceptVisitor returns the SAME ITypeDefinition instance.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, DeclaringTypeGenericDefinitionDeferredArm) {
    auto def = GenericDef();
    auto base = std::make_shared<TestBaseMember>("x", Int32(), def, Compilation());
    TestSpecializedMember sm(base);
    // No AddSubstitution -> Identity -> the deferred arm falls through to
    // AcceptVisitor(Identity), which returns the same ITypeDefinition instance.
    EXPECT_EQ(sm.DeclaringType().get(), def.get());
}

// ---------------------------------------------------------------------------
// DeclaringType() with a null base declaring type -> null (empty ITypePtr).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, DeclaringTypeNullBaseReturnsNull) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_EQ(sm.DeclaringType(), nullptr);
}

// ---------------------------------------------------------------------------
// DeclaringType() is cached: a second call returns the same instance.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, DeclaringTypeIsCached) {
    auto baseDecl = std::make_shared<ParameterizedType>(Object(), std::vector<ITypePtr>{String()});
    auto base = std::make_shared<TestBaseMember>("x", Int32(), baseDecl, Compilation());
    TestSpecializedMember sm(base);
    auto first = sm.DeclaringType();
    auto second = sm.DeclaringType();
    EXPECT_EQ(first.get(), second.get());
}

// ---------------------------------------------------------------------------
// Specialize() delegates to the base member's Specialize (returns the base's result).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, SpecializeDelegatesToBase) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    TypeParameterSubstitution s(List({String()}), std::nullopt);
    // The base's Specialize returns its lazily-built owned copy; the SpecializedMember
    // delegates, returning the same non-owning pointer the base returns.
    const IMember* baseResult = base->Specialize(&s);
    const IMember* smResult = sm.Specialize(&s);
    EXPECT_EQ(smResult, baseResult);
}

// ---------------------------------------------------------------------------
// ExplicitlyImplementedInterfaceMembers() forwards the base list (DEFERRED un-specialized;
// the stub returns empty, so the specialized returns empty).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, ExplicitlyImplementedInterfaceMembersForwardsBase) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_TRUE(sm.ExplicitlyImplementedInterfaceMembers().empty());
}

// ---------------------------------------------------------------------------
// Equals(IMember*, TypeVisitor*): same base (pointer-equal) + same substitution => equal.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsSameBaseAndSubstitution) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm1(base);
    TestSpecializedMember sm2(base);
    sm1.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    sm2.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm1.Equals(&sm2, nullptr));
    EXPECT_TRUE(sm2.Equals(&sm1, nullptr));
}

// ---------------------------------------------------------------------------
// Equals: a non-SpecializedMember other => not equal.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsNonSpecializedOther) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_FALSE(sm.Equals(base.get(), nullptr));
}

// ---------------------------------------------------------------------------
// Equals: null other => not equal.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsNull) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm(base);
    EXPECT_FALSE(sm.Equals(nullptr, nullptr));
}

// ---------------------------------------------------------------------------
// Equals: different base (different pointers) => not equal (the stub Equals is identity).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsDifferentBase) {
    auto base1 = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    auto base2 = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm1(base1);
    TestSpecializedMember sm2(base2);
    EXPECT_FALSE(sm1.Equals(&sm2, nullptr));
}

// ---------------------------------------------------------------------------
// Equals: same base, different substitution => not equal.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsDifferentSubstitution) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm1(base);
    TestSpecializedMember sm2(base);
    sm1.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    sm2.AddSubstitution(TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(sm1.Equals(&sm2, nullptr));
}

// ---------------------------------------------------------------------------
// Equals(const SpecializedMember*) (the object.Equals standalone): same base + same
// substitution => equal; null / different => not.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, EqualsObjectSameBaseAndSubstitution) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm1(base);
    TestSpecializedMember sm2(base);
    sm1.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    sm2.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    EXPECT_TRUE(sm1.Equals(&sm2));
    EXPECT_FALSE(sm1.Equals(nullptr));
    TestSpecializedMember sm3(base);
    sm3.AddSubstitution(TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_FALSE(sm1.Equals(&sm3));
}

// ---------------------------------------------------------------------------
// GetHashCode(): same base + same substitution => same hash; stable across calls.
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, GetHashCodeIsConsistent) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    // The substitution's `GetHashCode` uses pointer-identity hashing of its elements
    // (the C# `object.GetHashCode` identity hash -> `std::hash<IType*>` on each element,
    // the `TypeParameterSubstitutionTest::GetHashCodeIsConsistent` precedent). Use the
    // SAME `String` instance for both substitutions so the pointer-identity hashes match
    // (two value-equal-but-distinct instances would hash differently, faithful to the C#
    // identity-hash default -- the production concrete types' value-based overrides are
    // not ported yet).
    auto str = String();
    TestSpecializedMember sm1(base);
    TestSpecializedMember sm2(base);
    sm1.AddSubstitution(TypeParameterSubstitution(List({str}), std::nullopt));
    sm2.AddSubstitution(TypeParameterSubstitution(List({str}), std::nullopt));
    EXPECT_EQ(sm1.GetHashCode(), sm2.GetHashCode());
    EXPECT_EQ(sm1.GetHashCode(), sm1.GetHashCode()); // stable
}

// ---------------------------------------------------------------------------
// GetHashCode(): different substitution => different hash (the substitution contributes).
// ---------------------------------------------------------------------------
TEST(SpecializedMemberTest, GetHashCodeDiffersForDifferentSubstitution) {
    auto base = std::make_shared<TestBaseMember>("x", Int32(), nullptr, Compilation());
    TestSpecializedMember sm1(base);
    TestSpecializedMember sm2(base);
    sm1.AddSubstitution(TypeParameterSubstitution(List({String()}), std::nullopt));
    sm2.AddSubstitution(TypeParameterSubstitution(List({Int32()}), std::nullopt));
    EXPECT_NE(sm1.GetHashCode(), sm2.GetHashCode());
}
