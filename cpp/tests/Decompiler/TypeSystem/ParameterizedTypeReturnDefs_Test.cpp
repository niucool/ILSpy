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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `ParameterizedType` member-enumeration `ReturnMemberDefinitions` arm
// (D489). The C# `ParameterizedType.GetMethods(filter, options)` is:
//   if (options & ReturnMemberDefinitions) return genericType.GetMethods(filter, options);
//   else return GetMembersHelper.GetMethods(this, filter, options);
// This leaf ports the `ReturnMemberDefinitions` arm (delegate to the generic type,
// passing `options` through unchanged); the else (routing) arm is deferred to a later
// leaf (it returns the inherited empty default for now). `GetMembers` is NOT overridden
// -- the inherited `IType::GetMembers` composition (GetMethods + GetProperties +
// GetFields + GetEvents) is behaviorally equivalent to the C# override's
// `ReturnMemberDefinitions` arm, since each delegated family yields the generic
// type's family and the composition reconstructs `genericType.GetMembers`.
//
// The tests pin:
//  (a) the `ReturnMemberDefinitions` arm delegates to `genericType.GetXxx(filter,
//      options)`, passing the caller's `options` THROUGH unchanged (not `options |
//      declaredMembers` -- the C# passes `options` verbatim);
//  (b) the arm applies the caller's filter (a null filter passes everything);
//  (c) the non-`ReturnMemberDefinitions` call returns empty (the deferred routing
//      arm -- documents the deferral);
//  (d) the inherited `GetMembers` composes the delegated families (the
//      `ReturnMemberDefinitions` arm).

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
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupEvent;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal `IField` stub (a field definition with a name + return type). The full
// `IMember`/`IVariable`/`IField` surface is overridden; the two-`ISymbol`-subobject
// diamond is disambiguated by the single `SymbolKind`/`Name` overrides (the IField
// header comment convention).
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

// A `TestTypeDefinition : LookupTypeDefinition` that holds configurable member vectors
// for each family and records the `GetMemberOptions` it received on the last call to
// each family (so the test can verify the `ParameterizedType` delegation passes
// `options` through unchanged). The override returns the configured members regardless
// of `options`/`filter` -- it stands in for the not-yet-ported `MetadataTypeDefinition`
// member-enumeration overrides (which enumerate the declared members).
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, ::ILSpy::Decompiler::TypeSystem::TypeKind kind,
                       int typeParamCount, const ICompilation& compilation)
        : LookupTypeDefinition(std::move(name), "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName(
                                       "", name, typeParamCount)),
                               kind, Accessibility::Public, compilation, nullptr) {}

    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    void SetProperties(std::vector<const IProperty*> p) { properties_ = std::move(p); }
    void SetFields(std::vector<const IField*> f) { fields_ = std::move(f); }
    void SetEvents(std::vector<const IEvent*> e) { events_ = std::move(e); }
    void SetConstructors(std::vector<const IMethod*> c) { constructors_ = std::move(c); }
    void SetAccessors(std::vector<const IMethod*> a) { accessors_ = std::move(a); }

    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastMethodsOptions() const { return lastMethodsOpts_; }
    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastPropertiesOptions() const { return lastPropertiesOpts_; }
    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastFieldsOptions() const { return lastFieldsOpts_; }
    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastEventsOptions() const { return lastEventsOpts_; }
    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastConstructorsOptions() const { return lastCtorsOpts_; }
    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastAccessorsOptions() const { return lastAccessorsOpts_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastMethodsOpts_ = options;
        if (!filter) return methods_;
        std::vector<const IMethod*> out;
        for (const auto* m : methods_) if (filter(m)) out.push_back(m);
        return out;
    }
    std::vector<const IMethod*> GetMethods(
        const std::vector<ITypePtr>& /*typeArguments*/,
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastMethodsOpts_ = options;
        if (!filter) return methods_;
        std::vector<const IMethod*> out;
        for (const auto* m : methods_) if (filter(m)) out.push_back(m);
        return out;
    }
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastPropertiesOpts_ = options;
        if (!filter) return properties_;
        std::vector<const IProperty*> out;
        for (const auto* p : properties_) if (filter(p)) out.push_back(p);
        return out;
    }
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastFieldsOpts_ = options;
        if (!filter) return fields_;
        std::vector<const IField*> out;
        for (const auto* f : fields_) if (filter(f)) out.push_back(f);
        return out;
    }
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastEventsOpts_ = options;
        if (!filter) return events_;
        std::vector<const IEvent*> out;
        for (const auto* e : events_) if (filter(e)) out.push_back(e);
        return out;
    }
    std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastCtorsOpts_ = options;
        if (!filter) return constructors_;
        std::vector<const IMethod*> out;
        for (const auto* c : constructors_) if (filter(c)) out.push_back(c);
        return out;
    }
    std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastAccessorsOpts_ = options;
        if (!filter) return accessors_;
        std::vector<const IMethod*> out;
        for (const auto* a : accessors_) if (filter(a)) out.push_back(a);
        return out;
    }

private:
    std::vector<const IMethod*> methods_;
    std::vector<const IProperty*> properties_;
    std::vector<const IField*> fields_;
    std::vector<const IEvent*> events_;
    std::vector<const IMethod*> constructors_;
    std::vector<const IMethod*> accessors_;
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastMethodsOpts_{};
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastPropertiesOpts_{};
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastFieldsOpts_{};
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastEventsOpts_{};
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastCtorsOpts_{};
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastAccessorsOpts_{};
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const auto kReturnDefs = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions;
const auto kIgnoreInherited = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers;
const auto kReturnDefsIgnoreInherited = kReturnDefs | kIgnoreInherited;

} // namespace

// ---------------------------------------------------------------------------
// GetMethods: the ReturnMemberDefinitions arm delegates to genericType.GetMethods,
// passing options through unchanged.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetMethodsDelegatesWithOptionsThrough) {
    auto gen = std::make_shared<TestTypeDefinition>("List", TypeKind::Class, 1, Compilation());
    auto m1 = std::make_shared<LookupMethod>("Add", Compilation());
    auto m2 = std::make_shared<LookupMethod>("Remove", Compilation());
    gen->SetMethods({m1.get(), m2.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetMethods(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0]->Name(), "Add");
    EXPECT_EQ(result[1]->Name(), "Remove");
    // The delegation passes `options` THROUGH unchanged (the C# `genericType.GetMethods(filter, options)`).
    EXPECT_EQ(gen->LastMethodsOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetMethods: the arm applies the caller's filter (a non-null filter selects).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetMethodsAppliesFilter) {
    auto gen = std::make_shared<TestTypeDefinition>("List", TypeKind::Class, 1, Compilation());
    auto m1 = std::make_shared<LookupMethod>("Add", Compilation());
    auto m2 = std::make_shared<LookupMethod>("Remove", Compilation());
    gen->SetMethods({m1.get(), m2.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetMethods([](const IMethod* m) { return m->Name() == "Add"; },
                                 kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "Add");
}

// ---------------------------------------------------------------------------
// GetMethods: the non-ReturnMemberDefinitions call returns empty (the deferred
// routing arm -- documents the deferral).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetMethodsWithoutReturnDefsIsDeferredEmpty) {
    auto gen = std::make_shared<TestTypeDefinition>("List", TypeKind::Class, 1, Compilation());
    auto m = std::make_shared<LookupMethod>("Add", Compilation());
    gen->SetMethods({m.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetMethods(nullptr, ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::None);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// GetProperties: the ReturnMemberDefinitions arm delegates to genericType.GetProperties.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetPropertiesDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto p = std::make_shared<TestProperty>("Count", Int32(), Compilation());
    gen->SetProperties({p.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetProperties(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "Count");
    EXPECT_EQ(gen->LastPropertiesOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetFields: the ReturnMemberDefinitions arm delegates to genericType.GetFields.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetFieldsDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto f = std::make_shared<TestField>("count", Int32(), Compilation());
    gen->SetFields({f.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetFields(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "count");
    EXPECT_EQ(gen->LastFieldsOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetEvents: the ReturnMemberDefinitions arm delegates to genericType.GetEvents.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetEventsDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto e = std::make_shared<LookupEvent>("Changed", Object(), Compilation());
    gen->SetEvents({e.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetEvents(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "Changed");
    EXPECT_EQ(gen->LastEventsOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetConstructors: the ReturnMemberDefinitions arm delegates to
// genericType.GetConstructors.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetConstructorsDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto ctor = std::make_shared<LookupMethod>(".ctor", Compilation());
    gen->SetConstructors({ctor.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetConstructors(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), ".ctor");
    EXPECT_EQ(gen->LastConstructorsOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetAccessors: the ReturnMemberDefinitions arm delegates to genericType.GetAccessors.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetAccessorsDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto acc = std::make_shared<LookupMethod>("get_Count", Compilation());
    gen->SetAccessors({acc.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetAccessors(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Name(), "get_Count");
    EXPECT_EQ(gen->LastAccessorsOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetMembers: the inherited composition aggregates the delegated families (the
// ReturnMemberDefinitions arm). `pt->GetMembers(filter, ReturnMemberDefinitions |
// IgnoreInheritedMembers)` composes `pt->GetMethods + pt->GetProperties +
// pt->GetFields + pt->GetEvents`, each delegating to the generic type's family.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeReturnDefsTest, GetMembersComposesDelegatedFamilies) {
    auto gen = std::make_shared<TestTypeDefinition>("Foo", TypeKind::Class, 1, Compilation());
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    auto p = std::make_shared<TestProperty>("P", Int32(), Compilation());
    auto f = std::make_shared<TestField>("f", Int32(), Compilation());
    auto e = std::make_shared<LookupEvent>("E", Object(), Compilation());
    gen->SetMethods({m.get()});
    gen->SetProperties({p.get()});
    gen->SetFields({f.get()});
    gen->SetEvents({e.get()});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetMembers(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 4u);
    std::vector<std::string> names;
    for (const auto* mem : result) names.push_back(mem->Name());
    EXPECT_NE(std::find(names.begin(), names.end(), "M"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "P"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "f"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "E"), names.end());
}
