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
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

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
using ILSpy::Decompiler::IL::ValidateCompoundAssign;
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
