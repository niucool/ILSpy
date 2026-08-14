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

// RemoveUnreachableBlocks tests: a block with no reachable path from the
// container entry (a loop body that branches back to the header, leaving its
// fall-through successor unreachable; an inlined fall-through leaving the
// original next block unreachable) is dropped, so the seed does not render it
// as dead code after a `return;`/`continue;`/`throw`.

#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"
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

} // namespace

TEST(RemoveUnreachableBlocks, DropsUnreachableBlockAfterUnconditionalBranch) {
    // b0: work; br b1   b1: leave (return)   b2: DEAD (no predecessor -- b0
    // branches to b1, b1 leaves; nothing reaches b2). b2 must be dropped.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto b0 = std::make_unique<Block>(); Block* b0p = b0.get();
    auto b1 = std::make_unique<Block>(); Block* b1p = b1.get();
    auto b2 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    b2->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));  // dead
    auto fn = WrapBlocks({});
    b0->SetFinal(std::make_unique<Branch>(b1p));
    b1->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RemoveUnreachableBlocks().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(fn->Body->Blocks.size(), 2u) << "the dead b2 must be dropped";
    EXPECT_EQ(fn->Body->Blocks[0].get(), b0p);
    EXPECT_EQ(fn->Body->Blocks[1].get(), b1p);
}

TEST(RemoveUnreachableBlocks, KeepsBlockReachedByForwardBranch) {
    // b0: if (cond) br b2   b1: br b2   b2: leave. b1 is reachable via b0's
    // fall-through (the if has no else, so cond-false falls to b1). b2 is
    // reachable via both branches. Nothing is dropped.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto b0 = std::make_unique<Block>(); Block* b0p = b0.get();
    auto b1 = std::make_unique<Block>(); Block* b1p = b1.get();
    auto b2 = std::make_unique<Block>(); Block* b2p = b2.get();
    b0->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(b2p)));
    b1->SetFinal(std::make_unique<Branch>(b2p));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    b2p->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RemoveUnreachableBlocks().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fn->Body->Blocks.size(), 3u) << "all three blocks are reachable";
}
