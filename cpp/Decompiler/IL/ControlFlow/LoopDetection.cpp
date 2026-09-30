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

// The C# `Leave.IsLeavingFunction`: a leave whose target container is the
// function's root body (a return), as opposed to a leave breaking out of a
// nested container. The C#'s ControlFlowGraph.CreateEdges does not count a
// function return as a reachable exit.
bool IsFunctionLeave(const Leave* leave) {
    if (leave == nullptr || leave->TargetContainer == nullptr) return false;
    const ILInstruction* p = leave;
    while (p->Parent != nullptr) p = p->Parent;
    const auto* fn = dynamic_cast<const ILFunction*>(p);
    return fn != nullptr && fn->Body.get() == leave->TargetContainer;
}

// The C# ControlFlowGraph.HasReachableExit: "true iff there is a control
// flow path from node to a branch/leave instruction leaving the container,
// or to another node not dominated by node". A leave that exits the whole
// function (a return) is NOT considered a reachable exit, so a return
// block (and a self-contained linear continuation) qualifies as an exit
// point; a loop member never does (it reaches the loop head, which it does
// not dominate).
bool HasReachableExitNode(FlowAnalysis::ControlFlowNode* n,
                          BlockContainer* container) {
    std::vector<FlowAnalysis::ControlFlowNode*> stack = { n };
    std::unordered_set<FlowAnalysis::ControlFlowNode*> visited;
    while (!stack.empty()) {
        auto* cur = stack.back();
        stack.pop_back();
        if (!visited.insert(cur).second) continue;
        auto* block = static_cast<Block*>(cur->UserData);
        if (block != nullptr) {
            bool leavesContainer = false;
            std::function<void(const ILInstruction*)> scan2 =
                [&](const ILInstruction* inst) {
                if (inst == nullptr || leavesContainer) return;
                if (auto* br = dynamic_cast<const Branch*>(inst)) {
                    if (br->TargetBlock != nullptr &&
                        br->TargetBlock->Parent != container) {
                        bool nested = false;
                        for (const ILInstruction* q = br->TargetBlock;
                             q != nullptr; q = q->Parent) {
                            if (q == container) { nested = true; break; }
                        }
                        if (!nested) leavesContainer = true;
                    }
                } else if (auto* lv = dynamic_cast<const Leave*>(inst)) {
                    if (!IsFunctionLeave(lv)) leavesContainer = true;
                }
                if (leavesContainer) return;
                for (int i = 0; i < inst->ChildCount(); ++i)
                    scan2(inst->GetChild(i));
            };
            scan2(block->FinalInstruction.get());
            for (const auto& inst : block->Instructions) {
                scan2(inst.get());
                if (leavesContainer) break;
            }
            if (leavesContainer) return true;
        }
        for (auto* succ : cur->Successors) {
            if (!n->Dominates(succ)) return true;
            stack.push_back(succ);
        }
    }
    return false;
}

FlowAnalysis::ControlFlowNode* FindExitPointNode(
        const std::set<FlowAnalysis::ControlFlowNode*>& loop) {
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
    // Convergence: the candidate all other candidates reach without
    // re-entering the loop (the post-dominator of the exits).
    if (cands.size() > 1) {
        for (auto* cand : cands) {
            bool allReach = true;
            for (auto* other : cands) {
                if (other != cand && !ReachesOutsideLoop(other, cand, loop)) {
                    allReach = false;
                    break;
                }
            }
            if (allReach) return cand;  // convergence
        }
    }
    // The C# PickExitPoint source-order heuristic: among the candidates,
    // pick the HIGHEST-IL-offset node with no reachable exit ("the real
    // exit point... by simply picking the block with the highest IL
    // offset"). A return block qualifies (a function return is not a
    // reachable exit), so the loop-completion return is chosen over the
    // earlier returns, which belong inside the loop.
    BlockContainer* container = static_cast<BlockContainer*>(
        static_cast<Block*>(members.front()->UserData)->Parent);
    FlowAnalysis::ControlFlowNode* bestNode = nullptr;
    std::uint32_t bestOffset = 0;
    for (auto* c : cands) {
        auto* block = static_cast<Block*>(c->UserData);
        if (block == nullptr || block->Parent != container) continue;
        if (HasReachableExitNode(c, container)) continue;
        if (bestNode == nullptr || block->StartILOffset > bestOffset) {
            bestNode = c;
            bestOffset = block->StartILOffset;
        }
    }
    if (bestNode != nullptr) return bestNode;
    // No candidate qualifies (all have reachable exits): the earliest by
    // StartILOffset, the stable rule.
    Block* best = nullptr;
    bestOffset = std::numeric_limits<std::uint32_t>::max();
    for (auto* c : cands) {
        auto* block = static_cast<Block*>(c->UserData);
        if (!best || block->StartILOffset < bestOffset) {
            best = block;
            bestOffset = block->StartILOffset;
        }
    }
    for (auto* c : cands)
        if (static_cast<Block*>(c->UserData) == best) return c;
    return nullptr;
}

// The C# ExtendLoop (LoopDetection.cs): "Given a natural loop, add
// additional CFG nodes to the loop in order to reduce the number of exit
// points out of the loop" -- C# only allows reaching a single exit point
// with 'break', so any additional exit would need a 'goto'. The extension
// adds every block dominated by the loop head except the exit point's
// subtree (the C#'s dominator-tree preorder over the head, excluding the
// exit point): the early-return blocks end up inside the loop, where they
// render as plain 'return' statements, and the loop keeps one exit.
//
// The exit point must be validated first (the C# ValidateExitPoint): no
// node reachable from it may be dominated by the loop head but not by the
// exit point -- a cross edge back into the extended loop would break the
// single-entry invariant. Returns the exit-point node (also used as the
// loop's synthesized exit branch target).
FlowAnalysis::ControlFlowNode* ExtendLoop(
        const ControlFlowGraph& cfg, FlowAnalysis::ControlFlowNode* head,
        std::set<FlowAnalysis::ControlFlowNode*>& loop) {
    FlowAnalysis::ControlFlowNode* exitPoint = FindExitPointNode(loop);
    if (exitPoint == nullptr) return nullptr;
    if (!head->Dominates(exitPoint)) return exitPoint;  // no extension
    // The C# ValidateExitPoint (the recursive form over the dominator tree,
    // flattened to a successor DFS here): invalid iff a node reachable from
    // the exit point is dominated by the head but not by the exit point.
    {
        std::vector<FlowAnalysis::ControlFlowNode*> stack = { exitPoint };
        std::unordered_set<FlowAnalysis::ControlFlowNode*> visited;
        while (!stack.empty()) {
            auto* n = stack.back();
            stack.pop_back();
            if (!visited.insert(n).second) continue;
            for (auto* succ : n->Successors) {
                if (head != succ && head->Dominates(succ) &&
                    !exitPoint->Dominates(succ))
                    return exitPoint;  // invalid: keep the natural loop
                stack.push_back(succ);
            }
        }
    }
    // NOTE: the C# ExtendLoop also adds every head-dominated block outside
    // the exit point's subtree here (the early-return blocks join the loop,
    // keeping one exit). This port holds that extension until the guard-chain
    // condition combining lands: without it the extended loops render as
    // nested if-chains where the oracle combines the conditions (measured:
    // net10 +1168 with the extension, the CflowDecrypter.GetFixIndexs2 shape;
    // see the session record in HANDOFF_ILSPY.md).
    return exitPoint;
}

void ConstructLoop(BlockContainer* parent, FlowAnalysis::ControlFlowNode* headerNode,
                   const std::set<FlowAnalysis::ControlFlowNode*>& loop,
                   FlowAnalysis::ControlFlowNode* exitNode, ILTransformContext& ctx) {
    (void)ctx;
    Block* oldEntryPoint = static_cast<Block*>(headerNode->UserData);
    Block* exitBlock = exitNode != nullptr
                           ? static_cast<Block*>(exitNode->UserData)
                           : nullptr;

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
            // The C# ExtendLoop: include the head-dominated blocks outside
            // the exit point's subtree so the loop keeps one exit (the
            // early returns stay inside and render as returns).
            FlowAnalysis::ControlFlowNode* exitPoint = ExtendLoop(cfg, h, loop);
            context.StepOnce("Construct loop");
            ConstructLoop(c, h, loop, exitPoint, context);
            createdAny = true;
        }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
