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
