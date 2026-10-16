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

// Tests for SwitchDetection.LoopContext: the continue/break analysis over a
// per-container control-flow graph that the (deferred) SwitchDetection.Run
// needs to decide whether a detected if-chain can become a `switch` without
// gotos. A `continue;` jumps to the loop's increment block (for loop) or
// do-while condition block; LoopContext maps each such back-edge block to its
// continue depth and lists the blocks a `break;` would target. The tests build
// real CFGs (the production pre-LoopDetection shape: flat containers with
// back-edges as Branches) and verify the continue/break classification.

#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_set>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::FlowAnalysis::ControlFlowNode;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

std::unique_ptr<StLoc> MakeIncrement(ILVariablePtr v, int step) {
    auto add = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(step),
        BinaryNumericOperator::Add, StackType::I4);
    return std::make_unique<StLoc>(v, std::move(add));
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A for-loop container: head -> body; body -> {exit (if break), incr (fall-
// through)}; incr -> head (back-edge). The increment block is the `continue;`
// target. Built as the production pre-LoopDetection shape (flat blocks, the
// back-edge is a Branch to the head).
struct ForLoop {
    std::unique_ptr<ILFunction> fn;
    Block* head;
    Block* body;
    Block* incr;
    Block* exit;
    ILVariablePtr v;

    ForLoop() {
        fn = std::make_unique<ILFunction>();
        fn->Body = std::make_unique<BlockContainer>();
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        v = MakeLocal("v");
        fn->Variables.push_back(v);
        for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
        head = fn->Body->Blocks[0].get();
        body = fn->Body->Blocks[1].get();
        incr = fn->Body->Blocks[2].get();
        exit = fn->Body->Blocks[3].get();
        head->SetFinal(std::make_unique<Branch>(body));
        // if (v == 0) br exit; fall-through to incr
        body->SetFinal(std::make_unique<IfInstruction>(
            std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                                   ComparisonKind::Equality),
            std::make_unique<Branch>(exit), nullptr));
        incr->Add(MakeIncrement(v, 1));
        incr->SetFinal(std::make_unique<Branch>(head));
        exit->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        RecomputeIncomingEdgeCounts(*fn);
    }
};

// A do-while container: head -> body -> cond; cond -> {head (back-edge on
// true), exit (fall-through)}. The condition block is the `continue;` target.
struct DoWhileLoop {
    std::unique_ptr<ILFunction> fn;
    Block* head;
    Block* body;
    Block* cond;
    Block* exit;
    ILVariablePtr v;

    DoWhileLoop() {
        fn = std::make_unique<ILFunction>();
        fn->Body = std::make_unique<BlockContainer>();
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        v = MakeLocal("v");
        fn->Variables.push_back(v);
        for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
        head = fn->Body->Blocks[0].get();
        body = fn->Body->Blocks[1].get();
        cond = fn->Body->Blocks[2].get();
        exit = fn->Body->Blocks[3].get();
        head->SetFinal(std::make_unique<Branch>(body));
        body->SetFinal(std::make_unique<Branch>(cond));
        // if (v) br head; fall-through to exit
        cond->SetFinal(std::make_unique<IfInstruction>(
            std::make_unique<LdLoc>(v), std::make_unique<Branch>(head), nullptr));
        exit->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        RecomputeIncomingEdgeCounts(*fn);
    }
};

} // namespace

// In a for loop, the increment block (the back-edge source the head dominates)
// is the continue target at depth 1; the body, head, and exit are not.
TEST(LoopContext, ForLoopIncrementBlockIsContinueTarget) {
    ForLoop fl;
    fl.fn->CheckInvariant(ILPhase::Normal);
    ControlFlowGraph cfg(fl.fn->Body.get());
    auto* bodyNode = cfg.GetNode(fl.body);
    ASSERT_TRUE(bodyNode);
    SwitchDetection::LoopContext lc(cfg, bodyNode);

    auto* incrNode = cfg.GetNode(fl.incr);
    auto* headNode = cfg.GetNode(fl.head);
    auto* exitNode = cfg.GetNode(fl.exit);
    ASSERT_TRUE(incrNode);
    ASSERT_TRUE(headNode);
    ASSERT_TRUE(exitNode);

    EXPECT_TRUE(lc.MatchContinue(incrNode));
    EXPECT_EQ(lc.GetContinueDepth(incrNode), 1);
    EXPECT_TRUE(lc.MatchContinue(incrNode, 1));
    EXPECT_FALSE(lc.MatchContinue(incrNode, 2));

    EXPECT_FALSE(lc.MatchContinue(bodyNode));
    EXPECT_EQ(lc.GetContinueDepth(bodyNode), 0);
    EXPECT_FALSE(lc.MatchContinue(headNode));
    EXPECT_FALSE(lc.MatchContinue(exitNode));
}

// In a do-while loop, the condition block (the back-edge source) is the
// continue target at depth 1.
TEST(LoopContext, DoWhileConditionBlockIsContinueTarget) {
    DoWhileLoop dw;
    dw.fn->CheckInvariant(ILPhase::Normal);
    ControlFlowGraph cfg(dw.fn->Body.get());
    auto* bodyNode = cfg.GetNode(dw.body);
    ASSERT_TRUE(bodyNode);
    SwitchDetection::LoopContext lc(cfg, bodyNode);

    auto* condNode = cfg.GetNode(dw.cond);
    auto* headNode = cfg.GetNode(dw.head);
    auto* exitNode = cfg.GetNode(dw.exit);
    ASSERT_TRUE(condNode);
    ASSERT_TRUE(headNode);
    ASSERT_TRUE(exitNode);

    EXPECT_TRUE(lc.MatchContinue(condNode));
    EXPECT_EQ(lc.GetContinueDepth(condNode), 1);
    EXPECT_FALSE(lc.MatchContinue(bodyNode));
    EXPECT_FALSE(lc.MatchContinue(headNode));
    EXPECT_FALSE(lc.MatchContinue(exitNode));
}

// A node with no enclosing loop (the context node's successors never reach a
// dominator) has no continue targets.
TEST(LoopContext, NoLoopHasNoContinueTargets) {
    DoWhileLoop dw;
    ControlFlowGraph cfg(dw.fn->Body.get());
    // The exit block is outside the loop; its successors never dominate it.
    auto* exitNode = cfg.GetNode(dw.exit);
    ASSERT_TRUE(exitNode);
    SwitchDetection::LoopContext lc(cfg, exitNode);
    EXPECT_FALSE(lc.MatchContinue(cfg.GetNode(dw.head)));
    EXPECT_FALSE(lc.MatchContinue(cfg.GetNode(dw.body)));
    EXPECT_FALSE(lc.MatchContinue(cfg.GetNode(dw.cond)));
    EXPECT_EQ(lc.GetContinueDepth(exitNode), 0);
}

// GetBreakTargets walks a dominator subtree and returns the successors that
// leave it (a `break;` exits to a block the dominator does not dominate).
// Here A dominates B (A->B->C) but not C (C is also reachable directly from the
// entry, bypassing A), so GetBreakTargets(A) returns C.
TEST(LoopContext, GetBreakTargetsReturnsSuccessorsLeavingSubtree) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* entry = fn->Body->Blocks[0].get();
    Block* a = fn->Body->Blocks[1].get();
    Block* b = fn->Body->Blocks[2].get();
    Block* c = fn->Body->Blocks[3].get();
    // entry: if (v) br c; fall-through to a  -> entry -> {c, a}
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(c), nullptr));
    a->SetFinal(std::make_unique<Branch>(b));     // a -> b
    b->SetFinal(std::make_unique<Branch>(c));     // b -> c
    c->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    ControlFlowGraph cfg(fn->Body.get());
    auto* aNode = cfg.GetNode(a);
    ASSERT_TRUE(aNode);
    SwitchDetection::LoopContext lc(cfg, aNode);  // no loop around A
    auto breaks = lc.GetBreakTargets(aNode);

    ASSERT_EQ(breaks.size(), 1u);
    EXPECT_EQ(breaks[0]->UserData, static_cast<void*>(c));
}

// On the real mscorlib corpus, constructing LoopContext over every container
// (at the pipeline position where SwitchDetection.Run will sit -- after the
// second CFS, before LoopDetection, so loops are still flat back-edges) must
// not crash, must preserve the ILAst invariant, and must find real loops
// (continue targets) in the corpus.
TEST(LoopContext, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int loopsFound = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Run the pre-pipeline through the second CFS -- the point where
        // SwitchDetection.Run sits (before LoopDetection, so loops are flat
        // back-edges the CFG/dominance analysis LoopContext walks).
        ControlFlowSimplification().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        EarlyExpressionTransforms().Run(*fn, ctx);
        RemoveDeadVariableInit().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);

        // For each container, build the CFG and, for each block that is inside
        // a loop (a successor dominates it -- a back-edge to a loop head),
        // construct LoopContext and count the loops it finds.
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            auto* container = dynamic_cast<BlockContainer*>(i);
            if (!container || container->Blocks.empty()) return;
            ControlFlowGraph cfg(container);
            for (auto& block : container->Blocks) {
                auto* n = cfg.GetNode(block.get());
                if (!n) continue;
                bool inLoop = false;
                for (auto* s : n->Successors) {
                    if (s && s->Dominates(n)) { inLoop = true; break; }
                }
                if (!inLoop) continue;
                SwitchDetection::LoopContext lc(cfg, n);
                for (auto& b2 : container->Blocks) {
                    auto* bn = cfg.GetNode(b2.get());
                    if (bn && lc.GetContinueDepth(bn) > 0) {
                        ++loopsFound;
                        break;
                    }
                }
                break;  // one LoopContext per container is enough to count it
            }
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(loopsFound, 0) << "mscorlib must have loops LoopContext recognizes";
}
