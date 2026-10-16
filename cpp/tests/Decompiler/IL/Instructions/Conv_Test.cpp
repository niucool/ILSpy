// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Tests for the Conv node's faithful ConversionKind model (D85): the Kind /
// InputType / InputSign / TargetType the IL reader derives per conv.* opcode,
// and the UnwrapConv helper. The Kind computation mirrors the C#
// Conv.GetConversionKind (Ecma-335 Table 8); the array-index cleanup
// (ExpressionTransforms.CleanUpArrayIndices) consumes Kind to drop redundant
// `conv.i` widenings, so the per-opcode Kind is the contract that transform
// relies on.

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
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

// Build a Conv over an LdLoc of a variable of the given known type, mirroring
// the IL reader's per-opcode emission (targetType, checkForOverflow, inputSign).
Conv MakeConv(KnownTypeCode inputType, PrimitiveType target, bool ovf, Sign sign) {
    return Conv(std::make_unique<LdLoc>(Var("v", KT(inputType))), target, ovf, sign);
}

// Port of ILInstruction.UnwrapConv(kind) (non-owning): descend into a Conv of
// the requested kind's argument (recursively), else return inst unchanged.
const ILInstruction* UnwrapConvFree(const ILInstruction* inst, ConversionKind kind) {
    while (inst && inst->Op == OpCode::Conv) {
        auto* conv = static_cast<const Conv*>(inst);
        if (conv->Kind != kind) break;
        inst = conv->Argument.get();
    }
    return inst;
}

} // namespace

// GetStackType maps the PrimitiveType taxonomy to the StackType lattice,
// collapsing the signed/unsigned sizes (I1/U1/I2/U2/I4/U4 -> I4; I8/U8 -> I8;
// I/U -> I; R4 -> F4; R8/R -> F8; Ref -> Ref; None/Unknown -> Unknown).
TEST(Conv, PrimitiveTypeGetStackType) {
    EXPECT_EQ(GetStackType(PrimitiveType::I1), StackType::I4);
    EXPECT_EQ(GetStackType(PrimitiveType::U4), StackType::I4);
    EXPECT_EQ(GetStackType(PrimitiveType::I8), StackType::I8);
    EXPECT_EQ(GetStackType(PrimitiveType::U8), StackType::I8);
    EXPECT_EQ(GetStackType(PrimitiveType::I), StackType::I);
    EXPECT_EQ(GetStackType(PrimitiveType::U), StackType::I);
    EXPECT_EQ(GetStackType(PrimitiveType::R4), StackType::F4);
    EXPECT_EQ(GetStackType(PrimitiveType::R8), StackType::F8);
    EXPECT_EQ(GetStackType(PrimitiveType::R), StackType::F8);
    EXPECT_EQ(GetStackType(PrimitiveType::Ref), StackType::Ref);
    EXPECT_EQ(GetStackType(PrimitiveType::None), StackType::Unknown);
}

// IsIntegerType / IsFloatType on PrimitiveType and StackType.
TEST(Conv, IsIntegerAndFloatTypeQueries) {
    EXPECT_TRUE(IsIntegerType(PrimitiveType::I4));
    EXPECT_TRUE(IsIntegerType(PrimitiveType::U8));
    EXPECT_TRUE(IsIntegerType(PrimitiveType::I));
    EXPECT_FALSE(IsIntegerType(PrimitiveType::R4));
    EXPECT_FALSE(IsIntegerType(PrimitiveType::R));
    EXPECT_TRUE(IsFloatType(PrimitiveType::R4));
    EXPECT_TRUE(IsFloatType(PrimitiveType::R8));
    EXPECT_TRUE(IsFloatType(PrimitiveType::R));
    EXPECT_FALSE(IsFloatType(PrimitiveType::I4));
    EXPECT_TRUE(IsIntegerType(StackType::I4));
    EXPECT_TRUE(IsIntegerType(StackType::I));
    EXPECT_TRUE(IsIntegerType(StackType::I8));
    EXPECT_FALSE(IsIntegerType(StackType::F8));
    EXPECT_TRUE(IsFloatType(StackType::F4));
    EXPECT_TRUE(IsFloatType(StackType::F8));
    EXPECT_FALSE(IsFloatType(StackType::I4));
}

// HasOppositeSign(PrimitiveType): the integer primitives I1..U8/I/U have a
// distinct opposite-sign counterpart (so a sign-mismatch truncation can be
// fixed by flipping the target sign); the float/ref/none/unknown primitives do
// not. Consulted by TransformAssignment.CheckImplicitTruncation's Conv case.
TEST(Conv, PrimitiveTypeHasOppositeSign) {
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::I1));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::U1));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::I2));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::U2));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::I4));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::U4));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::I8));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::U8));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::I));
    EXPECT_TRUE(HasOppositeSign(PrimitiveType::U));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::R4));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::R8));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::R));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::Ref));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::None));
    EXPECT_FALSE(HasOppositeSign(PrimitiveType::Unknown));
}

// conv.i from I4 is a SignExtend (I4 -> I): the canonical array-index widening
// CleanUpArrayIndices drops. ResultType is I.
TEST(Conv, ConvIFromI4IsSignExtend) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::I, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::SignExtend);
    EXPECT_EQ(c.ResultType(), StackType::I);
    EXPECT_EQ(c.InputType, StackType::I4);
    EXPECT_EQ(c.InputSign, Sign::None);
    EXPECT_FALSE(c.CheckForOverflow);
}

// conv.u from I4 is a ZeroExtend (I4 -> I): the unsigned array-index widening.
TEST(Conv, ConvUFromI4IsZeroExtend) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::U, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::ZeroExtend);
    EXPECT_EQ(c.ResultType(), StackType::I);
}

// conv.i from I8 is a Truncate (I8 -> I) WITHOUT overflow check: a real
// truncation that CleanUpArrayIndices must KEEP.
TEST(Conv, ConvIFromI8IsUncheckedTruncate) {
    Conv c = MakeConv(KnownTypeCode::Int64, PrimitiveType::I, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::Truncate);
    EXPECT_EQ(c.ResultType(), StackType::I);
    EXPECT_FALSE(c.CheckForOverflow);
}

// conv.i from I (native) is a Nop: same-size, no widening, no truncation.
TEST(Conv, ConvIFromNativeIsNop) {
    Conv c = MakeConv(KnownTypeCode::IntPtr, PrimitiveType::I, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::Nop);
    EXPECT_EQ(c.ResultType(), StackType::I);
}

// conv.ovf.i from I4 is a SignExtend WITH overflow check: the checked widening
// CleanUpArrayIndices drops (inputSign forced Signed by needsSign).
TEST(Conv, ConvOvfIFromI4IsCheckedSignExtend) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::I, true, Sign::Signed);
    EXPECT_EQ(c.Kind, ConversionKind::SignExtend);
    EXPECT_TRUE(c.CheckForOverflow);
    EXPECT_EQ(c.InputSign, Sign::Signed);
}

// conv.ovf.u from I4 with a Signed input is a SignExtend (the C# GetConversionKind
// for the I/U target decides SignExtend vs ZeroExtend by the INPUT sign when
// overflow-checking, not the target -- a signed input sign-extends); the
// checked widening is still Kind == SignExtend, which CleanUpArrayIndices drops.
TEST(Conv, ConvOvfUFromI4IsCheckedSignExtend) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::U, true, Sign::Signed);
    EXPECT_EQ(c.Kind, ConversionKind::SignExtend);
    EXPECT_TRUE(c.CheckForOverflow);
    EXPECT_EQ(c.InputSign, Sign::Signed);
}

// conv.ovf.i_un from I4: the _un suffix forces an Unsigned input sign, so the
// I4->I widening is a ZeroExtend (not SignExtend) even though the target is I.
TEST(Conv, ConvOvfIUnFromI4IsZeroExtend) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::I, true, Sign::Unsigned);
    EXPECT_EQ(c.Kind, ConversionKind::ZeroExtend);
    EXPECT_EQ(c.InputSign, Sign::Unsigned);
    EXPECT_TRUE(c.CheckForOverflow);
}

// conv.i4 from I4 is a Nop: same-size signed-to-signed, no widening/truncation.
TEST(Conv, ConvI4FromI4IsNop) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::I4, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::Nop);
    EXPECT_EQ(c.ResultType(), StackType::I4);
}

// conv.i4 from I8 is a Truncate (I8 -> I4).
TEST(Conv, ConvI4FromI8IsTruncate) {
    Conv c = MakeConv(KnownTypeCode::Int64, PrimitiveType::I4, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::Truncate);
    EXPECT_EQ(c.ResultType(), StackType::I4);
}

// conv.i1 from I4 is a Truncate (I4 -> the smaller I1, then sign/zero-extended
// back to I4 on the stack): Kind Truncate, ResultType I4.
TEST(Conv, ConvI1FromI4IsTruncate) {
    Conv c = MakeConv(KnownTypeCode::Int32, PrimitiveType::I1, false, Sign::None);
    EXPECT_EQ(c.Kind, ConversionKind::Truncate);
    EXPECT_EQ(c.ResultType(), StackType::I4);
}

// conv.i8 from I4 is a SignExtend; conv.u8 from I4 is a ZeroExtend (the target
// signedness decides when the input sign is None).
TEST(Conv, ConvI8VersusU8FromI4) {
    Conv ci8 = MakeConv(KnownTypeCode::Int32, PrimitiveType::I8, false, Sign::None);
    Conv cu8 = MakeConv(KnownTypeCode::Int32, PrimitiveType::U8, false, Sign::None);
    EXPECT_EQ(ci8.Kind, ConversionKind::SignExtend);
    EXPECT_EQ(cu8.Kind, ConversionKind::ZeroExtend);
    EXPECT_EQ(ci8.ResultType(), StackType::I8);
    EXPECT_EQ(cu8.ResultType(), StackType::I8);
}

// conv.r4 / conv.r8 / conv.r.un from I4 are IntToFloat (the int->float
// conversions); conv.r.un targets PrimitiveType::R (the unspecified-size float
// the conv.r.un + conv.r[48] combining logic consults).
TEST(Conv, ConvR4R8RUnFromI4AreIntToFloat) {
    Conv cr4 = MakeConv(KnownTypeCode::Int32, PrimitiveType::R4, false, Sign::Signed);
    Conv cr8 = MakeConv(KnownTypeCode::Int32, PrimitiveType::R8, false, Sign::Signed);
    Conv crun = MakeConv(KnownTypeCode::Int32, PrimitiveType::R, false, Sign::Unsigned);
    EXPECT_EQ(cr4.Kind, ConversionKind::IntToFloat);
    EXPECT_EQ(cr8.Kind, ConversionKind::IntToFloat);
    EXPECT_EQ(crun.Kind, ConversionKind::IntToFloat);
    EXPECT_EQ(crun.TargetType, PrimitiveType::R);
    EXPECT_EQ(crun.InputSign, Sign::Unsigned);
    EXPECT_EQ(cr4.ResultType(), StackType::F4);
    EXPECT_EQ(cr8.ResultType(), StackType::F8);
    EXPECT_EQ(crun.ResultType(), StackType::F8);
}

// conv.r4 from F8 is a FloatPrecisionChange (double -> float); the input is
// already float, so needsSign is false and InputSign is None (the reader's passed
// Signed sign is dropped).
TEST(Conv, ConvR4FromF8IsFloatPrecisionChange) {
    Conv c = MakeConv(KnownTypeCode::Double, PrimitiveType::R4, false, Sign::Signed);
    EXPECT_EQ(c.Kind, ConversionKind::FloatPrecisionChange);
    EXPECT_EQ(c.InputType, StackType::F8);
    EXPECT_EQ(c.InputSign, Sign::None) << "float->float convs do not need a sign";
    EXPECT_EQ(c.ResultType(), StackType::F4);
}

// conv.r4 from F4 is a Nop (same float size).
TEST(Conv, ConvR4FromF4IsNop) {
    Conv c = MakeConv(KnownTypeCode::Single, PrimitiveType::R4, false, Sign::Signed);
    EXPECT_EQ(c.Kind, ConversionKind::Nop);
}

// A Conv with CheckForOverflow carries the MayThrow flag (it may raise an
// OverflowException); a plain conv does not.
TEST(Conv, CheckForOverflowSetsMayThrowFlag) {
    Conv ovf = MakeConv(KnownTypeCode::Int32, PrimitiveType::I8, true, Sign::Signed);
    Conv plain = MakeConv(KnownTypeCode::Int32, PrimitiveType::I8, false, Sign::None);
    EXPECT_TRUE(HasFlag(ovf.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_FALSE(HasFlag(plain.DirectFlags(), InstructionFlags::MayThrow));
}

// UnwrapConv peels a chain of the requested kind: conv.i(ldc.i4 0) (SignExtend)
// unwraps to ldc.i4 0; conv.i4(ldc.i4 0) (Nop) does NOT unwrap to SignExtend.
TEST(Conv, UnwrapConvPeelsRequestedKind) {
    // conv.i(ldc.i4 0): SignExtend I4 -> I around the 0.
    Conv signExt(std::make_unique<LdcI4>(0), PrimitiveType::I, false, Sign::None);
    ASSERT_EQ(signExt.Kind, ConversionKind::SignExtend);
    const ILInstruction* unwrapped = UnwrapConvFree(&signExt, ConversionKind::SignExtend);
    ASSERT_NE(unwrapped, nullptr);
    EXPECT_EQ(unwrapped->Op, OpCode::LdcI4)
        << "UnwrapConv(SignExtend) must peel the conv.i to its ldc.i4 0 argument";

    // conv.i4(ldc.i4 0): Nop, not SignExtend -- must not unwrap.
    Conv nop(std::make_unique<LdcI4>(0), PrimitiveType::I4, false, Sign::None);
    ASSERT_EQ(nop.Kind, ConversionKind::Nop);
    EXPECT_EQ(UnwrapConvFree(&nop, ConversionKind::SignExtend), &nop)
        << "UnwrapConv(SignExtend) on a Nop conv returns the conv unchanged";

    // conv.i8(ldc.i4 0): SignExtend I4 -> I8 around the 0 -- also peels.
    Conv signExtI8(std::make_unique<LdcI4>(0), PrimitiveType::I8, false, Sign::None);
    ASSERT_EQ(signExtI8.Kind, ConversionKind::SignExtend);
    const ILInstruction* peeledI8 = UnwrapConvFree(&signExtI8, ConversionKind::SignExtend);
    ASSERT_NE(peeledI8, nullptr);
    EXPECT_EQ(peeledI8->Op, OpCode::LdcI4);
}

// The lifted Conv (the D98 nullable-lifting conv.nop.lifted model): a Conv with
// isLifted=true operates on a boxed Nullable<T> (Argument.ResultType == O, not
// InputType) and produces a boxed Nullable<T> result (ResultType == O). The 6-arg
// constructor takes InputType explicitly (the argument's ResultType is O when
// lifted, not InputType); the 4-arg non-lifted constructor derives InputType from
// the argument's ResultType. The conv.nop.lifted case constructs
// `conv.nop.lifted(ldloc v)` (a no-op I4->I4 conv, Kind Nop) for Nullable<bool>
// where the underlying Boolean != the I4-stacked Int32 the GVO returns.
TEST(Conv, LiftedConvHasResultTypeOAndUnderlyingResultType) {
    // `conv.nop.lifted(ldloc v)`: a no-op I4 -> I4 lifted conv over a Nullable<bool>
    // load (the argument is an LdLoc of a Nullable<bool> variable, ResultType O).
    std::vector<ITypePtr> nboolArgs = {KT(KnownTypeCode::Boolean)};
    auto v = Var("v", std::make_shared<ParameterizedType>(
        KT(KnownTypeCode::NullableOfT), std::move(nboolArgs)));
    Conv lifted(std::make_unique<LdLoc>(v), StackType::I4, Sign::Unsigned,
                PrimitiveType::I4, false, true);
    EXPECT_TRUE(lifted.IsLifted)
        << "the 6-arg constructor with isLifted=true sets IsLifted";
    EXPECT_EQ(lifted.ResultType(), StackType::O)
        << "a lifted conv produces a boxed Nullable<T> result (ResultType O)";
    EXPECT_EQ(lifted.UnderlyingResultType(), StackType::I4)
        << "the underlying (non-lifted) result is GetStackType(TargetType) = I4";
    EXPECT_EQ(lifted.InputType, StackType::I4)
        << "InputType is the explicitly-passed I4 (not the argument's O)";
    EXPECT_EQ(lifted.Kind, ConversionKind::Nop)
        << "I4 -> I4 with a non-float target is a Nop conversion";
    EXPECT_FALSE(lifted.CheckForOverflow)
        << "conv.nop.lifted is not overflow-checked";
    // The dump appends ".lifted" after the sign suffix.
    std::string out;
    lifted.WriteTo(out);
    EXPECT_NE(out.find(".lifted"), std::string::npos)
        << "the dump must annotate a lifted conv with .lifted: " << out;
}

// The non-lifted 4-arg constructor leaves IsLifted false (the default); ResultType
// is GetStackType(TargetType) (not O). Every IL-reader conv.* opcode is non-lifted.
TEST(Conv, NonLiftedConvStaysResultTypeGetStackType) {
    Conv c(std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
           PrimitiveType::I8, false, Sign::None);
    EXPECT_FALSE(c.IsLifted)
        << "the 4-arg constructor leaves IsLifted at its false default";
    EXPECT_EQ(c.ResultType(), StackType::I8)
        << "a non-lifted conv's ResultType is GetStackType(TargetType) = I8";
    EXPECT_EQ(c.UnderlyingResultType(), StackType::I8)
        << "UnderlyingResultType is also GetStackType(TargetType) for non-lifted";
    std::string out;
    c.WriteTo(out);
    EXPECT_EQ(out.find(".lifted"), std::string::npos)
        << "a non-lifted conv must not annotate .lifted: " << out;
}
