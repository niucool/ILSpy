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

// StObjToStLoc tests. `stobj(ldloca V, value)` (which the seed renders as
// `*(&V) = value`) becomes `stloc V, value` (`V = value`), the shape ILInlining
// and the rest of the pipeline expect. Stores to non-local addresses (fields,
// array elements, byrefs) are left alone.

#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(StObjToStLoc, ConvertsStoreToLocalAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0");
    fn->Variables.push_back(V);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StObj>(
        std::make_unique<LdLoca>(V), std::make_unique<LdcI4>(1), nullptr));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    StObjToStLoc().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr) << "stobj(ldloca V, ..) becomes stloc V, ..";
    EXPECT_EQ(st->Variable.get(), V.get());
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::LdcI4);
}

TEST(StObjToStLoc, LeavesStoreToFieldAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StObj>(
        std::make_unique<LdFlda>(std::make_unique<LdLoc>(nullptr), "NS.T::field"),
        std::make_unique<LdcI4>(1), nullptr));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    StObjToStLoc().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // A store to a field address is not a local store -- left as stobj.
    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions[0]->Op, OpCode::StObj);
}

TEST(StObjToStLoc, MscorlibSweepConvertsSomeStObj) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int stobjBefore = 0, stobjAfter = 0, processed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        std::function<void(ILInstruction*)> count = [&](ILInstruction* inst) {
            if (!inst) return;
            if (inst->Op == OpCode::StObj) ++stobjBefore;
            for (int i = 0; i < inst->ChildCount(); ++i) count(inst->GetChild(i));
        };
        count(fn->Body.get());
        StObjToStLoc().Run(*fn, Ctx());
        std::function<void(ILInstruction*)> count2 = [&](ILInstruction* inst) {
            if (!inst) return;
            if (inst->Op == OpCode::StObj) ++stobjAfter;
            for (int i = 0; i < inst->ChildCount(); ++i) count2(inst->GetChild(i));
        };
        count2(fn->Body.get());
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    EXPECT_LT(stobjAfter, stobjBefore) << "some stobj(ldloca) should become stloc";
}
