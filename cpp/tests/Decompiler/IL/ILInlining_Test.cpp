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

// ILInlining tests: single-use variable inlining (the third transform in
// GetILTransforms). A StLoc whose variable is stored once and loaded/addressed
// once inlines its value into the load site; dead stores of pure expressions
// vanish. Hand-built trees pin the shapes; the mscorlib sweep pins the global
// contract (invariant + variable-count drop).

#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeVar(VariableKind kind, std::string name, std::int32_t index) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = kind;
    v->Index = index;
    return v;
}

std::unique_ptr<ILFunction> WrapBlocks(std::vector<std::unique_ptr<Block>> blocks) {
    auto container = std::make_unique<BlockContainer>();
    for (auto& b : blocks) container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(ILInlining, InlinesSingleUseLocalIntoLeave) {
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(42)));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(local)));
    fn->Variables.push_back(local);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    EXPECT_TRUE(only->Instructions.empty()) << "stloc must be removed";
    auto* leave = dynamic_cast<Leave*>(only->FinalInstruction.get());
    ASSERT_NE(leave, nullptr);
    ASSERT_NE(leave->Value, nullptr);
    EXPECT_EQ(leave->Value->Op, OpCode::LdcI4) << "ldloc inlined to ldc.i4";
}

TEST(ILInlining, InlinesStackSlotIntoCall) {
    auto slot = MakeVar(VariableKind::StackSlot, "S_0", -1);
    auto arg = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->AddArg(std::make_unique<LdLoc>(slot));
    call->AddArg(std::make_unique<LdcI4>(1));
    call->ReturnType = StackType::I4;
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(slot, std::make_unique<LdLoc>(arg)));
    block->Add(std::move(call));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(slot);
    fn->Variables.push_back(arg);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    ASSERT_EQ(only->Instructions.size(), 1u) << "stloc removed, call remains";
    auto* c = dynamic_cast<Call*>(only->Instructions[0].get());
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->Arguments.size(), 2u);
    EXPECT_EQ(c->Arguments[0]->Op, OpCode::LdLoc);
    auto* ld = dynamic_cast<LdLoc*>(c->Arguments[0].get());
    ASSERT_NE(ld, nullptr);
    EXPECT_EQ(ld->Variable->Name, "arg_1") << "S_0 inlined to its source";
}

TEST(ILInlining, RemovesDeadPureStackSlotStore) {
    auto slot = MakeVar(VariableKind::StackSlot, "S_0", -1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(slot, std::make_unique<LdcI4>(99)));
    block->Add(std::make_unique<LdcI4>(0));  // a stray constant, never loaded
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(slot);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    // The dead stloc (pure ldc.i4) is removed; the stray constant stays
    // (it's not a StLoc so ILInlining doesn't touch it).
    EXPECT_EQ(only->Instructions.size(), 1u);
}

TEST(ILInlining, DoesNotInlineMultiUseVariable) {
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto other = MakeVar(VariableKind::Local, "V_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(1)));
    block->Add(std::make_unique<StLoc>(other, std::make_unique<LdLoc>(local)));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(local)));
    fn->Variables.push_back(local);
    fn->Variables.push_back(other);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // V_0 has loadCount 2 (into V_1 store + leave) -> not single-use, not inlined.
    // V_1 is a dead store (never loaded) -> removed as dead.
    const auto& only = fn->Body->Blocks[0];
    EXPECT_EQ(only->Instructions.size(), 1u) << "V_0 survives (multi-use), V_1 dead store removed";
    auto* st = dynamic_cast<StLoc*>(only->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable->Name, "V_0");
}

TEST(ILInlining, InliningOnMscorlibReducesVariableCount) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int totalVarsBefore = 0;
    int totalVarsAfter = 0;
    int transformed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        totalVarsBefore += static_cast<int>(fn->Variables.size());
        ILInlining().Run(*fn, Ctx());
        fn->CheckInvariant(ILPhase::Normal);
        totalVarsAfter += static_cast<int>(fn->Variables.size());
        ++transformed;
        if (transformed >= 8000) break;
    }
    EXPECT_GT(transformed, 5000);
    EXPECT_LT(totalVarsAfter, totalVarsBefore)
        << "inlining must reduce the total variable count";
}
