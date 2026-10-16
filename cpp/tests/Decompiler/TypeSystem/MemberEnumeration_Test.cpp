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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the IType member-enumeration surface (the port of the
// ICSharpCode.Decompiler/TypeSystem/IType.cs `GetNestedTypes` x2 / `GetConstructors` /
// `GetMethods` x2 / `GetProperties` / `GetFields` / `GetEvents` / `GetMembers` /
// `GetAccessors` declarations + the `GetMemberOptions` [Flags] enum + the `AbstractType`
// defaults). The load-bearing cruxes are: (1) the `GetMemberOptions` flags are the
// single-bit `[Flags]` values the C# declares (`ReturnMemberDefinitions` = 0x01,
// `IgnoreInheritedMembers` = 0x02, composed/disjoint), (2) every family VIRTUAL exists
// and dispatches through an `IType&` base reference with the C# default options bound
// at the call site (`GetConstructors` -> `IgnoreInheritedMembers`; every other family
// -> `None`), (3) the `AbstractType` DEFAULT implementations are empty for the seven
// specific families, and (4) `GetMembers` composes `GetMethods.Concat(GetProperties)
// .Concat(GetFields).Concat(GetEvents)` (the C# `AbstractType.GetMembers` default) and
// applies the caller's `Delegate<Predicate>` filter to the composed set. Stub types
// override only the family virtuals under test and come from `LookupStubs.hpp` (the
// `LookupMethod` / `LookupMember` / `LookupEvent` stubs) plus a hand-rolled
// `EnumerationStubType` for the recording.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupEvent;
using TS::TestSupport::LookupMethod;

namespace {

// The smallest concrete `IType` carrying no real members: a `KnownTypeStub`-style
// stub overriding only the pure-virtual core (`Kind` / `Name` / `ReflectionName` /
// `TypeParameterCount` / `StructuralEquals`), so the member-enumeration family
// virtuals inherit the AbstractType-default empty/composed implementations under
// test.
class MinimalEnumerationType : public TS::IType {
public:
    TS::TypeKind Kind() const override { return TS::TypeKind::Class; }
    std::string Name() const override { return "Minimal"; }
    std::string ReflectionName() const override { return "Minimal"; }
    int TypeParameterCount() const override { return 0; }
protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }
};

// A recording concrete `IType`: overrides every member-enumeration family with a
// pre-wired canned snapshot and records the filter/options arguments each call
// received, so the tests can pin both the dispatch and the default-options binding.
class EnumerationStubType : public TS::IType {
public:
    explicit EnumerationStubType(const TS::ICompilation& compilation)
        : compilation_(compilation) {}

    // --- IType core ---
    TS::TypeKind Kind() const override { return TS::TypeKind::Class; }
    std::string Name() const override { return "Stub"; }
    std::string ReflectionName() const override { return "Stub"; }
    int TypeParameterCount() const override { return 0; }

    // Canned member snapshots the families return (wired by the test).
    std::vector<TS::ITypePtr> nestedTypes;
    std::vector<const TS::IMethod*> constructors;
    std::vector<const TS::IMethod*> methods;
    std::vector<const TS::IMethod*> accessors;
    std::vector<const TS::IProperty*> properties;
    std::vector<const TS::IField*> fields;
    std::vector<const TS::IEvent*> events;

    // The filter/options arguments the LAST call to each family received (the
    // recorded-arg snapshot the default-options assertions pin).
    mutable bool nestedTypesSawNullFilter = false;
    mutable bool nestedTypesSawNullFilterByArgs = false;
    mutable TS::GetMemberOptions nestedTypesOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable TS::GetMemberOptions nestedTypesOptionsByArgs = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool constructorsSawNullFilter = false;
    mutable TS::GetMemberOptions constructorsOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool methodsSawNullFilter = false;
    mutable TS::GetMemberOptions methodsOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool methodsSawNullFilterByArgs = false;
    mutable TS::GetMemberOptions methodsOptionsByArgs = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool propertiesSawNullFilter = false;
    mutable TS::GetMemberOptions propertiesOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool fieldsSawNullFilter = false;
    mutable TS::GetMemberOptions fieldsOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool eventsSawNullFilter = false;
    mutable TS::GetMemberOptions eventsOptions = TS::GetMemberOptions::ReturnMemberDefinitions;
    mutable bool accessorsSawNullFilter = false;
    mutable TS::GetMemberOptions accessorsOptions = TS::GetMemberOptions::ReturnMemberDefinitions;

    // --- the member-enumeration family overrides ---

    std::vector<TS::ITypePtr> GetNestedTypes(
        std::function<bool(const TS::ITypeDefinition*)> filter,
        TS::GetMemberOptions options) const override
    {
        nestedTypesSawNullFilter = !filter;
        nestedTypesOptions = options;
        return nestedTypes;
    }

    std::vector<TS::ITypePtr> GetNestedTypes(
        const std::vector<TS::ITypePtr>&,
        std::function<bool(const TS::ITypeDefinition*)> filter,
        TS::GetMemberOptions options) const override
    {
        nestedTypesSawNullFilterByArgs = !filter;
        nestedTypesOptionsByArgs = options;
        return nestedTypes;
    }

    std::vector<const TS::IMethod*> GetConstructors(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
        constructorsSawNullFilter = !filter;
        constructorsOptions = options;
        return Filtered(filter ? filter : [](const TS::IMethod*) { return true; }, constructors);
    }

    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
        methodsSawNullFilter = !filter;
        methodsOptions = options;
        return Filtered(filter ? filter : [](const TS::IMethod*) { return true; }, methods);
    }

    std::vector<const TS::IMethod*> GetMethods(
        const std::vector<TS::ITypePtr>&,
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
        methodsSawNullFilterByArgs = !filter;
        methodsOptionsByArgs = options;
        return Filtered(filter ? filter : [](const TS::IMethod*) { return true; }, methods);
    }

    std::vector<const TS::IProperty*> GetProperties(
        std::function<bool(const TS::IProperty*)> filter,
        TS::GetMemberOptions options) const override
    {
        propertiesSawNullFilter = !filter;
        propertiesOptions = options;
        return properties;
    }

    std::vector<const TS::IField*> GetFields(
        std::function<bool(const TS::IField*)> filter,
        TS::GetMemberOptions options) const override
    {
        fieldsSawNullFilter = !filter;
        fieldsOptions = options;
        return fields;
    }

    std::vector<const TS::IEvent*> GetEvents(
        std::function<bool(const TS::IEvent*)> filter,
        TS::GetMemberOptions options) const override
    {
        eventsSawNullFilter = !filter;
        eventsOptions = options;
        return events;
    }

    std::vector<const TS::IMethod*> GetAccessors(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
        accessorsSawNullFilter = !filter;
        accessorsOptions = options;
        return accessors;
    }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    template <typename T>
    static std::vector<T> Filtered(const std::function<bool(T)>& filter, const std::vector<T>& items)
    {
        std::vector<T> out;
        for (T item : items)
            if (filter(item))
                out.push_back(item);
        return out;
    }

    const TS::ICompilation& compilation_;
};

// Builds the shared LookupCompilation the stubs require (FindType registry is
// empty -- the tests exercise only the member-enumeration routing).
LookupCompilation& Comp()
{
    static LookupCompilation compilation;
    return compilation;
}

} // anonymous namespace

// ---- GetMemberOptions enum (the [Flags] value type) ----

TEST(GetMemberOptionsTest, MembersMatchCSharpLiterals)
{
    EXPECT_EQ(static_cast<std::int32_t>(TS::GetMemberOptions::None), 0);
    EXPECT_EQ(static_cast<std::int32_t>(TS::GetMemberOptions::ReturnMemberDefinitions), 0x01);
    EXPECT_EQ(static_cast<std::int32_t>(TS::GetMemberOptions::IgnoreInheritedMembers), 0x02);
}

TEST(GetMemberOptionsTest, UnderlyingTypeIsInt32)
{
    // The C# `enum GetMemberOptions` has no underlying-type annotation (the C# `int`
    // default): the port's `enum class` is `std::int32_t`-backed, 4 bytes wide.
    static_assert(sizeof(TS::GetMemberOptions) == sizeof(std::int32_t));
    EXPECT_EQ(sizeof(TS::GetMemberOptions), sizeof(std::int32_t));
}

TEST(GetMemberOptionsTest, IndividualFlagsAreDisjointSingleBits)
{
    // The two flag members are disjoint single bits the C# `[Flags]` enum composes
    // with `|`: neither carries the other's bit, None carries nothing.
    auto none = TS::GetMemberOptions::None;
    auto rmd = TS::GetMemberOptions::ReturnMemberDefinitions;
    auto iim = TS::GetMemberOptions::IgnoreInheritedMembers;
    EXPECT_EQ(none & rmd, TS::GetMemberOptions::None);
    EXPECT_EQ(none & iim, TS::GetMemberOptions::None);
    EXPECT_EQ(rmd & iim, TS::GetMemberOptions::None);
}

TEST(GetMemberOptionsTest, BitwiseOrComposesFlags)
{
    auto rmd = TS::GetMemberOptions::ReturnMemberDefinitions;
    auto iim = TS::GetMemberOptions::IgnoreInheritedMembers;
    auto both = rmd | iim;
    EXPECT_EQ(static_cast<std::int32_t>(both), 0x03);
    EXPECT_EQ((both & rmd), rmd);
    EXPECT_EQ((both & iim), iim);
    // `|` is idempotent and commutative for the [Flags] composition.
    EXPECT_EQ(rmd | rmd, rmd);
    EXPECT_EQ((iim | rmd), both);
}

TEST(GetMemberOptionsTest, BitwiseAndTestsFlagMembership)
{
    // The C# `(options & GetMemberOptions.IgnoreInheritedMembers) == GetMemberOptions.None`
    // flag-test idiom (the `GetMembersHelper` guard the `MemberLookup` Lookup region
    // relies on).
    TS::GetMemberOptions options = TS::GetMemberOptions::IgnoreInheritedMembers |
                                   TS::GetMemberOptions::ReturnMemberDefinitions;
    EXPECT_NE((options & TS::GetMemberOptions::IgnoreInheritedMembers), TS::GetMemberOptions::None);
    EXPECT_NE((options & TS::GetMemberOptions::ReturnMemberDefinitions), TS::GetMemberOptions::None);

    TS::GetMemberOptions options2 = TS::GetMemberOptions::IgnoreInheritedMembers;
    EXPECT_EQ((options2 & TS::GetMemberOptions::ReturnMemberDefinitions), TS::GetMemberOptions::None);
}

TEST(GetMemberOptionsTest, NoneIsTheZeroBaseline)
{
    // `None == 0x00` is the no-options baseline the default arguments use.
    EXPECT_EQ(TS::GetMemberOptions::None, static_cast<TS::GetMemberOptions>(0));
    EXPECT_EQ(TS::GetMemberOptions::None | TS::GetMemberOptions::None, TS::GetMemberOptions::None);
}

TEST(GetMemberOptionsTest, DeclaredMembersConstFoldsToBothFlags)
{
    // GetMembersHelper's private `const GetMemberOptions declaredMembers = IgnoreInheritedMembers
    // | ReturnMemberDefinitions` -- the mask the helper sets on the recursive-into-
    // `IType.GetMembers` call. Pin that the composed value keeps both bits AND has no
    // others (the helper's recursion relies on both flags being visible at once).
    auto declaredMembers = TS::GetMemberOptions::IgnoreInheritedMembers | TS::GetMemberOptions::ReturnMemberDefinitions;
    EXPECT_EQ(static_cast<std::int32_t>(declaredMembers), 0x03);
    EXPECT_EQ((declaredMembers & TS::GetMemberOptions::IgnoreInheritedMembers), TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_EQ((declaredMembers & TS::GetMemberOptions::ReturnMemberDefinitions), TS::GetMemberOptions::ReturnMemberDefinitions);
    EXPECT_EQ((declaredMembers & ~declaredMembers), TS::GetMemberOptions::None);
}

// ---- IType member-enumeration defaults (no override) ----

class MemberEnumerationDefaultTest : public ::testing::Test {
protected:
    MinimalEnumerationType type;
};

TEST_F(MemberEnumerationDefaultTest, GetNestedTypesDefaultsToEmpty)
{
    // The C# `AbstractType.GetNestedTypes` returns `EmptyList<IType>.Instance`; the
    // minimal port carries the empty default for a type that does not override it.
    EXPECT_TRUE(type.GetNestedTypes().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetNestedTypesWithTypeArgumentsDefaultsToEmpty)
{
    // The explicitly-named vector disambiguates the two GetNestedTypes overloads
    // (an `{}` argument binds both the filter-first and the type-argument-first forms).
    std::vector<TS::ITypePtr> typeArguments;
    EXPECT_TRUE(type.GetNestedTypes(typeArguments).empty());
}

TEST_F(MemberEnumerationDefaultTest, GetConstructorsDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetConstructors().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetMethodsDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetMethods().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetMethodsWithTypeArgumentsDefaultsToEmpty)
{
    std::vector<TS::ITypePtr> typeArguments;
    EXPECT_TRUE(type.GetMethods(typeArguments).empty());
}

TEST_F(MemberEnumerationDefaultTest, GetPropertiesDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetProperties().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetFieldsDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetFields().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetEventsDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetEvents().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetAccessorsDefaultsToEmpty)
{
    // The C# `AbstractType.GetAccessors` returns `EmptyList<IMethod>.Instance`; accessors
    // are not returned by `GetMembers()` or `GetMethods()`.
    EXPECT_TRUE(type.GetAccessors().empty());
}

TEST_F(MemberEnumerationDefaultTest, GetMembersDefaultsToEmpty)
{
    EXPECT_TRUE(type.GetMembers().empty());
}

// ---- IType member-enumeration dispatch + default-options binding ----

class MemberEnumerationDispatchTest : public ::testing::Test {
protected:
    EnumerationStubType type{ Comp() };
};

TEST_F(MemberEnumerationDispatchTest, GetNestedTypesDispatchesToOverride)
{
    auto nested = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName("N", "Stub.Outer"), TS::TypeKind::Class);
    type.nestedTypes.push_back(nested);
    const TS::IType& base = type;
    auto result = base.GetNestedTypes();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], nested);
}

TEST_F(MemberEnumerationDispatchTest, GetNestedTypesBindsDefaultOptionsNone)
{
    const TS::IType& base = type;
    (void)base.GetNestedTypes();
    EXPECT_TRUE(type.nestedTypesSawNullFilter);
    EXPECT_EQ(type.nestedTypesOptions, TS::GetMemberOptions::None);
}

TEST_F(MemberEnumerationDispatchTest, GetNestedTypesWithTypeArgumentsBindsDefaultOptionsNone)
{
    const TS::IType& base = type;
    std::vector<TS::ITypePtr> typeArguments;
    (void)base.GetNestedTypes(typeArguments);
    EXPECT_TRUE(type.nestedTypesSawNullFilterByArgs);
    EXPECT_EQ(type.nestedTypesOptionsByArgs, TS::GetMemberOptions::None);
}

TEST_F(MemberEnumerationDispatchTest, GetConstructorsBindsDefaultOptionsIgnoreInheritedMembers)
{
    // The ONE family whose C# default is not `None`: `GetConstructors` defaults
    // `options = GetMemberOptions.IgnoreInheritedMembers` (constructors are not
    // inherited). The default is bound at the call site through the base reference.
    const TS::IType& base = type;
    (void)base.GetConstructors();
    EXPECT_TRUE(type.constructorsSawNullFilter);
    EXPECT_EQ(type.constructorsOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
}

TEST_F(MemberEnumerationDispatchTest, GetConstructorsDispatchesToOverride)
{
    LookupMethod ctor{ "ctor", Comp() };
    type.constructors.push_back(&ctor);
    const TS::IType& base = type;
    auto result = base.GetConstructors();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], static_cast<const TS::IMethod*>(&ctor));
}

TEST_F(MemberEnumerationDispatchTest, GetMethodsDispatchesToOverride)
{
    LookupMethod method{ "M", Comp() };
    type.methods.push_back(&method);
    const TS::IType& base = type;
    auto result = base.GetMethods();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], static_cast<const TS::IMethod*>(&method));
}

TEST_F(MemberEnumerationDispatchTest, GetMethodsBindsDefaultOptionsNone)
{
    const TS::IType& base = type;
    (void)base.GetMethods();
    EXPECT_TRUE(type.methodsSawNullFilter);
    EXPECT_EQ(type.methodsOptions, TS::GetMemberOptions::None);
}

TEST_F(MemberEnumerationDispatchTest, GetMethodsWithTypeArgumentsBindsDefaultOptionsNone)
{
    const TS::IType& base = type;
    std::vector<TS::ITypePtr> typeArguments;
    (void)base.GetMethods(typeArguments);
    EXPECT_TRUE(type.methodsSawNullFilterByArgs);
    EXPECT_EQ(type.methodsOptionsByArgs, TS::GetMemberOptions::None);
}

TEST_F(MemberEnumerationDispatchTest, GetPropertiesDispatchesToOverride)
{
    // No pre-wired canned properties here (the stub's empty list): the dispatch itself
    // and the options forwarding are what this pins (a non-empty property snapshot is
    // exercised indirectly through GetMembers' composition).
    const TS::IType& base = type;
    auto result = base.GetProperties(nullptr, TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(type.propertiesSawNullFilter);
    EXPECT_EQ(type.propertiesOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
}

TEST_F(MemberEnumerationDispatchTest, GetFieldsDispatchesToOverride)
{
    const TS::IType& base = type;
    auto result = base.GetFields(nullptr, TS::GetMemberOptions::ReturnMemberDefinitions);
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(type.fieldsOptions, TS::GetMemberOptions::ReturnMemberDefinitions);
}

TEST_F(MemberEnumerationDispatchTest, GetEventsDispatchesToOverride)
{
    LookupEvent evt{ "E", std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), Comp() };
    type.events.push_back(&evt);
    const TS::IType& base = type;
    auto result = base.GetEvents();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], static_cast<const TS::IEvent*>(&evt));
}

TEST_F(MemberEnumerationDispatchTest, GetAccessorsDispatchesToOverride)
{
    LookupMethod accessor{ "get_P", Comp() };
    type.accessors.push_back(&accessor);
    const TS::IType& base = type;
    auto result = base.GetAccessors();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], static_cast<const TS::IMethod*>(&accessor));
}

// ---- The AbstractType.GetMembers composition ----

TEST_F(MemberEnumerationDispatchTest, GetMembersComposesMethodsPropertiesFieldsEventsInOrder)
{
    // The C# `AbstractType.GetMembers` default is
    // `GetMethods(filter, options).Concat(GetProperties(filter, options))
    // .Concat(GetFields(filter, options)).Concat(GetEvents(filter, options))`: a type
    // that overrides only the family virtuals sees them aggregated in that order.
    LookupMethod method{ "M", Comp() };
    LookupEvent evt{ "E", std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), Comp() };
    type.methods.push_back(&method);
    type.events.push_back(&evt);

    const TS::IType& base = type;
    auto result = base.GetMembers();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], static_cast<const TS::IMember*>(&method));
    EXPECT_EQ(result[1], static_cast<const TS::IMember*>(&evt));
}

TEST_F(MemberEnumerationDispatchTest, GetMembersAppliesTheFilterToTheComposedSet)
{
    // The C# `GetMembers(filter, options)` hands the caller's `Delegate<Predicate>`
    // down to each family (which filters on the member kind); the composed result is
    // filtered. Here the filter rejects everything, so the composition is empty even
    // though the families carry canned members.
    LookupMethod method{ "M", Comp() };
    LookupEvent evt{ "E", std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object), Comp() };
    type.methods.push_back(&method);
    type.events.push_back(&evt);

    const TS::IType& base = type;
    auto result = base.GetMembers([](const TS::IMember*) { return false; });
    EXPECT_TRUE(result.empty());

    auto everything = base.GetMembers([](const TS::IMember*) { return true; });
    EXPECT_EQ(everything.size(), 2u);
}

TEST_F(MemberEnumerationDispatchTest, GetMembersPropagatesTheOptionsToEveryFamily)
{
    // The C# `AbstractType.GetMembers(filter, options)` forwards the same `options`
    // to every family; pin that the composed call lands `IgnoreInheritedMembers` on
    // each of GetMethods / GetProperties / GetFields / GetEvents.
    const TS::IType& base = type;
    (void)base.GetMembers([](const TS::IMember*) { return true; },
                          TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_EQ(type.methodsOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_EQ(type.propertiesOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_EQ(type.fieldsOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
    EXPECT_EQ(type.eventsOptions, TS::GetMemberOptions::IgnoreInheritedMembers);
}

TEST_F(MemberEnumerationDispatchTest, GetMembersDefaultBindsNullFilterAndOptionsNone)
{
    // The no-argument call binds the C# defaults: filter = null (the stub records
    // "saw null filter" from every composed family), options = None.
    const TS::IType& base = type;
    (void)base.GetMembers();
    EXPECT_TRUE(type.methodsSawNullFilter);
    EXPECT_TRUE(type.propertiesSawNullFilter);
    EXPECT_TRUE(type.fieldsSawNullFilter);
    EXPECT_TRUE(type.eventsSawNullFilter);
    EXPECT_EQ(type.methodsOptions, TS::GetMemberOptions::None);
    EXPECT_EQ(type.propertiesOptions, TS::GetMemberOptions::None);
    EXPECT_EQ(type.fieldsOptions, TS::GetMemberOptions::None);
    EXPECT_EQ(type.eventsOptions, TS::GetMemberOptions::None);
}
