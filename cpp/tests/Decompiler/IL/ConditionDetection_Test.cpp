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
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
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

TEST(ConditionDetection, InlinesAndInvertsFallThroughReturn) {
    // b0: if (1 != 0) br b2     b1: return 1 (fall-through, 1 predecessor)
    // b2: return 0
    // -> b0: if (1 == 0) { return 1 }   (fall-through to b2; goto eliminated)
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

    // b1 is inlined into the if, then the if is inverted (b2 is the fall-through):
    // `if (1 != 0) goto b2; else { return 1 }` -> `if (1 == 0) { return 1 }`.
    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "1 != 0 negated to 1 == 0";
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Block) << "inlined b1 is the (inverted) true arm";
    EXPECT_EQ(iff->FalseInst, nullptr) << "goto dropped; fall-through to b2";
    // b1 is removed from the container; b2 stays as the fall-through target.
    EXPECT_EQ(fn->Body->Blocks.size(), 2u);
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

TEST(ConditionDetection, InvertsIfGotoElseExitWhenTargetIsFallThrough) {
    // b0: if (arg_0 >= 0) br X else { throw }   (X is the next block)
    // -> if (arg_0 < 0) { throw }   (fall-through to X, goto eliminated)
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // X
    auto exitBlock = std::make_unique<Block>();
    exitBlock->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));
    Block* exitPtr = exitBlock.get();
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdNull>(), std::make_unique<LdcI4>(0),
                               ComparisonKind::GreaterThanOrEqual),
        std::make_unique<Branch>(fn->Body->Blocks[1].get()),
        std::move(exitBlock)));
    // X is a *value* return so CFS does not pre-convert `br X` into a leave
    // (CFS only folds branches to void/value-less leaves); the goto must
    // survive to ConditionDetection so the inversion can fire.
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // The condition is negated: >=  becomes <
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr) << "condition stays a Comp after negation";
    EXPECT_EQ(cond->Kind, ComparisonKind::LessThan) << ">= negated to <";
    // The throw is now the true branch; the false branch is gone (fall-through).
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Block);
    EXPECT_EQ(iff->FalseInst, nullptr) << "goto dropped, fall-through to X";
}

TEST(ConditionDetection, DoesNotInvertWhenTargetIsNotNextBlock) {
    // b0: if (cond) br X else { throw }  where X is NOT the next block (there's
    // a block between b0 and X). Cannot invert -- fall-through would miss X.
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // middle
    fn->Body->AddBlock(std::make_unique<Block>());  // X
    auto exitBlock = std::make_unique<Block>();
    exitBlock->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::GreaterThanOrEqual),
        std::make_unique<Branch>(fn->Body->Blocks[2].get()),  // X is at index 2, not next
        std::move(exitBlock)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // Not inverted: the goto to X survives, condition not negated.
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Branch) << "goto to X survives";
    EXPECT_NE(iff->FalseInst, nullptr) << "else (throw) survives";
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
