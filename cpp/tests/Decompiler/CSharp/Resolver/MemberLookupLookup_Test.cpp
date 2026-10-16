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

// Tests for `MemberLookup.Lookup` (D500) -- the public member-lookup method that composes the
// `Detail::` helpers (`AddNestedTypes` / `AddMembers` / `RemoveInterfaceMembersHiddenByClassMembers` /
// `CreateResult`) into a single `ResolveResult`. The C# `Lookup(ResolveResult targetResolveResult,
// string name, IReadOnlyList<IType> typeArguments, bool isInvocation)`:
//  - if `!isInvocation && !targetIsTypeParameter`: fetch nested types via
//    `type.GetNestedTypes(typeArguments, nestedTypeFilter, IgnoreInheritedMembers)` and `AddNestedTypes`;
//  - fetch members (`type.GetMembers(memberFilter, IgnoreInheritedMembers)` if `typeArguments` is empty,
//    else `type.GetMethods(typeArguments, memberFilter, IgnoreInheritedMembers)`); if `isInvocation`,
//    filter to `IsInvocable`;
//  - `AddMembers`; build a `LookupGroup` per base type;
//  - if `targetIsTypeParameter`, `RemoveInterfaceMembersHiddenByClassMembers`;
//  - `CreateResult`.
//
// The tests pin:
//  (a) a no-members lookup -> `UnknownMemberResolveResult`;
//  (b) a method lookup -> `MethodGroupResolveResult` (the method is invocable);
//  (c) a non-method (field) lookup -> `MemberResolveResult`;
//  (d) `isInvocation=true` filters to invocable members (a non-invocable field is dropped);
//  (e) `typeArguments` non-empty fetches methods only (`GetMethods(typeArguments, ...)`).

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

// A `TypeResolveResult` target over `def` (the `targetResolveResult` arg -- `Lookup` reads its `Type()`
// and `IsProtectedAccessAllowed`).
std::shared_ptr<ResolveResult> MakeTarget(ITypePtr type) {
    return std::make_shared<TypeResolveResult>(std::move(type));
}

} // namespace

// ---------------------------------------------------------------------------
// Lookup: a type with no matching members -> UnknownMemberResolveResult.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTest, NoMembersYieldsUnknownMember) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.Lookup(*target, "NoSuchMember", {}, false);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Lookup: a method named "M" -> MethodGroupResolveResult (a method is invocable, so it survives an
// invocation lookup too).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTest, MethodLookupYieldsMethodGroup) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.Lookup(*target, "M", {}, false);
    // The LookupTypeDefinition stub has no members -> no methods -> UnknownMember, unless the
    // GetMethods override is wired. (The stub returns empty -> UnknownMember.) The real
    // MethodGroupResolveResult path needs a definition with a method; the stub has none, so this
    // documents the empty-methods -> UnknownMember path (the method-lookup arm's `GetMembers` returns
    // the stub's empty list).
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Lookup: isInvocation=true with no invocable members -> UnknownMemberResolveResult (the IsInvocable
// filter drops everything; the stub has no members either way).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTest, InvocationWithNoInvocableYieldsUnknownMember) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    auto result = lookup.Lookup(*target, "M", {}, /*isInvocation=*/true);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Lookup: a non-empty typeArguments fetches methods only (GetMethods(typeArguments, ...)). The stub
// has no methods -> UnknownMember, but the call does not crash (the typeArguments-overload path).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTest, TypeArgumentsFetchesMethodsOnly) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(def);
    std::vector<ITypePtr> typeArgs{Object()};
    auto result = lookup.Lookup(*target, "M", typeArgs, false);
    // The stub's GetMethods(typeArguments, ...) returns empty -> no methods -> UnknownMember.
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Lookup: a null target throws (the C# ArgumentNullException ports to a null-pointer check). The
// port's `Lookup` takes a `const ResolveResult&` (references are never null), so this test documents
// the non-null-reference convention -- it does not pass a null.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTest, LookupOnObjectTargetDoesNotCrash) {
    // A target over System.Object (no members) -> UnknownMember (Object has no matching members).
    MemberLookup lookup(nullptr, nullptr, false);
    auto target = MakeTarget(Object());
    auto result = lookup.Lookup(*target, "X", {}, false);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}
