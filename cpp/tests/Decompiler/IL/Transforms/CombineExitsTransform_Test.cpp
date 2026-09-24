// Tests for CombineExitsTransform (the C# CombineExitsTransform.cs, the
// nested-pipeline tail DelegateConstruction runs after the lambda body's
// transforms): the `if (cond) { leave(value); } leave(elseValue);` shape
// folds to `leave(if (cond) value else elseValue)` when both exits leave
// the function with non-nop values.

#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;

// A minimal known type for the test locals (the object stand-in: the
// variables' Type only feeds WriteTo/Invariant here).
struct CombineExitsFixture {
    IL::ILFunction fn;
    IL::BlockContainer* body = nullptr;
    IL::ILVariablePtr condVar;
    IL::Block* entry = nullptr;

    CombineExitsFixture()
    {
        auto container = std::make_unique<IL::BlockContainer>();
        body = container.get();
        auto entryBlock = std::make_unique<IL::Block>();
        entry = entryBlock.get();
        container->AddBlock(std::move(entryBlock));
        fn.Body = std::move(container);
        fn.Body->Parent = &fn;
        fn.Body->ChildIndex = 0;
        condVar = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local, nullptr, 0);
    }

    void Finish() { fn.CheckInvariant(IL::ILPhase::InILReader); }
};

// if (cond) { leave(ldloc v1); }  leave(ldc.i4 2)
// folds to: leave(if (cond) ldloc v1 else ldc.i4 2)
TEST(CombineExitsTransform, FoldsIfThenLeaveAndTrailingLeave)
{
    CombineExitsFixture fx;
    auto v1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                               nullptr, 1);

    // The true branch: a block whose single instruction is the leave.
    auto trueBlock = std::make_unique<IL::Block>();
    trueBlock->Add(std::make_unique<IL::Leave>(fx.body,
                                               std::make_unique<IL::LdLoc>(v1)));
    auto condition = std::make_unique<IL::LdLoc>(fx.condVar);
    auto ifInst = std::make_unique<IL::IfInstruction>(
        std::move(condition), std::move(trueBlock), nullptr);
    IL::IfInstruction* ifPtr = ifInst.get();
    auto elseLeave = std::make_unique<IL::Leave>(
        fx.body, std::make_unique<IL::LdcI4>(2));
    IL::Leave* elseLeavePtr = elseLeave.get();

    fx.entry->Add(std::move(ifInst));
    fx.entry->SetFinal(std::move(elseLeave));
    fx.Finish();

    IL::ILTransformContext ctx;
    IL::CombineExitsTransform().Run(fx.fn, ctx);

    // The if is replaced by a leave carrying the combined conditional value;
    // the old final leave's slot is gone (the combined leave now closes the
    // block, and the port leaves the final slot empty -- the leave itself is
    // the terminator).
    ASSERT_EQ(fx.entry->Instructions.size(), 1u);
    ASSERT_EQ(fx.entry->Instructions[0]->Op, IL::OpCode::Leave);
    auto* combined = static_cast<IL::Leave*>(fx.entry->Instructions[0].get());
    ASSERT_EQ(combined->TargetContainer, fx.body);
    ASSERT_NE(combined->Value, nullptr);
    EXPECT_EQ(combined->Value->Op, IL::OpCode::IfInstruction);
    auto* combinedIf = static_cast<IL::IfInstruction*>(combined->Value.get());
    EXPECT_EQ(combinedIf->TrueInst->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(combinedIf->FalseInst->Op, IL::OpCode::LdcI4);
    (void)ifPtr;
    (void)elseLeavePtr;
    fx.fn.CheckInvariant(IL::ILPhase::Normal);
}

// The transform requires both leaves to carry values (the C#
// `leave.Value.MatchNop() || leaveElse.Value.MatchNop()` rejection).
TEST(CombineExitsTransform, RejectsNopLeaveValues)
{
    CombineExitsFixture fx;

    auto trueBlock = std::make_unique<IL::Block>();
    trueBlock->Add(std::make_unique<IL::Leave>(fx.body, nullptr));
    auto ifInst = std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdLoc>(fx.condVar), std::move(trueBlock),
        nullptr);
    auto elseLeave =
        std::make_unique<IL::Leave>(fx.body, nullptr);
    fx.entry->Add(std::move(ifInst));
    fx.entry->SetFinal(std::move(elseLeave));
    fx.Finish();

    IL::ILTransformContext ctx;
    IL::CombineExitsTransform().Run(fx.fn, ctx);

    // Nothing folded: the if and the trailing leave stay.
    EXPECT_EQ(fx.entry->Instructions.size(), 1u);
    EXPECT_EQ(fx.entry->FinalInstruction->Op, IL::OpCode::Leave);
    EXPECT_EQ(static_cast<IL::Leave*>(fx.entry->FinalInstruction.get())->Value,
              nullptr);
    (void)0;
}

} // namespace
