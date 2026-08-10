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
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
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
using ILSpy::Decompiler::IL::CheckImplicitTruncation;
using ILSpy::Decompiler::IL::Comp;
using ILSpy::Decompiler::IL::ComparisonKind;
using ILSpy::Decompiler::IL::Conv;
using ILSpy::Decompiler::IL::ConversionKind;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::IfInstruction;
using ILSpy::Decompiler::IL::ImplicitTruncationResult;
using ILSpy::Decompiler::IL::IsImplicitTruncation;
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

// -----------------------------------------------------------------------------
// CheckImplicitTruncation / IsImplicitTruncation (the D128 foundation). The
// truncation analysis the future NumericCompoundAssign.IsBinaryCompatibleWithType
// gate consults: whether `stobj type(..., value)` would evaluate to a different
// value than `value` due to implicit truncation. Only small-integer targets can
// truncate; the analysis recurses into LdcI4 constants, Convs, Comps (0/1),
// BitAnd/BitOr/BitXor binaries, and IfInstruction arms.
// -----------------------------------------------------------------------------

namespace {

// A Comp(ldloc a, ldc.i4 0) -- comp returns 0 or 1, which always fits a small
// integer, so CheckImplicitTruncation must report ValuePreserved regardless of
// the target.
std::unique_ptr<Comp> MakeCompToZero() {
    return std::make_unique<Comp>(
        std::make_unique<LdLoc>(Var("a", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(0), ComparisonKind::Equality, false);
}

} // namespace

// A non-small-integer target (Int32) never truncates -- the outer guard returns
// ValuePreserved before consulting the value shape.
TEST(CheckImplicitTruncationTest, NonSmallIntegerTargetReturnsValuePreserved) {
    auto ldc = std::make_unique<LdcI4>(100000);
    EXPECT_EQ(CheckImplicitTruncation(ldc.get(), KT(KnownTypeCode::Int32).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// An LdcI4 that fits the small-integer target's range is preserved.
TEST(CheckImplicitTruncationTest, LdcI4FitsByteReturnsValuePreserved) {
    auto ldc = std::make_unique<LdcI4>(5);
    EXPECT_EQ(CheckImplicitTruncation(ldc.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// An LdcI4 that overflows the target's range is changed.
TEST(CheckImplicitTruncationTest, LdcI4DoesNotFitByteReturnsValueChanged) {
    auto ldc = std::make_unique<LdcI4>(256);
    EXPECT_EQ(CheckImplicitTruncation(ldc.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// A negative LdcI4 that fits SByte's range is preserved.
TEST(CheckImplicitTruncationTest, LdcI4FitsSByteNegativeReturnsValuePreserved) {
    auto ldc = std::make_unique<LdcI4>(-1);
    EXPECT_EQ(CheckImplicitTruncation(ldc.get(), KT(KnownTypeCode::SByte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// An LdcI4 outside SByte's range is changed.
TEST(CheckImplicitTruncationTest, LdcI4DoesNotFitSByteReturnsValueChanged) {
    auto ldc = std::make_unique<LdcI4>(200);
    EXPECT_EQ(CheckImplicitTruncation(ldc.get(), KT(KnownTypeCode::SByte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// Boolean accepts only 0 and 1.
TEST(CheckImplicitTruncationTest, LdcI4BooleanFitsZeroAndOneReturnsValuePreserved) {
    auto z = std::make_unique<LdcI4>(0);
    auto one = std::make_unique<LdcI4>(1);
    EXPECT_EQ(CheckImplicitTruncation(z.get(), KT(KnownTypeCode::Boolean).get()),
              ImplicitTruncationResult::ValuePreserved);
    EXPECT_EQ(CheckImplicitTruncation(one.get(), KT(KnownTypeCode::Boolean).get()),
              ImplicitTruncationResult::ValuePreserved);
}

TEST(CheckImplicitTruncationTest, LdcI4BooleanDoesNotFitTwoReturnsValueChanged) {
    auto two = std::make_unique<LdcI4>(2);
    EXPECT_EQ(CheckImplicitTruncation(two.get(), KT(KnownTypeCode::Boolean).get()),
              ImplicitTruncationResult::ValueChanged);
}

// Char / UInt16 accept 0..65535.
TEST(CheckImplicitTruncationTest, LdcI4CharBoundaryReturnsValuePreservedOrChanged) {
    auto max = std::make_unique<LdcI4>(65535);
    auto over = std::make_unique<LdcI4>(65536);
    EXPECT_EQ(CheckImplicitTruncation(max.get(), KT(KnownTypeCode::Char).get()),
              ImplicitTruncationResult::ValuePreserved);
    EXPECT_EQ(CheckImplicitTruncation(over.get(), KT(KnownTypeCode::Char).get()),
              ImplicitTruncationResult::ValueChanged);
    EXPECT_EQ(CheckImplicitTruncation(max.get(), KT(KnownTypeCode::UInt16).get()),
              ImplicitTruncationResult::ValuePreserved);
    EXPECT_EQ(CheckImplicitTruncation(over.get(), KT(KnownTypeCode::UInt16).get()),
              ImplicitTruncationResult::ValueChanged);
}

// Int16 accepts -32768..32767.
TEST(CheckImplicitTruncationTest, LdcI4Int16BoundaryReturnsValuePreservedOrChanged) {
    auto lo = std::make_unique<LdcI4>(-32768);
    auto hi = std::make_unique<LdcI4>(32768);
    EXPECT_EQ(CheckImplicitTruncation(lo.get(), KT(KnownTypeCode::Int16).get()),
              ImplicitTruncationResult::ValuePreserved);
    EXPECT_EQ(CheckImplicitTruncation(hi.get(), KT(KnownTypeCode::Int16).get()),
              ImplicitTruncationResult::ValueChanged);
}

// A conv whose TargetType equals the store target's PrimitiveType is preserved.
TEST(CheckImplicitTruncationTest, ConvSamePrimitiveTypeReturnsValuePreserved) {
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::U1, false, Sign::None);
    EXPECT_EQ(CheckImplicitTruncation(conv.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// A conv to the same size but opposite sign (U1 target, I1 conv) is a
// sign-mismatch fixable by flipping the target's sign.
TEST(CheckImplicitTruncationTest, ConvSameSizeOppositeSignReturnsSignMismatch) {
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::I1, false, Sign::None);
    EXPECT_EQ(CheckImplicitTruncation(conv.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChangedDueToSignMismatch);
}

// A conv to a different size (I4 conv, Byte target) is a plain truncation.
TEST(CheckImplicitTruncationTest, ConvDifferentSizeReturnsValueChanged) {
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int64))),
        PrimitiveType::I4, false, Sign::None);
    EXPECT_EQ(CheckImplicitTruncation(conv.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// A Comp always returns 0 or 1, which always fits a small integer.
TEST(CheckImplicitTruncationTest, CompAlwaysReturnsValuePreserved) {
    auto comp = MakeCompToZero();
    EXPECT_EQ(CheckImplicitTruncation(comp.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValuePreserved);
    EXPECT_EQ(CheckImplicitTruncation(comp.get(), KT(KnownTypeCode::SByte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// BitAnd of two LdcI4 that both fit the target is preserved.
TEST(CheckImplicitTruncationTest, BitAndOfFittingLdcI4ReturnsValuePreserved) {
    auto bni = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::BitAnd, StackType::I4);
    EXPECT_EQ(CheckImplicitTruncation(bni.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// BitAnd where one side overflows the target is changed.
TEST(CheckImplicitTruncationTest, BitAndWithNonFittingSideReturnsValueChanged) {
    auto bni = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(256),
        BinaryNumericOperator::BitAnd, StackType::I4);
    EXPECT_EQ(CheckImplicitTruncation(bni.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// A short-circuit: when the left side is a plain ValueChanged, the right side
// is not evaluated (the result is ValueChanged regardless).
TEST(CheckImplicitTruncationTest, BitAndShortCircuitsOnLeftValueChanged) {
    auto bni = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdcI4>(256), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::BitAnd, StackType::I4);
    EXPECT_EQ(CheckImplicitTruncation(bni.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// CommonImplicitTruncation: a sign-mismatch on one side and a preserved other
// side yields ValueChanged (the other side's sign must not be flipped).
TEST(CheckImplicitTruncationTest, BitAndSignMismatchAndPreservedReturnsValueChanged) {
    auto signMismatch = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::I1, false, Sign::None);
    auto bni = std::make_unique<BinaryNumericInstruction>(
        std::move(signMismatch), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::BitAnd, StackType::I4);
    EXPECT_EQ(CheckImplicitTruncation(bni.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// A BitOr operator other than BitAnd/BitOr/BitXor (e.g. Add) is not recursed
// into -- it falls through to the conservative else (ValueChanged for a small
// integer target).
TEST(CheckImplicitTruncationTest, BitAndNonBitwiseOperatorFallsThroughToChanged) {
    auto bni = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::Add, StackType::I4);
    EXPECT_EQ(CheckImplicitTruncation(bni.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// An IfInstruction whose both arms fit the target is preserved.
TEST(CheckImplicitTruncationTest, IfInstructionBothArmsFitReturnsValuePreserved) {
    auto iff = std::make_unique<IfInstruction>(
        MakeCompToZero(), std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2));
    EXPECT_EQ(CheckImplicitTruncation(iff.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValuePreserved);
}

// An IfInstruction whose one arm overflows is changed.
TEST(CheckImplicitTruncationTest, IfInstructionOneArmDoesNotFitReturnsValueChanged) {
    auto iff = std::make_unique<IfInstruction>(
        MakeCompToZero(), std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(256));
    EXPECT_EQ(CheckImplicitTruncation(iff.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// An unmodeled value (a LdLoc, the else-branch InferType case) on a small
// integer target is approximated conservatively as ValueChanged -- a compound
// assignment to a small integer with an unmodeled RHS does not fold.
TEST(CheckImplicitTruncationTest, UnmodeledValueReturnsValueChanged) {
    auto ld = std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32)));
    EXPECT_EQ(CheckImplicitTruncation(ld.get(), KT(KnownTypeCode::Byte).get()),
              ImplicitTruncationResult::ValueChanged);
}

// IsImplicitTruncation is the (Check != ValuePreserved) view.
TEST(CheckImplicitTruncationTest, IsImplicitTruncationMatchesCheck) {
    auto fits = std::make_unique<LdcI4>(5);
    auto overflows = std::make_unique<LdcI4>(256);
    EXPECT_FALSE(IsImplicitTruncation(fits.get(), KT(KnownTypeCode::Byte).get()));
    EXPECT_TRUE(IsImplicitTruncation(overflows.get(), KT(KnownTypeCode::Byte).get()));
    // A non-small-integer target never truncates.
    EXPECT_FALSE(IsImplicitTruncation(overflows.get(), KT(KnownTypeCode::Int32).get()));
}
