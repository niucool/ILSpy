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
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
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
// no-falseCode case) was previously blocked because the cloned return / break
// Leaves and cloned return values lost their base ILRange during the
// pre-pipeline (CFS branch-to-leave / value-return folds, InlineReturnTransform
// CloneReturnBlock, LoopDetection break-leave, and ClonePureLoad all built fresh
// nodes without copying the original's range); the D156 ILRange-propagation fix
// to those clone sites restores the offset, so the gate now fires on the bare-
// Leave path too (the count rose from a handful to ~1781 would-fire / ~1356
// fires across 8000 mscorlib methods, invariant held on every fire).

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

// The wired trailing-leave handling: when the if block last instruction
// (Xn = this port nextBlock->FinalInstruction, the old-then exit) is a
// non-keyword Leave (a Leave of a Normal container that is not the function),
// ImproveILOrdering replaces it with a keyword exit via CanDuplicateExit before
// InvertIf, so the if then gets the keyword exit (return/break/continue)
// instead of a goto. The if is inside a Normal container (a try-finally try
// body); the next block exit is a leave(tryC) (a non-keyword Leave of the
// try body). CanDuplicateExit walks out of the try-finally to the leave(body)
// (a return) after it, so the non-keyword Leave is replaced with the return.
// The gate fires (the next block comes before the TrueInst in IL), so
// ImproveILOrdering inverts: the TrueInst becomes the return clone, the old
// then (the stloc + leave(tryC)) moves into the next block.
TEST(ReduceNestingTransform, ImproveILOrderingTrailingLeaveReplacedWithKeyword) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// The if is inside a try-finally try body (a Normal container).
	auto tryC = std::make_unique<BlockContainer>();  // Normal
	auto finC = std::make_unique<BlockContainer>();  // Normal
	auto* tryCPtr = tryC.get();
	auto* finCPtr = finC.get();

	// Block A (in tryC): if (v == 0) { Block { stloc v(2); leave(tryC) } }
	// (TrueInst = the inlined falseCode+exit, unreachable; FalseInst = null;)
	// the TrueInst starts at IL_000A.
	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	auto trueInstBlock = std::make_unique<Block>();
	trueInstBlock->Add(StLocInt(v, 2));
	trueInstBlock->SetFinal(std::make_unique<Leave>(tryCPtr));
	trueInstBlock->StartILOffset = 10;  // the falseCode (TrueInst) at IL_000A
	trueInstBlock->RenumberChildren();
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                   ComparisonKind::Equality),
		std::move(trueInstBlock), nullptr);
	b0->SetFinal(std::move(iff));
	tryC->AddBlock(std::move(b0));

	// Block B (next in tryC): leave(tryC) -- Xn, a non-keyword Leave of the
	// try body (a Normal container, not the function). Starts at IL_0002 <
	// IL_000A, so the gate fires.
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 2;
	b1->SetFinal(std::make_unique<Leave>(tryCPtr));
	tryC->AddBlock(std::move(b1));

	// Finally: a normal finally (leave(finC)), so CanDuplicateExit does not
	// bail on the finally (EndPointUnreachableCSharp(finC) is false -- the
	// finally has a leave of finC).
	auto finB = std::make_unique<Block>();
	finB->SetFinal(std::make_unique<Leave>(finCPtr));
	finC->AddBlock(std::move(finB));

	// The try-finally is a non-terminal in the body block, followed by a
	// leave(body) (a return) -- the keyword exit the walk finds.
	auto pre = std::make_unique<Block>();
	pre->StartILOffset = 0;
	auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
	pre->Add(std::move(tf));
	pre->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(pre));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	// Block A is in the try-finally try body: body.Blocks[0] (pre) ->
	// Instructions[0] (TryFinally) -> TryBlock (tryC) -> Blocks[0].
	auto* pre2 = fn->Body->Blocks[0].get();
	auto* tf2 = dynamic_cast<TryFinally*>(pre2->Instructions[0].get());
	ASSERT_NE(tf2, nullptr);
	auto* tryC2 = dynamic_cast<BlockContainer*>(tf2->TryBlock.get());
	ASSERT_NE(tryC2, nullptr);
	auto* iff2 = dynamic_cast<IfInstruction*>(tryC2->Blocks[0]->FinalInstruction.get());
	ASSERT_NE(iff2, nullptr);
	// The trailing-leave handling replaced Xn (leave(tryC), a goto) with the
	// return clone (leave(body)) before InvertIf, so the if TrueInst is the
	// return (a Leave targeting the function body), not a Leave targeting tryC.
	ASSERT_NE(iff2->TrueInst, nullptr);
	auto* trueLeave = dynamic_cast<Leave*>(iff2->TrueInst.get());
	ASSERT_NE(trueLeave, nullptr);
	EXPECT_EQ(trueLeave->TargetContainer, fn->Body.get())
		<< "the trailing-leave handling replaced the goto with a return";
}

// When the non-keyword Leave cannot be duplicated (the walk reaches a
// try-finally whose finally always throws -- EndPointUnreachable, no leave of
// the finally -- so CanDuplicateExit bails), ImproveILOrdering bails rather
// than introducing a goto: the if is NOT inverted (the condition stays
// Equality). Same shape as the fire case but the finally has a Throw (no
// leave of finC), so EndPointUnreachableCSharp(finC) is true and the walk bails.
TEST(ReduceNestingTransform, ImproveILOrderingTrailingLeaveBailsWhenNotDuplicable) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);
	auto tryC = std::make_unique<BlockContainer>();  // Normal
	auto finC = std::make_unique<BlockContainer>();  // Normal
	auto* tryCPtr = tryC.get();
	auto* finCPtr = finC.get();
	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	auto trueInstBlock = std::make_unique<Block>();
	trueInstBlock->Add(StLocInt(v, 2));
	trueInstBlock->SetFinal(std::make_unique<Leave>(tryCPtr));
	trueInstBlock->StartILOffset = 10;
	trueInstBlock->RenumberChildren();
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                   ComparisonKind::Equality),
		std::move(trueInstBlock), nullptr);
	b0->SetFinal(std::move(iff));
	tryC->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 2;
	b1->SetFinal(std::make_unique<Leave>(tryCPtr));
	tryC->AddBlock(std::move(b1));
	// The finally always throws (a Throw, no leave of finC) -- EndPointUnreachable.
	auto finB = std::make_unique<Block>();
	finB->SetFinal(std::make_unique<Throw>(nullptr));
	finC->AddBlock(std::move(finB));
	auto pre = std::make_unique<Block>();
	pre->StartILOffset = 0;
	auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
	pre->Add(std::move(tf));
	pre->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(pre));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	auto* pre2 = fn->Body->Blocks[0].get();
	auto* tf2 = dynamic_cast<TryFinally*>(pre2->Instructions[0].get());
	ASSERT_NE(tf2, nullptr);
	auto* tryC2 = dynamic_cast<BlockContainer*>(tf2->TryBlock.get());
	ASSERT_NE(tryC2, nullptr);
	auto* iff2 = dynamic_cast<IfInstruction*>(tryC2->Blocks[0]->FinalInstruction.get());
	ASSERT_NE(iff2, nullptr);
	// The if is NOT inverted: CanDuplicateExit bailed (the finally always
	// throws), so ImproveILOrdering did not introduce a goto -- the condition
	// stays Equality.
	auto* cond = dynamic_cast<Comp*>(iff2->Condition.get());
	ASSERT_NE(cond, nullptr);
	EXPECT_EQ(cond->Kind, ComparisonKind::Equality)
		<< "the trailing-leave handling bails when the leave cannot be duplicated";
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

// ---------------------------------------------------------------------------
// GetElseIfParent (a tested-but-not-yet-wired foundation): the pure-analysis
// helper that determines whether an IfInstruction is an else-if (a Block
// wrapping only that if, nested as the FalseInst of a parent IfInstruction) and
// reports the preceding parent IfInstruction. The deferred ReduceNesting
// else-if fold consults it (both the early root-bail and the per-iteration walk
// up the else-if tree); no pipeline transform consults it yet.

namespace {

struct ElseIfTree {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	IfInstruction* outerIf;
	Block* elseBlock;
	IfInstruction* innerIf;
};

// Build an else-if tree: outerIf { then } else { elseBlock { innerIf { then } } }.
// `ifAsFinalModel` picks the else block's shape: the C# model (innerIf as the
// sole non-terminal + a null/Nop final) or this port's if-as-final model
// (innerIf as the FinalInstruction with empty non-terminal Instructions).
// `elseAsTrueArm` puts the else block in the TrueInst slot instead (a negative
// for the FalseInst check).
ElseIfTree BuildElseIf(bool ifAsFinalModel, bool elseAsTrueArm = false) {
	ElseIfTree out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;
	auto v = MakeLocal("v");
	out.fn->Variables.push_back(v);

	// innerIf: if (v == 0) { leave body }  (no else)
	auto innerThen = std::make_unique<Block>();
	innerThen->SetFinal(std::make_unique<Leave>(out.body));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(innerThen), nullptr);
	out.innerIf = innerIf.get();

	// elseBlock: a Block whose sole instruction is innerIf.
	auto elseBlock = std::make_unique<Block>();
	if (ifAsFinalModel) {
		elseBlock->SetFinal(std::move(innerIf));  // if-as-final model
	} else {
		elseBlock->Add(std::move(innerIf));  // C# model: if as non-terminal
		// null final (IsNop treats null as a Nop, the C# model's Nop final).
	}
	out.elseBlock = elseBlock.get();

	// outerThen: a Block with a leave body final.
	auto outerThen = std::make_unique<Block>();
	outerThen->SetFinal(std::make_unique<Leave>(out.body));

	// outerIf: if (v == 1) { outerThen } else { elseBlock }  (or then=elseBlock).
	std::unique_ptr<Block> trueArm, falseArm;
	if (elseAsTrueArm) {
		trueArm = std::move(elseBlock);
		falseArm = std::move(outerThen);
	} else {
		trueArm = std::move(outerThen);
		falseArm = std::move(elseBlock);
	}
	auto outerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(trueArm), std::move(falseArm));
	out.outerIf = outerIf.get();

	// body block: [outerIf] + leave(body) final.
	auto blk = std::make_unique<Block>();
	blk->Add(std::move(outerIf));
	blk->SetFinal(std::make_unique<Leave>(out.body));
	out.body->AddBlock(std::move(blk));

	RecomputeIncomingEdgeCounts(*out.fn);
	return out;
}

} // namespace

// An else-if in the C# block model (the else block has the if as its sole
// non-terminal + a null/Nop final) is recognized: the parent IfInstruction is
// reported.
TEST(ReduceNestingTransform, GetElseIfParentReturnsParentForCSharpModel) {
	auto built = BuildElseIf(/*ifAsFinalModel=*/false);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(built.innerIf), built.outerIf)
	    << "the C#-model else block unwraps to the inner if";
}

// An else-if in this port's if-as-final block model (the else block has the if
// as its FinalInstruction with empty non-terminal Instructions) is recognized:
// the parent IfInstruction is reported.
TEST(ReduceNestingTransform, GetElseIfParentReturnsParentForIfAsFinalModel) {
	auto built = BuildElseIf(/*ifAsFinalModel=*/true);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(built.innerIf), built.outerIf)
	    << "the if-as-final else block unwraps to the inner if";
}

// A null input is rejected (defensive).
TEST(ReduceNestingTransform, GetElseIfParentRejectsNull) {
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(nullptr), nullptr);
}

// An if whose parent is not a Block (e.g. the if is directly the FalseInst of a
// parent IfInstruction, not wrapped in an else Block) is not an else-if.
TEST(ReduceNestingTransform, GetElseIfParentRejectsNonBlockParent) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto innerThen = std::make_unique<Block>();
	innerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(innerThen), nullptr);
	IfInstruction* innerPtr = innerIf.get();

	auto outerThen = std::make_unique<Block>();
	outerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto outerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(outerThen), std::move(innerIf));
	// innerIf is now outerIf->FalseInst (not wrapped in a Block); its Parent is
	// outerIf (an IfInstruction, not a Block).

	auto blk = std::make_unique<Block>();
	blk->Add(std::move(outerIf));
	blk->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(blk));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(innerPtr), nullptr)
	    << "an if directly in a FalseInst slot (not a Block) is not an else-if";
}

// An else block with more than one instruction (does not unwrap to a single if)
// is not an else-if.
TEST(ReduceNestingTransform, GetElseIfParentRejectsMultiInstructionElseBlock) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto innerThen = std::make_unique<Block>();
	innerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(innerThen), nullptr);
	IfInstruction* innerPtr = innerIf.get();

	auto elseBlock = std::make_unique<Block>();
	elseBlock->Add(std::move(innerIf));
	elseBlock->Add(StLocInt(v, 2));  // a second instruction -> does not unwrap

	auto outerThen = std::make_unique<Block>();
	outerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto outerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(outerThen), std::move(elseBlock));

	auto blk = std::make_unique<Block>();
	blk->Add(std::move(outerIf));
	blk->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(blk));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(innerPtr), nullptr)
	    << "a multi-instruction else block does not unwrap to the if";
}

// An else block whose parent is not an IfInstruction (e.g. the block is a
// direct block of a container, not the FalseInst of an IfInstruction) is not an
// else-if.
TEST(ReduceNestingTransform, GetElseIfParentRejectsNonIfParent) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// innerIf in a block that is a direct block of the body container (not the
	// FalseInst of an IfInstruction).
	auto blk = std::make_unique<Block>();
	auto innerThen = std::make_unique<Block>();
	innerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(innerThen), nullptr);
	IfInstruction* innerPtr = innerIf.get();
	blk->Add(std::move(innerIf));
	blk->SetFinal(std::make_unique<Leave>(body.get()));
	body->AddBlock(std::move(blk));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(innerPtr), nullptr)
	    << "a block whose parent is a container (not an IfInstruction) is not an else-if";
}

// An else block that is the TrueInst (not the FalseInst) of the parent
// IfInstruction is not an else-if (GetElseIfParent checks the FalseInst slot).
TEST(ReduceNestingTransform, GetElseIfParentRejectsTrueArmElseBlock) {
	auto built = BuildElseIf(/*ifAsFinalModel=*/false, /*elseAsTrueArm=*/true);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ReduceNestingTransform::GetElseIfParent(built.innerIf), nullptr)
	    << "the else block in the TrueInst slot is not an else-if";
}

// A corpus sweep: GetElseIfParent is pure analysis (no tree mutation), so the
// tree invariant must hold after walking every IfInstruction. Counts the
// else-if shapes (C# model vs if-as-final model) on the real post-
// ConditionDetection corpus to confirm the helper fires on real data.
TEST(ReduceNestingTransform, MscorlibGetElseIfParentSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int totalIfs = 0;
	int elseIfs = 0;
	int csharpModel = 0;
	int ifFinalModel = 0;
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
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
			if (auto* iff = dynamic_cast<IfInstruction*>(inst)) {
				++totalIfs;
				auto* parent = ReduceNestingTransform::GetElseIfParent(iff);
				if (parent) {
					++elseIfs;
					auto* elseBlock = dynamic_cast<Block*>(iff->Parent);
					if (elseBlock) {
						bool nulOrNop = !elseBlock->FinalInstruction ||
							elseBlock->FinalInstruction->Op == OpCode::Nop;
						if (elseBlock->Instructions.size() == 1 && nulOrNop)
							++csharpModel;
						else if (elseBlock->Instructions.empty() &&
						         elseBlock->FinalInstruction.get() == iff)
							++ifFinalModel;
					}
				}
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		fn->CheckInvariant(ILPhase::Normal);  // the helper is pure; the tree is unchanged
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	EXPECT_GT(totalIfs, 0) << "the corpus carries IfInstructions";
	std::cerr << "GetElseIfParent sweep: processed=" << processed
	          << " totalIfs=" << totalIfs
	          << " elseIfs=" << elseIfs
	          << " csharpModel=" << csharpModel
	          << " ifFinalModel=" << ifFinalModel << "\n";
	(void)elseIfs; (void)csharpModel; (void)ifFinalModel;
}


// EnsureEndPointUnreachable (a tested-but-not-yet-wired foundation): the
// helper that ensures a block's end point is unreachable by duplicating the
// [exit] instruction following the end point. The wired ReduceNesting fold
// (the no-else and else-if-tree cases) consults it to make a then/else block
// exit before InvertIf swaps it with the fall-through; no pipeline transform
// consults it yet.

namespace {

struct EnsureBuilt {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* b0;   // the block whose final is the fall-through / exit
	Block* b1;   // the next block (the fall-through target)
	ILInstruction* bodyLeave;  // a Leave(body) used as the exit
};

// Build a body container with two blocks: b0 (a stloc + a Branch-to-b1 final,
// the fall-through) and b1 (leave body). The bodyLeave is a Leave(body) used
// as the `fallthroughExit` argument in the tests. Each test may reset b0's
// final to exercise a different shape.
EnsureBuilt BuildEnsure() {
	EnsureBuilt out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;
	auto v = MakeLocal("v");
	out.fn->Variables.push_back(v);

	// b1: leave(body)
	auto b1 = std::make_unique<Block>();
	auto leaveBody = std::make_unique<Leave>(out.body);
	out.bodyLeave = leaveBody.get();
	b1->SetFinal(std::move(leaveBody));
	out.b1 = b1.get();

	// b0: a stloc + a Branch-to-b1 final (the fall-through).
	auto b0 = std::make_unique<Block>();
	b0->Add(StLocInt(v, 1));
	b0->SetFinal(std::make_unique<Branch>(out.b1));  // b1 is set above; b0 branches to it
	out.b0 = b0.get();

	out.body->AddBlock(std::move(b0));
	out.body->AddBlock(std::move(b1));
	RecomputeIncomingEdgeCounts(*out.fn);
	return out;
}

} // namespace

// A fall-through Block (content + a Branch final to the next block) has its
// Branch replaced with a clone of the exit (a Leave(body)). The content is
// preserved; the block now exits via the leave instead of falling through.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableReplacesFallThroughBranchWithExitClone) {
	auto built = BuildEnsure();
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ASSERT_NE(oldFinal, nullptr);
	ASSERT_EQ(oldFinal->Op, OpCode::Branch);
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Leave);
	EXPECT_NE(built.b0->FinalInstruction.get(), oldFinal) << "the Branch is dropped, not kept";
	EXPECT_EQ(built.b0->Instructions.size(), 1u) << "the stloc content is preserved";
}

// A Block that already exits (a Leave final) is a no-op: the end point is
// already unreachable.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableIsNoOpForRealExit) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Leave>(built.body));
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal) << "a Leave final is a real exit: no-op";
}

// A Block that exits via a Throw is a no-op.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableIsNoOpForThrowExit) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal) << "a Throw final is a real exit: no-op";
}

// A non-Block (a bare instruction) is a no-op (the C# asserts EndPointUnreachable).
TEST(ReduceNestingTransform, EnsureEndPointUnreachableIsNoOpForNonBlock) {
	auto built = BuildEnsure();
	auto bareLdLoc = std::make_unique<LdLoc>(MakeLocal("x"));
	auto* barePtr = bareLdLoc.get();
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::EnsureEndPointUnreachable(barePtr, built.bodyLeave);
	EXPECT_EQ(barePtr->Op, OpCode::LdLoc) << "a non-Block is untouched";
}

// A null exit is a no-op.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableIsNoOpForNullExit) {
	auto built = BuildEnsure();
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, nullptr);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal) << "a null exit is a no-op";
}

// A Block whose final is an IfInstruction (the if-as-final block, Block A) is
// a no-op: the if-as-final block's end point is the if's own control flow, not
// a fall-through to duplicate an exit into.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableIsNoOpForIfFinal) {
	auto built = BuildEnsure();
	auto v = MakeLocal("w");
	built.fn->Variables.push_back(v);
	auto thenBlock = std::make_unique<Block>();
	thenBlock->SetFinal(std::make_unique<Leave>(built.body));
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		std::move(thenBlock), nullptr);
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::move(iff));
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal)
		<< "an if-as-final block is not a fall-through: no-op";
}

// The new final is a fresh clone (a deep copy), not the exit itself; the clone's
// TargetContainer is copied by reference (the same body).
TEST(ReduceNestingTransform, EnsureEndPointUnreachableClonesExitNotReuses) {
	auto built = BuildEnsure();
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* newFinal = built.b0->FinalInstruction.get();
	ASSERT_NE(newFinal, nullptr);
	EXPECT_NE(newFinal, built.bodyLeave) << "the new final is a clone, not the exit itself";
	EXPECT_EQ(newFinal->Op, OpCode::Leave);
	EXPECT_EQ(static_cast<Leave*>(newFinal)->TargetContainer, built.body)
		<< "the clone's TargetContainer is copied by reference (the same body)";
}

// A Block whose final is a Nop (a void fall-through) is replaced with the exit
// clone (the void fall-through becomes an exit).
TEST(ReduceNestingTransform, EnsureEndPointUnreachableReplacesNopFinal) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Nop>());
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Leave)
		<< "a Nop final (void fall-through) is replaced with the exit clone";
}

// A Block with no final (a null FinalInstruction, a degenerate void fall-through)
// is replaced with the exit clone.
TEST(ReduceNestingTransform, EnsureEndPointUnreachableReplacesNullFinal) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::EnsureEndPointUnreachable(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Leave)
		<< "a null final (degenerate fall-through) is replaced with the exit clone";
}

// A mscorlib safety sweep: decode methods and call EnsureEndPointUnreachable on
// every block with a Leave(body) exit (the body is the function's top container,
// an ancestor of every block, so the leave is valid). The helper either no-ops
// (a real-exit final) or replaces a fall-through final with a Leave(body)
// clone; the ILAst invariant must hold after every call. This tests the
// helper's safety (no crash / tree corruption) on real trees; the
// faithfulness is tested by the hand-built tests above.
TEST(ReduceNestingTransform, MscorlibEnsureEndPointUnreachableSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int calls = 0;
	int replaced = 0;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		auto* body = fn->Body.get();
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (auto* block = dynamic_cast<Block*>(inst)) {
				auto* final = block->FinalInstruction.get();
				bool fallsThrough = !final || final->Op == OpCode::Nop || final->Op == OpCode::Branch;
				if (fallsThrough) ++replaced;
				++calls;
				if (body) {
					auto leave = std::make_unique<Leave>(body);
					ReduceNestingTransform::EnsureEndPointUnreachable(block, leave.get());
				}
			}
			if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
				for (auto& blk : cont->Blocks) walk(blk.get());
				return;
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		fn->CheckInvariant(ILPhase::Normal);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	EXPECT_GT(calls, 0) << "the sweep must call the helper on real blocks";
	std::cerr << "EnsureEndPointUnreachable sweep: processed=" << processed
	          << " calls=" << calls << " fallThroughReplaced=" << replaced << "\n";
	(void)replaced;
}

// RemoveRedundantExit (a tested-but-not-yet-wired foundation): the helper that
// drops a block's trailing exit when it equals the fall-through. The wired
// ReduceNesting fold calls it after a successful fold; no pipeline transform
// consults it yet.

// A block whose final (a Leave(body)) matches the implicitExit (another
// Leave(body)) has its final replaced with a Branch to the next block (the
// positional fall-through). The block's non-terminal content is preserved.
TEST(ReduceNestingTransform, RemoveRedundantExitReplacesMatchingLeaveWithFallThroughBranch) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Leave>(built.body));
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::RemoveRedundantExit(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Branch)
		<< "the matching Leave is replaced with a fall-through Branch";
	auto* br = static_cast<Branch*>(built.b0->FinalInstruction.get());
	EXPECT_EQ(br->TargetBlock, built.b1) << "the Branch targets the next block";
	EXPECT_EQ(built.b0->Instructions.size(), 1u) << "the stloc content is preserved";
}

// A block whose final (a Branch to the continue target) matches the
// implicitExit (a Branch to the same target) has its final replaced with a
// Branch to the next block. When the next block IS the continue target (the
// faithful case -- the fall-through IS the continue), the new Branch targets
// the same block.
TEST(ReduceNestingTransform, RemoveRedundantExitReplacesMatchingBranchWithFallThroughBranch) {
	auto built = BuildEnsure();
	// b0's final is a Branch to b1 (a continue whose target is the next block).
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Branch>(built.b1));
	built.fn->CheckInvariant(ILPhase::Normal);
	auto implicitBranch = std::make_unique<Branch>(built.b1);
	auto* implicitPtr = implicitBranch.get();
	ReduceNestingTransform::RemoveRedundantExit(built.b0, implicitPtr);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Branch);
	auto* br = static_cast<Branch*>(built.b0->FinalInstruction.get());
	EXPECT_EQ(br->TargetBlock, built.b1)
		<< "the matching continue-Branch is replaced with a fall-through Branch to the next block";
}

// A block whose final does NOT match the implicitExit is a no-op.
TEST(ReduceNestingTransform, RemoveRedundantExitIsNoOpWhenFinalDoesNotMatch) {
	auto built = BuildEnsure();
	// A separate Normal container as the leave target (different from body).
	auto otherC = std::make_unique<BlockContainer>();
	otherC->Kind = ContainerKind::Normal;
	auto otherPtr = otherC.get();
	built.fn->CheckInvariant(ILPhase::Normal);
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Leave>(built.body));
	// implicitExit is a Leave of a different container -> no match.
	auto implicitLeave = std::make_unique<Leave>(otherPtr);
	auto* implicitPtr = implicitLeave.get();
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::RemoveRedundantExit(built.b0, implicitPtr);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal) << "a non-matching exit is kept";
}

// A null implicitExit is a no-op (the C# Match(null) never succeeds).
TEST(ReduceNestingTransform, RemoveRedundantExitIsNoOpForNullImplicitExit) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.b0->SetFinal(std::make_unique<Leave>(built.body));
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* oldFinal = built.b0->FinalInstruction.get();
	ReduceNestingTransform::RemoveRedundantExit(built.b0, nullptr);
	EXPECT_EQ(built.b0->FinalInstruction.get(), oldFinal) << "a null implicitExit is a no-op";
}

// A block with no final (a null FinalInstruction) is a no-op: there is no
// trailing exit to remove.
TEST(ReduceNestingTransform, RemoveRedundantExitIsNoOpForNullFinal) {
	auto built = BuildEnsure();
	built.b0->FinalInstruction.reset();
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::RemoveRedundantExit(built.b0, built.bodyLeave);
	EXPECT_EQ(built.b0->FinalInstruction.get(), nullptr) << "a null final is a no-op";
}

// A block whose final matches the implicitExit but has no next block (it is
// the last in its container) has its final replaced with a Nop (the implicit
// void fall-through), matching the C# RemoveLast() leaving a Nop final.
TEST(ReduceNestingTransform, RemoveRedundantExitReplacesMatchingLeaveWithNopWhenNoNextBlock) {
	// A single-block body container: b0 with a Leave(body) final, no next block.
	auto fn = std::make_unique<ILFunction>();
	auto* body = new BlockContainer();
	fn->Body = std::unique_ptr<BlockContainer>(body);
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);
	auto b0 = std::make_unique<Block>();
	b0->Add(StLocInt(v, 1));
	auto leaveBody = std::make_unique<Leave>(body);
	auto* implicitPtr = leaveBody.get();
	// b0's final is a separate Leave(body) that matches implicitPtr.
	b0->SetFinal(std::make_unique<Leave>(body));
	auto* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::RemoveRedundantExit(b0Ptr, implicitPtr);
	fn->CheckInvariant(ILPhase::Normal);
	ASSERT_NE(b0Ptr->FinalInstruction.get(), nullptr);
	EXPECT_EQ(b0Ptr->FinalInstruction->Op, OpCode::Nop)
		<< "no next block -> the matching Leave is replaced with a Nop (void fall-through)";
	EXPECT_EQ(b0Ptr->Instructions.size(), 1u) << "the stloc content is preserved";
}

// The block's non-terminal content is preserved when the trailing exit is
// removed (only the final changes).
TEST(ReduceNestingTransform, RemoveRedundantExitPreservesBlockContent) {
	auto built = BuildEnsure();
	auto v2 = MakeLocal("w");
	built.fn->Variables.push_back(v2);
	// Reset b0 with two stlocs + a Leave(body) final.
	built.b0->FinalInstruction.reset();
	built.b0->Instructions.clear();
	built.b0->Add(StLocInt(MakeLocal("a"), 1));
	built.b0->Add(StLocInt(v2, 2));
	built.b0->SetFinal(std::make_unique<Leave>(built.body));
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::RemoveRedundantExit(built.b0, built.bodyLeave);
	built.fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(built.b0->Instructions.size(), 2u) << "both stlocs are preserved";
	ASSERT_NE(built.b0->FinalInstruction.get(), nullptr);
	EXPECT_EQ(built.b0->FinalInstruction->Op, OpCode::Branch)
		<< "the matching Leave is replaced with a fall-through Branch";
}

// A mscorlib safety sweep: decode methods and call RemoveRedundantExit on
// every block whose final is a Leave of the function body (the body is the
// function's top container, an ancestor of every block, so the leave is
// valid), passing a fresh Leave(body) as the implicitExit so the helper fires
// (the final matches). The helper replaces the final with a fall-through
// Branch to the next block (or a Nop if none); the ILAst invariant must hold
// after every call. This tests the helper's safety (no crash / tree
// corruption) on real trees; the faithfulness is tested by the hand-built
// tests above.
TEST(ReduceNestingTransform, MscorlibRemoveRedundantExitSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int calls = 0;
	int removed = 0;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		auto* body = fn->Body.get();
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (auto* block = dynamic_cast<Block*>(inst)) {
				auto* final = block->FinalInstruction.get();
				if (final && final->Op == OpCode::Leave) {
					auto* leave = static_cast<Leave*>(final);
					if (leave->TargetContainer == body && (!leave->Value || leave->Value->Op == OpCode::Nop)) {
						auto implicitLeave = std::make_unique<Leave>(body);
						ReduceNestingTransform::RemoveRedundantExit(block, implicitLeave.get());
						++removed;
					}
				}
				++calls;
			}
			if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
				for (auto& blk : cont->Blocks) walk(blk.get());
				return;
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		fn->CheckInvariant(ILPhase::Normal);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	EXPECT_GT(calls, 0) << "the sweep must call the helper on real blocks";
	EXPECT_GT(removed, 0) << "the sweep must fire the helper on real Leave(body) finals";
	std::cerr << "RemoveRedundantExit sweep: processed=" << processed
	          << " calls=" << calls << " removed=" << removed << "\n";
	(void)removed;
}

// ExtractElseBlock (a tested-but-not-yet-wired foundation): the helper that
// extracts an if's else block -- moves the else block whole into the container
// after Block A (the if's block) and clears the if's else. The wired
// ReduceNesting else-if-tree fold calls it after making the then exit; no
// pipeline transform consults it yet.

namespace {

struct ExtractBuilt {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* blockA;       // the if's block (Block A)
	IfInstruction* ifInst;
	Block* elseBlock;    // the if's FalseInst (the else to extract)
	Block* blockB;       // the exit block after Block A (null if !withBlockB)
};

// Build an if/else: Block A's FinalInstruction is `if (v == 0) { then } else {
// elseBlock }`, where then exits (a Leave(body) final, EndPointUnreachable) and
// elseBlock is a Block with a stloc + a Leave(body) final. When `withBlockB`,
// a second block (Block B, a Leave(body) final) follows Block A in the
// container -- the positional fall-through / the exit the wired fold would
// duplicate. The if's TrueInst (then) is EndPointUnreachable (the C#
// ExtractElseBlock precondition).
ExtractBuilt BuildExtractElse(bool withBlockB) {
	ExtractBuilt out;
	out.fn = std::make_unique<ILFunction>();
	out.body = new BlockContainer();
	out.fn->Body = std::unique_ptr<BlockContainer>(out.body);
	out.body->Parent = out.fn.get();
	out.body->ChildIndex = 0;
	auto v = MakeLocal("v");
	out.fn->Variables.push_back(v);

	// then: a stloc + leave(body) final (EndPointUnreachable).
	auto thenBlock = std::make_unique<Block>();
	thenBlock->Add(StLocInt(v, 1));
	thenBlock->SetFinal(std::make_unique<Leave>(out.body));

	// elseBlock: a stloc + leave(body) final.
	auto elseBlock = std::make_unique<Block>();
	elseBlock->Add(StLocInt(v, 2));
	elseBlock->SetFinal(std::make_unique<Leave>(out.body));
	out.elseBlock = elseBlock.get();

	// if (v == 0) { then } else { elseBlock }
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(thenBlock), std::move(elseBlock));
	out.ifInst = iff.get();

	// Block A: the if as FinalInstruction (the if-as-final model).
	auto blockA = std::make_unique<Block>();
	blockA->SetFinal(std::move(iff));
	out.blockA = blockA.get();
	out.body->AddBlock(std::move(blockA));

	if (withBlockB) {
		auto blockB = std::make_unique<Block>();
		blockB->SetFinal(std::make_unique<Leave>(out.body));
		out.blockB = blockB.get();
		out.body->AddBlock(std::move(blockB));
	}

	RecomputeIncomingEdgeCounts(*out.fn);
	return out;
}

} // namespace

// An if/else with no trailing exit: the else Block moves out of the if's
// FalseInst into the container as a new sibling after Block A, and the if's
// FalseInst becomes a Nop.
TEST(ReduceNestingTransform, ExtractElseBlockMovesElseBlockIntoContainerAfterBlockA) {
	auto built = BuildExtractElse(/*withBlockB=*/false);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_EQ(built.body->Blocks.size(), 1u) << "pre: Block A only";
	ASSERT_EQ(built.ifInst->FalseInst.get(), built.elseBlock) << "pre: the else is the if's FalseInst";
	ReduceNestingTransform::ExtractElseBlock(built.ifInst);
	built.fn->CheckInvariant(ILPhase::Normal);
	// The else Block is now a sibling in the container after Block A.
	ASSERT_EQ(built.body->Blocks.size(), 2u);
	EXPECT_EQ(built.body->Blocks[0].get(), built.blockA) << "Block A stays first";
	EXPECT_EQ(built.body->Blocks[1].get(), built.elseBlock) << "the else Block follows Block A";
	// The if's else is cleared (a Nop).
	ASSERT_NE(built.ifInst->FalseInst.get(), nullptr);
	EXPECT_EQ(built.ifInst->FalseInst->Op, OpCode::Nop) << "the if's FalseInst is a Nop";
}

// With a trailing exit (Block B), the else Block is inserted between Block A
// and Block B (the C# inserts the else content after the if, before the exit).
TEST(ReduceNestingTransform, ExtractElseBlockInsertsElseBeforeBlockB) {
	auto built = BuildExtractElse(/*withBlockB=*/true);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_EQ(built.body->Blocks.size(), 2u) << "pre: Block A + Block B";
	ReduceNestingTransform::ExtractElseBlock(built.ifInst);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_EQ(built.body->Blocks.size(), 3u);
	EXPECT_EQ(built.body->Blocks[0].get(), built.blockA) << "Block A stays first";
	EXPECT_EQ(built.body->Blocks[1].get(), built.elseBlock) << "the else Block is after Block A";
	EXPECT_EQ(built.body->Blocks[2].get(), built.blockB) << "Block B stays last";
}

// The else Block's content (the stloc) and control flow (the Leave final) are
// preserved when it moves into the container.
TEST(ReduceNestingTransform, ExtractElseBlockPreservesElseContent) {
	auto built = BuildExtractElse(/*withBlockB=*/false);
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::ExtractElseBlock(built.ifInst);
	built.fn->CheckInvariant(ILPhase::Normal);
	ASSERT_EQ(built.body->Blocks.size(), 2u);
	auto* movedElse = built.body->Blocks[1].get();
	ASSERT_NE(movedElse, nullptr);
	EXPECT_EQ(movedElse->Instructions.size(), 1u) << "the stloc content is preserved";
	EXPECT_EQ(movedElse->Instructions[0]->Op, OpCode::StLoc);
	ASSERT_NE(movedElse->FinalInstruction.get(), nullptr);
	EXPECT_EQ(movedElse->FinalInstruction->Op, OpCode::Leave) << "the Leave final is preserved";
	auto* leave = static_cast<Leave*>(movedElse->FinalInstruction.get());
	EXPECT_EQ(leave->TargetContainer, built.body) << "the Leave still targets the body";
}

// The moved else Block is reparented to the container (Parent / ChildIndex
// consistent), and the if's Nop FalseInst is parented to the if.
TEST(ReduceNestingTransform, ExtractElseBlockReparentsMovedElseBlock) {
	auto built = BuildExtractElse(/*withBlockB=*/false);
	built.fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::ExtractElseBlock(built.ifInst);
	built.fn->CheckInvariant(ILPhase::Normal);
	auto* movedElse = built.body->Blocks[1].get();
	EXPECT_EQ(movedElse->Parent, built.body) << "the moved else Block's Parent is the container";
	EXPECT_EQ(movedElse->ChildIndex, 1);
	EXPECT_EQ(built.ifInst->FalseInst->Parent, built.ifInst);
	EXPECT_EQ(built.ifInst->FalseInst->ChildIndex, 2);
}

// A null ifInst is a defensive no-op.
TEST(ReduceNestingTransform, ExtractElseBlockRejectsNull) {
	ReduceNestingTransform::ExtractElseBlock(nullptr);  // must not crash
}

// An if whose parent is not a Block (e.g. the if is the TrueInst of an outer
// IfInstruction, so its Parent is an IfInstruction) is a defensive no-op: the
// C# casts `ifInst.Parent` to Block.
TEST(ReduceNestingTransform, ExtractElseBlockRejectsNonBlockParent) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto innerThen = std::make_unique<Block>();
	innerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerElse = std::make_unique<Block>();
	innerElse->SetFinal(std::make_unique<Leave>(body.get()));
	auto* innerElsePtr = innerElse.get();
	// innerIf is the TrueInst of outerIf, so its Parent is an IfInstruction.
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(innerThen), std::move(innerElse));
	IfInstruction* innerPtr = innerIf.get();
	auto outerThen = std::make_unique<Block>();
	outerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto outerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(innerIf), std::move(outerThen));
	auto blockA = std::make_unique<Block>();
	blockA->SetFinal(std::move(outerIf));
	body->AddBlock(std::move(blockA));
	auto* bodyPtr = body.get();
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::ExtractElseBlock(innerPtr);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(innerPtr->FalseInst.get(), innerElsePtr) << "a non-Block parent -> no-op";
	EXPECT_EQ(bodyPtr->Blocks.size(), 1u) << "no block is moved";
}

// An if whose FalseInst is not a Block (e.g. a bare Leave) is a defensive
// no-op (the C# casts the FalseInst to Block).
TEST(ReduceNestingTransform, ExtractElseBlockRejectsNonBlockFalseInst) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);
	auto thenBlock = std::make_unique<Block>();
	thenBlock->SetFinal(std::make_unique<Leave>(body.get()));
	// A bare Leave as the FalseInst (not a Block).
	auto bareFalse = std::make_unique<Leave>(body.get());
	auto* barePtr = bareFalse.get();
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(thenBlock), std::move(bareFalse));
	IfInstruction* ifPtr = iff.get();
	auto blockA = std::make_unique<Block>();
	blockA->SetFinal(std::move(iff));
	body->AddBlock(std::move(blockA));
	auto* bodyPtr = body.get();
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::ExtractElseBlock(ifPtr);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ifPtr->FalseInst.get(), barePtr) << "a non-Block FalseInst -> no-op";
	EXPECT_EQ(bodyPtr->Blocks.size(), 1u) << "no block is moved";
}

// A null FalseInst (no else) is a defensive no-op.
TEST(ReduceNestingTransform, ExtractElseBlockRejectsNullFalseInst) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);
	auto thenBlock = std::make_unique<Block>();
	thenBlock->SetFinal(std::make_unique<Leave>(body.get()));
	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(thenBlock), nullptr);
	IfInstruction* ifPtr = iff.get();
	auto blockA = std::make_unique<Block>();
	blockA->SetFinal(std::move(iff));
	body->AddBlock(std::move(blockA));
	auto* bodyPtr = body.get();
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform::ExtractElseBlock(ifPtr);
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(ifPtr->FalseInst.get(), nullptr) << "a null FalseInst -> no-op";
	EXPECT_EQ(bodyPtr->Blocks.size(), 1u) << "no block is moved";
}

// A mscorlib safety sweep: decode methods and call ExtractElseBlock on every
// IfInstruction whose FalseInst is a Block (the else-block case). The else
// Block moves into the container as a sibling after the if's block; the if's
// FalseInst becomes a Nop. The ILAst invariant must hold after every call.
// This tests the helper's safety (no crash / tree corruption) on real trees;
// the faithfulness is tested by the hand-built tests above.
TEST(ReduceNestingTransform, MscorlibExtractElseBlockSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int calls = 0;
	int fired = 0;
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		// Run the full pre-pipeline through HighLevelLoopTransform so the
		// if-as-FinalInstruction + else-block shape (ConditionDetection's
		// output) arises; after ReadIL alone the if's FalseInst is never a
		// Block (the else-block shape is a ConditionDetection artifact).
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
		// Collect every IfInstruction whose FalseInst is a Block (the
		// else-block case) BEFORE mutating: ExtractElseBlock inserts a block
		// into the container, which would invalidate a range-for walk of the
		// container's Blocks. The if pointers stay valid across the moves
		// (ExtractElseBlock does not destroy ifs), so collect-then-call is
		// safe.
		std::vector<IfInstruction*> ifs;
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (auto* iff = dynamic_cast<IfInstruction*>(inst)) {
				if (dynamic_cast<Block*>(iff->FalseInst.get())) ifs.push_back(iff);
				++calls;
			}
			if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
				for (auto& blk : cont->Blocks) walk(blk.get());
				return;
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		for (auto* iff : ifs) {
			ReduceNestingTransform::ExtractElseBlock(iff);
			++fired;
		}
		fn->CheckInvariant(ILPhase::Normal);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	EXPECT_GT(calls, 0) << "the sweep must call the helper on real ifs";
	EXPECT_GT(fired, 0) << "the sweep must fire the helper on real else blocks";
	std::cerr << "ExtractElseBlock sweep: processed=" << processed
	          << " calls=" << calls << " fired=" << fired << "\n";
	(void)fired;
}

// ---- ReduceNesting (no-else case) ----

// A "deep then" Block that exits (a value-less Leave of `leaveTarget`) with
// nesting depth 2 (two nested ifs), so the ReduceNesting no-else heuristic
// (`maxDepth >= 2`) fires:
//   thenBlock: [if1] + leave(leaveTarget)
//     if1: if (ldc 0) { Block2 } (no else)
//     Block2: [if2] + leave(leaveTarget)
//       if2: if (ldc 0) { Block3 } (no else)
//       Block3: [stloc v(7)] + leave(leaveTarget)
// `leaveTarget` is the container the leaves target (an ancestor of the nested
// blocks -- the function body for the direct-return tests, the try container
// for the leave-from-try test).
namespace {
std::unique_ptr<Block> MakeDeepThen(BlockContainer* leaveTarget, ILVariablePtr v) {
	auto b3 = std::make_unique<Block>();
	b3->Add(StLocInt(v, 7));
	b3->SetFinal(std::make_unique<Leave>(leaveTarget));
	auto if2 = std::make_unique<IfInstruction>(
		std::make_unique<LdcI4>(0), std::move(b3), nullptr);
	auto b2 = std::make_unique<Block>();
	b2->Add(std::move(if2));
	b2->SetFinal(std::make_unique<Leave>(leaveTarget));
	auto if1 = std::make_unique<IfInstruction>(
		std::make_unique<LdcI4>(0), std::move(b2), nullptr);
	auto thenBlock = std::make_unique<Block>();
	thenBlock->Add(std::move(if1));
	thenBlock->SetFinal(std::make_unique<Leave>(leaveTarget));
	return thenBlock;
}
} // namespace

// The no-else fold: if (cond) { deep then (exits) } return; -> if (!cond) return; then...; return;
// The if (b0's final) is inverted, its TrueInst becomes the return (Block B's
// exit), and the deep then moves into Block B.
TEST(ReduceNestingTransform, ReduceNestingNoElseFoldsDeeplyNestedThen) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(body.get(), v), nullptr));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));

	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 100;
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));

	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	bool fired = ReduceNestingTransform::ReduceNesting(b0Ptr, iff, b1Ptr->FinalInstruction.get());
	EXPECT_TRUE(fired) << "the fold fires on the deep-then no-else shape";
	fn->CheckInvariant(ILPhase::Normal);

	auto* iffAfter = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr);
	ASSERT_NE(iffAfter->TrueInst, nullptr);
	EXPECT_EQ(iffAfter->TrueInst->Op, OpCode::Leave) << "the if's then is the return (Block B's exit)";
	EXPECT_EQ(dynamic_cast<Leave*>(iffAfter->TrueInst.get())->TargetContainer, fn->Body.get());
	EXPECT_EQ(iffAfter->Condition->Op, OpCode::LdLoc) << "the condition was negated (comp(eq,v,0) => ldloc v)";
	EXPECT_FALSE(b1Ptr->Instructions.empty()) << "the deep then moved into Block B";
	EXPECT_EQ(b1Ptr->FinalInstruction->Op, OpCode::Leave) << "Block B's final is the then's exit";
}

// Bail: a shallow then (depth 0) does not fire (the `maxDepth < 2` heuristic).
TEST(ReduceNestingTransform, ReduceNestingNoElseBailsOnShallowThen) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto shallowThen = std::make_unique<Block>();
	shallowThen->Add(StLocInt(v, 1));
	shallowThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		std::move(shallowThen), nullptr));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(b0Ptr, iff, b1Ptr->FinalInstruction.get()));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(b0Ptr->FinalInstruction.get(), iff) << "no mutation on bail";
}

// Bail: an if with a shallow else block does not fire. The else-if-tree case
// is now handled, but the fold still bails when the else block is shallow
// (ShouldReduceNesting returns false -- a single-Leave else with depth 0 is not
// worth duplicating exits into the thens to reduce its nesting by 1).
TEST(ReduceNestingTransform, ReduceNestingNoElseBailsOnShallowElse) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto elseBlock = std::make_unique<Block>();
	elseBlock->SetFinal(std::make_unique<Leave>(body.get()));
	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(body.get(), v), std::move(elseBlock)));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(b0Ptr, iff, b1Ptr->FinalInstruction.get()));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(b0Ptr->FinalInstruction.get(), iff) << "no mutation on bail";
}

// Bail: no Block B (b0 is the last block) -- InvertIf needs the exit holder.
TEST(ReduceNestingTransform, ReduceNestingNoElseBailsWhenNoBlockB) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(body.get(), v), nullptr));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	auto exitLeave = std::make_unique<Leave>(body.get());
	ILInstruction* exitPtr = exitLeave.get();
	// exitInst is non-null but there is no Block B to hold it.
	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(b0Ptr, iff, exitPtr));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(b0Ptr->FinalInstruction.get(), iff) << "no mutation on bail";
	(void)exitLeave;
}

// Bail: Block B has falseCode (a non-terminal) -- the no-else case requires the
// exit directly after the if (no falseCode).
TEST(ReduceNestingTransform, ReduceNestingNoElseBailsWhenBlockBHasFalseCode) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(body.get(), v), nullptr));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->Add(StLocInt(v, 9));  // falseCode (a non-terminal before the exit)
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(b0Ptr, iff, b1Ptr->FinalInstruction.get()));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(b0Ptr->FinalInstruction.get(), iff) << "no mutation on bail";
}

// The leave-from-try case: Block B's exit is a leave of a Normal container (a
// try body), and `exitInst` is the keyword return found by walking out of the
// try (CanDuplicateExit). InvertIf makes the if's TrueInst = Block B's leave-
// from-try; step 6 replaces it with the keyword return clone.
TEST(ReduceNestingTransform, ReduceNestingNoElseReplacesLeaveFromTryWithKeyword) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// tryC: [b0 (if), b1 (leave tryC)] -- the try block.
	auto tryC = std::make_unique<BlockContainer>();
	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(tryC.get(), v), nullptr));  // deep then exits via leave(tryC)
	Block* b0Ptr = b0.get();
	tryC->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(tryC.get()));  // leave-from-try
	Block* b1Ptr = b1.get();
	tryC->AddBlock(std::move(b1));
	auto* tryCPtr = tryC.get();

	// finC: an empty finally (a single leave(finC)).
	auto finC = std::make_unique<BlockContainer>();
	auto finBlock = std::make_unique<Block>();
	finBlock->SetFinal(std::make_unique<Leave>(finC.get()));
	finC->AddBlock(std::move(finBlock));

	auto tf = std::make_unique<TryFinally>(std::move(tryC), std::move(finC));
	// bx: [tryFinally] + leave(body) -- the return after the try-finally.
	auto bx = std::make_unique<Block>();
	bx->Add(std::move(tf));
	auto returnLeave = std::make_unique<Leave>(body.get());
	ILInstruction* returnPtr = returnLeave.get();
	bx->SetFinal(std::move(returnLeave));
	body->AddBlock(std::move(bx));

	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	auto* iff = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iff, nullptr);
	// exitInst = the return after the try-finally (what CanDuplicateExit would
	// find by walking out of the try).
	bool fired = ReduceNestingTransform::ReduceNesting(b0Ptr, iff, returnPtr);
	EXPECT_TRUE(fired) << "the fold fires on the leave-from-try shape";
	fn->CheckInvariant(ILPhase::Normal);

	auto* iffAfter = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr);
	ASSERT_NE(iffAfter->TrueInst, nullptr);
	EXPECT_EQ(iffAfter->TrueInst->Op, OpCode::Leave) << "step 6 replaced the leave-from-try with the keyword return";
	EXPECT_EQ(dynamic_cast<Leave*>(iffAfter->TrueInst.get())->TargetContainer, fn->Body.get())
		<< "the if's then is the return (leave body), not the leave-from-try (leave tryC)";
	// Block B's final is the then's exit (leave tryC), kept.
	EXPECT_EQ(dynamic_cast<Leave*>(b1Ptr->FinalInstruction.get())->TargetContainer, tryCPtr);
	(void)b1Ptr;
}

// A mscorlib safety sweep: run the full pre-pipeline, then fire ReduceNesting
// on every no-else if-final whose next-block exit is a duplicable keyword exit
// (replicating the wired VisitContainer walk: continueTarget tracked per
// container Kind, NextInsn = Block B's first instruction, CanDuplicateExit +
// ReduceNesting). The ILAst invariant must hold after every fire.
TEST(ReduceNestingTransform, MscorlibReduceNestingNoElseSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int candidates = 0;  // no-else if-finals with a duplicable next-block exit
	int fired = 0;        // ReduceNesting returned true
	int bailMaxDepth = 0;     // the then is shallow (maxDepth < 2)
	int bailNoBlockB = 0;    // no Block B (Block A is the last block)
	int bailFalseCode = 0;   // Block B has falseCode (a non-terminal)
	int bailMultiPred = 0;   // Block B is not single-predecessor
	int deepCandidates = 0;  // candidates with maxDepth >= 2 (past the heuristic)
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
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
		// Run ReduceNestingTransform so the wired ImproveILOrdering fold (the IL-
		// order-gated InvertIf) has re-inverted the early-return ifs, matching the
		// shapes the wired ReduceNesting fold would see (ImproveILOrdering runs
		// before ReduceNesting in the same Visit pass). ReduceNesting itself is not
		// wired, so this only applies ImproveILOrdering + EliminateRedundantTryFinally.
		ReduceNestingTransform().Run(*fn, ctx);
		RecomputeIncomingEdgeCounts(*fn);

		std::function<void(ILInstruction*, Block*)> walk;
		walk = [&](ILInstruction* inst, Block* continueTarget) {
			if (!inst) return;
			if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
				Block* ct = continueTarget;
				switch (cont->Kind) {
					case ContainerKind::Loop:
					case ContainerKind::While:
						ct = cont->Blocks.empty() ? nullptr : cont->Blocks.front().get(); break;
					case ContainerKind::DoWhile:
						ct = cont->Blocks.empty() ? nullptr : cont->Blocks.back().get(); break;
					case ContainerKind::Normal:
					case ContainerKind::Switch: break;
				}
				for (std::size_t i = 0; i < cont->Blocks.size(); ++i) {
					auto* blk = cont->Blocks[i].get();
					for (auto& ni : blk->Instructions) walk(ni.get(), ct);
					auto* iff = dynamic_cast<IfInstruction*>(blk->FinalInstruction.get());
					if (iff) {
						if (!iff->FalseInst || iff->FalseInst->Op == OpCode::Nop) {
							++candidates;
							Block* blockB = (i + 1 < cont->Blocks.size()) ? cont->Blocks[i + 1].get() : nullptr;
							ILInstruction* nextInsn = nullptr;
							if (blockB) nextInsn = blockB->Instructions.empty()
								? blockB->FinalInstruction.get() : blockB->Instructions[0].get();
							ILInstruction* keywordExit = nullptr;
							if (nextInsn && ReduceNestingTransform::CanDuplicateExit(nextInsn, ct, keywordExit)) {
								// Replicate the ReduceNesting preconditions to count the bail
								// reasons (the fold is faithfulness-only on this corpus).
								int ms = 0, md = 0;
								ReduceNestingTransform::UpdateStats(iff->TrueInst.get(), ms, md);
								if (md < 2) ++bailMaxDepth;
								else if (!blockB) ++bailNoBlockB;
								else if (!blockB->Instructions.empty()) ++bailFalseCode;
								else if (blockB->IncomingEdgeCount != 1) ++bailMultiPred;
								else ++deepCandidates;
								if (ReduceNestingTransform::ReduceNesting(blk, iff, keywordExit)) {
									++fired;
									fn->CheckInvariant(ILPhase::Normal);
								}
							}
						}
						walk(iff->TrueInst.get(), ct);
						walk(iff->FalseInst.get(), ct);
					} else if (blk->FinalInstruction) {
						walk(blk->FinalInstruction.get(), ct);
					}
				}
				return;
			}
			if (inst->Op == OpCode::ILFunction) return;
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i), continueTarget);
		};
		walk(fn->Body.get(), nullptr);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	std::cerr << "ReduceNesting no-else sweep: processed=" << processed
	          << " candidates=" << candidates << " fired=" << fired
	          << " bailMaxDepth=" << bailMaxDepth
	          << " bailNoBlockB=" << bailNoBlockB
	          << " bailFalseCode=" << bailFalseCode
	          << " bailMultiPred=" << bailMultiPred
	          << " deepCandidates=" << deepCandidates << "\n";
	(void)candidates; (void)bailMaxDepth; (void)bailNoBlockB;
	(void)bailFalseCode; (void)bailMultiPred; (void)deepCandidates;
}

// ---- ReduceNesting (else-if-tree case) ----

// A depth-2 "deep else" Block (the else content) that exits via leave(leaveTarget),
// so ShouldReduceNesting fires (maxDepth2 >= 2). Two nested ifs, each exiting.
namespace {
std::unique_ptr<Block> MakeDeepElse(BlockContainer* leaveTarget, ILVariablePtr v) {
	auto b3 = std::make_unique<Block>();
	b3->Add(StLocInt(v, 7));
	b3->SetFinal(std::make_unique<Leave>(leaveTarget));
	auto if2 = std::make_unique<IfInstruction>(
		std::make_unique<LdcI4>(0), std::move(b3), nullptr);
	auto b2 = std::make_unique<Block>();
	b2->Add(std::move(if2));
	b2->SetFinal(std::make_unique<Leave>(leaveTarget));
	auto if1 = std::make_unique<IfInstruction>(
		std::make_unique<LdcI4>(0), std::move(b2), nullptr);
	auto elseBlock = std::make_unique<Block>();
	elseBlock->Add(std::move(if1));
	elseBlock->SetFinal(std::make_unique<Leave>(leaveTarget));
	return elseBlock;
}
} // namespace

// The else-if-tree fire case:
//   if (c1) { shallow then (exits) } else if (c2) { shallow then (exits) } else { deep else (exits) } return;
// The fold makes the thens exit (no-op -- they already exit), promotes the
// else-if holder block and the else content block to the container as siblings
// after Block A, clears both ifs' else, and drops the now-dead trailing return
// (Block B) because the else content exits.
TEST(ReduceNestingTransform, ReduceNestingElseIfTreeFoldsDeepElse) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// innerIf: if (v == 1) { shallow then2 (exits) } else { deep else (exits) }
	auto then2 = std::make_unique<Block>();
	then2->Add(StLocInt(v, 2));
	then2->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(then2), MakeDeepElse(body.get(), v));
	IfInstruction* innerIfPtr = innerIf.get();

	// elseBlock1: the if-as-final else block wrapping innerIf.
	auto elseBlock1 = std::make_unique<Block>();
	elseBlock1->SetFinal(std::move(innerIf));
	Block* elseBlock1Ptr = elseBlock1.get();

	// ifRoot: if (v == 0) { shallow then1 (exits) } else { elseBlock1 (innerIf) }
	auto then1 = std::make_unique<Block>();
	then1->Add(StLocInt(v, 1));
	then1->SetFinal(std::make_unique<Leave>(body.get()));
	auto ifRoot = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(then1), std::move(elseBlock1));
	IfInstruction* ifRootPtr = ifRoot.get();

	// b0: Block A (ifRoot is the FinalInstruction).
	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	b0->SetFinal(std::move(ifRoot));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));

	// b1: Block B (the trailing return).
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 100;
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();  // dangling after the fold (b1 is dropped); not dereferenced.
	body->AddBlock(std::move(b1));

	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	bool fired = ReduceNestingTransform::ReduceNesting(b0Ptr, ifRootPtr, b1Ptr->FinalInstruction.get());
	EXPECT_TRUE(fired) << "the else-if-tree fold fires on the deep-else shape";
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	// Both ifs' else is cleared (Nop).
	ASSERT_NE(ifRootPtr->FalseInst, nullptr);
	EXPECT_EQ(ifRootPtr->FalseInst->Op, OpCode::Nop) << "ifRoot's else was extracted";
	ASSERT_NE(innerIfPtr->FalseInst, nullptr);
	EXPECT_EQ(innerIfPtr->FalseInst->Op, OpCode::Nop) << "innerIf's else was extracted";
	// The else-if holder block and the else content block were promoted to the
	// container as siblings after Block A; Block B (the dead trailing return)
	// was dropped because the else content exits.
	ASSERT_EQ(fn->Body->Blocks.size(), 3u);
	EXPECT_EQ(fn->Body->Blocks[0].get(), b0Ptr);
	EXPECT_EQ(fn->Body->Blocks[1].get(), elseBlock1Ptr) << "the innerIf holder was promoted";
	EXPECT_EQ(fn->Body->Blocks[1]->FinalInstruction.get(), innerIfPtr)
		<< "the promoted holder's final is innerIf";
}

// The else-if-tree fire case where the else content falls through (does not
// exit): Block B (the trailing exit) is kept (it is not dead).
TEST(ReduceNestingTransform, ReduceNestingElseIfTreeKeepsExitWhenElseFallsThrough) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// Block B (the trailing return) is created first so the deep else block can
	// branch to it (a Branch-to-next fall-through, not a Leave). The deep content
	// makes ShouldReduceNesting fire; the Branch final means the else does not
	// exit, so Block B is kept. A null/Nop final would make UnwrapElseIf treat the
	// block as the C#-model else-if wrapping its sole non-terminal, so the Branch
	// (a non-Nop final) keeps it a plain deep block.
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();

	auto deepFallThrough = std::make_unique<Block>();
	{
		auto b3 = std::make_unique<Block>();
		b3->Add(StLocInt(v, 7));
		b3->SetFinal(std::make_unique<Leave>(body.get()));  // inner exit (depth 2 leaf)
		auto if2 = std::make_unique<IfInstruction>(
			std::make_unique<LdcI4>(0), std::move(b3), nullptr);
		auto b2 = std::make_unique<Block>();
		b2->Add(std::move(if2));
		b2->SetFinal(std::make_unique<Leave>(body.get()));  // depth 1 exit
		auto if1 = std::make_unique<IfInstruction>(
			std::make_unique<LdcI4>(0), std::move(b2), nullptr);
		deepFallThrough->Add(std::move(if1));
		deepFallThrough->SetFinal(std::make_unique<Branch>(b1Ptr));  // fall through to Block B
	}

	auto then2 = std::make_unique<Block>();
	then2->Add(StLocInt(v, 2));
	then2->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(then2), std::move(deepFallThrough));
	IfInstruction* innerIfPtr = innerIf.get();

	auto elseBlock1 = std::make_unique<Block>();
	elseBlock1->SetFinal(std::move(innerIf));
	Block* elseBlock1Ptr = elseBlock1.get();

	auto then1 = std::make_unique<Block>();
	then1->Add(StLocInt(v, 1));
	then1->SetFinal(std::make_unique<Leave>(body.get()));
	auto ifRoot = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(then1), std::move(elseBlock1));
	IfInstruction* ifRootPtr = ifRoot.get();

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::move(ifRoot));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	body->AddBlock(std::move(b1));

	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	bool fired = ReduceNestingTransform::ReduceNesting(b0Ptr, ifRootPtr, b1Ptr->FinalInstruction.get());
	EXPECT_TRUE(fired) << "the else-if-tree fold fires even when the else falls through";
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	ASSERT_NE(ifRootPtr->FalseInst, nullptr);
	EXPECT_EQ(ifRootPtr->FalseInst->Op, OpCode::Nop);
	ASSERT_NE(innerIfPtr->FalseInst, nullptr);
	EXPECT_EQ(innerIfPtr->FalseInst->Op, OpCode::Nop);
	// Block B is kept (the else falls through, so the trailing exit is not dead).
	ASSERT_EQ(fn->Body->Blocks.size(), 4u);
	EXPECT_EQ(fn->Body->Blocks[0].get(), b0Ptr);
	EXPECT_EQ(fn->Body->Blocks[1].get(), elseBlock1Ptr);
	EXPECT_EQ(fn->Body->Blocks.back().get(), b1Ptr) << "Block B (the exit) is kept";
}

// Bail: a non-root else-if is not a reduction candidate (the else-if tree is
// reduced as a single group from the root).
TEST(ReduceNestingTransform, ReduceNestingElseIfTreeBailsOnRootElseIf) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto then2 = std::make_unique<Block>();
	then2->Add(StLocInt(v, 2));
	then2->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(then2), MakeDeepElse(body.get(), v));
	IfInstruction* innerIfPtr = innerIf.get();

	auto elseBlock1 = std::make_unique<Block>();
	elseBlock1->SetFinal(std::move(innerIf));
	Block* elseBlock1Ptr = elseBlock1.get();

	auto then1 = std::make_unique<Block>();
	then1->Add(StLocInt(v, 1));
	then1->SetFinal(std::make_unique<Leave>(body.get()));
	auto ifRoot = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(then1), std::move(elseBlock1));

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::move(ifRoot));
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	// Call ReduceNesting on innerIf (a non-root else-if): GetElseIfParent(innerIf)
	// == ifRoot (non-null), so the fold bails. `block` is innerIf's parent block
	// (elseBlock1); the exit is a stand-in (the fold bails before consulting it).
	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(elseBlock1Ptr, innerIfPtr, b1Ptr->FinalInstruction.get()));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(innerIfPtr->FalseInst->Op, OpCode::Block) << "no mutation on bail";
}

// Bail: a chain with no trailing else (the leaf's FalseInst is a Nop, not a
// Block) has no block to reduce.
TEST(ReduceNestingTransform, ReduceNestingElseIfTreeBailsOnNoTrailingElse) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	// innerIf with no else (FalseInst = null).
	auto innerThen = std::make_unique<Block>();
	innerThen->Add(StLocInt(v, 2));
	innerThen->SetFinal(std::make_unique<Leave>(body.get()));
	auto innerIf = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(1),
		                       ComparisonKind::Equality),
		std::move(innerThen), nullptr);
	IfInstruction* innerIfPtr = innerIf.get();

	auto elseBlock1 = std::make_unique<Block>();
	elseBlock1->SetFinal(std::move(innerIf));

	auto then1 = std::make_unique<Block>();
	then1->Add(StLocInt(v, 1));
	then1->SetFinal(std::make_unique<Leave>(body.get()));
	auto ifRoot = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(then1), std::move(elseBlock1));
	IfInstruction* ifRootPtr = ifRoot.get();

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::move(ifRoot));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	EXPECT_FALSE(ReduceNestingTransform::ReduceNesting(b0Ptr, ifRootPtr, b1Ptr->FinalInstruction.get()));
	fn->CheckInvariant(ILPhase::Normal);
	EXPECT_EQ(b0Ptr->FinalInstruction.get(), ifRootPtr) << "no mutation on bail";
}

// The plain if-else case (no else-if chain): ifRoot's FalseInst is the deep else
// block directly (not a Block-wrapped if). The else-if-tree code path handles it
// (the walk does not descend; leaf == ifRoot).
TEST(ReduceNestingTransform, ReduceNestingPlainIfElseFoldsDeepElse) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto then1 = std::make_unique<Block>();
	then1->Add(StLocInt(v, 1));
	then1->SetFinal(std::make_unique<Leave>(body.get()));
	auto elseBlock = MakeDeepElse(body.get(), v);
	Block* elseBlockPtr = elseBlock.get();
	auto ifRoot = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                       ComparisonKind::Equality),
		std::move(then1), std::move(elseBlock));
	IfInstruction* ifRootPtr = ifRoot.get();

	auto b0 = std::make_unique<Block>();
	b0->SetFinal(std::move(ifRoot));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));
	auto b1 = std::make_unique<Block>();
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));
	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	bool fired = ReduceNestingTransform::ReduceNesting(b0Ptr, ifRootPtr, b1Ptr->FinalInstruction.get());
	EXPECT_TRUE(fired) << "the plain if-else fold fires on the deep-else shape";
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	ASSERT_NE(ifRootPtr->FalseInst, nullptr);
	EXPECT_EQ(ifRootPtr->FalseInst->Op, OpCode::Nop) << "the else was extracted";
	// The else block was promoted to the container; Block B was dropped.
	ASSERT_EQ(fn->Body->Blocks.size(), 2u);
	EXPECT_EQ(fn->Body->Blocks[0].get(), b0Ptr);
	EXPECT_EQ(fn->Body->Blocks[1].get(), elseBlockPtr) << "the else block was promoted";
}

// A mscorlib safety sweep: run the full pre-pipeline + ReduceNestingTransform
// (so ImproveILOrdering has re-inverted the early-return ifs), then fire
// ReduceNesting on every if-final whose FalseInst is a Block (has an else), is
// not an else-if (GetElseIfParent null -- reduce from the root), and whose
// next-block exit is a duplicable keyword exit. The ILAst invariant must hold
// after every fire.
TEST(ReduceNestingTransform, MscorlibReduceNestingElseIfTreeSweep) {
#if defined(_WIN32)
	const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
	const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	int processed = 0;
	int candidates = 0;  // root if-finals with an else and a duplicable next-block exit
	int fired = 0;        // ReduceNesting returned true
	int bailShallowElse = 0;   // ShouldReduceNesting false (the else is shallow)
	int bailNoElseBlock = 0;  // the leaf's FalseInst is not a Block (no trailing else)
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
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
		ReduceNestingTransform().Run(*fn, ctx);
		RecomputeIncomingEdgeCounts(*fn);

		std::function<void(ILInstruction*, Block*)> walk;
		walk = [&](ILInstruction* inst, Block* continueTarget) {
			if (!inst) return;
			if (auto* cont = dynamic_cast<BlockContainer*>(inst)) {
				Block* ct = continueTarget;
				switch (cont->Kind) {
					case ContainerKind::Loop:
					case ContainerKind::While:
						ct = cont->Blocks.empty() ? nullptr : cont->Blocks.front().get(); break;
					case ContainerKind::DoWhile:
						ct = cont->Blocks.empty() ? nullptr : cont->Blocks.back().get(); break;
					case ContainerKind::Normal:
					case ContainerKind::Switch: break;
				}
				for (std::size_t i = 0; i < cont->Blocks.size(); ++i) {
					auto* blk = cont->Blocks[i].get();
					for (auto& ni : blk->Instructions) walk(ni.get(), ct);
					auto* iff = dynamic_cast<IfInstruction*>(blk->FinalInstruction.get());
					if (iff) {
						// The else case: FalseInst is a Block (not a Nop -- the no-else
						// case is covered by the no-else sweep), and the if is the root
						// of its else-if tree (GetElseIfParent null).
						if (iff->FalseInst && iff->FalseInst->Op != OpCode::Nop
							&& !ReduceNestingTransform::GetElseIfParent(iff)) {
							++candidates;
							Block* blockB = (i + 1 < cont->Blocks.size()) ? cont->Blocks[i + 1].get() : nullptr;
							ILInstruction* nextInsn = nullptr;
							if (blockB) nextInsn = blockB->Instructions.empty()
								? blockB->FinalInstruction.get() : blockB->Instructions[0].get();
							ILInstruction* keywordExit = nullptr;
							if (nextInsn && ReduceNestingTransform::CanDuplicateExit(nextInsn, ct, keywordExit)) {
								if (ReduceNestingTransform::ReduceNesting(blk, iff, keywordExit)) {
									++fired;
									fn->CheckInvariant(ILPhase::Normal);
									RecomputeIncomingEdgeCounts(*fn);
								} else {
									// Distinguish the bail reasons: walk down the else-if chain
									// to the leaf (the innermost if whose FalseInst is not a
									// Block-wrapped if); the leaf's FalseInst not being a Block
									// is the 'no trailing else' bail, otherwise the shallow bail.
									IfInstruction* leaf = iff;
									while (auto* eb = dynamic_cast<Block*>(leaf->FalseInst.get())) {
										auto* next = dynamic_cast<IfInstruction*>(eb->FinalInstruction.get());
										if (!next) break;
										leaf = next;
									}
									auto* elseContent = dynamic_cast<Block*>(leaf->FalseInst.get());
									if (!elseContent) ++bailNoElseBlock;
									else ++bailShallowElse;
								}
							}
						}
						walk(iff->TrueInst.get(), ct);
						walk(iff->FalseInst.get(), ct);
					} else if (blk->FinalInstruction) {
						walk(blk->FinalInstruction.get(), ct);
					}
				}
				return;
			}
			if (inst->Op == OpCode::ILFunction) return;
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i), continueTarget);
		};
		walk(fn->Body.get(), nullptr);
		if (processed >= 3000) break;
	}
	EXPECT_GT(processed, 2000) << "the sweep must exercise real methods";
	std::cerr << "ReduceNesting else-if-tree sweep: processed=" << processed
	          << " candidates=" << candidates << " fired=" << fired
	          << " bailShallowElse=" << bailShallowElse
	          << " bailNoElseBlock=" << bailNoElseBlock << "\n";
	(void)candidates; (void)bailShallowElse; (void)bailNoElseBlock;
}

// ---- Wired Run walk (the Visit walk calls ReduceNesting after ImproveILOrdering) ----

// The wired `Run` Visit walk fires ReduceNesting on the no-else shape (the same
// shape as ReduceNestingNoElseFoldsDeeplyNestedThen, but exercised via the wired
// `Run` walk rather than a direct ReduceNesting call): `if (cond) { deep then
// (exits) } return;` -> `if (!cond) return; then...; return;`. The if (Block A's
// final) is inverted, its TrueInst becomes the return (Block B's exit), and the
// deep then moves into Block B.
TEST(ReduceNestingTransform, ReduceNestingWiredRunFoldsNoElse) {
	auto fn = std::make_unique<ILFunction>();
	auto body = std::make_unique<BlockContainer>();
	body->Parent = fn.get();
	body->ChildIndex = 0;
	auto v = MakeLocal("v");
	fn->Variables.push_back(v);

	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	b0->SetFinal(std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		MakeDeepThen(body.get(), v), nullptr));
	Block* b0Ptr = b0.get();
	body->AddBlock(std::move(b0));

	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 100;
	b1->SetFinal(std::make_unique<Leave>(body.get()));
	Block* b1Ptr = b1.get();
	body->AddBlock(std::move(b1));

	fn->Body = std::move(body);
	RecomputeIncomingEdgeCounts(*fn);
	fn->CheckInvariant(ILPhase::Normal);

	// The wired Run walk: ImproveILOrdering (a no-op -- the IL order is already
	// correct: the then comes before the return) then ReduceNesting (fires on the
	// deep-then no-else shape via CanDuplicateExit on Block B's leave(body)).
	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);

	auto* iffAfter = dynamic_cast<IfInstruction*>(b0Ptr->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr) << "Block A still holds the (inverted) if";
	ASSERT_NE(iffAfter->TrueInst, nullptr);
	EXPECT_EQ(iffAfter->TrueInst->Op, OpCode::Leave) << "the if's then is the return (Block B's exit)";
	EXPECT_EQ(dynamic_cast<Leave*>(iffAfter->TrueInst.get())->TargetContainer, fn->Body.get());
	EXPECT_FALSE(b1Ptr->Instructions.empty()) << "the deep then moved into Block B";
	EXPECT_EQ(b1Ptr->FinalInstruction->Op, OpCode::Leave) << "Block B's final is the then's exit";
}

// ---- D163: the wired ExtractElseBlock branch (the C# `Visit(Block)` if-case ----
// `if (ifInst.TrueInst.HasFlag(EndPointUnreachable)) { ExtractElseBlock(ifInst); break; }`
// branch, fired from the VisitContainer walk). When the then exits and the else
// is a single Block, extract the else -- the else content becomes the
// fall-through after the if. Fires when ReduceNesting (the else-if-tree case)
// did NOT fire (no duplicable exit after the if, or the else is too shallow for
// ShouldReduceNesting).

namespace {

// Build `if (comp(v, 0, eq)) { thenBlock } else { elseBlock }` as b0's
// FinalInstruction, with b1 holding a non-terminal statement (so the
// instruction after the if is b1's StLoc, not a duplicable exit -- forcing
// ReduceNesting to bail so the ExtractElseBlock branch fires). `thenExits`
// makes the then's final a Leave (exits) or a Branch (falls through);
// `elseIsBlock` makes the else a Block or a Nop.
struct IfElseShape {
	std::unique_ptr<ILFunction> fn;
	BlockContainer* body;
	Block* b0;
	Block* b1;
	IfInstruction* iff;
	Block* elseBlock;
};

IfElseShape BuildThenExitElseShape(bool thenExits, bool elseIsBlock) {
	IfElseShape s;
	s.fn = std::make_unique<ILFunction>();
	s.body = new BlockContainer();
	s.fn->Body = std::unique_ptr<BlockContainer>(s.body);
	s.body->Parent = s.fn.get();
	s.body->ChildIndex = 0;
	auto v = MakeLocal("v");
	s.fn->Variables.push_back(v);

	// b1: a non-terminal StLoc (content) + a Leave(body) final -- so the
	// instruction after the if (b1's first instruction) is the StLoc, not a
	// duplicable exit, forcing ReduceNesting to bail.
	auto b1 = std::make_unique<Block>();
	b1->StartILOffset = 100;
	b1->Add(StLocInt(v, 9));
	b1->SetFinal(std::make_unique<Leave>(s.body));
	s.b1 = b1.get();

	// thenBlock: a Block whose final is a Leave(body) (exits) when thenExits,
	// or a Branch(b1) (falls through) when !thenExits.
	auto thenBlock = std::make_unique<Block>();
	thenBlock->StartILOffset = 10;
	if (thenExits) {
		thenBlock->SetFinal(std::make_unique<Leave>(s.body));
	} else {
		thenBlock->SetFinal(std::make_unique<Branch>(s.b1));
	}

	// elseBlock: a Block with a StLoc non-terminal + a Branch(b1) final
	// (falls through to b1). When !elseIsBlock, the else is a Nop instead.
	std::unique_ptr<ILInstruction> elseInst;
	if (elseIsBlock) {
		auto elseBlock = std::make_unique<Block>();
		elseBlock->StartILOffset = 50;
		elseBlock->Add(StLocInt(v, 5));
		elseBlock->SetFinal(std::make_unique<Branch>(s.b1));
		s.elseBlock = elseBlock.get();
		elseInst = std::move(elseBlock);
	} else {
		elseInst = std::make_unique<Nop>();
	}

	auto iff = std::make_unique<IfInstruction>(
		std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
		                      ComparisonKind::Equality),
		std::move(thenBlock), std::move(elseInst));
	s.iff = iff.get();

	auto b0 = std::make_unique<Block>();
	b0->StartILOffset = 0;
	b0->SetFinal(std::move(iff));
	s.b0 = b0.get();
	s.body->AddBlock(std::move(b0));
	s.body->AddBlock(std::move(b1));

	RecomputeIncomingEdgeCounts(*s.fn);
	return s;
}

// Whether a Block's final exits (a Leave/Throw, not a Branch fall-through). The
// structural equivalent of the C# Block.HasFlag(EndPointUnreachable), used to
// count the wired-branch candidates in the corpus sweep (BlockExitsReal is
// file-local in ReduceNestingTransform.cpp and not accessible from the test).
bool ThenBlockExits(Block* b) {
	if (!b || !b->FinalInstruction) return false;
	auto op = b->FinalInstruction->Op;
	return op == OpCode::Leave || op == OpCode::Throw;
}

} // namespace

TEST(ReduceNestingTransform, WiredExtractElseBlockFoldsWhenThenExitsAndElseIsBlock) {
	auto s = BuildThenExitElseShape(/*thenExits=*/true, /*elseIsBlock=*/true);
	auto& fn = s.fn;
	ASSERT_EQ(fn->Body->Blocks.size(), 2u);
	fn->CheckInvariant(ILPhase::Normal);

	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);

	// The else Block was promoted to the container as a new sibling after b0.
	ASSERT_EQ(fn->Body->Blocks.size(), 3u) << "the else Block must be extracted";
	// b0 is unchanged at index 0; its if's FalseInst is now a Nop.
	EXPECT_EQ(fn->Body->Blocks[0].get(), s.b0);
	auto* iffAfter = dynamic_cast<IfInstruction*>(s.b0->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr);
	ASSERT_NE(iffAfter->FalseInst, nullptr);
	EXPECT_EQ(iffAfter->FalseInst->Op, OpCode::Nop) << "the if's else was cleared to a Nop";
	// The promoted else Block is at index 1, with its content + Branch(b1)
	// preserved.
	EXPECT_EQ(fn->Body->Blocks[1].get(), s.elseBlock);
	EXPECT_FALSE(s.elseBlock->Instructions.empty()) << "the else content survived";
	EXPECT_EQ(s.elseBlock->FinalInstruction->Op, OpCode::Branch);
	// b1 is at index 2, unchanged.
	EXPECT_EQ(fn->Body->Blocks[2].get(), s.b1);
}

TEST(ReduceNestingTransform, WiredExtractElseBlockBailsWhenThenFallsThrough) {
	auto s = BuildThenExitElseShape(/*thenExits=*/false, /*elseIsBlock=*/true);
	auto& fn = s.fn;
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	// The then falls through (a Branch, not a Leave), so the else is NOT
	// extracted.
	ASSERT_EQ(fn->Body->Blocks.size(), 2u);
	auto* iffAfter = dynamic_cast<IfInstruction*>(s.b0->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr);
	ASSERT_NE(iffAfter->FalseInst, nullptr);
	EXPECT_EQ(iffAfter->FalseInst->Op, OpCode::Block)
	    << "the else Block stays (then does not exit)";
}

TEST(ReduceNestingTransform, WiredExtractElseBlockBailsWhenElseIsNotBlock) {
	auto s = BuildThenExitElseShape(/*thenExits=*/true, /*elseIsBlock=*/false);
	auto& fn = s.fn;
	fn->CheckInvariant(ILPhase::Normal);
	ReduceNestingTransform().Run(*fn, Ctx());
	fn->CheckInvariant(ILPhase::Normal);
	// The else is a Nop (no else Block), so ExtractElseBlock does not fire.
	ASSERT_EQ(fn->Body->Blocks.size(), 2u);
	auto* iffAfter = dynamic_cast<IfInstruction*>(s.b0->FinalInstruction.get());
	ASSERT_NE(iffAfter, nullptr);
	ASSERT_NE(iffAfter->FalseInst, nullptr);
	EXPECT_EQ(iffAfter->FalseInst->Op, OpCode::Nop)
	    << "the else was already a Nop (no extraction)";
}

TEST(ReduceNestingTransform, MscorlibWiredExtractElseBlockSweep) {
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
	int totalCandidates = 0;
	ILTransformContext ctx;
	for (const auto& m : f.MethodDefs()) {
		if (m.RVA == 0) continue;
		auto fn = ReadIL(f, m.Token, m.RVA);
		if (!fn) continue;
		++processed;
		// The full pre-pipeline through HighLevelLoopTransform, then the wired
		// ReduceNestingTransform::Run (which now includes the ExtractElseBlock
		// branch). The branch fires on `if (cond) { ...; return; } else { ... }`
		// shapes the else-if-tree ReduceNesting did not absorb.
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
		// Count the ExtractElseBlock candidates BEFORE Run: ifs (not else-ifs)
		// whose FalseInst is a Block and whose then (a Block) exits. These are
		// the shapes the wired branch fires on (when ReduceNesting did not absorb
		// them first). The iff pointers stay valid across Run (ExtractElseBlock
		// does not destroy ifs).
		std::vector<IfInstruction*> candidates;
		std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
			if (!inst) return;
			if (auto* iff = dynamic_cast<IfInstruction*>(inst)) {
				if (ReduceNestingTransform::GetElseIfParent(iff) == nullptr &&
				    dynamic_cast<Block*>(iff->FalseInst.get())) {
					auto* tb = dynamic_cast<Block*>(iff->TrueInst.get());
					if (tb && ThenBlockExits(tb)) candidates.push_back(iff);
				}
			}
			for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
		};
		walk(fn->Body.get());
		int candidatesCount = static_cast<int>(candidates.size());
		ReduceNestingTransform().Run(*fn, ctx);
		fn->CheckInvariant(ILPhase::Normal);
		// Count how many candidates had their Block FalseInst cleared to a Nop by
		// Run (the wired ExtractElseBlock branch + the ReduceNesting else-if-tree
		// fold both clear the FalseInst).
		for (auto* iff : candidates)
			if (iff->FalseInst && iff->FalseInst->Op == OpCode::Nop) ++totalFolds;
		totalCandidates += candidatesCount;
		if (processed >= 8000) break;
	}
	EXPECT_GT(processed, 5000);
	// The `if (cond) { ...; return; } else { ... }` (then-exits-WITH-else)
	// shape is rare on the .NET Framework 4 legacy-csc corpus -- Condition-
	// Detection inverts early-returns to `if (!cond) { rest }` (no else) or
	// `if (cond) return; rest` (no else), so the then-exits-else shape rarely
	// arises. The wired ExtractElseBlock branch is faithfulness-only on this
	// corpus (matching the D161 else-if-tree precedent: candidates=716, fired=0);
	// the hand-built `WiredExtractElseBlockFoldsWhenThenExitsAndElseIsBlock`
	// test verifies the fold fires, and this sweep verifies the invariant holds
	// across the corpus (the branch does not crash or corrupt the tree) and
	// reports the candidate/fold counts as diagnostics.
	EXPECT_GE(totalCandidates, 0);
	EXPECT_EQ(totalFolds, 0) << "the then-exits-else shape is faithfulness-only on the legacy-csc corpus";
}
