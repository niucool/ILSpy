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

// ConditionDetection tests: the core if-goto-to-if/else transform. A block
// ending with `if (cond) goto target` followed by a single-predecessor
// fall-through block becomes `if (cond) { goto target } else { fall-through }`,
// eliminating the separate block + goto. The mscorlib sweep pins the global
// contract (invariant + goto-count drop).

#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

std::unique_ptr<ILFunction> WrapBlocks(std::vector<std::unique_ptr<Block>> blocks) {
    auto container = std::make_unique<BlockContainer>();
    for (auto& b : blocks) container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

int CountTopLevelBlocks(ILFunction& fn) {
    int count = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        if (auto* c = dynamic_cast<BlockContainer*>(i))
            count += static_cast<int>(c->Blocks.size());
    });
    return count;
}

int CountGotos(const std::string& s) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = s.find("br IL_", pos)) != std::string::npos) {
        ++count;
        pos += 6;
    }
    return count;
}

void RunPipeline(ILFunction& fn) {
    ILTransformContext ctx;
    ControlFlowSimplification().Run(fn, ctx);
    ILInlining().Run(fn, ctx);
    LoopDetection().Run(fn, ctx);
    ConditionDetection().Run(fn, ctx);
}

} // namespace

TEST(ConditionDetection, InlinesSinglePredFallThroughIntoIfFalseInst) {
    // b0: if (1 != 0) br b2     b1: return 1 (fall-through, 1 predecessor)
    // b2: return 0
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(fn->Body->Blocks[2].get())));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // b1 must be inlined into the IfInstruction's FalseInst.
    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->FalseInst, nullptr) << "the fall-through must become the else branch";
    EXPECT_EQ(iff->FalseInst->Op, OpCode::Block) << "FalseInst is the inlined block body";
    // b1 is no longer a top-level block in the container.
    EXPECT_EQ(fn->Body->Blocks.size(), 2u) << "the inlined block is removed from the container";
}

TEST(ConditionDetection, DoesNotInlineMultiPredFallThrough) {
    // b0: if (cond) br b2  (fall-through to b1)
    // b1: return 1          (predecessors: b0 fall-through + b2 branch = 2)
    // b2: br b1              (1 pred: b0 if-branch)
    // b1 has 2 predecessors -> NOT inlined.
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(fn->Body->Blocks[2].get())));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // b2 is a trampoline (empty body, just `br b1`) that CFS collapses,
    // repointing b0's if-branch to b1. b1 then has 2 predecessors (b0
    // fall-through + b0 if-branch) -> ConditionDetection must NOT inline it.
    EXPECT_EQ(fn->Body->Blocks.size(), 2u) << "trampoline b2 collapsed by CFS; b1 stays (multi-pred)";
    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    EXPECT_EQ(iff->FalseInst, nullptr) << "multi-pred block must not be inlined into FalseInst";
}

TEST(ConditionDetection, MscorlibSweepReducesGotoCount) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int transformed = 0;
    int gotosBefore = 0;
    int gotosAfter = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        // Count gotos before ConditionDetection (after CFS+Inlining+LoopDetection).
        ILTransformContext ctx0;
        ControlFlowSimplification().Run(*fn, ctx0);
        ILInlining().Run(*fn, ctx0);
        LoopDetection().Run(*fn, ctx0);
        int before = CountTopLevelBlocks(*fn);
        // Now run ConditionDetection.
        ConditionDetection().Run(*fn, ctx0);
        fn->CheckInvariant(ILPhase::Normal);
        int after = CountTopLevelBlocks(*fn);
        gotosBefore += before;
        gotosAfter += after;
        ++transformed;
        if (transformed >= 3000) break;
    }
    EXPECT_GT(transformed, 2000);
    EXPECT_LT(gotosAfter, gotosBefore) << "ConditionDetection must reduce goto count";
}
