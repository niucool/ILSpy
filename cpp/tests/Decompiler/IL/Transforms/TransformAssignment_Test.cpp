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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the TransformAssignment foundation (D126): the UnwrapSmallIntegerConv
// helper that the compound-assignment folds consult to peel the compiler's `conv`
// truncation to a small integer that a compound assign to a small-integer local/
// field carries. The full TransformAssignment (IsCompoundStore /
// IsMatchingCompoundLoad / ValidateCompoundAssign + RecombineVariables + the
// per-statement Run wiring) is a larger slice and a subsequent iteration; this
// tests the self-contained helper ahead of that.

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ILSpy::Decompiler::IL::BinaryNumericInstruction;
using ILSpy::Decompiler::IL::BinaryNumericOperator;
using ILSpy::Decompiler::IL::Conv;
using ILSpy::Decompiler::IL::ConversionKind;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::PrimitiveType;
using ILSpy::Decompiler::IL::StackType;
using ILSpy::Decompiler::IL::UnwrapSmallIntegerConv;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::VariableKind;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Sign;

namespace {

ILVariablePtr Var(std::string name, ITypePtr t) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = VariableKind::Local;
    v->Type = std::move(t);
    return v;
}

ITypePtr KT(KnownTypeCode c) { return std::make_shared<KnownType>(c); }

// A small-integer-truncating conv over an LdLoc -- `conv.i1(ldloc v)` for an
// Int64 v (I8 -> I1 is a Truncate to a small-integer TargetType), the shape the
// compiler emits wrapping the binary in a compound assign to a small-integer
// local/field.
std::unique_ptr<Conv> MakeSmallIntTruncConv() {
    return std::make_unique<Conv>(std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int64))),
                                  PrimitiveType::I1, false, Sign::None);
}

} // namespace

// A small-integer-truncating conv unwraps to its argument and reports the conv.
TEST(UnwrapSmallIntegerConvTest, UnwrapsTruncateToSmallIntegerTarget)
{
    auto convOwner = MakeSmallIntTruncConv();
    Conv* reported = nullptr;
    auto* unwrapped = UnwrapSmallIntegerConv(convOwner.get(), reported);
    // The unwrapped instruction is the conv's argument (the LdLoc).
    ASSERT_NE(unwrapped, nullptr);
    EXPECT_EQ(unwrapped, convOwner->Argument.get());
    // The conv is reported so the caller can validate it separately.
    ASSERT_NE(reported, nullptr);
    EXPECT_EQ(reported, convOwner.get());
    EXPECT_EQ(reported->Kind, ConversionKind::Truncate);
    EXPECT_TRUE(IsSmallIntegerType(reported->TargetType));
}

// A Truncate to a non-small-integer TargetType (conv.i4 from I8) does NOT
// unwrap -- the helper only peels truncations to small integers.
TEST(UnwrapSmallIntegerConvTest, DoesNotUnwrapTruncateToNonSmallInteger)
{
    auto convOwner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int64))),
        PrimitiveType::I4, false, Sign::None);
    ASSERT_EQ(convOwner->Kind, ConversionKind::Truncate);
    ASSERT_FALSE(IsSmallIntegerType(convOwner->TargetType));
    Conv* reported = nullptr;
    auto* unwrapped = UnwrapSmallIntegerConv(convOwner.get(), reported);
    // Returns the conv itself unchanged (not the argument).
    EXPECT_EQ(unwrapped, convOwner.get());
}

// A non-Truncate conv (a SignExtend from I4 to I8) does NOT unwrap.
TEST(UnwrapSmallIntegerConvTest, DoesNotUnwrapNonTruncateConv)
{
    auto convOwner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::I8, false, Sign::None);
    ASSERT_NE(convOwner->Kind, ConversionKind::Truncate);
    Conv* reported = nullptr;
    auto* unwrapped = UnwrapSmallIntegerConv(convOwner.get(), reported);
    EXPECT_EQ(unwrapped, convOwner.get());
}

// A non-Conv instruction is returned unchanged with conv == nullptr.
TEST(UnwrapSmallIntegerConvTest, ReturnsNonConvUnchanged)
{
    auto ldc = std::make_unique<LdcI4>(1);
    Conv* reported = nullptr;
    auto* unwrapped = UnwrapSmallIntegerConv(ldc.get(), reported);
    EXPECT_EQ(unwrapped, ldc.get());
    EXPECT_EQ(reported, nullptr);
}

// A small-integer-truncating conv over a BinaryNumericInstruction unwraps to
// the binary -- the shape the compound-assignment fold consumes
// (`stloc V(conv.i1(binary.add(ldloc V, ldc.i4 1)))` -> the binary).
TEST(UnwrapSmallIntegerConvTest, UnwrapsToBinaryNumericInstruction)
{
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Byte))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I4);
    auto* binaryRaw = binary.get();
    auto convOwner = std::make_unique<Conv>(std::move(binary),
                                            PrimitiveType::U1, false, Sign::None);
    ASSERT_EQ(convOwner->Kind, ConversionKind::Truncate);
    ASSERT_TRUE(IsSmallIntegerType(convOwner->TargetType));
    Conv* reported = nullptr;
    auto* unwrapped = UnwrapSmallIntegerConv(convOwner.get(), reported);
    ASSERT_NE(unwrapped, nullptr);
    EXPECT_EQ(unwrapped, binaryRaw);
    EXPECT_EQ(unwrapped->Op, ILSpy::Decompiler::IL::OpCode::BinaryNumericInstruction);
    ASSERT_NE(reported, nullptr);
    EXPECT_EQ(reported->TargetType, PrimitiveType::U1);
}
