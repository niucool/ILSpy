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

// Tests for the TransformAssignment foundation + the per-statement Run wiring
// (D132): the UnwrapSmallIntegerConv helper, the IsCompoundStore /
// IsMatchingCompoundLoad / ValidateCompoundAssign shared helpers, and the
// TransformPostIncDecOperatorWithInlineStore binary case (the local/StLoc
// post-increment/decrement fold the helpers unblock). The operator-call
// (op_Increment/op_Decrement) case and the TransformInlineAssignment* /
// TransformPostIncDecOperator / TransformPreIncDecOperatorWithInlineStore
// StObj/Call cases are deferred.

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <filesystem>
#include <functional>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ILSpy::Decompiler::IL::BinaryNumericInstruction;
using ILSpy::Decompiler::IL::BinaryNumericOperator;
using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockContainer;
using ILSpy::Decompiler::IL::CheckImplicitTruncation;
using ILSpy::Decompiler::IL::Comp;
using ILSpy::Decompiler::IL::ComparisonKind;
using ILSpy::Decompiler::IL::CompoundFinalizeMatch;
using ILSpy::Decompiler::IL::CompoundTargetKind;
using ILSpy::Decompiler::IL::Conv;
using ILSpy::Decompiler::IL::ConversionKind;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::IfInstruction;
using ILSpy::Decompiler::IL::ImplicitTruncationResult;
using ILSpy::Decompiler::IL::IsCompoundStore;
using ILSpy::Decompiler::IL::IsImplicitTruncation;
using ILSpy::Decompiler::IL::IsMatchingCompoundLoad;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::PrimitiveType;
using ILSpy::Decompiler::IL::StackType;
using ILSpy::Decompiler::IL::StLoc;
using ILSpy::Decompiler::IL::UnwrapSmallIntegerConv;
using ILSpy::Decompiler::IL::Branch;
using ILSpy::Decompiler::IL::CachedDelegateInitialization;
using ILSpy::Decompiler::IL::CachedReadOnlySpanInitialization;
using ILSpy::Decompiler::IL::ConditionDetection;
using ILSpy::Decompiler::IL::ControlFlowSimplification;
using ILSpy::Decompiler::IL::DetectCatchWhenConditionBlocks;
using ILSpy::Decompiler::IL::DetectPinnedRegions;
using ILSpy::Decompiler::IL::EarlyExpressionTransforms;
using ILSpy::Decompiler::IL::ExpressionTransforms;
using ILSpy::Decompiler::IL::ILInlining;
using ILSpy::Decompiler::IL::InlineReturnTransform;
using ILSpy::Decompiler::IL::LdLocaDupInitObjTransform;
using ILSpy::Decompiler::IL::LockTransform;
using ILSpy::Decompiler::IL::LoopDetection;
using ILSpy::Decompiler::IL::NullCoalescingTransform;
using ILSpy::Decompiler::IL::NullPropagationStatementTransform;
using ILSpy::Decompiler::IL::NullableLiftingStatementTransform;
using ILSpy::Decompiler::IL::PatternMatchingTransform;
using ILSpy::Decompiler::IL::ReadIL;
using ILSpy::Decompiler::IL::RemoveDeadVariableInit;
using ILSpy::Decompiler::IL::RemoveInfeasiblePathTransform;
using ILSpy::Decompiler::IL::StObjToStLoc;
using ILSpy::Decompiler::IL::SwitchDetection;
using ILSpy::Decompiler::IL::SwitchOnNullableTransform;
using ILSpy::Decompiler::IL::UsingTransform;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::CompoundEvalMode;
using ILSpy::Decompiler::IL::ILPhase;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::LdcF4;
using ILSpy::Decompiler::IL::LdcF8;
using ILSpy::Decompiler::IL::LdcI8;
using ILSpy::Decompiler::IL::NumericCompoundAssign;
using ILSpy::Decompiler::IL::StatementTransform;
using ILSpy::Decompiler::IL::StatementTransformContext;
using ILSpy::Decompiler::IL::TransformAssignment;
using ILSpy::Decompiler::IL::ValidateCompoundAssign;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::VariableKind;
using ILSpy::Decompiler::Metadata::MetadataFile;
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

// -----------------------------------------------------------------------------
// IsCompoundStore / IsMatchingCompoundLoad / ValidateCompoundAssign (the D131
// foundation subset). The shared helpers the next in-order TransformAssignment
// compound-assignment folds (HandleCompoundAssign / TransformPostIncDecOperator*
// / TransformPreIncDecOperatorWithInlineStore) consult. Only the StLoc case of
// IsCompoundStore and the LdLoc/StLoc case of IsMatchingCompoundLoad are ported
// (the self-contained cases); the StObj/Call cases of IsCompoundStore and the
// LdObj/StObj + getter/setter cases of IsMatchingCompoundLoad are deferred (need
// InferType / IsSameMember / IMethod). ValidateCompoundAssign is the full
// wrapper (IsBinaryCompatibleWithType + the conv-match check).
// -----------------------------------------------------------------------------

namespace {

// A variable with an explicit Kind + Index (the default `Var` helper makes a
// Local with Index = -1, the synthetic-slot sentinel; split-fragment variables
// share a non-negative Index).
ILVariablePtr VarIdx(std::string name, ITypePtr t, VariableKind kind,
                      std::int32_t index) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = kind;
    v->Type = std::move(t);
    v->Index = index;
    return v;
}

// An empty ILFunction whose body is a single-block container (the
// RecombineVariables finalizeMatch needs an ILFunction + the variables on its
// Variables list + a use of the variable in the body).
std::unique_ptr<ILFunction> MakeFn() {
    auto container = std::make_unique<BlockContainer>();
    container->AddBlock(std::make_unique<Block>());
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

} // namespace

// ---- IsCompoundStore (StLoc case) ----

// A stloc V(...) whose Variable is a Local: storeType = V.Type, value = the
// stored value (a non-owning view of the StLoc's Value child).
TEST(IsCompoundStoreTest, StLocLocalMatches) {
    auto v = Var("V_0", KT(KnownTypeCode::Int32));
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(5));
    auto* valuePtr = st->Value.get();
    ITypePtr storeType;
    ILInstruction* value = nullptr;
    EXPECT_TRUE(IsCompoundStore(st.get(), storeType, value));
    ASSERT_NE(storeType, nullptr);
    EXPECT_EQ(storeType.get(), v->Type.get());
    EXPECT_EQ(value, valuePtr);
}

// A stloc to a Parameter also matches (the C# accepts Local || Parameter).
TEST(IsCompoundStoreTest, StLocParameterMatches) {
    auto p = VarIdx("arg_0", KT(KnownTypeCode::Int32), VariableKind::Parameter, 0);
    auto st = std::make_unique<StLoc>(p, std::make_unique<LdLoc>(Var("x", KT(KnownTypeCode::Int32))));
    ITypePtr storeType;
    ILInstruction* value = nullptr;
    EXPECT_TRUE(IsCompoundStore(st.get(), storeType, value));
    ASSERT_NE(storeType, nullptr);
    EXPECT_EQ(storeType.get(), p->Type.get());
    EXPECT_NE(value, nullptr);
}

// A stloc to a StackSlot does not match (a stack slot is not a compound-assign
// target -- the C# requires Local || Parameter).
TEST(IsCompoundStoreTest, StLocStackSlotRejects) {
    auto s = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto st = std::make_unique<StLoc>(s, std::make_unique<LdcI4>(5));
    ITypePtr storeType;
    ILInstruction* value = nullptr;
    EXPECT_FALSE(IsCompoundStore(st.get(), storeType, value));
    EXPECT_EQ(storeType, nullptr);
    EXPECT_EQ(value, nullptr);
}

// A non-StLoc instruction (an LdLoc) does not match the StLoc case.
TEST(IsCompoundStoreTest, NonStLocRejects) {
    auto ld = std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32)));
    ITypePtr storeType;
    ILInstruction* value = nullptr;
    EXPECT_FALSE(IsCompoundStore(ld.get(), storeType, value));
    EXPECT_EQ(storeType, nullptr);
    EXPECT_EQ(value, nullptr);
}

// ---- IsMatchingCompoundLoad (LdLoc/StLoc case) ----

// A load that is an LdLoc of V and a store that is an StLoc of the same V: the
// target is a fresh LdLoca of V, TargetKind is Address, and finalizeMatch is set
// (to collapse split-fragment variables via RecombineVariables).
TEST(IsMatchingCompoundLoadTest, LdLocStLocSameVariableMatches) {
    auto v = Var("V_0", KT(KnownTypeCode::Int32));
    auto ld = std::make_unique<LdLoc>(v);
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_TRUE(IsMatchingCompoundLoad(ld.get(), st.get(), target, targetKind, finalizeMatch));
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Op, ILSpy::Decompiler::IL::OpCode::LdLoca);
    auto* lda = static_cast<LdLoca*>(target.get());
    EXPECT_EQ(lda->Variable.get(), v.get());
    EXPECT_EQ(targetKind, CompoundTargetKind::Address);
    EXPECT_NE(finalizeMatch, nullptr);
}

// Split fragments (different shared_ptrs, same Kind + Index) match: the
// finalizeMatch collapses them via RecombineVariables.
TEST(IsMatchingCompoundLoadTest, LdLocStLocSplitFragmentsMatch) {
    auto v1 = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    auto v2 = VarIdx("V_0b", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    auto ld = std::make_unique<LdLoc>(v2);
    auto st = std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_TRUE(IsMatchingCompoundLoad(ld.get(), st.get(), target, targetKind, finalizeMatch));
    ASSERT_NE(target, nullptr);
    // The target is the load's variable (the C# `new LdLoca(ldloc.Variable)`).
    auto* lda = static_cast<LdLoca*>(target.get());
    EXPECT_EQ(lda->Variable.get(), v2.get());
    ASSERT_NE(finalizeMatch, nullptr);
    // The finalizeMatch collapses the store's variable into the load's (the C#
    // `RecombineVariables(ldloc.Variable, stloc.Variable)`): v1 (store) is
    // reassigned to v2 (load), so the load/store use one variable afterward.
    auto fn = MakeFn();
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(5)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(),
        std::make_unique<LdLoc>(v1)));
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    finalizeMatch(*fn);
    EXPECT_EQ(std::find_if(fn->Variables.begin(), fn->Variables.end(),
                           [&](const ILVariablePtr& vv) { return vv.get() == v1.get(); }),
              fn->Variables.end())
        << "v1 (the store variable) is dropped from the function";
    bool v2Listed = false;
    for (auto& vv : fn->Variables) if (vv.get() == v2.get()) v2Listed = true;
    EXPECT_TRUE(v2Listed) << "v2 (the load variable) stays";
}

// A forbiddenVariable that is the load/store variable rejects the match (the
// transform would move a store over a use of the variable).
TEST(IsMatchingCompoundLoadTest, ForbiddenVariableRejects) {
    auto v = Var("V_0", KT(KnownTypeCode::Int32));
    auto ld = std::make_unique<LdLoc>(v);
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_FALSE(IsMatchingCompoundLoad(ld.get(), st.get(), target, targetKind,
                                         finalizeMatch, v.get()));
    EXPECT_EQ(target, nullptr);
    EXPECT_EQ(finalizeMatch, nullptr);
}


// Two locals with different Index do not match (distinct variable slots).
TEST(IsMatchingCompoundLoadTest, DifferentIndexRejects) {
    auto v0 = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    auto v1 = VarIdx("V_1", KT(KnownTypeCode::Int32), VariableKind::Local, 1);
    auto ld = std::make_unique<LdLoc>(v0);
    auto st = std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_FALSE(IsMatchingCompoundLoad(ld.get(), st.get(), target, targetKind, finalizeMatch));
    EXPECT_EQ(target, nullptr);
}

// Two DIFFERENT StackSlot variables never match (the
// ILVariableEqualityComparer treats stack slots as distinct -- a stack slot is
// not a compound-assign target). A same-object StackSlot pair would match by
// reference equality, but that case never arises: IsCompoundStore rejects a
// StackSlot store (Kind != Local && != Parameter) before IsMatchingCompoundLoad
// is consulted.
TEST(IsMatchingCompoundLoadTest, DifferentStackSlotsReject) {
    auto s1 = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto s2 = VarIdx("S_1", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto ld = std::make_unique<LdLoc>(s1);
    auto st = std::make_unique<StLoc>(s2, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_FALSE(IsMatchingCompoundLoad(ld.get(), st.get(), target, targetKind, finalizeMatch));
    EXPECT_EQ(target, nullptr);
}

// A non-LdLoc load (an LdLoca) does not match the LdLoc/StLoc case.
TEST(IsMatchingCompoundLoadTest, NonLdLocLoadRejects) {
    auto v = Var("V_0", KT(KnownTypeCode::Int32));
    auto lda = std::make_unique<LdLoca>(v);
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(5));
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_FALSE(IsMatchingCompoundLoad(lda.get(), st.get(), target, targetKind, finalizeMatch));
    EXPECT_EQ(target, nullptr);
}

// A non-StLoc store (an LdLoc) does not match the LdLoc/StLoc case.
TEST(IsMatchingCompoundLoadTest, NonStLocStoreRejects) {
    auto v = Var("V_0", KT(KnownTypeCode::Int32));
    auto ld = std::make_unique<LdLoc>(v);
    auto ld2 = std::make_unique<LdLoc>(v);
    std::unique_ptr<ILInstruction> target;
    CompoundTargetKind targetKind = CompoundTargetKind::Property;
    CompoundFinalizeMatch finalizeMatch;
    EXPECT_FALSE(IsMatchingCompoundLoad(ld.get(), ld2.get(), target, targetKind, finalizeMatch));
    EXPECT_EQ(target, nullptr);
}

// ---- ValidateCompoundAssign ----

// A plain add of two Int32 values is compatible with an Int32 target.
TEST(ValidateCompoundAssignTest, CompatibleBinaryAndTypePasses) {
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I4);
    EXPECT_TRUE(ValidateCompoundAssign(binary.get(), nullptr,
                                       KT(KnownTypeCode::Int32).get(), nullptr));
}

// A lifted binary over a non-Nullable target fails (the IsBinaryCompatibleWithType
// IsLifted gate requires a Nullable<T> store type).
TEST(ValidateCompoundAssignTest, LiftedBinaryOverNonNullableFails) {
    auto lhs = std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32)));
    auto rhs = std::make_unique<LdcI4>(1);
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::move(lhs), std::move(rhs), BinaryNumericOperator::Add,
        StackType::I4, StackType::I4, false, Sign::None, true /*isLifted*/);
    EXPECT_FALSE(ValidateCompoundAssign(binary.get(), nullptr,
                                        KT(KnownTypeCode::Int32).get(), nullptr));
}

// A null conv (the no-conv case) just delegates to IsBinaryCompatibleWithType.
TEST(ValidateCompoundAssignTest, NullConvPassesWhenCompatible) {
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Byte))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I4);
    EXPECT_TRUE(ValidateCompoundAssign(binary.get(), nullptr,
                                       KT(KnownTypeCode::Byte).get(), nullptr));
}

// A conv whose TargetType matches the target type's PrimitiveType and whose
// CheckForOverflow matches the binary's passes.
TEST(ValidateCompoundAssignTest, MatchingConvPasses) {
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Byte))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I4);
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::U1, false, Sign::None);
    // conv.TargetType (U1) == ToPrimitiveType(Byte) (U1), CheckForOverflow false.
    EXPECT_TRUE(ValidateCompoundAssign(binary.get(), conv.get(),
                                        KT(KnownTypeCode::Byte).get(), nullptr));
}

// A conv whose TargetType does not match the target type's PrimitiveType fails.
TEST(ValidateCompoundAssignTest, ConvTargetTypeMismatchFails) {
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Byte))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I4);
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::I1, false, Sign::None);  // I1 != U1 (Byte)
    EXPECT_FALSE(ValidateCompoundAssign(binary.get(), conv.get(),
                                         KT(KnownTypeCode::Byte).get(), nullptr));
}

// A conv whose CheckForOverflow does not match the binary's fails.
TEST(ValidateCompoundAssignTest, ConvOverflowMismatchFails) {
    auto lhs = std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32)));
    auto rhs = std::make_unique<LdcI4>(1);
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::move(lhs), std::move(rhs), BinaryNumericOperator::Add,
        true /*checkForOverflow*/, Sign::Signed);
    auto conv = std::make_unique<Conv>(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        PrimitiveType::I4, false /*checkForOverflow*/, Sign::None);
    // conv.CheckForOverflow (false) != binary.CheckForOverflow (true).
    EXPECT_FALSE(ValidateCompoundAssign(binary.get(), conv.get(),
                                         KT(KnownTypeCode::Int32).get(), nullptr));
}

// ---- TransformAssignment::TransformPostIncDecOperatorWithInlineStore ----
//
// The local/StLoc post-increment/decrement fold:
//   stloc target(binary.add(stloc tmp(ldloc target), ldc.i4 1))
//   -> stloc tmp(compound.assign.add.i4.address.old(ldloca target, ldc.i4 1))
//   = `tmp = target++`
// The store (stloc target) is a single non-terminal at block.Instructions[0];
// binary.Left is the "inline store" stloc tmp(ldloc target) (captures the old
// value into tmp and yields it). The fold promotes tmp to the compound assign's
// result. These tests build the exact post-pipeline shape directly and run the
// transform via TransformAssignment::Run; the shape does not arise on the .NET
// Framework 4 legacy-csc mscorlib corpus (the legacy csc emits the statement
// form `stloc V(binary.add(ldloc V, ldc.i4 1))` whose binary.Left is an LdLoc,
// the deferred TransformPostIncDecOperator / HandleCompoundAssign shape), so
// the fold is faithfulness-only on that corpus (the mscorlib sweep verifies the
// invariant holds, not a fold count).

namespace {

// Run TransformAssignment on `block` at position 0 (the single non-terminal).
void RunTA(Block& block, ILTransformContext& ctx) {
    TransformAssignment ta;
    StatementTransformContext stctx(ctx, &block);
    ta.Run(block, 0, stctx);
}

// Build the WithInlineStore block:
//   block.Instructions[0] = stloc target(binary.op(stloc tmp(ldloc target), ldc.i4 rhs))
// with a Leave(body) final, in a fresh single-block function.
std::unique_ptr<ILFunction> MakeWithInlineStore(
    ILVariablePtr& target, ILVariablePtr& tmp,
    BinaryNumericOperator op, std::int32_t rhsValue) {
    target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto innerStore = std::make_unique<StLoc>(tmp, std::make_unique<LdLoc>(target));
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::move(innerStore), std::make_unique<LdcI4>(rhsValue), op, StackType::I4);
    auto outerStore = std::make_unique<StLoc>(target, std::move(binary));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(outerStore));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    return fn;
}

// Build the non-inline-store (TransformPostIncDecOperator) block:
//   block.Instructions[0] = stloc tmp(ldloc target)
//   block.Instructions[1] = stloc target(binary.op(ldloc tmp, ldc.i4 rhs))
// with a Leave(body) final, in a fresh single-block function. tmp is a StackSlot
// capturing the old value of target; the binary increments target via tmp. When
// `tmpIsLive` is false (the default), tmp is dead (no other use) so the fold
// produces a statement-level `target++`; when true, a trailing `leave(body,
// ldloc tmp)` final keeps tmp live so the fold produces `stloc tmp(target++)`.
std::unique_ptr<ILFunction> MakePostIncDec(
    ILVariablePtr& target, ILVariablePtr& tmp,
    BinaryNumericOperator op, std::int32_t rhsValue, bool tmpIsLive = false) {
    target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto firstStore = std::make_unique<StLoc>(tmp, std::make_unique<LdLoc>(target));
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(tmp), std::make_unique<LdcI4>(rhsValue), op, StackType::I4);
    auto secondStore = std::make_unique<StLoc>(target, std::move(binary));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(firstStore));
    block->Add(std::move(secondStore));
    if (tmpIsLive) {
        block->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(tmp)));
    } else {
        block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    }
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    return fn;
}

} // namespace

// The positive Add fold: stloc target(binary.add(stloc tmp(ldloc target), ldc.i4 1))
// -> stloc tmp(compound.assign.add.i4.address.old(ldloca target, ldc.i4 1)) =
// `tmp = target++`.
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreFoldsAdd) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Add, 1);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* result = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(result, nullptr);
    // The result StLoc carries the inline-store temp (tmp), not the target.
    EXPECT_EQ(result->Variable.get(), tmp.get());
    auto* nca = dynamic_cast<NumericCompoundAssign*>(result->Value.get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Add);
    EXPECT_EQ(nca->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
    EXPECT_EQ(nca->TargetKind, CompoundTargetKind::Address);
    // The target is a fresh LdLoca of the post-inc target.
    auto* lda = dynamic_cast<LdLoca*>(nca->Target.get());
    ASSERT_NE(lda, nullptr);
    EXPECT_EQ(lda->Variable.get(), target.get());
    // The value is the constant 1.
    auto* one = dynamic_cast<LdcI4*>(nca->Value.get());
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->Value, 1);
}

// The Sub fold (post-decrement `tmp = target--`) is also recognised.
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreFoldsSub) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Sub, 1);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* result = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Variable.get(), tmp.get());
    auto* nca = dynamic_cast<NumericCompoundAssign*>(result->Value.get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Sub);
    EXPECT_EQ(nca->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
}

// A non-Add/Sub operator (Mul) does not fold (only ++ / -- are valid).
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreRejectsMul) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Mul, 1);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    // Unchanged: still stloc target(binary.mul(stloc tmp(ldloc target), ldc.i4 1)).
    auto* outer = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Variable.get(), target.get());
    EXPECT_EQ(outer->Value->Op, OpCode::BinaryNumericInstruction);
}

// A right operand that is not the constant 1 does not fold.
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreRejectsNonOneRight) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Add, 2);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* outer = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Variable.get(), target.get());
    EXPECT_EQ(outer->Value->Op, OpCode::BinaryNumericInstruction);
}

// When the inline-store StLoc's variable is a Parameter (not Local/StackSlot),
// the fold is rejected.
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreRejectsParameterTmp) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Add, 1);
    // Overwrite the tmp to a Parameter (the C# requires Local || StackSlot).
    tmp->Kind = VariableKind::Parameter;
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* outer = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Variable.get(), target.get());
    EXPECT_EQ(outer->Value->Op, OpCode::BinaryNumericInstruction);
}

// When binary.Left is not a StLoc (an LdLoc -- the statement-form V++ shape),
// the WithInlineStore fold is rejected (that shape is the deferred
// TransformPostIncDecOperator / HandleCompoundAssign case).
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreRejectsNonStLocLeft) {
    auto target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(target), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I4);
    auto outerStore = std::make_unique<StLoc>(target, std::move(binary));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(outerStore));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* outer = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Variable.get(), target.get());
    EXPECT_EQ(outer->Value->Op, OpCode::BinaryNumericInstruction);
}

// The IntroduceIncrementAndDecrement-off setting gates the fold to a no-op.
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreSettingOffNoOp) {
    ILVariablePtr target, tmp;
    auto fn = MakeWithInlineStore(target, tmp, BinaryNumericOperator::Add, 1);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    ctx.Settings.IntroduceIncrementAndDecrement = false;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* outer = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Variable.get(), target.get());
    EXPECT_EQ(outer->Value->Op, OpCode::BinaryNumericInstruction);
}

// A small-integer conv sign-swap: a Byte (U1, unsigned) store target with a
// conv.i1 (I1, signed) wrapper. The sizes match (1) and the signs differ, so the
// store type is swapped to SByte (I1) before ValidateCompoundAssign, and the
// fold fires with the corrected SByte type. The inline-store temp's type is
// Int32 (not a small integer) so IsImplicitTruncation(ldloc target, tmp.Type)
// returns ValuePreserved immediately (the D128 conservative approximation has
// no InferType, so an unmodeled LdLoc over a small-integer tmp type would
// conservatively reject; the real shape has tmp.Type == target.Type and the
// C# InferType resolves the ldloc to Byte which fits -- a faithfulness gap in
// the approximation, not the transform).
TEST(TransformAssignmentTest, PostIncDecWithInlineStoreSmallIntConvSignSwap) {
    auto target = VarIdx("V_0", KT(KnownTypeCode::Byte), VariableKind::Local, 0);
    auto tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto innerStore = std::make_unique<StLoc>(tmp, std::make_unique<LdLoc>(target));
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::move(innerStore), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I4);
    // conv.i1 (Truncate to I1, a small-integer target) wrapping the binary.
    auto conv = std::make_unique<Conv>(std::move(binary),
                                        PrimitiveType::I1, false, Sign::None);
    ASSERT_EQ(conv->Kind, ConversionKind::Truncate);
    ASSERT_TRUE(IsSmallIntegerType(conv->TargetType));
    auto outerStore = std::make_unique<StLoc>(target, std::move(conv));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(outerStore));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* result = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Variable.get(), tmp.get());
    auto* nca = dynamic_cast<NumericCompoundAssign*>(result->Value.get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Add);
    EXPECT_EQ(nca->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
    // The type operand was swapped from Byte (U1) to SByte (I1).
    ASSERT_NE(nca->Type, nullptr);
    auto* k = dynamic_cast<const KnownType*>(nca->Type.get());
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(k->Code(), KnownTypeCode::SByte);
}

// ---- TransformPostIncDecOperator (non-inline-store, two-instruction) tests ----
// The legacy csc / Roslyn post-increment codegen:
//   stloc tmp(ldloc target)              at Instructions[0]
//   stloc target(binary.op(ldloc tmp, 1)) at Instructions[1]
// -> stloc tmp(compound.op.old(ldloca target, 1)) = `tmp = target++`, and the
// store at [1] is removed. When tmp is dead (single-def, load-count 0), the StLoc
// is replaced with the compound assign directly (a statement-level `target++`).

// The positive Add fold with a dead tmp: the StLoc is replaced with the bare
// compound assign (a statement-level `target++`), and the store at [1] is gone.
TEST(TransformAssignmentTest, PostIncDecFoldsAddDeadTmp) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Add, 1, false);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    // The store at [1] was removed; the StLoc at [0] was replaced with the NCA.
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* nca = dynamic_cast<NumericCompoundAssign*>(block->Instructions[0].get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Add);
    EXPECT_EQ(nca->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
    EXPECT_EQ(nca->TargetKind, CompoundTargetKind::Address);
    auto* lda = dynamic_cast<LdLoca*>(nca->Target.get());
    ASSERT_NE(lda, nullptr);
    EXPECT_EQ(lda->Variable.get(), target.get());
    auto* one = dynamic_cast<LdcI4*>(nca->Value.get());
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->Value, 1);
}

// The Sub fold (post-decrement `target--`) with a dead tmp.
TEST(TransformAssignmentTest, PostIncDecFoldsSubDeadTmp) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Sub, 1, false);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* nca = dynamic_cast<NumericCompoundAssign*>(block->Instructions[0].get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Sub);
}

// When tmp is live (used by the leave final), the StLoc survives: `stloc
// tmp(target++)` + `leave(tmp)` = `tmp = target++; return tmp`.
TEST(TransformAssignmentTest, PostIncDecFoldsAddLiveTmp) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Add, 1, true);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    // The store at [1] was removed; the StLoc at [0] survives (tmp is live).
    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* st = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable.get(), tmp.get());
    auto* nca = dynamic_cast<NumericCompoundAssign*>(st->Value.get());
    ASSERT_NE(nca, nullptr);
    EXPECT_EQ(nca->Operator, BinaryNumericOperator::Add);
    EXPECT_EQ(nca->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
}

// A non-Add/Sub operator (Mul) does not fold (only ++ / -- are valid).
TEST(TransformAssignmentTest, PostIncDecRejectsMul) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Mul, 1, false);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: still 2 instructions.
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// A right operand that is not the constant 1 does not fold.
TEST(TransformAssignmentTest, PostIncDecRejectsNonOneRight) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Add, 2, false);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// When the store at [1] is not a StLoc (the IsCompoundStore gate rejects a
// non-Local/Parameter store), the fold does not fire. Build a block whose [1]
// instruction is not an StLoc.
TEST(TransformAssignmentTest, PostIncDecRejectsNonStLocStore) {
    ILVariablePtr target, tmp;
    target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    auto firstStore = std::make_unique<StLoc>(tmp, std::make_unique<LdLoc>(target));
    // A non-StLoc at [1] (an LdcI4, not a store): IsCompoundStore rejects it.
    auto notStore = std::make_unique<LdcI4>(42);
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(firstStore));
    block->Add(std::move(notStore));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// When the inst at [0] is not a StLoc, the fold does not fire.
TEST(TransformAssignmentTest, PostIncDecRejectsNonStLocInst) {
    ILVariablePtr target, tmp;
    target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    // A non-StLoc at [0]: the dynamic_cast<StLoc*> fails.
    auto notInst = std::make_unique<LdcI4>(42);
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(tmp), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I4);
    auto store = std::make_unique<StLoc>(target, std::move(binary));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(notInst));
    block->Add(std::move(store));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// When binary.Left is not ldloc tmp, the fold does not fire.
TEST(TransformAssignmentTest, PostIncDecRejectsNonMatchingLeft) {
    ILVariablePtr target, tmp, other;
    target = VarIdx("V_0", KT(KnownTypeCode::Int32), VariableKind::Local, 0);
    tmp = VarIdx("S_0", KT(KnownTypeCode::Int32), VariableKind::StackSlot, -1);
    other = VarIdx("V_1", KT(KnownTypeCode::Int32), VariableKind::Local, 1);
    auto firstStore = std::make_unique<StLoc>(tmp, std::make_unique<LdLoc>(target));
    // binary.Left is ldloc other (not tmp): the MatchLdLoc(tmpVar) check fails.
    auto binary = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(other), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I4);
    auto store = std::make_unique<StLoc>(target, std::move(binary));
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    block->Add(std::move(firstStore));
    block->Add(std::move(store));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(target);
    fn->Variables.push_back(tmp);
    fn->Variables.push_back(other);
    ILTransformContext ctx;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// When the IntroduceIncrementAndDecrement setting is off, the fold does not fire.
TEST(TransformAssignmentTest, PostIncDecSettingOffNoOp) {
    ILVariablePtr target, tmp;
    auto fn = MakePostIncDec(target, tmp, BinaryNumericOperator::Add, 1, false);
    auto* block = fn->Body->Blocks[0].get();
    ILTransformContext ctx;
    ctx.Settings.IntroduceIncrementAndDecrement = false;
    RunTA(*block, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(block->Instructions.size(), 2u);
}

// ---- mscorlib sweep (TransformAssignment in the full per-statement pipeline) ----

// Run the GetILTransforms() pre-pipeline through CachedReadOnlySpanInitialization
// (the position before the StatementTransform that holds TransformAssignment).
void RunPrePipeline(ILFunction& fn, ILTransformContext& ctx) {
    ControlFlowSimplification().Run(fn, ctx);
    StObjToStLoc().Run(fn, ctx);
    ILInlining().Run(fn, ctx);
    InlineReturnTransform().Run(fn, ctx);
    RemoveInfeasiblePathTransform().Run(fn, ctx);
    DetectPinnedRegions().Run(fn, ctx);
    DetectCatchWhenConditionBlocks().Run(fn, ctx);
    LdLocaDupInitObjTransform().Run(fn, ctx);
    EarlyExpressionTransforms().Run(fn, ctx);
    RemoveDeadVariableInit().Run(fn, ctx);
    ControlFlowSimplification().Run(fn, ctx);
    SwitchDetection().Run(fn, ctx);
    SwitchOnNullableTransform().Run(fn, ctx);
    LoopDetection().Run(fn, ctx);
    PatternMatchingTransform().Run(fn, ctx);
    ConditionDetection().Run(fn, ctx);
    LockTransform().Run(fn, ctx);
    UsingTransform().Run(fn, ctx);
    CachedDelegateInitialization().Run(fn, ctx);
    CachedReadOnlySpanInitialization().Run(fn, ctx);
    {
        StatementTransform st;
        st.AddChild(std::make_unique<ILInlining>());
        st.AddChild(std::make_unique<ExpressionTransforms>());
        st.AddChild(std::make_unique<TransformAssignment>());
        st.AddChild(std::make_unique<NullCoalescingTransform>());
        st.AddChild(std::make_unique<NullableLiftingStatementTransform>());
        st.AddChild(std::make_unique<NullPropagationStatementTransform>());
        st.Run(fn, ctx);
    }
}

// Count NumericCompoundAssign nodes with EvaluatesToOldValue (the
// TransformPostIncDecOperatorWithInlineStore result shape = `tmp = target++`).
int CountNumericCompoundAssignOld(ILFunction& fn) {
    int count = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* nca = dynamic_cast<NumericCompoundAssign*>(inst)) {
            if (nca->EvalMode == CompoundEvalMode::EvaluatesToOldValue)
                ++count;
        }
        for (int i = 0; i < inst->ChildCount(); ++i)
            walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return count;
}

// The TransformPostIncDecOperator shape (the non-inline-store two-instruction
// `stloc tmp(ldloc target)` + `stloc target(binary.op(ldloc tmp, 1))` post-
// increment) arises on the .NET Framework 4 legacy-csc mscorlib corpus (a corpus
// probe found 16 occurrences across 8000 methods), unlike the WithInlineStore
// expression form which fires 0 times. The sweep verifies the ILAst invariant
// holds across the corpus with TransformAssignment in the full per-statement
// pipeline (the transform does not crash or corrupt the tree) and the total
// NumericCompoundAssign-EvaluatesToOldValue count across the corpus is > 0
// (the transform fires on real code, not just faithfulness-only).
TEST(TransformAssignmentTest, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int totalFolds = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        totalFolds += CountNumericCompoundAssignOld(*fn);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The TransformPostIncDecOperator fold fires on the legacy-csc corpus (16
    // times across 8000 methods per a corpus probe), so the total NCA count is > 0.
    EXPECT_GT(totalFolds, 0);
}
