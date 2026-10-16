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

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

TS::TestSupport::LookupCompilation g_analyzeCompilation;

// The AnalyzeFunction walk on a display-class-shaped local: the container
// local is stored once from a `newobj Closure..ctor()`, followed by the
// field-init stores. The C# AnalyzeFunction detects the display class and
// records a VariableToDeclare per field store.
TEST(TransformDisplayClassUsageAnalyzeTest, AnalyzeDetectsDisplayClassFromNewObjStore)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto containerType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("Test"), std::string("Closure")));
    ILVariablePtr container = fn->RegisterVariable(
        IL::VariableKind::Local, containerType, std::string("closure"));
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto fieldX =
        std::make_shared<TS::TestSupport::LookupField>("x", intType, g_analyzeCompilation);
    auto fieldY =
        std::make_shared<TS::TestSupport::LookupField>("y", intType, g_analyzeCompilation);

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    // stloc closure(newobj Closure..ctor()); stobj(ldsflda closure.x, ...);
    // stobj(ldsflda closure.y, ...)
    auto ctor = std::make_unique<IL::Call>("Test.Closure::.ctor");
    ctor->IsNewObj = true;
    auto stloc = std::make_unique<IL::StLoc>(container, std::move(ctor));
    IL::StLoc* store = stloc.get();
    blockPtr->Add(std::move(stloc));
    {
        auto ldflda = std::make_unique<IL::LdsFlda>(
            "Test.Closure::x");
        ldflda->FieldName = "Test.Closure::x";
        ldflda->IsCompilerGeneratedField = true;
        auto stobj = std::make_unique<IL::StObj>(
            std::move(ldflda), std::make_unique<IL::LdLoc>(container),
            containerType);
        IL::StObj* initX = stobj.get();
        blockPtr->Add(std::move(stobj));
        (void)initX;
    }
    {
        auto ldflda = std::make_unique<IL::LdsFlda>("Test.Closure::y");
        ldflda->FieldName = "Test.Closure::y";
        ldflda->IsCompilerGeneratedField = true;
        blockPtr->Add(std::make_unique<IL::StObj>(
            std::move(ldflda), std::make_unique<IL::LdLoc>(container),
            containerType));
    }
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(container));

    IL::RecomputeIncomingEdgeCounts(*fn);
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.Settings.AggressiveScalarReplacementOfAggregates = true;
    std::map<IL::ILVariable*, std::shared_ptr<IL::TransformDisplayClassUsage::
                                           DisplayClass>>
        displayClasses;
    IL::TransformDisplayClassUsage::AnalyzeFunctionForTests(*fn, ctx, displayClasses);

    ASSERT_EQ(displayClasses.size(), 1u)
        << "the display class is detected from the newobj store";
    auto it = displayClasses.find(container.get());
    ASSERT_TRUE(it != displayClasses.end());
    EXPECT_EQ(it->second->Variable, container.get());
    EXPECT_EQ(it->second->Type, nullptr);
}

} // namespace
