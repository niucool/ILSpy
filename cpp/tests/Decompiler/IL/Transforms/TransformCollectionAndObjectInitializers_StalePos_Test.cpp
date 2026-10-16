// Copyright (c) 2026 Jim Hester
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <gtest/gtest.h>

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"

namespace {

using namespace ILSpy::Decompiler;

// A previous per-statement child's fold can shrink the block after it captured
// the rerun position (the harness corpus hits this through the inc/dec folds
// followed by the collection-initializer child). The transform's entry must
// treat a position past the block end as a no-op instead of reading past the
// instruction vector.
TEST(TransformCollectionAndObjectInitializersTest, StalePositionPastBlockEndIsNoop)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto container = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(container);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    block->Add(std::make_unique<IL::StLoc>(
        nullptr, std::make_unique<IL::LdNull>()));
    block->SetFinal(std::make_unique<IL::Nop>());
    blockPtr->Parent = fn->Body.get();
    fn->Body->AddBlock(std::move(block));

    IL::ILTransformContext ctx;
    ctx.Settings.ObjectOrCollectionInitializers = true;
    IL::TransformCollectionAndObjectInitializers transform;
    // pos == size: the stale-rerun shape the corpus produced.
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    transform.Run(*blockPtr, 1, driverCtx);
    EXPECT_EQ(blockPtr->Instructions.size(), 1u);
}

} // namespace
