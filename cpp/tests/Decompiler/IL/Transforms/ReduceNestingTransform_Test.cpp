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
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/StackType.hpp"
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
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
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

// ---------------------------------------------------------------------------
// Nesting-reduction heuristics (ComputeStats / UpdateStats / ShouldReduceNesting)
// + the self-contained pattern helpers. Ported as a tested-but-not-yet-wired
// foundation; these exercise the helpers indirectly through the public static
// heuristics on ReduceNestingTransform.

namespace {

// A void function body: one BlockContainer with one block holding the given
// non-terminal instructions and a value-less Leave(body) final.
struct BodyBlock {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* blk;
};

BodyBlock MakeBodyBlock(std::vector<std::unique_ptr<ILInstruction>> stmts) {
	BodyBlock out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;
	auto blk = std::make_unique<Block>();
	for (auto& s : stmts) blk->Add(std::move(s));
	blk->SetFinal(std::make_unique<Leave>(out.body));
	out.blk = blk.get();
	out.body->AddBlock(std::move(blk));
	return out;
}

ILVariablePtr MakeLocal(std::string name) {
	auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, -1);
	v->Name = std::move(name);
	return v;
}

std::unique_ptr<StLoc> StLocInt(ILVariablePtr v, int value) {
	return std::make_unique<StLoc>(v, std::make_unique<LdcI4>(value));
}

} // namespace

// A bare Block with two StLoc statements + a Leave final: the block counts its
// instructions as statements (the Leave is the control flow, counted the way the
// C# counts Instructions.Last()). The block itself is un-counted.
TEST(ReduceNestingTransform, ComputeStatsCountsBlockStatements) {
	auto [fn, body, blk] = ([]() {
		auto v1 = MakeLocal("a"), v2 = MakeLocal("b");
		std::vector<std::unique_ptr<ILInstruction>> stmts;
		stmts.push_back(StLocInt(v1, 1));
		stmts.push_back(StLocInt(v2, 2));
		return MakeBodyBlock(std::move(stmts));
	})();
	(void)fn; (void)body;
	fn->CheckInvariant(ILPhase::Normal);
	int numStatements = 0, maxDepth = 0;
	ReduceNestingTransform::ComputeStats(blk, numStatements, maxDepth, 0, true);
	// 2 StLocs + 1 Leave (the control flow) = 3 statements; the block itself is
	// un-counted. Depth 0 (the block is at the top level).
	EXPECT_EQ(numStatements, 3);
	EXPECT_EQ(maxDepth, 0);
}

// A Normal BlockContainer's implicit leave (the trailing Leave(body)) is not
// counted: 2 StLocs + a Leave => the container counts as 1 + 2 StLocs = 3, the
// Leave is net 0 (isImplicitExit). Depth 1 (the body block is nested).
TEST(ReduceNestingTransform, ComputeStatsNormalContainerDropsImplicitLeave) {
	auto built = ([]() {
		auto v1 = MakeLocal("a"), v2 = MakeLocal("b");
		std::vector<std::unique_ptr<ILInstruction>> stmts;
		stmts.push_back(StLocInt(v1, 1));
		stmts.push_back(StLocInt(v2, 2));
		return MakeBodyBlock(std::move(stmts));
	})();
	built.fn->CheckInvariant(ILPhase::Normal);
	int numStatements = 0, maxDepth = 0;
	ReduceNestingTransform::ComputeStats(built.body, numStatements, maxDepth, 0, true);
	EXPECT_EQ(numStatements, 3);
	EXPECT_EQ(maxDepth, 1);
}

// An if with a then-block of one statement: the if (void) counts the then-block
// at depth+1, the else (a Nop / null) at depth+1. The then-block has 1 statement
// + a Leave final.
TEST(ReduceNestingTransform, ComputeStatsNestedIfAddsDepth) {
	auto built = ([]() {
		auto v1 = MakeLocal("a");
		std::vector<std::unique_ptr<ILInstruction>> stmts;
		// then-block: one StLoc + a Leave(body) final
		auto thenBlk = std::make_unique<Block>();
		thenBlk->Add(StLocInt(v1, 1));
		thenBlk->SetFinal(std::make_unique<Leave>(nullptr));  // leave of some container
		auto iff = std::make_unique<IfInstruction>(
			std::make_unique<LdcI4>(0), std::move(thenBlk), nullptr);
		stmts.push_back(std::move(iff));
		return MakeBodyBlock(std::move(stmts));
	})();
	built.fn->CheckInvariant(ILPhase::Normal);
	int numStatements = 0, maxDepth = 0;
	ReduceNestingTransform::ComputeStats(built.body, numStatements, maxDepth, 0, true);
	// container(1) - implicit(1) + body: if-then-block(1 StLoc + 1 Leave = 2) +
	// the if counts the then at depth+1 and the else (null) at depth+1.
	// maxDepth should be >= 2 (the if body is at depth 2).
	EXPECT_GE(maxDepth, 2);
	EXPECT_GT(numStatements, 0);
}

// ShouldReduceNesting: a shallow block (depth 0) that is not the largest and
// not twice as large as the largest sibling does NOT reduce.
TEST(ReduceNestingTransform, ShouldReduceNestingDepthHeuristic) {
	auto v1 = MakeLocal("a");
	auto shallow = std::make_unique<Block>();
	shallow->Add(StLocInt(v1, 1));
	shallow->SetFinal(std::make_unique<Leave>(nullptr));
	// maxStatements2 = 2 (1 StLoc + 1 Leave), maxDepth2 = 0; siblings have 10
	// statements -> 2 >= 2*10 is false, 0 >= 2 is false, 0 >= 1 is false => no
	// reduction.
	EXPECT_FALSE(ReduceNestingTransform::ShouldReduceNesting(shallow.get(), 10, 0));
}

// ShouldReduceNesting: a block twice as large as any sibling reduces even at
// shallow depth (maxStatements2 >= 2 * maxStatements).
TEST(ReduceNestingTransform, ShouldReduceNestingSizeHeuristic) {
	auto v1 = MakeLocal("a");
	// A block with 4 statements (4x a sibling's 1): 2*1 = 2 <= 4 => reduce.
	auto big = std::make_unique<Block>();
	big->Add(StLocInt(v1, 1));
	big->Add(StLocInt(v1, 2));
	big->Add(StLocInt(v1, 3));
	big->Add(StLocInt(v1, 4));
	big->SetFinal(std::make_unique<Leave>(nullptr));
	EXPECT_TRUE(ReduceNestingTransform::ShouldReduceNesting(big.get(), 1, 0));
}

// A mscorlib sweep: run ComputeStats over decoded method bodies, asserting it
// does not crash and produces non-negative, finite stats (the heuristics must be
// robust on real trees).
TEST(ReduceNestingTransform, MscorlibComputeStatsSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());
	int processed = 0;
	int maxDepthSeen = 0;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn || !fn->Body) continue;
		++processed;
		int numStatements = 0, maxDepth = 0;
		ReduceNestingTransform::ComputeStats(fn->Body.get(), numStatements, maxDepth, 0, true);
		EXPECT_GE(numStatements, 0);
		EXPECT_GE(maxDepth, 0);
		maxDepthSeen = std::max(maxDepthSeen, maxDepth);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000);
	EXPECT_GT(maxDepthSeen, 0) << "some method should have nested depth";
}

// ImproveILOrdering corpus probe: runs the full pre-pipeline through
// HighLevelLoopTransform (the CLI's ReduceNestingTransform position), then for
// each if-as-FinalInstruction candidate (TrueInst unreachable + FalseInst null +
// next block single-pred) checks the IL-order gate (GetStartILOffset(TrueInst)
// vs GetStartILOffset(nextBlock)) and fires InvertIf on the would-fire
// candidates, asserting the ILAst invariant holds. The C# ImproveILOrdering
// operates on an if that is a NON-TERMINAL in the block with the falseCode+exit
// as siblings; this port's if-as-FinalInstruction model makes the falseCode+exit
// the next block, so the probe pins the real post-ConditionDetection shape. The
// gate fires when ConditionDetection's inversion put the code in the "wrong" IL
// order (the old then / next block comes BEFORE the falseCode / TrueInst in IL);
// the GetStartILOffset Block-label adaptation (the Block's own StartILOffset
// field = the first instruction's offset) makes the offsets valid for Block
// TrueInsts and the next block, so the gate fires on the corpus (a real-corpus
// transform, not faithfulness-only). The bare-Leave TrueInst path (the
// no-falseCode case whose Leave lost its base ILRange during the pre-pipeline)
// still bails at the gate -- the ILRange propagation through that path is a
// separate piece.

namespace {

bool IsLeavingFunction(Leave* leave) {
	return leave && leave->TargetContainer && leave->TargetContainer->Parent &&
	       leave->TargetContainer->Parent->Op == OpCode::ILFunction;
}

// The "exit" of the TrueInst: if the TrueInst is a Block wrapping [falseCode,
// exit], the exit is the Block's FinalInstruction (the control flow); if the
// TrueInst is a bare exit (a Leave/Branch/Throw), it is the TrueInst itself.
ILInstruction* TrueInstExit(ILInstruction* trueInst) {
	if (!trueInst) return nullptr;
	if (auto* b = dynamic_cast<Block*>(trueInst)) return b->FinalInstruction.get();
	return trueInst;
}

int CountIfFinals(ILFunction& fn) {
	int n = 0;
	std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
		if (!inst) return;
		if (auto* b = dynamic_cast<Block*>(inst)) {
			if (dynamic_cast<IfInstruction*>(b->FinalInstruction.get())) ++n;
		}
		for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
	};
	walk(fn.Body.get());
	return n;
}

} // namespace

TEST(ReduceNestingTransform, MscorlibImproveILOrderingShapeProbe) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int candidates = 0;            // if-final + TrueInst unreachable + FalseInst null
	int singlePred = 0;            // ... and next block IncomingEdgeCount == 1
	int gateWouldFire = 0;          // ... and falseRangeStart < trueRangeStart
	int nonKeywordLeaveBail = 0;    // ... and the exit is a non-keyword Leave (the deferred trailing-leave case)
	int fires = 0;                 // InvertIf actually fired (one per function)
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		// Full pre-pipeline through HighLevelLoopTransform (the CLI's
		// ReduceNestingTransform position).
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

		bool firedThisFn = false;
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (auto* container = dynamic_cast<BlockContainer*>(inst)) {
				for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
					auto* iff = dynamic_cast<IfInstruction*>(container->Blocks[i]->FinalInstruction.get());
					if (!iff || !iff->TrueInst) continue;
					if (!HasFlag(iff->TrueInst->Flags(), InstructionFlags::EndPointUnreachable)) continue;
					if (iff->FalseInst) continue;  // has an else
					++candidates;
					Block* nextBlock = (i + 1 < container->Blocks.size())
					    ? container->Blocks[i + 1].get() : nullptr;
					if (!nextBlock || nextBlock->IncomingEdgeCount != 1) continue;
					++singlePred;
					bool trueEmpty = false, falseEmpty = false;
					int trueStart = ConditionDetection::GetStartILOffset(iff->TrueInst.get(), trueEmpty);
					int falseStart = ConditionDetection::GetStartILOffset(nextBlock, falseEmpty);
					if (trueEmpty || falseEmpty || !(falseStart < trueStart)) continue;
					++gateWouldFire;
					// The trailing-leave guard: a non-keyword Leave exit (a leave
					// of a Normal container that is not the function) bails (the
					// C# CanDuplicateExit replacement is deferred).
					if (auto* leave = dynamic_cast<Leave*>(TrueInstExit(iff->TrueInst.get()))) {
						if (!IsLeavingFunction(leave) && leave->TargetContainer &&
						    leave->TargetContainer->Kind == ContainerKind::Normal) {
							++nonKeywordLeaveBail;
							continue;
						}
					}
					if (firedThisFn) return;  // one InvertIf per function
					firedThisFn = true;
					ConditionDetection::InvertIf(container->Blocks[i].get(), iff);
					fn->CheckInvariant(ILPhase::Normal);
					++fires;
				}
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		if (processed >= 8000) break;
	}
	EXPECT_GT(processed, 5000) << "the sweep must exercise real methods";
	// The gate fires on the corpus (ConditionDetection's inversion put the code
	// in the wrong IL order for some methods); the invariant check inside the
	// loop is the safety gate -- InvertIf must not corrupt the tree on any real
	// would-fire shape.
	EXPECT_GT(gateWouldFire, 0) << "the IL-order gate fires on the corpus";
	EXPECT_GT(fires, 0) << "InvertIf fired on the would-fire candidates";
	std::cerr << "ImproveILOrdering shape probe: processed=" << processed
	          << " candidates=" << candidates
	          << " singlePred=" << singlePred
	          << " gateWouldFire=" << gateWouldFire
	          << " nonKeywordLeaveBail=" << nonKeywordLeaveBail
	          << " fires=" << fires << "\n";
	(void)candidates; (void)singlePred; (void)nonKeywordLeaveBail;
}

// ImproveILOrdering through the pre-pipeline shape: an early-return if whose
// then (the return) is at a HIGHER IL offset than the fall-through (the rest of
// the method) inverts to match IL order. The hand-built shape mirrors the real
// post-ConditionDetection shape (the if is the block's FinalInstruction with
// TrueInst = the inlined falseCode+exit [unreachable], FalseInst = null,
// fall-through to the old then / next block). After ReduceNestingTransform runs
// ImproveILOrdering, the if is inverted: the condition is negated, the old then
// (the next block) moves into the if's TrueInst, and the falseCode (the old
// TrueInst) moves into the next block. The gate fires because the next block
// (the old then) comes BEFORE the TrueInst (the falseCode) in IL.
TEST(ReduceNestingTransform, ImproveILOrderingInvertsToMatchILOrder) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// block0: if (v == 0) { Block { stloc v(2); leave body } }  (TrueInst = the
	// inlined falseCode+exit, unreachable; FalseInst = null; fall-through to
	// block1). The TrueInst (a Block) starts at IL_000A; block1 (the old then)
	// starts at IL_0002. IL_0002 < IL_000A -> the gate fires and
	// ImproveILOrdering inverts.
	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	auto trueInstBlock = std::make_unique<Block>();
	trueInstBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
	trueInstBlock->SetFinal(std::make_unique<Leave>(body.get()));
	trueInstBlock->StartILOffset = 10;  // the falseCode (TrueInst) starts at IL_000A
	trueInstBlock->RenumberChildren();
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(trueInstBlock), nullptr);
	b0->SetFinal(std::move(iff));
	body->AddBlock(std::move(b0));
	// block1: the old then (a leave of body), at IL_0002 < IL_000A.
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 2;
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	ASSERT_EQ(CountIfFinals(*fn), 1);

	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	// The if is inverted: the condition is negated. NegateCondition folds
	// `comp(eq, v, 0)` to the bare `ldloc v` (the `comp(x == 0) => x` unwrap,
	// the negation `!(v == 0)` = `v != 0` = `v` as a bool), so the condition is
	// now a LdLoc, not a Comp.
	auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
	ASSERT_NE(iff2, nullptr);
	ASSERT_NE(iff2->TrueInst, nullptr);
	EXPECT_EQ(iff2->Condition->Op, OpCode::LdLoc) << "the condition was negated (comp(eq,v,0) => ldloc v)";
}

// Negative: when the IL order is already correct (the next block / old then
// comes AFTER the TrueInst / falseCode in IL), ImproveILOrdering does NOT invert
// -- the gate bails (falseRangeStart >= trueStart).
TEST(ReduceNestingTransform, ImproveILOrderingNoOpWhenILOrderCorrect) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	auto trueInstBlock = std::make_unique<Block>();
	trueInstBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
	trueInstBlock->SetFinal(std::make_unique<Leave>(body.get()));
	trueInstBlock->StartILOffset = 2;  // the falseCode (TrueInst) starts at IL_0002
	trueInstBlock->RenumberChildren();
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(trueInstBlock), nullptr);
	b0->SetFinal(std::move(iff));
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 10;  // the old then (next block) at IL_000A > IL_0002 -- IL order correct
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	// The if is NOT inverted (the condition stays Equality).
	auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
	ASSERT_NE(iff2, nullptr);
	auto* cond = dynamic_cast<Comp*>(iff2->Condition.get());
	ASSERT_NE(cond, nullptr);
	EXPECT_EQ(cond->Kind, ComparisonKind::Equality) << "the gate bails when the IL order is correct";
}

// ---------------------------------------------------------------------------
// CanDuplicateExit (a tested-but-not-yet-wired foundation): the helper that
// decides whether an exit is a duplicable keyword exit (return/break/continue),
// walking out of a try/pinned/lock container to the following instruction when
// the exit is a leave of a Normal container. The wired ImproveILOrdering
// trailing-leave handling and the deferred ReduceNesting / ReduceSwitchNesting
// folds consult it; no pipeline transform consults it yet.

namespace {

// A void function body: one BlockContainer (parent = the ILFunction) with one
// block holding the given non-terminal instructions and a value-less Leave(body)
// final. The body's Parent is the ILFunction so a Leave(body) is a return.
struct ExitBody {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* blk;
	Leave* bodyLeave;  // the Leave(body) final (a return)
};

ExitBody MakeExitBody(std::vector<std::unique_ptr<ILInstruction>> stmts) {
	ExitBody out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;
	auto blk = std::make_unique<Block>();
	for (auto& s : stmts) blk->Add(std::move(s));
	auto bodyLeave = std::make_unique<Leave>(out.body);
	out.bodyLeave = bodyLeave.get();
	blk->SetFinal(std::move(bodyLeave));
	out.blk = blk.get();
	out.body->AddBlock(std::move(blk));
	return out;
}

// Build a try-finally whose try block exits via `leave(tryC)` (the exit passed to
// CanDuplicateExit) and whose finally exits via `leave(finC)` (a normal
// finally). The TryFinally is a non-terminal in `blk`, followed by a Leave(body)
// final (the return after the try-finally -- the keyword exit the walk finds).
//   body (parent = ILFunction)
//     blk: TryFinally { tryC { tryB: leave(tryC) } finally { finC { finB: leave(finC) } } }
//          ; leave(body)
struct TryFinallyWalk {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* blk;
	TryFinally* tf;
	BlockContainer* tryC;
	BlockContainer* finC;
	Leave* tryLeave;   // the exit (leave tryC)
	Leave* bodyLeave;  // the return after the try-finally
};

TryFinallyWalk MakeTryFinallyWalk() {
	TryFinallyWalk out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;

	auto tryC = std::make_unique<BlockContainer>();  // Normal
	auto tryB = std::make_unique<Block>();
	auto tryLeave = std::make_unique<Leave>(tryC.get());
	out.tryLeave = tryLeave.get();
	tryB->SetFinal(std::move(tryLeave));
	tryC->AddBlock(std::move(tryB));
	out.tryC = tryC.get();

	auto finC = std::make_unique<BlockContainer>();  // Normal
	auto finB = std::make_unique<Block>();
	finB->SetFinal(std::make_unique<Leave>(finC.get()));  // normal finally exit
	finC->AddBlock(std::move(finB));
	out.finC = finC.get();

	auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
	out.tf = tf.get();

	auto blk = std::make_unique<Block>();
	blk->Add(std::move(tf));
	auto bodyLeave = std::make_unique<Leave>(out.body);
	out.bodyLeave = bodyLeave.get();
	blk->SetFinal(std::move(bodyLeave));
	out.blk = blk.get();
	out.body->AddBlock(std::move(blk));
	return out;
}

} // namespace

// A Branch to the continue target is a duplicable `continue` (keywordExit = the
// Branch itself).
TEST(ReduceNestingTransform, CanDuplicateExitContinueBranch) {
	auto cont = std::make_unique<BlockContainer>();
	auto target = std::make_unique<Block>();
	Block* targetPtr = target.get();
	cont->AddBlock(std::move(target));
	auto br = std::make_unique<Branch>(targetPtr);
	ILInstruction* kw = nullptr;
	EXPECT_TRUE(ReduceNestingTransform::CanDuplicateExit(br.get(), targetPtr, kw));
	EXPECT_EQ(kw, br.get()) << "the keyword exit is the continue Branch itself";
}

// A Leave of the function body is a duplicable `return`.
TEST(ReduceNestingTransform, CanDuplicateExitReturnLeave) {
	auto built = MakeExitBody({});
	built.fn->CheckInvariant(ILPhase::Normal);
	ILInstruction* kw = nullptr;
	EXPECT_TRUE(ReduceNestingTransform::CanDuplicateExit(built.bodyLeave, nullptr, kw));
	EXPECT_EQ(kw, built.bodyLeave) << "the keyword exit is the return Leave itself";
}

// A Leave of a Loop container (not the function) is a duplicable `break`.
TEST(ReduceNestingTransform, CanDuplicateExitBreakLeave) {
	auto loopC = std::make_unique<BlockContainer>();
	loopC->Kind = ContainerKind::Loop;
	auto leaveLoop = std::make_unique<Leave>(loopC.get());
	ILInstruction* kw = nullptr;
	EXPECT_TRUE(ReduceNestingTransform::CanDuplicateExit(leaveLoop.get(), nullptr, kw));
	EXPECT_EQ(kw, leaveLoop.get()) << "the keyword exit is the break Leave itself";
}

// A valued Leave (a `return expr`) is NOT duplicable -- duplicating it would
// re-evaluate the value.
TEST(ReduceNestingTransform, CanDuplicateExitRejectsValuedReturn) {
	auto built = MakeExitBody({});
	built.fn->CheckInvariant(ILPhase::Normal);
	auto valuedLeave = std::make_unique<Leave>(built.body);
	valuedLeave->Value = std::make_unique<LdcI4>(7);
	valuedLeave->Value->Parent = valuedLeave.get();
	valuedLeave->Value->ChildIndex = 0;
	built.blk->SetFinal(std::move(valuedLeave));  // replace the body leave (SetFinal sets Parent)
	built.fn->CheckInvariant(ILPhase::Normal);
	ILInstruction* kw = nullptr;
	EXPECT_FALSE(ReduceNestingTransform::CanDuplicateExit(
	    built.blk->FinalInstruction.get(), nullptr, kw));
}

// A non-Leave, non-Branch exit (e.g. a LdLoc) is not duplicable.
TEST(ReduceNestingTransform, CanDuplicateExitRejectsNonLeaveNonBranch) {
	auto v = MakeLocal("v");
	auto ld = std::make_unique<LdLoc>(v);
	ILInstruction* kw = nullptr;
	EXPECT_FALSE(ReduceNestingTransform::CanDuplicateExit(ld.get(), nullptr, kw));
}

// A Leave of a Normal container inside a try-finally walks out to the
// instruction following the try-finally (a return) and reports that as the
// keyword exit. The finally exits normally (leave finC) so the walk proceeds.
TEST(ReduceNestingTransform, CanDuplicateExitWalksOutOfTryFinally) {
	auto built = MakeTryFinallyWalk();
	built.fn->CheckInvariant(ILPhase::Normal);
	ILInstruction* kw = nullptr;
	EXPECT_TRUE(ReduceNestingTransform::CanDuplicateExit(built.tryLeave, nullptr, kw));
	EXPECT_EQ(kw, built.bodyLeave)
	    << "the keyword exit is the return after the try-finally";
}

// A Leave of the finally container is NOT duplicable (cannot duplicate leaves
// from finally containers -- the finally must always run).
TEST(ReduceNestingTransform, CanDuplicateExitRejectsFinallyLeave) {
	auto built = MakeTryFinallyWalk();
	built.fn->CheckInvariant(ILPhase::Normal);
	// The finally block's leave (leave finC).
	auto* finLeave = built.finC->Blocks[0]->FinalInstruction.get();
	ASSERT_NE(finLeave, nullptr);
	ILInstruction* kw = nullptr;
	EXPECT_FALSE(ReduceNestingTransform::CanDuplicateExit(finLeave, nullptr, kw));
}

// A Leave of the fault container is NOT duplicable (cannot duplicate leaves
// from fault containers).
TEST(ReduceNestingTransform, CanDuplicateExitRejectsFaultLeave) {
	auto tryC = std::make_unique<BlockContainer>();
	auto tryB = std::make_unique<Block>();
	tryB->SetFinal(std::make_unique<Leave>(tryC.get()));
	tryC->AddBlock(std::move(tryB));
	auto faultC = std::make_unique<BlockContainer>();
	auto faultB = std::make_unique<Block>();
	auto leaveFault = std::make_unique<Leave>(faultC.get());
	Leave* leaveFaultPtr = leaveFault.get();
	faultB->SetFinal(std::move(leaveFault));
	faultC->AddBlock(std::move(faultB));
	auto tflt = std::make_unique<TryFault>(std::move(tryC), std::move(faultC));
	auto blk = std::make_unique<Block>();
	blk->Add(std::move(tflt));
	blk->SetFinal(std::make_unique<Leave>(nullptr));
	ILInstruction* kw = nullptr;
	EXPECT_FALSE(ReduceNestingTransform::CanDuplicateExit(leaveFaultPtr, nullptr, kw));
}

// A Leave of a try container whose finally always throws (no leave of the
// finally container -> the finally's end point is unreachable) is NOT
// duplicable: duplicating the exit would skip the finally that throws.
TEST(ReduceNestingTransform, CanDuplicateExitRejectsUnreachableFinally) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;

	auto tryC = std::make_unique<BlockContainer>();
	auto tryB = std::make_unique<Block>();
	auto tryLeave = std::make_unique<Leave>(tryC.get());
	Leave* tryLeavePtr = tryLeave.get();
	tryB->SetFinal(std::move(tryLeave));
	tryC->AddBlock(std::move(tryB));

	auto finC = std::make_unique<BlockContainer>();
	auto finB = std::make_unique<Block>();
	// The finally always throws (a rethrow): no leave of finC, so the finally's
	// end point is unreachable per the C# BlockContainer.ComputeFlags semantics.
	finB->SetFinal(std::make_unique<Throw>(nullptr));
	finC->AddBlock(std::move(finB));

	auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
	auto blk = std::make_unique<Block>();
	blk->Add(std::move(tf));
	blk->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(blk));
	fn->Body = std::move(body);
	fn->CheckInvariant(ILPhase::Normal);

	ILInstruction* kw = nullptr;
	EXPECT_FALSE(ReduceNestingTransform::CanDuplicateExit(tryLeavePtr, nullptr, kw))
	    << "a finally that always throws makes the exit non-duplicable";
}

// Corpus sweep: run the full pre-pipeline through HighLevelLoopTransform (the
// CLI's ReduceNestingTransform position), then call CanDuplicateExit on every
// Leave and Branch in the tree (with continueTarget=null -- the continue
// detection needs the Visit walk's continueTarget tracking, deferred),
// asserting no crash and that the helper is robust on real trees. The recursive
// walk (out of a try/pinned/lock Normal container) fires on real Leaves of such
// containers; the EndPointUnreachableCSharp finally check fires on real
// try-finallys whose finally always throws/returns. The invariant check after
// each function is the safety gate -- CanDuplicateExit (a read-only helper)
// must not corrupt the tree.
TEST(ReduceNestingTransform, MscorlibCanDuplicateExitSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int checked = 0;
	int duplicable = 0;
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		// Full pre-pipeline through HighLevelLoopTransform (the CLI's
		// ReduceNestingTransform position), so real try-finallys / pinned / lock
		// containers exist.
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

		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (inst->Op == OpCode::Leave || inst->Op == OpCode::Branch) {
				ILInstruction* kw = nullptr;
				bool dup = ReduceNestingTransform::CanDuplicateExit(inst, nullptr, kw);
				++checked;
				if (dup) ++duplicable;
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		fn->CheckInvariant(ILPhase::Normal);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	EXPECT_GT(checked, 0) << "the corpus carries Leaves/Branches";
	EXPECT_GT(duplicable, 0) << "some returns/breaks are duplicable";
	std::cerr << "CanDuplicateExit sweep: processed=" << processed
	          << " checked=" << checked << " duplicable=" << duplicable << "\n";
}
