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

// ReduceNestingTransform tests (EliminateRedundantTryFinally subset). The C#
// compiler sometimes wraps a `fixed` block in a try-finally; once
// DetectPinnedRegions has formed the PinnedRegion the try-finally is redundant
// (its finally is an empty `leave (nop)`) and is replaced with the PinnedRegion.

#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
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
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

ILVariablePtr MakePinned(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::PinnedLocal, std::move(type), -1);
    v->Name = std::move(name);
    return v;
}

int CountPinnedRegions(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::PinnedRegion) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

int CountTryFinally(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::TryFinally) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Find the (first) TryFinally in the tree, or null.
TryFinally* FindTryFinally(ILFunction& fn) {
    TryFinally* found = nullptr;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst || found) return;
        if (auto* tf = dynamic_cast<TryFinally*>(inst)) { found = tf; return; }
        for (int i = 0; i < inst->ChildCount() && !found; ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return found;
}

// Build a redundant try-finally around a PinnedRegion:
//   body:
//     b0: TryFinally { try { b1: PinnedRegion(...) ; leave try } finally { b2: leave fin } }
//         ; leave body
// `trailingLeave` controls whether the try block carries a value-less
// `Leave(tryContainer)` final (the C# 2-instruction shape, the one
// DetectPinnedRegions produces) or a null final (the C# 1-instruction shape).
// `bodyFinal` is the block's outer final (a Leave of the body).
struct Built {
    std::unique_ptr<ILFunction> fn;
    BlockContainer* body;
    Block* b0;
    TryFinally* tf;
    BlockContainer* tryC;
    BlockContainer* finC;
    PinnedRegion* pr;
};

Built BuildRedundant(bool trailingLeave) {
    Built out;
    out.fn = std::make_unique<ILFunction>();
    out.body = new BlockContainer();  // owned by fn->Body
    out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
    out.body->Parent = out.fn.get();
    out.body->ChildIndex = 0;

    auto P = MakePinned("P", std::make_shared<KnownType>(KnownTypeCode::String));
    out.fn->Variables.push_back(P);

    // try container: one block holding the PinnedRegion (+ optional leave final).
    auto tryC = std::make_unique<BlockContainer>();
    auto tryBlock = std::make_unique<Block>();
    auto prBody = std::make_unique<BlockContainer>();
    auto prBodyBlock = std::make_unique<Block>();
    prBodyBlock->SetFinal(std::make_unique<Leave>(prBody.get()));
    prBody->AddBlock(std::move(prBodyBlock));
    auto pr = std::make_unique<PinnedRegion>(P, std::make_unique<LdLoc>(P), std::move(prBody));
    out.pr = pr.get();
    tryBlock->Add(std::move(pr));
    tryC->AddBlock(std::move(tryBlock));
    out.tryC = tryC.get();

    // finally container: one block whose single instruction is a value-less
    // Leave of the finally container (an empty finally).
    auto finC = std::make_unique<BlockContainer>();
    auto finBlock = std::make_unique<Block>();
    finBlock->SetFinal(std::make_unique<Leave>(finC.get()));
    finC->AddBlock(std::move(finBlock));
    out.finC = finC.get();

    auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
    out.tf = tf.get();

    // b0 holds the TryFinally as a non-terminal, followed by a Leave(body) final.
    auto b0 = std::make_unique<Block>();
    b0->Add(std::move(tf));
    b0->SetFinal(std::make_unique<Leave>(out.body));
    out.b0 = b0.get();
    out.body->AddBlock(std::move(b0));

    // Set the try block's trailing final after the structure is wired.
    if (trailingLeave) {
        // The try block is tryC->Blocks[0]; give it a value-less Leave of tryC.
        out.tryC->Blocks[0]->SetFinal(std::make_unique<Leave>(out.tryC));
    } else {
        out.tryC->Blocks[0]->FinalInstruction.reset();
    }
    return out;
}

} // namespace

// Positive: the C# 2-instruction shape (PinnedRegion + a value-less Leave of
// the try container final) folds to just the PinnedRegion.
TEST(ReduceNestingTransform, FoldsRedundantTryFinallyWithTrailingLeave) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    ASSERT_EQ(CountTryFinally(*fn), 1);
    ASSERT_EQ(CountPinnedRegions(*fn), 1);
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 0) << "redundant try-finally removed";
    EXPECT_EQ(CountPinnedRegions(*fn), 1) << "PinnedRegion preserved";
    // The PinnedRegion now sits where the TryFinally was (b0's first instruction).
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    ASSERT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions[0]->Op, OpCode::PinnedRegion);
}

// Positive: the C# 1-instruction shape (PinnedRegion with no trailing final)
// also folds.
TEST(ReduceNestingTransform, FoldsRedundantTryFinallyWithoutTrailingLeave) {
    auto built = BuildRedundant(/*trailingLeave=*/false);
    auto& fn = built.fn;
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 0);
    EXPECT_EQ(CountPinnedRegions(*fn), 1);
}

// Negative: a finally that carries a value (a valued return) is not duplicable
// / not redundant -- the transform must leave the try-finally alone.
TEST(ReduceNestingTransform, RejectsValuedFinallyLeave) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    // Replace the finally's value-less leave with a valued one.
    auto valuedLeave = std::make_unique<Leave>(built.finC);
    valuedLeave->Value = std::make_unique<LdcI4>(7);
    valuedLeave->Value->Parent = valuedLeave.get();
    valuedLeave->Value->ChildIndex = 0;
    built.finC->Blocks[0]->SetFinal(std::move(valuedLeave));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "valued finally leave is not redundant";
}

// Negative: a finally with more than just the empty leave (an extra non-terminal)
// is not a single-instruction finally.
TEST(ReduceNestingTransform, RejectsNonEmptyFinally) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    built.finC->Blocks[0]->Add(std::make_unique<LdcI4>(0));  // extra non-terminal
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "non-empty finally not eliminated";
}

// Negative: a try with more than one block is not a single-block try.
TEST(ReduceNestingTransform, RejectsMultiBlockTry) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    auto extra = std::make_unique<Block>();
    extra->SetFinal(std::make_unique<Leave>(built.tryC));
    built.tryC->AddBlock(std::move(extra));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "multi-block try not eliminated";
}

// Negative: the try block's single non-terminal is not a PinnedRegion.
TEST(ReduceNestingTransform, RejectsNonPinnedRegionTry) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    // Swap the PinnedRegion for a plain StLoc. Capture the variable before
    // clearing the Instructions (the clear destroys the PinnedRegion that
    // `built.pr` points into).
    auto P = built.pr->Variable;
    built.tryC->Blocks[0]->Instructions.clear();
    built.tryC->Blocks[0]->Add(std::make_unique<StLoc>(P, std::make_unique<LdcI4>(0)));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "non-PinnedRegion try not eliminated";
}

// Negative: the try block's trailing final is a Branch, not a value-less Leave
// of the try container (the C# Count==2 case requires MatchLeave(tryContainer)).
TEST(ReduceNestingTransform, RejectsTrailingBranchFinal) {
    auto built = BuildRedundant(/*trailingLeave=*/false);
    auto& fn = built.fn;
    // Give the try block a Branch final (to a second block in the try container)
    // instead of a Leave of the try container.
    auto extra = std::make_unique<Block>();
    extra->SetFinal(std::make_unique<Leave>(built.tryC));
    Block* extraPtr = extra.get();
    built.tryC->AddBlock(std::move(extra));
    built.tryC->Blocks[0]->SetFinal(std::make_unique<Branch>(extraPtr));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "trailing Branch final not a leave of try";
}

// Negative: the try block's trailing final is a Leave of a *different* container.
TEST(ReduceNestingTransform, RejectsTrailingLeaveOfOtherContainer) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    // Point the try block's trailing leave at the body container, not the try.
    built.tryC->Blocks[0]->SetFinal(std::make_unique<Leave>(built.body));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "trailing leave of other container rejected";
}

// Negative: the finally's single leave targets a different container (not the
// finally container itself).
TEST(ReduceNestingTransform, RejectsFinallyLeaveOfOtherContainer) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    built.finC->Blocks[0]->SetFinal(std::make_unique<Leave>(built.body));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "finally leave of other container rejected";
}

// Multiple redundant try-finallys in the same function all fold (the re-walk
// loop handles each in turn).
TEST(ReduceNestingTransform, FoldsMultipleRedundantTryFinallys) {
    auto fn = std::make_unique<ILFunction>();
    auto body = std::make_unique<BlockContainer>();
    body->Parent = fn.get();
    body->ChildIndex = 0;
    auto P = MakePinned("P", std::make_shared<KnownType>(KnownTypeCode::String));
    fn->Variables.push_back(P);

    auto makeBlock = [&]() {
        auto tryC = std::make_unique<BlockContainer>();
        auto tryBlock = std::make_unique<Block>();
        auto prBody = std::make_unique<BlockContainer>();
        auto prBodyBlock = std::make_unique<Block>();
        prBodyBlock->SetFinal(std::make_unique<Leave>(prBody.get()));
        prBody->AddBlock(std::move(prBodyBlock));
        tryBlock->Add(std::make_unique<PinnedRegion>(P, std::make_unique<LdLoc>(P), std::move(prBody)));
        tryBlock->SetFinal(std::make_unique<Leave>(tryC.get()));
        tryC->AddBlock(std::move(tryBlock));
        auto finC = std::make_unique<BlockContainer>();
        auto finBlock = std::make_unique<Block>();
        finBlock->SetFinal(std::make_unique<Leave>(finC.get()));
        finC->AddBlock(std::move(finBlock));
        return std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
    };

    auto b0 = std::make_unique<Block>();
    b0->Add(makeBlock());
    b0->Add(makeBlock());
    b0->SetFinal(std::make_unique<Leave>(body.get()));
    body->AddBlock(std::move(b0));
    fn->Body = std::move(body);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountTryFinally(*fn), 2);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 0) << "both redundant try-finallys removed";
    EXPECT_EQ(CountPinnedRegions(*fn), 2) << "both PinnedRegions preserved";
}

// An outer redundant try-finally wrapping an inner one: folding the outer
// destroys the inner (it lived in the outer's try block). The re-walk must not
// dangle -- the inner is gone, no crash, and the PinnedRegion the outer wrapped
// survives. (The inner's own PinnedRegion is destroyed with the inner try-finally;
// only the outer's PinnedRegion survives.)
TEST(ReduceNestingTransform, FoldsOuterTryFinallyWithoutDanglingInner) {
    auto fn = std::make_unique<ILFunction>();
    auto body = std::make_unique<BlockContainer>();
    body->Parent = fn.get();
    body->ChildIndex = 0;
    auto P = MakePinned("P", std::make_shared<KnownType>(KnownTypeCode::String));
    fn->Variables.push_back(P);

    // Inner try-finally (redundant, wraps a PinnedRegion).
    auto innerTryC = std::make_unique<BlockContainer>();
    auto innerTryBlock = std::make_unique<Block>();
    auto innerPrBody = std::make_unique<BlockContainer>();
    auto innerPrBodyBlock = std::make_unique<Block>();
    innerPrBodyBlock->SetFinal(std::make_unique<Leave>(innerPrBody.get()));
    innerPrBody->AddBlock(std::move(innerPrBodyBlock));
    innerTryBlock->Add(std::make_unique<PinnedRegion>(P, std::make_unique<LdLoc>(P), std::move(innerPrBody)));
    innerTryBlock->SetFinal(std::make_unique<Leave>(innerTryC.get()));
    innerTryC->AddBlock(std::move(innerTryBlock));
    auto innerFinC = std::make_unique<BlockContainer>();
    auto innerFinBlock = std::make_unique<Block>();
    innerFinBlock->SetFinal(std::make_unique<Leave>(innerFinC.get()));
    innerFinC->AddBlock(std::move(innerFinBlock));
    auto inner = std::make_unique<TryFinally>(std::move(innerTryC), std::move(innerFinC));

    // Outer try-finally (redundant): its try block is the inner try-finally
    // followed by a value-less Leave of the outer try container.
    auto outerTryC = std::make_unique<BlockContainer>();
    auto outerTryBlock = std::make_unique<Block>();
    outerTryBlock->Add(std::move(inner));  // inner try-finally is the try content
    outerTryBlock->SetFinal(std::make_unique<Leave>(outerTryC.get()));
    outerTryC->AddBlock(std::move(outerTryBlock));
    auto outerFinC = std::make_unique<BlockContainer>();
    auto outerFinBlock = std::make_unique<Block>();
    outerFinBlock->SetFinal(std::make_unique<Leave>(outerFinC.get()));
    outerFinC->AddBlock(std::move(outerFinBlock));
    auto outer = std::make_unique<TryFinally>(std::move(outerTryC), std::move(outerFinC));

    auto b0 = std::make_unique<Block>();
    b0->Add(std::move(outer));
    b0->SetFinal(std::make_unique<Leave>(body.get()));
    body->AddBlock(std::move(b0));
    fn->Body = std::move(body);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountTryFinally(*fn), 2);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 0) << "outer fold destroyed the inner too";
    EXPECT_EQ(CountPinnedRegions(*fn), 1)
        << "outer's PinnedRegion survives; inner's was inside the outer's try block";
}

// A non-redundant try-finally (the try has a PinnedRegion PLUS a real trailing
// statement, so Instructions.size() != 1) is left alone even if the finally is
// empty.
TEST(ReduceNestingTransform, RejectsTryWithExtraNonTerminal) {
    auto built = BuildRedundant(/*trailingLeave=*/true);
    auto& fn = built.fn;
    // Add an extra non-terminal after the PinnedRegion in the try block.
    built.tryC->Blocks[0]->Add(std::make_unique<LdcI4>(0));
    fn->CheckInvariant(ILPhase::Normal);

    ReduceNestingTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountTryFinally(*fn), 1) << "try with extra statement not eliminated";
}

// Corpus sweep: run the full pre-pipeline (through HighLevelLoopTransform, the
// CLI's ReduceNestingTransform position) so real PinnedRegions exist, then
// ReduceNestingTransform, and assert the ILAst invariant holds across the
// corpus. The redundant-try-finally-around-PinnedRegion shape is a C#-compiler
// artifact whose occurrence is corpus-dependent; the invariant (no crash / no
// tree corruption) is the gate.
TEST(ReduceNestingTransform, MscorlibSweepPreservesInvariant) {
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
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ILSpy::Decompiler::IL::ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Pre-pipeline through HighLevelLoopTransform (the CLI's
        // ReduceNestingTransform position), so DetectPinnedRegions has formed
        // real PinnedRegions before ReduceNestingTransform runs.
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        EarlyExpressionTransforms().Run(*fn, ctx);
        RemoveDeadVariableInit().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);
        SwitchDetection().Run(*fn, ctx);
        SwitchOnNullableTransform().Run(*fn, ctx);
        LoopDetection().Run(*fn, ctx);
        PatternMatchingTransform().Run(*fn, ctx);
        ConditionDetection().Run(*fn, ctx);
        HighLevelLoopTransform::Run(*fn, ctx);

        int before = CountTryFinally(*fn);
        ReduceNestingTransform().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        int after = CountTryFinally(*fn);
        if (before > after) totalFolds += (before - after);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The fold count is corpus-dependent (the redundant try-finally around a
    // `fixed` block is a C#-compiler artifact); the invariant check above is
    // the correctness gate. Assert at least the sweep ran and did not corrupt
    // the tree on any method.
    (void)totalFolds;
}
