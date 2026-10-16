// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Tests for the CSharpOperators `Invoke(CSharpResolver, ...)` constant-evaluation
// virtuals (CSharpOperators.cs lines 242/269 + 415/448 + 604 + 720/771 + 882) -- the
// region deferred on the CSharpResolver long pole since the operator-class slices
// (convention (j)), now landed with the resolver parameter type and its
// `CSharpPrimitiveCast` wrapper: the throwing NotSupportedException bases, the
// lambda-backed unary/binary null passthrough + operand casts + stored-func application,
// the checked/unchecked func selection by the resolver's CheckForOverflow, the
// StringConcatenation string.Concat rendering, the EqualityOperatorMethod null folds +
// Single/Double-vs-object.Equals comparison, the LiftedEqualityOperatorMethod
// delegation, and the RelationalOperatorMethod comparison application.
//
// The registered-compilation stub is the CSharpOperatorsUnary_Test.cpp precedent: every
// `KnownTypeCode` the parameter and operator tables resolve must be registered with a
// shared-managed `LookupTypeDefinition` (the ctors recover owning handles through
// `shared_from_this()`, the D529 convention). The resolver is the CSharpResolverSkeleton
// construction (make_shared over the compilation; the checked clone goes through
// WithCheckForOverflow).

#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::BinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::Decimal;
using ILSpy::Decompiler::CSharp::Resolver::EqualityOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LambdaBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LambdaUnaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedBinaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedEqualityOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::LiftedUnaryOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::RelationalOperatorMethod;
using ILSpy::Decompiler::CSharp::Resolver::StringConcatenation;
using ILSpy::Decompiler::CSharp::Resolver::UnaryOperatorMethod;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the parameter and operator tables
// resolve registered as a shared-managed `LookupTypeDefinition` (the CSharpOperatorsUnary
// precedent; `types[i]` is the registration for `KnownTypeCode(1 + i)` = Object..String,
// and `nullableOfT` is the `System.Nullable`1` generic definition the lifted forms build
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

// The default resolver (CheckForOverflow false) over the shared registered compilation --
// the CSharpResolverSkeleton construction (make_shared: the With* clones go through
// enable_shared_from_this).
std::shared_ptr<CSharpResolver> TheResolver() {
    static auto resolver = std::make_shared<CSharpResolver>(Compilation());
    return resolver;
}

// The checked clone (CheckForOverflow true).
std::shared_ptr<CSharpResolver> TheCheckedResolver() {
    static auto resolver = TheResolver()->WithCheckForOverflow(true);
    return resolver;
}

// The typed view of a table entry (the tables hold `shared_ptr<OperatorMethod>`; the
// typed classes are what the Invoke overrides live on). A failed cast is a
// test-configuration error, not an assertion about the code under test; the escaping
// exception is reported as the test failure (an ASSERT cannot appear in a non-void
// helper).
template <typename T>
const T* As(const std::shared_ptr<OperatorMethod>& method)
{
    const T* typed = dynamic_cast<const T*>(method.get());
    if (typed == nullptr)
        throw std::runtime_error("the table entry does not have the expected type");
    return typed;
}

} // namespace

// ---------------------------------------------------------------------------
// The throwing NotSupportedException bases + the lifted non-overrides
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, UnaryOperatorMethodBaseInvokeThrowsNotSupportedException) {
    const UnaryOperatorMethod method(Compilation());
    EXPECT_THROW(
        static_cast<void>(method.Invoke(*TheResolver(), std::any(std::int32_t(5)))),
        std::logic_error);
}

TEST(CSharpOperatorsInvokeTest, BinaryOperatorMethodBaseInvokeThrowsNotSupportedException) {
    const BinaryOperatorMethod method(Compilation());
    EXPECT_THROW(
        static_cast<void>(method.Invoke(*TheResolver(), std::any(std::int32_t(1)),
                                         std::any(std::int32_t(2)))),
        std::logic_error);
}

TEST(CSharpOperatorsInvokeTest, LiftedUnaryOperatorMethodInheritsTheThrowingInvoke) {
    // The C# LiftedUnaryOperatorMethod does NOT override Invoke -- the resolver never
    // invokes a lifted operator directly (it lifts null operands itself), so the
    // inherited throwing base is the faithful surface.
    const LiftedUnaryOperatorMethod lifted(
        Operators(), *As<LambdaUnaryOperatorMethod<std::int32_t>>(
                         Operators().UncheckedUnaryMinusOperators()[0]));
    EXPECT_THROW(
        static_cast<void>(lifted.Invoke(*TheResolver(), std::any(std::int32_t(5)))),
        std::logic_error);
}

TEST(CSharpOperatorsInvokeTest, LiftedBinaryOperatorMethodInheritsTheThrowingInvoke) {
    const LiftedBinaryOperatorMethod lifted(
        Operators(), *As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                         Operators().MultiplicationOperators()[0]));
    EXPECT_THROW(
        static_cast<void>(lifted.Invoke(*TheResolver(), std::any(std::int32_t(2)),
                                        std::any(std::int32_t(3)))),
        std::logic_error);
}

// ---------------------------------------------------------------------------
// The lambda-backed unary operators
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, LambdaUnaryInvokeAppliesTheStoredFunc) {
    const auto* minus =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().UncheckedUnaryMinusOperators()[0]);
    const std::any result = minus->Invoke(*TheResolver(), std::any(std::int32_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(result), -5);
}

TEST(CSharpOperatorsInvokeTest, LambdaUnaryInvokePassesNullThrough) {
    const auto* minus =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().UncheckedUnaryMinusOperators()[0]);
    EXPECT_FALSE(minus->Invoke(*TheResolver(), std::any{}).has_value());
}

TEST(CSharpOperatorsInvokeTest, LambdaUnaryInvokeCastsTheOperandThroughCSharpPrimitiveCast) {
    // The operand is an int16 constant; the int32 operator's Invoke must cast it to the
    // operator's own TypeCode before the func (a skipped cast would leave the int16 in
    // the any and fail the unbox).
    const auto* minus =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().UncheckedUnaryMinusOperators()[0]);
    const std::any result = minus->Invoke(*TheResolver(), std::any(std::int16_t(1000)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(result), -1000);
}

TEST(CSharpOperatorsInvokeTest, LambdaUnaryInvokeCheckedTableThrowsAtTheInt32Boundary) {
    // The unary checked/unchecked distinction is at TABLE level (two separate tables,
    // each with fixed bodies) -- the checked int32 negation throws at INT32_MIN
    // regardless of the resolver's flag.
    const auto* checkedMinus =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().CheckedUnaryMinusOperators()[0]);
    EXPECT_THROW(
        static_cast<void>(checkedMinus->Invoke(
            *TheResolver(), std::any(std::numeric_limits<std::int32_t>::min()))),
        std::runtime_error);
}

TEST(CSharpOperatorsInvokeTest, LambdaUnaryInvokeThreadsTheResolverCheckForOverflowIntoTheCast) {
    // The operand cast itself goes through the resolver's CSharpPrimitiveCast wrapper,
    // which threads the resolver's CheckForOverflow: an int64 operand outside the
    // int32 range throws under the checked resolver and wraps unchecked.
    const auto* minus =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().UncheckedUnaryMinusOperators()[0]);
    const std::any overflow(std::int64_t(2147483648));
    EXPECT_THROW(static_cast<void>(minus->Invoke(*TheCheckedResolver(), overflow)),
                 std::runtime_error);
    const std::any result = minus->Invoke(*TheResolver(), overflow);
    ASSERT_TRUE(result.has_value());
    // The unchecked cast wraps to INT32_MIN, whose unchecked negation is itself.
    EXPECT_EQ(std::any_cast<std::int32_t>(result), std::numeric_limits<std::int32_t>::min());
}

// ---------------------------------------------------------------------------
// The lambda-backed binary operators
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, LambdaBinaryInvokeAppliesTheStoredFunc) {
    const auto* divide =
        As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(Operators().DivisionOperators()[0]);
    const std::any result =
        divide->Invoke(*TheResolver(), std::any(std::int32_t(7)), std::any(std::int32_t(2)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(result), 3);
}

TEST(CSharpOperatorsInvokeTest, LambdaBinaryInvokePassesNullThroughEitherOperand) {
    const auto* divide =
        As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(Operators().DivisionOperators()[0]);
    EXPECT_FALSE(divide->Invoke(*TheResolver(), std::any{}, std::any(std::int32_t(2))).has_value());
    EXPECT_FALSE(divide->Invoke(*TheResolver(), std::any(std::int32_t(7)), std::any{}).has_value());
}

TEST(CSharpOperatorsInvokeTest, LambdaBinaryInvokeSelectsTheCheckedFuncWhenTheResolverChecksOverflow) {
    const auto* multiply =
        As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(Operators().MultiplicationOperators()[0]);
    EXPECT_THROW(
        static_cast<void>(multiply->Invoke(*TheCheckedResolver(), std::any(std::int32_t(100000)),
                                            std::any(std::int32_t(100000)))),
        std::runtime_error);
}

TEST(CSharpOperatorsInvokeTest, LambdaBinaryInvokeSelectsTheUncheckedFuncWhenTheResolverDoesNotCheck) {
    const auto* multiply =
        As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(Operators().MultiplicationOperators()[0]);
    const std::any result = multiply->Invoke(*TheResolver(), std::any(std::int32_t(100000)),
                                             std::any(std::int32_t(100000)));
    ASSERT_TRUE(result.has_value());
    // 100000 * 100000 wraps: 10^10 mod 2^32.
    EXPECT_EQ(std::any_cast<std::int32_t>(result), 1410065408);
}

TEST(CSharpOperatorsInvokeTest, LambdaBinaryInvokeCastsOperandsThroughCSharpPrimitiveCast) {
    const auto* add =
        As<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(Operators().AdditionOperators()[0]);
    const std::any result = add->Invoke(*TheResolver(), std::any(std::int16_t(3)),
                                        std::any(std::int16_t(4)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(result), 7);
}

// ---------------------------------------------------------------------------
// StringConcatenation
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, StringConcatenationInvokeConcatsTwoStrings) {
    const auto* concat = As<StringConcatenation>(Operators().AdditionOperators()[7]);
    const std::any result = concat->Invoke(*TheResolver(), std::any(std::string("Hello, ")),
                                            std::any(std::string("world")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::string>(result), "Hello, world");
}

TEST(CSharpOperatorsInvokeTest, StringConcatenationInvokeRendersNullAsTheEmptyString) {
    const auto* concat = As<StringConcatenation>(Operators().AdditionOperators()[7]);
    const std::any oneNull =
        concat->Invoke(*TheResolver(), std::any(std::string("x")), std::any{});
    ASSERT_TRUE(oneNull.has_value());
    EXPECT_EQ(std::any_cast<std::string>(oneNull), "x");
    const std::any bothNull = concat->Invoke(*TheResolver(), std::any{}, std::any{});
    ASSERT_TRUE(bothNull.has_value());
    EXPECT_EQ(std::any_cast<std::string>(bothNull), "");
}

TEST(CSharpOperatorsInvokeTest, StringConcatenationInvokeRendersTheObjectOperand) {
    // The (String, Object) form renders the boxed operand through the object.ToString
    // stand-in (the C# string.Concat(object, object) behavior; the form is not
    // constant-evaluable, so the resolver never invokes it -- the method itself still
    // concatenates anything).
    const auto* concat = As<StringConcatenation>(Operators().AdditionOperators()[8]);
    const std::any result = concat->Invoke(*TheResolver(), std::any(std::string("n=")),
                                           std::any(std::int32_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::string>(result), "n=5");
}

// ---------------------------------------------------------------------------
// EqualityOperatorMethod + LiftedEqualityOperatorMethod
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, EqualityInvokeBothNullReturnsTheNegateFold) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[0]);
    const std::any result = equality->Invoke(*TheResolver(), std::any{}, std::any{});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);  // == : true
    const auto* inequality = As<EqualityOperatorMethod>(Operators().ValueInequalityOperators()[0]);
    const std::any negated = inequality->Invoke(*TheResolver(), std::any{}, std::any{});
    ASSERT_TRUE(negated.has_value());
    EXPECT_EQ(std::any_cast<bool>(negated), false);  // != : false
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeOneNullReturnsNegate) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[0]);
    const std::any result =
        equality->Invoke(*TheResolver(), std::any(std::int32_t(5)), std::any{});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);  // == : false
    const auto* inequality = As<EqualityOperatorMethod>(Operators().ValueInequalityOperators()[0]);
    const std::any negated =
        inequality->Invoke(*TheResolver(), std::any(std::int32_t(5)), std::any{});
    ASSERT_TRUE(negated.has_value());
    EXPECT_EQ(std::any_cast<bool>(negated), true);  // != : true
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeComparesValues) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[0]);
    std::any result = equality->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                                      std::any(std::int32_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = equality->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                              std::any(std::int32_t(6)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
    const auto* inequality = As<EqualityOperatorMethod>(Operators().ValueInequalityOperators()[0]);
    result = inequality->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                                std::any(std::int32_t(6)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeCastsOperandsThroughCSharpPrimitiveCast) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[0]);
    const std::any result = equality->Invoke(*TheResolver(), std::any(std::int16_t(3)),
                                             std::any(std::int16_t(3)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeSingleUsesTheFloatComparison) {
    // The Single arm compares with the raw float `==` (NaN != NaN) -- NOT object.Equals
    // (which would report NaN equal to NaN bitwise). The arm is load-bearing.
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[4]);
    const std::any nan = std::any(std::numeric_limits<float>::quiet_NaN());
    std::any result = equality->Invoke(*TheResolver(), nan, nan);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
    result = equality->Invoke(*TheResolver(), std::any(1.5f), std::any(1.5f));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeDoubleUsesTheDoubleComparison) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[5]);
    const std::any nan = std::any(std::numeric_limits<double>::quiet_NaN());
    const std::any result = equality->Invoke(*TheResolver(), nan, nan);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeStringFormComparesByValue) {
    // The reference-equality tables carry a String original (Type != Object, so the
    // form IS constant-evaluable); the object.Equals arm compares the string values.
    const auto* equality = As<EqualityOperatorMethod>(Operators().ReferenceEqualityOperators()[1]);
    std::any result = equality->Invoke(*TheResolver(), std::any(std::string("a")),
                                       std::any(std::string("a")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = equality->Invoke(*TheResolver(), std::any(std::string("a")),
                              std::any(std::string("b")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
    const auto* inequality =
        As<EqualityOperatorMethod>(Operators().ReferenceInequalityOperators()[1]);
    result = inequality->Invoke(*TheResolver(), std::any(std::string("a")),
                                std::any(std::string("b")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeDecimalComparesNumerically) {
    // The object.Equals arm routes the Decimal stand-in through the scale-aligned
    // CompareDecimal (the System.Decimal value equality -- the scale is the number of
    // digits after the point, so 5 and 5.0 compare equal).
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[6]);
    std::any result = equality->Invoke(*TheResolver(), std::any(Decimal{5, 0, false}),
                                       std::any(Decimal{50, 1, false}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = equality->Invoke(*TheResolver(), std::any(Decimal{5, 0, false}),
                              std::any(Decimal{6, 0, false}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
}

TEST(CSharpOperatorsInvokeTest, EqualityInvokeBooleanComparesByValue) {
    const auto* equality = As<EqualityOperatorMethod>(Operators().ValueEqualityOperators()[7]);
    std::any result =
        equality->Invoke(*TheResolver(), std::any(true), std::any(true));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = equality->Invoke(*TheResolver(), std::any(true), std::any(false));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
}

TEST(CSharpOperatorsInvokeTest, LiftedEqualityInvokeDelegatesToTheBaseMethod) {
    const auto* lifted = As<LiftedEqualityOperatorMethod>(Operators().ValueEqualityOperators()[8]);
    std::any result = lifted->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                                     std::any(std::int32_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = lifted->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                            std::any(std::int32_t(6)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
    // The delegation carries the null folds too (the base's both-null `!Negate`).
    result = lifted->Invoke(*TheResolver(), std::any{}, std::any{});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

// ---------------------------------------------------------------------------
// RelationalOperatorMethod
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, RelationalInvokeAppliesTheStoredFunc) {
    const auto* lessThan =
        As<RelationalOperatorMethod<std::int32_t, std::int32_t>>(Operators().LessThanOperators()[0]);
    std::any result = lessThan->Invoke(*TheResolver(), std::any(std::int32_t(3)),
                                       std::any(std::int32_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
    result = lessThan->Invoke(*TheResolver(), std::any(std::int32_t(5)),
                              std::any(std::int32_t(3)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), false);
    const auto* atLeast =
        As<RelationalOperatorMethod<std::int32_t, std::int32_t>>(
            Operators().GreaterThanOrEqualOperators()[0]);
    result = atLeast->Invoke(*TheResolver(), std::any(std::int32_t(3)),
                             std::any(std::int32_t(3)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

TEST(CSharpOperatorsInvokeTest, RelationalInvokePassesNullThrough) {
    const auto* lessThan =
        As<RelationalOperatorMethod<std::int32_t, std::int32_t>>(Operators().LessThanOperators()[0]);
    EXPECT_FALSE(
        lessThan->Invoke(*TheResolver(), std::any{}, std::any(std::int32_t(3))).has_value());
    EXPECT_FALSE(
        lessThan->Invoke(*TheResolver(), std::any(std::int32_t(3)), std::any{}).has_value());
}

TEST(CSharpOperatorsInvokeTest, RelationalInvokeCastsOperandsThroughCSharpPrimitiveCast) {
    const auto* lessThan =
        As<RelationalOperatorMethod<std::int32_t, std::int32_t>>(Operators().LessThanOperators()[0]);
    const std::any result = lessThan->Invoke(*TheResolver(), std::any(std::int16_t(3)),
                                             std::any(std::int16_t(5)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<bool>(result), true);
}

// ---------------------------------------------------------------------------
// The virtual dispatch
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsInvokeTest, InvokeDispatchesThroughTheOperatorMethodBasePointer) {
    const UnaryOperatorMethod* unaryBase =
        As<LambdaUnaryOperatorMethod<std::int32_t>>(Operators().UncheckedUnaryMinusOperators()[0]);
    const std::any unaryResult = unaryBase->Invoke(*TheResolver(), std::any(std::int32_t(8)));
    ASSERT_TRUE(unaryResult.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(unaryResult), -8);

    const BinaryOperatorMethod* binaryBase = As<EqualityOperatorMethod>(
        Operators().ValueEqualityOperators()[7]);
    const std::any binaryResult =
        binaryBase->Invoke(*TheResolver(), std::any(true), std::any(false));
    ASSERT_TRUE(binaryResult.has_value());
    EXPECT_EQ(std::any_cast<bool>(binaryResult), false);
}
