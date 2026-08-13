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

// Tests for the HighLevelLoopTransform static helper subset (MatchIncrement,
// MatchIncrementBlock, MatchDoWhileConditionBlock, IsSimpleStatement): the
// shape matchers that identify the back-edge blocks a C# `continue;` jumps to,
// adapted to this port's if-as-final block model. These are the foundation
// SwitchDetection.LoopContext (and the later while/for loop transform) depend
// on; the full HighLevelLoopTransform is deferred.

#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace ILSpy::Decompiler::IL;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// stloc v(add(ldloc v, ldc.i4 step)) -- a numeric loop increment.
std::unique_ptr<StLoc> MakeIncrement(ILVariablePtr v, int step) {
    auto add = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(step),
        BinaryNumericOperator::Add, StackType::I4);
    return std::make_unique<StLoc>(v, std::move(add));
}

} // namespace

// stloc v(add(ldloc v, 1)) matches: the increment variable is v.
TEST(HighLevelLoopTransform, MatchIncrementRecognizesStlocAddLdLoc) {
    auto v = MakeLocal("v");
    auto st = MakeIncrement(v, 1);
    ILVariablePtr out;
    EXPECT_TRUE(HighLevelLoopTransform::MatchIncrement(st.get(), out));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.get(), v.get());
}

// stloc v(add(ldloc w, 1)) does not match: the add's left is ldloc w, not v.
TEST(HighLevelLoopTransform, MatchIncrementRejectsAddOfOtherVariable) {
    auto v = MakeLocal("v");
    auto w = MakeLocal("w");
    auto add = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(w), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I4);
    auto st = std::make_unique<StLoc>(v, std::move(add));
    ILVariablePtr out;
    EXPECT_FALSE(HighLevelLoopTransform::MatchIncrement(st.get(), out));
}

// stloc v(sub(ldloc v, 1)) does not match: the operator is Sub, not Add.
TEST(HighLevelLoopTransform, MatchIncrementRejectsNonAddOperator) {
    auto v = MakeLocal("v");
    auto sub = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Sub, StackType::I4);
    auto st = std::make_unique<StLoc>(v, std::move(sub));
    ILVariablePtr out;
    EXPECT_FALSE(HighLevelLoopTransform::MatchIncrement(st.get(), out));
}

// A non-StLoc instruction does not match.
TEST(HighLevelLoopTransform, MatchIncrementRejectsNonStloc) {
    auto v = MakeLocal("v");
    auto ld = std::make_unique<LdLoc>(v);
    ILVariablePtr out;
    EXPECT_FALSE(HighLevelLoopTransform::MatchIncrement(ld.get(), out));
}

// A block whose final is a Branch to the loop head and whose other instructions
// are all simple (a stloc increment) matches MatchIncrementBlock.
TEST(HighLevelLoopTransform, MatchIncrementBlockRecognizesBranchAndSimpleStatements) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* head = fn->Body->Blocks[0].get();
    Block* incr = fn->Body->Blocks[1].get();
    incr->Add(MakeIncrement(v, 1));
    incr->SetFinal(std::make_unique<Branch>(head));
    head->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    Block* loopHead = nullptr;
    EXPECT_TRUE(HighLevelLoopTransform::MatchIncrementBlock(incr, loopHead));
    EXPECT_EQ(loopHead, head);
    fn->CheckInvariant(ILPhase::Normal);
}

// A block whose final is a Branch but that carries a non-simple statement (a
// bare LdLoc) does not match.
TEST(HighLevelLoopTransform, MatchIncrementBlockRejectsNonSimpleStatement) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* head = fn->Body->Blocks[0].get();
    Block* incr = fn->Body->Blocks[1].get();
    incr->Add(std::make_unique<LdLoc>(v));  // LdLoc is not a simple statement
    incr->SetFinal(std::make_unique<Branch>(head));
    head->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    Block* loopHead = nullptr;
    EXPECT_FALSE(HighLevelLoopTransform::MatchIncrementBlock(incr, loopHead));
    fn->CheckInvariant(ILPhase::Normal);
}

// A block whose final is not a Branch (a Leave) does not match.
TEST(HighLevelLoopTransform, MatchIncrementBlockRejectsNonBranchFinal) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    fn->Body->AddBlock(std::make_unique<Block>());
    Block* b = fn->Body->Blocks[0].get();
    b->Add(MakeIncrement(v, 1));
    b->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // not a Branch
    RecomputeIncomingEdgeCounts(*fn);

    Block* loopHead = nullptr;
    EXPECT_FALSE(HighLevelLoopTransform::MatchIncrementBlock(b, loopHead));
    fn->CheckInvariant(ILPhase::Normal);
}

// A block whose final is an if with no else, true arm a Branch to the loop head
// and fall-through to the next block, matches MatchDoWhileConditionBlock:
// target1 = the if's true-branch target, target2 = the next block.
TEST(HighLevelLoopTransform, MatchDoWhileConditionBlockRecognizesIfAndFallThrough) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* head = fn->Body->Blocks[0].get();
    Block* cond = fn->Body->Blocks[1].get();
    Block* exit = fn->Body->Blocks[2].get();
    // if (v) br head; fall-through to exit
    cond->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(head), nullptr));
    head->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    exit->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    Block* t1 = nullptr;
    Block* t2 = nullptr;
    EXPECT_TRUE(HighLevelLoopTransform::MatchDoWhileConditionBlock(cond, t1, t2));
    EXPECT_EQ(t1, head);   // true-branch target
    EXPECT_EQ(t2, exit);   // fall-through (next block in container)
    fn->CheckInvariant(ILPhase::Normal);
}

// An if with an else (FalseInst non-null) is not a do-while condition.
TEST(HighLevelLoopTransform, MatchDoWhileConditionBlockRejectsIfWithElse) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* head = fn->Body->Blocks[0].get();
    Block* cond = fn->Body->Blocks[1].get();
    Block* other = fn->Body->Blocks[2].get();
    // if (v) br head else br other -- has an else, not a do-while condition
    cond->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(head),
        std::make_unique<Branch>(other)));
    head->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    other->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    Block* t1 = nullptr;
    Block* t2 = nullptr;
    EXPECT_FALSE(HighLevelLoopTransform::MatchDoWhileConditionBlock(cond, t1, t2));
    fn->CheckInvariant(ILPhase::Normal);
}

// A block whose final is a Branch (not an if) is not a do-while condition.
TEST(HighLevelLoopTransform, MatchDoWhileConditionBlockRejectsNonIfFinal) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* a = fn->Body->Blocks[0].get();
    Block* b = fn->Body->Blocks[1].get();
    a->SetFinal(std::make_unique<Branch>(b));
    b->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    Block* t1 = nullptr;
    Block* t2 = nullptr;
    EXPECT_FALSE(HighLevelLoopTransform::MatchDoWhileConditionBlock(a, t1, t2));
    fn->CheckInvariant(ILPhase::Normal);
}

// IsSimpleStatement recognizes calls and stores; loads and control flow do not.
TEST(HighLevelLoopTransform, IsSimpleStatementClassifiesCallsAndStores) {
    auto v = MakeLocal("v");
    auto stloc = std::make_unique<StLoc>(v, std::make_unique<LdLoc>(v));
    auto call = std::make_unique<Call>("System.Console::WriteLine");
    auto ldloc = std::make_unique<LdLoc>(v);
    auto branch = std::make_unique<Branch>(nullptr);

    EXPECT_TRUE(HighLevelLoopTransform::IsSimpleStatement(stloc.get()));
    EXPECT_TRUE(HighLevelLoopTransform::IsSimpleStatement(call.get()));
    EXPECT_FALSE(HighLevelLoopTransform::IsSimpleStatement(ldloc.get()));
    EXPECT_FALSE(HighLevelLoopTransform::IsSimpleStatement(branch.get()));
    EXPECT_FALSE(HighLevelLoopTransform::IsSimpleStatement(nullptr));
}

// A loop container whose entry point's first instruction is `if (cond) leave loop`
// (the while-condition break: break when cond is true => `while (!cond)`) becomes
// a While container with the condition extracted and the rest as the body.
TEST(HighLevelLoopTransform, RunTransformsWhileConditionLoop) {
    // Build: Loop { entry: if (other >= 0) leave loop; body: stloc num(num+1); br entry }
    // -> While { entry: if (other < 0) leave loop else br body; body: num++ }
    // The while condition rides `other` (not the incremented variable), so the
    // later MatchForLoop split (the condition must USE the increment variable)
    // correctly leaves this loop a plain While.
    auto num = MakeLocal("num");
    auto other = MakeLocal("other");
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(0)));
    // The loop container sits in the pre-header's instructions.
    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::Loop;
    BlockContainer* loopPtr = loopC.get();

    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    // if (other >= 0) leave loop  (break when other >= 0 => while (other < 0))
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(other), std::make_unique<LdcI4>(0),
                               ComparisonKind::GreaterThanOrEqual),
        std::make_unique<Leave>(loopPtr)));
    loopC->AddBlock(std::move(entry));

    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    body->Add(MakeIncrement(num, 1));
    body->SetFinal(std::make_unique<Branch>(entryPtr));  // back-edge
    loopC->AddBlock(std::move(body));

    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->Variables.push_back(other);
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    HighLevelLoopTransform::Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // The container is now a While kind (not For: the condition does not use
    // the increment variable).
    ASSERT_EQ(loopPtr->Kind, ContainerKind::While);
    // The entry point's if is the while condition: negated (>= becomes <), the
    // true arm branches to the body, the false arm is the leave (break).
    auto* iff = dynamic_cast<IfInstruction*>(loopPtr->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Kind, ComparisonKind::LessThan) << ">= negated to <";
    // A body block was extracted (the entry had only the if; the body is the
    // second block).
    EXPECT_GE(loopPtr->Blocks.size(), 2u);
}

TEST(HighLevelLoopTransform, RunTransformsLoopWithIncrementBlockToFor) {
    // Build: Loop { entry: if (num < 5) br body else leave; body: ... br incr;
    // incr: num++; br entry } -- the classic csc for-loop lowering where the
    // increment lives in its own block that `continue`-targets jump to.
    // -> For: the increment block moves to the container's end (the C#
    // MatchForLoop MoveElementToEnd(incrementBlock)).
    auto num = MakeLocal("num");
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::Loop;
    BlockContainer* loopPtr = loopC.get();

    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(nullptr),   // TrueInst -> body (patched below)
        std::make_unique<Leave>(nullptr)));  // FalseInst -> leave loop (patched below)
    loopC->AddBlock(std::move(entry));

    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    body->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(42)));  // body work
    // FinalInstruction -> patch to br incr below.
    loopC->AddBlock(std::move(body));

    auto incr = std::make_unique<Block>();
    Block* incrPtr = incr.get();
    incr->Add(MakeIncrement(num, 1));
    incr->SetFinal(std::make_unique<Branch>(entryPtr));  // back-edge to the loop head
    loopC->AddBlock(std::move(incr));

    // Patch the entry's arms now the block pointers are stable.
    auto* iff = dynamic_cast<IfInstruction*>(loopPtr->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    auto tru = std::make_unique<Branch>(bodyPtr);
    iff->TrueInst = std::move(tru);
    iff->TrueInst->Parent = iff;
    iff->TrueInst->ChildIndex = 1;
    auto* lv = dynamic_cast<Leave*>(iff->FalseInst.get());
    ASSERT_NE(lv, nullptr);
    lv->TargetContainer = loopPtr;

    bodyPtr->SetFinal(std::make_unique<Branch>(incrPtr));

    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    HighLevelLoopTransform::Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(loopPtr->Kind, ContainerKind::For)
        << "a while-shaped loop with a dedicated increment block becomes For";
    // The increment block moved to the end of the container (the C#
    // MoveElementToEnd), so iteration order is entry, body, increment.
    ASSERT_EQ(loopPtr->Blocks.size(), 3u);
    EXPECT_EQ(loopPtr->Blocks.back().get(), incrPtr);
}

TEST(HighLevelLoopTransform, RunSplitsTrailingIncrementIntoForBlock) {
    // Build: Loop { entry: if (num < 5) br body else leave;                // head
    //               body: stloc num(42); stloc num(num+1); br entry }      // incr inline
    // -- the common csc `for` body lowering with no dedicated increment
    // block. MatchForLoop's no-dedicated-block case splits the trailing
    // `stloc V(add(ldloc V, k)); br entry` tail into a new block appended to
    // the container and marks the loop For (the C# MatchForLoop else-branch).
    auto num = MakeLocal("num");
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::Loop;
    BlockContainer* loopPtr = loopC.get();

    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(nullptr),   // patched to body below
        std::make_unique<Leave>(nullptr)));  // patched to loop below
    loopC->AddBlock(std::move(entry));

    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    body->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(42)));  // body work
    body->Add(MakeIncrement(num, 1));                                       // trailing increment
    body->SetFinal(std::make_unique<Branch>(entryPtr));                     // back-edge
    loopC->AddBlock(std::move(body));

    auto* iff = dynamic_cast<IfInstruction*>(loopPtr->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    auto tru = std::make_unique<Branch>(bodyPtr);
    iff->TrueInst = std::move(tru);
    iff->TrueInst->Parent = iff;
    iff->TrueInst->ChildIndex = 1;
    auto* lv = dynamic_cast<Leave*>(iff->FalseInst.get());
    ASSERT_NE(lv, nullptr);
    lv->TargetContainer = loopPtr;

    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx;
    HighLevelLoopTransform::Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(loopPtr->Kind, ContainerKind::For)
        << "a body-inline increment tail splits out and the loop becomes For";
    // Three blocks now: entry, body (without the increment), and the new
    // increment block appended at the end, which the body now branches to.
    ASSERT_EQ(loopPtr->Blocks.size(), 3u);
    const Block* newIncr = loopPtr->Blocks[2].get();
    ASSERT_EQ(newIncr->Instructions.size(), 1u) << "increment block holds the split stloc";
    EXPECT_EQ(newIncr->Instructions[0]->Op, OpCode::StLoc);
    auto* incFinal = dynamic_cast<Branch*>(newIncr->FinalInstruction.get());
    ASSERT_NE(incFinal, nullptr);
    EXPECT_EQ(incFinal->TargetBlock, loopPtr->Blocks[0].get());
    // The body's trailing increment is gone; its final now branches to the
    // increment block.
    ASSERT_EQ(loopPtr->Blocks[1]->Instructions.size(), 1u);
    auto* bodyFinal = dynamic_cast<Branch*>(loopPtr->Blocks[1]->FinalInstruction.get());
    ASSERT_NE(bodyFinal, nullptr);
    EXPECT_EQ(bodyFinal->TargetBlock, loopPtr->Blocks[2].get());
}
