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

// DetectCatchWhenConditionBlocks tests. A `catch (T e) when (cond)` filter
// starts with a redundant isinst type test (the catch is already typed T);
// this transform drops the test so the entry branches straight to the
// when-condition block, and (in the extra-store variant) copies the caught
// exception into the temp the when-condition block reads.
//
// This port's block model makes the IfInstruction the block's final with an
// implicit fall-through to the next block (the C# carries the if as a
// non-terminal with an explicit `br falseBlock`), so the `br falseBlock` is
// the positional fall-through to the next block, and the container entry
// edge is not counted (the entry has IncomingEdgeCount 0, not 1).

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
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

ILVariablePtr MakeStackSlot(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, nullptr, -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeCatchLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), -1);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A `catch (T ex) when (cond)` filter compiled by the C# compiler. The filter
// container holds four blocks:
//   entry:    [stloc temp(isinst T(ldloc ex))]  if (comp(ldloc temp != null)) br whenCond
//   falseBlock:  stloc ret(ldc.i4 0)            br exitBlock
//   exitBlock:   leave filterContainer(ldloc ret)        (2 preds: falseBlock + whenCond)
//   whenCond:    stloc ret(ldc.i4 1)            br exitBlock
// `extraStore` selects the 3-instruction variant (with the temp store) vs the
// 2-instruction variant (the isinst inlined into the comparison). The catch
// variable starts typed `catchVarType`; the isinst type is always Exception.
struct FilterFixture {
    std::unique_ptr<ILFunction> fn;
    BlockContainer* filter;
    Block* entry;
    Block* falseBlock;
    Block* exitBlock;
    Block* whenCond;
    ILVariablePtr ex;
    ILVariablePtr temp;
    ILVariablePtr ret;
};

FilterFixture BuildFilter(bool extraStore, ITypePtr catchVarType) {
    FilterFixture fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;

    auto exceptionType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    fx.ex = MakeCatchLocal("ex", catchVarType);
    fx.temp = MakeStackSlot("S_0");
    fx.ret = MakeStackSlot("S_1");
    fx.fn->Variables.push_back(fx.ex);
    fx.fn->Variables.push_back(fx.temp);
    fx.fn->Variables.push_back(fx.ret);

    // The filter container (the catch-when condition).
    auto filter = std::make_unique<BlockContainer>();
    fx.filter = filter.get();
    for (int i = 0; i < 4; ++i) filter->AddBlock(std::make_unique<Block>());
    fx.entry = filter->Blocks[0].get();
    fx.falseBlock = filter->Blocks[1].get();
    fx.exitBlock = filter->Blocks[2].get();
    fx.whenCond = filter->Blocks[3].get();

    // entry: [stloc temp(isinst Exception(ldloc ex))]  if (comp(... != null)) br whenCond
    if (extraStore) {
        auto isinst = std::make_unique<IsInst>(exceptionType, std::make_unique<LdLoc>(fx.ex));
        fx.entry->Add(std::make_unique<StLoc>(fx.temp, std::move(isinst)));
        auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(fx.temp),
                                           std::make_unique<LdNull>(),
                                           ComparisonKind::Inequality, false);
        fx.entry->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                            std::make_unique<Branch>(fx.whenCond)));
    } else {
        auto isinst = std::make_unique<IsInst>(exceptionType, std::make_unique<LdLoc>(fx.ex));
        auto cond = std::make_unique<Comp>(std::move(isinst),
                                           std::make_unique<LdNull>(),
                                           ComparisonKind::Inequality, false);
        fx.entry->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                            std::make_unique<Branch>(fx.whenCond)));
    }
    // falseBlock: stloc ret(ldc.i4 0); br exitBlock
    fx.falseBlock->Add(std::make_unique<StLoc>(fx.ret, std::make_unique<LdcI4>(0)));
    fx.falseBlock->SetFinal(std::make_unique<Branch>(fx.exitBlock));
    // exitBlock: leave filterContainer(ldloc ret)   (2 preds)
    fx.exitBlock->SetFinal(std::make_unique<Leave>(fx.filter, std::make_unique<LdLoc>(fx.ret)));
    // whenCond: stloc ret(ldc.i4 1); br exitBlock
    fx.whenCond->Add(std::make_unique<StLoc>(fx.ret, std::make_unique<LdcI4>(1)));
    fx.whenCond->SetFinal(std::make_unique<Branch>(fx.exitBlock));

    // Wrap the filter in a try/catch handler inside the function body so the
    // tree is well-formed and the handler is discoverable.
    auto tryBody = std::make_unique<BlockContainer>();
    tryBody->AddBlock(std::make_unique<Block>());
    tryBody->Blocks[0]->SetFinal(std::make_unique<Leave>(tryBody.get()));

    auto catchBody = std::make_unique<BlockContainer>();
    catchBody->AddBlock(std::make_unique<Block>());
    catchBody->Blocks[0]->SetFinal(std::make_unique<Leave>(catchBody.get()));

    auto handler = std::make_unique<TryCatchHandler>(std::move(filter),
                                                      std::move(catchBody), fx.ex);
    auto tryCatch = std::make_unique<TryCatch>(std::move(tryBody));
    tryCatch->AddHandler(std::move(handler));

    auto main = std::make_unique<Block>();
    main->Add(std::move(tryCatch));
    main->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.fn->Body->AddBlock(std::move(main));

    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

} // namespace

// 3-instruction variant: the entry's isinst type test is dropped, the temp
// store is rewritten to copy the caught exception, and the entry branches
// straight to the when-condition block. The catch variable is refined from
// Object to the isinst's type (Exception).
TEST(DetectCatchWhenConditionBlocks, ThreeInstructionVariantDropsTypeTest) {
    auto fx = BuildFilter(/*extraStore*/ true, std::make_shared<KnownType>(KnownTypeCode::Object));
    ASSERT_EQ(fx.entry->IncomingEdgeCount, 0) << "filter entry has no Branch predecessor";
    ASSERT_EQ(fx.falseBlock->IncomingEdgeCount, 1) << "reached only by the entry fall-through";
    ASSERT_EQ(fx.exitBlock->IncomingEdgeCount, 2) << "reached by falseBlock + whenCond";
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectCatchWhenConditionBlocks().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // The catch variable is refined to the isinst type.
    ASSERT_TRUE(fx.ex->Type);
    auto* kt = dynamic_cast<const KnownType*>(fx.ex->Type.get());
    ASSERT_NE(kt, nullptr);
    EXPECT_EQ(kt->Code(), KnownTypeCode::Exception);

    // entry: stloc temp(ldloc ex); br whenCond  (the isinst is gone).
    ASSERT_EQ(fx.entry->Instructions.size(), 1u);
    auto* stloc = dynamic_cast<StLoc*>(fx.entry->Instructions[0].get());
    ASSERT_NE(stloc, nullptr);
    EXPECT_EQ(stloc->Variable.get(), fx.temp.get());
    auto* val = dynamic_cast<LdLoc*>(stloc->Value.get());
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(val->Variable.get(), fx.ex.get());
    auto* br = dynamic_cast<Branch*>(fx.entry->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.whenCond);
    // falseBlock is now unreachable (its only predecessor was the entry's
    // fall-through, which the direct branch removed). Its dead `br exitBlock`
    // stays in the tree (erasing unreachable blocks is unsafe here -- see D58),
    // so exitBlock keeps both Branch predecessors in the count.
    EXPECT_EQ(fx.falseBlock->IncomingEdgeCount, 0);
    EXPECT_EQ(fx.exitBlock->IncomingEdgeCount, 2);
}

// 2-instruction variant: no temp store; the entry's if (with the inlined
// isinst) is replaced by a direct branch to the when-condition block.
TEST(DetectCatchWhenConditionBlocks, TwoInstructionVariantDropsTypeTest) {
    auto fx = BuildFilter(/*extraStore*/ false, std::make_shared<KnownType>(KnownTypeCode::Object));
    ASSERT_EQ(fx.entry->IncomingEdgeCount, 0);
    ASSERT_EQ(fx.exitBlock->IncomingEdgeCount, 2);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectCatchWhenConditionBlocks().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    ASSERT_TRUE(fx.ex->Type);
    auto* kt = dynamic_cast<const KnownType*>(fx.ex->Type.get());
    ASSERT_NE(kt, nullptr);
    EXPECT_EQ(kt->Code(), KnownTypeCode::Exception);

    // entry: br whenCond  (no instructions; the if with the isinst is gone).
    EXPECT_TRUE(fx.entry->Instructions.empty());
    auto* br = dynamic_cast<Branch*>(fx.entry->FinalInstruction.get());
    ASSERT_NE(br, nullptr);
    EXPECT_EQ(br->TargetBlock, fx.whenCond);
    EXPECT_EQ(fx.falseBlock->IncomingEdgeCount, 0);
    EXPECT_EQ(fx.exitBlock->IncomingEdgeCount, 2);
}

// A stack-type mismatch (catch variable typed int32, isinst typed Exception)
// must not fire: the catch variable keeps its type and the entry is untouched.
TEST(DetectCatchWhenConditionBlocks, SkipsOnStackTypeMismatch) {
    auto fx = BuildFilter(/*extraStore*/ true, std::make_shared<KnownType>(KnownTypeCode::Int32));
    ASSERT_EQ(fx.entry->IncomingEdgeCount, 0);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectCatchWhenConditionBlocks().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // Untouched: catch variable keeps Int32, entry still carries the isinst.
    ASSERT_TRUE(fx.ex->Type);
    auto* kt = dynamic_cast<const KnownType*>(fx.ex->Type.get());
    ASSERT_NE(kt, nullptr);
    EXPECT_EQ(kt->Code(), KnownTypeCode::Int32);
    ASSERT_EQ(fx.entry->Instructions.size(), 1u);
    auto* stloc = dynamic_cast<StLoc*>(fx.entry->Instructions[0].get());
    ASSERT_NE(stloc, nullptr);
    ASSERT_TRUE(dynamic_cast<IsInst*>(stloc->Value.get()) != nullptr)
        << "the isinst type test must still be present";
    ASSERT_TRUE(dynamic_cast<IfInstruction*>(fx.entry->FinalInstruction.get()) != nullptr);
}

// A falseBlock that does not match (extra instruction) must not fire.
TEST(DetectCatchWhenConditionBlocks, SkipsOnNonMatchingFalseBlock) {
    auto fx = BuildFilter(/*extraStore*/ true, std::make_shared<KnownType>(KnownTypeCode::Object));
    // Pollute falseBlock with an extra instruction so it no longer matches.
    fx.falseBlock->Add(std::make_unique<StLoc>(fx.ret, std::make_unique<LdcI4>(0)));
    fx.falseBlock->RenumberChildren();
    RecomputeIncomingEdgeCounts(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    DetectCatchWhenConditionBlocks().Run(*fx.fn, Ctx());
    fx.fn->CheckInvariant(ILPhase::Normal);

    // Untouched: the entry still carries the isinst type test.
    ASSERT_EQ(fx.entry->Instructions.size(), 1u);
    auto* stloc = dynamic_cast<StLoc*>(fx.entry->Instructions[0].get());
    ASSERT_NE(stloc, nullptr);
    ASSERT_TRUE(dynamic_cast<IsInst*>(stloc->Value.get()) != nullptr);
    ASSERT_TRUE(dynamic_cast<IfInstruction*>(fx.entry->FinalInstruction.get()) != nullptr);
}

// A plain catch (no filter container, or the constant ldc.i4(1) filter the
// BlockBuilder synthesises) must be left alone.
TEST(DetectCatchWhenConditionBlocks, LeavesPlainCatchAlone) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto ex = MakeCatchLocal("ex", std::make_shared<KnownType>(KnownTypeCode::Exception));
    fn->Variables.push_back(ex);

    auto tryBody = std::make_unique<BlockContainer>();
    tryBody->AddBlock(std::make_unique<Block>());
    tryBody->Blocks[0]->SetFinal(std::make_unique<Leave>(tryBody.get()));
    auto catchBody = std::make_unique<BlockContainer>();
    catchBody->AddBlock(std::make_unique<Block>());
    catchBody->Blocks[0]->SetFinal(std::make_unique<Leave>(catchBody.get()));
    // Plain catch: the constant ldc.i4(1) filter (the BlockBuilder's marker for
    // an unconditional catch).
    auto handler = std::make_unique<TryCatchHandler>(std::make_unique<LdcI4>(1),
                                                      std::move(catchBody), ex);
    auto tryCatch = std::make_unique<TryCatch>(std::move(tryBody));
    tryCatch->AddHandler(std::move(handler));
    auto main = std::make_unique<Block>();
    main->Add(std::move(tryCatch));
    main->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(main));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    DetectCatchWhenConditionBlocks().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The plain-catch handler is unchanged (the LdcI4(1) filter is not a
    // BlockContainer, so the transform never matched).
    bool foundHandler = false;
    Walk(fn->Body.get(), [&](ILInstruction* i) {
        if (i->Op == OpCode::TryCatchHandler) {
            foundHandler = true;
            auto* h = static_cast<TryCatchHandler*>(i);
            EXPECT_TRUE(h->Filter != nullptr);
            EXPECT_EQ(h->Filter->Op, OpCode::LdcI4);
        }
    });
    EXPECT_TRUE(foundHandler);
}

// On the real mscorlib corpus the transform must not violate the invariant.
// Whether it fires depends on the reader producing the exact catch-when
// filter shape; the sweep's primary check is invariant preservation.
TEST(DetectCatchWhenConditionBlocks, MscorlibSweepPreservesInvariant) {
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
        // Run the pre-pipeline the CLI uses up to DetectPinnedRegions so the
        // catch-when filter shape (if any) is in its post-inlining form.
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // mscorlib carries no `catch ... when` (filter) handlers in the scanned
    // methods, so the transform does not fire here -- the sweep's check is that
    // the invariant holds across the corpus (the hand-built tests above verify
    // the rewrite itself).
}
