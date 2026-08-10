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
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
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
using ILSpy::Decompiler::TypeSystem::Sign;

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
