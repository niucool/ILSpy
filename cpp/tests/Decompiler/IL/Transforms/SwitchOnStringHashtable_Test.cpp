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
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace Util = ::ILSpy::Decompiler::Util;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeHashtableSwitchLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// The legacy mcs Hashtable switch shape (the C# ScanHashtableInitializerBlocks
// + MatchLegacySwitchOnStringWithHashtable pair), with the null case jumping
// to its own block:
//   entry:    if (ldobj Hashtable(field) != null) br switchHead; br initBlock
//   init:     stloc table(newobj Hashtable..ctor(2, 0.5f)); Add x2;
//             stobj Hashtable(field, ldloc table); br switchHead
//   head:     stloc tmp(ldloc s); stloc switchVar(ldloc tmp);
//             if (comp(ldloc tmp == ldnull)) br nullCase; br getItemBlock
//   getItem:  stloc tmp2(get_Item(ldobj Hashtable(field), ldloc switchVar));
//             stloc switchVar2(ldloc tmp2);
//             if (comp(ldloc tmp2 == ldnull)) br defaultBlock; br switchBlock
//   switch:   switch(ldobj Int32(unbox.any Int32(ldloc switchVar2)))
// The transform folds the head's 4-instruction shape into a switch over a
// StringToInt of the ORIGINAL switch value (tmp's value), keyed by the
// scan-extracted (string, index) pairs.
TEST(SwitchOnStringHashtableTest, LegacyHashtableSwitchFolds)
{
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto htType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Collections"), std::string("Hashtable")));
    auto int32Type = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto s = MakeHashtableSwitchLocal("s", stringType);
    auto tmp = MakeHashtableSwitchLocal("tmp", stringType);
    auto switchVar = MakeHashtableSwitchLocal("switchVar", stringType);
    auto tmp2 = MakeHashtableSwitchLocal("tmp2", TS::ITypePtr());
    auto switchVar2 = MakeHashtableSwitchLocal("switchVar2", TS::ITypePtr());
    auto table = MakeHashtableSwitchLocal("table", htType);

    auto fn = std::make_unique<IL::ILFunction>();
    auto container = std::make_unique<IL::BlockContainer>();
    // The entry block first: the C# scan reads the container's entry point
    // (the port's Blocks[0]).
    auto entry = std::make_unique<IL::Block>();
    entry->Kind = IL::BlockKind::ControlFlow;
    IL::Block* entryPtr = entry.get();
    {
        auto ldsflda = std::make_unique<IL::LdsFlda>(
            "System.Runtime.CompilerServices.CompilerGenerated::$$method0x600000c-1");
        ldsflda->IsCompilerGeneratedField = true;
        auto ldobj = std::make_unique<IL::LdObj>(std::move(ldsflda), htType);
        auto neq = std::make_unique<IL::Comp>(
            std::move(ldobj), std::make_unique<IL::LdNull>(),
            IL::ComparisonKind::Inequality, false);
        entry->Add(std::make_unique<IL::IfInstruction>(
            std::move(neq), std::make_unique<IL::Branch>(nullptr)));
        entry->Add(std::make_unique<IL::Branch>(nullptr));
    }
    container->Blocks.push_back(std::move(entry));
    for (ILVariablePtr* v : {&s, &tmp, &switchVar, &tmp2, &switchVar2, &table}) {
        fn->Variables.push_back(*v);
    }

    auto nullCase = std::make_unique<IL::Block>();
    nullCase->Kind = IL::BlockKind::ControlFlow;
    IL::Block* nullCasePtr = nullCase.get();
    nullCase->Add(std::make_unique<IL::Leave>(container.get()));
    container->Blocks.push_back(std::move(nullCase));
    auto exit = std::make_unique<IL::Block>();
    exit->Kind = IL::BlockKind::ControlFlow;
    IL::Block* exitPtr = exit.get();
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
    auto switchBlock = std::make_unique<IL::Block>();
    switchBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* switchBlockPtr = switchBlock.get();
    container->Blocks.push_back(std::move(switchBlock));
    auto getItemBlock = std::make_unique<IL::Block>();
    getItemBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* getItemBlockPtr = getItemBlock.get();
    container->Blocks.push_back(std::move(getItemBlock));
    auto head = std::make_unique<IL::Block>();
    head->Kind = IL::BlockKind::ControlFlow;
    IL::Block* headPtr = head.get();
    container->Blocks.push_back(std::move(head));
    auto initBlock = std::make_unique<IL::Block>();
    initBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* initBlockPtr = initBlock.get();
    container->Blocks.push_back(std::move(initBlock));

    // Wire the entry branches now that the target pointers exist.
    static_cast<IL::IfInstruction*>(entryPtr->Instructions[0].get())
        ->TrueInst = std::make_unique<IL::Branch>(headPtr);
    static_cast<IL::Branch*>(entryPtr->Instructions[1].get())->TargetBlock =
        initBlockPtr;

    // The init block: the ctor + the Add calls + the trailing stobj + branch.
    {
        auto ctor = std::make_unique<IL::Call>(
            "System.Collections.Hashtable::.ctor");
        ctor->IsNewObj = true;
        ctor->DeclaringType = htType;
        ctor->AddArg(std::make_unique<IL::LdcI4>(2));
        ctor->AddArg(std::make_unique<IL::LdcF4>(0.5f));
        initBlockPtr->Add(std::make_unique<IL::StLoc>(table, std::move(ctor)));
        auto makeAdd = [&](const char* value, int index) {
            auto add = std::make_unique<IL::Call>(
                "System.Collections.Hashtable::Add");
            add->DeclaringType = htType;
            add->AddArg(std::make_unique<IL::LdLoc>(table));
            add->AddArg(std::make_unique<IL::LdStr>(value));
            add->AddArg(std::make_unique<IL::Box>(
                int32Type, std::make_unique<IL::LdcI4>(index)));
            return add;
        };
        initBlockPtr->Add(makeAdd("alpha", 0));
        initBlockPtr->Add(makeAdd("beta", 1));
        auto ldsflda = std::make_unique<IL::LdsFlda>(
            "System.Runtime.CompilerServices.CompilerGenerated::$$method0x600000c-1");
        ldsflda->IsCompilerGeneratedField = true;
        initBlockPtr->Add(std::make_unique<IL::StObj>(
            std::move(ldsflda), std::make_unique<IL::LdLoc>(table), htType));
        initBlockPtr->Add(std::make_unique<IL::Branch>(headPtr));
    }

    // The switch head: the 4-instruction shape the transform folds.
    headPtr->Add(std::make_unique<IL::StLoc>(
        tmp, std::make_unique<IL::LdLoc>(s)));
    headPtr->Add(std::make_unique<IL::StLoc>(
        switchVar, std::make_unique<IL::LdLoc>(tmp)));
    {
        auto eq = std::make_unique<IL::Comp>(
            std::make_unique<IL::LdLoc>(tmp), std::make_unique<IL::LdNull>(),
            IL::ComparisonKind::Equality, false);
        headPtr->Add(std::make_unique<IL::IfInstruction>(
            std::move(eq), std::make_unique<IL::Branch>(nullCasePtr)));
    }
    headPtr->Add(std::make_unique<IL::Branch>(getItemBlockPtr));

    // The get_Item block: the 4-instruction shape.
    {
        auto getItem = std::make_unique<IL::Call>(
            "System.Collections.Hashtable::get_Item");
        getItem->DeclaringType = htType;
        auto ldsflda = std::make_unique<IL::LdsFlda>(
            "System.Runtime.CompilerServices.CompilerGenerated::$$method0x600000c-1");
        ldsflda->IsCompilerGeneratedField = true;
        getItem->AddArg(std::make_unique<IL::LdObj>(std::move(ldsflda), htType));
        getItem->AddArg(std::make_unique<IL::LdLoc>(switchVar));
        getItemBlockPtr->Add(std::make_unique<IL::StLoc>(tmp2, std::move(getItem)));
        getItemBlockPtr->Add(std::make_unique<IL::StLoc>(
            switchVar2, std::make_unique<IL::LdLoc>(tmp2)));
        auto eq = std::make_unique<IL::Comp>(
            std::make_unique<IL::LdLoc>(tmp2), std::make_unique<IL::LdNull>(),
            IL::ComparisonKind::Equality, false);
        getItemBlockPtr->Add(std::make_unique<IL::IfInstruction>(
            std::move(eq), std::make_unique<IL::Branch>(defaultBlockPtr)));
        getItemBlockPtr->Add(std::make_unique<IL::Branch>(switchBlockPtr));
    }

    // The switch block: switch(ldobj Int32(unbox.any Int32(ldloc switchVar2))).
    {
        auto unbox = std::make_unique<IL::UnboxAny>(
            int32Type, std::make_unique<IL::LdLoc>(switchVar2));
        auto ldobj = std::make_unique<IL::LdObj>(std::move(unbox), int32Type);
        auto sw = std::make_unique<IL::SwitchInstruction>(std::move(ldobj));
        auto addSection = [&](long long label, IL::Block* target) {
            auto section = std::make_unique<IL::SwitchSection>(
                Util::LongSet(Util::LongInterval(label, label + 1)));
            section->SetBody(std::make_unique<IL::Branch>(target));
            sw->Sections.push_back(std::move(section));
        };
        addSection(0, bodyAPtr);
        addSection(1, bodyBPtr);
        auto defaultSection = std::make_unique<IL::SwitchSection>(
            Util::LongSet(Util::LongInterval(0, 2)).Invert());
        defaultSection->SetBody(std::make_unique<IL::Branch>(defaultBlockPtr));
        sw->Sections.push_back(std::move(defaultSection));
        switchBlockPtr->Add(std::move(sw));
    }

    fn->Body = std::move(container);
    IL::RecomputeIncomingEdgeCounts(*fn);
    entryPtr->IncomingEdgeCount = 1;
    headPtr->IncomingEdgeCount = 1;
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.Settings.SwitchStatementOnString = true;
    IL::SwitchOnStringTransform transform;
    transform.Run(*fn, ctx);

    // The head's 4-instruction shape folds into a switch over a StringToInt
    // of the original switch value (tmp's value); the tmp/switchVar round
    // trip and the null-check if are gone.
    ASSERT_EQ(headPtr->Instructions.size(), 1u)
        << "the Hashtable shape folds into a switch";
    auto* sw = dynamic_cast<IL::SwitchInstruction*>(headPtr->Instructions[0].get());
    ASSERT_NE(sw, nullptr);
    auto* stringToInt = dynamic_cast<IL::StringToInt*>(sw->Value.get());
    ASSERT_NE(stringToInt, nullptr);
    ASSERT_EQ(stringToInt->Map.size(), 2u);
    ASSERT_TRUE(stringToInt->Map[0].first.has_value());
    EXPECT_EQ(*stringToInt->Map[0].first, "alpha");
    ASSERT_TRUE(stringToInt->Map[1].first.has_value());
    EXPECT_EQ(*stringToInt->Map[1].first, "beta");
    // The switch dispatches on the original value load (tmp's value).
    auto* argLdLoc = dynamic_cast<IL::LdLoc*>(stringToInt->Argument.get());
    ASSERT_NE(argLdLoc, nullptr);
    EXPECT_EQ(argLdLoc->Variable.get(), s.get());
}

} // namespace
