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

#include "Decompiler/IL/BlockBuilder.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"

#include <algorithm>

namespace ILSpy::Decompiler::IL {

using Metadata::ExceptionHandlerKind;

BlockBuilder::BlockBuilder(Util::Span<const Metadata::ExceptionHandlerClause> handlers,
                           const std::map<std::uint32_t, ILVariablePtr>& exceptionVarByHandlerOffset,
                           std::uint32_t codeSize)
    : handlers_(handlers), exceptionVarByHandlerOffset_(exceptionVarByHandlerOffset),
      codeSize_(codeSize) {}

void BlockBuilder::CreateContainerStructure() {
    // Group catch/filter clauses that share a try range into one TryCatch
    // (keyed by the [tryStart, tryEnd) range); finally/fault clauses each get
    // their own TryFinally/TryFault.
    std::vector<TryCatch*> tryCatches;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> tryCatchRanges;

    for (const auto& eh : handlers_) {
        std::uint32_t tryStart = eh.TryOffset;
        std::uint32_t tryEnd = eh.TryOffset + eh.TryLength;
        std::uint32_t handlerStart = eh.HandlerOffset;
        std::uint32_t handlerEnd = eh.HandlerOffset + eh.HandlerLength;

        auto handlerContainer = std::make_unique<BlockContainer>();
        containerRange_[handlerContainer.get()] = {handlerStart, handlerEnd};
        if (regionContainerByStart_.find(handlerStart) == regionContainerByStart_.end())
            regionContainerByStart_[handlerStart] = handlerContainer.get();

        if (eh.Kind == ExceptionHandlerKind::Finally || eh.Kind == ExceptionHandlerKind::Fault) {
            auto tryContainer = std::make_unique<BlockContainer>();
            containerRange_[tryContainer.get()] = {tryStart, tryEnd};
            TryEntry entry;
            entry.tryContainer = tryContainer.get();
            entry.tryStart = tryStart;
            entry.tryEnd = tryEnd;
            if (eh.Kind == ExceptionHandlerKind::Finally)
                entry.inst = std::make_unique<TryFinally>(std::move(tryContainer),
                                                          std::move(handlerContainer));
            else
                entry.inst = std::make_unique<TryFault>(std::move(tryContainer),
                                                        std::move(handlerContainer));
            tryList_.push_back(std::move(entry));
            continue;
        }

        // Catch / Filter: find or create the TryCatch for this try range.
        TryCatch* tryCatch = nullptr;
        for (std::size_t i = 0; i < tryCatches.size(); ++i) {
            if (tryCatchRanges[i].first == tryStart && tryCatchRanges[i].second == tryEnd) {
                tryCatch = tryCatches[i];
                break;
            }
        }
        if (!tryCatch) {
            auto tryContainer = std::make_unique<BlockContainer>();
            containerRange_[tryContainer.get()] = {tryStart, tryEnd};
            auto tc = std::make_unique<TryCatch>(std::move(tryContainer));
            tryCatch = tc.get();
            tryCatches.push_back(tryCatch);
            tryCatchRanges.push_back({tryStart, tryEnd});
            TryEntry entry;
            entry.inst = std::move(tc);
            // The try container now lives inside the TryCatch; fetch it back.
            entry.tryContainer = static_cast<BlockContainer*>(entry.inst->GetChild(0));
            entry.tryStart = tryStart;
            entry.tryEnd = tryEnd;
            tryList_.push_back(std::move(entry));
        }

        std::unique_ptr<ILInstruction> filter;
        if (eh.Kind == ExceptionHandlerKind::Filter) {
            // The filter region is [FilterOffset, HandlerOffset); its leave value
            // (endfilter) decides whether the handler runs.
            auto filterContainer = std::make_unique<BlockContainer>();
            containerRange_[filterContainer.get()] = {eh.ClassTokenOrFilterOffset, handlerStart};
            if (regionContainerByStart_.find(eh.ClassTokenOrFilterOffset) ==
                regionContainerByStart_.end())
                regionContainerByStart_[eh.ClassTokenOrFilterOffset] = filterContainer.get();
            filter = std::move(filterContainer);
        } else {
            // A plain catch is a constant-true filter.
            filter = std::make_unique<LdcI4>(1);
        }

        ILVariablePtr ehVar;
        auto varIt = exceptionVarByHandlerOffset_.find(handlerStart);
        if (varIt != exceptionVarByHandlerOffset_.end()) ehVar = varIt->second;
        tryCatch->AddHandler(std::make_unique<TryCatchHandler>(
            std::move(filter), std::move(handlerContainer), std::move(ehVar)));
    }

    // Outermost try first for equal start offsets (wider range first).
    std::sort(tryList_.begin(), tryList_.end(), [](const TryEntry& a, const TryEntry& b) {
        if (a.tryStart != b.tryStart) return a.tryStart < b.tryStart;
        return a.tryEnd > b.tryEnd;
    });
}

void BlockBuilder::CreateBlocks(
        BlockContainer& mainContainer,
        std::vector<std::pair<std::uint32_t, std::unique_ptr<Block>>>& blocks) {
    CreateContainerStructure();
    containerRange_[&mainContainer] = {0, codeSize_};

    std::sort(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::vector<BlockContainer*> containerStack;  // ancestors of `current`
    BlockContainer* current = &mainContainer;
    std::size_t tryIdx = 0;

    for (auto& [start, block] : blocks) {
        if (!block) continue;
        block->StartILOffset = start;

        // Leave nested containers whose range this block is past.
        while (!containerStack.empty() &&
               start >= containerRange_[current].second) {
            current = containerStack.back();
            containerStack.pop_back();
        }
        // Enter a handler/filter container starting here.
        auto hit = regionContainerByStart_.find(start);
        if (hit != regionContainerByStart_.end()) {
            containerStack.push_back(current);
            current = hit->second;
        }
        // Enter try region(s) starting here; each nests the try instruction in
        // a wrapper block of the enclosing container.
        while (tryIdx < tryList_.size() && start == tryList_[tryIdx].tryStart) {
            auto wrapper = std::make_unique<Block>();
            wrapper->StartILOffset = start;
            wrapper->Add(std::move(tryList_[tryIdx].inst));
            wrapperBlocks_.push_back({wrapper.get(), current});
            current->AddBlock(std::move(wrapper));
            containerStack.push_back(current);
            current = tryList_[tryIdx].tryContainer;
            ++tryIdx;
        }
        current->AddBlock(std::move(block));
    }

    WireWrapperBlockTerminators(mainContainer);
    std::vector<BlockContainer*> stack;
    AssignLeaveTargets(&mainContainer, stack);
    wrapperBlocks_.clear();
}

void BlockBuilder::WireWrapperBlockTerminators(BlockContainer& mainContainer) {
    // A wrapper block holds only its TryInstruction. The C# leaves it
    // unterminated (BlockBuilder appends a noted InvalidBranch that transforms
    // later discard); our Block model requires every block to end in control
    // flow, so wire the fall-through explicitly: branch to the following block
    // in the same container, or Leave(main) if the region ends the method.
    for (auto& [wrapper, container] : wrapperBlocks_) {
        if (wrapper->FinalInstruction) continue;
        auto& siblings = container->Blocks;
        for (std::size_t i = 0; i < siblings.size(); ++i) {
            if (siblings[i].get() != wrapper) continue;
            if (i + 1 < siblings.size()) {
                wrapper->SetFinal(std::make_unique<Branch>(siblings[i + 1].get()));
            } else {
                wrapper->SetFinal(std::make_unique<Leave>(&mainContainer));
            }
            break;
        }
    }
}

void BlockBuilder::AssignLeaveTargets(ILInstruction* inst,
                                      std::vector<BlockContainer*>& containerStack) {
    if (!inst) return;
    if (auto* container = dynamic_cast<BlockContainer*>(inst)) {
        containerStack.push_back(container);
        for (int i = 0; i < container->ChildCount(); ++i)
            AssignLeaveTargets(container->GetChild(i), containerStack);
        containerStack.pop_back();
        return;
    }
    // endfinally/endfilter decode as Leave with a null target; the target is
    // the innermost enclosing finally/filter container (the C# peeks its
    // containerStack in ConnectBranches the same way).
    if (auto* leave = dynamic_cast<Leave*>(inst)) {
        if (!leave->TargetContainer && !containerStack.empty())
            leave->TargetContainer = containerStack.back();
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        AssignLeaveTargets(inst->GetChild(i), containerStack);
}

} // namespace ILSpy::Decompiler::IL
