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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for ILInstruction::Clone -- the deep-clone port of the C# generated
// ILInstruction.Clone(). Each clone must be a disconnected (Parent == nullptr,
// ChildIndex == -1) deep copy whose ILAst dump (WriteTo) matches the original
// and whose children are fresh nodes reparented to the clone (not shared with
// the original). The corpus sweep clones every instruction in 8000 mscorlib
// methods (run through the pre-pipeline so the synthesized node kinds --
// PinnedRegion, SwitchInstruction, MatchInstruction, LockInstruction,
// UsingInstruction, ... -- appear, not just the raw-reader kinds) and asserts
// the dump-equality + disconnected invariants hold across the corpus; this is
// the completeness gate (any instruction kind whose Clone is missing or wrong
// surfaces as a dump mismatch or the default assert).

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::Sign;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// Assert the clone invariants for `inst`: the clone is disconnected, its dump
// matches the original, its children are fresh (deep copy, not shared) and
// reparented to the clone. Returns the clone for further field assertions.
std::unique_ptr<ILInstruction> AssertCloneInvariants(const ILInstruction& inst) {
    auto clone = inst.Clone();
    EXPECT_NE(clone, nullptr);
    if (!clone) return clone;
    EXPECT_EQ(clone->Op, inst.Op) << "Op preserved";
    EXPECT_EQ(clone->Parent, nullptr) << "clone is disconnected (no parent)";
    EXPECT_EQ(clone->ChildIndex, -1) << "clone is disconnected (ChildIndex -1)";
    EXPECT_EQ(clone->ToString(), inst.ToString()) << "clone dump matches original";
    // Children are fresh nodes (deep copy) reparented to the clone.
    for (int i = 0; i < inst.ChildCount(); ++i) {
        auto* origChild = inst.GetChild(i);
        auto* cloneChild = clone->GetChild(i);
        if (origChild && cloneChild) {
            EXPECT_NE(cloneChild, origChild) << "child " << i << " is a deep copy, not shared";
            EXPECT_EQ(cloneChild->Parent, clone.get()) << "child " << i << " reparented to clone";
            EXPECT_EQ(cloneChild->ChildIndex, i) << "child " << i << " ChildIndex preserved";
        }
    }
    return clone;
}

// Recursively walk `inst` and every descendant, cloning each and asserting the
// clone invariants. The completeness gate for the corpus sweep.
int WalkAndClone(const ILInstruction* inst) {
    int count = 0;
    if (!inst) return count;
    ++count;
    auto clone = inst->Clone();
    EXPECT_NE(clone, nullptr);
    if (clone) {
        EXPECT_EQ(clone->Parent, nullptr);
        EXPECT_EQ(clone->ChildIndex, -1);
        EXPECT_EQ(clone->Op, inst->Op);
        EXPECT_EQ(clone->ToString(), inst->ToString())
            << "dump mismatch for Op=" << static_cast<int>(inst->Op);
        for (int i = 0; i < clone->ChildCount(); ++i) {
            auto* cc = clone->GetChild(i);
            if (cc) EXPECT_EQ(cc->Parent, clone.get());
        }
    }
    for (int i = 0; i < inst->ChildCount(); ++i) count += WalkAndClone(inst->GetChild(i));
    return count;
}

} // namespace

// A leaf node with a shared ILVariable: the variable is a reference (not an
// owned child), so the clone shares the same ILVariablePtr.
TEST(ILInstructionClone, LdLocClonesSharingVariable) {
    auto v = MakeLocal("x");
    LdLoc ld(v);
    auto clone = AssertCloneInvariants(ld);
    ASSERT_TRUE(clone);
    auto* ldClone = static_cast<LdLoc*>(clone.get());
    EXPECT_EQ(ldClone->Variable.get(), v.get()) << "ILVariable is a shared reference";
}

// A leaf node with a scalar field: the scalar is copied.
TEST(ILInstructionClone, LdcI4ClonesScalar) {
    LdcI4 ldc(42);
    auto clone = AssertCloneInvariants(ldc);
    ASSERT_TRUE(clone);
    EXPECT_EQ(static_cast<LdcI4*>(clone.get())->Value, 42);
}

// A one-value-slot node: the Value child is deep-cloned and reparented; mutating
// the clone's Value does not affect the original (deep copy proof).
TEST(ILInstructionClone, StLocDeepClonesValue) {
    auto v = MakeLocal("s");
    auto origValue = std::make_unique<LdcI4>(7);
    auto* origValuePtr = origValue.get();
    StLoc st(v, std::move(origValue));
    auto clone = AssertCloneInvariants(st);
    ASSERT_TRUE(clone);
    // Mutate the clone's value: the original's value is untouched.
    static_cast<StLoc*>(clone.get())->Value = std::make_unique<LdcI4>(99);
    EXPECT_EQ(static_cast<LdcI4*>(origValuePtr)->Value, 7) << "original value untouched";
    EXPECT_EQ(st.Value.get(), origValuePtr) << "original still owns its value";
}

// A binary node with derived/extra scalar fields (Comp): Kind, LiftingKind,
// InputType, Unsigned survive the clone.
TEST(ILInstructionClone, CompPreservesAllScalars) {
    auto left = std::make_unique<LdLoc>(MakeLocal("a"));
    auto right = std::make_unique<LdcI4>(0);
    Comp comp(std::make_unique<LdLoc>(MakeLocal("a")), std::make_unique<LdcI4>(0),
              ComparisonKind::Inequality, ComparisonLiftingKind::CSharp, StackType::I4, true);
    comp.InputType = StackType::I8;  // override the derived value
    auto clone = AssertCloneInvariants(comp);
    ASSERT_TRUE(clone);
    auto* c = static_cast<Comp*>(clone.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality);
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->InputType, StackType::I8);
    EXPECT_TRUE(c->Unsigned);
}

// A binary numeric node: the ctor re-derives ResultStackType; the clone must
// preserve the original's ResultStackType (overridden) and IsLifted.
TEST(ILInstructionClone, BinaryNumericPreservesResultStackTypeAndLifted) {
    auto left = std::make_unique<LdLoc>(MakeLocal("i"));
    auto right = std::make_unique<LdcI4>(1);
    BinaryNumericInstruction bni(std::make_unique<LdLoc>(MakeLocal("i")),
        std::make_unique<LdcI4>(1), BinaryNumericOperator::Add, StackType::I8);
    bni.IsLifted = true;
    bni.Signed = false;
    auto clone = AssertCloneInvariants(bni);
    ASSERT_TRUE(clone);
    auto* c = static_cast<BinaryNumericInstruction*>(clone.get());
    EXPECT_EQ(c->Operator, BinaryNumericOperator::Add);
    EXPECT_EQ(c->ResultStackType, StackType::I8) << "overridden ResultStackType preserved";
    EXPECT_TRUE(c->IsLifted);
    EXPECT_FALSE(c->Signed);
}

// A Call: the Arguments collection is deep-cloned (each arg a fresh node) and
// the many scalar fields (MethodName, IsInstanceCall, IsNewObj, IsOperator,
// TypeArgumentsCount, ...) survive.
TEST(ILInstructionClone, CallClonesArgumentsAndScalars) {
    Call call("System.Console::WriteLine");
    call.IsInstanceCall = false;
    call.IsNewObj = false;
    call.IsOperator = false;
    call.TypeArgumentsCount = 2;
    call.AddArg(std::make_unique<LdStr>("hello"));
    call.AddArg(std::make_unique<LdcI4>(3));
    auto clone = AssertCloneInvariants(call);
    ASSERT_TRUE(clone);
    auto* c = static_cast<Call*>(clone.get());
    EXPECT_EQ(c->MethodName, "System.Console::WriteLine");
    EXPECT_FALSE(c->IsInstanceCall);
    EXPECT_FALSE(c->IsNewObj);
    EXPECT_EQ(c->TypeArgumentsCount, 2);
    ASSERT_EQ(c->Arguments.size(), 2u);
    EXPECT_EQ(c->Arguments[0]->Op, OpCode::LdStr);
    EXPECT_EQ(c->Arguments[1]->Op, OpCode::LdcI4);
    EXPECT_NE(c->Arguments[0].get(), call.Arguments[0].get()) << "args are deep copies";
}

// An IfInstruction: three slots (Condition/TrueInst/FalseInst), the optional
// FalseInst (null) is preserved as null in the clone.
TEST(ILInstructionClone, IfInstructionClonesThreeSlots) {
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(MakeLocal("b")),
        std::make_unique<LdNull>(), ComparisonKind::Equality);
    auto t = std::make_unique<Branch>(0u);
    IfInstruction iff(std::make_unique<Comp>(std::make_unique<LdLoc>(MakeLocal("b")),
        std::make_unique<LdNull>(), ComparisonKind::Equality), std::make_unique<Branch>(0u));
    auto clone = AssertCloneInvariants(iff);
    ASSERT_TRUE(clone);
    auto* c = static_cast<IfInstruction*>(clone.get());
    EXPECT_NE(c->Condition, nullptr);
    EXPECT_NE(c->TrueInst, nullptr);
    EXPECT_EQ(c->FalseInst, nullptr) << "null FalseInst preserved";
}

// A Block: Instructions + FinalInstruction are deep-cloned, Kind and the
// block's own StartILOffset are copied, and the clone's IncomingEdgeCount is 0
// (a disconnected clone has no incoming edges).
TEST(ILInstructionClone, BlockClonesInstructionsFinalKindOffset) {
    Block block;
    block.Kind = BlockKind::ControlFlow;
    block.StartILOffset = 0x1234;
    block.IncomingEdgeCount = 5;
    block.Add(std::make_unique<LdcI4>(1));
    block.SetFinal(std::make_unique<Branch>(0u));
    auto clone = AssertCloneInvariants(block);
    ASSERT_TRUE(clone);
    auto* c = static_cast<Block*>(clone.get());
    EXPECT_EQ(c->Kind, BlockKind::ControlFlow);
    EXPECT_EQ(c->StartILOffset, 0x1234u);
    EXPECT_EQ(c->IncomingEdgeCount, 0) << "disconnected clone has no incoming edges";
    ASSERT_EQ(c->Instructions.size(), 1u);
    EXPECT_EQ(c->Instructions[0]->Op, OpCode::LdcI4);
    EXPECT_NE(c->Instructions[0].get(), block.Instructions[0].get());
    EXPECT_NE(c->FinalInstruction.get(), block.FinalInstruction.get());
}

// A BlockContainer: the Blocks collection is deep-cloned (each block a fresh
// node reparented to the container) and Kind is copied.
TEST(ILInstructionClone, BlockContainerClonesBlocksAndKind) {
    BlockContainer bc;
    bc.Kind = ContainerKind::While;
    auto b1 = std::make_unique<Block>();
    b1->SetFinal(std::make_unique<Branch>(0u));
    bc.AddBlock(std::move(b1));
    auto clone = AssertCloneInvariants(bc);
    ASSERT_TRUE(clone);
    auto* c = static_cast<BlockContainer*>(clone.get());
    EXPECT_EQ(c->Kind, ContainerKind::While);
    ASSERT_EQ(c->Blocks.size(), 1u);
    EXPECT_NE(c->Blocks[0].get(), bc.Blocks[0].get()) << "block is a deep copy";
    EXPECT_EQ(c->Blocks[0]->Parent, c) << "block reparented to the cloned container";
}

// A Branch: the target block is a non-owning reference (copied by pointer), and
// TargetOffset / HasOffset are copied. (The dump uses TargetOffset, so the
// clone's dump matches.)
TEST(ILInstructionClone, BranchCopiesTargetReferenceAndOffset) {
    Block target;
    target.StartILOffset = 0x0040;
    Branch br(&target);
    auto clone = AssertCloneInvariants(br);
    ASSERT_TRUE(clone);
    auto* c = static_cast<Branch*>(clone.get());
    EXPECT_EQ(c->TargetBlock, &target) << "target block copied by reference";
    EXPECT_EQ(c->TargetOffset, 0x0040u);
    EXPECT_FALSE(c->HasOffset);
}

// A Leave: the target container is a non-owning reference (copied) and the
// optional Value child is deep-cloned.
TEST(ILInstructionClone, LeaveCopiesTargetContainerAndClonesValue) {
    BlockContainer target;
    Leave lv(&target, std::make_unique<LdLoc>(MakeLocal("r")));
    auto clone = AssertCloneInvariants(lv);
    ASSERT_TRUE(clone);
    auto* c = static_cast<Leave*>(clone.get());
    EXPECT_EQ(c->TargetContainer, &target) << "target container copied by reference";
    EXPECT_NE(c->Value.get(), lv.Value.get());
    EXPECT_EQ(c->Value->Parent, c);
}

// A TryFinally: both the TryBlock and FinallyBlock children are deep-cloned.
TEST(ILInstructionClone, TryFinallyClonesBothBlocks) {
    auto tryBlock = std::make_unique<Block>();
    static_cast<Block*>(tryBlock.get())->SetFinal(std::make_unique<Branch>(0u));
    auto finallyBlock = std::make_unique<Block>();
    static_cast<Block*>(finallyBlock.get())->SetFinal(std::make_unique<Leave>());
    TryFinally tf(std::move(tryBlock), std::move(finallyBlock));
    auto clone = AssertCloneInvariants(tf);
    ASSERT_TRUE(clone);
    auto* c = static_cast<TryFinally*>(clone.get());
    EXPECT_NE(c->TryBlock.get(), tf.TryBlock.get());
    EXPECT_NE(c->FinallyBlock.get(), tf.FinallyBlock.get());
    EXPECT_EQ(c->TryBlock->Parent, c);
    EXPECT_EQ(c->FinallyBlock->Parent, c);
}

// A NumericCompoundAssign: the CompoundAssignmentInstruction base scalars
// (EvalMode, TargetKind) and the NumericCompoundAssign scalars (Operator,
// IsLifted, ResultStackType, Type) survive, and Target/Value are deep-cloned.
TEST(ILInstructionClone, NumericCompoundAssignPreservesScalars) {
    NumericCompoundAssign nca(BinaryNumericOperator::Add, false, Sign::Signed,
        StackType::I4, StackType::I4, StackType::I4, true, nullptr,
        CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<LdLoca>(MakeLocal("v")), CompoundTargetKind::Address,
        std::make_unique<LdcI4>(1));
    auto clone = AssertCloneInvariants(nca);
    ASSERT_TRUE(clone);
    auto* c = static_cast<NumericCompoundAssign*>(clone.get());
    EXPECT_EQ(c->Operator, BinaryNumericOperator::Add);
    EXPECT_TRUE(c->IsLifted);
    EXPECT_EQ(c->EvalMode, CompoundEvalMode::EvaluatesToOldValue);
    EXPECT_EQ(c->TargetKind, CompoundTargetKind::Address);
    EXPECT_NE(c->Target.get(), nca.Target.get());
    EXPECT_NE(c->Value.get(), nca.Value.get());
}

// An ILFunction: the Body BlockContainer is deep-cloned and reparented to the
// cloned function, Variables are shared (references), and IsConstructor /
// IsStatic are copied.
TEST(ILInstructionClone, ILFunctionClonesBodyReparentsAndSharesVariables) {
    ILFunction fn;
    fn.IsConstructor = true;
    fn.IsStatic = false;
    auto v = MakeLocal("loc");
    fn.Variables.push_back(v);
    auto body = std::make_unique<BlockContainer>();
    body->AddBlock(std::make_unique<Block>());
    fn.Body = std::move(body);
    fn.Body->Parent = &fn;
    fn.Body->ChildIndex = 0;
    auto clone = AssertCloneInvariants(fn);
    ASSERT_TRUE(clone);
    auto* c = static_cast<ILFunction*>(clone.get());
    EXPECT_TRUE(c->IsConstructor);
    EXPECT_FALSE(c->IsStatic);
    ASSERT_EQ(c->Variables.size(), 1u);
    EXPECT_EQ(c->Variables[0].get(), v.get()) << "Variables are shared references";
    ASSERT_NE(c->Body, nullptr);
    EXPECT_NE(c->Body.get(), fn.Body.get()) << "Body is a deep copy";
    EXPECT_EQ(c->Body->Parent, c) << "Body reparented to the cloned function";
    EXPECT_EQ(c->Body->ChildIndex, 0);
}

// The corpus sweep: clone every instruction in 8000 mscorlib methods (run
// through the pre-pipeline so synthesized node kinds appear) and assert the
// clone invariants (dump-equal + disconnected) hold. This is the completeness
// gate -- any instruction kind whose Clone is missing or wrong surfaces here.
TEST(ILInstructionClone, MscorlibSweepClonesEveryInstruction) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int totalCloned = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ILSpy::Decompiler::IL::ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Pre-pipeline through HighLevelLoopTransform so the synthesized node
        // kinds (PinnedRegion, SwitchInstruction, MatchInstruction,
        // LockInstruction, UsingInstruction, While/DoWhile containers, ...)
        // appear, not just the raw-reader kinds.
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        EarlyExpressionTransforms().Run(*fn, ctx);
        RemoveDeadVariableInit().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);
        SwitchDetection().Run(*fn, ctx);
        SwitchOnNullableTransform().Run(*fn, ctx);
        LoopDetection().Run(*fn, ctx);
        PatternMatchingTransform().Run(*fn, ctx);
        ConditionDetection().Run(*fn, ctx);
        HighLevelLoopTransform::Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);

        totalCloned += WalkAndClone(fn->Body.get());
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(totalCloned, 100000) << "the sweep cloned a meaningful number of instructions";
}
