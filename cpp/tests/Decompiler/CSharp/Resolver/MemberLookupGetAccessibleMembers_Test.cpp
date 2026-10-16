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
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `MemberLookup.GetAccessibleMembers` (D503) -- the last of the `MemberLookup` Lookup-region
// public methods. The C# `GetAccessibleMembers(ResolveResult targetResolveResult)` retrieves all
// accessible, non-hidden members + nested type definitions (NOT extension methods). For each base type
// (base-first), it fetches `GetMembers(IgnoreInheritedMembers)` + (if not a type parameter)
// `GetNestedTypes(IgnoreInheritedMembers | ReturnMemberDefinitions)` projected to `GetDefinition()`,
// groups by name, composes `AddNestedTypes`/`AddMembers`, and (if a type parameter)
// `RemoveInterfaceMembersHiddenByClassMembers`; then yields the non-hidden methods, the non-hidden
// non-method, and the nested-type definitions (from `GetDefinition()`).
//
// The tests pin:
//  (a) a type with no members + no nested types -> empty;
//  (b) a member (a field) is yielded (as an `IEntity`);
//  (c) a method is yielded (as an `IEntity`);
//  (d) a nested type definition is yielded (its `GetDefinition()`);
//  (e) members + nested types of the same name are grouped (both yielded).

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMember;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

// A `TestTypeDefinition : LookupTypeDefinition` with a configurable `GetMembers` + `GetNestedTypes`
// (standing in for `MetadataTypeDefinition`'s member-enumeration overrides). Overrides `GetMembers`
// directly (bypassing the `IType::GetMembers` composition) and `GetNestedTypes` (the D492
// `ReturnMemberDefinitions` arm).
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, const ICompilation& compilation)
        : LookupTypeDefinition(std::move(name), "",
              ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                  ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName("", name, 0)),
              TypeKind::Class, Accessibility::Public, compilation, nullptr) {}

    void SetMembers(std::vector<const IMember*> m) { members_ = std::move(m); }
    void SetNestedTypes(std::vector<ITypePtr> n) { nestedTypes_ = std::move(n); }

    std::vector<const IMember*> GetMembers(
        std::function<bool(const IMember*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        if (!filter) return members_;
        std::vector<const IMember*> out;
        for (const IMember* m : members_) if (filter(m)) out.push_back(m);
        return out;
    }

    std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        (void)options;
        if (!filter) return nestedTypes_;
        std::vector<ITypePtr> out;
        for (const ITypePtr& t : nestedTypes_) {
            if (filter(t->GetDefinition())) out.push_back(t);
        }
        return out;
    }

private:
    std::vector<const IMember*> members_;
    std::vector<ITypePtr> nestedTypes_;
};

std::shared_ptr<ResolveResult> MakeTarget(ITypePtr type) {
    return std::make_shared<TypeResolveResult>(std::move(type));
}

const IMember* MakeField(std::string name) {
    static std::vector<std::shared_ptr<LookupMember>> keep;
    auto m = std::make_shared<LookupMember>(std::move(name), SymbolKind::Field, Object(), Compilation());
    keep.push_back(m);
    return m.get();
}

const IMethod* MakeMethod(std::string name) {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

} // namespace

// ---------------------------------------------------------------------------
// GetAccessibleMembers: a type with no members + no nested types -> empty.
// ---------------------------------------------------------------------------
TEST(MemberLookupGetAccessibleMembersTest, EmptyYieldsEmpty) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.GetAccessibleMembers(*target);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// GetAccessibleMembers: a field member is yielded (as an IEntity).
// ---------------------------------------------------------------------------
TEST(MemberLookupGetAccessibleMembersTest, FieldMemberYielded) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto f = MakeField("F");
    def->SetMembers({f});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.GetAccessibleMembers(*target);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), static_cast<const IEntity*>(f));
}

// ---------------------------------------------------------------------------
// GetAccessibleMembers: a method member is yielded (as an IEntity).
// ---------------------------------------------------------------------------
TEST(MemberLookupGetAccessibleMembersTest, MethodMemberYielded) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto m = MakeMethod("M");
    def->SetMembers({m});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.GetAccessibleMembers(*target);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), static_cast<const IEntity*>(m));
}

// ---------------------------------------------------------------------------
// GetAccessibleMembers: a nested type definition is yielded (its GetDefinition()).
// ---------------------------------------------------------------------------
TEST(MemberLookupGetAccessibleMembersTest, NestedTypeDefinitionYielded) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto nested = std::make_shared<TestTypeDefinition>("Nested", Compilation());
    def->SetNestedTypes({nested});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.GetAccessibleMembers(*target);
    ASSERT_EQ(result.size(), 1u);
    // The nested type's GetDefinition() is `nested` (the stub's GetDefinition returns `this`).
    EXPECT_EQ(result[0].get(), static_cast<const IEntity*>(nested.get()));
}

// ---------------------------------------------------------------------------
// GetAccessibleMembers: members + nested types of the same name are grouped (both yielded).
// ---------------------------------------------------------------------------
TEST(MemberLookupGetAccessibleMembersTest, SameNameMemberAndNestedTypeBothYielded) {
    auto def = std::make_shared<TestTypeDefinition>("Foo", Compilation());
    auto f = MakeField("X");  // a member named "X"
    auto nested = std::make_shared<TestTypeDefinition>("X", Compilation());  // a nested type named "X"
    def->SetMembers({f});
    def->SetNestedTypes({nested});
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.GetAccessibleMembers(*target);
    // Both the field and the nested-type definition are yielded (grouped by name "X").
    ASSERT_EQ(result.size(), 2u);
    std::vector<const IEntity*> yielded = {result[0].get(), result[1].get()};
    EXPECT_NE(std::find(yielded.begin(), yielded.end(), static_cast<const IEntity*>(f)), yielded.end());
    EXPECT_NE(std::find(yielded.begin(), yielded.end(), static_cast<const IEntity*>(nested.get())),
              yielded.end());
}
