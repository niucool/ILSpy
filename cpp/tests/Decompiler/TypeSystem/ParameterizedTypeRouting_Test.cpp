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

// Tests for the `ParameterizedType` member-enumeration ROUTING arm (D490) -- the `else` branch of
// each `ParameterizedType.cs` member override: `GetMembersHelper.GetXxx(this, filter, options)`,
// which builds the `Specialized*` instances. The D489 leaf ported the `ReturnMemberDefinitions`
// arm (delegate to the generic type); this leaf ports the routing arm -- the
// `ParameterizedType` caches the owning `Specialized*` vectors in `mutable` members (lazy, built
// once with `nullptr` filter + `IgnoreInheritedMembers`) and returns NON-OWNING `const T*`
// snapshots, applying the caller's filter at return time.
//
// The tests pin:
//  (a) `GetMethods(IgnoreInheritedMembers)` (no `ReturnMemberDefinitions`) -> `SpecializedMethod`
//      instances (the routing arm fires, not the D489 delegation);
//  (b) the `ReturnType` substitution effect (T at index 0 -> the type argument) for the routing
//      arm -- the `Specialized*` carry the substitution;
//  (c) the caller's filter is applied at return time (a name filter selects);
//  (d) the cache is stable -- a second call yields the same `Specialized*` instances (pointer
//      identity), documenting the "type system owns" convention;
//  (e) `GetMembers` (the inherited `IType::GetMembers` composition) aggregates the four family
//      routing arms -> `Specialized*` per family;
//  (f) the `ReturnMemberDefinitions` arm still delegates (D489 unchanged) -- the `Specialized*`
//      are NOT built when `ReturnMemberDefinitions` is set.

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedProperty.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEvent;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedEvent;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedField;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedMethod;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedProperty;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupEvent;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A minimal `ITypeParameter` (a class type parameter at index 0) used as a member's return type so
// the routing-arm `Specialized*` substitution effect is observable (T at index 0 -> the arg).
class TestSubstTypeParameter : public ITypeParameter {
public:
    explicit TestSubstTypeParameter(std::string name, int index)
        : name_(std::move(name)), index_(index) {}

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
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
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
    std::vector<TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::string name_;
    int index_;
};

// A minimal `IField` stub (a field definition with a name + return type).
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
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
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// A minimal `IProperty` stub (a property definition with a name + return type).
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
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
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }
    std::vector<const IParameter*> Parameters() const override { return {}; }
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// A `TestTypeDefinition : LookupTypeDefinition` holding configurable member vectors per family,
// applying the caller's filter (standing in for the not-yet-ported `MetadataTypeDefinition`).
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, int typeParamCount, const ICompilation& compilation)
        : LookupTypeDefinition(name, "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName(
                                       "", name, typeParamCount)),
                               TypeKind::Class, Accessibility::Public, compilation, nullptr) {}

    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    void SetProperties(std::vector<const IProperty*> p) { properties_ = std::move(p); }
    void SetFields(std::vector<const IField*> f) { fields_ = std::move(f); }
    void SetEvents(std::vector<const IEvent*> e) { events_ = std::move(e); }
    void SetConstructors(std::vector<const IMethod*> c) { constructors_ = std::move(c); }
    void SetAccessors(std::vector<const IMethod*> a) { accessors_ = std::move(a); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, methods_);
    }
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, properties_);
    }
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, fields_);
    }
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, events_);
    }
    std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, constructors_);
    }
    std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        return applyFilter(filter, accessors_);
    }

private:
    template <typename T>
    static std::vector<const T*> applyFilter(const std::function<bool(const T*)>& filter,
                                             const std::vector<const T*>& members) {
        if (!filter) return members;
        std::vector<const T*> out;
        for (const T* m : members) if (filter(m)) out.push_back(m);
        return out;
    }

    std::vector<const IMethod*> methods_;
    std::vector<const IProperty*> properties_;
    std::vector<const IField*> fields_;
    std::vector<const IEvent*> events_;
    std::vector<const IMethod*> constructors_;
    std::vector<const IMethod*> accessors_;
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const auto kNone = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::None;
const auto kIgnoreInherited = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers;
const auto kReturnDefs = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions;

} // namespace

// ---------------------------------------------------------------------------
// GetMethods: IgnoreInheritedMembers (no ReturnMemberDefinitions) -> SpecializedMethod (the
// routing arm fires, not the D489 delegation).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetMethodsRoutingBuildsSpecialized) {
    auto gen = std::make_shared<TestTypeDefinition>("List", 1, Compilation());
    auto m = std::make_shared<LookupMethod>("Add", Compilation());
    gen->SetMethods({m.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetMethods(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedMethod*>(result[0]), nullptr);
    EXPECT_EQ(result[0]->Name(), "Add");
}

// ---------------------------------------------------------------------------
// GetFields: the routing arm substitutes the ReturnType (T at index 0 -> the type argument).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetFieldsRoutingSubstitutesReturnType) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto tp = std::make_shared<TestSubstTypeParameter>("T", 0);
    auto f = std::make_shared<TestField>("field", tp, Compilation());
    gen->SetFields({f.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetFields(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedField*>(result[0]), nullptr);
    EXPECT_EQ(result[0]->ReturnType().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// GetProperties: the routing arm substitutes the ReturnType (T at index 0 -> String).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetPropertiesRoutingSubstitutesReturnType) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto tp = std::make_shared<TestSubstTypeParameter>("T", 0);
    auto p = std::make_shared<TestProperty>("Item", tp, Compilation());
    gen->SetProperties({p.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetProperties(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedProperty*>(result[0]), nullptr);
    EXPECT_EQ(result[0]->ReturnType().Name(), "String");
}

// ---------------------------------------------------------------------------
// GetEvents: the routing arm builds SpecializedEvent.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetEventsRoutingBuildsSpecialized) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto e = std::make_shared<LookupEvent>("Changed", Object(), Compilation());
    gen->SetEvents({e.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetEvents(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedEvent*>(result[0]), nullptr);
}

// ---------------------------------------------------------------------------
// GetConstructors: the routing arm builds SpecializedMethod for the constructors.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetConstructorsRoutingBuildsSpecialized) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto ctor = std::make_shared<LookupMethod>(".ctor", Compilation());
    gen->SetConstructors({ctor.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetConstructors(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedMethod*>(result[0]), nullptr);
}

// ---------------------------------------------------------------------------
// GetAccessors: the routing arm builds SpecializedMethod for the accessors.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetAccessorsRoutingBuildsSpecialized) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto acc = std::make_shared<LookupMethod>("get_Count", Compilation());
    gen->SetAccessors({acc.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetAccessors(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(dynamic_cast<const SpecializedMethod*>(result[0]), nullptr);
}

// ---------------------------------------------------------------------------
// GetMethods: the caller's filter is applied at return time (a name filter selects).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetMethodsRoutingAppliesFilter) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto m1 = std::make_shared<LookupMethod>("Keep", Compilation());
    auto m2 = std::make_shared<LookupMethod>("Drop", Compilation());
    gen->SetMethods({m1.get(), m2.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetMethods([](const IMethod* m) { return m->Name() == "Keep"; }, kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "Keep");
}

// ---------------------------------------------------------------------------
// GetMethods: the cache is stable -- a second call yields the same Specialized* instances
// (pointer identity), the D477 "type system owns" convention.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetMethodsRoutingCacheIsStable) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    gen->SetMethods({m.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto first = pt->GetMethods(nullptr, kIgnoreInherited);
    auto second = pt->GetMethods(nullptr, kIgnoreInherited);
    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0], second[0]);  // same SpecializedMethod instance (cached)
}

// ---------------------------------------------------------------------------
// GetMembers: the inherited IType::GetMembers composition aggregates the four family routing
// arms -> Specialized* per family.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetMembersRoutingComposesFamilies) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", 1, Compilation());
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    auto p = std::make_shared<TestProperty>("P", Int32(), Compilation());
    auto f = std::make_shared<TestField>("f", Int32(), Compilation());
    auto e = std::make_shared<LookupEvent>("E", Object(), Compilation());
    gen->SetMethods({m.get()});
    gen->SetProperties({p.get()});
    gen->SetFields({f.get()});
    gen->SetEvents({e.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetMembers(nullptr, kIgnoreInherited);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_NE(dynamic_cast<const SpecializedMethod*>(result[0]), nullptr);
    EXPECT_NE(dynamic_cast<const SpecializedProperty*>(result[1]), nullptr);
    EXPECT_NE(dynamic_cast<const SpecializedField*>(result[2]), nullptr);
    EXPECT_NE(dynamic_cast<const SpecializedEvent*>(result[3]), nullptr);
}

// ---------------------------------------------------------------------------
// GetMethods: the ReturnMemberDefinitions arm still delegates (D489 unchanged) -- the
// Specialized* are NOT built when ReturnMemberDefinitions is set.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeRoutingTest, GetMethodsReturnDefsStillDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("List", 1, Compilation());
    auto m = std::make_shared<LookupMethod>("Add", Compilation());
    gen->SetMethods({m.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{String()});

    auto result = pt->GetMethods(nullptr, kReturnDefs | kIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(dynamic_cast<const SpecializedMethod*>(result[0]), nullptr);
    EXPECT_EQ(result[0]->Name(), "Add");
}
