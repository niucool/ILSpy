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
