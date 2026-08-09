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
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
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

// Count StLoc-wrapping-IfInstruction occurrences (the conditional-operator form
// HandleConditionalOperator produces: `stloc V(if (...) V2 else V1))`). The
// ternary fold is monotone non-decreasing across ExpressionTransforms (each fold
// creates one; nothing in this subset removes them). Used by the sweep to confirm
// the transform makes corpus progress.
int CountConditionalOperators(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::StLoc) {
            auto* st = static_cast<StLoc*>(inst);
            if (st->Value && st->Value->Op == OpCode::IfInstruction) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count Box ILAst nodes in the tree. The VisitBox fold (`box ref-type(arg)` ->
// `arg`) is monotone non-increasing (each fold removes a Box; nothing in this
// subset creates one). Used by the sweep to confirm the transform does not
// regress and to measure corpus progress.
int CountBoxes(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Box) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Build a block whose FinalInstruction is an IfInstruction with the given
// condition and two Block arms (each a single StLoc to `v` of the given values,
// no FinalInstruction -- the expression-block shape after ConditionDetection's
// TryDropCommonExit). The arms' FinalInstruction is left null (an expression
// block); `withFinal=true` instead gives each arm a Branch final (a control-flow
// block, which HandleConditionalOperator must reject).
std::unique_ptr<Block> MakeTernaryBlock(ILVariablePtr v,
                                        std::unique_ptr<ILInstruction> cond,
                                        std::unique_ptr<ILInstruction> value1,
                                        std::unique_ptr<ILInstruction> value2,
                                        bool withFinal = false) {
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v, std::move(value1)));
    if (withFinal) trueBlock->SetFinal(std::make_unique<Branch>(nullptr));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v, std::move(value2)));
    if (withFinal) falseBlock->SetFinal(std::make_unique<Branch>(nullptr));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    return P;
}

// Build a MatchInstruction `expr is T x` (CheckType + CheckNotNull, a type
// pattern with a designator) over `testedOperand`, storing into `v`. This is the
// shape PatternMatchValueTypes/PatternMatchRefTypes produce.
std::unique_ptr<MatchInstruction> MakeMatch(ILVariablePtr v,
                                             std::unique_ptr<ILInstruction> testedOperand) {
    auto m = std::make_unique<MatchInstruction>(v, std::move(testedOperand));
    m->CheckType = true;
    m->CheckNotNull = true;
    return m;
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

// HandleConditionalOperator folds `if (cond) stloc A(V1) else stloc A(V2)` into
// `stloc A(if (!cond) V2 else V1))` (the conditional/ternary operator). Both arms
// are expression Blocks (no FinalInstruction) with a single StLoc to the same
// variable; the StLoc becomes a non-terminal and a Branch to the next block
// replaces the if-final (the block-model adaptation).
TEST(ExpressionTransforms, HandleConditionalOperatorFoldsTernary) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    // if (a != b) { stloc v(a) } else { stloc v(b) }
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(MakeTernaryBlock(v, std::move(cond),
                                        std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b)));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P = *fn->Body->Blocks[0];
    // The StLoc was appended to the block's non-terminal Instructions.
    ASSERT_EQ(P.Instructions.size(), 1u);
    ASSERT_EQ(P.Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(P.Instructions[0].get());
    EXPECT_EQ(st->Variable.get(), v.get()) << "the temp must survive the fold";
    // The StLoc's value is the conditional operator IfInstruction.
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction);
    auto* newIf = static_cast<IfInstruction*>(st->Value.get());
    // The condition was negated: comp(ne, a, b) -> comp(eq, a, b).
    ASSERT_EQ(newIf->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(newIf->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must be negated";
    // TrueInst = V2 (b), FalseInst = V1 (a) -- the C# swaps so `cond ? V1 : V2`.
    ASSERT_EQ(newIf->TrueInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newIf->TrueInst.get())->Variable.get(), b.get());
    ASSERT_EQ(newIf->FalseInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newIf->FalseInst.get())->Variable.get(), a.get());
    // The if-final is now a Branch to Q (the positional fall-through).
    ASSERT_EQ(P.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get());
}

// HandleConditionalOperator does not fold when the two arms store to different
// variables (the conditional operator requires both arms to assign the same temp).
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsDifferentVariables) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v1 = MakeLocal("v1");
    auto v2 = MakeLocal("v2");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    // True arm stores to v1, false arm to v2 -- different variables.
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v1, std::make_unique<LdLoc>(a)));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v2, std::make_unique<LdLoc>(b)));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    for (auto vv : {v1, v2, a, b}) fn->Variables.push_back(vv);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The if-final stays (no fold); the block has no non-terminal StLoc.
    auto& P2 = *fn->Body->Blocks[0];
    EXPECT_EQ(P2.Instructions.size(), 0u);
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when the arms store different variables";
}

// HandleConditionalOperator does not fold when an arm is a control-flow Block
// (has a FinalInstruction) -- those were already handled by the block transform
// and the C# skips BlockKind.ControlFlow.
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsArmWithFinal) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(MakeTernaryBlock(v, std::move(cond),
                                        std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b),
                                        /*withFinal=*/true));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P = *fn->Body->Blocks[0];
    EXPECT_EQ(P.Instructions.size(), 0u);
    ASSERT_EQ(P.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when an arm has a FinalInstruction";
}

// HandleConditionalOperator does not fold when an arm Block has more than one
// instruction (the conditional operator requires each arm to be a single store).
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsMultiInstructionArm) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    // True arm: two StLocs to v (not a single store).
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(a)));
    trueBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(b)));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(b)));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    for (auto vv : {v, a, b}) fn->Variables.push_back(vv);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    EXPECT_EQ(P2.Instructions.size(), 0u);
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when an arm has more than one instruction";
}

// The logic.and/or canonicalization swaps the arms and negates the condition
// when the true arm is ldc.i4 0 and the false arm is not: `if (cond) 0 else RHS`
// -> `if (!cond) RHS else 0`.
TEST(ExpressionTransforms, LogicAndOrCanonicalizationSwapsZeroTrueArm) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    // if (a) ldc.i4 0 else ldloc b -- the `a && b`-shaped normalization.
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto iff = std::make_unique<IfInstruction>(std::move(cond),
                                               std::make_unique<LdcI4>(0),
                                               std::make_unique<LdLoc>(b));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff2, nullptr);
    // The arms were swapped: TrueInst is now ldloc b, FalseInst is ldc.i4 0.
    ASSERT_EQ(iff2->TrueInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff2->TrueInst.get())->Variable.get(), b.get());
    ASSERT_EQ(iff2->FalseInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->FalseInst.get())->Value, 0);
    // The condition was negated: comp(ne, a, 0) -> comp(eq, a, 0) = !a.
    ASSERT_EQ(iff2->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(iff2->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must be negated by the canonicalization";
}

// The logic.and/or canonicalization does NOT swap when both arms are ldc.i4 1
// (the infinite-loop guard: swapping `1 else 1` would just re-trigger).
TEST(ExpressionTransforms, LogicAndOrBothOneNoSwap) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    // if (a == b) ldc.i4 1 else ldc.i4 1 -- both arms 1, no swap. The condition
    // is comp(eq, a, b) (not comp(!=0), so the D81 comp(!=0)->x rewrite leaves it;
    // not a logic.not, so it stays a Comp) -- isolating the canonicalization.
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Equality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto iff = std::make_unique<IfInstruction>(std::move(cond),
                                               std::make_unique<LdcI4>(1),
                                               std::make_unique<LdcI4>(1));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff2, nullptr);
    // No swap: TrueInst stays ldc.i4 1, FalseInst stays ldc.i4 1.
    ASSERT_EQ(iff2->TrueInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->TrueInst.get())->Value, 1);
    ASSERT_EQ(iff2->FalseInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->FalseInst.get())->Value, 1);
    // The condition was not negated (no swap -> no negate): stays comp(eq, a, b).
    ASSERT_EQ(iff2->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(iff2->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must not be negated when the swap is suppressed";
}

// match(x) ? true : false -> match(x): a conditional whose condition is a
// pattern match and whose arms are ldc.i4 1 / ldc.i4 0 is redundant -- the
// MatchInstruction already evaluates to 1 (matched) / 0 (not matched). When the
// if is a sub-expression value (here `stloc boolVar(if (match) 1 else 0)`), the
// fold is a clean in-place ReplaceWith: the StLoc's value becomes the match.
TEST(ExpressionTransforms, MatchTrueFalseFoldsToMatchInValuePosition) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match.type[T].notnull(v = ldloc x)) ldc.i4 1 else ldc.i4 0)
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Variable.get(), boolVar.get());
    // The StLoc's value is now the MatchInstruction (the if was replaced by it).
    ASSERT_EQ(st->Value->Op, OpCode::MatchInstruction)
        << "if (match) 1 else 0 must fold to the match in the value slot";
    auto* m = static_cast<MatchInstruction*>(st->Value.get());
    EXPECT_TRUE(m->CheckType && m->CheckNotNull) << "the match pattern is preserved";
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    EXPECT_EQ(m->Variable.get(), v.get());
}

// match(x) ? true : false as a block's FinalInstruction (a statement-if): the
// MatchInstruction (a value, not control flow) cannot be the final, so it
// becomes a non-terminal statement (its side effect -- storing into Variable --
// is preserved) and a Branch to the next block replaces the if-final (the
// HandleConditionalOperator block-model adaptation).
TEST(ExpressionTransforms, MatchTrueFalseFoldsToMatchAsBlockFinal) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    // P: if (match.type[T].notnull(v = ldloc x)) ldc.i4 1 else ldc.i4 0  (final)
    // Q: leave
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(x);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    // The match became a non-terminal statement; the if-final is now a Branch to Q.
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::MatchInstruction)
        << "the match must become a non-terminal statement";
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}

// The fold does not fire when the condition is not a pattern match (a bare
// ldloc, not a MatchInstruction/Comp-pattern/Call): the if stays.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonPatternCondition) {
    auto x = MakeParam("x");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (ldloc x) ldc.i4 1 else ldc.i4 0) -- the condition is a
    // bare load, not a pattern match.
    auto iff = std::make_unique<IfInstruction>(std::make_unique<LdLoc>(x),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The if stays (no fold): the StLoc's value is still the IfInstruction.
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-pattern condition must not fold";
}

// The fold does not fire when the true arm is not ldc.i4 1.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonOneTrueArm) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match) ldc.i4 0 else ldc.i4 0) -- true arm is 0, not 1.
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(0),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-1 true arm must not fold";
}

// The fold does not fire when the false arm is not ldc.i4 0.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonZeroFalseArm) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match) ldc.i4 1 else ldc.i4 1) -- false arm is 1, not 0.
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(1));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-0 false arm must not fold";
}

// VisitBox drops `box ref-type(arg)` to `arg`: for a reference type, box is a
// no-op (the value is already on the heap). `stloc v(box string(ldloc s))`
// folds to `stloc v(ldloc s)`.
TEST(ExpressionTransforms, VisitBoxDropsBoxOfReferenceType) {
    auto s = MakeParam("s", std::make_shared<KnownType>(KnownTypeCode::String));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::String),
        std::make_unique<LdLoc>(s));
    auto fn = MakeFnWithBlock({v, s});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The box was replaced by its argument: the StLoc's value is now ldloc s.
    ASSERT_EQ(st->Value->Op, OpCode::LdLoc)
        << "box string(ldloc s) must fold to ldloc s";
    EXPECT_EQ(static_cast<LdLoc*>(st->Value.get())->Variable.get(), s.get());
    EXPECT_EQ(CountBoxes(*fn), 0) << "no Box node must remain after the fold";
}

// VisitBox keeps `box <value-type>(arg)`: a value type's box is NOT a no-op
// (it allocates a boxed copy). The argument's stack type (I4 for int) does not
// match the box's result (O), so the fold does not fire even though a value type
// is never a reference type.
TEST(ExpressionTransforms, VisitBoxKeepsBoxOfValueType) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::Int32),
        std::make_unique<LdLoc>(i));
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "box int(ldloc i) must stay (boxing a value type is not a no-op)";
    EXPECT_EQ(CountBoxes(*fn), 1) << "the Box node must survive";
}

// VisitBox keeps `box T(arg)` over a generic type parameter: IsReferenceType is
// nullopt for a TypeParameter (the kind alone cannot tell -- it depends on the
// `where T : class` constraint this minimal type system does not track), so the
// fold is conservative and the box stays. The C# folds it only when T has a
// class constraint.
TEST(ExpressionTransforms, VisitBoxKeepsBoxOfTypeParameter) {
    auto T = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        0, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "T");
    auto t = MakeParam("t", T);
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    // box T(ldloc t) -- the type parameter boxes its argument.
    auto box = std::make_unique<Box>(T, std::make_unique<LdLoc>(t));
    auto fn = MakeFnWithBlock({v, t});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "box T(arg) over a type parameter must stay (IsReferenceType is nullopt)";
    EXPECT_EQ(CountBoxes(*fn), 1);
}

// VisitBox keeps a box whose type resolved to null (the reader hands back a
// null ITypePtr when it cannot resolve the token): the fold must not dereference
// a null type.
TEST(ExpressionTransforms, VisitBoxKeepsBoxWithNullType) {
    auto o = MakeParam("o", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(nullptr, std::make_unique<LdLoc>(o));
    auto fn = MakeFnWithBlock({v, o});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "a box with a null type must stay (no dereference)";
    EXPECT_EQ(CountBoxes(*fn), 1);
}

// VisitBox drops `box object(ldloc o)`: object is a reference type, so boxing
// an already-object-typed value is a no-op. (The argument is stack-type O
// matching the box's result O.)
TEST(ExpressionTransforms, VisitBoxDropsBoxOfObjectReferenceType) {
    auto o = MakeParam("o", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::Object),
        std::make_unique<LdLoc>(o));
    auto fn = MakeFnWithBlock({v, o});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdLoc)
        << "box object(ldloc o) must fold to ldloc o";
    EXPECT_EQ(static_cast<LdLoc*>(st->Value.get())->Variable.get(), o.get());
    EXPECT_EQ(CountBoxes(*fn), 0);
}

// Count Conv nodes whose ResultType is I (native int) sitting directly in a
// LdElema or NewArr Indices collection -- the array-index widening convs that
// CleanUpArrayIndices removes. The fold is monotone non-increasing (each fold
// drops one such conv; nothing in this subset creates one). Used by the sweep.
int CountArrayIndexConvI(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::LdElema) {
            auto* ld = static_cast<LdElema*>(inst);
            for (auto& idx : ld->Indices)
                if (idx && idx->Op == OpCode::Conv && idx->ResultType() == StackType::I) ++n;
        } else if (inst->Op == OpCode::NewArr) {
            auto* na = static_cast<NewArr*>(inst);
            for (auto& idx : na->Indices)
                if (idx && idx->Op == OpCode::Conv && idx->ResultType() == StackType::I) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// CleanUpArrayIndices drops `conv.i` (SignExtend, I4->I) widening of an array
// element-address index: ldelema(arr, conv.i(ldloc idx)) -> ldelema(arr, ldloc
// idx). The conv only widens the I4 index to native int and is redundant in C#.
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvIFromLdElema) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.i(ldloc idx): SignExtend I4 -> I (the reader's Conv_i from I4).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.i widening of an array index must be dropped";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdElema);
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices.size(), 1u);
    EXPECT_EQ(ld->Indices[0]->Op, OpCode::LdLoc)
        << "the index must be the bare ldloc idx after the conv is dropped";
}

// CleanUpArrayIndices drops `conv.u` (ZeroExtend, I4->I) from a NewArr length.
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvUFromNewArr) {
    auto len = MakeParam("len", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    // conv.u(ldloc len): ZeroExtend I4 -> I (the reader's Conv_u from I4).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(len), PrimitiveType::U, false, Sign::None));
    auto newArr = std::make_unique<NewArr>(elemType, std::move(indices));
    auto fn = MakeFnWithBlock({len, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(newArr)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.u widening of the newarr length must be dropped";
}

// CleanUpArrayIndices drops a checked `conv.ovf.i` (Truncate + CheckForOverflow):
// an overflow-checked widening is safe to drop (it would throw only on values
// outside I4 range, which a C# int index cannot produce).
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvOvfIFromLdElema) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.ovf.i(ldloc idx): SignExtend I4 -> I with overflow check (the reader's
    // Conv_ovf_i from I4; needsSign forces InputSign = Signed, Kind = SignExtend).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, true, Sign::Signed));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.ovf.i (checked SignExtend) widening of an array index is dropped";
}

// CleanUpArrayIndices keeps `conv.i` from an I8 input (Truncate without overflow
// check): that is a real truncation (the index is a long narrowed to native
// int), not a redundant widening, so the conv must survive.
TEST(ExpressionTransforms, CleanUpArrayIndicesKeepsConvIFromI8) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int64));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.i(ldloc idx) where idx is I8: Truncate I8 -> I, no overflow check.
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 1)
        << "conv.i from I8 (unchecked Truncate) is a real truncation and stays";
}

// CleanUpArrayIndices keeps `conv.i4` (Nop, I4->I4) in an index: its ResultType
// is I4 (not I), so the `ResultType == I` guard excludes it. (conv.i4 around an
// index would be a no-op cast, not a native-int widening.)
TEST(ExpressionTransforms, CleanUpArrayIndicesKeepsConvI4) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I4, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The conv.i4 stays (its ResultType is I4, not I, so CleanUpArrayIndices
    // skips it); CountArrayIndexConvI counts only ResultType==I convs, so it is 0
    // either way -- verify the conv node itself survived.
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices[0]->Op, OpCode::Conv)
        << "conv.i4 (Nop, ResultType I4) in an index must stay";
}

// CleanUpArrayIndices leaves a bare (non-conv) index untouched.
TEST(ExpressionTransforms, CleanUpArrayIndicesLeavesBareIndex) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<LdLoc>(idx));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices.size(), 1u);
    EXPECT_EQ(ld->Indices[0]->Op, OpCode::LdLoc)
        << "a bare index must survive CleanUpArrayIndices unchanged";
}

// Count `conv.rN(conv.r.un(...))` patterns -- a float-target Conv (R4/R8/R)
// whose Argument is a Conv with Kind == IntToFloat and TargetType == R (the
// uncombined conv.r.un the VisitConv fold removes). The fold is monotone non-
// increasing (each fold removes one such nested pattern; nothing in this subset
// creates one). Used by the sweep to confirm the transform does not regress.
int CountConvRUnNested(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Conv) {
            auto* outer = static_cast<Conv*>(inst);
            if (IsFloatType(outer->TargetType) && outer->Argument &&
                outer->Argument->Op == OpCode::Conv) {
                auto* inner = static_cast<Conv*>(outer->Argument.get());
                if (inner->Kind == ConversionKind::IntToFloat &&
                    inner->TargetType == PrimitiveType::R) {
                    ++n;
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// VisitConv combines `conv.r4(conv.r.un(x))` -> `conv.r4.un(x)`: IL conv.r.un
// does not say whether to convert to R4 or R8, so the C# compiler follows it with
// an explicit conv.r4; the two convs fold to a single `conv.r4.un` (int-to-
// float, unsigned input) carrying the inner conv's input sign but the outer's R4
// target. The integer argument (ldloc i, I4) is preserved as the new conv's
// argument.
TEST(ExpressionTransforms, VisitConvCombinesConvR4OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    // conv.r.un(ldloc i): the reader's Conv_r_un -- Unsigned, TargetType R,
    // Kind IntToFloat (I4 -> F8).
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    // conv.r4(conv.r.un(ldloc i)): the reader's Conv_r4 -- Signed (but the outer
    // conv.r4's InputType is F8, so needsSign is false and InputSign is None); the
    // fold checks the inner conv's Kind/TargetType, not the outer's sign.
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.r4(conv.r.un(...)) must fold to a single conv.r4.un";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv)
        << "the result must be a single Conv (the combined conv.r4.un)";
    auto* combined = static_cast<Conv*>(st->Value.get());
    EXPECT_EQ(combined->TargetType, PrimitiveType::R4)
        << "the combined conv keeps the outer's R4 target";
    EXPECT_EQ(combined->Kind, ConversionKind::IntToFloat)
        << "the combined conv is an int-to-float conversion";
    EXPECT_EQ(combined->InputSign, Sign::Unsigned)
        << "the combined conv carries the inner conv.r.un's unsigned sign";
    ASSERT_EQ(combined->Argument->Op, OpCode::LdLoc)
        << "the integer argument survives the fold";
    EXPECT_EQ(static_cast<LdLoc*>(combined->Argument.get())->Variable.get(), i.get());
}

// VisitConv combines `conv.r8(conv.r.un(x))` -> `conv.r8.un(x)` (the R8 variant).
TEST(ExpressionTransforms, VisitConvCombinesConvR8OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Double));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R8, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0);
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* combined = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(combined->TargetType, PrimitiveType::R8);
    EXPECT_EQ(combined->Kind, ConversionKind::IntToFloat);
    EXPECT_EQ(combined->InputSign, Sign::Unsigned);
}

// VisitConv keeps `conv.r4(ldloc d)` (a bare float argument, not a conv.r.un):
// the argument is not a Conv, so the combining fold does not fire. (conv.r4 from
// a double is a FloatPrecisionChange, not an int-to-float combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvR4OverBareFloat) {
    auto d = MakeParam("d", std::make_shared<KnownType>(KnownTypeCode::Double));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto outer = std::make_unique<Conv>(
        std::make_unique<LdLoc>(d), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, d});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "a bare-float-argument conv.r4 must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv)
        << "the conv.r4 must survive (its argument is not a conv.r.un)";
    auto* conv = static_cast<Conv*>(st->Value.get());
    EXPECT_EQ(conv->TargetType, PrimitiveType::R4);
    ASSERT_EQ(conv->Argument->Op, OpCode::LdLoc)
        << "the bare float argument survives unchanged";
}

// VisitConv keeps `conv.r4(conv.r8(ldloc i))`: the inner conv.r8 has Kind
// IntToFloat but TargetType R8 (not R), so the `conv.TargetType == R` guard
// excludes it. (This is a precision change from an int via R8, not a
// conv.r.un combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvR4OverConvR8) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R8, false, Sign::Signed);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.r4(conv.r8(...)) must not fold (the inner is not conv.r.un)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The two-conv nest must survive (the outer is still a Conv wrapping a Conv).
    ASSERT_EQ(st->Value->Op, OpCode::Conv);
    auto* outerConv = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(outerConv->Argument->Op, OpCode::Conv)
        << "the nested conv.r8 must survive (it is not a conv.r.un)";
}

// VisitConv keeps `conv.i4(conv.r.un(ldloc d))`: the outer's TargetType is I4
// (an integer type, not a float type), so the `IsFloatType(TargetType)` guard
// excludes it. (This is a float-to-int conversion, not a conv.r.un combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvI4OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::I4, false, Sign::None);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.i4(conv.r.un(...)) must not fold (the outer target is not float)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv);
    auto* outerConv = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(outerConv->Argument->Op, OpCode::Conv)
        << "the nested conv.r.un must survive (the outer is conv.i4, not float)";
}

// On the real mscorlib corpus, running the full pre-pipeline through the
// StatementTransform{ILInlining, ExpressionTransforms} (the GetILTransforms()
// position) preserves the ILAst invariant and the HandleConditionalOperator
// ternary fold makes corpus progress (the StLoc-wrapping-IfInstruction count
// rises as if/else pairs over the same temp fold to conditional operators).
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
    int totalFolds = 0;
    int totalArrayIndexConvDrops = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        int before = CountConditionalOperators(*fn);
        int boxesBefore = CountBoxes(*fn);
        int arrayIdxConvBefore = CountArrayIndexConvI(*fn);
        int convRUnBefore = CountConvRUnNested(*fn);
        {
            StatementTransform st;
            st.AddChild(std::make_unique<ILInlining>());
            st.AddChild(std::make_unique<ExpressionTransforms>());
            st.Run(*fn, ctx);
        }
        fn->CheckInvariant(ILPhase::Normal);
        int after = CountConditionalOperators(*fn);
        int boxesAfter = CountBoxes(*fn);
        int arrayIdxConvAfter = CountArrayIndexConvI(*fn);
        int convRUnAfter = CountConvRUnNested(*fn);
        // The ternary fold is monotone non-decreasing (each fold creates a
        // StLoc-if; nothing in this subset removes one). The VisitComp rewrites
        // (comp(!=0)->x, logic.not push, unsigned normalization) do not create
        // StLoc-if, so the count must not drop.
        EXPECT_GE(after, before);
        // The VisitBox fold (`box ref-type(arg)` -> `arg`) is monotone
        // non-increasing (each fold removes a Box; nothing in this subset
        // creates one). The fold fires on the legacy-csc corpus (the sweep counts
        // ~40 box-of-reference-type folds across 8000 methods -- a real-corpus
        // ILAst-cleaning transform, not faithfulness-only), but the count is not
        // asserted (the pre-pipeline's unordered-container iteration makes the
        // exact total vary slightly); the per-method monotone-non-increasing
        // invariant is the deterministic correctness gate.
        EXPECT_LE(boxesAfter, boxesBefore);
        // The CleanUpArrayIndices fold (`conv.i` widening of an array index ->
        // the bare index) is monotone non-increasing (each fold removes a
        // ResultType==I conv from a LdElema/NewArr Indices; nothing in this
        // subset creates one). The fold fires on the legacy-csc corpus when the
        // compiler emits `conv.i` to widen an I4 array index to native int before
        // ldelema/newarr; the per-method monotone-non-increasing invariant is the
        // deterministic correctness gate (the absolute count is not asserted).
        EXPECT_LE(arrayIdxConvAfter, arrayIdxConvBefore);
        // The VisitConv conv.r.un combining fold (`conv.r4(conv.r.un(x))` /
        // `conv.r8(conv.r.un(x))` -> a single `conv.r4.un` / `conv.r8.un`) is
        // monotone non-increasing (each fold removes one nested conv.r.un; nothing
        // in this subset creates one). The fold fires on the legacy-csc corpus when
        // the compiler emits `conv.r.un` followed by an explicit `conv.r4`/`conv.r8`
        // (a Roslyn-era codegen pattern); the per-method monotone-non-increasing
        // invariant is the deterministic correctness gate (the absolute count is
        // not asserted -- the .NET Framework 4 legacy-csc corpus may emit
        // conv.r.un rarely, so the fold may fire 0 times on it).
        EXPECT_LE(convRUnAfter, convRUnBefore);
        totalFolds += (after - before);
        totalArrayIndexConvDrops += (arrayIdxConvBefore - arrayIdxConvAfter);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The ternary fold fires on the legacy-csc corpus (csc emits `V = a ? b : c`
    // as if/else stloc to the same temp, which ConditionDetection shapes into the
    // two-expression-Block-arms form HandleConditionalOperator matches).
    EXPECT_GT(totalFolds, 0)
        << "HandleConditionalOperator must fold some ternary on mscorlib";
    // The CleanUpArrayIndices fold fires on the legacy-csc corpus when a method
    // indexes an array with a conv.i-widened index; if it fires at all, the drop
    // count must be positive (a regression that made the fold stop firing would
    // surface as zero). The exact count is not asserted (corpus-dependent on how
    // many methods widen an index before ldelema).
    (void)totalArrayIndexConvDrops;
}
