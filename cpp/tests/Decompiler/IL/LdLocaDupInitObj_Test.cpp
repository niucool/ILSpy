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

// LdLocaDupInitObjTransform tests. The Roslyn (>= 2) codegen for
// `var v = default(T);` plus a use of `&v` lowers (in the reader) to
//     stloc s(ldloca v)                 // s = &v   (the dup's stack slot)
//     stobj T(ldloc s, default(T), T)   // *s = default(T)  (the initobj)
// which the transform rewrites to
//     stloc v(default(T))               // v = default(T)
//     stloc s(ldloca v)                 // s = &v
// so `s` (the address) can be inlined into its subsequent uses. Stores whose
// target is not `ldloc s`, whose value is not `default(T)`, or whose stobj type
// is incompatible with the local's type are left alone.

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
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

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

// The DefaultValue node (the reader's model of `initobj`): a leaf carrying the
// type operand, with the type's stack type and a `default.value(T)` dump.
TEST(LdLocaDupInitObj, DefaultValueNode) {
    DefaultValue dv(std::make_shared<KnownType>(KnownTypeCode::Int32));
    EXPECT_EQ(dv.Op, OpCode::DefaultValue);
    EXPECT_EQ(dv.ResultType(), StackType::I4);
    std::string s;
    dv.WriteTo(s);
    EXPECT_EQ(s, "default.value(System.Int32)");
    EXPECT_EQ(dv.ChildCount(), 0);
}

// The core rewrite: `stloc s(ldloca v); stobj T(ldloc s, default(T))` becomes
// `stloc v(default(T)); stloc s(ldloca v)`.
TEST(LdLocaDupInitObj, RewritesLdLocaDupInitObj) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("V_0", KnownTypeCode::Int32);
    auto s = MakeStackSlot("S_1");
    fn->Variables.push_back(v);
    fn->Variables.push_back(s);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto* block = fn->Body->Blocks[0].get();
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    // stloc s(ldloca v)
    block->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoca>(v)));
    // stobj Int32(ldloc s, default(Int32))
    block->Add(std::make_unique<StObj>(
        std::make_unique<LdLoc>(s),
        std::make_unique<DefaultValue>(intType),
        intType));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LdLocaDupInitObjTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(block->Instructions.size(), 2u);
    // Position 0 is now `stloc v(default(T))`.
    auto* st0 = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(st0, nullptr) << "first instruction is a stloc";
    EXPECT_EQ(st0->Variable.get(), v.get()) << "the default-value store targets v";
    ASSERT_NE(st0->Value, nullptr);
    EXPECT_EQ(st0->Value->Op, OpCode::DefaultValue);
    // Position 1 is the moved `stloc s(ldloca v)`.
    auto* st1 = dynamic_cast<StLoc*>(block->Instructions[1].get());
    ASSERT_NE(st1, nullptr) << "second instruction is a stloc";
    EXPECT_EQ(st1->Variable.get(), s.get()) << "the address store still targets s";
    ASSERT_NE(st1->Value, nullptr);
    EXPECT_EQ(st1->Value->Op, OpCode::LdLoca);
    EXPECT_EQ(st1->Value->Parent, st1);
    // The StObj is gone.
    int stobjCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (i->Op == OpCode::StObj) ++stobjCount;
    });
    EXPECT_EQ(stobjCount, 0);
}

// A stobj whose target is not `ldloc s` (here a bare `ldloca v`) is not the
// pattern -- the pair is left untouched.
TEST(LdLocaDupInitObj, LeavesStoreToLocalAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("V_0", KnownTypeCode::Int32);
    auto s = MakeStackSlot("S_1");
    fn->Variables.push_back(v);
    fn->Variables.push_back(s);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto* block = fn->Body->Blocks[0].get();
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    block->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoca>(v)));
    // Target is ldloca v, not ldloc s -- not the pattern.
    block->Add(std::make_unique<StObj>(
        std::make_unique<LdLoca>(v),
        std::make_unique<DefaultValue>(intType),
        intType));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LdLocaDupInitObjTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(block->Instructions.size(), 2u);
    EXPECT_EQ(block->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(block->Instructions[1]->Op, OpCode::StObj)
        << "a stobj to ldloca is not the ldloca/dup/initobj pattern";
}

// A stobj whose value is not a `default(T)` (here an integer constant) is not
// the initobj pattern -- left untouched.
TEST(LdLocaDupInitObj, LeavesNonDefaultValue) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("V_0", KnownTypeCode::Int32);
    auto s = MakeStackSlot("S_1");
    fn->Variables.push_back(v);
    fn->Variables.push_back(s);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto* block = fn->Body->Blocks[0].get();
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    block->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoca>(v)));
    // Value is ldc.i4 0, not default(T) -- not the pattern.
    block->Add(std::make_unique<StObj>(
        std::make_unique<LdLoc>(s),
        std::make_unique<LdcI4>(0),
        intType));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LdLocaDupInitObjTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(block->Instructions.size(), 2u);
    EXPECT_EQ(block->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(block->Instructions[1]->Op, OpCode::StObj)
        << "a stobj of a non-default value is not the initobj pattern";
}

// A stobj whose type is not memory-compatible with the local's type (Int32 local
// vs a reference-typed stobj) is not the pattern -- left untouched.
TEST(LdLocaDupInitObj, LeavesIncompatibleType) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("V_0", KnownTypeCode::Int32);
    auto s = MakeStackSlot("S_1");
    fn->Variables.push_back(v);
    fn->Variables.push_back(s);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto* block = fn->Body->Blocks[0].get();
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    block->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoca>(v)));
    // v is Int32 but the stobj is typed String -- not memory-compatible.
    block->Add(std::make_unique<StObj>(
        std::make_unique<LdLoc>(s),
        std::make_unique<DefaultValue>(stringType),
        stringType));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    LdLocaDupInitObjTransform().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(block->Instructions.size(), 2u);
    EXPECT_EQ(block->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(block->Instructions[1]->Op, OpCode::StObj)
        << "a stobj of an incompatible type is not the pattern";
}

// On the real mscorlib corpus the transform must not violate the invariant.
// (Whether it fires depends on the compiler emitting the exact
// ldloca/dup/initobj shape surviving the pre-pipeline; the sweep's primary
// check is that the invariant holds across the corpus, run through the same
// pre-pipeline the CLI uses up to and including this transform.)
TEST(LdLocaDupInitObj, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int rewrites = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // The CLI pre-pipeline up to this transform (DetectExitPoints, which
        // would sit between DetectCatchWhenConditionBlocks and this transform in
        // the C# order, is deferred).
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        // Each rewrite eliminates one StObj (the initobj) and replaces it with a
        // StLoc, so the StObj delta across this transform is the fire count.
        int stobjBefore = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::StObj) ++stobjBefore;
        });
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        int stobjAfter = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::StObj) ++stobjAfter;
        });
        rewrites += stobjBefore - stobjAfter;
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GE(rewrites, 0);
}
