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

// InlineReturnTransform tests. The transform duplicates a shared return block
// (`leave (ldloc V)`) so that each `stloc V, expr; br retBlock` gets its own
// 1-predecessor return block. With 1 predecessor the block is moved into the
// store's container; with more it is cloned. CFS then merges the 1-pred block
// and inlines the single-definition variable, leaving `leave (expr)`.

#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

int CountBranches(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::Branch) ++n; });
    return n;
}

// Count `goto returnBlock` patterns: a Branch whose target block is a leave
// (a return). Both InlineReturnTransform (duplicate+merge) and CFS's
// branch-to-leave folding eliminate these.
int CountGotoReturns(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        if (auto* br = dynamic_cast<Branch*>(i)) {
            if (br->TargetBlock && br->TargetBlock->FinalInstruction &&
                br->TargetBlock->FinalInstruction->Op == OpCode::Leave &&
                br->TargetBlock->Instructions.empty())
                ++n;
        }
    });
    return n;
}

int CountTopLevelBlocks(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        if (auto* c = dynamic_cast<BlockContainer*>(i))
            n += static_cast<int>(c->Blocks.size());
    });
    return n;
}

} // namespace

TEST(InlineReturnTransform, MovesSinglePredReturnBlockIntoStoreContainer) {
    // b0: stloc V, ldc.i4 1; br ret
    // ret: leave body (ldloc V)   [1 pred: b0]
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0");
    fn->Variables.push_back(V);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    auto& b0 = fn->Body->Blocks[0];
    auto& ret = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(1)));
    b0->SetFinal(std::make_unique<Branch>(ret.get()));
    ret->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(V)));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    InlineReturnTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // The 1-pred return block is moved into b0's container (now 2 blocks).
    EXPECT_EQ(fn->Body->Blocks.size(), 2u);
    // b0's branch still targets a block, and that target has 1 predecessor.
    auto* br = dynamic_cast<Branch*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    ASSERT_NE(br->TargetBlock, nullptr);
    EXPECT_EQ(br->TargetBlock->IncomingEdgeCount, 1);
    // The target is a Leave block carrying ldloc V.
    auto* leave = dynamic_cast<Leave*>(br->TargetBlock->FinalInstruction.get());
    ASSERT_NE(leave, nullptr);
    EXPECT_EQ(leave->Value->Op, OpCode::LdLoc);
}

TEST(InlineReturnTransform, ClonesMultiPredReturnBlock) {
    // b0: stloc V, ldc.i4 1; br ret
    // b1: stloc V, ldc.i4 2; br ret
    // ret: leave body (ldloc V)   [2 preds: b0, b1]
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0");
    fn->Variables.push_back(V);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    auto& ret = fn->Body->Blocks[2];
    b0->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(1)));
    b0->SetFinal(std::make_unique<Branch>(ret.get()));
    b1->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(2)));
    b1->SetFinal(std::make_unique<Branch>(ret.get()));
    ret->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(V)));
    // Capture stable raw pointers before the transform (AddBlock may reallocate
    // the Blocks vector, invalidating references into it).
    Block* b0p = fn->Body->Blocks[0].get();
    Block* b1p = fn->Body->Blocks[1].get();
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    InlineReturnTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // Each store's branch now targets a distinct 1-predecessor block.
    auto* br0 = dynamic_cast<Branch*>(b0p->FinalInstruction.get());
    auto* br1 = dynamic_cast<Branch*>(b1p->FinalInstruction.get());
    ASSERT_NE(br0->TargetBlock, nullptr);
    ASSERT_NE(br1->TargetBlock, nullptr);
    EXPECT_NE(br0->TargetBlock, br1->TargetBlock) << "the two branches must target distinct blocks";
    EXPECT_EQ(br0->TargetBlock->IncomingEdgeCount, 1);
    EXPECT_EQ(br1->TargetBlock->IncomingEdgeCount, 1);
}

TEST(InlineReturnTransform, SkipsLeaveWithNonLdLocValue) {
    // b0: stloc V, ldc.i4 1; br ret
    // ret: leave body (ldc.i4 1)   -- value is not a ldloc, must not transform.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0");
    fn->Variables.push_back(V);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    auto& b0 = fn->Body->Blocks[0];
    auto& ret = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(1)));
    b0->SetFinal(std::make_unique<Branch>(ret.get()));
    ret->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));
    Block* b0p = fn->Body->Blocks[0].get();
    Block* retp = fn->Body->Blocks[1].get();
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    InlineReturnTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // Untouched: still 2 blocks, ret still shared.
    EXPECT_EQ(fn->Body->Blocks.size(), 2u);
    auto* br = dynamic_cast<Branch*>(b0p->FinalInstruction.get());
    EXPECT_EQ(br->TargetBlock, retp);
}

TEST(InlineReturnTransform, MscorlibSweepReducesBlockCount) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int gotoReturnsBefore = 0, gotoReturnsAfter = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        gotoReturnsBefore += CountGotoReturns(*fn);  // raw IL
        ILTransformContext ctx;
        ControlFlowSimplification().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);  // merge the duplicated return blocks
        fn->CheckInvariant(ILPhase::Normal);
        gotoReturnsAfter += CountGotoReturns(*fn);
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    EXPECT_LT(gotoReturnsAfter, gotoReturnsBefore)
        << "the CFS+Inlining+InlineReturnTransform pipeline must reduce goto-return count";
}
