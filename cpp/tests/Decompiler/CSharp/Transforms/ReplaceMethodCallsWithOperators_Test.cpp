// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the resolver-free statics of `ReplaceMethodCallsWithOperators` -- the
// operator-name mapping tables the instance `ProcessInvocationExpression` consumes, plus
// `IsInstantiableTypeParameter`, and the `SyntaxExtensions.UnwrapInDirectionExpression`
// helper they share. These are pure functions (name strings and a settings flag in, an
// operator enum and a checked flag out), so the tests pin the complete mapping tables and
// the settings gates without any AST resolver.

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

namespace {

using BinaryOperatorType = Syntax::BinaryOperatorType;
using UnaryOperatorType = Syntax::UnaryOperatorType;

// Asserts one binary-operator name maps to `expected` and leaves `isChecked` false.
void ExpectBinary(const std::string& name, BinaryOperatorType expected,
                  const DecompilerSettings& settings) {
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                  name, isChecked, settings),
              std::optional<BinaryOperatorType>(expected))
        << name;
    EXPECT_FALSE(isChecked) << name;
}

// Asserts one unary-operator name maps to `expected` and leaves `isChecked` false.
void ExpectUnary(const std::string& name, UnaryOperatorType expected,
                 const DecompilerSettings& settings) {
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                  name, isChecked, settings),
              std::optional<UnaryOperatorType>(expected))
        << name;
    EXPECT_FALSE(isChecked) << name;
}

} // namespace

// The full binary-operator name table (`settings.CheckedOperators` off, so the checked
// names are not in this table).
TEST(ReplaceMethodCallsWithOperatorsTest, MapsEveryBinaryOperator)
{
    DecompilerSettings settings;
    ExpectBinary("op_Addition", BinaryOperatorType::Add, settings);
    ExpectBinary("op_Subtraction", BinaryOperatorType::Subtract, settings);
    ExpectBinary("op_Multiply", BinaryOperatorType::Multiply, settings);
    ExpectBinary("op_Division", BinaryOperatorType::Divide, settings);
    ExpectBinary("op_Modulus", BinaryOperatorType::Modulus, settings);
    ExpectBinary("op_BitwiseAnd", BinaryOperatorType::BitwiseAnd, settings);
    ExpectBinary("op_BitwiseOr", BinaryOperatorType::BitwiseOr, settings);
    ExpectBinary("op_ExclusiveOr", BinaryOperatorType::ExclusiveOr, settings);
    ExpectBinary("op_LeftShift", BinaryOperatorType::ShiftLeft, settings);
    ExpectBinary("op_RightShift", BinaryOperatorType::ShiftRight, settings);
    ExpectBinary("op_Equality", BinaryOperatorType::Equality, settings);
    ExpectBinary("op_Inequality", BinaryOperatorType::InEquality, settings);
    ExpectBinary("op_LessThan", BinaryOperatorType::LessThan, settings);
    ExpectBinary("op_LessThanOrEqual", BinaryOperatorType::LessThanOrEqual, settings);
    ExpectBinary("op_GreaterThan", BinaryOperatorType::GreaterThan, settings);
    ExpectBinary("op_GreaterThanOrEqual", BinaryOperatorType::GreaterThanOrEqual, settings);
}

// The full unary-operator name table (`settings.CheckedOperators` off).
TEST(ReplaceMethodCallsWithOperatorsTest, MapsEveryUnaryOperator)
{
    DecompilerSettings settings;
    ExpectUnary("op_LogicalNot", UnaryOperatorType::Not, settings);
    ExpectUnary("op_OnesComplement", UnaryOperatorType::BitNot, settings);
    ExpectUnary("op_UnaryNegation", UnaryOperatorType::Minus, settings);
    ExpectUnary("op_UnaryPlus", UnaryOperatorType::Plus, settings);
    ExpectUnary("op_Increment", UnaryOperatorType::Increment, settings);
    ExpectUnary("op_Decrement", UnaryOperatorType::Decrement, settings);
}

// A name that is not an operator method maps to nullopt and leaves `isChecked` false.
TEST(ReplaceMethodCallsWithOperatorsTest, UnknownNamesMapToNothing)
{
    DecompilerSettings settings;
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "Concat", isChecked, settings)
                     .has_value());
    EXPECT_FALSE(isChecked);
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                     "op_True", isChecked, settings)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// The four checked binary operators are recognized only with `CheckedOperators` on, and
// report `isChecked`. With the setting off they fall to nullopt.
TEST(ReplaceMethodCallsWithOperatorsTest, CheckedBinaryOperatorsGatedBySetting)
{
    DecompilerSettings checked;
    checked.SetCheckedOperators(true);
    auto checkedAdd = [](const char* name, BinaryOperatorType expected, DecompilerSettings& s) {
        bool isChecked = false;
        EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::
                      GetBinaryOperatorTypeFromMetadataName(name, isChecked, s),
                  std::optional<BinaryOperatorType>(expected))
            << name;
        EXPECT_TRUE(isChecked) << name;
    };
    checkedAdd("op_CheckedAddition", BinaryOperatorType::Add, checked);
    checkedAdd("op_CheckedSubtraction", BinaryOperatorType::Subtract, checked);
    checkedAdd("op_CheckedMultiply", BinaryOperatorType::Multiply, checked);
    checkedAdd("op_CheckedDivision", BinaryOperatorType::Divide, checked);

    DecompilerSettings off;
    off.SetCheckedOperators(false);
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "op_CheckedAddition", isChecked, off)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// The checked unary operators group: the checked negation and the checked increment /
// decrement map only with `CheckedOperators` on.
TEST(ReplaceMethodCallsWithOperatorsTest, CheckedUnaryOperatorsGatedBySetting)
{
    DecompilerSettings checked;
    checked.SetCheckedOperators(true);
    auto checkedUnary = [](const char* name, UnaryOperatorType expected, DecompilerSettings& s) {
        bool isChecked = false;
        EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::
                      GetUnaryOperatorTypeFromMetadataName(name, isChecked, s),
                  std::optional<UnaryOperatorType>(expected))
            << name;
        EXPECT_TRUE(isChecked) << name;
    };
    checkedUnary("op_CheckedUnaryNegation", UnaryOperatorType::Minus, checked);
    checkedUnary("op_CheckedIncrement", UnaryOperatorType::Increment, checked);
    checkedUnary("op_CheckedDecrement", UnaryOperatorType::Decrement, checked);

    DecompilerSettings off;
    off.SetCheckedOperators(false);
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                     "op_CheckedIncrement", isChecked, off)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// `op_UnsignedRightShift` maps to `>>>` only with `UnsignedRightShift` on.
TEST(ReplaceMethodCallsWithOperatorsTest, UnsignedRightShiftGatedBySetting)
{
    DecompilerSettings on;
    on.SetUnsignedRightShift(true);
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                  "op_UnsignedRightShift", isChecked, on),
              std::optional<BinaryOperatorType>(BinaryOperatorType::UnsignedShiftRight));
    EXPECT_FALSE(isChecked);

    DecompilerSettings off;
    off.SetUnsignedRightShift(false);
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "op_UnsignedRightShift", isChecked, off)
                     .has_value());
}

// `IsInstantiableTypeParameter` accepts only a type parameter carrying the `new()`
// constraint; a plain type parameter without it and a non-type-parameter type both fail.
TEST(ReplaceMethodCallsWithOperatorsTest, IsInstantiableTypeParameter)
{
    auto withConstraint = std::make_shared<LookupTypeParameter>("T");
    withConstraint->SetHasDefaultConstructorConstraint(true);
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        *withConstraint));

    auto withoutConstraint = std::make_shared<LookupTypeParameter>("T");
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        *withoutConstraint));

    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        compilation.FindType(TS::KnownTypeCode::Int32)));
}

// The `in`-direction wrapper is unwrapped and its operand detached; `ref`/`out`
// wrappers and unwrapped expressions pass through unchanged.
TEST(SyntaxExtensionsTest, UnwrapInDirectionExpression)
{
    auto* inner = new Syntax::IdentifierExpression("x");
    auto* inDir = new Syntax::DirectionExpression(Syntax::FieldDirection::In, inner);
    Syntax::Expression* unwrapped = Syntax::UnwrapInDirectionExpression(inDir);
    EXPECT_EQ(unwrapped, inner);
    EXPECT_EQ(inner->Parent(), nullptr);
    EXPECT_EQ(inDir->Expression(), nullptr);

    auto* refDir = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Ref, new Syntax::IdentifierExpression("y"));
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(refDir), refDir);
    EXPECT_NE(refDir->Expression(), nullptr);

    auto* outDir = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Out, new Syntax::IdentifierExpression("z"));
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(outDir), outDir);

    auto* plain = new Syntax::IdentifierExpression("w");
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(plain), plain);
}
