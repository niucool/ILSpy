// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or permit persons to whom the Software is furnished to
// do so, subject to the following conditions:
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

// ControlFlowSimplification tests: branch-chain collapsing, dead stack-slot
// store removal, debug-mode return-block inlining, and next-block combining --
// the first transform of the ILAst pipeline. Hand-built trees pin the rewrite
// shapes; the mscorlib sweep pins the global contract (no branch targets an
// empty chain block afterwards).

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeVar(VariableKind kind, std::string name, std::int32_t index) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = kind;
    v->Index = index;
    return v;
}

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

// True iff the block is a pure branch trampoline: no statements, final Branch.
bool IsChainBlock(const Block* b) {
    return b && b->Instructions.empty() && b->FinalInstruction &&
           b->FinalInstruction->Op == OpCode::Branch;
}

ILTransformContext& TestContext() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(ControlFlowSimplification, CollapsesBranchChainsAndCombinesBlocks) {
    // b0: V_0 = 1; br b1     b1: br b2     b2: V_1 = 2; leave
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                    std::make_unique<LdcI4>(1)));
    auto b1 = std::make_unique<Block>();
    auto b2 = std::make_unique<Block>();
    b2->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_1", 1),
                                    std::make_unique<LdcI4>(2)));

    auto fn = WrapBlocks({});
    Block* b2p = b2.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Branch>(fn->Body->Blocks[1].get()));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Branch>(b2p));
    fn->Body->Blocks[2]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ControlFlowSimplification().Run(*fn, TestContext());
    fn->CheckInvariant(ILPhase::Normal);

    // b0 -> b1 -> b2 collapses, then the combined single-edge blocks merge:
    // one block remains holding both statements and the leave.
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    const auto& only = fn->Body->Blocks[0];
    ASSERT_EQ(only->Instructions.size(), 2u);
    EXPECT_EQ(only->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(only->Instructions[1]->Op, OpCode::StLoc);
    ASSERT_NE(only->FinalInstruction, nullptr);
    EXPECT_EQ(only->FinalInstruction->Op, OpCode::Leave);
}

TEST(ControlFlowSimplification, InlinesReturnBlockVariable) {
    // Debug-mode return block: v = <value>; leave v  ->  leave <value>.
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(42)));

    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(b0));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(local)));
    fn->CheckInvariant(ILPhase::Normal);

    ControlFlowSimplification().Run(*fn, TestContext());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    EXPECT_TRUE(only->Instructions.empty());
    auto* leave = dynamic_cast<Leave*>(only->FinalInstruction.get());
    ASSERT_NE(leave, nullptr);
    ASSERT_NE(leave->Value, nullptr);
    EXPECT_EQ(leave->Value->Op, OpCode::LdcI4);
}

TEST(ControlFlowSimplification, RemovesDeadStackSlotStores) {
    auto stackSlot = MakeVar(VariableKind::StackSlot, "S_0", -1);
    auto arg = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto realLocal = MakeVar(VariableKind::Local, "V_1", 1);
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(stackSlot, std::make_unique<LdLoc>(arg)));
    b0->Add(std::make_unique<StLoc>(realLocal, std::make_unique<LdcI4>(3)));

    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(b0));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ControlFlowSimplification().Run(*fn, TestContext());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    ASSERT_EQ(only->Instructions.size(), 1u) << "dead S_0 store must be removed";
    auto* st = dynamic_cast<StLoc*>(only->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable->Name, "V_1") << "the local store survives";
}

TEST(ControlFlowSimplification, ReplacesBranchToLeaveWithLeave) {
    // b0: if (1 == 1) br b1;  b1: leave
    auto b0 = std::make_unique<Block>();
    auto b1 = std::make_unique<Block>();

    auto fn = WrapBlocks({});
    Block* b1p = b1.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(b1p)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ControlFlowSimplification().Run(*fn, TestContext());
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    EXPECT_EQ(iff->TrueInst->Op, OpCode::Leave) << "branch-to-leave duplicates the leave";
}

TEST(ControlFlowSimplification, NoBranchTargetsChainBlocksAfterwardOnMscorlib) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int transformed = 0;
    int chainsLeft = 0;
    int blocksBefore = 0;
    int blocksAfter = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        int before = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::Block) ++before; });
        ControlFlowSimplification().Run(*fn, TestContext());
        fn->CheckInvariant(ILPhase::Normal);
        ++transformed;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::Block) ++blocksAfter;
            if (auto* br = dynamic_cast<Branch*>(i)) {
                if (br->TargetBlock && IsChainBlock(br->TargetBlock)) ++chainsLeft;
            }
        });
        blocksBefore += before;
        if (transformed >= 8000) break;
    }
    EXPECT_GT(transformed, 5000);
    EXPECT_EQ(chainsLeft, 0) << "branch chains must collapse";
    EXPECT_LT(blocksAfter, blocksBefore) << "empty blocks must merge away";
}
