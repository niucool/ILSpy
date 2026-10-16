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
// OTHERWISE, ARISING FROM, CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the BinaryNumericInstruction faithful Sign / LeftInputType /
// RightInputType model (the BNI Sign/input-type reconciliation D125/D126
// flagged as the contained model change the full TransformAssignment
// compound-assignment transform needs): the per-opcode Sign the IL reader derives
// (None for the plain add/sub/mul/and/or/xor/shl, Signed for div/rem/shr and the
// _ovf forms, Unsigned for the _un forms), the input types derived from the
// operands' ResultType, the ComputeResultType table (Ecma-335 Table 2/5/6/7),
// and the lifted form's explicit input types. The NumericCompoundAssign
// compound-assignment node copies these from a BinaryNumericInstruction, so the
// per-opcode Sign is the contract that transform relies on.

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
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

} // namespace

// ComputeResultType: Ecma-335 Table 2/5/6/7. Same-type operands yield the left
// input type (the common case for the reader's I4/I8 operands); a shift yields
// the left input type (Table 6); mixed Ref operands yield Ref (or I for
// sub(&,&)); otherwise Unknown.
TEST(BinaryNumericInstruction, ComputeResultType) {
    // Same-type operands.
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Add, StackType::I4, StackType::I4), StackType::I4);
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Add, StackType::I8, StackType::I8), StackType::I8);
    // Shift uses Table 6 (the result is the left input type) even for mismatched
    // operand types (shl takes I4 left, I4 right; the right is the shift count).
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::ShiftLeft, StackType::I4, StackType::I4), StackType::I4);
    // sub(&, &) returns Ref, not I: the C# ComputeResultType's first guard
    // (`left == right`) short-circuits and returns `left` for same-type operands,
    // so the inner `sub(&,&) = I` branch is dead code (only reachable when
    // `left != right`, but its own guard requires both to be Ref). Faithful
    // behavior is Ref (the same-type short-circuit).
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Sub, StackType::Ref, StackType::Ref), StackType::Ref);
    // add/sub with I or I4 and & = & (pointer offset).
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Add, StackType::Ref, StackType::I4), StackType::Ref);
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Add, StackType::I, StackType::Ref), StackType::Ref);
    // Mismatched non-Ref integer types (should not arise from the reader, which
    // widens via Conv) collapse to Unknown.
    EXPECT_EQ(BinaryNumericInstruction::ComputeResultType(
        BinaryNumericOperator::Add, StackType::I4, StackType::I8), StackType::Unknown);
}

// The faithful 5-arg constructor (the form the IL reader uses): Sign is set
// faithfully, LeftInputType/RightInputType are derived from the operands'
// ResultType, and ResultStackType is ComputeResultType(op, LeftInputType,
// RightInputType). The legacy Signed bool is derived (true for None/Signed,
// false for Unsigned).
TEST(BinaryNumericInstruction, FaithfulConstructorDerivesSignAndInputTypes) {
    // add(ldloc int, ldc.i4 1): Sign.None, I4 inputs, I4 result.
    auto add = BinaryNumericInstruction(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add,
        /*checkForOverflow=*/false, Sign::None);
    EXPECT_EQ(add.Sign, Sign::None);
    EXPECT_EQ(add.LeftInputType, StackType::I4);
    EXPECT_EQ(add.RightInputType, StackType::I4);
    EXPECT_EQ(add.ResultType(), StackType::I4);
    EXPECT_EQ(add.UnderlyingResultType(), StackType::I4);
    EXPECT_TRUE(add.Signed) << "Sign.None -> Signed bool true (no .un suffix)";
    EXPECT_FALSE(add.CheckForOverflow);
    EXPECT_FALSE(add.IsLifted);
    add.CheckInvariant(ILPhase::Normal);

    // div(ldloc int, ldc.i4 1): Sign.Signed (div is a signed op), I4 result.
    auto div = BinaryNumericInstruction(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Div,
        /*checkForOverflow=*/false, Sign::Signed);
    EXPECT_EQ(div.Sign, Sign::Signed);
    EXPECT_EQ(div.ResultType(), StackType::I4);
    EXPECT_TRUE(div.Signed);

    // div_un(ldloc int, ldc.i4 1): Sign.Unsigned, Signed bool false (the .un form).
    auto divUn = BinaryNumericInstruction(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Div,
        /*checkForOverflow=*/false, Sign::Unsigned);
    EXPECT_EQ(divUn.Sign, Sign::Unsigned);
    EXPECT_FALSE(divUn.Signed) << "Sign.Unsigned -> Signed bool false (.un suffix)";

    // add.ovf(ldloc int, ldc.i4 1): Sign.Signed, CheckForOverflow true.
    auto addOvf = BinaryNumericInstruction(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int32))),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add,
        /*checkForOverflow=*/true, Sign::Signed);
    EXPECT_EQ(addOvf.Sign, Sign::Signed);
    EXPECT_TRUE(addOvf.CheckForOverflow);

    // I8 operands -> I8 result (ComputeResultType same-type).
    auto addI8 = BinaryNumericInstruction(
        std::make_unique<LdLoc>(Var("v", KT(KnownTypeCode::Int64))),
        std::make_unique<LdcI8>(1), BinaryNumericOperator::Add,
        /*checkForOverflow=*/false, Sign::None);
    EXPECT_EQ(addI8.LeftInputType, StackType::I8);
    EXPECT_EQ(addI8.RightInputType, StackType::I8);
    EXPECT_EQ(addI8.ResultType(), StackType::I8);
}

// The legacy 4-arg constructor (backward-compatible form the pre-reconciliation
// tests use with an explicit result stack type): Sign defaults None, input types
// are derived from the operands' ResultType, CheckForOverflow defaults false.
TEST(BinaryNumericInstruction, LegacyConstructorDefaultsSignAndDerivesInputTypes) {
    BinaryNumericInstruction add(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
                                  BinaryNumericOperator::Add, StackType::I4);
    EXPECT_EQ(add.Sign, Sign::None);
    EXPECT_EQ(add.LeftInputType, StackType::I4);
    EXPECT_EQ(add.RightInputType, StackType::I4);
    EXPECT_EQ(add.ResultType(), StackType::I4);
    EXPECT_TRUE(add.Signed) << "Sign.None -> Signed bool true";
    EXPECT_FALSE(add.CheckForOverflow);
    EXPECT_FALSE(add.IsLifted);
    add.CheckInvariant(ILPhase::Normal);
}

// The lifted 8-arg constructor (the NullableLifting DoLift form): the input types
// are taken explicitly (the lifted operands have ResultType O, but the input types
// are the underlying Nullable<T> types); ResultType is O, UnderlyingResultType is
// ComputeResultType(op, input types).
TEST(BinaryNumericInstruction, LiftedConstructorTakesInputTypesExplicitly) {
    // A lifted add over Nullable<int>: the lifted operands are ldloc v (ResultType
    // O for a Nullable<int> local) + ldc.i4 5, but the input types are the
    // underlying int (I4). ResultType O, UnderlyingResultType I4.
    auto v = Var("v", KT(KnownTypeCode::Int32));
    auto lifted = BinaryNumericInstruction(
        std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(5),
        BinaryNumericOperator::Add,
        StackType::I4, StackType::I4,
        /*checkForOverflow=*/false, Sign::Signed, /*isLifted=*/true);
    EXPECT_TRUE(lifted.IsLifted);
    EXPECT_EQ(lifted.ResultType(), StackType::O);
    EXPECT_EQ(lifted.UnderlyingResultType(), StackType::I4);
    EXPECT_EQ(lifted.LeftInputType, StackType::I4);
    EXPECT_EQ(lifted.RightInputType, StackType::I4);
    EXPECT_EQ(lifted.Sign, Sign::Signed);
    EXPECT_TRUE(lifted.Signed);
    lifted.CheckInvariant(ILPhase::Normal);
    EXPECT_NE(lifted.ToString().find("binary.add.lifted"), std::string::npos);
}

// The dump suffixes: .un for Sign.Unsigned (via the Signed bool), .ovf for
// CheckForOverflow, .lifted for IsLifted. Faithful to the C# WriteToCore's
// .unsigned/.ovf/.lifted suffixes (this port renders .un for the .unsigned case,
// matching the pre-reconciliation dump).
TEST(BinaryNumericInstruction, DumpSuffixes) {
    auto add = BinaryNumericInstruction(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::Add, false, Sign::None);
    EXPECT_NE(add.ToString().find("binary.add("), std::string::npos) << add.ToString();
    EXPECT_EQ(add.ToString().find(".un"), std::string::npos) << add.ToString();
    EXPECT_EQ(add.ToString().find(".ovf"), std::string::npos) << add.ToString();

    auto divUn = BinaryNumericInstruction(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::Div, false, Sign::Unsigned);
    EXPECT_NE(divUn.ToString().find("binary.div.un("), std::string::npos) << divUn.ToString();

    auto addOvf = BinaryNumericInstruction(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::Add, true, Sign::Signed);
    EXPECT_NE(addOvf.ToString().find("binary.add.ovf("), std::string::npos) << addOvf.ToString();

    auto addOvfUn = BinaryNumericInstruction(
        std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
        BinaryNumericOperator::Add, true, Sign::Unsigned);
    EXPECT_NE(addOvfUn.ToString().find("binary.add.ovf.un("), std::string::npos)
        << addOvfUn.ToString();
}
