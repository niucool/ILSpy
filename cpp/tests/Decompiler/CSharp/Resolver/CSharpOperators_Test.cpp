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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpOperators` skeleton (the per-compilation built-in-operator
// tables, CSharpOperators.cs lines 33-100) and the `OperatorMethod` base class
// (lines 102-235): the `Get` CacheManager factory, the precomputed parameter tables
// (`InitParameterArrays` -> `MakeParameter` / `MakeNullableParameter`), the `Lift`
// lifted-form list builder, and the fixed built-in-operator member surface.
//
// The registered-compilation stub registers every `KnownTypeCode` the parameter tables
// resolve (`Object`..`String`, plus the `Nullable\`1` generic definition the
// `NullableType.Create` table arm resolves) with shared-managed `LookupTypeDefinition`
// stubs: `InitParameterArrays` recovers the owning `ITypePtr` from the `FindType`
// result via `shared_from_this()` (the D529 convention), so the registrations MUST be
// `make_shared`-managed (a `LookupCompilation` returns its non-shared `unknownType_`
// member for an unregistered code, and `shared_from_this()` on it would throw).

#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FindType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the parameter tables resolve
// registered as a shared-managed `LookupTypeDefinition`. `types[i]` is the registration
// for `KnownTypeCode(1 + i)` (Object..String); `nullableOfT` is the `System.Nullable\`1`
// generic definition (the `NullableType.Create` table arm resolves it through
// `FindType(KnownTypeCode::NullableOfT)` -- an unregistered code would return the
// compilation's non-shared `unknownType_` member and `shared_from_this()` would throw).
// The LookupCompilation is heap-allocated through a unique_ptr (it is non-movable: its
// LookupModule holds a `const ICompilation&` to itself).
struct RegisteredCompilation {
    std::unique_ptr<LookupCompilation> compilation;
    std::vector<std::shared_ptr<LookupTypeDefinition>> types;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;
};

RegisteredCompilation MakeRegisteredCompilation() {
    RegisteredCompilation rc;
    rc.compilation = std::make_unique<LookupCompilation>();
    for (int raw = static_cast<int>(KnownTypeCode::Object);
         raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
        KnownTypeCode code = static_cast<KnownTypeCode>(raw);
        std::string name = "T" + std::to_string(raw);
        auto def = std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)),
            TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr, code);
        rc.compilation->RegisterKnownType(code, def.get());
        rc.types.push_back(std::move(def));
    }
    rc.nullableOfT = std::make_shared<LookupTypeDefinition>(
        "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
        TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr,
        KnownTypeCode::NullableOfT);
    rc.compilation->RegisterKnownType(KnownTypeCode::NullableOfT, rc.nullableOfT.get());
    return rc;
}

// The shared registered compilation (the registered types are kept alive by the static
// struct for the program's lifetime -- the LookupCompilation stores only raw pointers).
RegisteredCompilation& TheCompilation() {
    static RegisteredCompilation rc = MakeRegisteredCompilation();
    return rc;
}

LookupCompilation& Compilation() {
    return *TheCompilation().compilation;
}

// A minimal concrete `OperatorMethod` for the surface tests: exposes the protected
// `parameters_` / `returnType_` initialization surface (the C# derived operator-method
// ctors' mutation path) and carries a configurable `Lift` result (the C# derived lifted
// classes override `Lift` to build the `Nullable<T>` form).
class TestOperatorMethod : public OperatorMethod {
public:
    explicit TestOperatorMethod(const ICompilation& compilation)
        : OperatorMethod(compilation) {}

    using OperatorMethod::parameters_;
    using OperatorMethod::returnType_;

    void SetLifted(std::shared_ptr<OperatorMethod> lifted) { lifted_ = std::move(lifted); }

    std::shared_ptr<OperatorMethod> Lift(const CSharpOperators& operators) const override {
        (void)operators;
        return lifted_;
    }

private:
    std::shared_ptr<OperatorMethod> lifted_;
};

// Compile-time pins: `OperatorMethod` IS-A `IParameterizedMember`, is polymorphic with
// a virtual destructor, and is NOT final (the C# is unsealed -- the derived
// *OperatorMethod families subclass it); `CSharpOperators` IS final (the C# is sealed).
static_assert(std::is_base_of_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember,
                                 OperatorMethod>);
static_assert(std::is_polymorphic_v<OperatorMethod>);
static_assert(std::has_virtual_destructor_v<OperatorMethod>);
static_assert(!std::is_final_v<OperatorMethod>);
static_assert(std::is_final_v<CSharpOperators>);

// ---------------------------------------------------------------------------
// Get (the per-compilation CacheManager singleton)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsTest, GetCachesTheInstancePerCompilation) {
    CSharpOperators& first = CSharpOperators::Get(Compilation());
    CSharpOperators& second = CSharpOperators::Get(Compilation());
    EXPECT_EQ(&first, &second);
}

TEST(CSharpOperatorsTest, GetReturnsDistinctInstancesPerCompilation) {
    RegisteredCompilation first = MakeRegisteredCompilation();
    RegisteredCompilation second = MakeRegisteredCompilation();
    CSharpOperators& firstOperators = CSharpOperators::Get(*first.compilation);
    CSharpOperators& secondOperators = CSharpOperators::Get(*second.compilation);
    EXPECT_NE(&firstOperators, &secondOperators);
}

// ---------------------------------------------------------------------------
// InitParameterArrays / MakeParameter / MakeNullableParameter (the parameter tables)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsTest, MakeParameterReturnsDefaultParameterWithRegisteredTypeAndEmptyName) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    std::shared_ptr<const IParameter> parameter = operators.MakeParameter(TypeCode::Int32);
    ASSERT_NE(parameter, nullptr);
    const DefaultParameter* def = dynamic_cast<const DefaultParameter*>(parameter.get());
    ASSERT_NE(def, nullptr);
    // The C# `new DefaultParameter(compilation.FindType(i), string.Empty)` -- an empty
    // name and the FindType-resolved type (pointer identity with the registration).
    EXPECT_EQ(def->Name(), std::string());
    const IType* expected = &Compilation().FindType(KnownTypeCode::Int32);
    EXPECT_EQ(&def->Type(), expected);
}

TEST(CSharpOperatorsTest, MakeParameterCoversTheFullNormalRange) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    // The C# table covers TypeCode.Object..String (17 entries, one per FindType code).
    for (int raw = static_cast<int>(TypeCode::Object);
         raw <= static_cast<int>(TypeCode::String); ++raw) {
        TypeCode code = static_cast<TypeCode>(raw);
        std::shared_ptr<const IParameter> parameter = operators.MakeParameter(code);
        ASSERT_NE(parameter, nullptr) << "TypeCode " << raw;
        const IType* expected =
            &Compilation().FindType(static_cast<KnownTypeCode>(code));
        EXPECT_EQ(&parameter->Type(), expected) << "TypeCode " << raw;
    }
}

TEST(CSharpOperatorsTest, MakeNullableParameterLiftsEveryNumericParameter) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    // The C# nullable table covers TypeCode.Boolean..Decimal (13 entries); each is the
    // Nullable<T> form of the matching normal parameter's type.
    for (int raw = static_cast<int>(TypeCode::Boolean);
         raw <= static_cast<int>(TypeCode::Decimal); ++raw) {
        TypeCode code = static_cast<TypeCode>(raw);
        std::shared_ptr<const IParameter> normal = operators.MakeParameter(code);
        ASSERT_NE(normal, nullptr);
        std::shared_ptr<const IParameter> nullable;
        // ASSERT_NO_THROW keeps a broken table a clean failure instead of an
        // exception-through-the-test (the iteration-73 debug-CRT abort learning).
        ASSERT_NO_THROW(nullable = operators.MakeNullableParameter(*normal));
        ASSERT_NE(nullable, nullptr) << "TypeCode " << raw;
        EXPECT_TRUE(IsNullable(nullable->Type())) << "TypeCode " << raw;
        // GetUnderlyingType returns the type argument -- the registration for the code
        // (pointer identity).
        const IType* underlying = &GetUnderlyingType(nullable->Type());
        const IType* expected = &Compilation().FindType(static_cast<KnownTypeCode>(code));
        EXPECT_EQ(underlying, expected) << "TypeCode " << raw;
        EXPECT_EQ(nullable->Name(), std::string());
    }
}

TEST(CSharpOperatorsTest, MakeNullableParameterReturnsTheSharedInstancePerCode) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    std::shared_ptr<const IParameter> normal = operators.MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> first;
    ASSERT_NO_THROW(first = operators.MakeNullableParameter(*normal));
    std::shared_ptr<const IParameter> second = operators.MakeNullableParameter(*normal);
    // The table is PRECOMPUTED (InitParameterArrays): two lookups return the SAME
    // instance (the C# shared-reference semantics).
    EXPECT_EQ(first.get(), second.get());
    // A different code yields a different instance (the per-code table entry).
    std::shared_ptr<const IParameter> other;
    ASSERT_NO_THROW(other = operators.MakeNullableParameter(
                        *operators.MakeParameter(TypeCode::Int64)));
    EXPECT_NE(first.get(), other.get());
}

TEST(CSharpOperatorsTest, MakeNullableParameterThrowsForForeignParameter) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    // A DefaultParameter NOT from the normal table is not reference-equal to any table
    // entry; the C# throws ArgumentException (the std::invalid_argument convention).
    auto foreign = std::make_shared<DefaultParameter>(
        TheCompilation().types[static_cast<std::size_t>(
            static_cast<int>(KnownTypeCode::Int32) - 1)],
        std::string());
    EXPECT_THROW(operators.MakeNullableParameter(*foreign), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Lift (the lifted-form list builder)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsTest, LiftWithoutLiftedFormsReturnsTheOriginalsInOrder) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    auto m1 = std::make_shared<TestOperatorMethod>(Compilation());
    auto m2 = std::make_shared<TestOperatorMethod>(Compilation());
    // The base Lift returns null (no lifted form), so Lift returns exactly the
    // originals, in order.
    std::vector<std::shared_ptr<OperatorMethod>> result =
        operators.Lift({m1, m2});
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), m1.get());
    EXPECT_EQ(result[1].get(), m2.get());
}

TEST(CSharpOperatorsTest, LiftAppendsLiftedFormsAfterAllOriginals) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    auto m1 = std::make_shared<TestOperatorMethod>(Compilation());
    auto m2 = std::make_shared<TestOperatorMethod>(Compilation());
    auto l1 = std::make_shared<TestOperatorMethod>(Compilation());
    auto l2 = std::make_shared<TestOperatorMethod>(Compilation());
    m1->SetLifted(l1);
    m2->SetLifted(l2);
    // The C# result starts as a copy of ALL the originals, then appends each lifted
    // form in iteration order: [m1, m2, l1, l2], NOT [m1, l1, m2, l2].
    std::vector<std::shared_ptr<OperatorMethod>> result =
        operators.Lift({m1, m2});
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].get(), m1.get());
    EXPECT_EQ(result[1].get(), m2.get());
    EXPECT_EQ(result[2].get(), l1.get());
    EXPECT_EQ(result[3].get(), l2.get());
}

TEST(CSharpOperatorsTest, LiftSkipsNullLiftedForms) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    auto m1 = std::make_shared<TestOperatorMethod>(Compilation());
    auto m2 = std::make_shared<TestOperatorMethod>(Compilation());
    auto l2 = std::make_shared<TestOperatorMethod>(Compilation());
    m2->SetLifted(l2);
    std::vector<std::shared_ptr<OperatorMethod>> result =
        operators.Lift({m1, m2});
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].get(), m1.get());
    EXPECT_EQ(result[1].get(), m2.get());
    EXPECT_EQ(result[2].get(), l2.get());
}

// ---------------------------------------------------------------------------
// OperatorMethod (the fixed built-in-operator member surface)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsTest, OperatorMethodNamesAreTheOperatorLiterals) {
    OperatorMethod method(Compilation());
    EXPECT_EQ(method.Name(), "operator");
    EXPECT_EQ(method.FullName(), "operator");
    EXPECT_EQ(method.ReflectionName(), "operator");
    EXPECT_EQ(method.Namespace(), std::string());
}

TEST(CSharpOperatorsTest, OperatorMethodEntityDefaults) {
    OperatorMethod method(Compilation());
    EXPECT_EQ(method.SymbolKind(), SymbolKind::Operator);
    EXPECT_TRUE(method.IsStatic());
    EXPECT_FALSE(method.IsAbstract());
    EXPECT_FALSE(method.IsSealed());
    EXPECT_EQ(method.Accessibility(), Accessibility::Public);
    EXPECT_EQ(method.MetadataToken(), 0u);
    EXPECT_EQ(method.DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(method.ParentModule(), &Compilation().MainModule());
    EXPECT_TRUE(method.GetAttributes().empty());
    EXPECT_FALSE(method.HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute::Obsolete));
    EXPECT_EQ(method.GetAttribute(
                  ILSpy::Decompiler::TypeSystem::KnownAttribute::Obsolete),
              nullptr);
    EXPECT_TRUE(method.ExplicitlyImplementedInterfaceMembers().empty());
    EXPECT_FALSE(method.IsExplicitInterfaceImplementation());
    EXPECT_FALSE(method.IsVirtual());
    EXPECT_FALSE(method.IsOverride());
    EXPECT_FALSE(method.IsOverridable());
    EXPECT_TRUE(method.Parameters().empty());
}

TEST(CSharpOperatorsTest, OperatorMethodDeclaringTypeIsUnknownType) {
    OperatorMethod method(Compilation());
    // The C# `public IType DeclaringType => SpecialType.UnknownType` -- the null object
    // (Kind::Unknown), a fresh instance per call.
    ITypePtr declaringType = method.DeclaringType();
    ASSERT_NE(declaringType, nullptr);
    EXPECT_EQ(declaringType->Kind(), TypeKind::Unknown);
}

TEST(CSharpOperatorsTest, OperatorMethodMemberDefinitionIsSelf) {
    OperatorMethod method(Compilation());
    // The C# `IMember IMember.MemberDefinition => this`.
    EXPECT_EQ(method.MemberDefinition(), &method);
}

TEST(CSharpOperatorsTest, OperatorMethodSubstitutionIsTheIdentitySingleton) {
    OperatorMethod method(Compilation());
    EXPECT_EQ(method.Substitution(), &TypeParameterSubstitution::Identity());
}

TEST(CSharpOperatorsTest, OperatorMethodSpecializeWithIdentityReturnsSelf) {
    OperatorMethod method(Compilation());
    const TypeParameterSubstitution* identity = &TypeParameterSubstitution::Identity();
    EXPECT_EQ(method.Specialize(identity), &method);
}

TEST(CSharpOperatorsTest, OperatorMethodSpecializeWithOtherThrows) {
    OperatorMethod method(Compilation());
    // A non-identity substitution: the C# throws NotSupportedException (the
    // std::logic_error convention).
    std::vector<ITypePtr> classArguments{std::make_shared<LookupTypeDefinition>(
        "X", "", FullTypeName(TopLevelTypeName("", "X", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr)};
    TypeParameterSubstitution substitution(classArguments, std::nullopt);
    EXPECT_THROW(method.Specialize(&substitution), std::logic_error);
}

TEST(CSharpOperatorsTest, OperatorMethodEqualsIsReferenceEquality) {
    OperatorMethod method(Compilation());
    OperatorMethod other(Compilation());
    EXPECT_TRUE(method.Equals(&method, nullptr));
    EXPECT_FALSE(method.Equals(&other, nullptr));
    EXPECT_FALSE(method.Equals(nullptr, nullptr));
}

TEST(CSharpOperatorsTest, OperatorMethodParametersSnapshotMatchesAddedParameters) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    TestOperatorMethod method(Compilation());
    std::shared_ptr<const IParameter> p1 = operators.MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> p2 = operators.MakeParameter(TypeCode::Int64);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    method.parameters_.push_back(p1);
    method.parameters_.push_back(p2);
    std::vector<const IParameter*> parameters = method.Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(parameters[0], p1.get());
    EXPECT_EQ(parameters[1], p2.get());
}

TEST(CSharpOperatorsTest, OperatorMethodReturnTypeIsSettable) {
    TestOperatorMethod method(Compilation());
    ITypePtr returnType = TheCompilation().types[static_cast<std::size_t>(
        static_cast<int>(KnownTypeCode::Int32) - 1)];
    method.returnType_ = returnType;
    EXPECT_EQ(&method.ReturnType(), returnType.get());
}

TEST(CSharpOperatorsTest, OperatorMethodCompilationIsTheCtorCompilation) {
    OperatorMethod method(Compilation());
    EXPECT_EQ(&method.Compilation(), &Compilation());
}

TEST(CSharpOperatorsTest, OperatorMethodToStringRendersTheSignature) {
    CSharpOperators& operators = CSharpOperators::Get(Compilation());
    TestOperatorMethod method(Compilation());
    // The registered Int32 stub is named "T9" (KnownTypeCode::Int32 == 9) and Int64
    // "T11"; the C# renders "<ReturnType> operator(<paramType, ...>)" with each type as
    // its ReflectionName.
    method.returnType_ = TheCompilation().types[static_cast<std::size_t>(
        static_cast<int>(KnownTypeCode::Int32) - 1)];
    std::shared_ptr<const IParameter> p1 = operators.MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> p2 = operators.MakeParameter(TypeCode::Int64);
    // ASSERTs keep a broken table a clean failure instead of a null dereference inside
    // ToString (the iteration-73 debug-CRT abort learning).
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    method.parameters_.push_back(p1);
    method.parameters_.push_back(p2);
    EXPECT_EQ(method.ToString(), "T9 operator(T9, T11)");
}

TEST(CSharpOperatorsTest, OperatorMethodLiftDefaultsToNull) {
    OperatorMethod method(Compilation());
    // The C# `public virtual OperatorMethod? Lift(CSharpOperators operators) =>
    // null` -- the base returns no lifted form.
    EXPECT_EQ(method.Lift(CSharpOperators::Get(Compilation())), nullptr);
}

} // namespace
