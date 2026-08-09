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

// RemoveDeadVariableInit tests. The transform drops dead stores to variables
// that are never read from: a variable flagged RemoveIfRedundant (e.g. by
// RemoveInfeasiblePath) or under the RemoveDeadStores setting, with LoadCount
// and AddressCount both 0, has every StLoc to it dropped (a pure value goes
// with the store; an impure value is unwrapped so its side effect survives).
// Dead-copy chains (a removed store whose value loaded a now-dead variable)
// are handled by a recompute fixpoint.

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
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
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, KnownTypeCode code) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local,
        std::make_shared<KnownType>(code), -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeStackSlot(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILTransformContext MakeCtx(bool removeDeadStores) {
    ILTransformContext ctx;
    ctx.Settings.RemoveDeadStores = removeDeadStores;
    return ctx;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

// A RemoveIfRedundant-flagged stack slot with two stores and no loads has both
// stores dropped.
TEST(RemoveDeadVariableInit, DropsStoresToRedundantVariable) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto s = MakeStackSlot("S_0");
    s->RemoveIfRedundant = true;
    fn->Variables.push_back(s);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(s, std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(s, std::make_unique<LdcI4>(2)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ false);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fn->Body->Blocks[0]->Instructions.empty())
        << "both dead stores to the flagged stack slot are dropped";
}

// Under the RemoveDeadStores setting, a never-loaded local's stores are dropped
// even without the RemoveIfRedundant flag.
TEST(RemoveDeadVariableInit, DropsStoresUnderRemoveDeadStoresSetting) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("num", KnownTypeCode::Int32);
    fn->Variables.push_back(v);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(42)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ true);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fn->Body->Blocks[0]->Instructions.empty())
        << "the dead store is dropped under RemoveDeadStores";
}

// Without the setting or the flag, a never-loaded local's store is kept.
TEST(RemoveDeadVariableInit, KeepsStoreWithoutSettingOrFlag) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("num", KnownTypeCode::Int32);
    fn->Variables.push_back(v);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(42)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ false);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u)
        << "a dead store is kept without the flag or setting";
}

// A variable that is loaded keeps its stores.
TEST(RemoveDeadVariableInit, KeepsStoreWhenVariableIsLoaded) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("num", KnownTypeCode::Int32);
    v->RemoveIfRedundant = true;
    fn->Variables.push_back(v);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(42)));
    // A use: leave(ldloc v) -- the store is not dead.
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(),
        std::make_unique<LdLoc>(v)));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ false);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u)
        << "a store to a loaded variable is kept";
}

// A dead store whose value has a side effect (a Call) is unwrapped: the store
// goes, the call stays.
TEST(RemoveDeadVariableInit, UnwrapsImpureValue) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("num", KnownTypeCode::Int32);
    v->RemoveIfRedundant = true;
    fn->Variables.push_back(v);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto call = std::make_unique<Call>("NS.T::M");
    call->ReturnType = StackType::I4;
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(call)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ false);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions[0]->Op, OpCode::Call)
        << "the impure call survives the dropped store";
    int stlocCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::StLoc) ++stlocCount; });
    EXPECT_EQ(stlocCount, 0) << "the dead store is gone";
}

// A dead-copy chain: `stloc v(ldloc w)` where v is flagged and w is also dead.
// Removing v's store (which loads w) makes w dead; the fixpoint drops w's store
// too. (removeDeadStores is on so both locals qualify without per-variable
// flags.)
TEST(RemoveDeadVariableInit, DropsDeadCopyChain) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("a", KnownTypeCode::Int32);
    auto w = MakeLocal("b", KnownTypeCode::Int32);
    fn->Variables.push_back(v);
    fn->Variables.push_back(w);
    fn->Body->AddBlock(std::make_unique<Block>());
    // stloc v(ldloc w)  -- a dead copy of w into v
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(w)));
    // stloc w(ldc.i4 7) -- w's only store; once the load above is gone, w is dead
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(w, std::make_unique<LdcI4>(7)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ true);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fn->Body->Blocks[0]->Instructions.empty())
        << "the dead-copy chain is fully collapsed";
    int stlocCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::StLoc) ++stlocCount; });
    EXPECT_EQ(stlocCount, 0);
}

// A parameter is never a dead-store target (Kind == Parameter is skipped), even
// under RemoveDeadStores.
TEST(RemoveDeadVariableInit, SkipsParameters) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto p = std::make_shared<ILVariable>(VariableKind::Parameter,
        std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    p->Name = "arg_0";
    fn->Variables.push_back(p);
    fn->Body->AddBlock(std::make_unique<Block>());
    // A store to the parameter (re-assignment) -- kept, because parameters are
    // not dead-store targets.
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(p, std::make_unique<LdcI4>(9)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ true);
    RemoveDeadVariableInit().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u)
        << "a store to a parameter is not a dead store";
}

// On the real mscorlib corpus the transform must not violate the invariant. The
// sweep runs the full CLI pre-pipeline through RemoveDeadVariableInit (with
// RemoveInfeasiblePath firing and flagging stack slots RemoveIfRedundant) and
// confirms the ILAst invariant holds across the corpus.
TEST(RemoveDeadVariableInit, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    ILTransformContext ctx = MakeCtx(/*removeDeadStores*/ false);
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
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
}
