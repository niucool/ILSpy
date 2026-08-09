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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for LockTransform: the no-flag MCS and V2 lock shapes (Monitor.Enter /
// Exit try/finally with a straight `call Exit` finally) fold into a
// LockInstruction (`lock (expr) { body }`), and the flag-based Roslyn shape
// (`stloc obj; stloc flag(ldc.i4 0); .try { call Enter(ldloc obj, ldloca flag);
// body } finally { if (!flag) leave; call Exit(ldloc obj); leave }`) folds the
// same way. Adapted to this port's block model (the TryFinally is a non-terminal
// at block->Instructions[i] with the stloc / call at [i-2] / [i-1] after CFS
// merges the EH wrapper with the preceding block; the endfinally `leave` is the
// finally entry's FinalInstruction). The flag finally is TWO blocks in this
// port (the if-skip-leave then the Exit), and the stlocs may sit in a preceding
// block when CFS did not merge the EH wrapper. The V4 / V4YieldReturn flag
// shapes (inline `stloc obj` as the Enter arg) and the single-block
// `if(flag){Exit}` finally are deferred.

#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, std::move(type), 1);
    v->Name = std::move(name);
    return v;
}

std::unique_ptr<Call> MakeMonitorCall(const char* shortName, ILVariablePtr arg) {
    auto call = std::make_unique<Call>(std::string("System.Threading.Monitor::") + shortName);
    call->AddArg(std::make_unique<LdLoc>(std::move(arg)));
    return call;
}

// `call Monitor.Enter(ldloc obj, ldloca flag)` -- the 2-argument Roslyn flag
// shape. The flag is taken by address (the ref bool Enter sets to true).
std::unique_ptr<Call> MakeMonitorEnter2(ILVariablePtr obj, ILVariablePtr flag) {
    auto call = std::make_unique<Call>(std::string("System.Threading.Monitor::") + "Enter");
    call->AddArg(std::make_unique<LdLoc>(obj));
    call->AddArg(std::make_unique<LdLoca>(flag));
    return call;
}

// A flag-based finally as it appears in this port after ConditionDetection: TWO
// blocks. block[0] = `if (comp(eq, ldloc flag, ldc.i4 0)) leave` (skip Exit when
// the lock was not taken), block[1] = `call Exit(ldloc obj); leave`.
void AddFlagFinally(BlockContainer* finallyContainer, ILVariablePtr flag, ILVariablePtr obj) {
    auto b0 = std::make_unique<Block>();
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(flag),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Equality, false);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                std::make_unique<Leave>(finallyContainer)));
    finallyContainer->AddBlock(std::move(b0));
    auto b1 = std::make_unique<Block>();
    b1->Add(MakeMonitorCall("Exit", obj));
    b1->SetFinal(std::make_unique<Leave>(finallyContainer));
    finallyContainer->AddBlock(std::move(b1));
}

// A Roslyn try entry: `call Enter(ldloc obj, ldloca flag); <body>; leave(try)`.
Block* AddRoslynTryEntry(BlockContainer* tryContainer, ILVariablePtr obj, ILVariablePtr flag) {
    auto entry = std::make_unique<Block>();
    entry->Add(MakeMonitorEnter2(obj, flag));
    entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    entry->SetFinal(std::make_unique<Leave>(tryContainer));
    auto* p = entry.get();
    tryContainer->AddBlock(std::move(entry));
    return p;
}

// Build a standalone Roslyn flag-lock ILFunction. If `precedingBlock` is true,
// the stloc obj / stloc flag sit in a separate block before the TryFinally
// (the dominant mscorlib shape); otherwise they share the TryFinally's block
// (the C# same-block shape). Returns the function and exposes the lock-object
// and lock-expression variables via the out-params.
struct RoslynSetup {
    std::unique_ptr<ILFunction> fn;
    ILVariablePtr exprObj;
    ILVariablePtr lockObj;
    ILVariablePtr flag;
};
RoslynSetup BuildRoslynLock(bool precedingBlock) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto exprObj = MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("obj", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto flag = MakeLocal("flag", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    fn->Variables.push_back(exprObj);
    fn->Variables.push_back(lockObj);
    fn->Variables.push_back(flag);

    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    AddRoslynTryEntry(tryC, lockObj, flag);
    AddFlagFinally(finC, flag, lockObj);

    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));

    if (precedingBlock) {
        // block P: [stloc obj, stloc flag], final = br Q; block Q: [TryFinally],
        // final = leave(body).
        auto P = std::make_unique<Block>();
        P->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
        P->Add(std::make_unique<StLoc>(flag, std::make_unique<LdcI4>(0)));
        auto Q = std::make_unique<Block>();
        Block* QPtr = Q.get();
        P->SetFinal(std::make_unique<Branch>(QPtr));
        Q->Add(std::move(tf));
        Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(P));
        fn->Body->AddBlock(std::move(Q));
    } else {
        // root: [stloc obj, stloc flag, TryFinally], final = br next.
        auto root = std::make_unique<Block>();
        root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
        root->Add(std::make_unique<StLoc>(flag, std::make_unique<LdcI4>(0)));
        root->Add(std::move(tf));
        root->SetFinal(std::make_unique<Branch>(nextPtr));
        fn->Body->AddBlock(std::move(root));
    }
    fn->Body->AddBlock(std::move(next));
    return {std::move(fn), exprObj, lockObj, flag};
}

// Count LockInstruction nodes in a function.
int CountLocks(ILFunction& fn);

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

int CountLocks(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        if (i->Op == OpCode::LockInstruction) ++n;
    });
    return n;
}

// Run the full pre-pipeline through LockTransform (the GetILTransforms()
// position -- after ConditionDetection in the BlockILTransform post-order set),
// so the sweep and integration tests exercise the realistic pipeline shape.
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
}

// A finally entry block holding `call Exit(ldloc obj)` then the endfinally
// `leave` targeting the finally container. (The block model puts the leave in
// the FinalInstruction slot.)
Block* AddFinallyEntry(BlockContainer* finallyContainer, ILVariablePtr obj) {
    auto entry = std::make_unique<Block>();
    entry->Add(MakeMonitorCall("Exit", obj));
    entry->SetFinal(std::make_unique<Leave>(finallyContainer));
    auto* p = entry.get();
    finallyContainer->AddBlock(std::move(entry));
    return p;
}

// A try entry block holding one body instruction (a void call) then a branch to
// `after` (the block after the try/finally), modelling the IL `leave` inside
// the try that exits to the code after the region.
Block* AddTryEntry(BlockContainer* tryContainer, Block* after) {
    auto entry = std::make_unique<Block>();
    entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    entry->SetFinal(std::make_unique<Branch>(after));
    auto* p = entry.get();
    tryContainer->AddBlock(std::move(entry));
    return p;
}

} // namespace

// ---- LockInstruction node ----

TEST(LockTransform, LockInstructionNodeInvariant) {
    auto obj = MakeParam("obj", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto body = std::make_unique<BlockContainer>();
    auto lock = std::make_unique<LockInstruction>(std::make_unique<LdLoc>(obj), std::move(body));
    EXPECT_EQ(lock->Op, OpCode::LockInstruction);
    EXPECT_EQ(lock->ResultType(), StackType::Void);
    EXPECT_TRUE(HasFlag(lock->DirectFlags(), InstructionFlags::ControlFlow));
    EXPECT_TRUE(HasFlag(lock->DirectFlags(), InstructionFlags::SideEffect));
    ASSERT_EQ(lock->ChildCount(), 2);
    EXPECT_EQ(lock->GetChild(0)->Op, OpCode::LdLoc);
    EXPECT_EQ(lock->GetChild(1)->Op, OpCode::BlockContainer);
    EXPECT_EQ(lock->GetChild(0)->Parent, lock.get());
    EXPECT_EQ(lock->GetChild(1)->Parent, lock.get());
    // Dump mentions "lock (" and the body container.
    std::string dump;
    lock->WriteTo(dump);
    EXPECT_NE(dump.find("lock ("), std::string::npos);
}

// ---- TransformLockMCS ----

// `stloc lockObj(ldloc expr); call Enter(ldloc lockObj); .try { body } finally
// { call Exit(ldloc lockObj); leave }` -> `lock (ldloc expr) { body }`.
TEST(LockTransform, TransformLockMCSFoldsToLockInstruction) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto exprObj = MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("lockObj", std::make_shared<KnownType>(KnownTypeCode::Object));
    fn->Variables.push_back(exprObj);
    fn->Variables.push_back(lockObj);

    // Body container: root (stloc + call Enter + TryFinally + br next), next.
    auto root = std::make_unique<Block>();
    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
    root->Add(MakeMonitorCall("Enter", lockObj));
    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    root->Add(std::move(tf));
    root->SetFinal(std::make_unique<Branch>(nextPtr));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->Body->AddBlock(std::move(next));
    AddTryEntry(tryC, nextPtr);
    AddFinallyEntry(finC, lockObj);

    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(fn->Body->Blocks.size(), 2u);
    auto* rootBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(rootBlock->Instructions.size(), 1u);
    ASSERT_EQ(rootBlock->Instructions[0]->Op, OpCode::LockInstruction);
    auto* lk = static_cast<LockInstruction*>(rootBlock->Instructions[0].get());
    ASSERT_EQ(lk->OnExpression->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(lk->OnExpression.get())->Variable.get(), exprObj.get());
    ASSERT_EQ(lk->Body->Op, OpCode::BlockContainer);
    // The try body survived the fold (moved into the LockInstruction).
    auto* bodyC = static_cast<BlockContainer*>(lk->Body.get());
    EXPECT_FALSE(bodyC->Blocks.empty());
}

// ---- TransformLockV2 ----

// `stloc lockObj(ldloc temp); call Enter(ldloc temp); .try { body } finally
// { call Exit(ldloc lockObj); leave }` -> `lock (ldloc temp) { body }`.
TEST(LockTransform, TransformLockV2FoldsToLockInstruction) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto temp = MakeParam("temp", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("lockObj", std::make_shared<KnownType>(KnownTypeCode::Object));
    fn->Variables.push_back(temp);
    fn->Variables.push_back(lockObj);

    auto root = std::make_unique<Block>();
    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(temp)));
    root->Add(MakeMonitorCall("Enter", temp));
    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    root->Add(std::move(tf));
    root->SetFinal(std::make_unique<Branch>(nextPtr));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->Body->AddBlock(std::move(next));
    AddTryEntry(tryC, nextPtr);
    AddFinallyEntry(finC, lockObj);

    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    auto* rootBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(rootBlock->Instructions.size(), 1u);
    ASSERT_EQ(rootBlock->Instructions[0]->Op, OpCode::LockInstruction);
    auto* lk = static_cast<LockInstruction*>(rootBlock->Instructions[0].get());
    ASSERT_EQ(lk->OnExpression->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(lk->OnExpression.get())->Variable.get(), temp.get());
}

// ---- TransformLockRoslyn (flag-based, same-block) ----

// `stloc obj(lockExpr); stloc flag(ldc.i4 0); .try { call Enter(ldloc obj,
// ldloca flag); body } finally { if (!flag) leave; call Exit(ldloc obj); leave }`
// -> `lock (lockExpr) { body }`. The stlocs and TryFinally share one block.
TEST(LockTransform, TransformLockRoslynSameBlockFolds) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(CountLocks(*fn), 1);
    auto* rootBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(rootBlock->Instructions.size(), 1u);
    ASSERT_EQ(rootBlock->Instructions[0]->Op, OpCode::LockInstruction);
    auto* lk = static_cast<LockInstruction*>(rootBlock->Instructions[0].get());
    ASSERT_EQ(lk->OnExpression->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(lk->OnExpression.get())->Variable.get(), setup.exprObj.get());
    // The call Enter was dropped from the try entry; the lock body still has the
    // body call.
    auto* bodyC = static_cast<BlockContainer*>(lk->Body.get());
    ASSERT_FALSE(bodyC->Blocks.empty());
    auto* te = bodyC->Blocks[0].get();
    ASSERT_EQ(te->Instructions.size(), 1u);
    EXPECT_EQ(te->Instructions[0]->Op, OpCode::Call);
}

// ---- TransformLockRoslyn (flag-based, preceding-block) ----

// The dominant mscorlib shape: the stloc obj / stloc flag sit in a separate
// block before the TryFinally's block (CFS did not merge them). The fold puts
// the LockInstruction in the preceding block, absorbs the TryFinally's block
// final (the after-lock continuation) into it, and drops the now-empty
// TryFinally block.
TEST(LockTransform, TransformLockRoslynPrecedingBlockFolds) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/true);
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(CountLocks(*fn), 1);
    // The TryFinally's block was dropped; the body now has the (former) preceding
    // block and the trailing next block.
    ASSERT_EQ(fn->Body->Blocks.size(), 2u);
    auto* lockBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(lockBlock->Instructions.size(), 1u);
    ASSERT_EQ(lockBlock->Instructions[0]->Op, OpCode::LockInstruction);
    auto* lk = static_cast<LockInstruction*>(lockBlock->Instructions[0].get());
    ASSERT_EQ(lk->OnExpression->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(lk->OnExpression.get())->Variable.get(), setup.exprObj.get());
    // The preceding block absorbed the TryFinally's block final (the function
    // body leave).
    ASSERT_EQ(lockBlock->FinalInstruction->Op, OpCode::Leave);
}

// ---- Roslyn negatives ----

// A non-Boolean flag (e.g. Int32) must not fold (the flag is the Enter ref bool).
TEST(LockTransform, RoslynRejectsNonBooleanFlag) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    // Retype the flag to Int32 (not Boolean).
    setup.flag->Type = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ILTransformContext ctx;
    LockTransform().Run(*setup.fn, ctx);
    EXPECT_EQ(CountLocks(*setup.fn), 0);
}

// A flag initialised to a non-zero value must not fold (Enter sets the flag;
// the init must be the canonical ldc.i4 0).
TEST(LockTransform, RoslynRejectsNonZeroFlagInit) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    // Rebuild the flag store with ldc.i4 1.
    auto* root = setup.fn->Body->Blocks[0].get();
    auto* fs = static_cast<StLoc*>(root->Instructions[1].get());
    fs->Value = std::make_unique<LdcI4>(1);
    fs->Value->Parent = fs;
    ILTransformContext ctx;
    LockTransform().Run(*setup.fn, ctx);
    EXPECT_EQ(CountLocks(*setup.fn), 0);
}

// A non-Monitor Enter (some other type's Enter, 2-arg) must not fold.
TEST(LockTransform, RoslynRejectsNonMonitorEnter) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    // Replace the Enter call's method name.
    auto* trc = static_cast<BlockContainer*>(
        static_cast<TryFinally*>(setup.fn->Body->Blocks[0]->Instructions[2].get())->TryBlock.get());
    auto* enter = static_cast<Call*>(trc->Blocks[0]->Instructions[0].get());
    enter->MethodName = "MyNamespace.MyType::Enter";
    ILTransformContext ctx;
    LockTransform().Run(*setup.fn, ctx);
    EXPECT_EQ(CountLocks(*setup.fn), 0);
}

// A lock object loaded three times (Enter + two Exits) exceeds the Roslyn limit
// of two and must not fold. (The second Exit call stands in for the extra load.)
TEST(LockTransform, RoslynRejectsHighLoadCount) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    // Add a second Exit call to the finally's Exit block.
    auto* tf = static_cast<TryFinally*>(setup.fn->Body->Blocks[0]->Instructions[2].get());
    auto* fin = static_cast<BlockContainer*>(tf->FinallyBlock.get());
    fin->Blocks[1]->Add(MakeMonitorCall("Exit", setup.lockObj));
    ILTransformContext ctx;
    LockTransform().Run(*setup.fn, ctx);
    EXPECT_EQ(CountLocks(*setup.fn), 0);
}

// A finally that is not the two-block flag shape (here a single no-flag Exit
// block) must not fold as a Roslyn lock.
TEST(LockTransform, RoslynRejectsNonTwoBlockFinally) {
    auto setup = BuildRoslynLock(/*precedingBlock=*/false);
    // Replace the finally with a single no-flag Exit block.
    auto* tf = static_cast<TryFinally*>(setup.fn->Body->Blocks[0]->Instructions[2].get());
    auto newFin = std::make_unique<BlockContainer>();
    auto* finC = newFin.get();
    AddFinallyEntry(finC, setup.lockObj);
    tf->FinallyBlock = std::move(newFin);
    tf->FinallyBlock->Parent = tf;
    ILTransformContext ctx;
    LockTransform().Run(*setup.fn, ctx);
    EXPECT_EQ(CountLocks(*setup.fn), 0);
}

// ---- Negatives ----

// A non-Monitor Enter (some other type's Enter) must not fold.
TEST(LockTransform, RejectsNonMonitorEnter) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto exprObj = MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("lockObj", std::make_shared<KnownType>(KnownTypeCode::Object));
    fn->Variables.push_back(exprObj);
    fn->Variables.push_back(lockObj);

    auto root = std::make_unique<Block>();
    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
    // A non-Monitor Enter: "MyNamespace.MyType::Enter".
    auto enter = std::make_unique<Call>("MyNamespace.MyType::Enter");
    enter->AddArg(std::make_unique<LdLoc>(lockObj));
    root->Add(std::move(enter));
    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    root->Add(std::move(tf));
    root->SetFinal(std::make_unique<Branch>(nextPtr));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->Body->AddBlock(std::move(next));
    AddTryEntry(tryC, nextPtr);
    AddFinallyEntry(finC, lockObj);

    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);

    auto* rootBlock = fn->Body->Blocks[0].get();
    // The TryFinally survived (no fold): three non-terminals still present.
    ASSERT_EQ(rootBlock->Instructions.size(), 3u);
    EXPECT_EQ(rootBlock->Instructions[2]->Op, OpCode::TryFinally);
}

// A lock object loaded three times (Enter + two Exits) exceeds the MCS limit
// of two and must not fold. (The second Exit call stands in for the extra load.)
TEST(LockTransform, RejectsHighLoadCountMCS) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto exprObj = MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("lockObj", std::make_shared<KnownType>(KnownTypeCode::Object));
    fn->Variables.push_back(exprObj);
    fn->Variables.push_back(lockObj);

    auto root = std::make_unique<Block>();
    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
    root->Add(MakeMonitorCall("Enter", lockObj));
    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    root->Add(std::move(tf));
    root->SetFinal(std::make_unique<Branch>(nextPtr));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->Body->AddBlock(std::move(next));
    AddTryEntry(tryC, nextPtr);
    // Finally with two Exit calls -> lockObj loaded three times total.
    auto fe = std::make_unique<Block>();
    fe->Add(MakeMonitorCall("Exit", lockObj));
    fe->Add(MakeMonitorCall("Exit", lockObj));
    fe->SetFinal(std::make_unique<Leave>(finC));
    finC->AddBlock(std::move(fe));

    ILTransformContext ctx;
    LockTransform().Run(*fn, ctx);

    auto* rootBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(rootBlock->Instructions.size(), 3u);
    EXPECT_EQ(rootBlock->Instructions[2]->Op, OpCode::TryFinally);
}

// ---- Setting gate ----

TEST(LockTransform, LockStatementOffIsNoOp) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto exprObj = MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lockObj = MakeLocal("lockObj", std::make_shared<KnownType>(KnownTypeCode::Object));
    fn->Variables.push_back(exprObj);
    fn->Variables.push_back(lockObj);

    auto root = std::make_unique<Block>();
    auto next = std::make_unique<Block>();
    Block* nextPtr = next.get();
    root->Add(std::make_unique<StLoc>(lockObj, std::make_unique<LdLoc>(exprObj)));
    root->Add(MakeMonitorCall("Enter", lockObj));
    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    root->Add(std::move(tf));
    root->SetFinal(std::make_unique<Branch>(nextPtr));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->Body->AddBlock(std::move(next));
    AddTryEntry(tryC, nextPtr);
    AddFinallyEntry(finC, lockObj);

    ILTransformContext ctx;
    ctx.Settings.LockStatement = false;
    LockTransform().Run(*fn, ctx);

    auto* rootBlock = fn->Body->Blocks[0].get();
    ASSERT_EQ(rootBlock->Instructions.size(), 3u);
    EXPECT_EQ(rootBlock->Instructions[2]->Op, OpCode::TryFinally);
}

// ---- Seed rendering ----

// The ILAstToCSharp seed renders a LockInstruction as `lock (expr) { body }`.
TEST(LockTransform, SeedRendersLockStatement) {
    auto obj = MakeParam("obj", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(obj);

    auto tryContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto entry = std::make_unique<Block>();
    entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    entry->SetFinal(std::make_unique<Leave>(tryC));
    tryContainer->AddBlock(std::move(entry));

    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<LockInstruction>(std::make_unique<LdLoc>(obj), std::move(tryContainer)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "object obj");
    EXPECT_NE(text.find("lock (obj)"), std::string::npos);
    // The body call flattens "::" to ".": "System.Foo::Bar" -> "System.Foo.Bar".
    EXPECT_NE(text.find("System.Foo.Bar"), std::string::npos);
}

// ---- mscorlib sweep ----

// On the real mscorlib corpus, running the full pre-pipeline through
// LockTransform (the GetILTransforms() position -- after ConditionDetection)
// preserves the ILAst invariant AND fires the flag-based Roslyn shape (the
// dominant .NET Framework 4 codegen), so the sweep asserts both the invariant
// and a non-trivial fold count. The no-flag MCS/V2 shapes fire only on
// mono-compiled assemblies.
TEST(LockTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int folded = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        LockTransform().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::LockInstruction) ++folded;
        });
        fn->CheckInvariant(ILPhase::Normal);
    }
    EXPECT_GT(processed, 5000);
    // The flag-based Roslyn shape is the dominant .NET Framework 4 codegen, so
    // the transform now fires on the corpus (the no-flag MCS/V2 shapes fire only
    // on mono-compiled assemblies). Assert a non-trivial fold count so a
    // regression that drops the flag-based path is caught.
    EXPECT_GT(folded, 100);
}
