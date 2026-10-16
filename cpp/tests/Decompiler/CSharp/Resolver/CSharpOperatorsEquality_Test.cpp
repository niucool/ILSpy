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

// Tests for the CSharpOperators equality operator region (CSharpOperators.cs lines
// 700-786 + 789-862): the `EqualityOperatorMethod` (a built-in `==`/`!=` operator over one
// TypeCode's operands -- the Boolean return type, the shared normal-table parameter
// instances, the `Type`/`Negate` fields, the `CanEvaluateAtCompileTime => Type !=
// TypeCode.Object` flag, the `Lift` guard keeping the reference-typed Object/String forms
// unlifted), the `LiftedEqualityOperatorMethod` (the `Nullable<T>` form -- both
// parameters lifted but the return type STAYS the plain Boolean, the same shared
// nullable parameter instance added twice, the `ILiftedOperator` surface), and the four
// lazy equality operator tables (value equality / value inequality / reference equality /
// reference inequality).
//
// The registered-compilation stub is the CSharpOperatorsBinary_Test.cpp precedent: every
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
using ILSpy::Decompiler::CSharp::Resolver::EqualityOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::LiftedEqualityOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
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
// CSharpOperatorsBinary_Test precedent).
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

// Asserts the table entry at `index` is an ORIGINAL `==`/`!=` operator over the TypeCode:
// both parameters are the SAME shared normal-table instance of the code, the return type is
// Boolean, the flag follows `Type != TypeCode.Object`, and the `Type`/`Negate` fields
// expose the ctor arguments.
void ExpectEqualityOriginalAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                              std::size_t index, TypeCode code, bool negate) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(code);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get()) << "index " << index;
    EXPECT_EQ(parameters[1], shared.get()) << "index " << index;
    // The C# `this.ReturnType = operators.compilation.FindType(KnownTypeCode.Boolean)`.
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean)) << "index " << index;
    const EqualityOperatorMethod* equality =
        dynamic_cast<const EqualityOperatorMethod*>(method);
    ASSERT_NE(equality, nullptr) << "index " << index;
    EXPECT_EQ(equality->Type(), code) << "index " << index;
    EXPECT_EQ(equality->Negate(), negate) << "index " << index;
    EXPECT_EQ(equality->CanEvaluateAtCompileTime(), code != TypeCode::Object)
        << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED `==`/`!=` form over the TypeCode: both
// parameters are the SAME shared nullable-table instance, the return type STAYS the base's
// plain Boolean (NOT Nullable<bool>), the entry cross-casts to ILiftedOperator, and the
// NonLifted surface exposes the base's parameter list and return type.
void ExpectEqualityLiftedAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                            std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const LiftedEqualityOperatorMethod* lifted =
        dynamic_cast<const LiftedEqualityOperatorMethod*>(method);
    ASSERT_NE(lifted, nullptr) << "index " << index;
    // The crux: a lifted comparison still produces a definite bool -- the return type is
    // NOT Nullable<bool>.
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
    // The lifted flag delegates to the base (a value TypeCode's flag is true).
    EXPECT_TRUE(lifted->CanEvaluateAtCompileTime()) << "index " << index;
}

// Compile-time pins: the class-shape conventions (the C# `internal class` bases are
// unsealed; the Equality/Lifted classes are `sealed`).
static_assert(std::is_base_of_v<BinaryOperatorMethod, EqualityOperatorMethod>);
static_assert(std::is_final_v<EqualityOperatorMethod>);
static_assert(std::is_base_of_v<BinaryOperatorMethod, LiftedEqualityOperatorMethod>);
static_assert(std::is_base_of_v<ILiftedOperator, LiftedEqualityOperatorMethod>);
static_assert(std::is_final_v<LiftedEqualityOperatorMethod>);

// ---------------------------------------------------------------------------
// EqualityOperatorMethod
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsEqualityTest, CtorResolvesBooleanReturnTypeAndSharedParameters) {
    EqualityOperatorMethod method(Operators(), TypeCode::Int32, false);
    // The C# `this.ReturnType = operators.compilation.FindType(KnownTypeCode.Boolean)`.
    EXPECT_EQ(&method.ReturnType(), TypeFor(TypeCode::Boolean));
    std::vector<const IParameter*> parameters = method.Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    // The C# `parameters.Add(operators.MakeParameter(type))` twice -- the SAME shared
    // normal-table instance for both operands (the diagonal `T == T` shape).
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int32));
}

TEST(CSharpOperatorsEqualityTest, TypeAndNegateFieldsExposeTheCtorArguments) {
    EqualityOperatorMethod equality(Operators(), TypeCode::Int64, false);
    EqualityOperatorMethod inequality(Operators(), TypeCode::Int64, true);
    EXPECT_EQ(equality.Type(), TypeCode::Int64);
    EXPECT_FALSE(equality.Negate());
    EXPECT_EQ(inequality.Type(), TypeCode::Int64);
    EXPECT_TRUE(inequality.Negate());
}

TEST(CSharpOperatorsEqualityTest, CanEvaluateAtCompileTimeExcludesObjectOnly) {
    // The C# `CanEvaluateAtCompileTime => Type != TypeCode.Object` -- even the String form
    // folds (only the reference comparison against Object is not constant-evaluable).
    EqualityOperatorMethod object(Operators(), TypeCode::Object, false);
    EqualityOperatorMethod string(Operators(), TypeCode::String, false);
    EqualityOperatorMethod int32(Operators(), TypeCode::Int32, false);
    EXPECT_FALSE(object.CanEvaluateAtCompileTime());
    EXPECT_TRUE(string.CanEvaluateAtCompileTime());
    EXPECT_TRUE(int32.CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsEqualityTest, LiftReturnsNullForObjectAndString) {
    // The C# `Lift` guard: reference-typed operands do not lift.
    EqualityOperatorMethod object(Operators(), TypeCode::Object, false);
    EqualityOperatorMethod string(Operators(), TypeCode::String, false);
    EXPECT_EQ(object.Lift(Operators()), nullptr);
    EXPECT_EQ(string.Lift(Operators()), nullptr);
}

TEST(CSharpOperatorsEqualityTest, LiftBuildsTheLiftedEqualityForm) {
    EqualityOperatorMethod method(Operators(), TypeCode::Int32, false);
    std::shared_ptr<OperatorMethod> lifted = method.Lift(Operators());
    ASSERT_NE(lifted, nullptr);
    EXPECT_NE(dynamic_cast<const LiftedEqualityOperatorMethod*>(lifted.get()), nullptr);
    // The lifted parameter is the shared nullable-table instance (the MakeNullableParameter
    // reference-equality lookup over the base's first parameter).
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::Int32);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal));
    ASSERT_NE(nullable, nullptr);
    std::vector<const IParameter*> parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(parameters[0], nullable.get());
    EXPECT_EQ(parameters[1], nullable.get());
}

// ---------------------------------------------------------------------------
// LiftedEqualityOperatorMethod
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsEqualityTest, LiftedEqualityKeepsThePlainBooleanReturnType) {
    // The crux: unlike the arithmetic lifted forms (whose return type lifts to
    // Nullable<T>), a lifted comparison of possibly-null operands still produces a
    // definite bool -- `this.ReturnType = baseMethod.ReturnType` keeps the base's Boolean.
    EqualityOperatorMethod base(Operators(), TypeCode::Single, false);
    LiftedEqualityOperatorMethod lifted(Operators(), base);
    EXPECT_FALSE(IsNullable(lifted.ReturnType()));
    EXPECT_EQ(&lifted.ReturnType(), TypeFor(TypeCode::Boolean));
    // The ILiftedOperator surface exposes the SAME Boolean as the non-lifted return type.
    EXPECT_EQ(&lifted.NonLiftedReturnType(), &base.ReturnType());
}

TEST(CSharpOperatorsEqualityTest, LiftedEqualityParametersAreTheSameSharedInstance) {
    EqualityOperatorMethod base(Operators(), TypeCode::Decimal, true);
    LiftedEqualityOperatorMethod lifted(Operators(), base);
    std::vector<const IParameter*> parameters = lifted.Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    // The C# `IParameter p = operators.MakeNullableParameter(baseMethod.Parameters[0]);
    // parameters.Add(p); parameters.Add(p);` -- the SAME instance added twice.
    EXPECT_EQ(parameters[0], parameters[1]);
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(TypeCode::Decimal);
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal));
    ASSERT_NE(nullable, nullptr);
    EXPECT_EQ(parameters[0], nullable.get());
    // The NonLiftedParameters expose the base's (normal) parameter list.
    std::vector<const IParameter*> nonLifted = lifted.NonLiftedParameters();
    ASSERT_EQ(nonLifted.size(), 2u);
    EXPECT_EQ(nonLifted[0], normal.get());
    EXPECT_EQ(nonLifted[1], normal.get());
}

TEST(CSharpOperatorsEqualityTest, LiftedEqualityCanEvaluateDelegatesToBase) {
    // The C# `CanEvaluateAtCompileTime => baseMethod.CanEvaluateAtCompileTime` -- a value
    // TypeCode's flag is true through the delegation.
    EqualityOperatorMethod base(Operators(), TypeCode::Boolean, false);
    LiftedEqualityOperatorMethod lifted(Operators(), base);
    EXPECT_TRUE(base.CanEvaluateAtCompileTime());
    EXPECT_TRUE(lifted.CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsEqualityTest, LiftedEqualityOverNonLiftableBaseThrows) {
    // The C# ctor unconditionally calls MakeNullableParameter over the base's first
    // parameter; the Object/String parameters are outside the nullable table (the table
    // covers only TypeCode.Boolean..Decimal), so the C# ArgumentException (the port's
    // std::invalid_argument) fires -- constructing the lifted form over a non-liftable
    // base is the shape the `Lift` guard exists to prevent.
    EqualityOperatorMethod object(Operators(), TypeCode::Object, false);
    EqualityOperatorMethod string(Operators(), TypeCode::String, false);
    EXPECT_THROW(LiftedEqualityOperatorMethod(Operators(), object), std::invalid_argument);
    EXPECT_THROW(LiftedEqualityOperatorMethod(Operators(), string), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// The four lazy equality operator tables
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsEqualityTest, ValueEqualityOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().ValueEqualityOperators();
    // The C# 7.10 value equality set: the eight value-type originals, then their eight
    // lifted forms -- 16 entries.
    ASSERT_EQ(table.size(), 16u);
    ExpectEqualityOriginalAt(table, 0, TypeCode::Int32, false);
    ExpectEqualityOriginalAt(table, 1, TypeCode::UInt32, false);
    ExpectEqualityOriginalAt(table, 2, TypeCode::Int64, false);
    ExpectEqualityOriginalAt(table, 3, TypeCode::UInt64, false);
    ExpectEqualityOriginalAt(table, 4, TypeCode::Single, false);
    ExpectEqualityOriginalAt(table, 5, TypeCode::Double, false);
    ExpectEqualityOriginalAt(table, 6, TypeCode::Decimal, false);
    ExpectEqualityOriginalAt(table, 7, TypeCode::Boolean, false);
    ExpectEqualityLiftedAt(table, 8, TypeCode::Int32);
    ExpectEqualityLiftedAt(table, 9, TypeCode::UInt32);
    ExpectEqualityLiftedAt(table, 10, TypeCode::Int64);
    ExpectEqualityLiftedAt(table, 11, TypeCode::UInt64);
    ExpectEqualityLiftedAt(table, 12, TypeCode::Single);
    ExpectEqualityLiftedAt(table, 13, TypeCode::Double);
    ExpectEqualityLiftedAt(table, 14, TypeCode::Decimal);
    ExpectEqualityLiftedAt(table, 15, TypeCode::Boolean);
}

TEST(CSharpOperatorsEqualityTest, ValueInequalityOperatorsTableHasOriginalsThenLifts) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().ValueInequalityOperators();
    // The same eight originals with negate=true, then their lifted forms.
    ASSERT_EQ(table.size(), 16u);
    ExpectEqualityOriginalAt(table, 0, TypeCode::Int32, true);
    ExpectEqualityOriginalAt(table, 3, TypeCode::UInt64, true);
    ExpectEqualityOriginalAt(table, 6, TypeCode::Decimal, true);
    ExpectEqualityOriginalAt(table, 7, TypeCode::Boolean, true);
    ExpectEqualityLiftedAt(table, 8, TypeCode::Int32);
    ExpectEqualityLiftedAt(table, 15, TypeCode::Boolean);
}

TEST(CSharpOperatorsEqualityTest, ReferenceEqualityOperatorsTableHasObjectAndStringOnly) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().ReferenceEqualityOperators();
    // The Object and String originals -- neither lifts (reference-typed operands), so the
    // table is exactly the two originals.
    ASSERT_EQ(table.size(), 2u);
    ExpectEqualityOriginalAt(table, 0, TypeCode::Object, false);
    ExpectEqualityOriginalAt(table, 1, TypeCode::String, false);
}

TEST(CSharpOperatorsEqualityTest, ReferenceInequalityOperatorsTableHasObjectAndStringOnly) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().ReferenceInequalityOperators();
    ASSERT_EQ(table.size(), 2u);
    ExpectEqualityOriginalAt(table, 0, TypeCode::Object, true);
    ExpectEqualityOriginalAt(table, 1, TypeCode::String, true);
}

TEST(CSharpOperatorsEqualityTest, EqualityOperatorTablesMemoizeTheBuiltLists) {
    // The C# LazyInit memoization (convention (m)): a repeat call returns the SAME list
    // (the reference identity) with the same method instances.
    const std::vector<std::shared_ptr<OperatorMethod>>& first =
        Operators().ValueEqualityOperators();
    std::vector<std::shared_ptr<OperatorMethod>> snapshot(first);
    const std::vector<std::shared_ptr<OperatorMethod>>& second =
        Operators().ValueEqualityOperators();
    EXPECT_EQ(&first, &second);
    ASSERT_EQ(snapshot.size(), second.size());
    for (std::size_t i = 0; i < snapshot.size(); i++) {
        EXPECT_EQ(snapshot[i].get(), second[i].get()) << "index " << i;
    }
    // The four tables are independent memos (each builds its own list).
    const std::vector<std::shared_ptr<OperatorMethod>>& inequality =
        Operators().ValueInequalityOperators();
    ASSERT_EQ(inequality.size(), 16u);
    EXPECT_NE(first[0].get(), inequality[0].get());
    const std::vector<std::shared_ptr<OperatorMethod>>& reference =
        Operators().ReferenceEqualityOperators();
    ASSERT_EQ(reference.size(), 2u);
    EXPECT_NE(first[0].get(), reference[0].get());
}

TEST(CSharpOperatorsEqualityTest, EqualityTableLiftedFormsCrossCastAndOriginalsDoNot) {
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().ValueEqualityOperators();
    ASSERT_EQ(table.size(), 16u);
    // The BetterFunctionMember non-lifted-operator tiebreak shape (the D549 cross-cast
    // from the OperatorMethod base): only the lifted half implements ILiftedOperator.
    for (std::size_t i = 0; i < 8; i++) {
        EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
    for (std::size_t i = 8; i < 16; i++) {
        EXPECT_NE(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
    // The reference tables have no lifted forms at all.
    const std::vector<std::shared_ptr<OperatorMethod>>& reference =
        Operators().ReferenceEqualityOperators();
    ASSERT_EQ(reference.size(), 2u);
    for (std::size_t i = 0; i < 2; i++) {
        EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(reference[i].get())),
                  nullptr)
            << "index " << i;
    }
}

} // namespace
