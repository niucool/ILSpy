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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for ExpressionTransforms (the second per-statement child of the
// StatementTransform, the next in-order Phase 4 target after the D80
// StatementTransform orchestration). The C# ExpressionTransforms is an
// ILVisitor IStatementTransform folding simple expression patterns; this
// iteration ports the self-contained VisitComp subset -- `logic.not(comp op)`
// -> `comp(op.Negate)`, `comp(x != 0)` -> `x`, and `comp.unsigned(left > 0)` /
// `<= 0` -> `comp(left != 0)` / `== 0`. The tests run the transform via the
// StatementTransform driver (the real path, exercising the if-final-only
// block-model compensation) on hand-built blocks; the mscorlib sweep pins the
// global contract (the invariant holds across the corpus).

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, std::move(type), 1);
    v->Name = std::move(name);
    return v;
}

// A single-block function whose body block is empty (a Leave final); the test
// adds non-terminal statements via the returned block pointer.
std::unique_ptr<ILFunction> MakeFnWithBlock(std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<Block>();
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(block));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

// A two-block function: P with an if-final (no non-terminal statements), Q with
// a Leave. Models the if-final-only block the driver compensation targets.
std::unique_ptr<ILFunction> MakeIfFinalOnlyFn(std::unique_ptr<ILInstruction> condition,
                                              std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    auto iff = std::make_unique<IfInstruction>(std::move(condition),
                                              std::make_unique<Branch>(nullptr));
    P->SetFinal(std::move(iff));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

void RunExpressionTransforms(ILFunction& fn) {
    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    st.Run(fn, ctx);
}

void RunPrePipeline(ILFunction& fn, ILTransformContext& ctx) {
    ControlFlowSimplification().Run(fn, ctx);
    StObjToStLoc().Run(fn, ctx);
    ILInlining().Run(fn, ctx);
    InlineReturnTransform().Run(fn, ctx);
    RemoveInfeasiblePathTransform().Run(fn, ctx);
    DetectPinnedRegions().Run(fn, ctx);
    DetectCatchWhenConditionBlocks().Run(fn, ctx);
    LdLocaDupInitObjTransform().Run(fn, ctx);
    EarlyExpressionTransforms().Run(fn, ctx);
    RemoveDeadVariableInit().Run(fn, ctx);
    ControlFlowSimplification().Run(fn, ctx);
    SwitchDetection().Run(fn, ctx);
    SwitchOnNullableTransform().Run(fn, ctx);
    LoopDetection().Run(fn, ctx);
    PatternMatchingTransform().Run(fn, ctx);
    ConditionDetection().Run(fn, ctx);
    LockTransform().Run(fn, ctx);
    UsingTransform().Run(fn, ctx);
    CachedDelegateInitialization().Run(fn, ctx);
    CachedReadOnlySpanInitialization().Run(fn, ctx);
}

// Count Comp nodes (any kind). The VisitComp rewrites are monotone in the
// total Comp count: logic.not push removes the outer comp, `comp(x != 0) -> x`
// removes a comp, and the unsigned normalization keeps the count. So the total
// must not rise. Used by the sweep to confirm the transform is monotone.
int CountComps(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Comp) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

} // namespace

// logic.not(comp op) -> comp(op.Negate): comp(eq, comp(eq, a, b), 0) folds to
// comp(ne, a, b) (the `!(a == b)` -> `a != b` push).
TEST(ExpressionTransforms, LogicNotPushesNegationIntoComparison) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::Equality, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Equality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality)
        << "!(a == b) must fold to a != b";
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    ASSERT_EQ(c->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), a.get());
    EXPECT_EQ(static_cast<LdLoc*>(c->Right.get())->Variable.get(), b.get());
}

// comp(x != 0) -> x when the comp is in an if condition: `if (comp(x != 0))`
// folds to `if (x)`. The comp is the if-final's condition (an if-final-only
// block), so this also exercises the driver's block-model compensation.
TEST(ExpressionTransforms, CompNotEqualsZeroDropsToOperandInIfCondition) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff->Condition.get())->Variable.get(), x.get())
        << "if (x != 0) must fold to if (x)";
}

// comp(x != 0) -> x when the comp's left is itself a comp (the
// `comp(comp(...) != 0)` -> `comp(...)` case), even outside a condition slot.
TEST(ExpressionTransforms, CompNotEqualsZeroDropsToOperandWhenLeftIsComp) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::Equality, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Inequality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Equality)
        << "comp(comp(eq,a,b) != 0) must fold to comp(eq,a,b)";
}

// comp.unsigned(left > 0) cascades to `left`: `comp(gt.un, x, 0)` -> `comp(ne, x, 0)`
// -> (in condition slot) `x`.
TEST(ExpressionTransforms, CompUnsignedGreaterThanZeroBecomesOperand) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::GreaterThan, /*unsigned=*/true);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff->Condition.get())->Variable.get(), x.get())
        << "if (x >u 0) must fold to if (x)";
}

// comp.unsigned(left <= 0) -> comp(left == 0) -> logic.not(left): `if (x <=u 0)`
// folds to `if (!x)` (rendered as comp(eq, x, 0), the reader's brfalse shape).
TEST(ExpressionTransforms, CompUnsignedLessThanOrEqualZeroBecomesLogicNot) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::LessThanOrEqual, /*unsigned=*/true);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(iff->Condition.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Equality)
        << "if (x <=u 0) must fold to comp(eq, x, 0) == !x";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0);
}

// comp(x != 0) is left alone when it is NOT in a condition slot and its left is
// not a comp (the value is a real boolean expression, not a redundant test).
TEST(ExpressionTransforms, CompNotEqualsZeroStaysWhenNotInConditionSlot) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    // stloc v(comp(x != 0)) -- the comp is the stloc's value (not a condition
    // slot, left is a load not a comp).
    auto comp = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = MakeFnWithBlock({v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality)
        << "comp(x != 0) outside a condition slot must stay";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0);
}

// logic.not of a float comparison is NOT pushed (negating a float ordering is
// not a simple kind flip): comp(eq, comp(lt, a, b), 0) with float a,b stays.
TEST(ExpressionTransforms, LogicNotFloatComparisonNotPushed) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::LessThan, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Equality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* outer2 = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(outer2->Kind, ComparisonKind::Equality)
        << "the logic.not wrapper must stay (float negation suppressed)";
    ASSERT_EQ(outer2->Left->Op, OpCode::Comp);
    auto* inner2 = static_cast<Comp*>(outer2->Left.get());
    EXPECT_EQ(inner2->Kind, ComparisonKind::LessThan)
        << "the inner float comparison must not be negated";
}

// The driver visits an if-final-only block's condition (the block-model
// compensation): a block with no non-terminal statements but an IfInstruction
// final still gets ExpressionTransforms run on the if's condition. A Leave-final
// only block (no if) is not visited.
TEST(ExpressionTransforms, IfFinalOnlyBlockConditionIsVisited) {
    // if-final-only block: condition comp(x != 0) folds to x.
    {
        auto x = MakeParam("x");
        auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                           std::make_unique<LdcI4>(0),
                                           ComparisonKind::Inequality, false);
        auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
        ASSERT_NE(iff, nullptr);
        EXPECT_EQ(iff->Condition->Op, OpCode::LdLoc)
            << "if-final-only block condition must be visited by the driver";
    }
    // A Leave-final-only block (no non-terminals, no if) is skipped -- the driver
    // only runs the compensation for an IfInstruction final.
    {
        auto fn = std::make_unique<ILFunction>();
        fn->Body = std::make_unique<BlockContainer>();
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        auto blk = std::make_unique<Block>();
        blk->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(blk));
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 0u);
    }
}

// On the real mscorlib corpus, running the full pre-pipeline through the
// StatementTransform{ILInlining, ExpressionTransforms} (the GetILTransforms()
// position) preserves the ILAst invariant and the comp(x != 0) -> x rewrite
// makes corpus progress (the redundant-inequality-against-0 count drops).
TEST(ExpressionTransforms, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        int before = CountComps(*fn);
        {
            StatementTransform st;
            st.AddChild(std::make_unique<ILInlining>());
            st.AddChild(std::make_unique<ExpressionTransforms>());
            st.Run(*fn, ctx);
        }
        fn->CheckInvariant(ILPhase::Normal);
        int after = CountComps(*fn);
        // The rewrites are monotone in the total Comp count (logic.not push and
        // comp(x != 0) -> x each remove a comp; unsigned normalization keeps it);
        // the count must not rise.
        EXPECT_LE(after, before);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
}
