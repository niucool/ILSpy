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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. HOWEVER CAUSED AND ON WHICHEVER THEORY OF LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ExpressionType` (cpp/Decompiler/TypeSystem/ExpressionType.hpp), the
// port of the BCL `System.Linq.Expressions.ExpressionType` enum -- the node-kind
// classification the expression-tree API uses for every expression node. It is the
// type `OperatorResolveResult.OperatorType` returns (the resolved operator kind of
// a unary / binary / ternary operator invocation). The tests pin the enum's int
// backing, its 85 values in .NET declaration order (`Add = 0` through `IsFalse =
// 84`, with no gaps), and the equality-comparison / switch patterns the
// `ExpressionBuilder` consumer uses.

#include "Decompiler/TypeSystem/ExpressionType.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// The enum is `int`-backed with 85 members in .NET declaration order. A
// representative spread of values is pinned explicitly (the section boundaries
// and the gap-prone transitions such as `Negate`/`UnaryPlus`/`NegateChecked` at
// 28/29/30 and the `SubtractChecked`/`TypeAs`/`TypeIs`/`Assign` run at 43/44/45/46
// and the compound-assignment block starting at `AddAssign = 63`): the values
// are stored by value in `OperatorResolveResult` and compared to the named
// literals, so reordering would silently remap every operator classification.
// ---------------------------------------------------------------------------
TEST(ExpressionTypeTest, EnumValuesMatchCSharpDeclarationOrder)
{
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Add), 0);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::AddChecked), 1);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::And), 2);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::AndAlso), 3);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Coalesce), 7);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Conditional), 8);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Convert), 10);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Equal), 13);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::GreaterThan), 15);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::LeftShift), 19);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::MemberAccess), 23);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Modulo), 25);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Negate), 28);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::UnaryPlus), 29);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::NegateChecked), 30);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::New), 31);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Not), 34);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::OrElse), 37);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Power), 39);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Quote), 40);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Subtract), 42);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::SubtractChecked), 43);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::TypeAs), 44);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::TypeIs), 45);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Assign), 46);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Block), 47);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Default), 51);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Extension), 52);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Increment), 54);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Throw), 60);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Try), 61);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::Unbox), 62);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::AddAssign), 63);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::AndAssign), 64);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::SubtractAssign), 73);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::AddAssignChecked), 74);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::SubtractAssignChecked), 76);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::PreIncrementAssign), 77);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::PreDecrementAssign), 78);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::PostIncrementAssign), 79);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::PostDecrementAssign), 80);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::TypeEqual), 81);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::OnesComplement), 82);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::IsTrue), 83);
    EXPECT_EQ(static_cast<std::int32_t>(TS::ExpressionType::IsFalse), 84);
}

// ---------------------------------------------------------------------------
// The BCL `int` backing ports to std::int32_t. The enum is exactly four bytes
// (the C# default `int` enum backing), and a value-initialized ExpressionType is
// the zero value `Add` (the C# default(int) for an uninitialized ExpressionType).
// ---------------------------------------------------------------------------
TEST(ExpressionTypeTest, IsInt32Backed)
{
    static_assert(sizeof(TS::ExpressionType) == 4,
                  "ExpressionType must be int32-backed");
    static_assert(static_cast<std::int32_t>(TS::ExpressionType{}) == 0,
                  "value-initialized ExpressionType must be Add (the zero)");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// All 85 members are present, distinct, and contiguous: every enumerator in
// .NET declaration order maps to its index (the declaration has NO gaps from
// `Add = 0` to `IsFalse = 84`). This single check proves the port did not drop,
// duplicate, or reorder any member relative to the BCL enum.
// ---------------------------------------------------------------------------
TEST(ExpressionTypeTest, AllValuesAreSequentialAndContiguous)
{
    const std::vector<TS::ExpressionType> all = {
        TS::ExpressionType::Add,
        TS::ExpressionType::AddChecked,
        TS::ExpressionType::And,
        TS::ExpressionType::AndAlso,
        TS::ExpressionType::ArrayLength,
        TS::ExpressionType::ArrayIndex,
        TS::ExpressionType::Call,
        TS::ExpressionType::Coalesce,
        TS::ExpressionType::Conditional,
        TS::ExpressionType::Constant,
        TS::ExpressionType::Convert,
        TS::ExpressionType::ConvertChecked,
        TS::ExpressionType::Divide,
        TS::ExpressionType::Equal,
        TS::ExpressionType::ExclusiveOr,
        TS::ExpressionType::GreaterThan,
        TS::ExpressionType::GreaterThanOrEqual,
        TS::ExpressionType::Invoke,
        TS::ExpressionType::Lambda,
        TS::ExpressionType::LeftShift,
        TS::ExpressionType::LessThan,
        TS::ExpressionType::LessThanOrEqual,
        TS::ExpressionType::ListInit,
        TS::ExpressionType::MemberAccess,
        TS::ExpressionType::MemberInit,
        TS::ExpressionType::Modulo,
        TS::ExpressionType::Multiply,
        TS::ExpressionType::MultiplyChecked,
        TS::ExpressionType::Negate,
        TS::ExpressionType::UnaryPlus,
        TS::ExpressionType::NegateChecked,
        TS::ExpressionType::New,
        TS::ExpressionType::NewArrayInit,
        TS::ExpressionType::NewArrayBounds,
        TS::ExpressionType::Not,
        TS::ExpressionType::NotEqual,
        TS::ExpressionType::Or,
        TS::ExpressionType::OrElse,
        TS::ExpressionType::Parameter,
        TS::ExpressionType::Power,
        TS::ExpressionType::Quote,
        TS::ExpressionType::RightShift,
        TS::ExpressionType::Subtract,
        TS::ExpressionType::SubtractChecked,
        TS::ExpressionType::TypeAs,
        TS::ExpressionType::TypeIs,
        TS::ExpressionType::Assign,
        TS::ExpressionType::Block,
        TS::ExpressionType::DebugInfo,
        TS::ExpressionType::Decrement,
        TS::ExpressionType::Dynamic,
        TS::ExpressionType::Default,
        TS::ExpressionType::Extension,
        TS::ExpressionType::Goto,
        TS::ExpressionType::Increment,
        TS::ExpressionType::Index,
        TS::ExpressionType::Label,
        TS::ExpressionType::RuntimeVariables,
        TS::ExpressionType::Loop,
        TS::ExpressionType::Switch,
        TS::ExpressionType::Throw,
        TS::ExpressionType::Try,
        TS::ExpressionType::Unbox,
        TS::ExpressionType::AddAssign,
        TS::ExpressionType::AndAssign,
        TS::ExpressionType::DivideAssign,
        TS::ExpressionType::ExclusiveOrAssign,
        TS::ExpressionType::LeftShiftAssign,
        TS::ExpressionType::ModuloAssign,
        TS::ExpressionType::MultiplyAssign,
        TS::ExpressionType::OrAssign,
        TS::ExpressionType::PowerAssign,
        TS::ExpressionType::RightShiftAssign,
        TS::ExpressionType::SubtractAssign,
        TS::ExpressionType::AddAssignChecked,
        TS::ExpressionType::MultiplyAssignChecked,
        TS::ExpressionType::SubtractAssignChecked,
        TS::ExpressionType::PreIncrementAssign,
        TS::ExpressionType::PreDecrementAssign,
        TS::ExpressionType::PostIncrementAssign,
        TS::ExpressionType::PostDecrementAssign,
        TS::ExpressionType::TypeEqual,
        TS::ExpressionType::OnesComplement,
        TS::ExpressionType::IsTrue,
        TS::ExpressionType::IsFalse,
    };
    ASSERT_EQ(all.size(), 85u);
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::int32_t>(all[i]), static_cast<std::int32_t>(i))
            << "enumerator at index " << i << " does not map to its declaration index";
    }
}

// ---------------------------------------------------------------------------
// The `ExpressionBuilder` consumer switches on `OperatorResolveResult.OperatorType`
// (e.g. `case ExpressionType.Add: ... case ExpressionType.Subtract: ...`). The
// 85 values are distinct so every equality comparison resolves to a single arm;
// the built-in `==` / `!=` of the C++ enum class is the only operator the
// consumers need (the enum is ordinal, NOT [Flags]).
// ---------------------------------------------------------------------------
TEST(ExpressionTypeTest, EqualityComparisonMatchesConsumerPattern)
{
    EXPECT_TRUE(TS::ExpressionType::Add == TS::ExpressionType::Add);
    EXPECT_FALSE(TS::ExpressionType::Add == TS::ExpressionType::Subtract);
    EXPECT_TRUE(TS::ExpressionType::Subtract != TS::ExpressionType::Add);
    EXPECT_FALSE(TS::ExpressionType::Add != TS::ExpressionType::Add);
    EXPECT_TRUE(TS::ExpressionType::IsFalse != TS::ExpressionType::IsTrue);
    EXPECT_FALSE(TS::ExpressionType::Assign == TS::ExpressionType::AddAssign);
}

// ---------------------------------------------------------------------------
// The 85 operator kinds are distinguishable: a switch over the enum (the
// `ExpressionBuilder` switch that maps each resolved operator back to its C#
// AST node) reaches a distinct arm for each value. A representative subset is
// exercised here (the arithmetic, comparison, shift, compound-assignment, and
// truth-test families the decompiler's operator handling centers on).
// ---------------------------------------------------------------------------
TEST(ExpressionTypeTest, OperatorKindsAreDistinguishable)
{
    for (auto value : {
        TS::ExpressionType::Add,
        TS::ExpressionType::Subtract,
        TS::ExpressionType::Multiply,
        TS::ExpressionType::Equal,
        TS::ExpressionType::LessThan,
        TS::ExpressionType::LeftShift,
        TS::ExpressionType::Assign,
        TS::ExpressionType::AddAssign,
        TS::ExpressionType::PostIncrementAssign,
        TS::ExpressionType::IsTrue,
        TS::ExpressionType::IsFalse,
    }) {
        int arm = -1;
        switch (value) {
            case TS::ExpressionType::Add: arm = 0; break;
            case TS::ExpressionType::Subtract: arm = 1; break;
            case TS::ExpressionType::Multiply: arm = 2; break;
            case TS::ExpressionType::Equal: arm = 3; break;
            case TS::ExpressionType::LessThan: arm = 4; break;
            case TS::ExpressionType::LeftShift: arm = 5; break;
            case TS::ExpressionType::Assign: arm = 6; break;
            case TS::ExpressionType::AddAssign: arm = 7; break;
            case TS::ExpressionType::PostIncrementAssign: arm = 8; break;
            case TS::ExpressionType::IsTrue: arm = 9; break;
            case TS::ExpressionType::IsFalse: arm = 10; break;
            default: break;
        }
        EXPECT_NE(arm, -1);
    }
}
