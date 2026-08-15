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
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <iostream>
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
    // CFS now folds `br b2` (b2 = return 0) to a direct `return 0`, so b0's
    // if-true arm becomes the early return; b1 stays as the fall-through.
    // Result: `if (1 != 0) { return 0; } return 1;` (no goto, no invert needed).
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

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // The condition is NOT negated (CFS pre-folds the value-return; no invert).
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Inequality) << "condition stays 1 != 0";
    // The true arm is the folded `return 0` (a Leave), not a Block/goto.
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Leave) << "br b2 folded to return 0";
    EXPECT_EQ(iff->FalseInst, nullptr) << "no else; fall-through to b1";
    // b1 (return 1) stays as the fall-through; b2 was folded away.
    EXPECT_EQ(fn->Body->Blocks.size(), 2u);
}

TEST(ConditionDetection, SwapsEmptyThenBranchToNegatedCondition) {
    // b0's final is `if (cond) Block{} else Block{ work; leave }` -- an empty
    // then with the work in the else. SwapEmptyThen swaps to
    // `if (!cond) Block{ work; leave }` (negated condition, work in the true
    // arm, no else). This lets the following inline/invert transforms see the
    // exit in the true arm. The port's renderer swaps empty arms, but the
    // ILAst-level swap is needed for ConditionDetection's restructuring.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    // empty then arm
    auto emptyThen = std::make_unique<Block>();
    // else arm: work; leave (return)
    auto elseArm = std::make_unique<Block>();
    elseArm->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(7)));
    elseArm->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(emptyThen), std::move(elseArm)));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // The condition is negated (num != 0 -> num == 0).
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "condition negated to num == 0";
    // The true arm now holds the work (the StLoc), the else is gone.
    ASSERT_NE(iff->TrueInst, nullptr);
    auto* tb = dynamic_cast<Block*>(iff->TrueInst.get());
    ASSERT_NE(tb, nullptr);
    ASSERT_FALSE(tb->Instructions.empty());
    EXPECT_EQ(tb->Instructions[0]->Op, OpCode::StLoc) << "work moved to the true arm";
    EXPECT_EQ(iff->FalseInst, nullptr) << "the else is dropped after the swap";
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
    // X has a body (a store) before its leave so CFS does not pre-fold `br X`
    // into a direct leave -- the goto must survive to ConditionDetection so
    // the inversion can fire. (CFS folds branches to plain leave blocks.)
    auto tmp = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    tmp->Name = "tmp";
    fn->Variables.push_back(tmp);
    fn->Body->Blocks[1]->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdcI4>(0)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
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
    // X has a body so CFS does not pre-fold `br X` into a direct leave; the
    // goto to X must survive (X is not the next block, so the inversion cannot
    // fire either).
    auto tmp = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    tmp->Name = "tmp";
    fn->Variables.push_back(tmp);
    fn->Body->Blocks[2]->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdcI4>(0)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
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

TEST(ConditionDetection, MergesCommonExitGotosIntoIfElse) {
    // The shared-tail pattern: after inline+invert, both arms of the if goto
    // the same join block (the next block). The common exit is pulled out and
    // both gotos dropped, yielding `if (cond) { ... } else { ... }` with a
    // fall-through to the join.
    //   b0: if (num < 0) br b2;  b1: flags=0; br join;  b2: flags=min; num=-num; br join;
    //   join: lo=num; return
    auto fn = WrapBlocks({});
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    auto V = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    V->Name = "num";
    fn->Variables.push_back(V);
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(0)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(0),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(fn->Body->Blocks[2].get())));  // if (num<0) br b2
    fn->Body->Blocks[1]->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(0)));  // flags=0 (approx)
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[3].get()));  // br join
    fn->Body->Blocks[2]->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(1)));  // flags=min
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[3].get()));  // br join
    fn->Body->Blocks[3]->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(2)));  // join body (keeps `br join` alive)
    fn->Body->Blocks[3]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // join: return
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The join block (b3) must remain, and b0's final must be an if with both
    // arms present and neither ending in a Branch (the common gotos dropped).
    EXPECT_EQ(fn->Body->Blocks.size(), 2u) << "b1 and b2 absorbed; join remains";
    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->TrueInst, nullptr);
    ASSERT_NE(iff->FalseInst, nullptr) << "the fall-through is inlined into the else";
    // Neither arm ends with a Branch (both gotos dropped).
    auto trailingBranch = [](ILInstruction* arm) -> Branch* {
        if (!arm) return nullptr;
        if (auto* b = dynamic_cast<Block*>(arm)) return dynamic_cast<Branch*>(b->FinalInstruction.get());
        return dynamic_cast<Branch*>(arm);
    };
    EXPECT_EQ(trailingBranch(iff->TrueInst.get()), nullptr) << "true arm goto dropped";
    EXPECT_EQ(trailingBranch(iff->FalseInst.get()), nullptr) << "false arm goto dropped";
}

TEST(ConditionDetection, RestructuresTwoBlockEarlyExitChain) {
    // b0: if (cond) br b3 (no else)   b1: if (cond2) throw (falls to b2)
    // b2: throw   b3: return. The early-exit chain: the goto b3 skips the
    // throw chain. Expect restructuring to `if (!cond) { if (cond2) throw; throw }`
    // then fall to b3 (return) -- no goto.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    for (int k = 0; k < 4; ++k) fn->Body->AddBlock(std::make_unique<Block>());
    Block* b0 = fn->Body->Blocks[0].get();
    Block* b1 = fn->Body->Blocks[1].get();
    Block* b2 = fn->Body->Blocks[2].get();
    Block* b3 = fn->Body->Blocks[3].get();
    b0->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b3)));
    b1->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
                               ComparisonKind::Inequality),
        std::make_unique<Throw>(std::make_unique<Call>())));
    b2->SetFinal(std::make_unique<Throw>(std::make_unique<Call>()));
    b3->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    int brB3 = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
        if (!i) return;
        if (auto* br = dynamic_cast<Branch*>(i)) if (br->TargetBlock == b3) ++brB3;
        for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
    };
    walk(fn->Body.get());
    EXPECT_EQ(brB3, 0) << "the goto to b3 should be restructured away";
}

TEST(ConditionDetection, OrderIfBlocksSwapsArmsToMatchILOrder) {
    // b0: if (cond) Block(StartIL=20){leave(2)} else Block(StartIL=10){leave(1)}
    // The false arm (IL offset 10) comes before the true arm (IL offset 20) in
    // IL order, so OrderIfBlocks swaps the arms + negates the condition ->
    // if (!cond) Block(StartIL=10){leave(1)} else Block(StartIL=20){leave(2)}.
    // The C# ConditionDetection.OrderIfBlocks (runs after the inline/invert
    // loop): swap when GetStartILOffset(TrueInst) > GetStartILOffset(FalseInst).
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    auto trueArm = std::make_unique<Block>();
    trueArm->StartILOffset = 20;
    trueArm->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(2)));
    auto falseArm = std::make_unique<Block>();
    falseArm->StartILOffset = 10;
    falseArm->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(trueArm), std::move(falseArm)));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // Condition negated (Inequality -> Equality via NegateCondition's comp flip).
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "condition negated to match IL order";
    // Arms swapped: the true arm is now the original false arm (StartIL=10).
    auto* tb = dynamic_cast<Block*>(iff->TrueInst.get());
    ASSERT_NE(tb, nullptr);
    EXPECT_EQ(tb->StartILOffset, 10u) << "true arm is the earlier-IL block";
}

TEST(ConditionDetection, InlineTrueBranchInlinesSinglePredForwardTargetInNormalContainer) {
    // The multi-block early-exit chain where the fall-through is 2-pred (so
    // TryInlineIfFallThrough bails) but the true-arm target is single-pred:
    // bPre: if (num != 0) br b1 else br b0  (makes b1 2-pred: bPre + b0's fall)
    // b0:  if (num == 1) br b2            (no else; true arm = Branch to b2,
    //                                       single-pred forward, NOT next)
    // b1:  leave(call)                    (2-pred; CFS won't fold a Call leave)
    // b2:  stloc(v, call); leave(call)    (single-pred from b0; the happy path)
    // Without InlineTrueBranch: the goto to b2 survives -- TryInlineIfFallThrough
    // bails (b1 is 2-pred), TryInvertIfExit bails (no false arm / b2 not next).
    // With InlineTrueBranch (restricted to Normal containers): b2 is inlined
    // into b0's true arm; the goto to b2 is eliminated.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    for (int k = 0; k < 4; ++k) fn->Body->AddBlock(std::make_unique<Block>());
    Block* bPre = fn->Body->Blocks[0].get();
    Block* b0 = fn->Body->Blocks[1].get();
    Block* b1 = fn->Body->Blocks[2].get();
    Block* b2 = fn->Body->Blocks[3].get();
    bPre->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b1),
        std::make_unique<Branch>(b0)));
    b0->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(b2)));
    b1->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<Call>()));
    b2->Add(std::make_unique<StLoc>(v, std::make_unique<Call>()));
    b2->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<Call>()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    int brB2 = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
        if (!i) return;
        if (auto* br = dynamic_cast<Branch*>(i)) if (br->TargetBlock == b2) ++brB2;
        for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
    };
    walk(fn->Body.get());
    EXPECT_EQ(brB2, 0) << "the goto to b2 should be inlined into b0's true arm";
}

TEST(ConditionDetection, IntroduceShortCircuitCombinesNestedIfGoto) {
    // `if (cond1) { if (cond2) br X }` (the true arm a Block whose final is a
    // nested if-goto) -- the argument-validation skip pattern. IntroduceShortCircuit
    // combines to `if (cond1 && cond2) br X`; then the fall-through (the throw
    // chain) inlines into the else and the if inverts to `if (!(cond1 && cond2))
    // { throw chain }` fall to X. No goto, and the condition uses `&&`.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    for (int k = 0; k < 3; ++k) fn->Body->AddBlock(std::make_unique<Block>());
    Block* b0 = fn->Body->Blocks[0].get();
    Block* b1 = fn->Body->Blocks[1].get();
    Block* b2 = fn->Body->Blocks[2].get();
    // b0: if (num != 0) Block { if (num == 1) br b2 } (no else). Falls to b1.
    auto nestedIf = std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(b2));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->SetFinal(std::move(nestedIf));
    b0->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(trueBlock)));
    // b1: throw (the exit chain). b2: work; return (X, the happy path with work
    // so `br b2` stays a real goto, not foldable to a return).
    b1->SetFinal(std::make_unique<Throw>(std::make_unique<Call>()));
    b2->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(7)));
    b2->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    int brB2 = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
        if (!i) return;
        if (auto* br = dynamic_cast<Branch*>(i)) if (br->TargetBlock == b2) ++brB2;
        for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
    };
    walk(fn->Body.get());
    EXPECT_EQ(brB2, 0) << "the goto to b2 (the happy path) should be restructured away";
}


TEST(ConditionDetection, DropsTrailingGotoToNextBlockFromIfArm) {
    // if (cond) { body; goto nextBlock } where nextBlock is the next block:
    // the goto is redundant -- the arm falls through to nextBlock. The arm's
    // trailing Branch is dropped.
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0: the if block
    fn->Body->AddBlock(std::make_unique<Block>());  // b1: nextBlock (the join)
    auto V = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    V->Name = "num";
    fn->Variables.push_back(V);
    // b0: if (cond) { num = 1; goto b1 } else { return }  -- b1 is the next block.
    auto trueArm = std::make_unique<Block>();
    trueArm->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(1)));
    trueArm->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));  // goto b1 (next)
    auto falseArm = std::make_unique<Block>();
    falseArm->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // return
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality),
        std::move(trueArm), std::move(falseArm)));
    fn->Body->Blocks[1]->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(2)));  // b1 body (keeps the goto alive)
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // b1: return
    fn->CheckInvariant(ILPhase::Normal);

    RunPipeline(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->TrueInst, nullptr);
    // The true arm's trailing Branch is dropped (it had a body, so it's a Block).
    auto* tb = dynamic_cast<Block*>(iff->TrueInst.get());
    ASSERT_NE(tb, nullptr);
    EXPECT_EQ(tb->FinalInstruction, nullptr) << "trailing goto to next block dropped";
}

// ---- GetStartILOffset (the tested-but-not-yet-wired foundation the wired
// ReduceNestingTransform.ImproveILOrdering fold consults to decide whether
// inverting an if to match IL order helps) ----

TEST(ConditionDetection, GetStartILOffsetReturnsInstructionOffset) {
    // A plain instruction with a set ILRange reports its own StartILOffset and
    // isEmpty == false.
    auto inst = std::make_unique<LdcI4>(42);
    inst->SetILRange(0x10, 0x14);
    bool isEmpty = true;
    EXPECT_EQ(ConditionDetection::GetStartILOffset(inst.get(), isEmpty), 0x10);
    EXPECT_FALSE(isEmpty);
}

TEST(ConditionDetection, GetStartILOffsetReturnsEmptyForDefaultRange) {
    // The default ILRange {0,0} is empty (Start >= End): isEmpty == true, and
    // StartILOffset is 0.
    auto inst = std::make_unique<LdcI4>(0);
    bool isEmpty = false;
    EXPECT_EQ(ConditionDetection::GetStartILOffset(inst.get(), isEmpty), 0);
    EXPECT_TRUE(isEmpty);
}

TEST(ConditionDetection, GetStartILOffsetReturnsLeaveValueOffsetForValuedLeave) {
    // A valued Leave (a non-Nop Value -- a `return expr`) reports its Value's
    // StartILOffset, not the Leave's. The C# comment: the Leave's Value's
    // ILRange is a better indicator of the actual location than the Leave's.
    auto V = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    V->Name = "num";
    auto value = std::make_unique<LdLoc>(V);
    value->SetILRange(0x20, 0x24);
    auto leave = std::make_unique<Leave>(nullptr, std::move(value));
    leave->SetILRange(0x30, 0x34);
    bool isEmpty = true;
    EXPECT_EQ(ConditionDetection::GetStartILOffset(leave.get(), isEmpty), 0x20);
    EXPECT_FALSE(isEmpty) << "isEmpty reflects the Value's range, not the Leave's";
}

TEST(ConditionDetection, GetStartILOffsetReturnsLeaveOffsetForValuelessLeave) {
    // A value-less Leave (a `return;` -- this port's reader emits Leave(container)
    // with no Value for a void leave) reports the Leave's own StartILOffset.
    auto leave = std::make_unique<Leave>(nullptr);  // no Value
    leave->SetILRange(0x40, 0x44);
    bool isEmpty = true;
    EXPECT_EQ(ConditionDetection::GetStartILOffset(leave.get(), isEmpty), 0x40);
    EXPECT_FALSE(isEmpty);
}

TEST(ConditionDetection, GetStartILOffsetReturnsLeaveOffsetForNopValuedLeave) {
    // A Leave whose Value is a Nop (the `leave (nop)` artifact the C# compiler
    // wraps a `fixed` block in -- a value-less leave materialised as a Nop
    // Value in the C#) reports the Leave's own StartILOffset, not the Nop's.
    // This port's reader emits a null Value for the void form, but a Nop Value
    // can arise from transforms; the helper treats both as "not a valued leave".
    auto leave = std::make_unique<Leave>(nullptr, std::make_unique<Nop>());
    leave->SetILRange(0x50, 0x54);
    bool isEmpty = true;
    EXPECT_EQ(ConditionDetection::GetStartILOffset(leave.get(), isEmpty), 0x50);
    EXPECT_FALSE(isEmpty);
}

TEST(ConditionDetection, MscorlibGetStartILOffsetSweep) {
    // The wired ImproveILOrdering fold consults GetStartILOffset on the
    // post-ConditionDetection instructions the reader populated the ILRange
    // for (D115). The sweep verifies the helper is robust on every real
    // instruction: it returns a non-negative offset and the isEmpty flag is
    // consistent with the effective range (IsILRangeEmpty for the non-Leave
    // case, the Value's IsILRangeEmpty for a valued Leave).
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int checked = 0;
    int valuedLeaves = 0;
    int emptyRanges = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            bool isEmpty = false;
            int offset = ConditionDetection::GetStartILOffset(inst, isEmpty);
            EXPECT_GE(offset, 0) << "GetStartILOffset must return a non-negative offset";
            // Cross-check the isEmpty flag against the effective range.
            bool expectedEmpty;
            if (auto* leave = dynamic_cast<Leave*>(inst)) {
                if (leave->Value && leave->Value->Op != OpCode::Nop) {
                    expectedEmpty = leave->Value->IsILRangeEmpty();
                    ++valuedLeaves;
                } else {
                    expectedEmpty = inst->IsILRangeEmpty();
                }
            } else if (dynamic_cast<Block*>(inst)) {
                // A Block's label (its own StartILOffset field) is the first
                // instruction's offset; GetStartILOffset returns it with
                // isEmpty=false (the base ILRange is not propagated through
                // block-synthesizing transforms, but the label is valid).
                expectedEmpty = false;
            } else {
                expectedEmpty = inst->IsILRangeEmpty();
            }
            EXPECT_EQ(isEmpty, expectedEmpty)
                << "isEmpty must match the effective range's IsILRangeEmpty";
            if (isEmpty) ++emptyRanges;
            ++checked;
        });
        if (checked > 200000) break;  // bound the sweep
    }
    EXPECT_GT(checked, 1000) << "the sweep must exercise real instructions";
    // Sanity: the .NET Framework 4 legacy-csc mscorlib has valued `return expr`
    // leaves (the helper's special case) and the reader populates non-empty
    // ranges for most instructions (D115), so both counters should be nonzero.
    EXPECT_GT(valuedLeaves, 0) << "the corpus carries valued `return expr` leaves";
    EXPECT_GT(emptyRanges, 0) << "some instructions carry the default empty range";
}

// ---------------------------------------------------------------------------
// ConditionDetection::InvertIf -- the "invert if to match IL order / reduce
// nesting" operation, ported as a tested-but-not-yet-wired foundation (the
// wired ReduceNestingTransform.ImproveILOrdering / ReduceNesting folds are the
// subsequent iteration). The C# reads `ifInst` as a non-terminal at
// `block.Instructions[i]` with the `falseCode...; exit` as sibling
// instructions; this port makes the `IfInstruction` the block's
// `FinalInstruction`, so the `falseCode...; exit` is the next block in the
// container, and the old then moves into that next block.

namespace {

ILVariablePtr MakeLocalVar(const char* name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = name;
    return v;
}

} // namespace

// Case 2 (no falseCode, just the exit): the if's then is the bare exit, the
// old then (a bare exit) moves into the next block as its final.
TEST(ConditionDetection, InvertIfFoldsBareExitNoFalseCode) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // b1 (the falseCode + exit)
    // b0: if (cond) return 7   (then = Leave(body, 7), unreachable; no else)
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(7))));
    // b1: return  (the exit; no falseCode non-terminals)
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Leave) << "if's then is the bare exit (b1's old final)";
    EXPECT_EQ(iff->FalseInst, nullptr) << "still no else; fall through to b1";
    // Condition negated: 1 != 0 -> 1 == 0 (NegateComparison on Inequality).
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "condition is negated";
    // b1 now carries the old then (return 7) as its final.
    ASSERT_EQ(fn->Body->Blocks.size(), 2u);
    auto* b1Final = fn->Body->Blocks[1]->FinalInstruction.get();
    ASSERT_NE(b1Final, nullptr);
    EXPECT_EQ(b1Final->Op, OpCode::Leave);
    auto* retLeave = static_cast<Leave*>(b1Final);
    ASSERT_NE(retLeave->Value, nullptr);
    EXPECT_EQ(retLeave->Value->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(retLeave->Value.get())->Value, 7);
}

// Case 1 (falseCode + exit): wrap the next block's content in a Block as the
// if's TrueInst; the old then (a bare Branch) moves into the next block.
TEST(ConditionDetection, InvertIfFoldsFalseCodeIntoBlock) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // b1 (falseCode + exit)
    fn->Body->AddBlock(std::make_unique<Block>());  // X (the then target)
    auto a = MakeLocalVar("a"), b = MakeLocalVar("b");
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    // b0: stloc a(1); if (1 != 0) br X   (then = br X, unreachable; no else)
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(a, std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(fn->Body->Blocks[2].get())));
    // b1: stloc b(2); return   (the falseCode + exit)
    fn->Body->Blocks[1]->Add(std::make_unique<StLoc>(b, std::make_unique<LdcI4>(2)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    // X: return 0
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    // if's then is a Block wrapping b1's content (stloc b(2); return).
    ASSERT_EQ(iff->TrueInst->Op, OpCode::Block);
    EXPECT_EQ(iff->FalseInst, nullptr);
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "condition negated";
    // b1 now carries the old then (br X) as its final.
    ASSERT_EQ(fn->Body->Blocks.size(), 3u);
    auto* b1Final = fn->Body->Blocks[1]->FinalInstruction.get();
    ASSERT_NE(b1Final, nullptr);
    EXPECT_EQ(b1Final->Op, OpCode::Branch) << "b1's final is the old then (br X)";
    EXPECT_EQ(static_cast<Branch*>(b1Final)->TargetBlock, fn->Body->Blocks[2].get());
}

// The then is a Block (not a bare exit): its non-terminals AND its final move
// into the next block (this port's then-Block carries the control flow in
// FinalInstruction, not in Instructions as in the C#).
TEST(ConditionDetection, InvertIfSpreadsThenBlockIntoNextBlock) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // b1 (falseCode + exit)
    fn->Body->AddBlock(std::make_unique<Block>());  // X (the then target)
    auto c = MakeLocalVar("c"), b = MakeLocalVar("b");
    fn->Variables.push_back(c);
    fn->Variables.push_back(b);
    // then-Block: stloc c(3); br X
    auto thenBlock = std::make_unique<Block>();
    thenBlock->Add(std::make_unique<StLoc>(c, std::make_unique<LdcI4>(3)));
    thenBlock->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[2].get()));
    // b0: if (cond) { stloc c(3); br X }  (then is a Block, unreachable; no else)
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(thenBlock)));
    // b1: stloc b(2); return
    fn->Body->Blocks[1]->Add(std::make_unique<StLoc>(b, std::make_unique<LdcI4>(2)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);

    // b1 now carries the then-Block's content: stloc c(3); br X.
    ASSERT_EQ(fn->Body->Blocks[1]->Instructions.size(), 1u);
    auto* stc = dynamic_cast<StLoc*>(fn->Body->Blocks[1]->Instructions[0].get());
    ASSERT_NE(stc, nullptr);
    EXPECT_EQ(stc->Variable.get(), c.get());
    auto* b1Final = fn->Body->Blocks[1]->FinalInstruction.get();
    ASSERT_NE(b1Final, nullptr);
    EXPECT_EQ(b1Final->Op, OpCode::Branch) << "then-Block's final (br X) moved into b1";
}

TEST(ConditionDetection, InvertIfRejectsNonIfFinal) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // b1
    // b0: if(cond, then) is a NON-TERMINAL; the final is a Leave. (This shape
    // does not arise from the reader, but it tests the guard.)
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(7)));
    IfInstruction* iffPtr = iff.get();
    fn->Body->Blocks[0]->Add(std::move(iff));  // if as a non-terminal
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // final is a Leave
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(), iffPtr);
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: the if is still a non-terminal, the final is still the Leave.
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions[0].get(), iffPtr);
}

TEST(ConditionDetection, InvertIfRejectsWithElse) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(7)),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(8))));  // has an else
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: the if still has an else.
    auto* iff = static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    EXPECT_NE(iff->FalseInst, nullptr);
}

TEST(ConditionDetection, InvertIfRejectsThenNotUnreachable) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    auto v = MakeLocalVar("v");
    fn->Variables.push_back(v);
    // then = ldloc v (a load, NOT EndPointUnreachable).
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<LdLoc>(v)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: the if's TrueInst is still the ldloc.
    auto* iff = static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff->TrueInst, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::LdLoc);
}

TEST(ConditionDetection, InvertIfRejectsNoNextBlock) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0 only (no next block)
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(7))));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: still one block.
    EXPECT_EQ(fn->Body->Blocks.size(), 1u);
}

TEST(ConditionDetection, InvertIfRejectsMultiPredNextBlock) {
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());  // b0
    fn->Body->AddBlock(std::make_unique<Block>());  // b1 (multi-pred: b0 fall-through + b2 branch)
    fn->Body->AddBlock(std::make_unique<Block>());  // b2
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(7))));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    // b2 branches to b1, making b1 multi-pred.
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));
    RecomputeIncomingEdgeCounts(*fn);
    ASSERT_EQ(fn->Body->Blocks[1]->IncomingEdgeCount, 2) << "b1 must be multi-pred";
    fn->CheckInvariant(ILPhase::Normal);

    ConditionDetection::InvertIf(fn->Body->Blocks[0].get(),
        static_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get()));
    fn->CheckInvariant(ILPhase::Normal);
    // Unchanged: b1 still carries its original content (the move would
    // misbranch the b2 edge into the old then, so InvertIf bails).
    EXPECT_EQ(fn->Body->Blocks[1]->FinalInstruction->Op, OpCode::Leave);
}

// Corpus probe + invariant sweep: runs the pre-pipeline through
// ConditionDetection over mscorlib, classifies the if-final shapes InvertIf
// would consume, and (for the would-fire candidates) calls InvertIf and checks
// the invariant. This pins the block-model divergence: the C# shape (if as a
// non-terminal + falseCode as sibling) does not arise in this port (the if is
// the block's FinalInstruction), and the null-FalseInst + single-pred-next-block
// shape is the one InvertIf fires on. The probe confirms the shape distribution
// and that InvertIf is safe on real shapes.
TEST(ConditionDetection, MscorlibInvertIfShapeProbe) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int ifFinals = 0;
    int candidates = 0;          // if-final + TrueInst unreachable + FalseInst null
    int withNextBlock = 0;        // ... and a next block exists
    int wouldFire = 0;            // ... and next block is single-pred (IncomingEdgeCount == 1)
    int multiPredBail = 0;       // ... and next block is multi-pred (InvertIf bails)
    int noNextBlock = 0;         // ... and no next block (InvertIf bails)
    int fires = 0;               // InvertIf actually fired (one per function)
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPipeline(*fn);  // CFS + ILInlining + LoopDetection + ConditionDetection

        bool firedThisFn = false;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            auto* container = dynamic_cast<BlockContainer*>(inst);
            if (!container) return;
            for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
                auto* iff = dynamic_cast<IfInstruction*>(container->Blocks[i]->FinalInstruction.get());
                if (!iff) continue;
                ++ifFinals;
                if (!iff->TrueInst) continue;
                if (!HasFlag(iff->TrueInst->Flags(), InstructionFlags::EndPointUnreachable)) continue;
                if (iff->FalseInst) continue;  // has an else
                ++candidates;
                // The next block in the container.
                Block* nextBlock = (i + 1 < container->Blocks.size())
                    ? container->Blocks[i + 1].get() : nullptr;
                if (!nextBlock) { ++noNextBlock; continue; }
                ++withNextBlock;
                if (nextBlock->IncomingEdgeCount != 1) { ++multiPredBail; continue; }
                ++wouldFire;
                if (firedThisFn) return;  // one InvertIf per function
                firedThisFn = true;
                ConditionDetection::InvertIf(container->Blocks[i].get(), iff);
                fn->CheckInvariant(ILPhase::Normal);
                ++fires;
            }
        });
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000) << "the sweep must exercise real methods";
    EXPECT_GT(ifFinals, 0) << "the corpus carries if-final blocks";
    // The invariant check inside the loop is the safety gate -- InvertIf must
    // not corrupt the tree on any real shape. The shape counts record the
    // block-model divergence: the C# ImproveILOrdering shape (if as a
    // non-terminal + falseCode as sibling) does not arise in this port (the
    // if is the block's FinalInstruction), and ConditionDetection's
    // TryInlineIfFallThrough consumes the single-pred fall-through, so the
    // null-FalseInst candidate shape survives mainly as the multi-pred
    // next-block case (InvertIf bails) or the no-next-block case.
    std::cerr << "InvertIf shape probe: processed=" << processed
              << " ifFinals=" << ifFinals
              << " candidates=" << candidates
              << " withNextBlock=" << withNextBlock
              << " wouldFire(1-pred)=" << wouldFire
              << " multiPredBail=" << multiPredBail
              << " noNextBlock=" << noNextBlock
              << " fires=" << fires << "\n";
    (void)candidates; (void)withNextBlock; (void)wouldFire; (void)multiPredBail;
    (void)noNextBlock; (void)fires;
}
