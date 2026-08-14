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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// DetectExitPoints tests: an inner Branch to a loop's exit block (the block
// after the loop) is replaced with a Leave(loop), so the following
// ConditionDetection can restructure `if (cond) leave` patterns.

#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
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
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>

using namespace ILSpy::Decompiler::IL;

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

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

TEST(DetectExitPoints, ReplacesInnerBranchToLoopExitWithLeave) {
    // A Loop container (holder block + exit block) whose body has an inner
    // `if (cond) br exit` (skip to the exit). DetectExitPoints replaces the
    // inner br-exit with a Leave(loop).
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = WrapBlocks({});
    // main: [loopHolder (Loop{ header, body }), exitB]
    auto loopHolder = std::make_unique<Block>(); Block* loopHolderPtr = loopHolder.get();
    auto exitB = std::make_unique<Block>(); Block* exitPtr = exitB.get();
    fn->Body->AddBlock(std::move(loopHolder));
    fn->Body->AddBlock(std::move(exitB));

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::Loop;
    BlockContainer* loopPtr = loopC.get();
    // header: if (num) br body else leave(loop)
    auto header = std::make_unique<Block>(); Block* headerPtr = header.get();
    auto body = std::make_unique<Block>(); Block* bodyPtr = body.get();
    header->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(bodyPtr), std::make_unique<Leave>(nullptr)));
    loopC->AddBlock(std::move(header));
    // body: if (num) br exit (the inner exit branch); then br header (back-edge)
    body->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(exitPtr)));
    // We also need a back-edge; append a second block that branches to header.
    auto back = std::make_unique<Block>();
    back->SetFinal(std::make_unique<Branch>(headerPtr));
    loopC->AddBlock(std::move(body));
    loopC->AddBlock(std::move(back));
    // Patch the header's leave target + the if's true-arm (already bodyPtr).
    dynamic_cast<Leave*>(static_cast<IfInstruction*>(loopPtr->Blocks[0]->FinalInstruction.get())->FalseInst.get())->TargetContainer = loopPtr;
    loopHolderPtr->Add(std::move(loopC));
    loopHolderPtr->SetFinal(std::make_unique<Branch>(exitPtr));  // falls to exit
    exitPtr->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    DetectExitPoints().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The inner `br exit` (in the body's if true-arm) must now be a Leave(loop).
    int leaveCount = 0;
    int branchToExit = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (auto* lv = dynamic_cast<Leave*>(i)) {
            if (lv->TargetContainer == loopPtr) ++leaveCount;
        }
        if (auto* br = dynamic_cast<Branch*>(i)) {
            if (br->TargetBlock == exitPtr) ++branchToExit;
        }
    });
    // header's false-arm leave + the converted inner branch -> at least 2 leaves.
    EXPECT_GE(leaveCount, 2) << "the inner br-exit must become a Leave(loop)";
    // The body's inner branch to exit is gone (only the loopHolder's final br-exit
    // and the back-edge remain as branches to exit/header).
    EXPECT_EQ(branchToExit, 1) << "only the loopHolder's fall-through br to exit remains";
}
