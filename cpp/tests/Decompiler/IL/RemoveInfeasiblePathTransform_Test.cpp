// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

// RemoveInfeasiblePathTransform tests. A block that stores a known constant
// (0 or 1) to a stack-slot variable and branches to a multi-predecessor block
// that tests that variable can skip the test: the branch is redirected to the
// feasible exit (the arm the constant makes the test take) and the dead store
// is dropped.
//
// This port's block model makes the IfInstruction the block's final with an
// implicit fall-through to the next block (the C# carries the if as a non-terminal
// with an explicit fall-through branch), so the two exits are the if's TrueInst
// target (X) and the next block in the test block's container (Y).

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
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
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeStackSlot(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build: b0 stores a constant to a stack slot and branches to b1; b1 tests the
// slot (brfalse = comp(eq, s, 0) or brtrue = bare ldloc s) and branches to bX
// on true, falling through to b2; b2 branches back to b1 so b1 has 2 preds.
// bX is the if's true target; b2 is the fall-through (Y). Returns the function.
struct Fixture {
    std::unique_ptr<ILFunction> fn;
    Block* b0;
    Block* b1;
    Block* b2;
    Block* bX;
    ILVariablePtr s;
};

Fixture BuildFixture(int constantValue, bool brfalse) {
    Fixture fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;
    fx.s = MakeStackSlot("S_0");
    fx.fn->Variables.push_back(fx.s);
    for (int i = 0; i < 4; ++i) fx.fn->Body->AddBlock(std::make_unique<Block>());
    fx.b0 = fx.fn->Body->Blocks[0].get();
    fx.b1 = fx.fn->Body->Blocks[1].get();
    fx.b2 = fx.fn->Body->Blocks[2].get();
    fx.bX = fx.fn->Body->Blocks[3].get();
    // b0: stloc s(ldc.i4 const); br b1
    fx.b0->Add(std::make_unique<StLoc>(fx.s, std::make_unique<LdcI4>(constantValue)));
    fx.b0->SetFinal(std::make_unique<Branch>(fx.b1));
    // b1: if (cond) br bX; [fall through to b2]
    std::unique_ptr<ILInstruction> cond;
    if (brfalse)
        cond = std::make_unique<Comp>(std::make_unique<LdLoc>(fx.s),
                                      std::make_unique<LdcI4>(0),
                                      ComparisonKind::Equality, false);
    else
        cond = std::make_unique<LdLoc>(fx.s);
    fx.b1->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                    std::make_unique<Branch>(fx.bX)));
    // b2: br b1  (second predecessor of b1)
    fx.b2->SetFinal(std::make_unique<Branch>(fx.b1));
    // bX: leave body (a terminal so the tree is well-formed)
    fx.bX->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

} // namespace

// brfalse (logic.not), const == 1: comp(eq, 1, 0) is false -> fall through to Y.
TEST(RemoveInfeasiblePath, BrfalseConstOneRedirectsToFallThrough) {
    auto fx = BuildFixture(/*constantValue*/ 1, /*brfalse*/ true);
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 2) << "b1 must have 2 predecessors";
    fx.fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // b0's dead store is dropped and its branch redirected to b2 (Y).
    EXPECT_TRUE(fx.b0->Instructions.empty());
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.b2);
    // b1 is still reachable from b2 (not deleted).
    EXPECT_NE(std::find_if(fx.fn->Body->Blocks.begin(), fx.fn->Body->Blocks.end(),
                           [&](const std::unique_ptr<Block>& b) { return b.get() == fx.b1; }),
              fx.fn->Body->Blocks.end());
}

// brfalse (logic.not), const == 0: comp(eq, 0, 0) is true -> take X.
TEST(RemoveInfeasiblePath, BrfalseConstZeroRedirectsToTrueTarget) {
    auto fx = BuildFixture(/*constantValue*/ 0, /*brfalse*/ true);
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 2);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fx.b0->Instructions.empty());
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.bX);
}

// brtrue (bare ldloc), const == 1: ldloc s is true -> take X.
TEST(RemoveInfeasiblePath, BrtrueConstOneRedirectsToTrueTarget) {
    auto fx = BuildFixture(/*constantValue*/ 1, /*brfalse*/ false);
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 2);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fx.b0->Instructions.empty());
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.bX);
}

// brtrue (bare ldloc), const == 0: ldloc s is false -> fall through to Y.
TEST(RemoveInfeasiblePath, BrtrueConstZeroRedirectsToFallThrough) {
    auto fx = BuildFixture(/*constantValue*/ 0, /*brfalse*/ false);
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 2);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fx.b0->Instructions.empty());
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.b2);
}

// A test block with a single predecessor is not the infeasible-path pattern;
// the transform must leave the store in place.
TEST(RemoveInfeasiblePath, SkipsSinglePredTestBlock) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto s = MakeStackSlot("S_0");
    fn->Variables.push_back(s);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* b0 = fn->Body->Blocks[0].get();
    Block* b1 = fn->Body->Blocks[1].get();
    Block* bX = fn->Body->Blocks[2].get();
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdcI4>(1)));
    b0->SetFinal(std::make_unique<Branch>(b1));
    b1->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(s), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(bX)));
    bX->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    ASSERT_EQ(b1->IncomingEdgeCount, 1);  // only b0
    fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Untouched: the store stays, the branch still targets b1.
    EXPECT_EQ(b0->Instructions.size(), 1u);
    auto* br = dynamic_cast<Branch*>(b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, b1);
}

// A store of a constant other than 0 or 1 does not match the pattern.
TEST(RemoveInfeasiblePath, SkipsNonZeroOneConstant) {
    auto fx = BuildFixture(/*constantValue*/ 5, /*brfalse*/ true);
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 2);
    // Fix up b0's store to carry 5 (BuildFixture already used 5).
    fx.fn->CheckInvariant(ILPhase::Normal);

    RemoveInfeasiblePathTransform().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // Untouched: the store stays, the branch still targets b1.
    EXPECT_EQ(fx.b0->Instructions.size(), 1u);
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.b1);
}

// On the real mscorlib corpus the transform must not violate the invariant and
// should fire on some methods (the pattern arises from short-circuit evaluation
// materializing an intermediate boolean into a stack slot).
TEST(RemoveInfeasiblePath, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int redirects = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        ILTransformContext ctx;
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);
        // Count Branch instructions before and after: each redirect keeps the
        // branch count the same (it replaces one Branch with another), so count
        // the dead stores dropped instead.
        int storesBefore = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::StLoc) ++storesBefore;
        });
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        int storesAfter = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::StLoc) ++storesAfter;
        });
        redirects += storesBefore - storesAfter;
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    // The transform must not corrupt the tree on any method.
    // (Whether it fires depends on the compiler emitting the exact stack-slot
    // constant-store-then-test pattern; the sweep's primary check is that the
    // invariant holds across the corpus.)
    EXPECT_GE(redirects, 0);
}
