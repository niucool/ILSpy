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

#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

void WalkContainers(ILInstruction* inst, const std::function<void(BlockContainer*)>& visit) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) visit(c);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkContainers(inst->GetChild(i), visit);
}

Block* FindExitPoint(const std::set<FlowAnalysis::ControlFlowNode*>& loop) {
    for (auto* n : loop) {
        for (auto* s : n->Successors) {
            if (!loop.count(s) && s->UserData)
                return static_cast<Block*>(s->UserData);
        }
    }
    return nullptr;
}

void ConstructLoop(BlockContainer* parent, FlowAnalysis::ControlFlowNode* headerNode,
                   const std::set<FlowAnalysis::ControlFlowNode*>& loop,
                   ILTransformContext& ctx) {
    (void)ctx;
    Block* oldEntryPoint = static_cast<Block*>(headerNode->UserData);
    Block* exitBlock = FindExitPoint(loop);

    auto loopContainer = std::make_unique<BlockContainer>();
    loopContainer->Kind = ContainerKind::Loop;
    BlockContainer* loopPtr = loopContainer.get();

    auto newEntryPoint = std::make_unique<Block>();
    Block* newEntryPointPtr = newEntryPoint.get();
    loopContainer->AddBlock(std::move(newEntryPoint));

    while (!oldEntryPoint->Instructions.empty()) {
        auto inst = std::move(oldEntryPoint->Instructions.front());
        oldEntryPoint->Instructions.erase(oldEntryPoint->Instructions.begin());
        newEntryPointPtr->Add(std::move(inst));
    }
    if (oldEntryPoint->FinalInstruction)
        newEntryPointPtr->SetFinal(std::move(oldEntryPoint->FinalInstruction));
    oldEntryPoint->RenumberChildren();
    newEntryPointPtr->RenumberChildren();

    oldEntryPoint->Add(std::move(loopContainer));
    {
        std::unique_ptr<ILInstruction> final = exitBlock
            ? std::unique_ptr<ILInstruction>(std::make_unique<Branch>(exitBlock))
            : std::unique_ptr<ILInstruction>(std::make_unique<Leave>(parent));
        oldEntryPoint->SetFinal(std::move(final));
    }

    for (auto* n : loop) {
        if (n == headerNode) continue;
        Block* block = static_cast<Block*>(n->UserData);
        if (!block || block->Parent != parent) continue;
        for (auto it = parent->Blocks.begin(); it != parent->Blocks.end(); ++it) {
            if (it->get() == block) {
                loopPtr->AddBlock(std::move(*it));
                parent->Blocks.erase(it);
                break;
            }
        }
    }
    for (std::size_t i = 0; i < parent->Blocks.size(); ++i) {
        parent->Blocks[i]->ChildIndex = static_cast<int>(i);
        parent->Blocks[i]->Parent = parent;
    }

    std::vector<Branch*> branches;
    WalkAll(loopPtr, [&](ILInstruction* inst) {
        if (auto* br = dynamic_cast<Branch*>(inst)) branches.push_back(br);
    });
    for (auto* br : branches) {
        if (br->TargetBlock == oldEntryPoint)
            br->TargetBlock = newEntryPointPtr;
        else if (exitBlock && br->TargetBlock == exitBlock)
            br->ReplaceWith(std::make_unique<Leave>(loopPtr));
    }
}

} // namespace

void LoopDetection::Run(ILFunction& function, ILTransformContext& context) {
    // Process each Normal-kind container in one pass (innermost loops first via
    // reverse post-order over the dominator tree). Loop-kind containers created
    // by this transform are skipped — the C# avoids re-detecting them because the
    // newly created entry point has no CFG node; we skip by Kind.
    WalkContainers(function.Body.get(), [&](BlockContainer* c) {
        if (c->Kind != ContainerKind::Normal) return;
        ControlFlowGraph cfg(c);
        if (cfg.Nodes().empty()) return;
        for (int i = static_cast<int>(cfg.Nodes().size()) - 1; i >= 0; --i) {
            auto* h = cfg.Nodes()[static_cast<std::size_t>(i)].get();
            Block* headerBlock = static_cast<Block*>(h->UserData);
            if (!headerBlock || headerBlock->Parent != c) continue;
            bool isLoopHeader = false;
            for (auto* t : h->Predecessors)
                if (h->Dominates(t)) { isLoopHeader = true; break; }
            if (!isLoopHeader) continue;
            std::set<FlowAnalysis::ControlFlowNode*> loop = { h };
            std::vector<FlowAnalysis::ControlFlowNode*> worklist;
            for (auto* t : h->Predecessors) {
                if (h->Dominates(t) && loop.insert(t).second)
                    worklist.push_back(t);
            }
            while (!worklist.empty()) {
                auto* n = worklist.back();
                worklist.pop_back();
                for (auto* p : n->Predecessors) {
                    if (p != h && loop.insert(p).second)
                        worklist.push_back(p);
                }
            }
            context.StepOnce("Construct loop");
            ConstructLoop(c, h, loop, context);
        }
    });
}

} // namespace ILSpy::Decompiler::IL
