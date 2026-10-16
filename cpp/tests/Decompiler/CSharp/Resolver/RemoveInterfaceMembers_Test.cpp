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

// Tests for the `MemberLookup.RemoveInterfaceMembersHiddenByClassMembers` + `IsInterfaceOrSystemObject`
// helpers (D498). The C# `static bool IsInterfaceOrSystemObject(IType)` returns true if the type is an
// interface OR `System.Object` (KnownTypeCode.Object). `RemoveInterfaceMembersHiddenByClassMembers(
// List<LookupGroup>)` walks the lookup groups: a CLASS group (NOT interface/Object) with nested types OR
// a visible non-method hides ALL interface groups' members; a class group with visible methods removes
// the same-signature methods from interface groups (and hides their non-methods + nested types). Both
// are pure transformations over `lookupGroups` (no instance state), so they lift to `Detail` free
// functions.
//
// The tests pin:
//  (a) `IsInterfaceOrSystemObject`: an interface type -> true; a class type -> false; `System.Object`
//      (KnownTypeCode.Object) -> true; a non-definition type -> false;
//  (b) a class group with nested types -> hides ALL interface groups' members (methods + non-method +
//      nested types);
//  (c) a class group with a visible non-method -> hides ALL interface groups' members;
//  (d) a class group with visible methods (no nested, non-method hidden) -> removes the same-signature
//      methods from interface groups + hides interface non-methods + nested types;
//  (e) an interface/Object group is skipped (not treated as a "class" group).

#include "Decompiler/CSharp/Resolver/LookupHelpers.hpp"
#include "Decompiler/CSharp/Resolver/LookupGroup.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::IsInterfaceOrSystemObject;
using ILSpy::Decompiler::CSharp::Resolver::Detail::RemoveInterfaceMembersHiddenByClassMembers;
using ILSpy::Decompiler::CSharp::Resolver::LookupGroup;
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

// A `LookupTypeDefinition` with a configurable kind + type-parameter count + (optional)
// `KnownTypeCode` (for the System.Object test).
std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name, TypeKind kind, int tpc,
                                              KnownTypeCode ktc = KnownTypeCode::None) {
    auto d = std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, tpc)), kind,
        Accessibility::Public, Compilation(), nullptr, ktc);
    return d;
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

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

} // namespace

// ---------------------------------------------------------------------------
// IsInterfaceOrSystemObject: an interface -> true; a class -> false; System.Object -> true;
// a non-definition type -> false.
// ---------------------------------------------------------------------------
TEST(RemoveInterfaceMembersTest, IsInterfaceOrSystemObjectClassifies) {
    auto iface = MakeDef("IFoo", TypeKind::Interface, 0);
    EXPECT_TRUE(IsInterfaceOrSystemObject(*iface));
    auto klass = MakeDef("Foo", TypeKind::Class, 0);
    EXPECT_FALSE(IsInterfaceOrSystemObject(*klass));
    auto systemObject = MakeDef("Object", TypeKind::Class, 0, KnownTypeCode::Object);
    EXPECT_TRUE(IsInterfaceOrSystemObject(*systemObject));
    // A non-definition type (KnownType has GetDefinition() -> null) -> false.
    EXPECT_FALSE(IsInterfaceOrSystemObject(*Object()));
}

// ---------------------------------------------------------------------------
// A class group with nested types -> hides ALL interface groups' members.
// ---------------------------------------------------------------------------
TEST(RemoveInterfaceMembersTest, ClassWithNestedTypesHidesAllInterfaceMembers) {
    auto klass = MakeDef("Foo", TypeKind::Class, 0);
    auto iface = MakeDef("IFoo", TypeKind::Interface, 0);

    // Interface group: a method + a non-method + a nested type (all should be hidden).
    std::vector<const IParameterizedMember*> ifaceMethods{MakeMethod("IM")};
    std::vector<ITypePtr> ifaceNested{MakeDef("INested", TypeKind::Class, 0)};
    LookupGroup ifaceGroup(iface.get(), &ifaceNested, &ifaceMethods, MakeField("ifield"));
    ASSERT_FALSE(ifaceGroup.MethodsAreHidden());
    ASSERT_FALSE(ifaceGroup.NonMethodIsHidden());
    ASSERT_EQ(ifaceGroup.NestedTypes().size(), 1u);

    // Class group: a nested type (hasNestedTypes=true) -- no methods/non-method (both hidden by ctor).
    std::vector<ITypePtr> classNested{MakeDef("CNested", TypeKind::Class, 0)};
    LookupGroup classGroup(klass.get(), &classNested, nullptr, nullptr);
    std::vector<LookupGroup> groups{std::move(ifaceGroup), std::move(classGroup)};

    RemoveInterfaceMembersHiddenByClassMembers(groups);
    // The interface group's methods + non-method + nested types are hidden.
    EXPECT_TRUE(groups[0].MethodsAreHidden());
    EXPECT_TRUE(groups[0].NonMethodIsHidden());
    EXPECT_TRUE(groups[0].NestedTypes().empty());
    // The class group is unchanged (it's not interface/Object) -- its nested type survives.
    EXPECT_EQ(groups[1].NestedTypes().size(), 1u);
}

// ---------------------------------------------------------------------------
// A class group with a visible non-method -> hides ALL interface groups' members.
// ---------------------------------------------------------------------------
TEST(RemoveInterfaceMembersTest, ClassWithVisibleNonMethodHidesAllInterfaceMembers) {
    auto klass = MakeDef("Foo", TypeKind::Class, 0);
    auto iface = MakeDef("IFoo", TypeKind::Interface, 0);

    std::vector<const IParameterizedMember*> ifaceMethods{MakeMethod("IM")};
    std::vector<ITypePtr> ifaceNested{MakeDef("INested", TypeKind::Class, 0)};
    LookupGroup ifaceGroup(iface.get(), &ifaceNested, &ifaceMethods, MakeField("ifield"));
    // Class group: a visible non-method (NonMethodIsHidden=false), no nested types, methods hidden.
    std::vector<const IParameterizedMember*> emptyMethods;
    LookupGroup classGroup(klass.get(), nullptr, &emptyMethods, MakeField("cfield"));
    ASSERT_FALSE(classGroup.NonMethodIsHidden());
    std::vector<LookupGroup> groups{std::move(ifaceGroup), std::move(classGroup)};

    RemoveInterfaceMembersHiddenByClassMembers(groups);
    EXPECT_TRUE(groups[0].MethodsAreHidden());
    EXPECT_TRUE(groups[0].NonMethodIsHidden());
    EXPECT_TRUE(groups[0].NestedTypes().empty());
}

// ---------------------------------------------------------------------------
// A class group with visible methods (no nested, non-method hidden) -> removes the same-signature
// methods from interface groups + hides interface non-methods + nested types.
// ---------------------------------------------------------------------------
TEST(RemoveInterfaceMembersTest, ClassWithVisibleMethodsRemovesSameSignatureInterfaceMethods) {
    auto klass = MakeDef("Foo", TypeKind::Class, 0);
    auto iface = MakeDef("IFoo", TypeKind::Interface, 0);

    // Interface group: a method "M" + a method "Other" + a non-method + a nested type.
    // The "M" method (same signature as the class method) is removed; "Other" survives.
    auto* ifaceM = MakeMethod("M");
    auto* ifaceOther = MakeMethod("Other");
    std::vector<const IParameterizedMember*> ifaceMethods{ifaceM, ifaceOther};
    std::vector<ITypePtr> ifaceNested{MakeDef("INested", TypeKind::Class, 0)};
    LookupGroup ifaceGroup(iface.get(), &ifaceNested, &ifaceMethods, MakeField("ifield"));
    ASSERT_EQ(ifaceGroup.Methods().size(), 2u);

    // Class group: a visible method "M" (same signature), no nested, non-method hidden (null nonMethod).
    auto* classM = MakeMethod("M");
    std::vector<const IParameterizedMember*> classMethods{classM};
    LookupGroup classGroup(klass.get(), nullptr, &classMethods, nullptr);
    ASSERT_FALSE(classGroup.MethodsAreHidden());
    ASSERT_TRUE(classGroup.NonMethodIsHidden());  // null nonMethod -> hidden
    std::vector<LookupGroup> groups{std::move(ifaceGroup), std::move(classGroup)};

    RemoveInterfaceMembersHiddenByClassMembers(groups);
    // The interface group's "M" method is removed (same signature as the class "M"); "Other" survives.
    ASSERT_EQ(groups[0].Methods().size(), 1u);
    EXPECT_EQ(groups[0].Methods()[0], ifaceOther);
    // The interface group's non-method + nested types are hidden.
    EXPECT_TRUE(groups[0].NonMethodIsHidden());
    EXPECT_TRUE(groups[0].NestedTypes().empty());
    // MethodsAreHidden is NOT set (the interface methods list still has "Other").
    EXPECT_FALSE(groups[0].MethodsAreHidden());
}

// ---------------------------------------------------------------------------
// An interface/Object group is skipped (not treated as a "class" group -- its members are not
// re-examined as the hiding group).
// ---------------------------------------------------------------------------
TEST(RemoveInterfaceMembersTest, InterfaceGroupSkippedAsClassGroup) {
    auto iface1 = MakeDef("IFoo", TypeKind::Interface, 0);
    auto iface2 = MakeDef("IBar", TypeKind::Interface, 0);

    // Two interface groups: neither is a class, so neither hides the other.
    std::vector<const IParameterizedMember*> i1Methods{MakeMethod("M")};
    std::vector<ITypePtr> i1Nested{MakeDef("N1", TypeKind::Class, 0)};
    LookupGroup i1(iface1.get(), &i1Nested, &i1Methods, MakeField("f1"));
    std::vector<const IParameterizedMember*> i2Methods{MakeMethod("M")};
    LookupGroup i2(iface2.get(), nullptr, &i2Methods, MakeField("f2"));
    std::vector<LookupGroup> groups{std::move(i1), std::move(i2)};

    RemoveInterfaceMembersHiddenByClassMembers(groups);
    // Both interface groups are unchanged (neither is a class group).
    EXPECT_FALSE(groups[0].MethodsAreHidden());
    EXPECT_FALSE(groups[0].NonMethodIsHidden());
    EXPECT_EQ(groups[0].NestedTypes().size(), 1u);
    EXPECT_FALSE(groups[1].MethodsAreHidden());
    EXPECT_FALSE(groups[1].NonMethodIsHidden());
}
