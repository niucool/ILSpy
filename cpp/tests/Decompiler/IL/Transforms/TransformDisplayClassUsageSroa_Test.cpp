// Copyright (c) 2026 Jim Hester
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The SROA rewrite visitor (the C# `void Transform(ILFunction)` in
// TransformDisplayClassUsage.cs): the analysis phase detects the display
// class; the transform removes the container store, rewrites each
// `ldflda(ldloc container, field)` into `ldloca declaredVariable`, and lets
// the stobj stores fold into `stloc declaredVariable(value)`.

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// The class-shape SROA: `stloc closure(newobj Closure..ctor())` followed by
// the field-init stores `stobj(ldflda(ldloc closure, f), value)` in the same
// block and one field read. The transform removes the container store and
// rewrites every field access into an access of a freshly declared local, so
// the stores fold into `stloc xVar(...)`.
TEST(TransformDisplayClassUsageSroaTest, TransformInlinesFieldInitStoresIntoDeclaredLocals)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto containerType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("Test"), std::string("Closure")));
    ILVariablePtr container = fn->RegisterVariable(
        IL::VariableKind::Local, containerType, std::string("closure"));
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    auto ctor = std::make_unique<IL::Call>("Test.Closure::.ctor");
    ctor->IsNewObj = true;
    auto stloc = std::make_unique<IL::StLoc>(container, std::move(ctor));
    IL::StLoc* containerStore = stloc.get();
    blockPtr->Add(std::move(stloc));
    for (const char* fieldName : {"x", "y"}) {
        auto ldflda = std::make_unique<IL::LdFlda>(
            std::make_unique<IL::LdLoc>(container),
            std::string("Test.Closure::") + fieldName);
        int value = fieldName[0] == 'x' ? 1 : 2;
        blockPtr->Add(std::make_unique<IL::StObj>(
            std::move(ldflda), std::make_unique<IL::LdcI4>(value),
            containerType));
    }
    auto readFlda = std::make_unique<IL::LdFlda>(
        std::make_unique<IL::LdLoc>(container), std::string("Test.Closure::x"));
    blockPtr->SetFinal(std::make_unique<IL::LdObj>(std::move(readFlda),
                                                   containerType));

    IL::RecomputeIncomingEdgeCounts(*fn);
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.Settings.AggressiveScalarReplacementOfAggregates = true;
    IL::TransformDisplayClassUsage::AnalysisState state;
    IL::TransformDisplayClassUsage::AnalyzeFunction(*fn, ctx, nullptr, state);

    ASSERT_EQ(state.displayClasses.size(), 1u)
        << "the display class is detected from the newobj store";
    auto dcIt = state.displayClasses.find(container.get());
    ASSERT_TRUE(dcIt != state.displayClasses.end());
    EXPECT_EQ(dcIt->second->Initializer, containerStore)
        << "the analysis records the initializer store";
    ASSERT_EQ(dcIt->second->VariablesToDeclare.size(), 2u)
        << "both field stores are recorded as variables to declare";

    IL::TransformDisplayClassUsage::Transform(*fn, ctx, state);

    ASSERT_EQ(blockPtr->Instructions.size(), 2u)
        << "the container store and both stobj stores are replaced";
    auto* first = dynamic_cast<IL::StLoc*>(blockPtr->Instructions[0].get());
    ASSERT_NE(first, nullptr) << "the first store targets the x variable";
    auto* second = dynamic_cast<IL::StLoc*>(blockPtr->Instructions[1].get());
    ASSERT_NE(second, nullptr) << "the second store targets the y variable";
    EXPECT_EQ(first->Variable->Name, "x");
    EXPECT_EQ(first->Variable->Kind, IL::VariableKind::Local);
    EXPECT_EQ(second->Variable->Name, "y");
    // The container local is fully gone: no instruction in the tree
    // references it (the port's StoreCount/LoadCount counters are snapshots
    // from ComputeVariableUsage, so the check walks the instruction tree).
    std::function<void(IL::ILInstruction*)> assertNoContainerUse =
        [&](IL::ILInstruction* inst) {
            ASSERT_NE(inst, nullptr);
            if (auto* stloc = dynamic_cast<IL::StLoc*>(inst)) {
                ASSERT_NE(stloc->Variable.get(), container.get())
                    << "a store to the closure local survived the rewrite";
            }
            if (auto* ldloc = dynamic_cast<IL::LdLoc*>(inst)) {
                ASSERT_NE(ldloc->Variable.get(), container.get())
                    << "a load of the closure local survived the rewrite";
            }
            if (auto* ldloca = dynamic_cast<IL::LdLoca*>(inst)) {
                ASSERT_NE(ldloca->Variable.get(), container.get())
                    << "an address of the closure local survived the rewrite";
            }
            for (int i = 0; i < inst->ChildCount(); i++) {
                assertNoContainerUse(inst->GetChild(i));
            }
        };
    assertNoContainerUse(fn.get());
    // The final read became ldloc of the x variable.
    auto* finalLoad = dynamic_cast<IL::LdLoc*>(blockPtr->FinalInstruction.get());
    ASSERT_NE(finalLoad, nullptr) << "the field read becomes a plain ldloc";
    EXPECT_EQ(finalLoad->Variable.get(), first->Variable.get());
}

} // namespace
