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
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <unordered_set>
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

// Whether exit candidate `from` can reach exit candidate `target` following CFG
// successors WITHOUT re-entering the loop (DFS, visited-set bounded -- the graph
// is cyclic). Convergence into an exit only counts along post-loop paths.
static bool ReachesOutsideLoop(FlowAnalysis::ControlFlowNode* from,
                               FlowAnalysis::ControlFlowNode* target,
                               const std::set<FlowAnalysis::ControlFlowNode*>& loop) {
    if (from == target) return true;
    std::vector<FlowAnalysis::ControlFlowNode*> stack;
    std::unordered_set<FlowAnalysis::ControlFlowNode*> seen;
    stack.push_back(from);
    seen.insert(from);
    while (!stack.empty()) {
        auto* n = stack.back();
        stack.pop_back();
        for (auto* s : n->Successors) {
            if (s == target) return true;
            if (loop.count(s)) continue;  // do not converge by re-entering the loop
            if (seen.insert(s).second) stack.push_back(s);
        }
    }
    return false;
}

Block* FindExitPoint(const std::set<FlowAnalysis::ControlFlowNode*>& loop) {
    // Deterministic, convergence-aware exit-point pick. Iterating the
    // std::set<ControlFlowNode*> directly would leak raw pointer order, so the
    // members are ordered by block StartILOffset first. A loop usually has a
    // single out-of-loop successor; when it has several (an early break path and
    // a normal loop-completion path), the correct exit is the one every other
    // path CONVERGES to (the C# post-dominator of the loop exits), NOT the
    // lowest-offset one -- a match/break block often has a lower StartILOffset
    // than the block both it and the loop-completion fall-through flow into, and
    // exiting there mangles the loop structure. Prefer the unique candidate that
    // all other candidates reach without re-entering the loop; fall back to the
    // earliest candidate (min StartILOffset) when exits genuinely diverge, which
    // keeps the choice stable run-to-run. (The C# computes the exact
    // post-dominator via a reverse-CFG analysis; this convergence check is the
    // deterministic subset that handles the dominant structured cases.)
    std::vector<FlowAnalysis::ControlFlowNode*> members(loop.begin(), loop.end());
    std::sort(members.begin(), members.end(), [](const FlowAnalysis::ControlFlowNode* a,
                                                 const FlowAnalysis::ControlFlowNode* b) {
        auto* ba = static_cast<const Block*>(a->UserData);
        auto* bb = static_cast<const Block*>(b->UserData);
        return (ba ? ba->StartILOffset : 0) < (bb ? bb->StartILOffset : 0);
    });
    // Distinct out-of-loop successor nodes, in deterministic first-seen order.
    std::vector<FlowAnalysis::ControlFlowNode*> cands;
    std::unordered_set<FlowAnalysis::ControlFlowNode*> seen;
    for (auto* n : members) {
        for (auto* s : n->Successors) {
            if (loop.count(s) || !s->UserData) continue;
            if (seen.insert(s).second) cands.push_back(s);
        }
    }
    if (cands.empty()) return nullptr;
    if (cands.size() > 1) {
        for (auto* cand : cands) {
            bool allReach = true;
            for (auto* other : cands) {
                if (other != cand && !ReachesOutsideLoop(other, cand, loop)) {
                    allReach = false;
                    break;
                }
            }
            if (allReach) return static_cast<Block*>(cand->UserData);  // convergence
        }
    }
    // Divergent exits (or a single candidate): pick the earliest by StartILOffset.
    Block* best = nullptr;
    std::uint32_t bestOffset = std::numeric_limits<std::uint32_t>::max();
    for (auto* c : cands) {
        auto* block = static_cast<Block*>(c->UserData);
        if (!best || block->StartILOffset < bestOffset) {
            best = block;
            bestOffset = block->StartILOffset;
        }
    }
    return best;
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

    // The new entry point (the loop header) may carry the while-condition as
    // its FinalInstruction: `if (cond) br body` with no else. The fall-through
    // (cond false) is the loop exit, but after the body blocks are moved into
    // the container the fall-through would go to the body (the next block) --
    // losing the exit. Materialize it: if the if's true arm branches to a block
    // that will be inside the loop, add a `leave(loop)` as the false arm so the
    // exit path is an explicit break. (The body blocks haven't been moved yet,
    // but `loop` tells us which blocks end up inside.)
    if (auto* iff = dynamic_cast<IfInstruction*>(newEntryPointPtr->FinalInstruction.get())) {
        if (!iff->FalseInst && iff->TrueInst && iff->TrueInst->Op == OpCode::Branch) {
            auto* br = static_cast<Branch*>(iff->TrueInst.get());
            Block* tgt = br->TargetBlock;
            if (tgt) {
                bool tgtInLoop = false;
                for (auto* n : loop) {
                    if (n != headerNode && static_cast<Block*>(n->UserData) == tgt) {
                        tgtInLoop = true; break;
                    }
                }
                if (tgtInLoop) {
                    iff->FalseInst = std::make_unique<Leave>(loopPtr);
                    iff->FalseInst->Parent = iff;
                    iff->FalseInst->ChildIndex = 2;
                }
            }
        }
    }

    oldEntryPoint->Add(std::move(loopContainer));
    {
        std::unique_ptr<ILInstruction> final = exitBlock
            ? std::unique_ptr<ILInstruction>(std::make_unique<Branch>(exitBlock))
            : std::unique_ptr<ILInstruction>(std::make_unique<Leave>(parent));
        oldEntryPoint->SetFinal(std::move(final));
    }

    // Move the loop body blocks into the loop container, scanning parent->Blocks
    // in the enclosing container's existing block order. The old loop iterated the
    // `loop` std::set (raw pointer order), so member blocks landed in address
    // order -- which varies run-to-run, making the emitted block order (and the
    // IL_XXXX label/goto naming) nondeterministic. Scanning parent->Blocks
    // preserves the source layout and is stable.
    std::unordered_set<Block*> memberBlocks;
    memberBlocks.reserve(loop.size());
    for (auto* n : loop) {
        if (n == headerNode) continue;
        if (auto* b = static_cast<Block*>(n->UserData)) memberBlocks.insert(b);
    }
    for (auto it = parent->Blocks.begin(); it != parent->Blocks.end();) {
        if (memberBlocks.count(it->get())) {
            loopPtr->AddBlock(std::move(*it));
            it = parent->Blocks.erase(it);
        } else {
            ++it;
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
        else if (exitBlock && br->TargetBlock == exitBlock) {
            // Replace the branch to the loop exit with a `leave(loopContainer)`
            // (a `break`), carrying the branch's IL byte-range -- the C#
            // `.WithILRange(branch)`. The break keeps the offset of the branch
            // site so downstream transforms (notably the ImproveILOrdering
            // GetStartILOffset gate on a bare break Leave) see a valid range.
            auto leave = std::make_unique<Leave>(loopPtr);
            leave->AddILRange(*br);
            br->ReplaceWith(std::move(leave));
        }
    }
    // Branches outside the loop that targeted the old entry point (the loop
    // header) must also be repointed to the new entry point inside the
    // container, otherwise both the pre-header (oldEntryPoint) and the header
    // (newEntryPoint) carry the same IL_XXXX label.
    ILInstruction* root = parent;
    while (root->Parent) root = root->Parent;
    WalkAll(root, [&](ILInstruction* inst) {
        if (auto* br = dynamic_cast<Branch*>(inst)) {
            if (br->TargetBlock == oldEntryPoint)
                br->TargetBlock = newEntryPointPtr;
        }
    });
}

} // namespace

void LoopDetection::Run(ILFunction& function, ILTransformContext& context) {
    // Process containers in post-order (innermost first): a nested back-edge
    // inside an outer loop's body must be formed into a nested Loop BEFORE the
    // outer loop is formed (matching the C#'s post-order block-transform order).
    // Re-snapshot after each pass: processing a container creates new Loop
    // containers whose bodies may carry nested back-edges. Loop until a pass
    // creates no new Loop (bounded to avoid infinite re-detection).
    bool createdAny = true;
    int pass = 0;
    while (createdAny && pass < 8) {
        ++pass;
        createdAny = false;
        std::vector<BlockContainer*> containers;
        // Post-order: recurse into children before visiting the container.
        std::function<void(ILInstruction*)> walkPost = [&](ILInstruction* inst) {
            if (!inst) return;
            for (int i = 0; i < inst->ChildCount(); ++i) walkPost(inst->GetChild(i));
            if (auto* c = dynamic_cast<BlockContainer*>(inst))
                if (c->Kind == ContainerKind::Normal || c->Kind == ContainerKind::Loop)
                    containers.push_back(c);
        };
        walkPost(function.Body.get());
        for (BlockContainer* c : containers) {
        ControlFlowGraph cfg(c);
        if (cfg.Nodes().empty()) continue;
        for (int i = static_cast<int>(cfg.Nodes().size()) - 1; i >= 0; --i) {
            auto* h = cfg.Nodes()[static_cast<std::size_t>(i)].get();
            Block* headerBlock = static_cast<Block*>(h->UserData);
            if (!headerBlock || headerBlock->Parent != c) continue;
            // Skip a Loop container's own entry block (the structured loop
            // header ConstructLoop created): its back-edge is the loop's own,
            // already structured. Re-detecting it would re-wrap the loop into
            // itself forever. (The first block of a Loop container is the
            // header; the C# excludes it because the new entry point has no CFG
            // node.)
            if (c->Kind == ContainerKind::Loop && !c->Blocks.empty() &&
                c->Blocks.front().get() == headerBlock)
                continue;
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
            createdAny = true;
        }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
