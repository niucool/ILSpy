// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software") to deal in the
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
// PURPOSE NONINFRINGEMENT. CAUSED BY ON THE WHICHEVER THEORY OF LIABILITY, WHETHER IN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `IntersectionType` (IntersectionType.cs) -- the intersection of several types
// (the synthetic type the `TypeInference` `ImprovedReturnAllResults` fixing algorithm
// produces). It is the prerequisite the `Fix` / `FindTypeInBounds`
// `IntersectionType.Create` arms consume (`TypeInferenceHelpers.cpp`'s two arms, now
// wired); it landed as a tested-but-not-yet-wired foundation (the D63 LongSet /
// D533 GetDelegateInvokeMethod precedent: port the leaf the next in-order piece needs
// ahead of it, so the wiring iteration is separate and lower-risk).
//
// The tests pin:
//  (a) the `Create` factory: empty -> `UnknownType()`, singleton -> the type itself
//      (pointer identity), the `IType::Equals` dedup keeping the FIRST occurrence, the
//      `std::invalid_argument` on a null entry (the C# `ArgumentNullException`), and the
//      order preservation;
//  (b) the type surface: `Kind` / `TypeParameterCount` (the `AbstractType` 0), the
//      " & " `Name` / `ReflectionName` joins, the `IsReferenceType` first-definite-value
//      fold with the indeterminate-skip, the element-wise structural `Equals`, and
//      `DirectBaseTypes` == the constituents;
//  (c) the member families: each override routes through `GetMembersHelper` over the
//      non-interface base types (the constituents), composes `FilterNonStatic` with the
//      caller's filter (static members excluded), skips interface-kind constituents (the
//      `GetNonInterfaceBaseTypes` semantics), and returns the EMPTY set for an
//      `IgnoreInheritedMembers`-bearing call -- the documented safe fallback for the C#
//      `GetMembersHelper` re-entry landmine (the C# unconditionally stack-overflows for
//      those shapes; the empty set is what its control flow reduces to when the recursion
//      terminates -- the header comment's finding (a)).

#include "Decompiler/TypeSystem/IntersectionType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
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
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IntersectionType;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupEvent;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

const auto kNone = GetMemberOptions::None;
const auto kIgnoreInherited = GetMemberOptions::IgnoreInheritedMembers;
const auto kBothDeclaredFlags =
    GetMemberOptions::IgnoreInheritedMembers | GetMemberOptions::ReturnMemberDefinitions;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A `LookupTypeDefinition` holding a configurable member list per family, overriding the
// `IType` member-enumeration virtuals to apply the caller's filter (standing in for the
// not-yet-ported `MetadataTypeDefinition`; the `GetMembersHelper_Test` `TestTypeDefinition`
// precedent). `GetMembers` is NOT overridden -- the `GetMembersHelper` `GetMembersImpl`
// composes the four families directly, never calling it.
class MemberHost : public LookupTypeDefinition {
public:
    MemberHost(std::string name, ::ILSpy::Decompiler::TypeSystem::TypeKind kind)
        : LookupTypeDefinition(name, "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   TopLevelTypeName("", name)),
                               kind, ::ILSpy::Decompiler::TypeSystem::Accessibility::Public,
                               ::Compilation(), nullptr) {}

    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    void SetProperties(std::vector<const IProperty*> p) { properties_ = std::move(p); }
    void SetFields(std::vector<const IField*> f) { fields_ = std::move(f); }
    void SetEvents(std::vector<const IEvent*> e) { events_ = std::move(e); }
    void SetAccessors(std::vector<const IMethod*> a) { accessors_ = std::move(a); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        return applyFilter(filter, methods_);
    }
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        return applyFilter(filter, properties_);
    }
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        return applyFilter(filter, fields_);
    }
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        return applyFilter(filter, events_);
    }
    std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        return applyFilter(filter, accessors_);
    }

private:
    template <typename T>
    static std::vector<const T*> applyFilter(const std::function<bool(const T*)>& filter,
                                             const std::vector<const T*>& members) {
        if (!filter) return members;
        std::vector<const T*> out;
        for (const T* m : members)
            if (filter(m)) out.push_back(m);
        return out;
    }

    std::vector<const IMethod*> methods_;
    std::vector<const IProperty*> properties_;
    std::vector<const IField*> fields_;
    std::vector<const IEvent*> events_;
    std::vector<const IMethod*> accessors_;
};

// A minimal `IProperty` stub (the `GetMembersHelper_Test` `TestProperty` precedent -- the
// shared `LookupStubs.hpp` has no property stub). `IsStatic` is configurable so the
// `FilterNonStatic` crux can pin static-member exclusion on a second family too.
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
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
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return isStatic_; }
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

    void SetStatic(bool v) { isStatic_ = v; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    bool isStatic_ = false;
    mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// A minimal `IField` stub (the `GetMembersHelper_Test` `TestField` precedent).
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
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
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
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

} // namespace

// ---- Create: the empty / singleton / intersection / dedup / throw / order arms ----

TEST(IntersectionTypeTest, CreateEmptyListYieldsUnknownType) {
    ITypePtr result = IntersectionType::Create({});
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

TEST(IntersectionTypeTest, CreateSingletonListYieldsTheTypeItself) {
    ITypePtr only = Int32();
    ITypePtr result = IntersectionType::Create({only});
    ASSERT_NE(result, nullptr);
    // The C# returns arr[0] verbatim -- pointer identity with the single input.
    EXPECT_EQ(result.get(), only.get());
}

TEST(IntersectionTypeTest, CreateTwoDistinctTypesBuildsIntersection) {
    ITypePtr result = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Intersection);
    const auto* intersection = dynamic_cast<const IntersectionType*>(result.get());
    ASSERT_NE(intersection, nullptr);
    EXPECT_EQ(intersection->Types().size(), 2u);
}

TEST(IntersectionTypeTest, CreateDeduplicatesEqualTypesKeepingFirst) {
    ITypePtr first = Int32();
    ITypePtr secondEqualInstance = Int32();
    ASSERT_NE(first.get(), secondEqualInstance.get()); // distinct instances, Equals-true
    ITypePtr result = IntersectionType::Create({first, secondEqualInstance});
    ASSERT_NE(result, nullptr);
    // The C# `Distinct()` keeps the FIRST occurrence: the singleton is `first` itself.
    EXPECT_EQ(result.get(), first.get());
}

TEST(IntersectionTypeTest, CreateDedupsAmongDistinctToo) {
    ITypePtr intA = Int32();
    ITypePtr intB = Int32();
    ITypePtr str = String();
    ITypePtr result = IntersectionType::Create({intA, intB, str});
    ASSERT_NE(result, nullptr);
    const auto* intersection = dynamic_cast<const IntersectionType*>(result.get());
    ASSERT_NE(intersection, nullptr);
    ASSERT_EQ(intersection->Types().size(), 2u);
    EXPECT_EQ(intersection->Types()[0].get(), intA.get());
    EXPECT_EQ(intersection->Types()[1].get(), str.get());
}

TEST(IntersectionTypeTest, CreateThrowsOnNullEntry) {
    EXPECT_THROW(IntersectionType::Create({Int32(), ITypePtr()}), std::invalid_argument);
    EXPECT_THROW(IntersectionType::Create({ITypePtr()}), std::invalid_argument);
}

TEST(IntersectionTypeTest, CreatePreservesInputOrder) {
    ITypePtr a = Int32();
    ITypePtr b = String();
    ITypePtr result = IntersectionType::Create({a, b});
    ASSERT_NE(result, nullptr);
    const auto* intersection = dynamic_cast<const IntersectionType*>(result.get());
    ASSERT_NE(intersection, nullptr);
    ASSERT_EQ(intersection->Types().size(), 2u);
    EXPECT_EQ(intersection->Types()[0].get(), a.get());
    EXPECT_EQ(intersection->Types()[1].get(), b.get());
}

// ---- The type surface ----

TEST(IntersectionTypeTest, NameJoinsWithAmpersand) {
    ITypePtr result = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Name(), "Int32 & String");
}

TEST(IntersectionTypeTest, ReflectionNameJoinsWithAmpersand) {
    ITypePtr result = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->ReflectionName(), "System.Int32 & System.String");
}

TEST(IntersectionTypeTest, TypeParameterCountIsZero) {
    // The C# does not override the AbstractType default (0).
    ITypePtr result = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->TypeParameterCount(), 0);
}

TEST(IntersectionTypeTest, IsReferenceTypeReturnsFirstDefiniteValue) {
    // Int32 is a definite value type (false): it wins over the later String (true).
    ITypePtr result = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(result, nullptr);
    auto isRef = result->IsReferenceType();
    ASSERT_TRUE(isRef.has_value());
    EXPECT_FALSE(*isRef);
}

TEST(IntersectionTypeTest, IsReferenceTypeSkipsIndeterminateConstituent) {
    // A plain LookupTypeDefinition is indeterminate (nullopt): skipped, String's true wins.
    auto indeterminate = std::make_shared<LookupTypeDefinition>(
        "Indeterminate", "", FullTypeName(TopLevelTypeName("", "Indeterminate")),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    ITypePtr result = IntersectionType::Create({indeterminate, String()});
    ASSERT_NE(result, nullptr);
    auto isRef = result->IsReferenceType();
    ASSERT_TRUE(isRef.has_value());
    EXPECT_TRUE(*isRef);
}

TEST(IntersectionTypeTest, IsReferenceTypeYieldsNulloptWhenAllIndeterminate) {
    auto a = std::make_shared<LookupTypeDefinition>(
        "A", "", FullTypeName(TopLevelTypeName("", "A")),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    auto b = std::make_shared<LookupTypeDefinition>(
        "B", "", FullTypeName(TopLevelTypeName("", "B")),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    ITypePtr result = IntersectionType::Create({a, b});
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->IsReferenceType().has_value());
}

TEST(IntersectionTypeTest, EqualsIsElementWiseStructural) {
    // Two creations over DISTINCT instances of the same constituent types are equal:
    // the element-wise comparison goes through IType::Equals (KnownType compares by code).
    ITypePtr first = IntersectionType::Create({Int32(), String()});
    ITypePtr second = IntersectionType::Create({Int32(), String()});
    ASSERT_NE(first.get(), second.get());
    EXPECT_TRUE(first->Equals(*second));
    EXPECT_TRUE(second->Equals(*first));
}

TEST(IntersectionTypeTest, EqualsIsOrderSensitive) {
    ITypePtr ab = IntersectionType::Create({Int32(), String()});
    ITypePtr ba = IntersectionType::Create({String(), Int32()});
    ASSERT_NE(ab.get(), ba.get());
    EXPECT_FALSE(ab->Equals(*ba));
}

TEST(IntersectionTypeTest, EqualsDifferentCountIsFalse) {
    // The third constituent must be genuinely distinct (an `Int32()` third entry would
    // dedup against the first under `IType::Equals`, collapsing to the same 2-constituent
    // intersection -- the `CreateDeduplicatesEqualTypesKeepingFirst` behavior).
    ITypePtr ab = IntersectionType::Create({Int32(), String()});
    ITypePtr abc = IntersectionType::Create({Int32(), String(),
                                            std::make_shared<KnownType>(KnownTypeCode::Int64)});
    const auto* abcIntersection = dynamic_cast<const IntersectionType*>(abc.get());
    ASSERT_NE(abcIntersection, nullptr);
    ASSERT_EQ(abcIntersection->Types().size(), 3u);
    EXPECT_FALSE(ab->Equals(*abc));
}

TEST(IntersectionTypeTest, EqualsDifferentKindIsFalse) {
    // IType::Equals short-circuits on the Kind: an intersection never equals a constituent.
    ITypePtr ab = IntersectionType::Create({Int32(), String()});
    EXPECT_FALSE(ab->Equals(*Int32()));
}

TEST(IntersectionTypeTest, DirectBaseTypesReturnsTheConstituents) {
    ITypePtr a = Int32();
    ITypePtr b = String();
    ITypePtr result = IntersectionType::Create({a, b});
    ASSERT_NE(result, nullptr);
    std::vector<ITypePtr> bases = result->DirectBaseTypes();
    ASSERT_EQ(bases.size(), 2u);
    EXPECT_EQ(bases[0].get(), a.get());
    EXPECT_EQ(bases[1].get(), b.get());
}

// ---- The member families ----

TEST(IntersectionTypeTest, GetMethodsUnionsConstituentMethods) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    auto m2 = std::make_shared<LookupMethod>("M2", Compilation());
    hostA->SetMethods({m1.get()});
    hostB->SetMethods({m2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods(nullptr, kNone);
    ASSERT_EQ(methods.size(), 2u);
    // The results alias the constituents' declared members -- pointer identity.
    EXPECT_EQ(methods[0], static_cast<const IMethod*>(m1.get()));
    EXPECT_EQ(methods[1], static_cast<const IMethod*>(m2.get()));
}

TEST(IntersectionTypeTest, GetMethodsExcludesStaticMethods) {
    // FilterNonStatic: the static member of HostA is excluded; the instance member survives.
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto staticMethod = std::make_shared<LookupMethod>("StaticMethod", Compilation());
    staticMethod->SetStatic(true);
    auto instanceMethod = std::make_shared<LookupMethod>("InstanceMethod", Compilation());
    hostA->SetMethods({staticMethod.get(), instanceMethod.get()});
    hostB->SetMethods({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods(nullptr, kNone);
    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(methods[0], static_cast<const IMethod*>(instanceMethod.get()));
}

TEST(IntersectionTypeTest, GetMethodsAppliesCallerFilterTogetherWithNonStatic) {
    // The caller's filter composes WITH FilterNonStatic: a name-matching static method is
    // still excluded, a name-matching instance method is kept, a non-matching one dropped.
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto staticMatch = std::make_shared<LookupMethod>("Match", Compilation());
    staticMatch->SetStatic(true);
    auto instanceMatch = std::make_shared<LookupMethod>("Match", Compilation());
    auto instanceOther = std::make_shared<LookupMethod>("Other", Compilation());
    hostA->SetMethods({staticMatch.get(), instanceMatch.get(), instanceOther.get()});
    hostB->SetMethods({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods(
        [](const IMethod* m) { return m->Name() == "Match"; }, kNone);
    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(methods[0], static_cast<const IMethod*>(instanceMatch.get()));
}

TEST(IntersectionTypeTest, GetMethodsSkipsInterfaceConstituents) {
    // GetNonInterfaceBaseTypes skips interface-kind base types of a non-interface,
    // non-type-parameter input: the IntersectionType's Interface constituent contributes
    // nothing (faithful to the C# -- the collector's SkipImplementedInterfaces guard).
    auto classHost = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto interfaceHost = std::make_shared<MemberHost>("IHost", TypeKind::Interface);
    auto classMethod = std::make_shared<LookupMethod>("ClassMethod", Compilation());
    auto interfaceMethod = std::make_shared<LookupMethod>("InterfaceMethod", Compilation());
    classHost->SetMethods({classMethod.get()});
    interfaceHost->SetMethods({interfaceMethod.get()});
    ITypePtr result = IntersectionType::Create({classHost, interfaceHost});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods(nullptr, kNone);
    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(methods[0], static_cast<const IMethod*>(classMethod.get()));
}

TEST(IntersectionTypeTest, GetPropertiesUnionsConstituentProperties) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto p1 = std::make_shared<TestProperty>("P1", Int32(), Compilation());
    auto p2 = std::make_shared<TestProperty>("P2", String(), Compilation());
    hostA->SetProperties({p1.get()});
    hostB->SetProperties({p2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IProperty*> properties = result->GetProperties(nullptr, kNone);
    ASSERT_EQ(properties.size(), 2u);
    EXPECT_EQ(properties[0], static_cast<const IProperty*>(p1.get()));
    EXPECT_EQ(properties[1], static_cast<const IProperty*>(p2.get()));
}

TEST(IntersectionTypeTest, GetPropertiesExcludesStaticProperties) {
    // The FilterNonStatic composition is per-family: a static property is excluded too.
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto staticProp = std::make_shared<TestProperty>("StaticProp", Int32(), Compilation());
    staticProp->SetStatic(true);
    auto instanceProp = std::make_shared<TestProperty>("InstanceProp", Int32(), Compilation());
    hostA->SetProperties({staticProp.get(), instanceProp.get()});
    hostB->SetProperties({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IProperty*> properties = result->GetProperties(nullptr, kNone);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(properties[0], static_cast<const IProperty*>(instanceProp.get()));
}

TEST(IntersectionTypeTest, GetFieldsUnionsConstituentFields) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto f1 = std::make_shared<TestField>("F1", Int32(), Compilation());
    auto f2 = std::make_shared<TestField>("F2", String(), Compilation());
    hostA->SetFields({f1.get()});
    hostB->SetFields({f2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IField*> fields = result->GetFields(nullptr, kNone);
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], static_cast<const IField*>(f1.get()));
    EXPECT_EQ(fields[1], static_cast<const IField*>(f2.get()));
}

TEST(IntersectionTypeTest, GetEventsUnionsConstituentEvents) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto e1 = std::make_shared<LookupEvent>("E1", Int32(), Compilation());
    auto e2 = std::make_shared<LookupEvent>("E2", Int32(), Compilation());
    hostA->SetEvents({e1.get()});
    hostB->SetEvents({e2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IEvent*> events = result->GetEvents(nullptr, kNone);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0], static_cast<const IEvent*>(e1.get()));
    EXPECT_EQ(events[1], static_cast<const IEvent*>(e2.get()));
}

TEST(IntersectionTypeTest, GetMembersComposesAllFourFamilies) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    auto p1 = std::make_shared<TestProperty>("P1", Int32(), Compilation());
    auto f1 = std::make_shared<TestField>("F1", Int32(), Compilation());
    auto e1 = std::make_shared<LookupEvent>("E1", Int32(), Compilation());
    auto m2 = std::make_shared<LookupMethod>("M2", Compilation());
    hostA->SetMethods({m1.get()});
    hostA->SetProperties({p1.get()});
    hostA->SetFields({f1.get()});
    hostA->SetEvents({e1.get()});
    hostB->SetMethods({m2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMember*> members = result->GetMembers(nullptr, kNone);
    ASSERT_EQ(members.size(), 5u);
    // The composition order: methods, then properties, then fields, then events,
    // per base type in GetNonInterfaceBaseTypes order (hostA first, hostB second).
    EXPECT_EQ(members[0], static_cast<const IMember*>(m1.get()));
    EXPECT_EQ(members[1], static_cast<const IMember*>(p1.get()));
    EXPECT_EQ(members[2], static_cast<const IMember*>(f1.get()));
    EXPECT_EQ(members[3], static_cast<const IMember*>(e1.get()));
    EXPECT_EQ(members[4], static_cast<const IMember*>(m2.get()));
}

TEST(IntersectionTypeTest, GetAccessorsUnionsConstituentAccessors) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto a1 = std::make_shared<LookupMethod>("get_P1", Compilation());
    auto a2 = std::make_shared<LookupMethod>("get_P2", Compilation());
    hostA->SetAccessors({a1.get()});
    hostB->SetAccessors({a2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> accessors = result->GetAccessors(nullptr, kNone);
    ASSERT_EQ(accessors.size(), 2u);
    EXPECT_EQ(accessors[0], static_cast<const IMethod*>(a1.get()));
    EXPECT_EQ(accessors[1], static_cast<const IMethod*>(a2.get()));
}

TEST(IntersectionTypeTest, GetMembersAppliesCallerFilter) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("Keep", Compilation());
    auto p1 = std::make_shared<TestProperty>("Drop", Int32(), Compilation());
    auto m2 = std::make_shared<LookupMethod>("Keep", Compilation());
    hostA->SetMethods({m1.get()});
    hostA->SetProperties({p1.get()});
    hostB->SetMethods({m2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMember*> members = result->GetMembers(
        [](const IMember* mem) { return mem->Name() == "Keep"; }, kNone);
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(members[0], static_cast<const IMember*>(m1.get()));
    EXPECT_EQ(members[1], static_cast<const IMember*>(m2.get()));
}

// ---- The IgnoreInheritedMembers re-entry landmine guard ----

TEST(IntersectionTypeTest, GetMethodsIgnoreInheritedMembersYieldsEmpty) {
    // The C# StackOverflowException landmine: with IgnoreInheritedMembers set, the C#
    // override re-enters GetMembersHelper's *Impl -> baseType.GetMethods(options |
    // declaredMembers) -> the override -> ..., unconditionally. The port returns the
    // empty set (the semantically-faithful declared-members answer: an intersection
    // declares none of its own).
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    hostA->SetMethods({m1.get()});
    hostB->SetMethods({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->GetMethods(nullptr, kIgnoreInherited).empty());
    // The both-flags shape (the exact re-entry the helper's *Impl bodies perform) is the
    // same guarded call.
    EXPECT_TRUE(result->GetMethods(nullptr, kBothDeclaredFlags).empty());
}

TEST(IntersectionTypeTest, GetMembersIgnoreInheritedMembersYieldsEmpty) {
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    hostA->SetMethods({m1.get()});
    hostB->SetMethods({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->GetMembers(nullptr, kIgnoreInherited).empty());
    EXPECT_TRUE(result->GetProperties(nullptr, kIgnoreInherited).empty());
    EXPECT_TRUE(result->GetFields(nullptr, kIgnoreInherited).empty());
    EXPECT_TRUE(result->GetEvents(nullptr, kIgnoreInherited).empty());
    EXPECT_TRUE(result->GetAccessors(nullptr, kIgnoreInherited).empty());
}

// ---- The generic-method GetMethods overload ----

TEST(IntersectionTypeTest, GetMethodsEmptyTypeArgumentsBehavesLikeSimpleOverload) {
    // The helper's entry treats an EMPTY typeArguments vector as "no type arguments"
    // (the C# `typeArguments != null && Count > 0` guard): the routing matches the simple
    // overload.
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    auto m2 = std::make_shared<LookupMethod>("M2", Compilation());
    hostA->SetMethods({m1.get()});
    hostB->SetMethods({m2.get()});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods(std::vector<ITypePtr>{}, nullptr, kNone);
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0], static_cast<const IMethod*>(m1.get()));
    EXPECT_EQ(methods[1], static_cast<const IMethod*>(m2.get()));
}

TEST(IntersectionTypeTest, GetMethodsTypeArgumentsFilterArity) {
    // A NON-EMPTY typeArguments vector composes FilterTypeParameterCount(count) into the
    // filter: the stub methods (0 type parameters, the LookupMethod default) do not match
    // a 1-argument request, so nothing is returned.
    auto hostA = std::make_shared<MemberHost>("HostA", TypeKind::Class);
    auto hostB = std::make_shared<MemberHost>("HostB", TypeKind::Class);
    auto m1 = std::make_shared<LookupMethod>("M1", Compilation());
    hostA->SetMethods({m1.get()});
    hostB->SetMethods({});
    ITypePtr result = IntersectionType::Create({hostA, hostB});
    ASSERT_NE(result, nullptr);
    std::vector<const IMethod*> methods = result->GetMethods({Int32()}, nullptr, kNone);
    EXPECT_TRUE(methods.empty());
}
