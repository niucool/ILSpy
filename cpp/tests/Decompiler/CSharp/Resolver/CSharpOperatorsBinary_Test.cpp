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

// Tests for the CSharpOperators binary operator region (CSharpOperators.cs lines
// 411-481 + 622-655 + 484-695): the `BinaryOperatorMethod` base (the
// `CanEvaluateAtCompileTime` flag), the lambda-backed `LambdaBinaryOperatorMethod<T1,T2>`
// (the checked/unchecked func pair, the `Type.GetTypeCode(typeof(T1/T2))`-resolved types,
// the `Lift` override), the `LiftedBinaryOperatorMethod` (the `Nullable<T>` lifted form
// implementing `ILiftedOperator`), the `StringConcatenation` (the built-in string
// concatenations -- only `string + string` is constant-evaluable, never lifted), and the
// eight lazy arithmetic operator tables (multiplication / division / remainder /
// addition / subtraction / shift left / shift right / unsigned shift right -- each the
// originals followed by their lifted forms via `Lift`).
//
// The registered-compilation stub is the CSharpOperatorsUnary_Test.cpp precedent: every
// `KnownTypeCode` the parameter and operator tables resolve must be registered with a
// shared-managed `LookupTypeDefinition` (the ctors recover owning handles through
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
#include <stdexcept>
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
using ILSpy::Decompiler::CSharp::Resolver::StringConcatenation;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the parameter and operator tables
// resolve registered as a shared-managed `LookupTypeDefinition` (the
// CSharpOperatorsUnary_Test precedent).
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
// the lambda ctors, and the lifted forms all resolve (pointer identity).
const IType* TypeFor(TypeCode code) {
    return &Compilation().FindType(static_cast<KnownTypeCode>(code));
}

// A lambda-backed int32 x int32 binary operator (the checked/unchecked multiply pair of
// the multiplication table).
std::shared_ptr<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>
MakeInt32MultiplyOperator() {
    return std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
        Operators(),
        [](std::int32_t a, std::int32_t b) { return a * b; },
        [](std::int32_t a, std::int32_t b) { return a * b; });
}

// Asserts the table entry at `index` is an ORIGINAL with the two parameters and the
// return type of the TypeCode (the diagonal int x int shape), both parameter instances
// shared with the CSharpOperators table.
void ExpectOriginalAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
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
    // The C# `this.ReturnType = operators.compilation.FindType(t1)`.
    EXPECT_EQ(&method->ReturnType(), TypeFor(code)) << "index " << index;
}

// Asserts the table entry at `index` is an ORIGINAL with the two given parameter codes
// (the shift shape: T1 x int) and the FIRST code's return type.
void ExpectOriginalWithCodesAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                               std::size_t index, TypeCode code1, TypeCode code2) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> first = Operators().MakeParameter(code1);
    std::shared_ptr<const IParameter> second = Operators().MakeParameter(code2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(parameters[0], first.get()) << "index " << index;
    EXPECT_EQ(parameters[1], second.get()) << "index " << index;
    EXPECT_EQ(&method->ReturnType(), TypeFor(code1)) << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED form over the TypeCode's original: the
// return type is `Nullable<T>` and both parameters are the shared nullable table
// instances (the diagonal shape).
void ExpectLiftedAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                    std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const IType* returnType = &method->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType)) << "index " << index;
    EXPECT_EQ(&GetUnderlyingType(*returnType), TypeFor(code)) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(code);
    ASSERT_NE(normal, nullptr);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal)) << "index " << index;
    ASSERT_NE(nullable, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], nullable.get()) << "index " << index;
    EXPECT_EQ(parameters[1], nullable.get()) << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED form over the two given parameter codes
// (the shift shape: Nullable<T1> x Nullable<int>), with the `Nullable<T1>` return type.
void ExpectLiftedWithCodesAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                              std::size_t index, TypeCode code1, TypeCode code2) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const IType* returnType = &method->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType)) << "index " << index;
    EXPECT_EQ(&GetUnderlyingType(*returnType), TypeFor(code1)) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> normal1 = Operators().MakeParameter(code1);
    std::shared_ptr<const IParameter> normal2 = Operators().MakeParameter(code2);
    ASSERT_NE(normal1, nullptr);
    ASSERT_NE(normal2, nullptr);
    std::shared_ptr<const IParameter> nullable1;
    std::shared_ptr<const IParameter> nullable2;
    ASSERT_NO_THROW(nullable1 = Operators().MakeNullableParameter(*normal1)) << "index " << index;
    ASSERT_NO_THROW(nullable2 = Operators().MakeNullableParameter(*normal2)) << "index " << index;
    ASSERT_NE(nullable1, nullptr) << "index " << index;
    ASSERT_NE(nullable2, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], nullable1.get()) << "index " << index;
    EXPECT_EQ(parameters[1], nullable2.get()) << "index " << index;
}

// Compile-time pins: the class-shape conventions (the C# `internal class` bases are
// unsealed; the Lambda/Lifted/StringConcatenation classes are `sealed`).
static_assert(std::is_base_of_v<OperatorMethod, BinaryOperatorMethod>);
static_assert(!std::is_final_v<BinaryOperatorMethod>);
static_assert(std::is_base_of_v<BinaryOperatorMethod, LiftedBinaryOperatorMethod>);
static_assert(std::is_base_of_v<ILiftedOperator, LiftedBinaryOperatorMethod>);
static_assert(std::is_final_v<LiftedBinaryOperatorMethod>);
static_assert(std::is_final_v<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>);
static_assert(std::is_final_v<StringConcatenation>);

// ---------------------------------------------------------------------------
// BinaryOperatorMethod / LambdaBinaryOperatorMethod<T1,T2>
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBinaryTest, BinaryOperatorMethodCanEvaluateAtCompileTimeDefaultsToFalse) {
    BinaryOperatorMethod method(Compilation());
    // The C# `public virtual bool CanEvaluateAtCompileTime => false` -- the base default.
    EXPECT_FALSE(method.CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodCanEvaluateAtCompileTimeIsTrue) {
    auto method = MakeInt32MultiplyOperator();
    // The C# `public override bool CanEvaluateAtCompileTime => true` -- the lambda-backed
    // operator is compile-time evaluable.
    EXPECT_TRUE(method->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodParametersAreTheSharedTableInstances) {
    auto method = MakeInt32MultiplyOperator();
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    // Both parameters are the precomputed table entry (the C# shared-reference
    // semantics), with the FindType-resolved type.
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodReturnTypeIsTheFindTypeResult) {
    auto method = MakeInt32MultiplyOperator();
    // The C# `this.ReturnType = operators.compilation.FindType(t1)` -- the
    // `Type.GetTypeCode(typeof(T1))` resolution (TypeCodeFor<int> -> Int32).
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodForMixedTypesResolvesBothCodes) {
    // The shift shape `<uint, int>`: the FIRST parameter and the return type come from
    // T1 (UInt32), the SECOND parameter from T2 (Int32) -- the count parameter.
    auto method = std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::int32_t>>(
        Operators(), [](std::uint32_t a, std::int32_t b) { return a << b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::UInt32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::UInt32));
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodForDecimalResolvesTheDecimalCode) {
    // The `decimal` stand-in routes through the same TypeCodeFor mapping:
    // TypeCodeFor<Decimal> -> TypeCode::Decimal (the arithmetic table entry for
    // System.Decimal, whose parameters and return type are the FindType resolution).
    auto method = std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
        Operators(), [](Decimal a, Decimal b) { return a * b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Decimal));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Decimal));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Decimal));
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodSingleFuncCtorBuildsTheSameSignature) {
    // The C# `LambdaBinaryOperatorMethod(operators, func) : this(operators, func, func)`
    // -- the single-func ctor the shift tables use (a shift never overflows, so there is
    // no checked/unchecked distinction). The delegating ctor produces the same
    // parameter/return-type signature as the pair ctor.
    auto method = std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
        Operators(), [](std::int32_t a, std::int32_t b) { return a >> b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodLiftProducesLiftedBinaryOperatorMethod) {
    auto original = MakeInt32MultiplyOperator();
    // The C# `public override OperatorMethod Lift(CSharpOperators operators) => new
    // LiftedBinaryOperatorMethod(operators, this)`.
    std::shared_ptr<OperatorMethod> lifted = original->Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    EXPECT_NE(dynamic_cast<LiftedBinaryOperatorMethod*>(lifted.get()), nullptr);
}

TEST(CSharpOperatorsBinaryTest, LambdaBinaryOperatorMethodToStringRendersTheSignature) {
    auto method = MakeInt32MultiplyOperator();
    // The inherited OperatorMethod renderer: "<ReturnType> operator(<p1, p2>)" with
    // each type as its ReflectionName (the registered Int32 stub is "T9").
    EXPECT_EQ(method->ToString(), "T9 operator(T9, T9)");
}

// ---------------------------------------------------------------------------
// LiftedBinaryOperatorMethod (directly constructed -- the public ctor)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodReturnTypeIsNullableOfTheBaseReturn) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    // The C# `this.ReturnType = NullableType.Create(operators.compilation,
    // baseMethod.ReturnType)` -- the return type is Nullable<T> with the base's return
    // type as the underlying.
    const IType* returnType = &lifted->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType));
    EXPECT_EQ(&GetUnderlyingType(*returnType), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodParametersAreTheSharedNullableInstances) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    std::vector<const IParameter*> parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    // The C# `operators.MakeNullableParameter(baseMethod.Parameters[0/1])` -- the shared
    // nullable table entry for both of the base's parameters.
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(normal, nullptr);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal));
    ASSERT_NE(nullable, nullptr);
    EXPECT_EQ(parameters[0], nullable.get());
    EXPECT_EQ(parameters[1], nullable.get());
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodNonLiftedAccessorsExposeTheBaseMethod) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    const ILiftedOperator* asLifted = dynamic_cast<const ILiftedOperator*>(
        static_cast<const OperatorMethod*>(lifted.get()));
    ASSERT_NE(asLifted, nullptr);
    // The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` --
    // the pre-lifting parameter list (pointer identity with the base method's).
    std::vector<const IParameter*> nonLifted = asLifted->NonLiftedParameters();
    std::vector<const IParameter*> originalParameters = original->Parameters();
    ASSERT_EQ(nonLifted.size(), 2u);
    ASSERT_EQ(originalParameters.size(), 2u);
    EXPECT_EQ(nonLifted[0], originalParameters[0]);
    EXPECT_EQ(nonLifted[1], originalParameters[1]);
    // The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
    EXPECT_EQ(&asLifted->NonLiftedReturnType(), &original->ReturnType());
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodIsAnILiftedOperatorCrossCast) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    // The D549 cross-cast through the OperatorMethod base (the shape the resolver's
    // `member is ILiftedOperator` checks and BetterFunctionMember's non-lifted tiebreak
    // perform): the lifted form cross-casts, the original does not.
    const OperatorMethod* liftedAsBase = static_cast<const OperatorMethod*>(lifted.get());
    EXPECT_NE(dynamic_cast<const ILiftedOperator*>(liftedAsBase), nullptr);
    const OperatorMethod* originalAsBase = static_cast<const OperatorMethod*>(original.get());
    EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(originalAsBase), nullptr);
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodIsNotLiftedAgain) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    // The inherited OperatorMethod default: a lifted operator is not lifted again.
    EXPECT_EQ(lifted->Lift(Operators()), nullptr);
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodCanEvaluateAtCompileTimeIsInheritedFalse) {
    auto original = MakeInt32MultiplyOperator();
    auto lifted = std::make_shared<LiftedBinaryOperatorMethod>(Operators(), *original);
    // The C# LiftedBinaryOperatorMethod does not override CanEvaluateAtCompileTime -- the
    // BinaryOperatorMethod base default (false) is inherited (the C# resolver lifts null
    // operands itself; only the non-lifted lambda operators are constant-evaluable).
    EXPECT_FALSE(lifted->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsBinaryTest, LiftedBinaryOperatorMethodOnShortBaseParameterListThrows) {
    // A degenerate base method with NO parameters (the C# `baseMethod.Parameters[0]`
    // throws IndexOutOfRangeException; the port throws std::out_of_range -- the same
    // bounds contract instead of UB).
    BinaryOperatorMethod emptyBase(Compilation());
    EXPECT_THROW(LiftedBinaryOperatorMethod lifted(Operators(), emptyBase), std::out_of_range);
}

// ---------------------------------------------------------------------------
// StringConcatenation
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBinaryTest, StringConcatenationCanEvaluateOnlyForStringString) {
    // The C# `canEvaluateAtCompileTime = p1 == TypeCode.String && p2 == TypeCode.String`
    // -- only the `string + string` form is constant-evaluable (a `string + object` may
    // invoke ToString at run time).
    auto stringString = std::make_shared<StringConcatenation>(
        Operators(), TypeCode::String, TypeCode::String);
    auto stringObject = std::make_shared<StringConcatenation>(
        Operators(), TypeCode::String, TypeCode::Object);
    auto objectString = std::make_shared<StringConcatenation>(
        Operators(), TypeCode::Object, TypeCode::String);
    EXPECT_TRUE(stringString->CanEvaluateAtCompileTime());
    EXPECT_FALSE(stringObject->CanEvaluateAtCompileTime());
    EXPECT_FALSE(objectString->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsBinaryTest, StringConcatenationReturnTypeIsStringAndParametersMatch) {
    auto stringObject = std::make_shared<StringConcatenation>(
        Operators(), TypeCode::String, TypeCode::Object);
    // The C# `this.ReturnType = operators.compilation.FindType(KnownTypeCode.String)`.
    EXPECT_EQ(&stringObject->ReturnType(), TypeFor(TypeCode::String));
    std::vector<const IParameter*> parameters = stringObject->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    // The parameters are the shared table entries for the two codes.
    std::shared_ptr<const IParameter> stringParam = Operators().MakeParameter(TypeCode::String);
    std::shared_ptr<const IParameter> objectParam = Operators().MakeParameter(TypeCode::Object);
    ASSERT_NE(stringParam, nullptr);
    ASSERT_NE(objectParam, nullptr);
    EXPECT_EQ(parameters[0], stringParam.get());
    EXPECT_EQ(parameters[1], objectParam.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::String));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Object));
}

TEST(CSharpOperatorsBinaryTest, StringConcatenationIsNotLifted) {
    // The C# StringConcatenation does not override Lift -- the inherited OperatorMethod
    // default returns null (the addition table's lifted forms come from the numeric
    // lambdas only).
    auto stringString = std::make_shared<StringConcatenation>(
        Operators(), TypeCode::String, TypeCode::String);
    EXPECT_EQ(stringString->Lift(Operators()), nullptr);
}

// ---------------------------------------------------------------------------
// The lazy binary operator-table properties
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBinaryTest, MultiplicationOperatorsTableHasSevenOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().MultiplicationOperators();
    // The C# 7.8.1 set: int, uint, long, ulong, float, double, decimal -- then their
    // lifted forms (the Lift contract: all originals first, then the lifted forms).
    ASSERT_EQ(table.size(), 14u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectOriginalAt(table, 4, TypeCode::Single);
    ExpectOriginalAt(table, 5, TypeCode::Double);
    ExpectOriginalAt(table, 6, TypeCode::Decimal);
    ExpectLiftedAt(table, 7, TypeCode::Int32);
    ExpectLiftedAt(table, 8, TypeCode::UInt32);
    ExpectLiftedAt(table, 9, TypeCode::Int64);
    ExpectLiftedAt(table, 10, TypeCode::UInt64);
    ExpectLiftedAt(table, 11, TypeCode::Single);
    ExpectLiftedAt(table, 12, TypeCode::Double);
    ExpectLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsBinaryTest, DivisionOperatorsTableHasSevenOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().DivisionOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectOriginalAt(table, 4, TypeCode::Single);
    ExpectOriginalAt(table, 5, TypeCode::Double);
    ExpectOriginalAt(table, 6, TypeCode::Decimal);
    ExpectLiftedAt(table, 7, TypeCode::Int32);
    ExpectLiftedAt(table, 8, TypeCode::UInt32);
    ExpectLiftedAt(table, 9, TypeCode::Int64);
    ExpectLiftedAt(table, 10, TypeCode::UInt64);
    ExpectLiftedAt(table, 11, TypeCode::Single);
    ExpectLiftedAt(table, 12, TypeCode::Double);
    ExpectLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsBinaryTest, RemainderOperatorsTableHasSevenOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().RemainderOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectOriginalAt(table, 4, TypeCode::Single);
    ExpectOriginalAt(table, 5, TypeCode::Double);
    ExpectOriginalAt(table, 6, TypeCode::Decimal);
    ExpectLiftedAt(table, 7, TypeCode::Int32);
    ExpectLiftedAt(table, 8, TypeCode::UInt32);
    ExpectLiftedAt(table, 9, TypeCode::Int64);
    ExpectLiftedAt(table, 10, TypeCode::UInt64);
    ExpectLiftedAt(table, 11, TypeCode::Single);
    ExpectLiftedAt(table, 12, TypeCode::Double);
    ExpectLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsBinaryTest, AdditionOperatorsTableHasNumericThenStringConcatThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().AdditionOperators();
    // The C# 7.8.3 addition set: the seven numeric originals, then the three built-in
    // string concatenations, then the seven lifted numeric forms (the StringConcatenation
    // entries are NOT lifted) -- 17 entries.
    ASSERT_EQ(table.size(), 17u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectOriginalAt(table, 4, TypeCode::Single);
    ExpectOriginalAt(table, 5, TypeCode::Double);
    ExpectOriginalAt(table, 6, TypeCode::Decimal);
    // The three string concatenations: string+string, string+object, object+string --
    // every entry returns String with the two codes as its parameters.
    for (std::size_t i = 7; i < 10; i++)
    {
        const OperatorMethod* method = table[i].get();
        ASSERT_NE(method, nullptr) << "index " << i;
        EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::String)) << "index " << i;
        std::vector<const IParameter*> parameters = method->Parameters();
        ASSERT_EQ(parameters.size(), 2u) << "index " << i;
    }
    EXPECT_EQ(&table[7]->Parameters()[0]->Type(), TypeFor(TypeCode::String));
    EXPECT_EQ(&table[7]->Parameters()[1]->Type(), TypeFor(TypeCode::String));
    EXPECT_EQ(&table[8]->Parameters()[0]->Type(), TypeFor(TypeCode::String));
    EXPECT_EQ(&table[8]->Parameters()[1]->Type(), TypeFor(TypeCode::Object));
    EXPECT_EQ(&table[9]->Parameters()[0]->Type(), TypeFor(TypeCode::Object));
    EXPECT_EQ(&table[9]->Parameters()[1]->Type(), TypeFor(TypeCode::String));
    // Only the `string + string` form is constant-evaluable.
    const BinaryOperatorMethod* stringString =
        dynamic_cast<const BinaryOperatorMethod*>(table[7].get());
    const BinaryOperatorMethod* stringObject =
        dynamic_cast<const BinaryOperatorMethod*>(table[8].get());
    const BinaryOperatorMethod* objectString =
        dynamic_cast<const BinaryOperatorMethod*>(table[9].get());
    ASSERT_NE(stringString, nullptr);
    ASSERT_NE(stringObject, nullptr);
    ASSERT_NE(objectString, nullptr);
    EXPECT_TRUE(stringString->CanEvaluateAtCompileTime());
    EXPECT_FALSE(stringObject->CanEvaluateAtCompileTime());
    EXPECT_FALSE(objectString->CanEvaluateAtCompileTime());
    // The seven lifted numeric forms (the string concatenations contribute none).
    ExpectLiftedAt(table, 10, TypeCode::Int32);
    ExpectLiftedAt(table, 11, TypeCode::UInt32);
    ExpectLiftedAt(table, 12, TypeCode::Int64);
    ExpectLiftedAt(table, 13, TypeCode::UInt64);
    ExpectLiftedAt(table, 14, TypeCode::Single);
    ExpectLiftedAt(table, 15, TypeCode::Double);
    ExpectLiftedAt(table, 16, TypeCode::Decimal);
}

TEST(CSharpOperatorsBinaryTest, SubtractionOperatorsTableHasSevenOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().SubtractionOperators();
    ASSERT_EQ(table.size(), 14u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectOriginalAt(table, 4, TypeCode::Single);
    ExpectOriginalAt(table, 5, TypeCode::Double);
    ExpectOriginalAt(table, 6, TypeCode::Decimal);
    ExpectLiftedAt(table, 7, TypeCode::Int32);
    ExpectLiftedAt(table, 8, TypeCode::UInt32);
    ExpectLiftedAt(table, 9, TypeCode::Int64);
    ExpectLiftedAt(table, 10, TypeCode::UInt64);
    ExpectLiftedAt(table, 11, TypeCode::Single);
    ExpectLiftedAt(table, 12, TypeCode::Double);
    ExpectLiftedAt(table, 13, TypeCode::Decimal);
}

TEST(CSharpOperatorsBinaryTest, ShiftLeftOperatorsTableHasFourOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().ShiftLeftOperators();
    // The C# 7.8.5 shift set: int, uint, long, ulong -- each shifting by an int count
    // (the second parameter is Int32 for every entry), then their lifted forms.
    ASSERT_EQ(table.size(), 8u);
    ExpectOriginalWithCodesAt(table, 0, TypeCode::Int32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 1, TypeCode::UInt32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 2, TypeCode::Int64, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 3, TypeCode::UInt64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 4, TypeCode::Int32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 5, TypeCode::UInt32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 6, TypeCode::Int64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 7, TypeCode::UInt64, TypeCode::Int32);
}

TEST(CSharpOperatorsBinaryTest, ShiftRightOperatorsTableHasFourOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().ShiftRightOperators();
    ASSERT_EQ(table.size(), 8u);
    ExpectOriginalWithCodesAt(table, 0, TypeCode::Int32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 1, TypeCode::UInt32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 2, TypeCode::Int64, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 3, TypeCode::UInt64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 4, TypeCode::Int32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 5, TypeCode::UInt32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 6, TypeCode::Int64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 7, TypeCode::UInt64, TypeCode::Int32);
}

TEST(CSharpOperatorsBinaryTest, UnsignedShiftRightTableHasFourOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().UnsignedShiftRightOperators();
    ASSERT_EQ(table.size(), 8u);
    ExpectOriginalWithCodesAt(table, 0, TypeCode::Int32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 1, TypeCode::UInt32, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 2, TypeCode::Int64, TypeCode::Int32);
    ExpectOriginalWithCodesAt(table, 3, TypeCode::UInt64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 4, TypeCode::Int32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 5, TypeCode::UInt32, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 6, TypeCode::Int64, TypeCode::Int32);
    ExpectLiftedWithCodesAt(table, 7, TypeCode::UInt64, TypeCode::Int32);
}

TEST(CSharpOperatorsBinaryTest, BinaryOperatorTablesMemoizeTheBuiltLists) {
    // The C# LazyInit memoization (convention (m)): a repeat call returns the SAME list
    // (the reference identity) with the same method instances.
    const std::vector<std::shared_ptr<OperatorMethod>>& first =
        Operators().MultiplicationOperators();
    std::vector<std::shared_ptr<OperatorMethod>> snapshot(first);
    const std::vector<std::shared_ptr<OperatorMethod>>& second =
        Operators().MultiplicationOperators();
    EXPECT_EQ(&first, &second);
    ASSERT_EQ(snapshot.size(), second.size());
    for (std::size_t i = 0; i < snapshot.size(); i++) {
        EXPECT_EQ(snapshot[i].get(), second[i].get()) << "index " << i;
    }
    // The eight tables are independent memos (each builds its own list).
    const std::vector<std::shared_ptr<OperatorMethod>>& division =
        Operators().DivisionOperators();
    ASSERT_EQ(division.size(), 14u);
    EXPECT_NE(first[0].get(), division[0].get());
}

TEST(CSharpOperatorsBinaryTest, BinaryTableLiftedFormsCrossCastAndOriginalsDoNot) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().MultiplicationOperators();
    ASSERT_EQ(table.size(), 14u);
    // The BetterFunctionMember non-lifted-operator tiebreak shape (the D549 cross-cast
    // from the OperatorMethod base): only the lifted half implements ILiftedOperator.
    EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                  static_cast<const OperatorMethod*>(table[0].get())),
              nullptr);
    for (std::size_t i = 7; i < 14; i++) {
        EXPECT_NE(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
}

} // namespace
