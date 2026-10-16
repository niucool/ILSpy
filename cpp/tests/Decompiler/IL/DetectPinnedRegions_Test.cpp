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

// DetectPinnedRegions tests. IL pins locals so the GC will not move them; the
// only C# surface for pinning is a `fixed` block, which is scoped. This transform
// detects the scoped region a pinned local covers and wraps it in a PinnedRegion.
//
// This port's block model makes the IfInstruction the block's final with an
// implicit fall-through (the C# carries the if as a non-terminal with an explicit
// fall-through branch), so a pinned write is the block's last non-final
// instruction and the Branch to the region entry is the block's final. The pin
// block, after the region is extracted, falls through to the next outer block.

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
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
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
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

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

ILVariablePtr MakePinned(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::PinnedLocal, std::move(type), -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeStackSlot(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

// Build:
//   b0: stloc P, ldloc arr; br b1        (the pin store + branch to the region)
//   b1: stloc S0, ldloc P; br b2         (region: use P)
//   b2: br b3                            (region: fall through to the unpin)
//   b3: stloc P, ldc.i4(0); leave body   (the unpin + exit)
// DetectPinnedRegions should wrap b1+b2 in a PinnedRegion and strip b3's unpin.
struct Fixture {
    std::unique_ptr<ILFunction> fn;
    Block* b0;
    Block* b1;
    Block* b2;
    Block* b3;
    ILVariablePtr P;
    ILVariablePtr arr;
    ILVariablePtr S0;
};

Fixture BuildFixture() {
    Fixture fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;
    fx.P = MakePinned("P", std::make_shared<KnownType>(KnownTypeCode::String));
    fx.arr = MakeLocal("arr");
    fx.arr->Type = std::make_shared<KnownType>(KnownTypeCode::String);
    fx.S0 = MakeStackSlot("S0");
    fx.fn->Variables.push_back(fx.P);
    fx.fn->Variables.push_back(fx.arr);
    fx.fn->Variables.push_back(fx.S0);
    for (int i = 0; i < 4; ++i) fx.fn->Body->AddBlock(std::make_unique<Block>());
    fx.b0 = fx.fn->Body->Blocks[0].get();
    fx.b1 = fx.fn->Body->Blocks[1].get();
    fx.b2 = fx.fn->Body->Blocks[2].get();
    fx.b3 = fx.fn->Body->Blocks[3].get();
    fx.b0->Add(std::make_unique<StLoc>(fx.P, std::make_unique<LdLoc>(fx.arr)));
    fx.b0->SetFinal(std::make_unique<Branch>(fx.b1));
    fx.b1->Add(std::make_unique<StLoc>(fx.S0, std::make_unique<LdLoc>(fx.P)));
    fx.b1->SetFinal(std::make_unique<Branch>(fx.b2));
    fx.b2->SetFinal(std::make_unique<Branch>(fx.b3));
    fx.b3->Add(std::make_unique<StLoc>(fx.P, std::make_unique<LdcI4>(0)));
    fx.b3->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

} // namespace

// A bare PinnedRegion node passes the tree invariant.
TEST(DetectPinnedRegions, PinnedRegionNodeInvariant) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = MakePinned("P", std::make_shared<KnownType>(KnownTypeCode::String));
    fn->Variables.push_back(P);
    auto body = std::make_unique<BlockContainer>();
    body->AddBlock(std::make_unique<Block>());
    body->Blocks.front()->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    auto region = std::make_unique<PinnedRegion>(
        P, std::make_unique<LdLoc>(P), std::move(body));
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks.front()->Add(std::move(region));
    fn->Body->Blocks.front()->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    // The region renders as a pinned.region dump.
    std::string dump;
    fn->Body->Blocks.front()->WriteTo(dump);
    EXPECT_NE(dump.find("pinned.region"), std::string::npos);
}

// The transform wraps the region in a PinnedRegion and strips the unpin store.
TEST(DetectPinnedRegions, FormsPinnedRegionAndStripsUnpin) {
    auto fx = BuildFixture();
    ASSERT_EQ(fx.b1->IncomingEdgeCount, 1);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectPinnedRegions().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // b0 now holds a PinnedRegion (replacing the stloc P) and falls through to b3.
    ASSERT_FALSE(fx.b0->Instructions.empty());
    auto* region = dynamic_cast<PinnedRegion*>(fx.b0->Instructions.back().get());
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->Variable.get(), fx.P.get());
    // The body container exists and contains the region blocks (b1, b2).
    auto* body = dynamic_cast<BlockContainer*>(region->Body.get());
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Blocks.size(), 2u);
    // b0 falls through to b3 (the unpin block, now the next outer block).
    auto* br = dynamic_cast<Branch*>(fx.b0->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.b3);
    // b3's leading unpin store was stripped (it had a single incoming edge).
    EXPECT_TRUE(fx.b3->Instructions.empty());
}

// A pinned write followed by non-branch code is split so the region starts
// cleanly right after the pin store.
TEST(DetectPinnedRegions, SplitsBlockAfterPinnedWrite) {
    auto fx = BuildFixture();
    // Inject a non-branch instruction after the pin store in b0.
    fx.b0->Instructions.clear();
    fx.b0->Add(std::make_unique<StLoc>(fx.P, std::make_unique<LdLoc>(fx.arr)));
    fx.b0->Add(std::make_unique<StLoc>(fx.S0, std::make_unique<LdcI4>(1)));
    fx.b0->SetFinal(std::make_unique<Branch>(fx.b1));
    RecomputeIncomingEdgeCounts(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectPinnedRegions().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // The pin block holds a PinnedRegion; the stloc S0 was moved into the region
    // body (it runs inside the fixed block).
    ASSERT_FALSE(fx.b0->Instructions.empty());
    auto* region = dynamic_cast<PinnedRegion*>(fx.b0->Instructions.back().get());
    ASSERT_NE(region, nullptr);
    auto* body = dynamic_cast<BlockContainer*>(region->Body.get());
    ASSERT_NE(body, nullptr);
    // The region entry carries the stloc S0 (moved in by the split).
    bool foundS0 = false;
    Walk(body, [&](ILInstruction* inst) {
        auto* st = dynamic_cast<StLoc*>(inst);
        if (st && st->Variable.get() == fx.S0.get()) foundS0 = true;
    });
    EXPECT_TRUE(foundS0);
}

// A value-typed pinned local (where the pinned flag has no effect) is left
// alone -- no PinnedRegion is formed.
TEST(DetectPinnedRegions, SkipsValueTypedPinnedLocal) {
    auto fx = BuildFixture();
    // Re-type P to a struct (System.Int32 is a struct) -- pinning a value type
    // has no effect, so the transform must not form a region.
    fx.P->Type = std::make_shared<KnownType>(KnownTypeCode::Int32);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectPinnedRegions().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    ASSERT_FALSE(fx.b0->Instructions.empty());
    EXPECT_EQ(dynamic_cast<PinnedRegion*>(fx.b0->Instructions.back().get()), nullptr);
}

// On the real mscorlib corpus the transform must not violate the invariant.
// Pinned locals are common (204 methods), so this is a real exercise.
TEST(DetectPinnedRegions, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int regionsFormed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        ILTransformContext ctx;
        // The C# GetILTransforms() order: CFS, ILInlining, InlineReturn,
        // RemoveInfeasiblePath, DetectPinnedRegions. (SplitVariables is deferred;
        // StObjToStLoc is pulled forward.) DetectPinnedRegions must run after
        // inlining and before loop detection.
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        int before = 0;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (dynamic_cast<PinnedRegion*>(inst)) ++before;
        });
        DetectPinnedRegions().Run(*fn, ctx);
        int after = 0;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (dynamic_cast<PinnedRegion*>(inst)) ++after;
        });
        regionsFormed += (after - before);
        fn->CheckInvariant(ILPhase::Normal);
    }
    // The invariant holds on every method; pinned locals are common (204
    // methods in this mscorlib), so the transform fires on at least some.
    EXPECT_GT(processed, 0);
    EXPECT_GT(regionsFormed, 0);
}
