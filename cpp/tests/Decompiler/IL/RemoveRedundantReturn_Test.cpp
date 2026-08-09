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

// RemoveRedundantReturn tests. A void function's trailing `return;` (a
// value-less Leave of the body container on the last block) is made implicit
// (the final is dropped, the block falls through). Value returns are kept.

#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(RemoveRedundantReturn, DropsTrailingVoidReturn) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(nullptr, std::make_unique<LdcI4>(1)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // void return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // The trailing void return is dropped (final is null -> implicit return).
    EXPECT_EQ(fn->Body->Blocks[0]->FinalInstruction, nullptr);
}

TEST(RemoveRedundantReturn, KeepsValueReturn) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(1)));  // value return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // A value return is not redundant -- it stays.
    ASSERT_NE(fn->Body->Blocks[0]->FinalInstruction, nullptr);
    EXPECT_EQ(fn->Body->Blocks[0]->FinalInstruction->Op, OpCode::Leave);
}

TEST(RemoveRedundantReturn, KeepsReturnBeforeLastBlock) {
    // A void return on a non-last block is NOT redundant (control flow reaches
    // it conditionally); only the last block's trailing return is dropped.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // void return (not last)
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Leave>(fn->Body.get()));  // last block void return
    fn->CheckInvariant(ILPhase::Normal);

    RemoveRedundantReturn().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // Only the last block's return is dropped.
    EXPECT_NE(fn->Body->Blocks[0]->FinalInstruction, nullptr) << "non-last return kept";
    EXPECT_EQ(fn->Body->Blocks[1]->FinalInstruction, nullptr) << "last return dropped";
}

TEST(RemoveRedundantReturn, MscorlibSweepDropsSomeTrailingReturns) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int dropped = 0;
    int processed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Count last-block void returns before.
        if (!fn->Body || fn->Body->Blocks.empty()) continue;
        auto& last = fn->Body->Blocks.back();
        auto* leave = dynamic_cast<Leave*>(last->FinalInstruction.get());
        bool hadTrailingVoid = leave && leave->TargetContainer == fn->Body.get() && !leave->Value;
        RemoveRedundantReturn().Run(*fn, Ctx());
        if (hadTrailingVoid && last->FinalInstruction == nullptr) ++dropped;
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    EXPECT_GT(dropped, 0) << "some trailing void return should be dropped";
}
