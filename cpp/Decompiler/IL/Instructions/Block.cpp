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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Block.cpp -- the out-of-line Block members that would otherwise drag the
// Disassembler include graph into every IL TU (Label is the DisassemblerHelpers
// OffsetToString of the block's own start offset), plus the out-of-line
// BlockContainer loop-shape matchers (MatchConditionBlock / MatchIncrementBlock
// -- their bodies call the shared PatternMatching.hpp matchers, whose include
// chain reaches BlockContainer.hpp through Branch.hpp, so in-class definitions
// would recurse the include guards).

#include "Decompiler/IL/Instructions/Block.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/PatternMatching.hpp"

#include <algorithm>

namespace ILSpy::Decompiler::IL {

std::string Block::Label() const {
    return Disassembler::OffsetToString(static_cast<int>(StartILOffset));
}

bool Block::MatchInlineAssignBlock(ILInstruction*& call,
                                    ILInstruction*& value) const {
    call = nullptr;
    value = nullptr;
    if (Kind != BlockKind::CallInlineAssign)
        return false;
    if (Instructions.size() != 1)
        return false;
    call = Instructions[0].get();
    auto* callInstruction = dynamic_cast<Call*>(call);
    if (callInstruction == nullptr || callInstruction->Arguments.empty())
        return false;
    ILVariable* tmp = nullptr;
    ILInstruction* storedValue = nullptr;
    if (!MatchStLoc(callInstruction->Arguments.back().get(), tmp)
        || !MatchStLoc(callInstruction->Arguments.back().get(), tmp, storedValue))
        return false;
    if (!(tmp->IsSingleDefinition() && tmp->LoadCount == 1))
        return false;
    value = storedValue;
    return MatchLdLoc(FinalInstruction.get(), tmp);
}

bool BlockContainer::MatchConditionBlock(Block* block, ILInstruction*& condition,
                                          Block*& bodyStartBlock) {
    condition = nullptr;
    bodyStartBlock = nullptr;
    if (block->Instructions.size() != 1)
        return false;
    ILInstruction* cond = nullptr;
    ILInstruction* trueInst = nullptr;
    ILInstruction* falseInst = nullptr;
    if (!MatchIfInstruction(block->Instructions[0].get(), cond, trueInst, falseInst))
        return false;
    condition = cond;
    return MatchLeave(falseInst, this) && MatchBranch(trueInst, bodyStartBlock);
}

bool BlockContainer::MatchIncrementBlock(Block* block) {
    if (block->Instructions.empty())
        return false;
    if (!MatchBranch(block->Instructions.back().get(), EntryPoint()))
        return false;
    return true;
}

// The C# TopologicalSort successor local (BlockContainer.cs lines 308-316):
// every Branch descendant of the block whose target block's parent is this
// container, in tree order.
static void CollectBranchTargetsIn(const Block* block,
                                   const BlockContainer* container,
                                   std::vector<Block*>& successors) {
    // The port's terminator convention carries the branch in the
    // FinalInstruction slot; the walk covers both lists.
    std::vector<const ILInstruction*> stack;
    auto push = [&stack](const ILInstruction* inst) {
        if (inst != nullptr) stack.push_back(inst);
    };
    for (auto& inst : block->Instructions) push(inst.get());
    push(block->FinalInstruction.get());
    while (!stack.empty()) {
        const ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* branch = dynamic_cast<const Branch*>(node)) {
            if (branch->TargetBlock != nullptr &&
                branch->TargetBlock->Parent ==
                    const_cast<BlockContainer*>(container))
                successors.push_back(branch->TargetBlock);
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (const ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
}

std::vector<Block*> BlockContainer::TopologicalSort(
    bool deleteUnreachableBlocks) const {
    // Visit blocks in post-order from the entry (the C#
    // GraphTraversal.DepthFirstSearch with reverseSuccessors: true -- a
    // non-recursive DFS whose worklist pushes the successors in tree order
    // and pops them last-first).
    std::vector<bool> visited(Blocks.size(), false);
    std::vector<Block*> postOrder;
    if (Blocks.empty())
        return postOrder;
    std::vector<std::pair<Block*, bool>> worklist;
    worklist.push_back({Blocks.front().get(), false});
    while (!worklist.empty()) {
        Block* node = worklist.back().first;
        bool isPostOrderContinuation = worklist.back().second;
        worklist.pop_back();
        if (isPostOrderContinuation) {
            postOrder.push_back(node);
            continue;
        }
        if (node->ChildIndex < 0 ||
            static_cast<std::size_t>(node->ChildIndex) >= visited.size() ||
            visited[static_cast<std::size_t>(node->ChildIndex)])
            continue;
        visited[static_cast<std::size_t>(node->ChildIndex)] = true;
        worklist.push_back({node, true});
        std::vector<Block*> successors;
        CollectBranchTargetsIn(node, this, successors);
        for (Block* child : successors)
            worklist.push_back({child, false});
    }
    std::reverse(postOrder.begin(), postOrder.end());
    if (!deleteUnreachableBlocks) {
        for (std::size_t i = 0; i < Blocks.size(); i++) {
            if (!visited[i])
                postOrder.push_back(Blocks[i].get());
        }
    }
    return postOrder;
}

void BlockContainer::SortBlocks(bool deleteUnreachableBlocks) {
    if (Blocks.size() < 2)
        return;
    std::vector<Block*> newOrder = TopologicalSort(deleteUnreachableBlocks);
    // Re-home the blocks in the new order (the C# Blocks.ReplaceList keeps
    // the same Block objects and renumbers).
    std::vector<std::unique_ptr<Block>> reordered;
    reordered.reserve(newOrder.size());
    for (Block* block : newOrder) {
        for (auto& owned : Blocks) {
            if (owned.get() == block) {
                reordered.push_back(std::move(owned));
                break;
            }
        }
    }
    Blocks = std::move(reordered);
    for (std::size_t i = 0; i < Blocks.size(); i++) {
        Blocks[i]->Parent = this;
        Blocks[i]->ChildIndex = static_cast<int>(i);
    }
}

}  // namespace ILSpy::Decompiler::IL
