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

#include <gtest/gtest.h>

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace Util = ::ILSpy::Decompiler::Util;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLenCharLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// The Roslyn length+char switch (roslyn#66081) for single-character strings:
//   head:      if (comp(ldloc s == ldnull)) br nullCase; br lenBlock
//   lenBlock:  switch(call String::get_Length(ldloc s)): [1]->charBlockA,
//              [2]->charBlockB, default->defaultBlock
//   charBlockA: switch(call String::get_Chars(ldloc s, 0)): ['a']->bodyA,
//              ['b']->bodyB, default->defaultBlock
//   charBlockB: switch(call get_Chars(ldloc s, 0)): ['c']->bodyC,
//              default->defaultBlock
// The transform folds the whole shape into a switch over a StringToInt of
// the switch-value variable, with the null case as its own section.
TEST(SwitchOnStringLengthCharTest, LengthAndCharSwitchFolds)
{
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto s = MakeLenCharLocal("s", stringType);

    auto fn = std::make_unique<IL::ILFunction>();
    auto container = std::make_unique<IL::BlockContainer>();
    fn->Variables.push_back(s);

    auto nullCase = std::make_unique<IL::Block>();
    nullCase->Kind = IL::BlockKind::ControlFlow;
    IL::Block* nullCasePtr = nullCase.get();
    nullCase->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(nullCase));
    auto exit = std::make_unique<IL::Block>();
    exit->Kind = IL::BlockKind::ControlFlow;
    exit->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(exit));
    auto bodyA = std::make_unique<IL::Block>();
    bodyA->Kind = IL::BlockKind::ControlFlow;
    IL::Block* bodyAPtr = bodyA.get();
    bodyA->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(bodyA));
    auto bodyB = std::make_unique<IL::Block>();
    bodyB->Kind = IL::BlockKind::ControlFlow;
    IL::Block* bodyBPtr = bodyB.get();
    bodyB->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(bodyB));
    auto defaultBlock = std::make_unique<IL::Block>();
    defaultBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* defaultBlockPtr = defaultBlock.get();
    defaultBlock->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(defaultBlock));
    auto charBlockA = std::make_unique<IL::Block>();
    charBlockA->Kind = IL::BlockKind::ControlFlow;
    IL::Block* charBlockAPtr = charBlockA.get();
    container->Blocks.push_back(std::move(charBlockA));
    auto lenBlock = std::make_unique<IL::Block>();
    lenBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* lenBlockPtr = lenBlock.get();
    container->Blocks.push_back(std::move(lenBlock));
    auto head = std::make_unique<IL::Block>();
    head->Kind = IL::BlockKind::ControlFlow;
    IL::Block* headPtr = head.get();
    container->Blocks.push_back(std::move(head));

    // Reorder the container so the block walk visits the head before the
    // length switch (the C# Descendants order follows the IL layout, whose
    // head precedes the dispatch blocks).
    auto& blocks = container->Blocks;
    std::rotate(blocks.begin(), blocks.begin() + 7, blocks.begin() + 8);
    std::rotate(blocks.begin() + 1, blocks.begin() + 8, blocks.end());
    // The head: the null check + the jump into the length switch.
    {
        auto eq = std::make_unique<IL::Comp>(
            std::make_unique<IL::LdLoc>(s), std::make_unique<IL::LdNull>(),
            IL::ComparisonKind::Equality, false);
        headPtr->Add(std::make_unique<IL::IfInstruction>(
            std::move(eq), std::make_unique<IL::Branch>(nullCasePtr)));
        headPtr->Add(std::make_unique<IL::Branch>(lenBlockPtr));
    }

    // The length switch: get_Length(ldloc s) with the two length cases.
    {
        auto getLength = std::make_unique<IL::Call>("System.String::get_Length");
        getLength->DeclaringType = stringType;
        getLength->AddArg(std::make_unique<IL::LdLoc>(s));
        auto sw = std::make_unique<IL::SwitchInstruction>(std::move(getLength));
        auto addSection = [&](long long label, IL::Block* target) {
            auto section = std::make_unique<IL::SwitchSection>(
                Util::LongSet(Util::LongInterval(label, label + 1)));
            section->SetBody(std::make_unique<IL::Branch>(target));
            sw->Sections.push_back(std::move(section));
        };
        addSection(1, charBlockAPtr);
        auto defaultSection = std::make_unique<IL::SwitchSection>(
            Util::LongSet(Util::LongInterval(0, 2)).Invert());
        defaultSection->SetBody(std::make_unique<IL::Branch>(defaultBlockPtr));
        sw->Sections.push_back(std::move(defaultSection));
        lenBlockPtr->Add(std::move(sw));
    }

    // The char switches: get_Chars(ldloc s, 0) over the case bodies.
    auto makeCharSwitch = [&](IL::Block* block, char first, char second,
                              bool hasSecond) {
        auto getChars = std::make_unique<IL::Call>("System.String::get_Chars");
        getChars->DeclaringType = stringType;
        getChars->AddArg(std::make_unique<IL::LdLoc>(s));
        getChars->AddArg(std::make_unique<IL::LdcI4>(0));
        auto sw = std::make_unique<IL::SwitchInstruction>(std::move(getChars));
        auto addSection = [&](long long label, IL::Block* target) {
            auto section = std::make_unique<IL::SwitchSection>(
                Util::LongSet(Util::LongInterval(label, label + 1)));
            section->SetBody(std::make_unique<IL::Branch>(target));
            sw->Sections.push_back(std::move(section));
        };
        addSection(static_cast<long long>(first), bodyAPtr);
        addSection(static_cast<long long>(second), bodyBPtr);
        long long upper = hasSecond ? 256 : static_cast<long long>(first) + 1;
        auto defaultSection = std::make_unique<IL::SwitchSection>(
            Util::LongSet(Util::LongInterval(0, upper)).Invert());
        defaultSection->SetBody(std::make_unique<IL::Branch>(defaultBlockPtr));
        sw->Sections.push_back(std::move(defaultSection));
        block->Add(std::move(sw));
    };
    (void)exit;
    makeCharSwitch(charBlockAPtr, 'a', 'b', true);

    fn->Body = std::move(container);
    IL::RecomputeIncomingEdgeCounts(*fn);
    headPtr->IncomingEdgeCount = 1;
    lenBlockPtr->IncomingEdgeCount = 1;
    charBlockAPtr->IncomingEdgeCount = 1;
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.Settings.SwitchStatementOnString = true;
    ctx.Settings.SwitchOnReadOnlySpanChar = true;
    IL::SwitchOnStringTransform transform;
    transform.Run(*fn, ctx);

    // The head's if+br pair folds into a switch over a StringToInt of the
    // switch-value variable; the length/char dispatch is consumed.
    ASSERT_EQ(headPtr->Instructions.size(), 1u)
        << "the length+char shape folds into a switch";
    auto* sw = dynamic_cast<IL::SwitchInstruction*>(headPtr->Instructions[0].get());
    ASSERT_NE(sw, nullptr);
    auto* stringToInt = dynamic_cast<IL::StringToInt*>(sw->Value.get());
    ASSERT_NE(stringToInt, nullptr);
    // The map: the 1-char case strings in scan order plus the null case
    // (the C# `string?[]` carries null for the case-null entry).
    ASSERT_EQ(stringToInt->Map.size(), 3u);
    ASSERT_TRUE(stringToInt->Map[0].first.has_value());
    EXPECT_EQ(*stringToInt->Map[0].first, "a");
    ASSERT_TRUE(stringToInt->Map[1].first.has_value());
    EXPECT_EQ(*stringToInt->Map[1].first, "b");
    EXPECT_FALSE(stringToInt->Map[2].first.has_value());
}

} // namespace
