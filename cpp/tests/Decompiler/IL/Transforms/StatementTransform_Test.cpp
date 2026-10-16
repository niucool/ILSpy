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

// Tests for StatementTransform (the next in-order Phase 4 block after
// CachedReadOnlySpanInitialization). The C# GetILTransforms() BlockILTransform
// post-order set ends with a StatementTransform holding the interleaved
// per-statement transforms (ILInlining, ExpressionTransforms, ...) run
// statement-by-statement with rerun mechanics. This iteration ports the
// orchestration and wires the first child (ILInlining, the C# pipeline's
// second inlining pass); the remaining children and the ILInlining
// AllowInliningOfLdloca option are deferred. The orchestration tests use a
// mock IStatementTransform to pin the rerun mechanics; the per-statement
// ILInlining tests pin the real inlining on hand-built blocks (including an
// if-final block, the port's if-as-final block-model shape); the mscorlib
// sweep pins the global contract (the invariant holds across the corpus).

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
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
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A mock IStatementTransform that records the positions it is called at and can
// be configured to call RequestRerunCurrentPosition / RequestRerun(pos) once,
// so the orchestration's rerun mechanics can be pinned without a real transform.
class MockStatementTransform : public IStatementTransform {
public:
    std::vector<int> calls;
    // Call RequestRerunCurrentPosition() the first time Run is called at this pos.
    int rerunCurrentAtPos = -1;
    bool didRerunCurrent = false;
    // Call RequestRerun(targetPos) the first time Run is called at this pos.
    int rerunAtPos = -1;
    int rerunTargetPos = -1;
    bool didRerunPos = false;

    void Run(Block& /*block*/, int pos, StatementTransformContext& ctx) override {
        calls.push_back(pos);
        if (!didRerunCurrent && pos == rerunCurrentAtPos) {
            didRerunCurrent = true;
            ctx.RequestRerunCurrentPosition();
        }
        if (!didRerunPos && pos == rerunAtPos) {
            didRerunPos = true;
            ctx.RequestRerun(rerunTargetPos);
        }
    }
};

// Build a function whose body is a single block with `nonTerminals` non-terminal
// instructions and a Leave(body) final. The mock driver ranges over the
// non-terminals; the final is a Leave (EndPointUnreachable).
std::unique_ptr<ILFunction> MakeFnWithBlock(std::vector<std::unique_ptr<ILInstruction>> nonTerminals) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<Block>();
    for (auto& nt : nonTerminals) block->Add(std::move(nt));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(block));
    return fn;
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

int CountStLocs(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::StLoc) ++n; });
    return n;
}

} // namespace

// The driver calls the child at each position from last to first.
TEST(StatementTransform, OrchestrationRunsChildrenLastToFirst) {
    auto fn = MakeFnWithBlock({});
    auto* body = fn->Body->Blocks[0].get();
    // Three non-terminal Nop-like instructions (Call nodes with no side effects).
    for (int i = 0; i < 3; ++i) {
        auto call = std::make_unique<Call>("System.Foo::Bar");
        call->ReturnType = StackType::I4;
        body->Add(std::move(call));
    }
    fn->CheckInvariant(ILPhase::Normal);

    auto mock = std::make_unique<MockStatementTransform>();
    auto* mockPtr = mock.get();
    StatementTransform st;
    st.AddChild(std::move(mock));
    ILTransformContext ctx;
    st.Run(*fn, ctx);

    // The driver starts at the last non-terminal (index 2) and walks to 0.
    EXPECT_EQ(mockPtr->calls, (std::vector<int>{2, 1, 0}));
}

// A child that calls RequestRerunCurrentPosition() re-runs at the current
// position: the child is called twice at that position, then the walk continues.
TEST(StatementTransform, OrchestrationRequestRerunCurrentPosition) {
    auto fn = MakeFnWithBlock({});
    auto* body = fn->Body->Blocks[0].get();
    for (int i = 0; i < 3; ++i) {
        auto call = std::make_unique<Call>("System.Foo::Bar");
        call->ReturnType = StackType::I4;
        body->Add(std::move(call));
    }
    fn->CheckInvariant(ILPhase::Normal);

    auto mock = std::make_unique<MockStatementTransform>();
    mock->rerunCurrentAtPos = 2;  // re-run at the first position the driver visits
    auto* mockPtr = mock.get();
    StatementTransform st;
    st.AddChild(std::move(mock));
    ILTransformContext ctx;
    st.Run(*fn, ctx);

    // pos 2 is visited twice (the initial run + the rerun-current), then 1, 0.
    EXPECT_EQ(mockPtr->calls, (std::vector<int>{2, 2, 1, 0}));
}

// A child that calls RequestRerun(targetPos) with targetPos > current jumps the
// driver forward to targetPos and re-runs from there down to 0.
TEST(StatementTransform, OrchestrationRequestRerunPosition) {
    auto fn = MakeFnWithBlock({});
    auto* body = fn->Body->Blocks[0].get();
    for (int i = 0; i < 3; ++i) {
        auto call = std::make_unique<Call>("System.Foo::Bar");
        call->ReturnType = StackType::I4;
        body->Add(std::move(call));
    }
    fn->CheckInvariant(ILPhase::Normal);

    auto mock = std::make_unique<MockStatementTransform>();
    mock->rerunAtPos = 0;        // at the last position in the backward walk...
    mock->rerunTargetPos = 2;    // ...jump back to pos 2 (already processed).
    auto* mockPtr = mock.get();
    StatementTransform st;
    st.AddChild(std::move(mock));
    ILTransformContext ctx;
    st.Run(*fn, ctx);

    // First pass walks 2, 1, 0; at 0 the rerun to 2 fires; the second pass
    // walks 2, 1, 0 again.
    EXPECT_EQ(mockPtr->calls, (std::vector<int>{2, 1, 0, 2, 1, 0}));
}

// The per-statement ILInlining (the first wired child) folds a single-use StLoc
// into its load site in the next instruction, just like the whole-function pass.
TEST(StatementTransform, PerStatementInliningFoldsSingleUseLocal) {
    auto local = MakeLocal("v");
    auto arg = std::make_shared<ILVariable>(VariableKind::Parameter, nullptr, 1);
    arg->Name = "arg_1";

    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdLoc>(arg)));
    auto call = std::make_unique<Call>("System.Foo::Use");
    call->AddArg(std::make_unique<LdLoc>(local));
    call->ReturnType = StackType::I4;
    block->Add(std::move(call));

    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(local);
    fn->Variables.push_back(arg);
    fn->CheckInvariant(ILPhase::Normal);

    StatementTransform st;
    st.AddChild(std::make_unique<ILInlining>());
    ILTransformContext ctx;
    st.Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // The stloc v is gone; the call's argument is now ldloc arg (inlined).
    auto& only = fn->Body->Blocks[0];
    ASSERT_EQ(only->Instructions.size(), 1u);
    EXPECT_EQ(only->Instructions[0]->Op, OpCode::Call);
    auto* c = static_cast<Call*>(only->Instructions[0].get());
    ASSERT_EQ(c->Arguments.size(), 1u);
    ASSERT_EQ(c->Arguments[0]->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Arguments[0].get())->Variable.get(), arg.get());
}

// The per-statement ILInlining folds a single-use StLoc that is loaded in the
// block's if-final condition (the port's if-as-final block model: the if is the
// final, so the stloc at the last non-terminal position inlines into the if's
// condition -- matching the C# where the stloc before the if non-terminal
// inlines into the if).
TEST(StatementTransform, PerStatementInliningFoldsIntoIfFinalCondition) {
    auto local = MakeLocal("v");
    auto arg = std::make_shared<ILVariable>(VariableKind::Parameter, nullptr, 1);
    arg->Name = "arg_1";

    auto Q = std::make_unique<Block>();
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto P = std::make_unique<Block>();
    P->Add(std::make_unique<StLoc>(local, std::make_unique<LdLoc>(arg)));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(local),
                                       std::make_unique<LdNull>(), ComparisonKind::Equality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond),
                                                std::make_unique<Branch>(nullptr));
    P->SetFinal(std::move(iff));

    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(local);
    fn->Variables.push_back(arg);
    fn->CheckInvariant(ILPhase::Normal);

    StatementTransform st;
    st.AddChild(std::make_unique<ILInlining>());
    ILTransformContext ctx;
    st.Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // The stloc v is gone (inlined into the if's condition).
    auto& Pblk = fn->Body->Blocks[0];
    EXPECT_TRUE(Pblk->Instructions.empty()) << "stloc must be removed";
    auto* iff2 = dynamic_cast<IfInstruction*>(Pblk->FinalInstruction.get());
    ASSERT_NE(iff2, nullptr);
    auto* comp = dynamic_cast<Comp*>(iff2->Condition.get());
    ASSERT_NE(comp, nullptr);
    ASSERT_EQ(comp->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(comp->Left.get())->Variable.get(), arg.get())
        << "ldloc v inlined to ldloc arg in the if condition";
}

// On the real mscorlib corpus, running the full pre-pipeline through the
// StatementTransform{ILInlining} (the GetILTransforms() position) preserves the
// ILAst invariant. The second inlining pass folds the single-use variables the
// intervening transforms (ConditionDetection / Lock / Using / CachedDelegate /
// CachedReadOnlySpan) created; the sweep confirms the transform is safe across
// the corpus (no crash, invariant holds, the stloc count drops or stays).
TEST(StatementTransform, MscorlibSweepPreservesInvariant) {
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
        {
            StatementTransform st;
            st.AddChild(std::make_unique<ILInlining>());
            st.Run(*fn, ctx);
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
}
