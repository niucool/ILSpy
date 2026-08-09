// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for CachedReadOnlySpanInitialization (the next in-order Phase 4
// transform after CachedDelegateInitialization). It collapses the
// compiler-synthesized lazy cache Roslyn emits for a ReadOnlySpan<T> created
// from an array literal on frameworks without RuntimeHelpers.CreateSpan:
//
//   stloc V(ldobj T[](ldsflda <PrivateImplementationDetails>.cache))
//   if (comp(ldloc V == ldnull)) {
//       stloc V(arrayInitializer)
//       stobj T[](ldsflda <PrivateImplementationDetails>.cache, ldloc V)
//   }
//   ... single usage of V ...
//
// into the unconditional init `stloc V(arrayInitializer); ... usage ...`, so a
// later array-initializer transform recovers the literal and the escaped
// <PrivateImplementationDetails> cache field disappears. ReadOnlySpan<T> is
// absent from the .NET Framework 4 mscorlib corpus, so this fires 0 times on it
// (it fires on Roslyn-compiled / modern .NET with System.Memory); the hand-built
// tests verify the rewrite and the sweep verifies the ILAst invariant holds,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj /
// SwitchOnNullable precedent (a transform that fires only on Roslyn-compiled
// assemblies is still ported for faithfulness).
//
// The positive test builds the post-reader shape (the cache load + the if-final
// with the body as a separate fall-through block + the usage block, mirroring
// what the IL reader emits) and runs the full pre-pipeline through
// ConditionDetection (the GetILTransforms() point the transform sits at), so
// the transform is exercised on the real post-ConditionDetection shape (the
// C# shape: condition `V == null`, body in the if's TrueInst as a Block
// carrying a trailing Branch to the usage block).

#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
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
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
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

std::unique_ptr<LdsFlda> MakeCacheFlda(std::string name, bool compilerGenerated,
                                      std::uint32_t token = 0x04000001) {
    auto f = std::make_unique<LdsFlda>(std::move(name));
    f->IsCompilerGeneratedField = compilerGenerated;
    f->FieldToken = token;
    return f;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

int CountIfs(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::IfInstruction) ++n; });
    return n;
}

// Count stobj write-backs to the cache field (0 after a successful fold).
int CountStObjsTo(ILFunction& fn, const std::string& fieldName) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        auto* st = dynamic_cast<StObj*>(i);
        if (!st) return;
        auto* f = dynamic_cast<LdsFlda*>(st->Target.get());
        if (f && f->FieldName == fieldName) ++n;
    });
    return n;
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
}

// Build the post-reader ReadOnlySpan cache shape and run the pre-pipeline
// through ConditionDetection + CachedDelegateInitialization (the
// GetILTransforms() point CachedReadOnlySpanInitialization sits at). The body
// block holds the init store + the cache write-back; the usage block holds the
// single downstream use of V. `cacheCG` controls the cache field's
// CompilerGenerated flag; `usageCount` adds extra loads of V (to break the
// LoadCount == 3 guard); `elseArm` adds an else to the if (to break the
// no-else guard); `wrongInitVar` makes trueBlock[0] store a different variable;
// `wrongWritebackField` makes the write-back target a different field; `condKind`
// overrides the null-check comparison kind.
struct Setup {
    std::unique_ptr<ILFunction> fn;
    ILVariablePtr V;
    Block* P = nullptr;
    Block* usage = nullptr;
};

Setup BuildAndRunPrePipeline(bool cacheCG = true, int usageCount = 1,
                             bool elseArm = false, bool wrongInitVar = false,
                             bool wrongWritebackField = false,
                             ComparisonKind condKind = ComparisonKind::Inequality,
                             bool extraStore = false, bool extraLoad = false) {
    Setup s;
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    s.V = MakeLocal("V");
    fn->Variables.push_back(s.V);

    // usage block: <usageCount> uses of V; leave(body).
    auto usage = std::make_unique<Block>();
    usage->StartILOffset = 0x40;
    for (int i = 0; i < usageCount; ++i) {
        auto call = std::make_unique<Call>("System.ReadOnlySpan`1..ctor");
        call->AddArg(std::make_unique<LdLoc>(s.V));
        call->ReturnType = StackType::Void;
        usage->Add(std::move(call));
    }
    if (extraLoad) {
        auto call = std::make_unique<Call>("System.Foo::Extra");
        call->AddArg(std::make_unique<LdLoc>(s.V));
        call->ReturnType = StackType::Void;
        usage->Add(std::move(call));
    }
    usage->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    Block* usagePtr = usage.get();

    // body block: stloc V(init); stobj(ldsflda cache, ldloc V); br usage.
    auto body = std::make_unique<Block>();
    body->StartILOffset = 0x20;
    auto init = std::make_unique<Call>("System.Runtime.CompilerServices.CreateArray");
    init->ReturnType = StackType::O;
    if (wrongInitVar) {
        auto other = MakeLocal("W");
        fn->Variables.push_back(other);
        body->Add(std::make_unique<StLoc>(other, std::move(init)));
    } else {
        body->Add(std::make_unique<StLoc>(s.V, std::move(init)));
    }
    auto wbFlda = MakeCacheFlda(wrongWritebackField ? "<PrivateImplementationDetails>::other" :
                                 "<PrivateImplementationDetails>::cache",
                                 cacheCG, wrongWritebackField ? 0x04000002 : 0x04000001);
    body->Add(std::make_unique<StObj>(std::move(wbFlda), std::make_unique<LdLoc>(s.V), nullptr));
    body->SetFinal(std::make_unique<Branch>(usagePtr));

    // P block: stloc V(ldobj(ldsflda cache)); if (comp(V != null)) br usage [+ else].
    auto P = std::make_unique<Block>();
    P->StartILOffset = 0x00;
    auto loadFlda = MakeCacheFlda("<PrivateImplementationDetails>::cache", cacheCG, 0x04000001);
    auto load = std::make_unique<LdObj>(std::move(loadFlda), nullptr);
    P->Add(std::make_unique<StLoc>(s.V, std::move(load)));
    if (extraStore) {
        P->Add(std::make_unique<StLoc>(s.V, std::make_unique<LdNull>()));
    }
    // The reader emits `if (comp(V != null)) br usage` for `brtrue usage` (skip
    // the body when the cache is populated). ConditionDetection inverts it to
    // `if (comp(V == null)) { body }` (the C# shape) -- but to exercise both the
    // pre-inversion shape and the condKind override, emit condKind directly.
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(s.V), std::make_unique<LdNull>(),
                                       condKind, false);
    std::unique_ptr<ILInstruction> trueInst = std::make_unique<Branch>(usagePtr);
    std::unique_ptr<ILInstruction> falseInst = nullptr;
    if (elseArm) {
        // Add an else (a Block ending in a leave) so the no-else guard fails.
        auto elseBlock = std::make_unique<Block>();
        elseBlock->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        falseInst = std::move(elseBlock);
    }
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueInst),
                                               std::move(falseInst));
    P->SetFinal(std::move(iff));
    s.P = P.get();

    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(body));
    fn->Body->AddBlock(std::move(usage));
    s.usage = usagePtr;

    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    RunPrePipeline(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    s.fn = std::move(fn);
    return s;
}

} // namespace

// The canonical shape folds: the if is gone, the cache write-back (stobj to the
// cache field) is gone, and the cache-load store now holds the initializer
// value (the array constructor) instead of the ldobj(ldsflda cache).
TEST(CachedReadOnlySpanInitialization, FoldsCacheToInitializer) {
    auto setup = BuildAndRunPrePipeline();
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountIfs(*fn), 0);
    EXPECT_EQ(CountStObjsTo(*fn, "<PrivateImplementationDetails>::cache"), 0);
    // The host block's last instruction is stloc V(initializer).
    ASSERT_FALSE(setup.P->Instructions.empty());
    auto* st = dynamic_cast<StLoc*>(setup.P->Instructions.back().get());
    ASSERT_NE(st, nullptr);
    ASSERT_EQ(st->Variable.get(), setup.V.get());
    ASSERT_EQ(st->Value->Op, OpCode::Call);
    EXPECT_EQ(static_cast<Call*>(st->Value.get())->MethodName,
              std::string("System.Runtime.CompilerServices.CreateArray"));
    // The block falls through to the usage (no goto): the final is null or a
    // Branch to the usage block. Either way no spurious goto to a non-next block.
    if (setup.P->FinalInstruction) {
        // If a final remains it must target the usage (the next block).
        auto* br = dynamic_cast<Branch*>(setup.P->FinalInstruction.get());
        ASSERT_NE(br, nullptr);
        EXPECT_EQ(br->TargetBlock, setup.usage);
    }
}

// A cache field that is not compiler-generated does not fold.
TEST(CachedReadOnlySpanInitialization, RejectsNonCompilerGeneratedCacheField) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/false);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A variable with an extra store (StoreCount != 2) does not fold.
TEST(CachedReadOnlySpanInitialization, RejectsExtraStore) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/true, /*usageCount=*/1, /*elseArm=*/false,
                                        /*wrongInitVar=*/false, /*wrongWritebackField=*/false,
                                        /*condKind=*/ComparisonKind::Inequality, /*extraStore=*/true);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A variable with an extra load (LoadCount != 3) does not fold.
TEST(CachedReadOnlySpanInitialization, RejectsExtraLoad) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/true, /*usageCount=*/1, /*elseArm=*/false,
                                        /*wrongInitVar=*/false, /*wrongWritebackField=*/false,
                                        /*condKind=*/ComparisonKind::Inequality, /*extraStore=*/false,
                                        /*extraLoad=*/true);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// An if with an else arm does not fold (the C# requires FalseInst.MatchNop()).
TEST(CachedReadOnlySpanInitialization, RejectsElseArm) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/true, /*usageCount=*/1, /*elseArm=*/true);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A condition that is comp(V != null) (Inequality, not Equality) does not
// fold (MatchCompEqualsNull requires Equality). The real pipeline always
// inverts the cache null-check to Equality, so this shape is constructed
// directly (the post-ConditionDetection C# shape but with Inequality) to test
// the guard in isolation.
TEST(CachedReadOnlySpanInitialization, RejectsInequalityCondition) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);

    // usage block (the single downstream use).
    auto usage = std::make_unique<Block>();
    usage->StartILOffset = 0x40;
    auto use = std::make_unique<Call>("System.ReadOnlySpan`1..ctor");
    use->AddArg(std::make_unique<LdLoc>(V));
    use->ReturnType = StackType::Void;
    usage->Add(std::move(use));
    usage->SetFinal(std::make_unique<Leave>(fn->Body.get()));

    // P block: stloc V(ldobj(cache)); if (comp(V != null)) { Block{ stloc V(init); stobj(cache, ldloc V) } }
    auto P = std::make_unique<Block>();
    P->StartILOffset = 0x00;
    P->Add(std::make_unique<StLoc>(V,
        std::make_unique<LdObj>(MakeCacheFlda("<PrivateImplementationDetails>::cache", true, 0x04000001), nullptr)));
    auto body = std::make_unique<Block>();
    body->Add(std::make_unique<StLoc>(V, [] {
        auto c = std::make_unique<Call>("System.Runtime.CompilerServices.CreateArray");
        c->ReturnType = StackType::O; return c; }()));
    body->Add(std::make_unique<StObj>(MakeCacheFlda("<PrivateImplementationDetails>::cache", true, 0x04000001),
                                      std::make_unique<LdLoc>(V), nullptr));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdNull>(),
                                       ComparisonKind::Inequality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(body));
    P->SetFinal(std::move(iff));

    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(usage));

    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A trueBlock[0] that stores a different variable does not fold.
TEST(CachedReadOnlySpanInitialization, RejectsInitStoreDifferentVariable) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/true, /*usageCount=*/1, /*elseArm=*/false,
                                        /*wrongInitVar=*/true);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A trueBlock[1] write-back to a different cache field does not fold.
TEST(CachedReadOnlySpanInitialization, RejectsWritebackToDifferentField) {
    auto setup = BuildAndRunPrePipeline(/*cacheCG=*/true, /*usageCount=*/1, /*elseArm=*/false,
                                        /*wrongInitVar=*/false, /*wrongWritebackField=*/true);
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// With the ArrayInitializers setting off, the transform is a no-op.
TEST(CachedReadOnlySpanInitialization, ArrayInitializersOffIsNoOp) {
    auto setup = BuildAndRunPrePipeline();
    auto& fn = setup.fn;
    ILTransformContext ctx;
    ctx.Settings.ArrayInitializers = false;
    CachedReadOnlySpanInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// On the real mscorlib corpus, running the full pre-pipeline through
// CachedDelegateInitialization + CachedReadOnlySpanInitialization (the
// GetILTransforms() position) preserves the ILAst invariant. ReadOnlySpan<T>
// is absent from the .NET Framework 4 mscorlib corpus, so the transform fires 0
// times; the sweep confirms it is a safe no-op across the corpus (no crash,
// invariant holds).
TEST(CachedReadOnlySpanInitialization, MscorlibSweepPreservesInvariant) {
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
        CachedReadOnlySpanInitialization().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
}
