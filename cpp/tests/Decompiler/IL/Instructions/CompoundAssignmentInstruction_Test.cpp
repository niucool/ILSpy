// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
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

// Tests for the NumericCompoundAssign ILAst node (the C# compound-assignment
// `op=` family) -- a tested-but-not-yet-wired foundation unblocking the next
// in-order per-statement child of StatementTransform, TransformAssignment.
// HandleCompoundAssign (which folds `stloc V(binary.op(ldloc V, rhs))` into a
// NumericCompoundAssign `V op= rhs`). No pipeline transform constructs these
// nodes yet (the MatchInstruction / UsingInstruction / NullCoalescingInstruction
// / ThreeValuedBoolAnd/Or precedent); the tests cover the node
// invariant/flags/ResultType/dump, the dump variants (operator/ovf/signed/
// unsigned/type/lifted/suffix), IsLifted/UnderlyingResultType, the two-child
// tree (Target/Value slots) + re-parenting, the SideEffect/MayThrow flag
// propagation, the ILAstToCSharp seed rendering of `target op= value` and the
// post-increment `target++`, the MakeAssignmentExpressions /
// IntroduceIncrementAndDecrement setting defaults, and a mscorlib sweep that
// constructs the nodes over real BinaryNumericInstruction operands.

#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::Sign;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnknownType;

namespace {

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

ILVariablePtr MakeLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, type, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build a standalone ILFunction whose body root block holds a
// NumericCompoundAssign as a non-terminal statement (it is not control flow,
// so it cannot be the block final) followed by a `return;` Leave, so the
// seed-rendering test exercises a realistic statement-level compound assign.
std::unique_ptr<ILFunction> MakeCompoundFn(std::unique_ptr<NumericCompoundAssign> node) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto root = std::make_unique<Block>();
    root->Add(std::move(node));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// ---- Node invariant, flags, ResultType, dump ----

TEST(CompoundAssignmentInstruction, NumericCompoundAssignInvariantFlagsAndDump) {
    auto num = MakeLocal("num", Int32());
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    EXPECT_EQ(node->Op, OpCode::NumericCompoundAssign);
    EXPECT_EQ(node->ResultType(), StackType::I4);
    // DirectFlags is SideEffect (Add is not div/rem/ovf, so no MayThrow).
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_FALSE(HasFlag(node->DirectFlags(), InstructionFlags::MayThrow));
    // The dump renders the faithful ILAst mnemonic
    // `compound.assign.add.i4.address.new(target, value)`.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("compound.assign.add."), std::string::npos) << dump;
    EXPECT_NE(dump.find(".i4.address.new("), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloca"), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldc.i4"), std::string::npos) << dump;
}

// ---- The dump variants: operator, .ovf, .signed/.unsigned, type, .lifted, suffix ----

TEST(CompoundAssignmentInstruction, DumpRendersOvfAndSignedUnsigned) {
    auto num = MakeLocal("num", Int32());
    // conv.ovf.u (a checked, unsigned multiply) -> `.ovf` + `.unsigned`.
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Mul, true, Sign::Unsigned,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(2));
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("compound.assign.mul.ovf.unsigned"), std::string::npos) << dump;
    // A checked mul carries MayThrow (CheckForOverflow).
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::MayThrow));
}

TEST(CompoundAssignmentInstruction, DumpRendersSignedAndLifted) {
    auto num = MakeLocal("num", Int32());
    // A signed, lifted subtract -> `.signed` + `.lifted`; ResultType is O (a
    // boxed Nullable<T> for a lifted compound assign).
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Sub, false, Sign::Signed,
        StackType::I4, StackType::I4, StackType::I4, true, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    EXPECT_TRUE(node->IsLifted);
    EXPECT_EQ(node->ResultType(), StackType::O);
    EXPECT_EQ(node->UnderlyingResultType(), StackType::I4);
    std::string dump = node->ToString();
    EXPECT_NE(dump.find(".signed."), std::string::npos) << dump;
    EXPECT_NE(dump.find(".lifted."), std::string::npos) << dump;
}

TEST(CompoundAssignmentInstruction, DumpRendersEvaluatesToOldValueSuffix) {
    auto num = MakeLocal("num", Int32());
    // A post-increment (EvaluatesToOldValue) renders the `.old` suffix.
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    std::string dump = node->ToString();
    EXPECT_NE(dump.find(".address.old"), std::string::npos) << dump;
}

TEST(CompoundAssignmentInstruction, DivAndRemCarryMayThrow) {
    auto num = MakeLocal("num", Int32());
    // Div / Rem carry MayThrow even without CheckForOverflow (faithful to the
    // C# ComputeFlags/DirectFlags).
    auto div = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Div, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(2));
    EXPECT_TRUE(HasFlag(div->DirectFlags(), InstructionFlags::MayThrow));
    auto rem = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Rem, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(2));
    EXPECT_TRUE(HasFlag(rem->DirectFlags(), InstructionFlags::MayThrow));
    // A plain Add does not.
    auto add = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(2));
    EXPECT_FALSE(HasFlag(add->DirectFlags(), InstructionFlags::MayThrow));
}

// ---- Children in typed slots (Target/Value) + re-parenting ----

TEST(CompoundAssignmentInstruction, ChildrenAreInTypedSlots) {
    auto num = MakeLocal("num", Int32());
    auto target = std::make_unique<LdLoca>(num);
    auto value = std::make_unique<LdcI4>(1);
    ILInstruction* targetPtr = target.get();
    ILInstruction* valuePtr = value.get();
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::move(target), CompoundTargetKind::Address, std::move(value));
    node->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(node->ChildCount(), 2);
    EXPECT_EQ(node->GetChild(0), targetPtr);
    EXPECT_EQ(node->GetChild(1), valuePtr);
    EXPECT_EQ(targetPtr->Parent, node.get());
    EXPECT_EQ(targetPtr->ChildIndex, 0);
    EXPECT_EQ(valuePtr->Parent, node.get());
    EXPECT_EQ(valuePtr->ChildIndex, 1);

    // SetChild re-parents: swapping the Value slot replaces the old occupant.
    auto newVal = std::make_unique<LdcI4>(2);
    ILInstruction* newValPtr = newVal.get();
    node->SetChild(1, std::move(newVal));
    EXPECT_EQ(node->GetChild(1), newValPtr);
    EXPECT_EQ(newValPtr->Parent, node.get());
    EXPECT_EQ(newValPtr->ChildIndex, 1);
    EXPECT_NE(node->GetChild(1), valuePtr);
    node->CheckInvariant(ILPhase::Normal);
}

// ---- Flags propagation: Target | Value | SideEffect ----

TEST(CompoundAssignmentInstruction, FlagsPropagateFromChildren) {
    auto num = MakeLocal("num", Int32());
    // A node over a Target that carries MayReadLocals (LdLoca) and a Value
    // (LdcI4) should carry SideEffect (the compound assign stores) and the
    // operands' read flags.
    auto node = std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::SideEffect));
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::MayThrow));
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
}

// ---- Seed rendering ----

// The seed renders a NumericCompoundAssign statement as `target op= value;`
// (the C# compound-assignment statement). The Address Target (an LdLoca)
// renders as the bare variable name (the back end loads the value at the
// address).
TEST(CompoundAssignmentInstruction, SeedRendersCompoundAssignment) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeCompoundFn(std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num += 1;"), std::string::npos) << text;
}

// A post-increment (Add, EvaluatesToOldValue, ldc.i4 1) renders as `num++;`.
TEST(CompoundAssignmentInstruction, SeedRendersPostIncrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeCompoundFn(std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Add, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num++;"), std::string::npos) << text;
}

// A post-decrement (Sub, EvaluatesToOldValue, ldc.i4 1) renders as `num--;`.
TEST(CompoundAssignmentInstruction, SeedRendersPostDecrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeCompoundFn(std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::Sub, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num--;"), std::string::npos) << text;
}

// The other operators render with their compound-assignment operator.
TEST(CompoundAssignmentInstruction, SeedRendersBitAndOperator) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeCompoundFn(std::make_unique<NumericCompoundAssign>(
        BinaryNumericOperator::BitAnd, false, Sign::None,
        StackType::I4, StackType::I4, StackType::I4, false, Int32(),
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num &= 1;"), std::string::npos) << text;
}

// ---- Setting defaults ----

TEST(CompoundAssignmentInstruction, AssignmentSettingsDefaultTrue) {
    ILTransformSettings settings;
    // Both gate TransformAssignment.HandleCompoundAssign and default true
    // (DecompilerSettings.MakeAssignmentExpressions /
    // IntroduceIncrementAndDecrement).
    EXPECT_TRUE(settings.MakeAssignmentExpressions);
    EXPECT_TRUE(settings.IntroduceIncrementAndDecrement);
    // NativeIntegers (C# 9.0) and UnsignedRightShift (C# 11.0) also default
    // true; NumericCompoundAssign.IsBinaryCompatibleWithType consults both.
    EXPECT_TRUE(settings.NativeIntegers);
    EXPECT_TRUE(settings.UnsignedRightShift);
    // CheckedOperators (C# 11.0) also defaults true; the
    // UserDefinedCompoundAssign.IsIncrementOrDecrement helper consults it for
    // the op_CheckedIncrement/op_CheckedDecrement variants.
    EXPECT_TRUE(settings.CheckedOperators);
}

// -----------------------------------------------------------------------------
// IsBinaryCompatibleWithType (the D129 validator). The C# static gate on
// NumericCompoundAssign that the future TransformAssignment.HandleCompoundAssign
// consults before building a NumericCompoundAssign from a
// `stloc V(binary.op(ldloc V, rhs))` pattern. Ports the IsLifted / Unknown /
// Enum / Pointer (deferred-conservative) / IntPtr-UIntPtr / Sign /
// IsImplicitTruncation gates faithfully; the Pointer case's
// PointerArithmeticOffset.Detect is a substantial deferred slice, so pointer
// types are rejected conservatively (no pointer compound assignment confirmed).
// -----------------------------------------------------------------------------

namespace {

// A fresh ILTransformSettings for a test (the defaults -- NativeIntegers on,
// UnsignedRightShift on).
std::unique_ptr<ILTransformSettings> DefaultSettings() {
    return std::make_unique<ILTransformSettings>();
}

// A KnownType for the given code (Byte / IntPtr / UIntPtr / etc.).
ITypePtr KT(KnownTypeCode c) { return std::make_shared<KnownType>(c); }

// A non-lifted BinaryNumericInstruction with an Int32 LdLoc Left and the given
// Right, operator, and sign (the faithful 5-arg constructor). The validator
// does not consult LeftInputType for these gates, so an Int32 Left suffices.
std::unique_ptr<BinaryNumericInstruction> MakeBinary(BinaryNumericOperator op,
                                                       Sign sign,
                                                       bool checkForOverflow,
                                                       std::unique_ptr<ILInstruction> right) {
    auto left = std::make_unique<LdLoc>(MakeLocal("v", Int32()));
    return std::make_unique<BinaryNumericInstruction>(
        std::move(left), std::move(right), op, checkForOverflow, sign);
}

// A lifted BinaryNumericInstruction (IsLifted=true, ResultType O) with the
// given operator/sign and an Int32 underlying input type.
std::unique_ptr<BinaryNumericInstruction> MakeLiftedBinary(
    BinaryNumericOperator op, Sign sign, std::unique_ptr<ILInstruction> right) {
    auto left = std::make_unique<LdLoc>(MakeLocal("v", Int32()));
    return std::make_unique<BinaryNumericInstruction>(
        std::move(left), std::move(right), op,
        StackType::I4, StackType::I4, false, sign, true);
}

// A Nullable<T> as a generic instantiation: ParameterizedType(KnownType(NullableOfT), {T}).
ITypePtr MakeNullableOf(KnownTypeCode underlying) {
    std::vector<ITypePtr> args;
    args.push_back(std::make_shared<KnownType>(underlying));
    return std::make_shared<ParameterizedType>(
        std::make_shared<KnownType>(KnownTypeCode::NullableOfT), std::move(args));
}

// An enum type (Kind==Enum): a SimpleType with the Enum kind.
ITypePtr EnumType() {
    return std::make_shared<SimpleType>(TopLevelTypeName("", "E"),
                                        TypeKind::Enum);
}

// A pointer type (Kind==Pointer) over Int32.
ITypePtr PtrType() {
    return std::make_shared<PointerType>(Int32());
}

}  // namespace

// ---- IsLifted gate ----

// A lifted binary over a Nullable<int> store type passes: the IsLifted gate
// unwraps to the underlying Int32, which the remaining gates accept (Sign.None
// Add, Int32 not small integer, LdcI4(1) fits).
TEST(IsBinaryCompatibleWithTypeTest, LiftedBinaryOverNullableTypePasses) {
    auto b = MakeLiftedBinary(BinaryNumericOperator::Add, Sign::None,
                               std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), MakeNullableOf(KnownTypeCode::Int32).get(), settings.get()));
}

// A lifted binary over a non-Nullable store type fails: the IsLifted gate
// requires the store type to be Nullable<T>.
TEST(IsBinaryCompatibleWithTypeTest, LiftedBinaryOverNonNullableTypeFails) {
    auto b = MakeLiftedBinary(BinaryNumericOperator::Add, Sign::None,
                               std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), Int32().get(), settings.get()));
}

// ---- Unknown gate ----

// An Unknown store type is rejected (avoid introducing a potentially-incorrect
// compound assignment).
TEST(IsBinaryCompatibleWithTypeTest, UnknownTypeFails) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), UnknownType().get(), settings.get()));
}

// ---- Enum gate ----

// Add/Sub/BitAnd/BitOr/BitXor are supported on enum types (the operator switch
// breaks out and the remaining gates pass -- an Enum is not a C# small integer,
// GetSign(Enum) is None so a Sign.None Add skips the Sign gate).
TEST(IsBinaryCompatibleWithTypeTest, EnumAllowsAddSubBitOps) {
    auto settings = DefaultSettings();
    for (auto op : { BinaryNumericOperator::Add, BinaryNumericOperator::Sub,
                     BinaryNumericOperator::BitAnd, BinaryNumericOperator::BitOr,
                     BinaryNumericOperator::BitXor }) {
        auto b = MakeBinary(op, Sign::None, false, std::make_unique<LdcI4>(1));
        EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
            b.get(), EnumType().get(), settings.get()))
            << "operator " << static_cast<int>(op);
    }
}

// Div/Rem/Mul/ShiftLeft/ShiftRight are NOT supported on enum types.
TEST(IsBinaryCompatibleWithTypeTest, EnumRejectsOtherOperators) {
    auto settings = DefaultSettings();
    for (auto op : { BinaryNumericOperator::Div, BinaryNumericOperator::Rem,
                     BinaryNumericOperator::Mul,
                     BinaryNumericOperator::ShiftLeft,
                     BinaryNumericOperator::ShiftRight }) {
        auto b = MakeBinary(op, Sign::None, false, std::make_unique<LdcI4>(1));
        EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
            b.get(), EnumType().get(), settings.get()))
            << "operator " << static_cast<int>(op);
    }
}

// ---- Pointer gate (deferred-conservative) ----

// Pointer compound assignment is rejected conservatively (the C# consults
// PointerArithmeticOffset.Detect, not yet ported). Even Add/Sub -- the only
// operators the C# considers -- are rejected until the Detect helper lands.
TEST(IsBinaryCompatibleWithTypeTest, PointerRejectedConservatively) {
    auto settings = DefaultSettings();
    for (auto op : { BinaryNumericOperator::Add, BinaryNumericOperator::Sub,
                     BinaryNumericOperator::Mul }) {
        auto b = MakeBinary(op, Sign::None, false, std::make_unique<LdcI4>(1));
        EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
            b.get(), PtrType().get(), settings.get()))
            << "operator " << static_cast<int>(op);
    }
}

// ---- IntPtr/UIntPtr gate ----

// A System.IntPtr LHS (Kind==Struct, not NInt) with NativeIntegers on and a
// non-shift operator passes (the IntPtr gate does not reject; Add is not a
// shift; Sign.None Add skips the Sign gate; IntPtr is not a small integer).
TEST(IsBinaryCompatibleWithTypeTest, IntPtrWithNativeIntegersPasses) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::IntPtr).get(), settings.get()));
}

// A System.IntPtr LHS with NativeIntegers OFF fails (the trick of casting the
// RHS to n(u)int requires native integers to be available).
TEST(IsBinaryCompatibleWithTypeTest, IntPtrWithoutNativeIntegersFails) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    settings->NativeIntegers = false;
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::IntPtr).get(), settings.get()));
}

// Shifts on a System.IntPtr LHS fail even with NativeIntegers on (casting the
// RHS to n(u)int does not work for shifts).
TEST(IsBinaryCompatibleWithTypeTest, IntPtrShiftFails) {
    auto settings = DefaultSettings();
    for (auto op : { BinaryNumericOperator::ShiftLeft,
                     BinaryNumericOperator::ShiftRight }) {
        auto b = MakeBinary(op, Sign::None, false, std::make_unique<LdcI4>(1));
        EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
            b.get(), KT(KnownTypeCode::IntPtr).get(), settings.get()))
            << "operator " << static_cast<int>(op);
    }
}

// A System.UIntPtr LHS behaves the same as IntPtr (the gate covers both).
TEST(IsBinaryCompatibleWithTypeTest, UIntPtrWithNativeIntegersPasses) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::UIntPtr).get(), settings.get()));
}

// A native-int nint (Kind==NInt) is NOT caught by the IntPtr gate (Kind is
// NInt, not Struct-KnownType-IntPtr), so a plain Add passes.
TEST(IsBinaryCompatibleWithTypeTest, NIntBypassesIntPtrGate) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    ITypePtr nintType = std::make_shared<SpecialType>(TypeKind::NInt);
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), nintType.get(), settings.get()));
}

// ---- Sign gate ----

// A signed binary over a signed type (Int32) passes the Sign gate.
TEST(IsBinaryCompatibleWithTypeTest, SignedBinaryOverSignedTypePasses) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Signed, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), Int32().get(), settings.get()));
}

// An unsigned binary over a signed type (Int32) fails the Sign gate (the type's
// sign does not match and Add is not a right shift, so signMismatchAllowed is
// false).
TEST(IsBinaryCompatibleWithTypeTest, UnsignedBinaryOverSignedTypeFails) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Unsigned, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), Int32().get(), settings.get()));
}

// An unsigned right shift over a signed type passes when UnsignedRightShift is
// on (the C# 11 `>>>` operator allows the sign mismatch).
TEST(IsBinaryCompatibleWithTypeTest, UnsignedRightShiftOverSignedTypePasses) {
    auto b = MakeBinary(BinaryNumericOperator::ShiftRight, Sign::Unsigned, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), Int32().get(), settings.get()));
}

// An unsigned right shift over a signed type fails when UnsignedRightShift is
// off (no `>>>` operator available, so the sign mismatch is not allowed).
TEST(IsBinaryCompatibleWithTypeTest, UnsignedRightShiftFailsWhenSettingOff) {
    auto b = MakeBinary(BinaryNumericOperator::ShiftRight, Sign::Unsigned, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    settings->UnsignedRightShift = false;
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), Int32().get(), settings.get()));
}

// A C# small integer (Byte) requires the binary to be signed (C# numeric-
// promotes a small integer to int); an unsigned Add fails.
TEST(IsBinaryCompatibleWithTypeTest, UnsignedBinaryOverCSharpSmallIntegerFails) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Unsigned, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::Byte).get(), settings.get()));
}

// A signed binary over a C# small integer (Byte) passes the Sign gate (the
// binary is signed, matching the int promotion) and the IsImplicitTruncation
// gate (LdcI4(1) fits a Byte).
TEST(IsBinaryCompatibleWithTypeTest, SignedBinaryOverCSharpSmallIntegerPasses) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Signed, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::Byte).get(), settings.get()));
}

// ---- IsImplicitTruncation gate ----

// An RHS that would be truncated for the LHS type fails: a signed Add over a
// Byte with LdcI4(100000) -- the Sign gate passes (Signed matches the int
// promotion), but IsImplicitTruncation reports ValueChanged (100000 > 255).
TEST(IsBinaryCompatibleWithTypeTest, TruncatingRhsFails) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Signed, false,
                        std::make_unique<LdcI4>(100000));
    auto settings = DefaultSettings();
    EXPECT_FALSE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::Byte).get(), settings.get()));
}

// An RHS that fits the LHS type passes: a signed Add over a Byte with
// LdcI4(1) -- both the Sign gate and IsImplicitTruncation pass.
TEST(IsBinaryCompatibleWithTypeTest, FittingRhsPasses) {
    auto b = MakeBinary(BinaryNumericOperator::Add, Sign::Signed, false,
                        std::make_unique<LdcI4>(1));
    auto settings = DefaultSettings();
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b.get(), KT(KnownTypeCode::Byte).get(), settings.get()));
}

// ---- settings=null (the Debug.Assert form) ----

// A null settings pointer is treated as the defaults (NativeIntegers /
// UnsignedRightShift permissive), matching the C# constructor Debug.Assert call.
TEST(IsBinaryCompatibleWithTypeTest, NullSettingsTreatedAsDefaults) {
    // IntPtr + Add passes with null settings (NativeIntegers permissive).
    auto b1 = MakeBinary(BinaryNumericOperator::Add, Sign::None, false,
                         std::make_unique<LdcI4>(1));
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b1.get(), KT(KnownTypeCode::IntPtr).get(), nullptr));
    // Unsigned right shift over Int32 passes with null settings
    // (UnsignedRightShift permissive).
    auto b2 = MakeBinary(BinaryNumericOperator::ShiftRight, Sign::Unsigned, false,
                         std::make_unique<LdcI4>(1));
    EXPECT_TRUE(NumericCompoundAssign::IsBinaryCompatibleWithType(
        b2.get(), Int32().get(), nullptr));
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods, find real BinaryNumericInstruction
// operands, and construct a NumericCompoundAssign over each (approximating the
// fields the future TransformAssignment.HandleCompoundAssign will copy from
// the binary), asserting the node invariant holds and the dump renders. This
// exercises the node on thousands of real binary operations (the volume the
// transform will see once wired).
TEST(CompoundAssignmentInstruction, MscorlibConstructFromRealBinaryOpsSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int constructed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Find the first BinaryNumericInstruction with a pure LdLoca target
        // variable (the compound-assign target) and an integer RHS.
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (constructed > 0) return;
            if (!inst || inst->Op != OpCode::BinaryNumericInstruction) return;
            auto* bni = static_cast<BinaryNumericInstruction*>(inst);
            // The compound-assign target is the binary's Left (a load of a
            // single-def local -- LdLoc for a local, LdLoca for a field/ref);
            // the RHS is the binary's Right. Approximate Sign from the binary's
            // Signed bool + operator (None for sign-independent ops, else
            // Signed/Unsigned) -- the BNI Sign reconciliation is deferred to
            // the transform.
            ILVariablePtr targetVar;
            if (bni->Left && bni->Left->Op == OpCode::LdLoc) {
                targetVar = static_cast<LdLoc*>(bni->Left.get())->Variable;
            } else if (bni->Left && bni->Left->Op == OpCode::LdLoca) {
                targetVar = static_cast<LdLoca*>(bni->Left.get())->Variable;
            }
            if (!targetVar) return;
            Sign sign = Sign::None;
            if (bni->Operator == BinaryNumericOperator::Add ||
                bni->Operator == BinaryNumericOperator::Sub ||
                bni->Operator == BinaryNumericOperator::Mul ||
                bni->Operator == BinaryNumericOperator::Div ||
                bni->Operator == BinaryNumericOperator::Rem ||
                bni->Operator == BinaryNumericOperator::ShiftRight) {
                sign = bni->Signed ? Sign::Signed : Sign::Unsigned;
            }
            auto node = std::make_unique<NumericCompoundAssign>(
                bni->Operator, bni->CheckForOverflow, sign,
                bni->ResultType(), bni->ResultType(), bni->UnderlyingResultType(),
                bni->IsLifted, targetVar->Type,
                CompoundEvalMode::EvaluatesToNewValue,
                std::make_unique<LdLoca>(targetVar), CompoundTargetKind::Address,
                std::make_unique<LdcI4>(1));
            node->CheckInvariant(ILPhase::Normal);
            std::string dump = node->ToString();
            EXPECT_NE(dump.find("compound.assign."), std::string::npos) << dump;
            EXPECT_EQ(node->ResultType(), bni->IsLifted ? StackType::O : bni->UnderlyingResultType());
            ++constructed;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}

// A mscorlib sweep for the IsBinaryCompatibleWithType validator: walk real
// BinaryNumericInstructions whose Left is an LdLoc, take the LdLoc's
// Variable->Type as the store type (the compound-assign target's type), and
// call the validator -- exercising every gate on thousands of real binary
// operations + their resolved variable types. Confirms the validator returns a
// bool without crashing and the compatible count is non-negative (the validator
// is not wired into the pipeline yet, so this is a foundation-level exercise).
TEST(IsBinaryCompatibleWithTypeTest, MscorlibValidatorSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto settings = DefaultSettings();
    int processed = 0;
    int evaluated = 0;
    int compatible = 0;
    int intPtrCompat = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst || inst->Op != OpCode::BinaryNumericInstruction) return;
            auto* bni = static_cast<BinaryNumericInstruction*>(inst);
            if (!bni->Left || bni->Left->Op != OpCode::LdLoc) return;
            auto* ld = static_cast<LdLoc*>(bni->Left.get());
            if (!ld->Variable || !ld->Variable->Type) return;
            const auto* storeType = ld->Variable->Type.get();
            bool result = NumericCompoundAssign::IsBinaryCompatibleWithType(
                bni, storeType, settings.get());
            ++evaluated;
            if (result) ++compatible;
            // Track the IntPtr gate specifically (a KnownType(IntPtr) store
            // type with NativeIntegers on) -- the gate the validator consults.
            if (result && storeType->Kind() == TypeKind::Struct) {
                if (const auto* k = dynamic_cast<const KnownType*>(storeType)) {
                    if (k->Code() == KnownTypeCode::IntPtr ||
                        k->Code() == KnownTypeCode::UIntPtr) {
                        ++intPtrCompat;
                    }
                }
            }
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(evaluated, 0);
    // The validator must not crash and must return a non-negative compatible
    // count (the legacy-csc corpus has many Int32 binary ops that pass all
    // gates, so compatible > 0).
    EXPECT_GE(compatible, 0);
    EXPECT_LE(compatible, evaluated);
}

// =============================================================================
// UserDefinedCompoundAssign (the C# user-defined-operator compound assignment
// node, the next foundation after NumericCompoundAssign). A tested-but-not-yet-
// wired foundation (the NumericCompoundAssign / MatchInstruction / UsingInstruction
// precedent): no pipeline transform constructs one yet, so `--csharp` output is
// unchanged. Unblocks the operator-call (op_Increment/op_Decrement) case of the
// TransformAssignment increment/decrement folds (D134's first deferred target).
// =============================================================================

namespace {

// Build a standalone ILFunction whose body root block holds a
// UserDefinedCompoundAssign as a non-terminal statement (it is not control
// flow, so it cannot be the block final) followed by a `return;` Leave, so the
// seed-rendering test exercises a realistic statement-level compound assign.
std::unique_ptr<ILFunction> MakeUserCompoundFn(std::unique_ptr<UserDefinedCompoundAssign> node) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto root = std::make_unique<Block>();
    root->Add(std::move(node));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

// A static operator Call named "Namespace.Type::op_Xxx" with the given
// declaring type and return stack type (the shape the inc/dec folds build a
// UserDefinedCompoundAssign from).
std::unique_ptr<Call> MakeOperatorCall(std::string methodName,
                                       ITypePtr declaringType,
                                       StackType returnType) {
    auto call = std::make_unique<Call>(std::move(methodName));
    call->IsOperator = true;
    call->IsInstanceCall = false;  // static
    call->DeclaringType = std::move(declaringType);
    call->ReturnType = returnType;
    return call;
}

}  // namespace

// ---- Node invariant, flags, ResultType, dump ----

TEST(UserDefinedCompoundAssign, InvariantFlagsAndDump) {
    auto num = MakeLocal("num", Int32());
    auto node = std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    EXPECT_EQ(node->Op, OpCode::UserDefinedCompoundAssign);
    // ResultType is the method's return StackType (Method.ReturnType.GetStackType()).
    EXPECT_EQ(node->ResultType(), StackType::I4);
    // DirectFlags is SideEffect | MayThrow (a user-defined operator call can
    // throw), faithful to the C# generated override.
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::MayThrow));
    // IsLifted is hardcoded false (faithful to the C# `public bool IsLifted => false`).
    EXPECT_FALSE(node->IsLifted);
    // The dump renders the family-consistent `compound.assign.userdefined`
    // root + the `.address`/`.old` suffix + the method + the operands.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("compound.assign.userdefined.address.old"), std::string::npos) << dump;
    EXPECT_NE(dump.find("System.SByte::op_Increment"), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloca"), std::string::npos) << dump;
}

TEST(UserDefinedCompoundAssign, DumpRendersPropertyAndNewValueSuffix) {
    auto num = MakeLocal("num", Int32());
    auto node = std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Property,
        std::make_unique<LdcI4>(1));
    std::string dump = node->ToString();
    EXPECT_NE(dump.find(".property.new"), std::string::npos) << dump;
}

// ---- Children in typed slots (Target/Value) + re-parenting ----

TEST(UserDefinedCompoundAssign, ChildrenAreInTypedSlots) {
    auto num = MakeLocal("num", Int32());
    auto target = std::make_unique<LdLoca>(num);
    auto value = std::make_unique<LdcI4>(1);
    ILInstruction* targetPtr = target.get();
    ILInstruction* valuePtr = value.get();
    auto node = std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::move(target), CompoundTargetKind::Address, std::move(value));
    node->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(node->ChildCount(), 2);
    EXPECT_EQ(node->GetChild(0), targetPtr);
    EXPECT_EQ(node->GetChild(1), valuePtr);
    EXPECT_EQ(targetPtr->Parent, node.get());
    EXPECT_EQ(targetPtr->ChildIndex, 0);
    EXPECT_EQ(valuePtr->Parent, node.get());
    EXPECT_EQ(valuePtr->ChildIndex, 1);

    // SetChild re-parents: swapping the Value slot replaces the old occupant.
    auto newVal = std::make_unique<LdcI4>(2);
    ILInstruction* newValPtr = newVal.get();
    node->SetChild(1, std::move(newVal));
    EXPECT_EQ(node->GetChild(1), newValPtr);
    EXPECT_EQ(newValPtr->Parent, node.get());
    EXPECT_EQ(newValPtr->ChildIndex, 1);
    EXPECT_NE(node->GetChild(1), valuePtr);
    node->CheckInvariant(ILPhase::Normal);
}

// ---- Flags propagation: Target | Value | SideEffect | MayThrow ----

TEST(UserDefinedCompoundAssign, FlagsPropagateFromChildren) {
    auto num = MakeLocal("num", Int32());
    auto node = std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::MayThrow));
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
}

// ---- IsIncrementOrDecrement helper ----

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRecognizesOpIncrement) {
    auto call = MakeOperatorCall("System.SByte::op_Increment", Int32(), StackType::I4);
    auto settings = DefaultSettings();
    EXPECT_TRUE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRecognizesOpDecrement) {
    auto call = MakeOperatorCall("System.SByte::op_Decrement", Int32(), StackType::I4);
    auto settings = DefaultSettings();
    EXPECT_TRUE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRecognizesCheckedWhenSettingOn) {
    auto call = MakeOperatorCall("System.SByte::op_CheckedIncrement", Int32(), StackType::I4);
    auto settings = DefaultSettings();
    settings->CheckedOperators = true;
    EXPECT_TRUE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRejectsCheckedWhenSettingOff) {
    auto call = MakeOperatorCall("System.SByte::op_CheckedIncrement", Int32(), StackType::I4);
    auto settings = DefaultSettings();
    settings->CheckedOperators = false;
    EXPECT_FALSE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementNullSettingsPermissiveForChecked) {
    // A null settings pointer is permissive for the checked variants (the C#
    // `settings?.CheckedOperators ?? true`).
    auto call = MakeOperatorCall("System.SByte::op_CheckedDecrement", Int32(), StackType::I4);
    EXPECT_TRUE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), nullptr));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRejectsNonOperator) {
    auto call = MakeOperatorCall("System.SByte::op_Increment", Int32(), StackType::I4);
    call->IsOperator = false;
    auto settings = DefaultSettings();
    EXPECT_FALSE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRejectsInstanceCall) {
    auto call = MakeOperatorCall("System.SByte::op_Increment", Int32(), StackType::I4);
    call->IsInstanceCall = true;  // not static
    auto settings = DefaultSettings();
    EXPECT_FALSE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRejectsOpAddition) {
    auto call = MakeOperatorCall("System.SByte::op_Addition", Int32(), StackType::I4);
    auto settings = DefaultSettings();
    EXPECT_FALSE(UserDefinedCompoundAssign::IsIncrementOrDecrement(call.get(), settings.get()));
}

TEST(UserDefinedCompoundAssign, IsIncrementOrDecrementRejectsNullCall) {
    auto settings = DefaultSettings();
    EXPECT_FALSE(UserDefinedCompoundAssign::IsIncrementOrDecrement(nullptr, settings.get()));
}

// ---- IsStringConcat helper ----

TEST(UserDefinedCompoundAssign, IsStringConcatRecognizesStringConcat) {
    auto call = MakeOperatorCall("System.String::Concat",
                                  std::make_shared<KnownType>(KnownTypeCode::String),
                                  StackType::O);
    // `Concat` is not an `op_*` name, so IsOperator would be false from the
    // reader's name check; but IsStringConcat does not consult IsOperator (the
    // C# checks Name + IsStatic + DeclaringType only), so set it false to mirror
    // a real Concat call.
    call->IsOperator = false;
    EXPECT_TRUE(UserDefinedCompoundAssign::IsStringConcat(call.get()));
}

TEST(UserDefinedCompoundAssign, IsStringConcatRejectsNonStringDeclaringType) {
    auto call = MakeOperatorCall("System.Foo::Concat", Int32(), StackType::O);
    call->IsOperator = false;
    EXPECT_FALSE(UserDefinedCompoundAssign::IsStringConcat(call.get()));
}

TEST(UserDefinedCompoundAssign, IsStringConcatRejectsInstanceCall) {
    auto call = MakeOperatorCall("System.String::Concat",
                                  std::make_shared<KnownType>(KnownTypeCode::String),
                                  StackType::O);
    call->IsOperator = false;
    call->IsInstanceCall = true;  // not static
    EXPECT_FALSE(UserDefinedCompoundAssign::IsStringConcat(call.get()));
}

TEST(UserDefinedCompoundAssign, IsStringConcatRejectsWrongName) {
    auto call = MakeOperatorCall("System.String::op_Addition",
                                  std::make_shared<KnownType>(KnownTypeCode::String),
                                  StackType::O);
    EXPECT_FALSE(UserDefinedCompoundAssign::IsStringConcat(call.get()));
}

TEST(UserDefinedCompoundAssign, IsStringConcatRejectsNullCall) {
    EXPECT_FALSE(UserDefinedCompoundAssign::IsStringConcat(nullptr));
}

// ---- Seed rendering ----

TEST(UserDefinedCompoundAssign, SeedRendersPostIncrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num++;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersPreIncrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Increment", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("++num;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersPostDecrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Decrement", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num--;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersPreDecrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_Decrement", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("--num;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersCheckedIncrementAsIncrement) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.SByte::op_CheckedIncrement", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num++;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersAdditionCompoundAssign) {
    auto num = MakeLocal("num", Int32());
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.Vector::op_Addition", Int32(), StackType::I4,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(num), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(2)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("num += 2;"), std::string::npos) << text;
}

TEST(UserDefinedCompoundAssign, SeedRendersStringConcatCompoundAssign) {
    auto text = MakeLocal("text", std::make_shared<KnownType>(KnownTypeCode::String));
    auto fn = MakeUserCompoundFn(std::make_unique<UserDefinedCompoundAssign>(
        "System.String::Concat",
        std::make_shared<KnownType>(KnownTypeCode::String), StackType::O,
        CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<LdLoca>(text), CompoundTargetKind::Address,
        std::make_unique<LdStr>("x")));
    fn->CheckInvariant(ILPhase::Normal);
    std::string out = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(out.find("text += "), std::string::npos) << out;
}

// ---- Setting default ----

TEST(UserDefinedCompoundAssign, CheckedOperatorsSettingDefaultTrue) {
    ILTransformSettings settings;
    // DecompilerSettings.CheckedOperators (C# 11.0) defaults true.
    EXPECT_TRUE(settings.CheckedOperators);
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods, find real operator Calls (op_* names)
// the IL reader marked IsOperator, and construct a UserDefinedCompoundAssign
// from each (carrying the call's method name + declaring type + return stack
// type), asserting the node invariant holds and the dump renders. This
// exercises the node on real operator calls (the volume the future
// TransformAssignment operator-call fold will see once wired).
TEST(UserDefinedCompoundAssign, MscorlibConstructFromRealOperatorCallsSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto settings = DefaultSettings();
    int processed = 0;
    int operatorCalls = 0;
    int constructed = 0;
    int incrementOrDecrement = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (constructed >= 200) return;
            if (!inst || inst->Op != OpCode::Call) return;
            auto* call = static_cast<Call*>(inst);
            if (!call->IsOperator) return;
            ++operatorCalls;
            if (UserDefinedCompoundAssign::IsIncrementOrDecrement(call, settings.get()))
                ++incrementOrDecrement;
            // Build a UserDefinedCompoundAssign from the call's method metadata.
            auto target = std::make_unique<LdLoca>(MakeLocal("t", call->DeclaringType));
            auto node = std::make_unique<UserDefinedCompoundAssign>(
                call->MethodName, call->DeclaringType, call->ReturnType,
                CompoundEvalMode::EvaluatesToNewValue,
                std::move(target), CompoundTargetKind::Address,
                std::make_unique<LdcI4>(1));
            node->CheckInvariant(ILPhase::Normal);
            std::string dump = node->ToString();
            EXPECT_NE(dump.find("compound.assign.userdefined"), std::string::npos) << dump;
            EXPECT_EQ(node->ResultType(), call->ReturnType);
            ++constructed;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The .NET Framework 4 legacy-csc mscorlib carries operator overloads
    // (System.Decimal's op_Equality/op_Addition/etc.), so the reader marks real
    // operator calls and the sweep constructs the nodes.
    EXPECT_GT(operatorCalls, 0);
    EXPECT_GT(constructed, 0);
}
