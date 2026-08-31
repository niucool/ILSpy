// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
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

// Tests for the CSharpOperators bitwise operator region (CSharpOperators.cs lines
// 996-1101): the five lazy logical/bitwise operator tables -- `LogicalAndOperators`
// and `LogicalOrOperators` (the single bool `&`/`|` originals, NOT lifted within their
// own tables) and `BitwiseAndOperators` / `BitwiseOrOperators` / `BitwiseXorOperators`
// (the four integer originals plus the bool entry, each followed by their five lifted
// `Nullable<T>` forms via `Lift`). The region's load-bearing crux is the SHARED bool
// entry: the C# `BitwiseAndOperators` appends `this.LogicalAndOperators[0]` (and the
// `|` table `this.LogicalOrOperators[0]`), so the logical and bitwise tables hold
// POINTER-IDENTICAL entries; `BitwiseXorOperators` has no logical twin to share with,
// so its bool entry is a fresh instance.
//
// The registered-compilation stub is the CSharpOperatorsRelational_Test.cpp precedent:
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
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::LambdaBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
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
// CSharpOperatorsRelational_Test precedent).
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

// Asserts the table entry at `index` is an ORIGINAL bitwise/logical operator over the
// TypeCode (the DIAGONAL `T op T` shape the C# tables instantiate): both parameters the
// SAME shared normal-table instance of the code, the return type the SAME code's
// `FindType` result (the `LambdaBinaryOperatorMethod` ctor resolves it from
// `Type.GetTypeCode(typeof(T1))`), the flag true.
void ExpectBitwiseOriginalAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                              std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(code);
    ASSERT_NE(shared, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], shared.get()) << "index " << index;
    EXPECT_EQ(parameters[1], shared.get()) << "index " << index;
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(code)) << "index " << index;
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(code)) << "index " << index;
    // The C# `this.ReturnType = operators.compilation.FindType(t1)` -- the OPERAND's own
    // type (Int32 for the integer forms, Boolean for the bool forms), unlike the
    // comparison operators that always return Boolean.
    EXPECT_EQ(&method->ReturnType(), TypeFor(code)) << "index " << index;
    // The flag lives on the BinaryOperatorMethod base (the C# `public override bool
    // CanEvaluateAtCompileTime => true`).
    const BinaryOperatorMethod* binary = dynamic_cast<const BinaryOperatorMethod*>(method);
    ASSERT_NE(binary, nullptr) << "index " << index;
    EXPECT_TRUE(binary->CanEvaluateAtCompileTime()) << "index " << index;
}

// Asserts the table entry at `index` is a LIFTED bitwise form over the TypeCode: the
// entry IS a LiftedBinaryOperatorMethod whose parameters are the shared nullable-table
// instances, whose return type IS `Nullable<T>` (the ARITHMETIC-style lift -- NOT reset
// to the plain type the way `RelationalOperatorMethod.Lift` resets its lifted return),
// which cross-casts to ILiftedOperator with the NonLifted surface at the base, and which
// does not lift again.
void ExpectBitwiseLiftedAt(const std::vector<std::shared_ptr<OperatorMethod>>& table,
                           std::size_t index, TypeCode code) {
    ASSERT_LT(index, table.size());
    const OperatorMethod* method = table[index].get();
    ASSERT_NE(method, nullptr) << "index " << index;
    const LiftedBinaryOperatorMethod* lifted =
        dynamic_cast<const LiftedBinaryOperatorMethod*>(method);
    ASSERT_NE(lifted, nullptr) << "index " << index;
    // The arithmetic-style lift: the return type IS Nullable<T> (GetUnderlyingType
    // resolves the underlying operand type).
    const IType* returnType = &method->ReturnType();
    EXPECT_TRUE(IsNullable(*returnType)) << "index " << index;
    const IType& underlying = GetUnderlyingType(*returnType);
    EXPECT_EQ(&underlying, TypeFor(code)) << "index " << index;
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u) << "index " << index;
    std::shared_ptr<const IParameter> normal = Operators().MakeParameter(code);
    ASSERT_NE(normal, nullptr) << "index " << index;
    std::shared_ptr<const IParameter> nullable;
    ASSERT_NO_THROW(nullable = Operators().MakeNullableParameter(*normal))
        << "index " << index;
    ASSERT_NE(nullable, nullptr) << "index " << index;
    EXPECT_EQ(parameters[0], nullable.get()) << "index " << index;
    EXPECT_EQ(parameters[1], nullable.get()) << "index " << index;
    // The ILiftedOperator surface: the base's parameter list and return type.
    EXPECT_EQ(lifted->NonLiftedParameters().size(), 2u) << "index " << index;
    EXPECT_EQ(lifted->NonLiftedParameters()[0], normal.get()) << "index " << index;
    EXPECT_EQ(lifted->NonLiftedParameters()[1], normal.get()) << "index " << index;
    EXPECT_EQ(&lifted->NonLiftedReturnType(), TypeFor(code)) << "index " << index;
    // A lifted operator is not lifted again (the OperatorMethod default returns null).
    EXPECT_EQ(method->Lift(Operators()), nullptr) << "index " << index;
}

// Compile-time pins: the class-shape conventions (the C# `internal class` bases are
// unsealed; the LambdaBinaryOperatorMethod is `sealed`).
static_assert(std::is_base_of_v<BinaryOperatorMethod, LambdaBinaryOperatorMethod<bool, bool>>);
static_assert(std::is_final_v<LambdaBinaryOperatorMethod<bool, bool>>);
static_assert(std::is_base_of_v<BinaryOperatorMethod, LiftedBinaryOperatorMethod>);
static_assert(std::is_base_of_v<ILiftedOperator, LiftedBinaryOperatorMethod>);
static_assert(std::is_final_v<LiftedBinaryOperatorMethod>);

// ---------------------------------------------------------------------------
// Direct construction (the sentinel tests -- independent of the lazy tables)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBitwiseTest, CtorResolvesTheIntegerOperandAndReturnTypes) {
    // The diagonal integer shape the bitwise tables instantiate: both parameters are
    // the SAME shared normal-table Int32 instance, and the return type is the operand's
    // own Int32 (`FindType(Type.GetTypeCode(typeof(T1)))`).
    auto method = std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
        Operators(), [](std::int32_t a, std::int32_t b) { return a & b; });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Int32);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Int32));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Int32));
    EXPECT_TRUE(method->CanEvaluateAtCompileTime());
}

TEST(CSharpOperatorsBitwiseTest, BoolBitwiseCtorResolvesBoolean) {
    // The bool shape the logical tables instantiate: both parameters the shared Boolean
    // instance, the return type Boolean (the C# `&`/`|`/`^` on bools produce bool).
    auto method = std::make_shared<LambdaBinaryOperatorMethod<bool, bool>>(
        Operators(), [](bool a, bool b) { return static_cast<bool>(a & b); });
    std::vector<const IParameter*> parameters = method->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    std::shared_ptr<const IParameter> shared = Operators().MakeParameter(TypeCode::Boolean);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(parameters[0], shared.get());
    EXPECT_EQ(parameters[1], shared.get());
    EXPECT_EQ(&parameters[0]->Type(), TypeFor(TypeCode::Boolean));
    EXPECT_EQ(&parameters[1]->Type(), TypeFor(TypeCode::Boolean));
    EXPECT_EQ(&method->ReturnType(), TypeFor(TypeCode::Boolean));
    EXPECT_TRUE(method->CanEvaluateAtCompileTime());
}

// ---------------------------------------------------------------------------
// The logical tables (the single bool originals, not lifted within their own tables)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBitwiseTest, LogicalAndOperatorsTableHasTheSingleBoolOriginal) {
    // The C# `new OperatorMethod[] { new LambdaBinaryOperatorMethod<bool, bool>(this,
    // (a, b) => a & b) }` -- a plain one-entry array, NOT wrapped in Lift: the logical
    // table holds the bool original alone (its lifted `Nullable<bool>` form appears only
    // in BitwiseAndOperators).
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().LogicalAndOperators();
    ASSERT_EQ(table.size(), 1u);
    ExpectBitwiseOriginalAt(table, 0, TypeCode::Boolean);
    // The single entry is NOT a lifted form.
    EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                  static_cast<const OperatorMethod*>(table[0].get())),
              nullptr);
}

TEST(CSharpOperatorsBitwiseTest, LogicalOrOperatorsTableHasTheSingleBoolOriginal) {
    // The same single-entry shape for `|`.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().LogicalOrOperators();
    ASSERT_EQ(table.size(), 1u);
    ExpectBitwiseOriginalAt(table, 0, TypeCode::Boolean);
    EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                  static_cast<const OperatorMethod*>(table[0].get())),
              nullptr);
}

// ---------------------------------------------------------------------------
// The bitwise tables (originals, then their lifted forms)
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBitwiseTest, BitwiseAndOperatorsTableHasOriginalsThenLifts) {
    // The C# 4.0 spec 7.11 bitwise `&`: the four integer originals, then the shared
    // bool original, then their five lifted `Nullable<T>` forms -- 10 entries.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseAndOperators();
    ASSERT_EQ(table.size(), 10u);
    ExpectBitwiseOriginalAt(table, 0, TypeCode::Int32);
    ExpectBitwiseOriginalAt(table, 1, TypeCode::UInt32);
    ExpectBitwiseOriginalAt(table, 2, TypeCode::Int64);
    ExpectBitwiseOriginalAt(table, 3, TypeCode::UInt64);
    ExpectBitwiseOriginalAt(table, 4, TypeCode::Boolean);
    ExpectBitwiseLiftedAt(table, 5, TypeCode::Int32);
    ExpectBitwiseLiftedAt(table, 6, TypeCode::UInt32);
    ExpectBitwiseLiftedAt(table, 7, TypeCode::Int64);
    ExpectBitwiseLiftedAt(table, 8, TypeCode::UInt64);
    ExpectBitwiseLiftedAt(table, 9, TypeCode::Boolean);
}

TEST(CSharpOperatorsBitwiseTest, BitwiseAndSharesTheLogicalAndInstance) {
    // THE CRUX: the C# `this.LogicalAndOperators[0]` -- the bitwise table's bool
    // original is the SAME instance the logical table holds (pointer identity).
    const std::vector<std::shared_ptr<OperatorMethod>>& bitwise =
        Operators().BitwiseAndOperators();
    ASSERT_EQ(bitwise.size(), 10u);
    const std::vector<std::shared_ptr<OperatorMethod>>& logical =
        Operators().LogicalAndOperators();
    ASSERT_EQ(logical.size(), 1u);
    EXPECT_EQ(bitwise[4].get(), logical[0].get());
}

TEST(CSharpOperatorsBitwiseTest, BitwiseOrOperatorsTableHasOriginalsThenLifts) {
    // The C# 4.0 spec 7.11 bitwise `|`: the four integer originals, then the shared
    // bool original, then their five lifted forms -- 10 entries.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseOrOperators();
    ASSERT_EQ(table.size(), 10u);
    ExpectBitwiseOriginalAt(table, 0, TypeCode::Int32);
    ExpectBitwiseOriginalAt(table, 3, TypeCode::UInt64);
    ExpectBitwiseOriginalAt(table, 4, TypeCode::Boolean);
    ExpectBitwiseLiftedAt(table, 5, TypeCode::Int32);
    ExpectBitwiseLiftedAt(table, 8, TypeCode::UInt64);
    ExpectBitwiseLiftedAt(table, 9, TypeCode::Boolean);
}

TEST(CSharpOperatorsBitwiseTest, BitwiseOrSharesTheLogicalOrInstance) {
    // THE CRUX: the C# `this.LogicalOrOperators[0]` -- the `|` bitwise table shares the
    // logical table's instance too.
    const std::vector<std::shared_ptr<OperatorMethod>>& bitwise =
        Operators().BitwiseOrOperators();
    ASSERT_EQ(bitwise.size(), 10u);
    const std::vector<std::shared_ptr<OperatorMethod>>& logical =
        Operators().LogicalOrOperators();
    ASSERT_EQ(logical.size(), 1u);
    EXPECT_EQ(bitwise[4].get(), logical[0].get());
}

TEST(CSharpOperatorsBitwiseTest, BitwiseXorOperatorsTableHasOriginalsThenLifts) {
    // The C# 4.0 spec 7.11 bitwise `^`: the four integer originals, then a FRESH bool
    // original, then their five lifted forms -- 10 entries.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseXorOperators();
    ASSERT_EQ(table.size(), 10u);
    ExpectBitwiseOriginalAt(table, 0, TypeCode::Int32);
    ExpectBitwiseOriginalAt(table, 1, TypeCode::UInt32);
    ExpectBitwiseOriginalAt(table, 2, TypeCode::Int64);
    ExpectBitwiseOriginalAt(table, 3, TypeCode::UInt64);
    ExpectBitwiseOriginalAt(table, 4, TypeCode::Boolean);
    ExpectBitwiseLiftedAt(table, 5, TypeCode::Int32);
    ExpectBitwiseLiftedAt(table, 6, TypeCode::UInt32);
    ExpectBitwiseLiftedAt(table, 7, TypeCode::Int64);
    ExpectBitwiseLiftedAt(table, 8, TypeCode::UInt64);
    ExpectBitwiseLiftedAt(table, 9, TypeCode::Boolean);
}

TEST(CSharpOperatorsBitwiseTest, BitwiseXorBoolEntryIsFreshNotSharedWithTheLogicalTables) {
    // THE CRUX (the xor direction): there is no logical-xor table, so the `^` bool
    // original is a FRESH instance -- pointer-DISTINCT from both logical tables'
    // entries (and from the bitwise `&`/`|` shared bool originals).
    const std::vector<std::shared_ptr<OperatorMethod>>& xorTable =
        Operators().BitwiseXorOperators();
    ASSERT_EQ(xorTable.size(), 10u);
    const std::vector<std::shared_ptr<OperatorMethod>>& andTable =
        Operators().BitwiseAndOperators();
    ASSERT_EQ(andTable.size(), 10u);
    const std::vector<std::shared_ptr<OperatorMethod>>& orTable =
        Operators().BitwiseOrOperators();
    ASSERT_EQ(orTable.size(), 10u);
    EXPECT_NE(xorTable[4].get(), andTable[4].get());
    EXPECT_NE(xorTable[4].get(), orTable[4].get());
    // ... while the `&` and `|` bool originals are also distinct from each other (the
    // two logical tables hold separate `&`/`|` instances).
    EXPECT_NE(andTable[4].get(), orTable[4].get());
    // The four integer originals are freshly built per table too (each LazyInit GetOrSet
    // constructs its own LambdaBinaryOperatorMethod instances).
    EXPECT_NE(xorTable[0].get(), andTable[0].get());
    EXPECT_NE(andTable[0].get(), orTable[0].get());
}

TEST(CSharpOperatorsBitwiseTest, BitwiseLiftedFormsLiftTheReturnType) {
    // THE CONTRAST CRUX: the bitwise lifted forms use the ARITHMETIC-style lift -- the
    // return type IS `Nullable<T>` (unlike the relational `Lift` that resets the lifted
    // return type to the plain Boolean; the `RelationalOperatorMethod` reset is confined
    // to the comparison operators). The lifted bool entry carries `Nullable<bool>` --
    // the C# note: the lifted bool? bitwise logic ("true | null" = null) is wrong but
    // irrelevant because bool? cannot be a compile-time type; the divergence lives in
    // the Invoke bodies, not the type shape.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseAndOperators();
    ASSERT_EQ(table.size(), 10u);
    // The lifted int form: Nullable<int>, NOT the plain Int32.
    EXPECT_TRUE(IsNullable(table[5]->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(table[5]->ReturnType()), TypeFor(TypeCode::Int32));
    // The lifted bool form: Nullable<bool>, NOT the plain Boolean.
    EXPECT_TRUE(IsNullable(table[9]->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(table[9]->ReturnType()), TypeFor(TypeCode::Boolean));
}

// ---------------------------------------------------------------------------
// The memoization and the cross-cast split
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsBitwiseTest, BitwiseOperatorTablesMemoizeTheBuiltLists) {
    // The C# LazyInit memoization (convention (m)): a repeat call returns the SAME list
    // (the reference identity) with the same method instances.
    const std::vector<std::shared_ptr<OperatorMethod>>& first =
        Operators().BitwiseAndOperators();
    std::vector<std::shared_ptr<OperatorMethod>> snapshot(first);
    const std::vector<std::shared_ptr<OperatorMethod>>& second =
        Operators().BitwiseAndOperators();
    EXPECT_EQ(&first, &second);
    ASSERT_EQ(snapshot.size(), second.size());
    for (std::size_t i = 0; i < snapshot.size(); i++) {
        EXPECT_EQ(snapshot[i].get(), second[i].get()) << "index " << i;
    }
    // The five tables are independent memos (each builds its own list), while the `&`
    // and `|` bitwise tables still share their bool entries with the logical tables.
    const std::vector<std::shared_ptr<OperatorMethod>>& logicalOr =
        Operators().LogicalOrOperators();
    ASSERT_EQ(logicalOr.size(), 1u);
    const std::vector<std::shared_ptr<OperatorMethod>>& bitwiseOr =
        Operators().BitwiseOrOperators();
    ASSERT_EQ(bitwiseOr.size(), 10u);
    EXPECT_EQ(bitwiseOr[4].get(), logicalOr[0].get());
    const std::vector<std::shared_ptr<OperatorMethod>>& xorTable =
        Operators().BitwiseXorOperators();
    ASSERT_EQ(xorTable.size(), 10u);
    EXPECT_NE(first[0].get(), xorTable[0].get());
    // The shared-entry memoization is itself memoized: the logical `&` table was built
    // as a side effect of the bitwise `&` table's first access (the C#
    // `this.LogicalAndOperators[0]` in the GetOrSet factory), so a direct logical access
    // returns that SAME instance.
    const std::vector<std::shared_ptr<OperatorMethod>>& logicalAnd =
        Operators().LogicalAndOperators();
    ASSERT_EQ(logicalAnd.size(), 1u);
    EXPECT_EQ(first[4].get(), logicalAnd[0].get());
}

TEST(CSharpOperatorsBitwiseTest, BitwiseTableLiftedFormsCrossCastAndOriginalsDoNot) {
    // The BetterFunctionMember non-lifted-operator tiebreak shape (the D549 cross-cast
    // from the OperatorMethod base): only the lifted half implements ILiftedOperator.
    const std::vector<std::shared_ptr<OperatorMethod>>& table =
        Operators().BitwiseXorOperators();
    ASSERT_EQ(table.size(), 10u);
    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_EQ(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
    for (std::size_t i = 5; i < 10; i++) {
        EXPECT_NE(dynamic_cast<const ILiftedOperator*>(
                      static_cast<const OperatorMethod*>(table[i].get())),
                  nullptr)
            << "index " << i;
    }
}

} // namespace
