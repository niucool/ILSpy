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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpOperators relational operator region (CSharpOperators.cs lines
// 865-898 + 901-990): the `RelationalOperatorMethod<T1,T2>` (the lambda-backed built-in
// comparison -- the Boolean return type, the T1/T2 TypeCode parameters, the
// `CanEvaluateAtCompileTime => true` flag, and the `Lift` override that builds the
// `LiftedBinaryOperatorMethod` then RESETS its return type to the base's plain Boolean:
// "don't lift the return type for relational operators"), the four lazy comparison
// operator tables (`<` / `<=` / `>` / `>=`), and the Decimal stand-in's comparison
// operators the `<decimal,decimal>` entries store.
//
// The registered-compilation stub is the CSharpOperatorsEquality_Test.cpp precedent:
// every `KnownTypeCode` the parameter and operator tables resolve must be registered
// with a shared-managed `LookupTypeDefinition` (the ctors recover owning handles through
// `shared_from_this()`, the D529 convention).

#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter (the parameter Type()/Name() reads)
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::BinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::Decimal;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::LambdaBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::RelationalOperatorMethod;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the parameter and operator tables
// resolve registered as a shared-managed `LookupTypeDefinition` (the
// CSharpOperatorsEquality_Test precedent).
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

// The shared registered compilation (the registrations are kept alive by the static struct
// for the program's lifetime -- the LookupCompilation stores only raw pointers).
RegisteredCompilation& TheCompilation() {
    static RegisteredCompilation rc = MakeRegisteredCompilation();
    return rc;
}

LookupCompilation& Compilation() {
    return *TheCompilation().compilation;
}

// The per-compilation CSharpOperators singleton for the shared registered compilation.
CSharpOperators& Operators() {
    return CSharpOperators::Get(Compilation());
}

// The `FindType`-resolved type for a TypeCode -- the registration the parameter tables,
// the operator ctors, and the lifted forms all resolve (pointer identity).
const IType* TypeFor(TypeCode code) {
    return &Compilation().FindType(static_cast<KnownTypeCode>(code));
}

// Asserts the table entry at `index` is an ORIGINAL comparison operator over the TypeCode
// (the DIAGONAL `T op T` shape): both parameters the SAME shared normal-table instance of
// the code, the return type Boolean, the flag true.
void ExpectRelationalOriginalAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                                 std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(code);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get()) << "index " << index;
    EXPECT_EQ(parameters[1], shared.get()) << "index " << index;
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(code)) << "index " << index;
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(code)) << "index " << index;
    // The C# `this.ReturnType = operators.compilation.FindType(KnownTypeCode.Boolean)`.
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean)) << "index " << index;
    // The flag lives on the BinaryOperatorMethod base (the C# `public override bool
    // CanEvaluateAtCompileTime => true`).
    const BinaryOperatorMethod* binary = dynamic_cast<const BinaryOperatorMethod*>(method);
    ASSERT_NE(binary, nullptr) << "index " << index;
    EXPECT_TRUE(binary->CanEvaluateAtCompileTime()) << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED comparison form over the TypeCode: the
// entry IS a LiftedBinaryOperatorMethod whose parameters are the shared nullable-table
// instances, whose return type STAYS the base's plain Boolean (NOT Nullable<bool> -- the
// `RelationalOperatorMethod::Lift` reset), which cross-casts to ILiftedOperator with the
// NonLifted surface at the base, and which does not lift again.
void ExpectRelationalLiftedAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                              std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const LiftedBinaryOperatorMethod* lifted =
        dynamic_cast<const LiftedBinaryOperatorMethod*>(method);
    ASSERT_NE(lifted, nullptr) << "index " << index;
    // The crux: "don't lift the return type for relational operators" -- the return type
    // is NOT Nullable<bool>.
    const IType* returnType = &method->ReturnType();
    EXPECT_FALSE(IsNullable(*returnType)) << "index " << index;
    EXPECT_EQ(returnType, TypeFor(TypeCode::Boolean)) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(code);
    ASSERT_NE(normal, nullptr);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal))
        << "index " << index;
    ASSERT_NE(nullable, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], nullable.get()) << "index " << index;
    EXPECT_EQ(parameters[1], nullable.get()) << "index " << index;
    // The ILiftedOperator surface: the base's parameter list and Boolean return type.
    EXPECT_EQ(lifted->NonLiftedParameters().size(), 2u) << "index " << index;
    EXPECT_EQ(lifted->NonLiftedParameters()[0], normal.get()) << "index " << index;
    EXPECT_EQ(lifted->NonLiftedParameters()[1], normal.get()) << "index " << index;
    EXPECT_EQ(&lifted->NonLiftedReturnType(), TypeFor(TypeCode::Boolean)) << "index " << index;
    // A lifted operator is not lifted again (the OperatorMethod default returns null).
    EXPECT_EQ(method->Lift(Operators()), nullptr) << "index " << index;
}

// Compile-time pins: the class-shape conventions (the C# `internal class` bases are
// unsealed; the RelationalOperatorMethod is `sealed`).
static_assert(std::is_base_of_v<BinaryOperatorMethod, RelationalOperatorMethod<int, int>>);
static_assert(std::is_final_v<RelationalOperatorMethod<int, int>>);
static_assert(std::is_base_of_v<BinaryOperatorMethod, LiftedBinaryOperatorMethod>);
static_assert(std::is_base_of_v<ILiftedOperator, LiftedBinaryOperatorMethod>);
static_assert(std::is_final_v<LiftedBinaryOperatorMethod>);

// ---------------------------------------------------------------------------
// RelationalOperatorMethod<T1, T2>
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsRelationalTest, CtorResolvesBooleanReturnTypeAndSharedParameters) {
    // The diagonal shape the four comparison tables instantiate: both parameters are
    // the SAME shared normal-table instance (the `MakeParameter(TypeCodeFor<T>)` calls
    // resolve the same instance for T1 == T2).
    auto method = std::make_shared<RelationalOperatorMethod<std::int32_t, std::int32_t>>(
        Operators(), [](std::int32_t a, std::int32_t b) { return a < b; });
    // The C# `this.ReturnType = operators.compilation.FindType(KnownTypeCode.Boolean)`.
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean));
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsRelationalTest, CtorResolvesTheDistinctTypeParameters) {
    // A mixed-type instantiation (the ctor supports any T1/T2 pair -- the shift tables
    // use the mixed shape in the arithmetic region): the two parameters come from the
    // T1/T2 TypeCodes respectively.
    auto method = std::make_shared<RelationalOperatorMethod<std::int32_t, std::int64_t>>(
        Operators(), [](std::int32_t a, std::int64_t b) { return a < b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> int32p = Operators().MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> int64p = Operators().MakeParameter(TypeCode::Int64);
    ASSERT_NE(int32p, nullptr);
    ASSERT_NE(int64p, nullptr);
    EXPECT_EQ(parameters[0], int32p.get());
    EXPECT_EQ(parameters[1], int64p.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int64));
    EXPECT_NE(parameters[0], parameters[1]);
    // The return type is Boolean regardless of the operand types.
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean));
}

TEST(CSharpOperatorsRelationalTest, CanEvaluateAtCompileTimeIsAlwaysTrue) {
    // The C# `public override bool CanEvaluateAtCompileTime => true`.
    RelationalOperatorMethod<std::int32_t, std::int32_t> lessThan(
        Operators(), [](std::int32_t a, std::int32_t b) { return a < b; });
    RelationalOperatorMethod<Decimal, Decimal> decimalCompare(
        Operators(), [](Decimal a, Decimal b) { return a >= b; });
    EXPECT_TRUE(lessThan.CanEvaluateAtCompileTime());
    EXPECT_TRUE(decimalCompare.CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsRelationalTest, LiftBuildsTheLiftedBinaryFormKeepingThePlainBoolean) {
    // THE CRUX: the C# `Lift` builds the LiftedBinaryOperatorMethod (whose ctor lifts
    // the return type to Nullable<bool>) and then resets it -- "don't lift the return
    // type for relational operators" (a lifted comparison of possibly-null operands
    // still produces a definite bool).
    RelationalOperatorMethod<std::int32_t, std::int32_t> method(
        Operators(), [](std::int32_t a, std::int32_t b) { return a < b; });
    std::shared_ptr<OperatorMethod> lifted = method.Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    EXPECT_NE(dynamic_cast<const LiftedBinaryOperatorMethod*>(lifted.get()), nullptr);
    // The return type stays the PLAIN Boolean -- not Nullable<bool>.
    EXPECT_FALSE(IsNullable(lifted->ReturnType()));
    EXPECT_EQ(&lifted->ReturnType(), TypeFor(TypeCode::Boolean));
    // The parameters ARE the lifted Nullable<T> instances the LiftedBinaryOperatorMethod
    // ctor built (the reset touches only the return type).
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal));
    ASSERT_NE(nullable, nullptr);
    std::vector<const IParameter*> parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(parameters[0], nullable.get());
    EXPECT_EQ(parameters[1], nullable.get());
}

TEST(CSharpOperatorsRelationalTest, LiftDiffersFromTheArithmeticLiftedReturnTypes) {
    // Pins that the relational reset is load-bearing: the ARITHMETIC lifted form (the
    // LambdaBinaryOperatorMethod::Lift, no reset) lifts its return type to Nullable<T>,
    // while the RELATIONAL lifted form keeps the plain Boolean.
    LambdaBinaryOperatorMethod<std::int32_t, std::int32_t> arithmetic(
        Operators(),
        [](std::int32_t a, std::int32_t b) { return a + b; },
        [](std::int32_t a, std::int32_t b) { return a + b; });
    RelationalOperatorMethod<std::int32_t, std::int32_t> relational(
        Operators(), [](std::int32_t a, std::int32_t b) { return a < b; });
    std::shared_ptr<OperatorMethod> arithmeticLifted = arithmetic.Lift(Operators());
    std::shared_ptr<OperatorMethod> relationalLifted = relational.Lift(Operators());
    ASSERT_NE(arithmeticLifted, nullptr);
    ASSERT_NE(relationalLifted, nullptr);
    EXPECT_TRUE(IsNullable(arithmeticLifted->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(arithmeticLifted->ReturnType()), TypeFor(TypeCode::Int32));
    EXPECT_FALSE(IsNullable(relationalLifted->ReturnType()));
    EXPECT_EQ(&relationalLifted->ReturnType(), TypeFor(TypeCode::Boolean));
}

TEST(CSharpOperatorsRelationalTest, LiftedRelationalExposesTheNonLiftedSurface) {
    // The ILiftedOperator cross-cast (the BetterFunctionMember non-lifted-operator
    // tiebreak shape): the NonLifted parameters/return type expose the relational
    // original's normal signature.
    RelationalOperatorMethod<std::uint64_t, std::uint64_t> method(
        Operators(), [](std::uint64_t a, std::uint64_t b) { return a > b; });
    std::shared_ptr<OperatorMethod> lifted = method.Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    const ILiftedOperator* liftedOperator =
        dynamic_cast<const ILiftedOperator*>(static_cast<const OperatorMethod*>(lifted.get()));
    ASSERT_NE(liftedOperator, nullptr);
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::UInt64);
    ASSERT_NE(normal, nullptr);
    ASSERT_EQ(liftedOperator->NonLiftedParameters().size(), 2u);
    EXPECT_EQ(liftedOperator->NonLiftedParameters()[0], normal.get());
    EXPECT_EQ(liftedOperator->NonLiftedParameters()[1], normal.get());
    EXPECT_EQ(&liftedOperator->NonLiftedReturnType(), TypeFor(TypeCode::Boolean));
    EXPECT_EQ(&liftedOperator->NonLiftedReturnType(), &method.ReturnType());
}

TEST(CSharpOperatorsRelationalTest, LiftedRelationalIsNotLiftedAgain) {
    // The lifted form inherits the OperatorMethod `Lift` default (null) -- a lifted
    // operator is not lifted again.
    RelationalOperatorMethod<double, double> method(
        Operators(), [](double a, double b) { return a <= b; });
    std::shared_ptr<OperatorMethod> lifted = method.Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    EXPECT_EQ(lifted->Lift(Operators()), nullptr);
}

// ---------------------------------------------------------------------------
// The Decimal stand-in's comparison operators (the `<decimal,decimal>` entry bodies)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsRelationalTest, DecimalComparisonsOrderBySignThenMagnitude) {
    Decimal three{3, 0, false};
    Decimal five{5, 0, false};
    Decimal negativeThree{3, 0, true};
    Decimal negativeFive{5, 0, true};
    EXPECT_TRUE(three < five);
    EXPECT_TRUE(three <= five);
    EXPECT_FALSE(three > five);
    EXPECT_FALSE(three >= five);
    // A negative value is less than a non-negative one.
    EXPECT_TRUE(negativeThree < three);
    EXPECT_TRUE(negativeThree <= three);
    EXPECT_FALSE(negativeThree > three);
    // Equal values satisfy <= and >= but neither < nor >.
    EXPECT_FALSE(three < three);
    EXPECT_TRUE(three <= three);
    EXPECT_TRUE(three >= three);
    EXPECT_FALSE(three > three);
    (void)negativeFive;
}

TEST(CSharpOperatorsRelationalTest, DecimalComparisonsInvertForNegatives) {
    // Same-sign values compare by magnitude with the direction inverted: -5 < -3.
    Decimal negativeThree{3, 0, true};
    Decimal negativeFive{5, 0, true};
    EXPECT_TRUE(negativeFive < negativeThree);
    EXPECT_TRUE(negativeFive <= negativeThree);
    EXPECT_FALSE(negativeFive > negativeThree);
    EXPECT_FALSE(negativeFive >= negativeThree);
}

TEST(CSharpOperatorsRelationalTest, DecimalComparisonsAlignScales) {
    // Both magnitudes scale to the larger scale before comparing (the addition
    // alignment): 0.5 (mantissa 5, scale 1) equals 0.50 (mantissa 50, scale 2), and
    // 0.5 < 4 (mantissa 4, scale 0).
    Decimal half{5, 1, false};
    Decimal halfAgain{50, 2, false};
    Decimal four{4, 0, false};
    EXPECT_TRUE(half <= halfAgain);
    EXPECT_TRUE(half >= halfAgain);
    EXPECT_FALSE(half < halfAgain);
    EXPECT_FALSE(half > halfAgain);
    EXPECT_TRUE(half < four);
    EXPECT_TRUE(four > half);
}

// ---------------------------------------------------------------------------
// The four lazy relational operator tables
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsRelationalTest, LessThanOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().LessThanOperators();
    // The C# 7.10 relational set: the seven numeric originals, then their seven lifted
    // forms -- 14 entries.
    ASSERT_EQ(table.size(), 14u);
    ExpectRelationalOriginalAt(table, 0, TypeCode::Int32);
    ExpectRelationalOriginalAt(table, 1, TypeCode::UInt32);
    ExpectRelationalOriginalAt(table, 2, TypeCode::Int64);
    ExpectRelationalOriginalAt(table, 3, TypeCode::UInt64);
    ExpectRelationalOriginalAt(table, 4, TypeCode::Single);
    ExpectRelationalOriginalAt(table, 5, TypeCode::Double);
    ExpectRelationalOriginalAt(table, 6, TypeCode::Decimal);
    ExpectRelationalLiftedAt(table, 7, TypeCode::Int32);
    ExpectRelationalLiftedAt(table, 8, TypeCode::UInt32);
    ExpectRelationalLiftedAt(table, 9, TypeCode::Int64);
    ExpectRelationalLiftedAt(table, 10, TypeCode::UInt64);
    ExpectRelationalLiftedAt(table, 11, TypeCode::Single);
    ExpectRelationalLiftedAt(table, 12, TypeCode::Double);
    ExpectRelationalLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsRelationalTest, LessThanOrEqualOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().LessThanOrEqualOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectRelationalOriginalAt(table, 0, TypeCode::Int32);
    ExpectRelationalOriginalAt(table, 6, TypeCode::Decimal);
    ExpectRelationalLiftedAt(table, 7, TypeCode::Int32);
    ExpectRelationalLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsRelationalTest, GreaterThanOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().GreaterThanOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectRelationalOriginalAt(table, 0, TypeCode::Int32);
    ExpectRelationalOriginalAt(table, 6, TypeCode::Decimal);
    ExpectRelationalLiftedAt(table, 7, TypeCode::Int32);
    ExpectRelationalLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsRelationalTest, GreaterThanOrEqualOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().GreaterThanOrEqualOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectRelationalOriginalAt(table, 0, TypeCode::Int32);
    ExpectRelationalOriginalAt(table, 6, TypeCode::Decimal);
    ExpectRelationalLiftedAt(table, 7, TypeCode::Int32);
    ExpectRelationalLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsRelationalTest, RelationalOperatorTablesMemoizeTheBuiltLists) {
    // The C# LazyInit memoization (convention (m)): a repeat call returns the SAME list
    // (the reference identity) with the same method instances.
    const std::vector<std::shared_ptr<OperatorMethod>>& first =
        Operators().LessThanOperators();
    std::vector<std::shared_ptr<OperatorMethod>> snapshot(first);
    const std::vector<std::shared_ptr<OperatorMethod>>& second =
        Operators().LessThanOperators();
    EXPECT_EQ(&first, &second);
    ASSERT_EQ(snapshot.size(), second.size());
    for (std::size_t i = 0; i < snapshot.size(); i++) {
        EXPECT_EQ(snapshot[i].get(), second[i].get()) << "index " << i;
    }
    // The four tables are independent memos (each builds its own list).
    const std::vector<std::shared_ptr<OperatorMethod>>& lessThanOrEqual =
        Operators().LessThanOrEqualOperators();
    ASSERT_EQ(lessThanOrEqual.size(), 14u);
    EXPECT_NE(first[0].get(), lessThanOrEqual[0].get());
    const std::vector<std::shared_ptr<OperatorMethod>>& greaterThan =
        Operators().GreaterThanOperators();
    ASSERT_EQ(greaterThan.size(), 14u);
    EXPECT_NE(first[0].get(), greaterThan[0].get());
    const std::vector<std::shared_ptr<OperatorMethod>>& greaterThanOrEqual =
        Operators().GreaterThanOrEqualOperators();
    ASSERT_EQ(greaterThanOrEqual.size(), 14u);
    EXPECT_NE(first[0].get(), greaterThanOrEqual[0].get());
}

TEST(CSharpOperatorsRelationalTest, RelationalTableLiftedFormsCrossCastAndOriginalsDoNot) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().GreaterThanOperators();
    ASSERT_EQ(table.size(), 14u);
    // The BetterFunctionMember non-lifted-operator tiebreak shape (the D549 cross-cast
    // from the OperatorMethod base): only the lifted half implements ILiftedOperator.
    for (std::size_t i = 0; i < 7; i++) {
        EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
    for (std::size_t i = 7; i < 14; i++) {
        EXPECT_NE(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
}

} // namespace
