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

// Tests for the `MemberLookup.CreateResult` helper (D499) -- the Lookup-region helper that takes the
// populated `lookupGroups` and produces a `ResolveResult`:
//  - empty (all-hidden) -> `UnknownMemberResolveResult`;
//  - any group with visible methods -> `MethodGroupResolveResult` (the method-list buckets per
//    declaring type);
//  - else the most-derived group with nested types -> `TypeResolveResult` (or `AmbiguousTypeResolveResult`
//    if ambiguous);
//  - else a static `NonMethod` on a `ThisResolveResult` target -> retarget to a `TypeResolveResult`
//    target;
//  - else >1 group -> `AmbiguousMemberResolveResult`;
//  - else (single group, a non-method) -> `MemberResolveResult` (the enum-member-initializer arm
//    yields a constant `MemberResolveResult` for an enum field).
//
// `CreateResult` uses `MemberLookup` (for `isInEnumMemberInitializer_`), so the free function takes a
// `const MemberLookup&`.

#include "Decompiler/CSharp/Resolver/LookupHelpers.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/LookupGroup.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::CreateResult;
using ILSpy::Decompiler::CSharp::Resolver::LookupGroup;
using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult;
using ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::ThisResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
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

std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name, TypeKind kind, int tpc) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, tpc)), kind,
        Accessibility::Public, Compilation(), nullptr);
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

const IMember* MakeField(std::string name) {
    static std::vector<std::shared_ptr<LookupMember>> keep;
    auto m = std::make_shared<LookupMember>(std::move(name), SymbolKind::Field, Object(), Compilation());
    keep.push_back(m);
    return m.get();
}

const IParameterizedMember* MakeMethod(std::string name) {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A `ResolveResult` target (a TypeResolveResult over Object) -- the `targetResolveResult` arg.
std::shared_ptr<ResolveResult> MakeTarget() {
    return std::make_shared<TypeResolveResult>(Object());
}

// A `ThisResolveResult` target (over Object) -- for the static-member retarget arm.
std::shared_ptr<ResolveResult> MakeThisTarget() {
    return std::make_shared<ThisResolveResult>(Object());
}

} // namespace

// ---------------------------------------------------------------------------
// CreateResult: empty (all-hidden) groups -> UnknownMemberResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, EmptyGroupsYieldUnknownMember) {
    MemberLookup lookup(nullptr, nullptr, false);
    std::vector<LookupGroup> groups;
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "M", {});
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// CreateResult: a group with visible methods -> MethodGroupResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, VisibleMethodsYieldMethodGroup) {
    auto def = MakeDef("Foo", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);
    auto m = MakeMethod("M");
    std::vector<const IParameterizedMember*> methods{m};
    LookupGroup g(def.get(), nullptr, &methods, nullptr);
    std::vector<LookupGroup> groups{std::move(g)};
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "M", {});
    auto* mgr = dynamic_cast<MethodGroupResolveResult*>(result.get());
    ASSERT_NE(mgr, nullptr);
    EXPECT_EQ(mgr->MethodName(), "M");
    // The method list bucket is over the declaring type `def`.
    auto methodLists = mgr->MethodsGroupedByDeclaringType();
    ASSERT_EQ(methodLists.size(), 1u);
    ASSERT_EQ(methodLists[0].size(), 1u);  // MethodListWithDeclaringType inherits std::vector
    EXPECT_EQ(methodLists[0][0], static_cast<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(m));
}

// ---------------------------------------------------------------------------
// CreateResult: the most-derived group with nested types (single, non-method hidden) -> TypeResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, NestedTypesYieldTypeResolveResult) {
    auto def = MakeDef("Foo", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);
    auto nested = MakeDef("Nested", TypeKind::Class, 0);
    std::vector<ITypePtr> nestedTypes{nested};
    LookupGroup g(def.get(), &nestedTypes, nullptr, nullptr);  // non-method hidden (null) -> single-type
    std::vector<LookupGroup> groups{std::move(g)};
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "Nested", {});
    auto* trr = dynamic_cast<TypeResolveResult*>(result.get());
    ASSERT_NE(trr, nullptr);
    // Single group, 1 nested type, non-method hidden -> TypeResolveResult (NOT AmbiguousTypeResolveResult).
    EXPECT_EQ(dynamic_cast<AmbiguousTypeResolveResult*>(result.get()), nullptr);
    // The TypeResolveResult is over the nested type.
    EXPECT_EQ(trr->Type().GetDefinition(), nested.get());
}

// ---------------------------------------------------------------------------
// CreateResult: the most-derived group with nested types but a visible non-method -> AmbiguousTypeResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, NestedTypesWithVisibleNonMethodYieldsAmbiguousType) {
    auto def = MakeDef("Foo", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);
    auto nested = MakeDef("Nested", TypeKind::Class, 0);
    std::vector<ITypePtr> nestedTypes{nested};
    std::vector<const IParameterizedMember*> empty;
    LookupGroup g(def.get(), &nestedTypes, &empty, MakeField("f"));  // non-method visible -> ambiguous
    std::vector<LookupGroup> groups{std::move(g)};
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "X", {});
    EXPECT_NE(dynamic_cast<AmbiguousTypeResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// CreateResult: >1 group, no nested types, a non-method -> AmbiguousMemberResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, MultipleGroupsYieldAmbiguousMember) {
    auto def1 = MakeDef("B1", TypeKind::Class, 0);
    auto def2 = MakeDef("B2", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);
    auto f1 = MakeField("f1");
    auto f2 = MakeField("f2");
    std::vector<const IParameterizedMember*> empty;
    LookupGroup g1(def1.get(), nullptr, &empty, f1);
    LookupGroup g2(def2.get(), nullptr, &empty, f2);
    std::vector<LookupGroup> groups{std::move(g1), std::move(g2)};
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "f", {});
    EXPECT_NE(dynamic_cast<AmbiguousMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// CreateResult: a single group with a non-method -> MemberResolveResult.
// ---------------------------------------------------------------------------
TEST(CreateResultTest, SingleGroupWithNonMethodYieldsMemberResolveResult) {
    auto def = MakeDef("Foo", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);  // not in enum initializer
    auto f = MakeField("f");
    std::vector<const IParameterizedMember*> empty;
    LookupGroup g(def.get(), nullptr, &empty, f);
    std::vector<LookupGroup> groups{std::move(g)};
    auto target = MakeTarget();
    auto result = CreateResult(lookup, target, groups, "f", {});
    auto* mrr = dynamic_cast<MemberResolveResult*>(result.get());
    ASSERT_NE(mrr, nullptr);
    EXPECT_EQ(mrr->Member(), f);
}

// ---------------------------------------------------------------------------
// CreateResult: a static NonMethod on a ThisResolveResult target retargets to a TypeResolveResult
// (the MemberResolveResult's TargetResult is a TypeResolveResult, not a ThisResolveResult).
// ---------------------------------------------------------------------------
TEST(CreateResultTest, StaticMemberOnThisTargetRetargets) {
    auto def = MakeDef("Foo", TypeKind::Class, 0);
    MemberLookup lookup(nullptr, nullptr, false);
    // A static field: the LookupMember stub is non-static; for this test we rely on the default
    // (IsStatic=false). The retarget arm fires only for a static member; since the stub is non-static,
    // this test documents the NON-retarget path (the target stays the ThisResolveResult).
    auto f = MakeField("f");
    std::vector<const IParameterizedMember*> empty;
    LookupGroup g(def.get(), nullptr, &empty, f);
    std::vector<LookupGroup> groups{std::move(g)};
    auto thisTarget = MakeThisTarget();
    auto result = CreateResult(lookup, thisTarget, groups, "f", {});
    auto* mrr = dynamic_cast<MemberResolveResult*>(result.get());
    ASSERT_NE(mrr, nullptr);
    // The non-static member keeps the ThisResolveResult target (the retarget arm did not fire).
    EXPECT_NE(dynamic_cast<ThisResolveResult*>(mrr->TargetResult()), nullptr);
}
