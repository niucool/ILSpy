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

// CopyPropagation tests (subset). (1) A dead store to a single-definition stack
// slot with no loads is dropped (pure value) or replaced with its value (side
// effects kept). (2) A single-definition stack slot assigned from a never-
// assigned parameter has all its loads replaced with the parameter, and the
// store is dropped.

#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, VariableKind kind = VariableKind::StackSlot) {
    auto v = std::make_shared<ILVariable>(kind, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

int CountLoads(ILFunction& fn, ILVariable* v) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* ld = dynamic_cast<LdLoc*>(inst))
            if (ld->Variable.get() == v) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

int CountAddressLoads(ILFunction& fn, ILVariable* v) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* lda = dynamic_cast<LdLoca*>(inst))
            if (lda->Variable.get() == v) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

bool HasStoreOf(ILFunction& fn, ILVariable* v) {
    bool found = false;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst || found) return;
        if (auto* st = dynamic_cast<StLoc*>(inst))
            if (st->Variable.get() == v) found = true;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return found;
}

} // namespace

TEST(CopyPropagation, DropsDeadStoreOfPureValueToStackSlot) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto S = MakeLocal("S_0");
    fn->Variables.push_back(S);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdcI4>(0)));  // dead (pure, no load)
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_TRUE(fn->Body->Blocks[0]->Instructions.empty()) << "dead pure store dropped";
}

TEST(CopyPropagation, PropagatesSingleDefStackSlotFromNeverAssignedParameter) {
    // stloc S_0(ldloc arg); ldloc S_0; ldloc S_0  -- S_0 is single-def, arg is a
    // never-assigned parameter. Both loads of S_0 become loads of arg; the
    // store is dropped.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto arg = MakeLocal("value", VariableKind::Parameter);
    auto S = MakeLocal("S_0", VariableKind::StackSlot);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(arg);
    fn->Variables.push_back(S);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdLoc>(arg)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));  // use 1
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));  // use 2
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // S_0 has no remaining loads (both became arg).
    EXPECT_EQ(CountLoads(*fn, S.get()), 0) << "S_0 loads propagated to arg";
    // The store of S_0 is dropped (S_0 no longer in the block).
    bool foundSStore = false;
    for (auto& inst : fn->Body->Blocks[0]->Instructions)
        if (auto* st = dynamic_cast<StLoc*>(inst.get()))
            if (st->Variable.get() == S.get()) foundSStore = true;
    EXPECT_FALSE(foundSStore) << "S_0 store dropped";
}

TEST(CopyPropagation, DoesNotPropagateWhenParameterIsAssigned) {
    // If the parameter is assigned (starg), copy propagation is unsafe -- skip.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto arg = MakeLocal("value", VariableKind::Parameter);
    auto S = MakeLocal("S_0", VariableKind::StackSlot);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(arg);
    fn->Variables.push_back(S);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdLoc>(arg)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(arg, std::make_unique<LdcI4>(1)));  // assign arg -> not single-def
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    ComputeVariableUsage(*fn);  // arg.StoreCount = 2 (param init + the stloc)
    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Not propagated: S_0 still has a load.
    EXPECT_EQ(CountLoads(*fn, S.get()), 1) << "S_0 not propagated (arg is assigned)";
}

TEST(CopyPropagation, PropagatesLdLocaSourceToEveryLoad) {
    // stloc S_0(ldloca src); ldloc S_0; ldloc S_0 -- S_0 is a single-def stack
    // slot holding a managed pointer (the materialised address of `src`).
    // Copy propagation replaces each `ldloc S_0` with a clone of `ldloca src`
    // (a different opcode, so the load is replaced, not re-pointed) and drops
    // the store. The C# uses a virtual Clone per load; this port's Clone (the
    // D147 foundation) is the machinery.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto src = MakeLocal("src", VariableKind::Local);
    auto S = MakeLocal("S_0", VariableKind::StackSlot);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(src);
    fn->Variables.push_back(S);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdLoca>(src)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));  // use 1
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));  // use 2
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // S_0 has no remaining ldloc loads (both became ldloca src clones).
    EXPECT_EQ(CountLoads(*fn, S.get()), 0) << "S_0 ldloc loads propagated to ldloca src";
    // Two ldloca src now (the clones), where the two ldloc S_0 were.
    EXPECT_EQ(CountAddressLoads(*fn, src.get()), 2) << "two ldloca src clones inserted";
    // The store of S_0 is dropped.
    EXPECT_FALSE(HasStoreOf(*fn, S.get())) << "S_0 store dropped";
}

TEST(CopyPropagation, PropagatesLdsFldaSourceToEveryLoad) {
    // stloc S_0(ldsflda field); ldloc S_0 -- a single-def stack slot holding a
    // static field address. Copy propagation replaces the load with a clone of
    // `ldsflda field` and drops the store.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto S = MakeLocal("S_0", VariableKind::StackSlot);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(S);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdsFlda>("Ns.T::field")));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(S)));  // use
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLoads(*fn, S.get()), 0) << "S_0 ldloc load propagated to ldsflda";
    EXPECT_FALSE(HasStoreOf(*fn, S.get())) << "S_0 store dropped";
    // A ldsflda clone now sits where the ldloc S_0 was (inside the dst store).
    bool foundLdsFlda = false;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* f = dynamic_cast<LdsFlda*>(inst))
            if (f->FieldName == "Ns.T::field") foundLdsFlda = true;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn->Body.get());
    EXPECT_TRUE(foundLdsFlda) << "ldsflda clone inserted";
}

TEST(CopyPropagation, DoesNotPropagateLdLocaWhenAddressTaken) {
    // If the stack slot's address is taken (ldloca S_0), S_0 is not single-
    // definition (AddressCount > 0) and copy propagation is unsafe -- skip.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto src = MakeLocal("src", VariableKind::Local);
    auto S = MakeLocal("S_0", VariableKind::StackSlot);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(src);
    fn->Variables.push_back(S);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S, std::make_unique<LdLoca>(src)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoca>(S)));  // takes S_0's address
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Not propagated: the ldloca S_0 is still there (S_0 not single-def).
    EXPECT_EQ(CountAddressLoads(*fn, S.get()), 1) << "S_0 address-taken, not propagated";
    EXPECT_TRUE(HasStoreOf(*fn, S.get())) << "S_0 store kept";
}

TEST(CopyPropagation, DoesNotPropagateLdLocaWhenTargetIsNotStackSlot) {
    // stloc localV(ldloca src); ldloc localV -- localV is a Local, not a
    // StackSlot. The C# propagates ldloca to any single-def target; this port
    // gates on StackSlot (the D142 simplification), so a Local target is skipped.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto src = MakeLocal("src", VariableKind::Local);
    auto localV = MakeLocal("localV", VariableKind::Local);
    auto dst = MakeLocal("dst", VariableKind::Local);
    fn->Variables.push_back(src);
    fn->Variables.push_back(localV);
    fn->Variables.push_back(dst);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(localV, std::make_unique<LdLoca>(src)));
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(dst, std::make_unique<LdLoc>(localV)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    CopyPropagation().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Not propagated (Local target, not StackSlot): the ldloc localV remains.
    EXPECT_EQ(CountLoads(*fn, localV.get()), 1) << "localV not propagated (not a stack slot)";
    EXPECT_TRUE(HasStoreOf(*fn, localV.get())) << "localV store kept";
}

TEST(CopyPropagation, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        CopyPropagation().Run(*fn, Ctx());
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
}
