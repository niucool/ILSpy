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

// Tests for the `LookupGroup` helper class (D495) -- the value type the `MemberLookup` Lookup region
// (the deferred `GetAccessibleMembers` / `LookupType` / `Lookup` / `LookupIndexers`) builds and
// mutates. The C# `sealed class LookupGroup` (nested in `MemberLookup`) holds a `DeclaringType`
// (readonly), `NestedTypes` (mutable -- cleared when hidden), `Methods` (readonly
// `List<IParameterizedMember>`), `MethodsAreHidden` (mutable), `NonMethod` (mutable `IMember`),
// `NonMethodIsHidden` (mutable), and the `AllHidden` computed property. The ctor sets
// `MethodsAreHidden = (methods == null || methods.Count == 0)` and `NonMethodIsHidden = (nonMethod == null)`.
//
// The tests pin:
//  (a) the ctor with null methods + null nonMethod -> MethodsAreHidden AND NonMethodIsHidden;
//  (b) the ctor with a non-empty methods list + null nonMethod -> !MethodsAreHidden, NonMethodIsHidden;
//  (c) the ctor with null methods + a non-null nonMethod -> MethodsAreHidden, !NonMethodIsHidden;
//  (d) AllHidden is true iff NonMethodIsHidden && MethodsAreHidden && NestedTypes is empty/null;
//  (e) the NestedTypes / NonMethod / MethodsAreHidden / NonMethodIsHidden fields are mutable
//      (the Lookup region hides members by setting these).

#include "Decompiler/CSharp/Resolver/LookupGroup.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::LookupGroup;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMember;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

using MethodList = std::vector<const IParameterizedMember*>;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A minimal non-null IMember (a LookupMember with a Field SymbolKind + Object return type).
const IMember* MakeMember() {
    static auto m = std::make_shared<LookupMember>("F", SymbolKind::Field, Object(), Compilation());
    return m.get();
}

} // namespace

// ---------------------------------------------------------------------------
// ctor: null methods + null nonMethod -> both hidden, AllHidden true.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, CtorEmptyMethodsAndNullNonMethodHidesBoth) {
    LookupGroup g(Object().get(), nullptr, nullptr, nullptr);
    EXPECT_TRUE(g.MethodsAreHidden());
    EXPECT_TRUE(g.NonMethodIsHidden());
    EXPECT_TRUE(g.AllHidden());
}

// ---------------------------------------------------------------------------
// ctor: a non-empty methods list + null nonMethod -> !MethodsAreHidden, NonMethodIsHidden.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, CtorNonEmptyMethodsNotHidden) {
    MethodList methods{nullptr};  // a non-empty list (the ctor checks `methods == nullptr || empty`)
    LookupGroup g(Object().get(), nullptr, &methods, nullptr);
    EXPECT_FALSE(g.MethodsAreHidden());
    EXPECT_TRUE(g.NonMethodIsHidden());
}

// ---------------------------------------------------------------------------
// ctor: null methods + a non-null nonMethod -> MethodsAreHidden, !NonMethodIsHidden.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, CtorNonNullNonMethodNotHidden) {
    const IMember* nonMethod = MakeMember();
    LookupGroup g(Object().get(), nullptr, nullptr, nonMethod);
    EXPECT_TRUE(g.MethodsAreHidden());
    EXPECT_FALSE(g.NonMethodIsHidden());
    EXPECT_EQ(g.NonMethod(), nonMethod);
}

// ---------------------------------------------------------------------------
// AllHidden: nested types present -> !AllHidden even if methods + nonMethod are hidden.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, NestedTypesPresentNotAllHidden) {
    std::vector<ITypePtr> nestedTypes{Object()};
    LookupGroup g(Object().get(), &nestedTypes, nullptr, nullptr);
    g.MethodsAreHidden() = true;
    g.NonMethodIsHidden() = true;
    EXPECT_FALSE(g.AllHidden());
}

// ---------------------------------------------------------------------------
// AllHidden: empty nestedTypes + both hidden -> AllHidden true.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, EmptyNestedTypesAndBothHiddenAllHidden) {
    std::vector<ITypePtr> emptyNested;
    LookupGroup g(Object().get(), &emptyNested, nullptr, nullptr);
    g.MethodsAreHidden() = true;
    g.NonMethodIsHidden() = true;
    EXPECT_TRUE(g.AllHidden());
}

// ---------------------------------------------------------------------------
// NestedTypes is mutable: the Lookup region hides nested types by clearing it (null in C#).
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, NestedTypesIsMutable) {
    std::vector<ITypePtr> nestedTypes{Object()};
    LookupGroup g(Object().get(), &nestedTypes, nullptr, nullptr);
    ASSERT_FALSE(g.NestedTypes().empty());
    g.NestedTypes().clear();
    EXPECT_TRUE(g.NestedTypes().empty());
}

// ---------------------------------------------------------------------------
// NonMethod is mutable: the AddMembers region replaces the virtual NonMethod with the override.
// ---------------------------------------------------------------------------
TEST(LookupGroupTest, NonMethodIsMutable) {
    const IMember* nonMethod = MakeMember();
    LookupGroup g(Object().get(), nullptr, nullptr, nonMethod);
    ASSERT_EQ(g.NonMethod(), nonMethod);
    const IMember* replacement = MakeMember();
    g.NonMethod() = replacement;
    EXPECT_EQ(g.NonMethod(), replacement);
}
