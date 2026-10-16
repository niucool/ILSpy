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

// Tests for the `MemberLookup.AddNestedTypes` helper (D496) -- the Lookup-region helper that adds
// nested types to `newNestedTypes` and removes hidden members from the existing lookup groups. The
// C# `AddNestedTypes(IType type, IEnumerable<IType> nestedTypes, int typeArgumentCount,
// List<LookupGroup> lookupGroups, ref IEnumerable<IType> typeBaseTypes, ref List<IType>
// newNestedTypes)`:
//   foreach (IType nestedType in nestedTypes) {
//     foreach (var lookupGroup in lookupGroups) {
//       if (lookupGroup.AllHidden) continue;
//       if (typeBaseTypes == null) typeBaseTypes = type.GetNonInterfaceBaseTypes();
//       if (typeBaseTypes.Contains(lookupGroup.DeclaringType)) {
//         lookupGroup.MethodsAreHidden = true;
//         lookupGroup.NonMethodIsHidden = true;
//         if (lookupGroup.NestedTypes != null)
//           lookupGroup.NestedTypes.RemoveAll(t => InnerTypeParameterCount(t) == typeArgumentCount);
//       }
//     }
//     if (newNestedTypes == null) newNestedTypes = new List<IType>();
//     newNestedTypes.Add(nestedType);
//   }
//
// The C# `ref` lazily-initialized params (`typeBaseTypes` -- filled on demand from
// `GetNonInterfaceBaseTypes`; `newNestedTypes` -- allocated on the first nested type) map to
// `std::optional<std::vector<...>>` (`std::nullopt` = the C# `null`).
//
// The tests pin:
//  (a) `InnerTypeParameterCount` -- the static helper (TPC minus the declaring type's TPC; a
//      top-level type with no declaring type returns its full TPC);
//  (b) a base group (whose `DeclaringType` is a base of `type`) gets its methods + non-method
//      hidden and its same-count nested types removed;
//  (c) a base group whose `DeclaringType` is NOT a base of `type` is untouched;
//  (d) the new nested types accumulate in `newNestedTypes`;
//  (e) an `AllHidden` group is skipped (not re-processed);
//  (f) `typeBaseTypes` is lazily initialized (only filled on first use);
//  (g) an empty `nestedTypes` leaves `newNestedTypes` unset and the groups untouched.

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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::AddNestedTypes;
using ILSpy::Decompiler::CSharp::Resolver::Detail::InnerTypeParameterCount;
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
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` with a configurable type-parameter count (for `InnerTypeParameterCount`).
std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name, int tpc) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, tpc)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

// A non-null IMember (a LookupMember with a Field SymbolKind).
const IMember* MakeMember() {
    static auto m = std::make_shared<LookupMember>("F", SymbolKind::Field,
        std::make_shared<KnownType>(KnownTypeCode::Object), Compilation());
    return m.get();
}

} // namespace

// ---------------------------------------------------------------------------
// InnerTypeParameterCount: a top-level type (no declaring type) returns its full TPC.
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, InnerTypeParameterCountTopLevelReturnsFullTpc) {
    auto def = MakeDef("Foo`1", 1);
    // The LookupTypeDefinition stub's DeclaringType() returns null -> InnerTypeParameterCount
    // returns the full TPC (1).
    EXPECT_EQ(InnerTypeParameterCount(*def), 1);
    auto def0 = MakeDef("Foo", 0);
    EXPECT_EQ(InnerTypeParameterCount(*def0), 0);
}

// ---------------------------------------------------------------------------
// AddNestedTypes: a base group (DeclaringType is a base of `type`) gets its methods + non-method
// hidden and its same-count nested types removed; the new nested type accumulates.
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, HidesBaseGroupAndAccumulatesNewNestedType) {
    auto base = MakeDef("Base", 0);
    auto derived = MakeDef("Derived", 0);
    derived->AddDirectBaseType(base);

    // A base-group: DeclaringType = base, a non-method member, a non-empty methods list (so
    // MethodsAreHidden starts false), and a nested type with inner count 0.
    std::vector<ITypePtr> baseNested{MakeDef("BaseNested", 0)};
    std::vector<const IParameterizedMember*> baseMethods{nullptr};
    LookupGroup baseGroup(base.get(), &baseNested, &baseMethods, MakeMember());
    ASSERT_FALSE(baseGroup.MethodsAreHidden());
    ASSERT_FALSE(baseGroup.NonMethodIsHidden());
    ASSERT_EQ(baseGroup.NestedTypes().size(), 1u);

    std::vector<LookupGroup> groups{std::move(baseGroup)};
    std::optional<std::vector<const IType*>> typeBaseTypes;
    std::optional<std::vector<ITypePtr>> newNestedTypes;

    auto newNested = MakeDef("DerivedNested", 0);
    std::vector<ITypePtr> toAdd{newNested};
    AddNestedTypes(*derived, toAdd, 0, groups, typeBaseTypes, newNestedTypes);

    // The base group's methods + non-method are hidden, and its inner-count-0 nested type removed.
    EXPECT_TRUE(groups[0].MethodsAreHidden());
    EXPECT_TRUE(groups[0].NonMethodIsHidden());
    EXPECT_TRUE(groups[0].NestedTypes().empty());
    // The new nested type accumulated.
    ASSERT_TRUE(newNestedTypes.has_value());
    ASSERT_EQ(newNestedTypes->size(), 1u);
    EXPECT_EQ((*newNestedTypes)[0].get(), newNested.get());
    // typeBaseTypes was lazily filled.
    ASSERT_TRUE(typeBaseTypes.has_value());
}

// ---------------------------------------------------------------------------
// AddNestedTypes: a base group whose DeclaringType is NOT a base of `type` is untouched.
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, NonBaseGroupUntouched) {
    auto unrelated = MakeDef("Unrelated", 0);
    auto derived = MakeDef("Derived", 0);  // no base -- GetNonInterfaceBaseTypes = {derived}

    std::vector<ITypePtr> unrelatedNested{MakeDef("UN", 0)};
    std::vector<const IParameterizedMember*> unrelatedMethods{nullptr};
    LookupGroup unrelatedGroup(unrelated.get(), &unrelatedNested, &unrelatedMethods, MakeMember());
    std::vector<LookupGroup> groups{std::move(unrelatedGroup)};
    std::optional<std::vector<const IType*>> typeBaseTypes;
    std::optional<std::vector<ITypePtr>> newNestedTypes;

    auto newNested = MakeDef("DN", 0);
    std::vector<ITypePtr> toAdd{newNested};
    AddNestedTypes(*derived, toAdd, 0, groups, typeBaseTypes, newNestedTypes);

    // The unrelated group is NOT a base of `derived` -> untouched.
    EXPECT_FALSE(groups[0].MethodsAreHidden());
    EXPECT_FALSE(groups[0].NonMethodIsHidden());
    EXPECT_EQ(groups[0].NestedTypes().size(), 1u);
    // The new nested type still accumulates.
    ASSERT_TRUE(newNestedTypes.has_value());
    ASSERT_EQ(newNestedTypes->size(), 1u);
}

// ---------------------------------------------------------------------------
// AddNestedTypes: an AllHidden group is skipped (not re-processed; its nested types are not
// re-examined).
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, AllHiddenGroupSkipped) {
    auto base = MakeDef("Base", 0);
    auto derived = MakeDef("Derived", 0);
    derived->AddDirectBaseType(base);

    // An AllHidden group (empty nested, null non-method -> both hidden -> AllHidden).
    LookupGroup hiddenGroup(base.get(), nullptr, nullptr, nullptr);
    ASSERT_TRUE(hiddenGroup.AllHidden());
    std::vector<LookupGroup> groups{std::move(hiddenGroup)};
    std::optional<std::vector<const IType*>> typeBaseTypes;
    std::optional<std::vector<ITypePtr>> newNestedTypes;

    std::vector<ITypePtr> toAdd{MakeDef("DN", 0)};
    AddNestedTypes(*derived, toAdd, 0, groups, typeBaseTypes, newNestedTypes);

    // The hidden group stays hidden; nothing changes. typeBaseTypes is NOT filled (the AllHidden
    // group was skipped before the lazy init).
    EXPECT_TRUE(groups[0].AllHidden());
    EXPECT_FALSE(typeBaseTypes.has_value());
    // The new nested type still accumulates.
    ASSERT_TRUE(newNestedTypes.has_value());
    EXPECT_EQ(newNestedTypes->size(), 1u);
}

// ---------------------------------------------------------------------------
// AddNestedTypes: the nested types with inner count != typeArgumentCount are NOT removed from the
// base group (only same-count nested types are hidden).
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, OnlySameCountNestedTypesRemoved) {
    auto base = MakeDef("Base", 0);
    auto derived = MakeDef("Derived", 0);
    derived->AddDirectBaseType(base);

    // A base group with a count-1 nested type; we add with typeArgumentCount=0 -> the count-1
    // nested type is NOT removed (1 != 0).
    std::vector<ITypePtr> baseNested{MakeDef("BaseNested`1", 1)};
    std::vector<const IParameterizedMember*> baseMethods{nullptr};
    LookupGroup baseGroup(base.get(), &baseNested, &baseMethods, MakeMember());
    std::vector<LookupGroup> groups{std::move(baseGroup)};
    std::optional<std::vector<const IType*>> typeBaseTypes;
    std::optional<std::vector<ITypePtr>> newNestedTypes;

    std::vector<ITypePtr> toAdd{MakeDef("DN", 0)};
    AddNestedTypes(*derived, toAdd, 0, groups, typeBaseTypes, newNestedTypes);
    // Methods + non-method hidden (the group is a base), but the count-1 nested type kept.
    EXPECT_TRUE(groups[0].MethodsAreHidden());
    EXPECT_TRUE(groups[0].NonMethodIsHidden());
    EXPECT_EQ(groups[0].NestedTypes().size(), 1u);  // the count-1 nested type survives
}

// ---------------------------------------------------------------------------
// AddNestedTypes: an empty `nestedTypes` leaves `newNestedTypes` unset and the groups untouched
// (the loop body never runs).
// ---------------------------------------------------------------------------
TEST(AddNestedTypesTest, EmptyNestedTypesLeavesEverythingUntouched) {
    auto base = MakeDef("Base", 0);
    std::vector<ITypePtr> baseNested{MakeDef("BN", 0)};
    std::vector<const IParameterizedMember*> baseMethods{nullptr};
    LookupGroup baseGroup(base.get(), &baseNested, &baseMethods, MakeMember());
    std::vector<LookupGroup> groups{std::move(baseGroup)};
    std::optional<std::vector<const IType*>> typeBaseTypes;
    std::optional<std::vector<ITypePtr>> newNestedTypes;

    std::vector<ITypePtr> empty;
    AddNestedTypes(*base, empty, 0, groups, typeBaseTypes, newNestedTypes);
    EXPECT_FALSE(groups[0].MethodsAreHidden());
    EXPECT_FALSE(groups[0].NonMethodIsHidden());
    EXPECT_FALSE(newNestedTypes.has_value());
    EXPECT_FALSE(typeBaseTypes.has_value());  // never needed
}
