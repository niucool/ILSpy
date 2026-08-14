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

// LoopDetection tests: back-edge detection and loop containerization. A
// back-branch (branch to a block that dominates the source) forms a natural
// loop; the body blocks move into a BlockContainer(Kind=Loop) and the exit
// branch becomes a Leave(loopContainer).

#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>

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

} // namespace

TEST(LoopDetection, DetectsAndWrapsBackEdgeLoop) {
    // b0: init; br b1     b1: if (true) br b3 (exit)   b2: body; br b1 (back edge)
    // b1 dominates b2 (every path to b2 goes through b1); b2->b1 is a back edge.
    auto b0 = std::make_unique<Block>();
    auto b1 = std::make_unique<Block>();
    auto b2 = std::make_unique<Block>();
    auto b3 = std::make_unique<Block>();

    auto fn = WrapBlocks({});
    Block* b1p = b1.get();
    Block* b3p = b3.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(b3));
    b1p = fn->Body->Blocks[1].get();
    b3p = fn->Body->Blocks[3].get();

    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b3p)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(b1p));
    fn->Body->Blocks[3]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // A Loop-kind container must now exist in the tree.
    bool foundLoop = false;
    int leaveCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* c = dynamic_cast<BlockContainer*>(i)) {
            if (c->Kind == ContainerKind::Loop) foundLoop = true;
        }
        if (i->Op == OpCode::Leave) ++leaveCount;
    });
    EXPECT_TRUE(foundLoop) << "a BlockContainer(Kind=Loop) must be constructed";
    EXPECT_GE(leaveCount, 1) << "the loop exit branch becomes a Leave";
}

TEST(LoopDetection, DetectsNestedBackEdgeInsideOuterLoop) {
    // An outer loop whose body contains a nested back-edge: b0 (entry);
    // b1 (outer header: if cond br b2 else leave); b2 (body; br b3);
    // b3 (inner header: if cond2 br b4 else br b5); b4 (inner body; br b3 -- the
    // INNER back-edge); b5 (br b1 -- the OUTER back-edge). b1 dominates b5
    // (outer loop), b3 dominates b4 (inner loop). LoopDetection must form BOTH:
    // an outer Loop containing b1..b5, and a nested Loop inside it for b3..b4.
    // The port used to form only the outer Loop (its body container was skipped
    // as Loop-kind), leaving the inner back-edge as a goto.
    auto b0 = std::make_unique<Block>();
    auto b1 = std::make_unique<Block>();
    auto b2 = std::make_unique<Block>();
    auto b3 = std::make_unique<Block>();
    auto b4 = std::make_unique<Block>();
    auto b5 = std::make_unique<Block>();
    auto b6 = std::make_unique<Block>();  // after the outer loop
    auto fn = WrapBlocks({});
    Block* b1p = b1.get(); Block* b2p = b2.get(); Block* b3p = b3.get();
    Block* b4p = b4.get(); Block* b5p = b5.get(); Block* b6p = b6.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(b3));
    fn->Body->AddBlock(std::move(b4));
    fn->Body->AddBlock(std::move(b5));
    fn->Body->AddBlock(std::move(b6));
    b1p = fn->Body->Blocks[1].get(); b2p = fn->Body->Blocks[2].get();
    b3p = fn->Body->Blocks[3].get(); b4p = fn->Body->Blocks[4].get();
    b5p = fn->Body->Blocks[5].get(); b6p = fn->Body->Blocks[6].get();

    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(b1p));
    // b1: outer header -- if (1 != 0) br b2 else leave(fn)
    b1p->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                              ComparisonKind::Inequality),
        std::make_unique<Branch>(b2p), std::make_unique<Leave>(fn->Body.get())));
    b2p->SetFinal(std::make_unique<Branch>(b3p));
    // b3: inner header -- if (1 != 0) br b4 else br b5
    b3p->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                              ComparisonKind::Inequality),
        std::make_unique<Branch>(b4p), std::make_unique<Branch>(b5p)));
    b4p->SetFinal(std::make_unique<Branch>(b3p));  // inner back-edge
    b5p->SetFinal(std::make_unique<Branch>(b1p));  // outer back-edge
    b6p->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    int loopCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* c = dynamic_cast<BlockContainer*>(i))
            if (c->Kind == ContainerKind::Loop) ++loopCount;
    });
    EXPECT_EQ(loopCount, 2) << "both the outer and the nested inner loop must be formed";
}

TEST(LoopDetection, RepointsExternalBranchToHeaderToNewEntryPoint) {
    // b0: if (cond) br b1 else br b4   (entry: loop header or external b4)
    // b1: if (cond2) br b3 else br b2  (loop header; b1 dominates b2)
    // b2: br b1 (back edge)
    // b3: leave (exit)
    // b4: br b1 (external entry to the loop header)
    // After LoopDetection the external b4->b1 must be repointed to the new
    // entry point inside the loop container, so the pre-header (b1) carries no
    // branch target and no duplicate IL_XXXX label appears.
    auto fn = WrapBlocks({});
    for (int i = 0; i < 5; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* b1p = fn->Body->Blocks[1].get();
    Block* b3p = fn->Body->Blocks[3].get();
    Block* b4p = fn->Body->Blocks[4].get();
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b1p), std::make_unique<Branch>(b4p)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b3p), std::make_unique<Branch>(fn->Body->Blocks[2].get())));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(b1p));
    fn->Body->Blocks[3]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->Blocks[4]->SetFinal(std::make_unique<Branch>(b1p));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // No branch in the tree targets the original header (now the pre-header);
    // all entries (back-edge + external) go to the new entry point.
    int targetingOldHeader = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* br = dynamic_cast<Branch*>(i))
            if (br->TargetBlock == b1p) ++targetingOldHeader;
    });
    EXPECT_EQ(targetingOldHeader, 0)
        << "external branches to the header must be repointed to the new entry";
}

TEST(LoopDetection, NoFalseLoopOnAcyclicFlow) {
    // b0: br b1; b1: br b2; b2: leave -- no back edges, no loops.
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[2].get()));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    bool foundLoop = false;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* c = dynamic_cast<BlockContainer*>(i))
            if (c->Kind == ContainerKind::Loop) foundLoop = true;
    });
    EXPECT_FALSE(foundLoop) << "acyclic flow must not produce a loop";
}

TEST(LoopDetection, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int transformed = 0;
    int loopCount = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ILTransformContext ctx;
        ControlFlowSimplification().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        LoopDetection().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        ++transformed;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (auto* c = dynamic_cast<BlockContainer*>(i))
                if (c->Kind == ContainerKind::Loop) ++loopCount;
        });
        if (transformed >= 5000) break;
    }
    EXPECT_GT(transformed, 3000);
    EXPECT_GT(loopCount, 100) << "mscorlib must have many loops detected";
}

TEST(LoopDetection, PicksEarliestOutOfLoopExitDeterministically) {
    // Nondeterminism regression guard (the LoopDetection determinism fix). A loop
    // with TWO out-of-loop successor blocks -- b4 at a lower StartILOffset and b5
    // at a higher one -- must exit to the earliest block (b4):
    //   b1 (header): if (c) br b4 else br b2
    //   b2 (body):   if (c) br b5 else br b3
    //   b3 (body):   br b1        (back edge; b1 dominates b2/b3)
    //   b4, b5: leave (function)  (both loop exits)
    // FindExitPoint/ConstructLoop used to iterate a std::set<ControlFlowNode*> in
    // pointer-address order, so a multi-exit loop got a run-to-run-varying exit
    // (and member block order), breaking output determinism. Now the earliest
    // out-of-loop successor is chosen and members keep their container order.
    auto fn = WrapBlocks({});
    for (int i = 0; i < 6; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    auto cd = [] {
        return std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                                      ComparisonKind::Inequality);
    };
    Block* b1 = fn->Body->Blocks[1].get();
    Block* b2 = fn->Body->Blocks[2].get();
    Block* b3 = fn->Body->Blocks[3].get();
    Block* b4 = fn->Body->Blocks[4].get();
    Block* b5 = fn->Body->Blocks[5].get();
    // Give the blocks increasing StartILOffset so "earliest" is well-defined.
    const std::uint32_t offs[6] = { 0, 0x10, 0x20, 0x30, 0x40, 0x50 };
    for (int i = 0; i < 6; ++i) fn->Body->Blocks[i]->StartILOffset = offs[i];

    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(b1));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<IfInstruction>(cd(), std::make_unique<Branch>(b4),
                                                                  std::make_unique<Branch>(b2)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<IfInstruction>(cd(), std::make_unique<Branch>(b5),
                                                                  std::make_unique<Branch>(b3)));
    fn->Body->Blocks[3]->SetFinal(std::make_unique<Branch>(b1));
    fn->Body->Blocks[4]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->Blocks[5]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // A Loop container was constructed. Find the pre-header block -- the one (in
    // any container) that directly owns a Loop-kind container as a child. Its
    // FinalInstruction is the synthesized loop-exit branch; it must target b4
    // (the earliest out-of-loop successor), never the later b5.
    Block* preHeader = nullptr;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        auto* blk = dynamic_cast<Block*>(i);
        if (!blk) return;
        // A block's Instruction that is a Loop container marks this block as the
        // pre-header (ConstructLoop replaces the header's content list with the
        // loop container, leaving a trailing exit Branch as the final).
        for (const auto& inst : blk->Instructions) {
            if (auto* c = dynamic_cast<BlockContainer*>(inst.get()))
                if (c->Kind == ContainerKind::Loop) preHeader = blk;
        }
    });
    ASSERT_NE(preHeader, nullptr) << "the loop must be detected and wrapped";
    ASSERT_TRUE(preHeader->FinalInstruction) << "pre-header must end in an exit branch";
    ASSERT_EQ(preHeader->FinalInstruction->Op, OpCode::Branch)
        << "pre-header final is the synthesized loop-exit branch";
    auto* exitBr = static_cast<Branch*>(preHeader->FinalInstruction.get());
    EXPECT_EQ(exitBr->TargetBlock, b4)
        << "the exit branch must target the earliest out-of-loop successor (b4, "
           "offset 0x40), not the later b5 (offset 0x50): the exit pick must be "
           "deterministic, not pointer-order-dependent";
    EXPECT_NE(exitBr->TargetBlock, b5) << "the later exit b5 must not be chosen";
}

TEST(LoopDetection, PrefersConvergenceExitOverEarlyBreakPath) {
    // The GetCVTypeFromClass shape (mscorlib): a while loop with a bottom
    // condition and an early break to a match-block that has a LOWER StartILOffset
    // than the true post-loop convergence.
    //   b0(0x00): br b4                    (pretest, enter at condition)
    //   b1(0x06): if (!c) br b3  else fallthrough b2
    //   b2(0x15): br b5                    (match path: lower offset)
    //   b3(0x19): br b4                    (increment; back edge b3->b4: b4 dominates b3)
    //   b4(0x1D): if (c) br b1 else fallthrough b5   (condition; loop header)
    //   b5(0x27): leave                    (true post-loop convergence -- b2 flows to b5)
    // Natural loop of back edge b3->b4 = {b4,b1,b3}; its out-of-loop successors are
    // b2 (0x15) and b5 (0x27). The correct exit is the CONVERGENCE b5 (b2 reaches
    // b5), not the lowest-offset b2. A pure min-offset pick wrongly exits to the
    // match path and mangles the loop. FindExitPoint must prefer the candidate all
    // other candidates converge to; min-offset only breaks ties.
    auto fn = WrapBlocks({});
    for (int i = 0; i < 6; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    const std::uint32_t offs[6] = { 0x00, 0x06, 0x15, 0x19, 0x1D, 0x27 };
    for (int i = 0; i < 6; ++i) fn->Body->Blocks[i]->StartILOffset = offs[i];
    auto cd = [] {
        return std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                                      ComparisonKind::Inequality);
    };
    Block* b1 = fn->Body->Blocks[1].get();
    Block* b2 = fn->Body->Blocks[2].get();
    Block* b3 = fn->Body->Blocks[3].get();
    Block* b4 = fn->Body->Blocks[4].get();
    Block* b5 = fn->Body->Blocks[5].get();

    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(b4));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<IfInstruction>(cd(), std::make_unique<Branch>(b3)));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Branch>(b5));
    fn->Body->Blocks[3]->SetFinal(std::make_unique<Branch>(b4));
    fn->Body->Blocks[4]->SetFinal(std::make_unique<IfInstruction>(cd(), std::make_unique<Branch>(b1)));
    fn->Body->Blocks[5]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LoopDetection().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Find the pre-header (the block owning a Loop container) and assert its
    // synthesized exit branch targets the convergence b5, not the lower-offset
    // break path b2.
    Block* preHeader = nullptr;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* blk = dynamic_cast<Block*>(i))
            for (const auto& inst : blk->Instructions)
                if (auto* c = dynamic_cast<BlockContainer*>(inst.get()))
                    if (c->Kind == ContainerKind::Loop) preHeader = blk;
    });
    ASSERT_NE(preHeader, nullptr) << "the loop must be detected and wrapped";
    ASSERT_TRUE(preHeader->FinalInstruction);
    ASSERT_EQ(preHeader->FinalInstruction->Op, OpCode::Branch)
        << "pre-header final must be the synthesized loop-exit branch";
    auto* exitBr = static_cast<Branch*>(preHeader->FinalInstruction.get());
    EXPECT_EQ(exitBr->TargetBlock, b5)
        << "exit must be the loop convergence b5 (0x27); b2 reaches b5, so b5 is the "
           "post-dominating exit, not the lower-offset break path b2 (0x15)";
    EXPECT_NE(exitBr->TargetBlock, b2) << "must not exit into the early break/match path";
}
