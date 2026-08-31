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

// Tests for the CSharpOperators unary operator region (CSharpOperators.cs lines 237-299 +
// 300-409): the `UnaryOperatorMethod` base (the `CanEvaluateAtCompileTime` flag), the
// lambda-backed `LambdaUnaryOperatorMethod<T>` (the `Type.GetTypeCode(typeof(T))`-resolved
// parameter/return types, the `Lift` override), the `LiftedUnaryOperatorMethod`
// (`Nullable<T>` lifted form implementing `ILiftedOperator`), and the five lazy
// operator-table properties (unary plus / unchecked and checked unary minus / logical
// negation / bitwise complement -- each the originals followed by their lifted forms).
//
// The registered-compilation stub is the CSharpOperators_Test.cpp precedent: every
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

using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::Decimal;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::LambdaUnaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedUnaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::UnaryOperatorMethod;
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
// resolve registered as a shared-managed `LookupTypeDefinition` (the CSharpOperators_Test
// precedent; `types[i]` is the registration for `KnownTypeCode(1 + i)` = Object..String,
// and `nullableOfT` is the `System.Nullable\`1` generic definition the lifted forms build
// through `NullableType.Create`).
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

// A lambda-backed int32 unary operator (the `+i` body of the unary-plus table).
std::shared_ptr<LambdaUnaryOperatorMethod<std::int32_t>> MakeInt32Operator() {
    return std::make_shared<LambdaUnaryOperatorMethod<std::int32_t>>(
        Operators(), [](std::int32_t i) { return +i; });
}

// A directly-constructed lifted form over the given base method (the public ctor -- the
// tests exercise the ctor/accessors independently of the `Lift` override; the base method
// stays alive in the caller's scope, the convention (n) contract).
std::shared_ptr<LiftedUnaryOperatorMethod> MakeLifted(const UnaryOperatorMethod& baseMethod) {
    return std::make_shared<LiftedUnaryOperatorMethod>(Operators(), baseMethod);
}

// Asserts the table entry at `index` is an ORIGINAL with the single parameter and the
// return type of the TypeCode (both pointer-identical with the FindType resolution), the
// parameter instance shared with the CSharpOperators table.
void ExpectOriginalAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                      std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 1u) << "index " << index;
    // The C# `parameters.Add(operators.MakeParameter(typeCode))` -- the shared table
    // instance (pointer identity with a fresh MakeParameter lookup).
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(code);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get()) << "index " << index;
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(code)) << "index " << index;
    // The C# `this.ReturnType = operators.compilation.FindType(typeCode)`.
    EXPECT_EQ(&method->ReturnType(), TypeFor(code)) << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED form over the TypeCode's original: the
// return type is `Nullable<T>` (its underlying type is the FindType resolution) and the
// parameter is the shared nullable table instance.
void ExpectLiftedAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                    std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const IType* returnType = &method->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType)) << "index " << index;
    EXPECT_EQ(&GetUnderlyingType(*returnType), TypeFor(code)) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 1u) << "index " << index;
    // The C# `operators.MakeNullableParameter(baseMethod.Parameters[0])` -- the shared
    // nullable table instance (pointer identity).
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(code);
    ASSERT_NE(normal, nullptr);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal)) << "index " << index;
    ASSERT_NE(nullable, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], nullable.get()) << "index " << index;
}

// Compile-time pins: the class-shape conventions (the C# `internal class` is unsealed;
// the Lambda/Lifted classes are `sealed`).
static_assert(std::is_base_of_v<OperatorMethod, UnaryOperatorMethod>);
static_assert(!std::is_final_v<UnaryOperatorMethod>);
static_assert(std::is_final_v<LambdaUnaryOperatorMethod<std::int32_t>>);
static_assert(std::is_base_of_v<UnaryOperatorMethod, LiftedUnaryOperatorMethod>);
static_assert(std::is_base_of_v<ILiftedOperator, LiftedUnaryOperatorMethod>);
static_assert(std::is_final_v<LiftedUnaryOperatorMethod>);

// ---------------------------------------------------------------------------
// UnaryOperatorMethod / LambdaUnaryOperatorMethod<T>
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUnaryTest, UnaryOperatorMethodCanEvaluateAtCompileTimeDefaultsToFalse) {
    UnaryOperatorMethod method(Compilation());
    // The C# `public virtual bool CanEvaluateAtCompileTime => false` -- the base default.
    EXPECT_FALSE(method.CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodCanEvaluateAtCompileTimeIsTrue) {
    auto method = MakeInt32Operator();
    // The C# `public override bool CanEvaluateAtCompileTime => true` -- the lambda-backed
    // operator is compile-time evaluable.
    EXPECT_TRUE(method->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodParameterIsTheSharedTableInstance) {
    auto method = MakeInt32Operator();
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 1u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    // The parameter instance is the precomputed table entry (the C# shared-reference
    // semantics), with the FindType-resolved type.
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodReturnTypeIsTheFindTypeResult) {
    auto method = MakeInt32Operator();
    // The C# `this.ReturnType = operators.compilation.FindType(typeCode)` -- the
    // `Type.GetTypeCode(typeof(T))` resolution (TypeCodeFor<int> -> Int32).
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodForBoolResolvesTheBooleanCode) {
    // The `b => !b` logical-negation operator: TypeCodeFor<bool> -> TypeCode::Boolean.
    auto method = std::make_shared<LambdaUnaryOperatorMethod<bool>>(
        Operators(), [](bool b) { return !b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 1u);
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Boolean));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean));
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodForDecimalResolvesTheDecimalCode) {
    // The `decimal` stand-in routes through the same TypeCodeFor mapping:
    // TypeCodeFor<Decimal> -> TypeCode::Decimal (the operator table entry for
    // System.Decimal, whose parameter/return types are the FindType resolution).
    auto method = std::make_shared<LambdaUnaryOperatorMethod<Decimal>>(
        Operators(), [](Decimal d) { return +d; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 1u);
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Decimal));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Decimal));
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodToStringRendersTheSignature) {
    auto method = MakeInt32Operator();
    // The inherited OperatorMethod renderer: "<ReturnType> operator(<paramType>)" with
    // each type as its ReflectionName (the registered Int32 stub is "T9").
    EXPECT_EQ(method->ToString(), "T9 operator(T9)");
}

TEST(CSharpOperatorsUnaryTest, LambdaUnaryOperatorMethodLiftProducesLiftedUnaryOperatorMethod) {
    auto original = MakeInt32Operator();
    // The C# `public override OperatorMethod Lift(CSharpOperators operators) => new
    // LiftedUnaryOperatorMethod(operators, this)`.
    std::shared_ptr<OperatorMethod> lifted = original->Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    EXPECT_NE(dynamic_cast<LiftedUnaryOperatorMethod*>(lifted.get()), nullptr);
}

// ---------------------------------------------------------------------------
// LiftedUnaryOperatorMethod (directly constructed -- the public ctor)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodReturnTypeIsNullableOfTheBaseReturn) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    // The C# `this.ReturnType = NullableType.Create(baseMethod.Compilation,
    // baseMethod.ReturnType)` -- the return type is Nullable<T> with the base's return
    // type as the underlying.
    const IType* returnType = &lifted->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType));
    EXPECT_EQ(&GetUnderlyingType(*returnType), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodParameterIsTheSharedNullableInstance) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    std::vector<const IParameter*> parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 1u);
    // The C# `operators.MakeNullableParameter(baseMethod.Parameters[0])` -- the shared
    // nullable table entry for the base's parameter.
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(normal, nullptr);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal));
    ASSERT_NE(nullable, nullptr);
    EXPECT_EQ(parameters[0], nullable.get());
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodNonLiftedAccessorsExposeTheBaseMethod) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    const ILiftedOperator* asLifted =
        dynamic_cast<const ILiftedOperator*>(static_cast<const OperatorMethod*>(lifted.get()));
    ASSERT_NE(asLifted, nullptr);
    // The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` --
    // the pre-lifting parameter list (pointer identity with the base method's).
    std::vector<const IParameter*> nonLifted = asLifted->NonLiftedParameters();
    std::vector<const IParameter*> originalParameters = original->Parameters();
    ASSERT_EQ(nonLifted.size(), 1u);
    ASSERT_EQ(originalParameters.size(), 1u);
    EXPECT_EQ(nonLifted[0], originalParameters[0]);
    // The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
    EXPECT_EQ(&asLifted->NonLiftedReturnType(), &original->ReturnType());
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodIsAnILiftedOperatorCrossCast) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    // The D549 cross-cast through the OperatorMethod base (the shape the resolver's
    // `member is ILiftedOperator` checks and BetterFunctionMember's non-lifted tiebreak
    // perform): the lifted form cross-casts, the original does not.
    const OperatorMethod* liftedAsBase = static_cast<const OperatorMethod*>(lifted.get());
    EXPECT_NE(dynamic_cast<const ILiftedOperator*>(liftedAsBase), nullptr);
    const OperatorMethod* originalAsBase = static_cast<const OperatorMethod*>(original.get());
    EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(originalAsBase), nullptr);
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodIsNotLiftedAgain) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    // The inherited OperatorMethod default: a lifted operator is not lifted again.
    EXPECT_EQ(lifted->Lift(Operators()), nullptr);
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodCanEvaluateAtCompileTimeIsInheritedFalse) {
    auto original = MakeInt32Operator();
    auto lifted = MakeLifted(*original);
    // The C# LiftedUnaryOperatorMethod does not override CanEvaluateAtCompileTime -- the
    // UnaryOperatorMethod base default (false) is inherited (only the non-lifted lambda
    // operators are constant-evaluable; the C# resolver lifts null operands itself).
    EXPECT_FALSE(lifted->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsUnaryTest, LiftedUnaryOperatorMethodOnEmptyBaseParameterListThrows) {
    // A degenerate base method with NO parameters (the C# `baseMethod.Parameters[0]`
    // throws IndexOutOfRangeException; the port throws std::out_of_range -- the same
    // bounds contract instead of UB).
    UnaryOperatorMethod emptyBase(Compilation());
    EXPECT_THROW(LiftedUnaryOperatorMethod lifted(Operators(), emptyBase), std::out_of_range);
}

// ---------------------------------------------------------------------------
// The lazy unary operator-table properties
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUnaryTest, UnaryPlusOperatorsTableHasSevenOriginalsThenLiftedForms) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().UnaryPlusOperators();
    // The C# 7.7.1 set: int, uint, long, ulong, float, double, decimal -- then their
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

TEST(CSharpOperatorsUnaryTest, UncheckedUnaryMinusOperatorsTableHasFiveOriginalsAndLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().UncheckedUnaryMinusOperators();
    // The C# 7.7.2 set: int, long, float, double, decimal (the signed and floating types
    // -- no uint/ulong unary minus), then their lifted forms.
    ASSERT_EQ(table.size(), 10u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::Int64);
    ExpectOriginalAt(table, 2, TypeCode::Single);
    ExpectOriginalAt(table, 3, TypeCode::Double);
    ExpectOriginalAt(table, 4, TypeCode::Decimal);
    ExpectLiftedAt(table, 5, TypeCode::Int32);
    ExpectLiftedAt(table, 6, TypeCode::Int64);
    ExpectLiftedAt(table, 7, TypeCode::Single);
    ExpectLiftedAt(table, 8, TypeCode::Double);
    ExpectLiftedAt(table, 9, TypeCode::Decimal);
}

TEST(CSharpOperatorsUnaryTest, CheckedUnaryMinusOperatorsTableHasFiveOriginalsAndLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().CheckedUnaryMinusOperators();
    // The same five originals as the unchecked table (the checked/unchecked bodies differ
    // only in the stored funcs, which the deferred Invoke consumes), then their lifts.
    ASSERT_EQ(table.size(), 10u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::Int64);
    ExpectOriginalAt(table, 2, TypeCode::Single);
    ExpectOriginalAt(table, 3, TypeCode::Double);
    ExpectOriginalAt(table, 4, TypeCode::Decimal);
    ExpectLiftedAt(table, 5, TypeCode::Int32);
    ExpectLiftedAt(table, 6, TypeCode::Int64);
    ExpectLiftedAt(table, 7, TypeCode::Single);
    ExpectLiftedAt(table, 8, TypeCode::Double);
    ExpectLiftedAt(table, 9, TypeCode::Decimal);
}

TEST(CSharpOperatorsUnaryTest, LogicalNegationOperatorsTableHasBoolPlusLiftedBool) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().LogicalNegationOperators();
    // The single bool original (`b => !b`), then its lifted Nullable<bool> form -- the
    // lifted logical negation IS a C# operator.
    ASSERT_EQ(table.size(), 2u);
    ExpectOriginalAt(table, 0, TypeCode::Boolean);
    ExpectLiftedAt(table, 1, TypeCode::Boolean);
}

TEST(CSharpOperatorsUnaryTest, BitwiseComplementOperatorsTableHasFourOriginalsAndLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseComplementOperators();
    // The C# 7.7.4 set: int, uint, long, ulong (the bitwise-complement integer types),
    // then their lifted forms.
    ASSERT_EQ(table.size(), 8u);
    ExpectOriginalAt(table, 0, TypeCode::Int32);
    ExpectOriginalAt(table, 1, TypeCode::UInt32);
    ExpectOriginalAt(table, 2, TypeCode::Int64);
    ExpectOriginalAt(table, 3, TypeCode::UInt64);
    ExpectLiftedAt(table, 4, TypeCode::Int32);
    ExpectLiftedAt(table, 5, TypeCode::UInt32);
    ExpectLiftedAt(table, 6, TypeCode::Int64);
    ExpectLiftedAt(table, 7, TypeCode::UInt64);
}

TEST(CSharpOperatorsUnaryTest, UnaryOperatorTablesMemoizeTheBuiltLists) {
    // The C# LazyInit memoization (convention (m)): a repeat call returns the SAME list
    // (the reference identity) with the same method instances.
    const std::vector<std::shared_ptr<OperatorMethod>>& first = Operators().UnaryPlusOperators();
    std::vector<std::shared_ptr<OperatorMethod>> snapshot(first);
    const std::vector<std::shared_ptr<OperatorMethod>>& second = Operators().UnaryPlusOperators();
    EXPECT_EQ(&first, &second);
    ASSERT_EQ(snapshot.size(), second.size());
    for (std::size_t i = 0; i < snapshot.size(); i++) {
        EXPECT_EQ(snapshot[i].get(), second[i].get()) << "index " << i;
    }
    // The five tables are independent memos (each builds its own list).
    const std::vector<std::shared_ptr<OperatorMethod>>& minus =
        Operators().UncheckedUnaryMinusOperators();
    ASSERT_EQ(minus.size(), 10u);
    EXPECT_NE(first[0].get(), minus[0].get());
}

TEST(CSharpOperatorsUnaryTest, TableLiftedFormsCrossCastToILiftedOperatorAndOriginalsDoNot) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table = Operators().UnaryPlusOperators();
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
