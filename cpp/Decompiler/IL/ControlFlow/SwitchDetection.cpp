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

#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Match an integer constant (LdcI4 or LdcI8). The C# MatchLdcI also unwraps
// Conv sign/zero-extend; this port's reader does not wrap ldc in Conv, so the
// plain cases cover the inputs. Mirrors the local MatchLdcI in SwitchAnalysis.cpp
// (a shared match-helpers header is not yet established in this port).
bool MatchLdcI(ILInstruction* inst, long long& val) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdcI4) {
        val = static_cast<LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == OpCode::LdcI8) {
        val = static_cast<LdcI8*>(inst)->Value;
        return true;
    }
    return false;
}

// The SwitchInstruction that ends `block`, or nullptr if the block has no final
// or the final is not a switch. Matches the C# assumption that, with basic
// blocks at this pipeline stage, a switch can only be the block's last
// instruction.
SwitchInstruction* BlockSwitch(Block* block) {
    if (!block || !block->FinalInstruction) return nullptr;
    if (block->FinalInstruction->Op != OpCode::SwitchInstruction) return nullptr;
    return static_cast<SwitchInstruction*>(block->FinalInstruction.get());
}

// Walk every BlockContainer in the tree (the C# function.Descendants.OfType<
// BlockContainer>()).
void WalkContainers(ILInstruction* inst, const std::function<void(BlockContainer*)>& visit) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) visit(c);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkContainers(inst->GetChild(i), visit);
}

// The C# builds a SwitchInstruction whose sections hold the *same* Branch
// objects the analysis found (the GC keeps them alive after the inner blocks
// are cleared). This port has no GC: clearing the inner blocks would destroy the
// Branches the sections point at. CloneBody makes an independent copy of a
// section body so the SwitchInstruction owns its sections outright. Section
// bodies are Branch (common -- the if-chain's true arm and the fall-through) or
// Leave (an exit). A Leave may carry a return value; the value is a simple load
// at this pipeline stage (LdLoc/LdcI4/LdcI8/LdNull/LdStr) and is cloned
// accordingly. An uncloneable body (a Leave with a non-simple value, or an
// unknown instruction kind) yields nullptr so the caller can skip forming the
// switch for that block (the if-chain stays as ifs -- still valid output).
std::unique_ptr<ILInstruction> CloneSimpleValue(ILInstruction* v) {
    if (!v) return nullptr;
    switch (v->Op) {
        case OpCode::LdLoc:
            return std::make_unique<LdLoc>(static_cast<LdLoc*>(v)->Variable);
        case OpCode::LdcI4:
            return std::make_unique<LdcI4>(static_cast<LdcI4*>(v)->Value);
        case OpCode::LdcI8:
            return std::make_unique<LdcI8>(static_cast<LdcI8*>(v)->Value);
        case OpCode::LdNull:
            return std::make_unique<LdNull>();
        case OpCode::LdStr:
            return std::make_unique<LdStr>(static_cast<LdStr*>(v)->Value);
        default:
            return nullptr;  // uncloneable
    }
}

std::unique_ptr<ILInstruction> CloneBody(ILInstruction* body) {
    if (!body) return nullptr;
    if (auto* br = dynamic_cast<Branch*>(body)) {
        auto clone = std::make_unique<Branch>(br->TargetBlock);
        clone->TargetOffset = br->TargetOffset;
        clone->HasOffset = br->HasOffset;
        return clone;
    }
    if (auto* leave = dynamic_cast<Leave*>(body)) {
        std::unique_ptr<ILInstruction> valueClone;
        if (leave->Value) {
            valueClone = CloneSimpleValue(leave->Value.get());
            if (!valueClone) return nullptr;  // uncloneable value
        }
        return std::make_unique<Leave>(leave->TargetContainer, std::move(valueClone));
    }
    return nullptr;  // uncloneable body kind
}

// MaxValuesPerSection: the C# threshold (100) above which a section is treated
// as the default (a switch's default arm holds the complement of the finite
// case labels, which is huge).
const std::uint64_t MaxValuesPerSection = 100;

} // namespace

void SwitchDetection::SimplifySwitchInstruction(Block* block, ILTransformContext& context) {
    auto* sw = BlockSwitch(block);
    if (!sw) return;

    // Combine sections with identical branch target: the first section branching
    // to a block is the primary; later sections branching to the same block
    // merge their label sets into it and are removed. Mirrors the C#
    // sw.Sections.RemoveAll(...) with a Block->primary dictionary. Only Branch
    // bodies are de-duplicated (the C# MatchBranch); Leave bodies are left
    // alone (they are handled by the later exit-point transforms).
    std::unordered_map<Block*, SwitchSection*> primaryByTarget;
    std::vector<std::size_t> remove;
    for (std::size_t i = 0; i < sw->Sections.size(); ++i) {
        auto& section = sw->Sections[i];
        if (!section) continue;
        auto* br = dynamic_cast<Branch*>(section->Body.get());
        Block* target = (br && br->TargetBlock) ? br->TargetBlock : nullptr;
        if (!target) continue;  // not a resolved Branch-to-block: leave alone
        auto it = primaryByTarget.find(target);
        if (it != primaryByTarget.end()) {
            context.StepOnce("Combine switch sections with same branch target");
            it->second->Labels = it->second->Labels.UnionWith(section->Labels);
            // section.HasNullLabel is not modeled in this port (a SwitchOnNullable
            // concern); the C# `primarySection.HasNullLabel |= section.HasNullLabel`
            // line is skipped here.
            remove.push_back(i);
        } else {
            primaryByTarget.emplace(target, section.get());
        }
    }
    if (!remove.empty()) {
        for (auto it = remove.rbegin(); it != remove.rend(); ++it)
            sw->Sections.erase(sw->Sections.begin() + static_cast<std::ptrdiff_t>(*it));
        for (std::size_t i = 0; i < sw->Sections.size(); ++i)
            if (sw->Sections[i]) sw->Sections[i]->ChildIndex = static_cast<int>(i + 1);
    }

    AdjustLabels(sw, context);
    SortSwitchSections(sw, context);
}

void SwitchDetection::AdjustLabels(SwitchInstruction* sw, ILTransformContext& /*context*/) {
    if (!sw || !sw->Value) return;
    auto* bop = dynamic_cast<BinaryNumericInstruction*>(sw->Value.get());
    if (!bop || bop->CheckForOverflow) return;
    long long val;
    if (!MatchLdcI(bop->Right.get(), val)) return;
    long long offset;
    switch (bop->Operator) {
        case BinaryNumericOperator::Add:
            // C# unchecked(-val): wrap the negation through uint64 (signed
            // overflow is UB in C++).
            offset = static_cast<long long>(static_cast<std::uint64_t>(0) -
                                            static_cast<std::uint64_t>(val));
            break;
        case BinaryNumericOperator::Sub:
            offset = val;
            break;
        default:
            return;  // not an Add/Sub offset
    }
    // Move the offset into the labels: the switch value becomes the bare
    // operand (bop->Left), and every section's labels shift by `offset`.
    auto left = std::move(bop->Left);  // detach; bop->Left is now null
    sw->Value = std::move(left);       // destroys the bop, takes ownership of Left
    if (sw->Value) {
        sw->Value->Parent = sw;
        sw->Value->ChildIndex = 0;
    }
    for (auto& section : sw->Sections) {
        if (section) section->Labels = section->Labels.AddOffset(offset);
    }
}

void SwitchDetection::SortSwitchSections(SwitchInstruction* sw, ILTransformContext& context) {
    if (!sw) return;
    auto& secs = sw->Sections;
    if (secs.size() <= 1) return;

    // First label value (C# s.Labels.Values.FirstOrDefault()), or 0 for an
    // empty/default section (FirstOrDefault on an empty long sequence is 0).
    // Precomputed once per section (not per comparison) so the universe-sized
    // default set is never enumerated.
    auto labelFirst = [](SwitchSection* s) -> long long {
        if (!s) return 0;
        const auto& intervals = s->Labels.Intervals();
        if (intervals.empty()) return 0;
        return intervals.front().Start;
    };

    if (context.Settings.SortSwitchSections) {
        // Sort by label value (a diffing aid for obfuscated assemblies).
        std::vector<int> order(secs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return labelFirst(secs[static_cast<std::size_t>(a)].get()) <
                   labelFirst(secs[static_cast<std::size_t>(b)].get());
        });
        std::vector<std::unique_ptr<SwitchSection>> moved;
        moved.reserve(secs.size());
        for (int idx : order)
            moved.push_back(std::move(secs[static_cast<std::size_t>(idx)]));
        secs = std::move(moved);
    } else {
        // Sort by branch-target IL offset (the C# default; preserves the
        // original case order across rebuilds). Primary key: Branch -> its
        // TargetOffset; Leave -> its start IL offset; other -> null (sorts
        // first, as in C# OrderBy over int?). This port does not carry
        // per-instruction ILRange, so a Leave body uses offset 0 as a
        // fallback (the CFS first pass only sees Branch bodies, so this does
        // not affect output today). Secondary key: the first label value.
        auto keyOf = [&](SwitchSection* s) -> std::tuple<bool, long long, long long> {
            long long lf = labelFirst(s);
            if (auto* br = dynamic_cast<Branch*>(s ? s->Body.get() : nullptr)) {
                return {true, static_cast<long long>(br->TargetOffset), lf};
            }
            if (dynamic_cast<Leave*>(s ? s->Body.get() : nullptr)) {
                return {true, 0, lf};
            }
            return {false, 0, lf};
        };
        std::vector<int> order(secs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return keyOf(secs[static_cast<std::size_t>(a)].get()) <
                   keyOf(secs[static_cast<std::size_t>(b)].get());
        });
        std::vector<std::unique_ptr<SwitchSection>> moved;
        moved.reserve(secs.size());
        for (int idx : order)
            moved.push_back(std::move(secs[static_cast<std::size_t>(idx)]));
        secs = std::move(moved);
    }

    // Re-wire each section's ChildIndex (Value is child 0, sections are 1..n).
    for (std::size_t i = 0; i < secs.size(); ++i)
        if (secs[i]) secs[i]->ChildIndex = static_cast<int>(i + 1);
}

// ---------------------------------------------------------------------------
// LoopContext
// ---------------------------------------------------------------------------

SwitchDetection::LoopContext::LoopContext(const ControlFlowGraph& cfg,
                                          FlowAnalysis::ControlFlowNode* contextNode) {
    if (!contextNode) return;
    // Walk contextNode's successors; a node that dominates contextNode is a loop
    // head (record it, do not recurse), else recurse into its successors. The
    // C# uses a recursive local function; an explicit stack avoids deep
    // recursion on large methods.
    std::vector<FlowAnalysis::ControlFlowNode*> loopHeads;
    std::vector<FlowAnalysis::ControlFlowNode*> stack;
    for (auto* s : contextNode->Successors) stack.push_back(s);
    while (!stack.empty()) {
        auto* n = stack.back();
        stack.pop_back();
        if (!n || n->Visited) continue;
        n->Visited = true;
        if (n->Dominates(contextNode))
            loopHeads.push_back(n);
        else
            for (auto* s : n->Successors) stack.push_back(s);
    }
    // Reset Visited on every node (the C# ResetVisited(cfg.cfg)).
    for (const auto& node : cfg.Nodes()) node->Visited = false;

    // Order loop heads by post-order number and assign increasing depths.
    std::sort(loopHeads.begin(), loopHeads.end(),
              [](FlowAnalysis::ControlFlowNode* a, FlowAnalysis::ControlFlowNode* b) {
                  return a->PostOrderNumber < b->PostOrderNumber;
              });
    int depth = 1;
    for (auto* head : loopHeads) {
        continueDepth_.emplace_back(FindContinue(head), depth);
        ++depth;
    }
}

FlowAnalysis::ControlFlowNode* SwitchDetection::LoopContext::FindContinue(
        FlowAnalysis::ControlFlowNode* loopHead) {
    // OnlyOrDefault(p => p != loopHead && loopHead.Dominates(p)): the single
    // predecessor the loop head dominates (the back-edge source), or null.
    FlowAnalysis::ControlFlowNode* pred = nullptr;
    int matchCount = 0;
    for (auto* p : loopHead->Predecessors) {
        if (p == loopHead || !loopHead->Dominates(p)) continue;
        ++matchCount;
        pred = p;
    }
    if (matchCount != 1 || !pred) return loopHead;

    auto* headBlock = static_cast<Block*>(loopHead->UserData);
    if (pred->Successors.size() == 1) {
        Block* target = nullptr;
        if (HighLevelLoopTransform::MatchIncrementBlock(static_cast<Block*>(pred->UserData), target) &&
            target == headBlock)
            return pred;
    }
    if (pred->Successors.size() <= 2) {
        Block* t1 = nullptr;
        Block* t2 = nullptr;
        if (HighLevelLoopTransform::MatchDoWhileConditionBlock(static_cast<Block*>(pred->UserData), t1, t2) &&
            (t1 == headBlock || t2 == headBlock))
            return pred;
    }
    return loopHead;
}

bool SwitchDetection::LoopContext::MatchContinue(FlowAnalysis::ControlFlowNode* node) const {
    return GetContinueDepth(node) != 0;
}

bool SwitchDetection::LoopContext::MatchContinue(FlowAnalysis::ControlFlowNode* node, int depth) const {
    return GetContinueDepth(node) == depth;
}

int SwitchDetection::LoopContext::GetContinueDepth(FlowAnalysis::ControlFlowNode* node) const {
    for (const auto& p : continueDepth_) {
        if (p.first == node) return p.second;
    }
    return 0;
}

std::vector<FlowAnalysis::ControlFlowNode*>
SwitchDetection::LoopContext::GetBreakTargets(FlowAnalysis::ControlFlowNode* dominator) const {
    std::vector<FlowAnalysis::ControlFlowNode*> result;
    if (!dominator) return result;
    // Pre-order DFS over the dominator tree (skipping continue targets),
    // collecting each node's successors that the dominator does not dominate
    // and that are not depth-1 continues. Mirrors TreeTraversal.PreOrder +
    // SelectMany + Where. May contain duplicates (a successor reachable from
    // several subtree nodes); the caller dedups.
    std::vector<FlowAnalysis::ControlFlowNode*> stack;
    stack.push_back(dominator);
    while (!stack.empty()) {
        auto* n = stack.back();
        stack.pop_back();
        if (!n) continue;
        if (n->DominatorTreeChildren) {
            auto& kids = *n->DominatorTreeChildren;
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                if (!MatchContinue(*it)) stack.push_back(*it);
            }
        }
        for (auto* s : n->Successors) {
            if (!s) continue;
            if (dominator->Dominates(s)) continue;
            if (MatchContinue(s, 1)) continue;
            result.push_back(s);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Run / ProcessBlock / UseCSharpSwitch -- the full SwitchDetection transform.
// ---------------------------------------------------------------------------

SwitchDetection::~SwitchDetection() = default;

void SwitchDetection::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.SparseIntegerSwitch) return;
    context_ = &context;
    analysis_.AllowUnreachableCases = context.Settings.RemoveDeadCode;
    WalkContainers(function.Body.get(), [&](BlockContainer* container) {
        currentContainer_ = container;
        controlFlowGraph_.reset();
        loopContext_.reset();
        bool needsCleanup = false;
        std::vector<Block*> clearedBlocks;
        // Iterate by index up to the original block count; ProcessBlock does not
        // mutate the Blocks vector (it only clears inner blocks' instructions and
        // records them for removal after the loop), so the iteration is stable.
        const std::size_t nBlocks = container->Blocks.size();
        for (std::size_t i = 0; i < nBlocks; ++i) {
            Block* block = container->Blocks[i].get();
            if (block) ProcessBlock(block, needsCleanup, clearedBlocks);
        }
        if (needsCleanup) {
            // Drop the cleared (now-empty) inner blocks the switch absorbed. They
            // are unreachable (the if-chain that reached them is gone) and empty
            // (no children to dangle). The C# uses Blocks.RemoveAll(empty) (or
            // SortBlocks(deleteUnreachable) under RemoveDeadCode -- unsafe in this
            // port per D58); building a new vector without them is the safe
            // equivalent. The container entry block is always kept.
            Block* entryBlock = container->Blocks.empty() ? nullptr : container->Blocks[0].get();
            std::unordered_set<Block*> toRemove(clearedBlocks.begin(), clearedBlocks.end());
            std::vector<std::unique_ptr<Block>> kept;
            kept.reserve(container->Blocks.size());
            for (auto& b : container->Blocks) {
                Block* raw = b.get();
                if (raw && raw != entryBlock && toRemove.count(raw) &&
                    raw->Instructions.empty() && !raw->FinalInstruction)
                    continue;  // drop the cleared inner block
                kept.push_back(std::move(b));
            }
            container->Blocks = std::move(kept);
            for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
                container->Blocks[i]->ChildIndex = static_cast<int>(i);
                container->Blocks[i]->Parent = container;
            }
        }
    });
    // Refresh edge counts so downstream transforms see the switch's section-body
    // branches targeting the case blocks.
    RecomputeIncomingEdgeCounts(function);
    context_ = nullptr;
    currentContainer_ = nullptr;
    controlFlowGraph_.reset();
    loopContext_.reset();
}

void SwitchDetection::ProcessBlock(Block* block, bool& needsCleanup,
                                   std::vector<Block*>& clearedBlocks) {
    bool analysisSuccess = analysis_.AnalyzeBlock(block);
    bool formedSwitch = false;
    if (analysisSuccess && UseCSharpSwitch()) {
        // Clone each section body so the SwitchInstruction owns its sections
        // outright. This port has no GC: the originals (the if-chain's Branches)
        // are destroyed when the inner blocks are cleared below, so the sections
        // must hold independent copies. A Branch body is trivial; a Leave body
        // may carry a simple-load value. An uncloneable body aborts the switch.
        std::vector<std::unique_ptr<SwitchSection>> sections;
        bool allCloneable = true;
        for (auto& s : analysis_.Sections) {
            auto body = CloneBody(s.Body);
            if (!body) { allCloneable = false; break; }
            auto ss = std::make_unique<SwitchSection>(s.Labels);
            ss->SetBody(std::move(body));
            sections.push_back(std::move(ss));
        }
        if (allCloneable && analysis_.SwitchVariable) {
            // switch(SwitchVariable), widened to I8 if not I4/I8 (the C# wraps a
            // non-integer-typed switch value in Conv I8 signed).
            std::unique_ptr<ILInstruction> switchValue =
                std::make_unique<LdLoc>(analysis_.SwitchVariable);
            auto rt = switchValue->ResultType();
            if (!(rt == StackType::I4 || rt == StackType::I8))
                switchValue = std::make_unique<Conv>(std::move(switchValue),
                                                      StackType::I8, false);
            auto sw = std::make_unique<SwitchInstruction>(std::move(switchValue));
            for (auto& s : sections) sw->AddSection(std::move(s));
            auto* swp = sw.get();
            // In the C# block model the if is Instructions[Count-2] with a
            // fall-through Branch as the last instruction (removed here); in
            // this port the if is the block's FinalInstruction, so replacing the
            // final is the whole operation (no fall-through branch to remove).
            block->SetFinal(std::move(sw));
            // Clear the absorbed inner blocks (the C# innerBlock.Instructions.Clear()
            // -- here clear both the non-terminal instructions and the final, so
            // the block is fully empty and removable).
            for (Block* innerBlock : analysis_.InnerBlocks) {
                if (!innerBlock) continue;
                innerBlock->Instructions.clear();
                innerBlock->FinalInstruction.reset();
                clearedBlocks.push_back(innerBlock);
            }
            controlFlowGraph_.reset();  // the CFG is no longer valid
            needsCleanup = true;
            SortSwitchSections(swp, *context_);
            formedSwitch = true;
        }
    }
    if (!formedSwitch) {
        // 2nd pass of SimplifySwitchInstruction (1st pass was in CFS), or the
        // fallback when the if-chain did not qualify as a switch / a section body
        // was uncloneable.
        SimplifySwitchInstruction(block, *context_);
    }
    // InlineSwitchExpressionDefaultCaseThrowHelper is deferred (needs
    // IMethod/IType resolution via the type system, not yet carried).
}

bool SwitchDetection::UseCSharpSwitch() {
    if (analysis_.InnerBlocks.empty()) return false;
    // The default section is the one with > MaxValuesPerSection values (the
    // complement of the finite case labels, which is huge).
    int defaultIdx = -1;
    for (int i = 0; i < static_cast<int>(analysis_.Sections.size()); ++i) {
        if (analysis_.Sections[static_cast<std::size_t>(i)].Labels.Count() > MaxValuesPerSection) {
            defaultIdx = i;
            break;
        }
    }
    if (defaultIdx < 0) return false;  // no default section
    const auto& defaultKey = analysis_.Sections[static_cast<std::size_t>(defaultIdx)].Labels;
    // Only the default may have tons of keys (C# has no `case 1 to 100000000`).
    for (int i = 0; i < static_cast<int>(analysis_.Sections.size()); ++i) {
        if (i == defaultIdx) continue;
        const auto& key = analysis_.Sections[static_cast<std::size_t>(i)].Labels;
        if (!key.SetEquals(defaultKey) && key.Count() > MaxValuesPerSection)
            return false;
    }
    // An existing IL switch (or a Roslyn switch-on-string) is a strong signal
    // the surrounding code is a switch. MatchRoslynSwitchOnString is deferred
    // (needs SwitchOnStringTransform.MatchComputeStringOrReadOnlySpanHashCall).
    if (analysis_.ContainsILSwitch) return true;

    // Heuristic: prefer an if-chain when it has fewer branches than the switch
    // would have label-intervals.
    int ifCount = static_cast<int>(analysis_.InnerBlocks.size()) + 1;
    int intervalCount = 0;
    for (int i = 0; i < static_cast<int>(analysis_.Sections.size()); ++i) {
        if (i == defaultIdx) continue;
        intervalCount += static_cast<int>(
            analysis_.Sections[static_cast<std::size_t>(i)].Labels.Intervals().size());
    }
    if (ifCount < intervalCount) return false;

    std::vector<FlowAnalysis::ControlFlowNode*> flowNodes, caseNodes;
    AnalyzeControlFlow(flowNodes, caseNodes);
    // A 2-section switch that is a single short-circuited condition (e.g. a
    // loop condition `while (c == '\n' || c == '\r')`) reads better as an if.
    if (analysis_.Sections.size() == 2 && IsSingleCondition(flowNodes, caseNodes))
        return false;
    Block* breakBlock = nullptr;
    if (SwitchUsesGoto(flowNodes, caseNodes, breakBlock)) return false;
    if (breakBlock == nullptr) return true;
    // The break target should have the highest IL offset of all the switch
    // targets, so it can be the fall-through after the switch.
    long long maxTargetOffset = -1;
    for (auto& s : analysis_.Sections) {
        auto* br = dynamic_cast<Branch*>(s.Body);
        if (br && br->TargetBlock)
            maxTargetOffset = std::max(maxTargetOffset,
                                       static_cast<long long>(br->TargetBlock->StartILOffset));
    }
    return static_cast<long long>(breakBlock->StartILOffset) >= maxTargetOffset;
}

void SwitchDetection::AnalyzeControlFlow(
        std::vector<FlowAnalysis::ControlFlowNode*>& flowNodes,
        std::vector<FlowAnalysis::ControlFlowNode*>& caseNodes) {
    if (!controlFlowGraph_)
        controlFlowGraph_ = std::make_unique<ControlFlowGraph>(currentContainer_);
    auto* switchHead = controlFlowGraph_->GetNode(analysis_.RootBlock);
    loopContext_ = std::make_unique<LoopContext>(*controlFlowGraph_, switchHead);
    flowNodes.clear();
    flowNodes.push_back(switchHead);
    for (Block* ib : analysis_.InnerBlocks) {
        if (auto* n = controlFlowGraph_->GetNode(ib)) flowNodes.push_back(n);
    }
    caseNodes.clear();
    for (auto& s : analysis_.Sections) {
        auto* br = dynamic_cast<Branch*>(s.Body);
        if (!br || !br->TargetBlock) continue;
        Block* target = br->TargetBlock;
        if (target->Parent == currentContainer_) {
            auto* node = controlFlowGraph_->GetNode(target);
            if (node && !loopContext_->MatchContinue(node)) caseNodes.push_back(node);
        }
    }
    // AddNullCase is deferred (needs NullableLiftingTransform.MatchHasValueCall).
}

bool SwitchDetection::SwitchUsesGoto(
        const std::vector<FlowAnalysis::ControlFlowNode*>& flowNodes,
        const std::vector<FlowAnalysis::ControlFlowNode*>& caseNodes,
        Block*& breakBlock) {
    breakBlock = nullptr;
    // A case with a predecessor outside the switch logic must be a "goto case"
    // or a single "break;". More than one such external case requires gotos.
    std::vector<FlowAnalysis::ControlFlowNode*> externalCases;
    for (auto* c : caseNodes) {
        for (auto* p : c->Predecessors) {
            if (std::find(flowNodes.begin(), flowNodes.end(), p) == flowNodes.end()) {
                externalCases.push_back(c);
                break;
            }
        }
    }
    if (externalCases.size() > 1) return true;
    // Break targets: successors leaving the case-node subtrees that are not
    // depth-1 continues, excluding the external cases.
    std::unordered_set<FlowAnalysis::ControlFlowNode*> externalSet(
        externalCases.begin(), externalCases.end());
    std::unordered_set<FlowAnalysis::ControlFlowNode*> breakTargets;
    for (auto* n : caseNodes) {
        if (externalSet.count(n)) continue;
        for (auto* bt : loopContext_->GetBreakTargets(n)) breakTargets.insert(bt);
    }
    if (breakTargets.size() != 1) return breakTargets.size() > 1;
    breakBlock = static_cast<Block*>((*breakTargets.begin())->UserData);
    if (externalCases.size() == 1)
        return breakBlock != static_cast<Block*>(externalCases[0]->UserData);
    return false;
}

bool SwitchDetection::IsSingleCondition(
        const std::vector<FlowAnalysis::ControlFlowNode*>& flowNodes,
        const std::vector<FlowAnalysis::ControlFlowNode*>& caseNodes) {
    if (flowNodes.size() == 1) return true;
    auto* rootNode = controlFlowGraph_->GetNode(analysis_.RootBlock);
    if (!rootNode) return false;
    rootNode->Visited = true;
    auto* n = rootNode;
    while (n->Successors.size() > 0 && (n == rootNode || IsFlowNode(n))) {
        if (n->Successors.size() == 1) {
            if (caseNodes.size() > 1) break;
            n = n->Successors[0];
        } else {
            if (IsShortCircuit(n, 0)) n = n->Successors[0];
            else if (IsShortCircuit(n, 1)) n = n->Successors[1];
            else break;
        }
        n->Visited = true;
        if (loopContext_->MatchContinue(n)) break;
    }
    bool ret = true;
    for (auto* f : flowNodes) if (!f->Visited) { ret = false; break; }
    for (const auto& node : controlFlowGraph_->Nodes()) node->Visited = false;
    return ret;
}

bool SwitchDetection::IsFlowNode(FlowAnalysis::ControlFlowNode* n) {
    if (!n) return false;
    auto* block = static_cast<Block*>(n->UserData);
    // The C# checks `Instructions.FirstOrDefault() is IfInstruction`: a block
    // whose first instruction is an if (a pure condition test). In this port's
    // block model the if is the FinalInstruction, so the equivalent is an empty
    // Instructions list with an IfInstruction final.
    return block && block->Instructions.empty() && block->FinalInstruction &&
           block->FinalInstruction->Op == OpCode::IfInstruction;
}

bool SwitchDetection::IsShortCircuit(FlowAnalysis::ControlFlowNode* parent, int side) {
    if (!parent || parent->Successors.size() < 2) return false;
    auto* node = parent->Successors[side];
    auto* sibling = parent->Successors[side ^ 1];
    if (!IsFlowNode(node) || node->Successors.size() > 2 || node->Predecessors.size() != 1)
        return false;
    return std::find(node->Successors.begin(), node->Successors.end(), sibling) !=
           node->Successors.end();
}

} // namespace ILSpy::Decompiler::IL
