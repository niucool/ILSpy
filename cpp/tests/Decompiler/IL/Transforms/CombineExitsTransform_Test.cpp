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

// Tests for CombineExitsTransform -- the compact early fold
// `if (cond) leave(a); leave(b)` -> `leave (cond ? a : b)` that the C#
// DecompileBodyForAnalysis prefix uses so a release-mode `return a && b` body is
// one statement. The hand-built tests cover the simple combine, the nested-else
// recursion, the RunOnSingleStatement post-fold (the condition comp(x != 0) is
// folded to x), and the negative guards (multi-block body, non-empty else arm,
// non-leave true arm, Nop leave value, non-function leave target).

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <gtest/gtest.h>

#include <memory>

using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockContainer;
using ILSpy::Decompiler::IL::CombineExitsTransform;
using ILSpy::Decompiler::IL::Comp;
using ILSpy::Decompiler::IL::ComparisonKind;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::IfInstruction;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::VariableKind;

namespace {

std::unique_ptr<LdcI4> I4(int v) { return std::make_unique<LdcI4>(v); }

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// A one-block function; `block` receives the entry block.
std::unique_ptr<ILFunction> MakeFn(Block*& block) {
    auto container = std::make_unique<BlockContainer>();
    auto b = std::make_unique<Block>();
    block = b.get();
    container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

void RunTransform(ILFunction& fn) {
    ILTransformContext ctx;
    CombineExitsTransform().Run(fn, ctx);
}

// The fold result: a Leave whose Value is an IfInstruction, or null.
IfInstruction* CombinedIf(Block* block) {
    auto* leave = dynamic_cast<Leave*>(block->FinalInstruction.get());
    if (!leave) return nullptr;
    return dynamic_cast<IfInstruction*>(leave->Value.get());
}

} // namespace

// ---- Positive: the simple tail folds ----

TEST(CombineExitsTransform, SimpleIfLeaveThenLeaveFolds) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    auto cond = I4(1);
    auto trueLeave = std::make_unique<Leave>(body, I4(10));
    block->Add(std::make_unique<IfInstruction>(std::move(cond), std::move(trueLeave)));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    // The if is gone; the final is a single leave carrying the conditional.
    EXPECT_TRUE(block->Instructions.empty());
    auto* combined = CombinedIf(block);
    ASSERT_NE(combined, nullptr);
    EXPECT_EQ(combined->Condition->Op, ILSpy::Decompiler::IL::OpCode::LdcI4);
    ASSERT_NE(dynamic_cast<LdcI4*>(combined->TrueInst.get()), nullptr);
    ASSERT_NE(dynamic_cast<LdcI4*>(combined->FalseInst.get()), nullptr);
    EXPECT_EQ(static_cast<LdcI4*>(combined->TrueInst.get())->Value, 10);
    EXPECT_EQ(static_cast<LdcI4*>(combined->FalseInst.get())->Value, 20);
}

TEST(CombineExitsTransform, NestedBlockArmRecurses) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    // Outer: if (cond) { if (cond2) leave(a); leave(b) } leave(c)
    auto nested = std::make_unique<Block>();
    nested->Add(std::make_unique<IfInstruction>(I4(1),
        std::make_unique<Leave>(body, I4(1))));
    nested->SetFinal(std::make_unique<Leave>(body, I4(2)));
    block->Add(std::make_unique<IfInstruction>(I4(1), std::move(nested)));
    block->SetFinal(std::make_unique<Leave>(body, I4(3)));

    RunTransform(*fn);

    EXPECT_TRUE(block->Instructions.empty());
    auto* outer = CombinedIf(block);
    ASSERT_NE(outer, nullptr);
    // The true arm is the nested conditional; the false arm is the outer else.
    auto* inner = dynamic_cast<IfInstruction*>(outer->TrueInst.get());
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(dynamic_cast<LdcI4*>(inner->TrueInst.get()), nullptr);
    ASSERT_NE(dynamic_cast<LdcI4*>(inner->FalseInst.get()), nullptr);
    EXPECT_EQ(static_cast<LdcI4*>(inner->TrueInst.get())->Value, 1);
    EXPECT_EQ(static_cast<LdcI4*>(inner->FalseInst.get())->Value, 2);
    ASSERT_NE(dynamic_cast<LdcI4*>(outer->FalseInst.get()), nullptr);
    EXPECT_EQ(static_cast<LdcI4*>(outer->FalseInst.get())->Value, 3);
}

TEST(CombineExitsTransform, RunsExpressionTransformsOnCombinedExit) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    auto x = MakeLocal("x");
    // if (comp(x != 0)) leave(a); leave(b)
    block->Add(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(x), I4(0),
                               ComparisonKind::Inequality),
        std::make_unique<Leave>(body, I4(10))));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    auto* combined = CombinedIf(block);
    ASSERT_NE(combined, nullptr);
    // ExpressionTransforms folded `comp(x != 0)` to `x` (the condition slot).
    auto* ldloc = dynamic_cast<LdLoc*>(combined->Condition.get());
    ASSERT_NE(ldloc, nullptr);
    EXPECT_EQ(ldloc->Variable.get(), x.get());
}

// ---- Negative guards ----

TEST(CombineExitsTransform, MultiBlockBodyIsIgnored) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    body->AddBlock(std::make_unique<Block>());
    block->Add(std::make_unique<IfInstruction>(I4(1),
        std::make_unique<Leave>(body, I4(10))));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(block->FinalInstruction->Op, ILSpy::Decompiler::IL::OpCode::Leave);
    EXPECT_EQ(dynamic_cast<Leave*>(block->FinalInstruction.get())->Value->Op,
              ILSpy::Decompiler::IL::OpCode::LdcI4);
}

TEST(CombineExitsTransform, NonEmptyElseArmIsIgnored) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    block->Add(std::make_unique<IfInstruction>(I4(1),
        std::make_unique<Leave>(body, I4(10)), I4(99)));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(CombinedIf(block), nullptr);
}

TEST(CombineExitsTransform, NonLeaveTrueArmIsIgnored) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    block->Add(std::make_unique<IfInstruction>(I4(1), I4(10)));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(CombinedIf(block), nullptr);
}

TEST(CombineExitsTransform, NopLeaveValueIsIgnored) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    // The true leave has no value (the port's Nop shape).
    block->Add(std::make_unique<IfInstruction>(I4(1),
        std::make_unique<Leave>(body, nullptr)));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(CombinedIf(block), nullptr);
}

TEST(CombineExitsTransform, NonFunctionLeaveTargetIsIgnored) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    // A detached container (parent is not an ILFunction) is not a function exit.
    auto detached = std::make_unique<BlockContainer>();
    block->Add(std::make_unique<IfInstruction>(I4(1),
        std::make_unique<Leave>(detached.get(), I4(10))));
    block->SetFinal(std::make_unique<Leave>(body, I4(20)));

    RunTransform(*fn);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(CombinedIf(block), nullptr);
}
