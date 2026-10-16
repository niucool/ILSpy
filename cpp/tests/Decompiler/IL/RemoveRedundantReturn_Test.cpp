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

// RemoveRedundantReturn tests. A void function's trailing `return;` (a
// value-less Leave of the body container on the last block) is made implicit
// (the final is dropped, the block falls through). Value returns are kept.

#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>
#include <functional>

#include <filesystem>
#include <memory>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(RemoveRedundantReturn, DropsTrailingVoidReturn) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(nullptr, std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // void return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The trailing void return is dropped (final is null -> implicit return).
    EXPECT_EQ(fn->Body->Blocks[0]->FinalInstruction, nullptr);
}

TEST(RemoveRedundantReturn, KeepsValueReturn) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));  // value return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // A value return is not redundant -- it stays.
    ASSERT_NE(fn->Body->Blocks[0]->FinalInstruction, nullptr);
    EXPECT_EQ(fn->Body->Blocks[0]->FinalInstruction->Op, OpCode::Leave);
}

TEST(RemoveRedundantReturn, KeepsReturnBeforeLastBlock) {
    // A void return on a non-last block is NOT redundant (control flow reaches
    // it conditionally); only the last block's trailing return is dropped.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // void return (not last)
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // last block void return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Only the last block's return is dropped.
    EXPECT_NE(fn->Body->Blocks[0]->FinalInstruction, nullptr) << "non-last return kept";
    EXPECT_EQ(fn->Body->Blocks[1]->FinalInstruction, nullptr) << "last return dropped";
}

TEST(RemoveRedundantReturn, MscorlibSweepDropsSomeTrailingReturns) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int dropped = 0;
    int processed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Count last-block void returns before.
        if (!fn->Body || fn->Body->Blocks.empty()) continue;
        auto& last = fn->Body->Blocks.back();
        auto* leave = dynamic_cast<Leave*>(last->FinalInstruction.get());
        bool hadTrailingVoid = leave && leave->TargetContainer == fn->Body.get() && !leave->Value;
        RemoveRedundantReturn().Run(*fn, Ctx());
        if (hadTrailingVoid && last->FinalInstruction == nullptr) ++dropped;
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    EXPECT_GT(dropped, 0) << "some trailing void return should be dropped";
}

TEST(RemoveRedundantReturn, DropsTrailingReturnAfterDeadBlockWhenUnreachableRunsFirst) {
    // A void function whose last block is an EMPTY unreachable dead block, with
    // the real trailing `return;` on the second-to-last block. The structure-
    // changing transforms can leave a trailing empty dead block after a return
    // (e.g. a `try/finally` that always returns leaves its fall-through successor
    // unreachable and empty). RemoveRedundantReturn alone bails on the empty
    // last block; RemoveUnreachableBlocks must run first to drop the dead block,
    // making the return-block last so RemoveRedundantReturn fires on it.
    auto v = std::make_shared<ILVariable>();
    v->Name = "V_0";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());  // Block 0: work; return
    fn->Body->AddBlock(std::make_unique<Block>());  // Block 1: empty dead
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // void return
    fn->CheckInvariant(ILPhase::Normal);

    // The OLD order (RemoveRedundantReturn first) bails on the empty last block.
    auto fnOld = std::make_unique<ILFunction>();
    fnOld->Body = std::make_unique<BlockContainer>();
    fnOld->Body->Parent = fnOld.get();
    fnOld->Body->ChildIndex = 0;
    fnOld->Body->AddBlock(std::make_unique<Block>());
    fnOld->Body->AddBlock(std::make_unique<Block>());
    fnOld->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    fnOld->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fnOld->Body.get()));
    RemoveRedundantReturn().Run(*fnOld, Ctx());
    RemoveUnreachableBlocks().Run(*fnOld, Ctx());
    fnOld->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(fnOld->Body->Blocks.size(), 1u);
    EXPECT_NE(fnOld->Body->Blocks[0]->FinalInstruction, nullptr)
        << "the old order leaves the trailing return (the bug)";

    // The fixed order: RemoveUnreachableBlocks first (drops Block 1), then
    // RemoveRedundantReturn (drops Block 0's now-trailing return).
    RemoveUnreachableBlocks().Run(*fn, Ctx());
    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    EXPECT_EQ(fn->Body->Blocks[0]->FinalInstruction, nullptr)
        << "the trailing return after the dead block should be dropped";
}

TEST(RemoveRedundantReturn, KeepsSwitchCaseBodyReturn) {
    // A void function whose last block is a switch case body (a block a switch
    // section branches to) ending in `return;`. Removing that return would
    // change the case body from self-terminating to fall-through and break the
    // seed's switch-inline analysis. RemoveRedundantReturn must keep it (the
    // C# avoids this by only recursing into try/lock/using/if, not switch).
    auto v = std::make_shared<ILVariable>();
    v->Name = "V_0";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());  // Block 0: switch host
    fn->Body->AddBlock(std::make_unique<Block>());  // Block 1: case body; return
    Block* caseBody = fn->Body->Blocks[1].get();
    // Block 1: work; return (the case body).
    caseBody->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    caseBody->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    // Block 0: switch(V_0) with a section branching to Block 1 (the case body).
    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto section = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    section->SetBody(std::make_unique<Branch>(caseBody));
    sw->AddSection(std::move(section));
    fn->Body->Blocks[0]->SetFinal(std::move(sw));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The case body's `return;` is kept (not removed): it is the case body's
    // terminator, not a method-level trailing return.
    ASSERT_NE(fn->Body->Blocks[1]->FinalInstruction, nullptr);
    EXPECT_EQ(fn->Body->Blocks[1]->FinalInstruction->Op, OpCode::Leave)
        << "the switch case body's return must be kept";
}

TEST(RemoveRedundantReturn, ConvertsTryBodyTrailingReturnToFallthrough) {
    // A void method whose body is a single block whose FinalInstruction is a
    // TryFinally, and the try body's last block ends in `return;` (a value-less
    // Leave of the function body). The try/finally is the method's last
    // statement, so the `return;` is redundant: the try body can fall through to
    // the finally, which runs, then the method falls through to its implicit
    // exit. ConvertReturnToFallthrough (the C# RemoveRedundantReturn recursion)
    // removes the try body's trailing return.
    auto v = std::make_shared<ILVariable>();
    v->Name = "V_0";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());  // the method body block
    Block* host = fn->Body->Blocks[0].get();
    // try body: one block -- work; return.
    auto tryBody = std::make_unique<BlockContainer>();
    auto tBlock = std::make_unique<Block>();
    tBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    tBlock->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // return;
    tryBody->AddBlock(std::move(tBlock));
    // finally body: one block -- work; endfinally.
    auto finallyBody = std::make_unique<BlockContainer>();
    auto fBlock = std::make_unique<Block>();
    fBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    fBlock->SetFinal(std::make_unique<Leave>(nullptr));  // endfinally
    finallyBody->AddBlock(std::move(fBlock));
    host->SetFinal(std::make_unique<TryFinally>(std::move(tryBody), std::move(finallyBody)));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The try body's last block's `return;` is removed: no `Leave(fn->Body)` (a
    // function return) remains in the try body -- the try body falls through
    // to the finally, which runs, then the method falls through.
    auto* tf = dynamic_cast<TryFinally*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(tf, nullptr);
    int tryReturns = 0;
    std::function<void(ILInstruction*)> countReturns = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* lv = dynamic_cast<Leave*>(inst))
            if (lv->TargetContainer == fn->Body.get()) ++tryReturns;
        for (int i = 0; i < inst->ChildCount(); ++i) countReturns(inst->GetChild(i));
    };
    countReturns(tf->TryBlock.get());
    EXPECT_EQ(tryReturns, 0)
        << "the try body's trailing return should be converted to a fallthrough";
}

TEST(RemoveRedundantReturn, ConvertsPinnedRegionBodyTrailingReturnToFallthrough) {
    // A void method whose body is a single block whose FinalInstruction is a
    // PinnedRegion (`fixed`), and the pinned body's last block ends in `return;`.
    // The fixed is the method's last statement, so the `return;` is redundant:
    // the pinned body falls through to the end of the fixed, then the method
    // falls through to its implicit exit. ConvertReturnToFallthrough (the C#
    // RemoveRedundantReturn recursion, which handles PinnedRegion) removes it.
    auto v = std::make_shared<ILVariable>();
    v->Name = "V_0";
    v->Kind = VariableKind::Local;
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());  // the method body block
    Block* host = fn->Body->Blocks[0].get();
    // pinned body: one block -- work; return.
    auto pinnedBody = std::make_unique<BlockContainer>();
    auto pBlock = std::make_unique<Block>();
    pBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    pBlock->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // return;
    pinnedBody->AddBlock(std::move(pBlock));
    // PinnedRegion(V_0, ldloc V_0, <body>) -- a `fixed (V_0 = ...) { ... }`.
    host->SetFinal(std::make_unique<PinnedRegion>(
        v, std::make_unique<LdLoc>(v), std::move(pinnedBody)));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The pinned body's last block's `return;` is removed (no Leave of the
    // function body remains in the pinned body).
    auto* pr = dynamic_cast<PinnedRegion*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(pr, nullptr);
    int pinnedReturns = 0;
    std::function<void(ILInstruction*)> countReturns = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* lv = dynamic_cast<Leave*>(inst))
            if (lv->TargetContainer == fn->Body.get()) ++pinnedReturns;
        for (int i = 0; i < inst->ChildCount(); ++i) countReturns(inst->GetChild(i));
    };
    countReturns(pr->Body.get());
    EXPECT_EQ(pinnedReturns, 0)
        << "the pinned (fixed) body's trailing return should be converted to a fallthrough";
}
