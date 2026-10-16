// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/FlowAnalysis/ReachingDefinitionsVisitor.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace ILSpy::Decompiler::IL {

namespace {

// The store-carrying node kinds (the C# MayWriteLocals filter over
// IInstructionWithVariableOperand: StLoc / MatchInstruction /
// TryCatchHandler / UsingInstruction).
bool IsStoreNode(const ILInstruction* inst) {
    switch (inst->Op) {
        case OpCode::StLoc:
        case OpCode::MatchInstruction:
        case OpCode::TryCatchHandler:
        case OpCode::UsingInstruction:
            return true;
        default:
            return false;
    }
}

// The store's target variable (the C#
// `((IInstructionWithVariableOperand)inst).Variable`).
ILVariable* StoreVariable(ILInstruction* inst) {
    switch (inst->Op) {
        case OpCode::StLoc:
            return static_cast<StLoc*>(inst)->Variable.get();
        case OpCode::MatchInstruction:
            return static_cast<MatchInstruction*>(inst)->Variable.get();
        default:
            return nullptr;
    }
}

} // namespace

ReachingDefinitionsVisitor::ReachingDefinitionsVisitor(
    ILFunction& scope, const std::function<bool(const ILVariable*)>& isAnalyzed)
    : scope_(&scope), isAnalyzed_(isAnalyzed) {}

bool ReachingDefinitionsVisitor::IsAnalyzedVariable(const ILVariable* v) const {
    // The port's ILVariable carries no Function back-pointer (the C#
    // ILVariable.Function); the scope membership is the identity check.
    if (v == nullptr) return false;
    for (const auto& owned : scope_->Variables) {
        if (owned.get() == v) return isAnalyzed_(v);
    }
    return false;
}

bool ReachingDefinitionsVisitor::Analyze() {
    // Fill the store tables (the C# FindAllStoresByVariable walk): a
    // contiguous index segment per analyzed variable, preceded by the
    // uninitialized-entry slot.
    std::vector<std::vector<ILInstruction*>> storesByVariable;
    storesByVariable.resize(scope_->Variables.size());
    for (std::size_t vi = 0; vi < scope_->Variables.size(); ++vi) {
        const ILVariable* v = scope_->Variables[vi].get();
        if (v != nullptr && isAnalyzed_(v)) storesByVariable[vi].push_back(nullptr);
    }
    struct CollectFrame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<CollectFrame> collect;
    collect.push_back({scope_->Body.get(), 0});
    while (!collect.empty()) {
        CollectFrame& frame = collect.back();
        if (frame.nextChild >= frame.node->ChildCount()) {
            collect.pop_back();
            continue;
        }
        ILInstruction* child = frame.node->GetChild(frame.nextChild);
        frame.nextChild++;
        if (child == nullptr) continue;
        if (IsStoreNode(child)) {
            ILVariable* v = StoreVariable(child);
            if (v != nullptr) {
                for (std::size_t vi = 0; vi < scope_->Variables.size(); ++vi) {
                    if (scope_->Variables[vi].get() == v &&
                        !storesByVariable[vi].empty()) {
                        storesByVariable[vi].push_back(child);
                        break;
                    }
                }
            }
        }
        collect.push_back({child, 0});
    }
    int si = kFirstStoreIndex;
    firstStoreIndexForVariable_.assign(scope_->Variables.size() + 1, si);
    for (std::size_t vi = 0; vi < storesByVariable.size(); ++vi) {
        firstStoreIndexForVariable_[vi] = si;
        const auto& stores = storesByVariable[vi];
        if (!stores.empty()) {
            for (std::size_t i = 1; i < stores.size(); ++i)
                storeIndexMap_.emplace(stores[i], si + static_cast<int>(i));
            ILVariable* key = scope_->Variables[vi].get();
            storesByVariable_.emplace(key, stores);
            variableIndex_.emplace(key, static_cast<int>(vi));
            si += static_cast<int>(stores.size());
        }
    }
    firstStoreIndexForVariable_[scope_->Variables.size()] = si;

    // The initial state: reachable + the per-variable uninitialized bits.
    State initialState(si);
    initialState.bits.Set(kReachableBit);
    for (std::size_t vi = 0; vi < storesByVariable.size(); ++vi) {
        if (!storesByVariable[vi].empty()) {
            initialState.bits.Set(firstStoreIndexForVariable_[vi]);
        }
    }
    bottomState = State(si);
    bottomState.bits.ClearAll();
    currentStateOnException = initialState.Clone();
    state = initialState.Clone();

    Visit(scope_->Body.get());
    return !unsupported_;
}

void ReachingDefinitionsVisitor::VisitDefault(ILInstruction* inst) {
    // The C# Default: evaluate the children left-to-right (normal control
    // flow assumed).
    for (int i = 0; i < inst->ChildCount(); ++i) {
        ILInstruction* child = inst->GetChild(i);
        if (child != nullptr) Visit(child);
    }
}

void ReachingDefinitionsVisitor::Visit(ILInstruction* inst) {
    if (inst == nullptr || unsupported_) return;
    switch (inst->Op) {
        case OpCode::BlockContainer: {
            VisitBlockContainerBlock(inst);
            break;
        }
        case OpCode::Block: {
            // The C# Block visit is the Default walk (the block's final
            // Branch/Leave dispatches through its own Visit).
            VisitDefault(inst);
            break;
        }
        case OpCode::IfInstruction: {
            VisitIfInstruction(inst);
            break;
        }
        case OpCode::Branch: {
            VisitBranch(inst);
            break;
        }
        case OpCode::Leave: {
            VisitLeave(inst);
            break;
        }
        case OpCode::StLoc: {
            auto* stloc = static_cast<StLoc*>(inst);
            if (stloc->Value != nullptr) Visit(stloc->Value.get());
            HandleStore(stloc, stloc->Variable.get());
            break;
        }
        case OpCode::MatchInstruction: {
            EvaluateMatch(inst);
            break;
        }
        case OpCode::TryCatch: {
            // The C# VisitTryCatch (DataFlowVisitor.cs lines 533-551): the
            // try block is visited through HandleTryBlock (the exceptional
            // path seeds the handler inputs -- the state at an arbitrary
            // point inside the try), then each handler is visited with the
            // on-exception state and the endpoints join.
            VisitTryCatchBlocks(inst);
            break;
        }
        case OpCode::TryCatchHandler: {
            VisitDefault(inst);
            break;
        }
        case OpCode::SwitchInstruction: {
            // The C# VisitSwitchInstruction (DataFlowVisitor.cs): the value
            // is visited once, then EVERY section starts from the same
            // before-sections state (each section's terminator marks the
            // state unreachable; without the restore only the first
            // section's successors would receive flow).
            auto* sw = static_cast<SwitchInstruction*>(inst);
            if (sw->Value != nullptr) Visit(sw->Value.get());
            State beforeSections = state.Clone();
            if (!sw->Sections.empty()) {
                Visit(sw->Sections[0].get());
                State afterSections = state.Clone();
                for (std::size_t i = 1; i < sw->Sections.size(); ++i) {
                    state.ReplaceWith(beforeSections);
                    Visit(sw->Sections[i].get());
                    afterSections.JoinWith(state);
                }
                state = std::move(afterSections);
            }
            break;
        }
        case OpCode::LdLoc: {
            OnLdLoc(static_cast<LdLoc*>(inst));
            break;
        }
        case OpCode::LdLoca: {
            OnLdLoca(static_cast<LdLoca*>(inst));
            break;
        }
        default: {
            VisitDefault(inst);
            break;
        }
    }
}

void ReachingDefinitionsVisitor::VisitIfInstruction(ILInstruction* inst) {
    auto* ifInst = static_cast<IfInstruction*>(inst);
    // The C# VisitIfInstruction: EvaluateCondition splits the condition's
    // true/false states (the nested-if recursion), then each branch is
    // visited with its state and the two outcomes join.
    auto before = EvaluateCondition(ifInst->Condition.get());
    state = std::move(before.first);
    if (ifInst->TrueInst != nullptr) Visit(ifInst->TrueInst.get());
    State afterTrueState = state;
    state = std::move(before.second);
    if (ifInst->FalseInst != nullptr) Visit(ifInst->FalseInst.get());
    state.JoinWith(afterTrueState);
}

std::pair<ReachingDefinitionsVisitor::State, ReachingDefinitionsVisitor::State>
ReachingDefinitionsVisitor::EvaluateCondition(ILInstruction* inst) {
    if (inst == nullptr) return {state, state};
    if (inst->Op == OpCode::IfInstruction) {
        // The C# nested-condition case (the `a && b` / `a || b` sugar
        // shapes): split both sub-conditions and recombine the arms.
        auto* ifInst = static_cast<IfInstruction*>(inst);
        auto before = EvaluateCondition(ifInst->Condition.get());
        state = std::move(before.first);
        auto afterThen = EvaluateCondition(ifInst->TrueInst.get());
        state = std::move(before.second);
        auto afterElse = EvaluateCondition(ifInst->FalseInst.get());
        State onTrue = std::move(afterThen.first);
        onTrue.JoinWith(afterElse.first);
        State onFalse = std::move(afterThen.second);
        onFalse.JoinWith(afterElse.second);
        return {std::move(onTrue), std::move(onFalse)};
    }
    if (inst->Op == OpCode::LdcI4) {
        // A constant condition: only one side is reachable.
        auto* constant = static_cast<const LdcI4*>(inst);
        if (constant->Value == 0)
            return {bottomState.Clone(), std::move(state)};
        return {std::move(state), bottomState.Clone()};
    }
    if (inst->Op == OpCode::MatchInstruction) {
        // The C# MatchInstruction case: EvaluateMatch returns the
        // (onMatch, onFail) pair. The port reuses the walk's EvaluateMatch
        // (which drives the HandleStore via the hook) and clones the pair.
        EvaluateMatch(inst);
        return {state, state};
    }
    // Any other condition: visit once; both sides start equal (the C#
    // `inst.AcceptVisitor(this); return (state, state.Clone())`).
    Visit(inst);
    return {state, state};
}

void ReachingDefinitionsVisitor::EvaluateMatch(ILInstruction* inst) {
    auto* match = static_cast<MatchInstruction*>(inst);
    if (match->TestedOperand != nullptr) Visit(match->TestedOperand.get());
    // The port's sub-pattern evaluation walks the patterns as plain
    // expressions (the C# splits each sub-pattern's match/fail states; the
    // port bails on the compound shapes the PatternMatchingTransform
    // produces -- the PatternLocal stores stay conservative).
    if (!match->SubPatterns.empty() || match->CheckType || match->CheckNotNull) {
        unsupported_ = true;
    }
    HandleStore(match, match->Variable.get());
}

void ReachingDefinitionsVisitor::VisitBranch(ILInstruction* inst) {
    auto* branch = static_cast<Branch*>(inst);
    Block* target = branch->TargetBlock;
    if (target == nullptr) {
        // The reader-built target-less branch (an unresolved dump): treat
        // the walk as unsupported (the C# Branch always resolves).
        unsupported_ = true;
    } else {
        State& targetState = GetBlockInputState(target);
        int actual = -1;
        if (auto* c = dynamic_cast<BlockContainer*>(target->Parent)) {
            for (std::size_t k = 0; k < c->Blocks.size(); k++)
                if (c->Blocks[k].get() == target) actual = static_cast<int>(k);
        }
        if (!state.LessThanOrEqual(targetState)) {
            targetState.JoinWith(state);
            auto wl = blockWorklist_.find(target->Parent);
            if (wl != blockWorklist_.end())
                wl->second->insert(target->ChildIndex);
        }
    }
    MarkUnreachable();
}

void ReachingDefinitionsVisitor::VisitLeave(ILInstruction* inst) {
    auto* leave = static_cast<Leave*>(inst);
    if (leave->Value != nullptr) Visit(leave->Value.get());
    if (leave->TargetContainer != nullptr) {
        auto it = stateOnLeave_.find(leave->TargetContainer);
        if (it != stateOnLeave_.end()) {
            it->second.JoinWith(state);
        } else {
            stateOnLeave_.emplace(leave->TargetContainer, state.Clone());
        }
    } else {
        unsupported_ = true;
    }
    MarkUnreachable();
}

void ReachingDefinitionsVisitor::HandleStore(ILInstruction* inst,
                                             ILVariable* v) {
    if (v == nullptr) return;
    auto idxIt = variableIndex_.find(v);
    if (idxIt == variableIndex_.end()) return;  // not an analyzed variable
    const int first = firstStoreIndexForVariable_[idxIt->second];
    if (!state.IsReachable()) return;
    // Kill the variable's store bits; the segment end is this store's own
    // index range (the store tables hold exactly this variable's stores).
    auto storesIt = storesByVariable_.find(v);
    const int segmentEnd =
        storesIt != storesByVariable_.end()
            ? first + static_cast<int>(storesIt->second.size())
            : first;
    state.KillStores(first, segmentEnd);
    auto siIt = storeIndexMap_.find(inst);
    assert(siIt != storeIndexMap_.end() && "HandleStore: store not indexed");
    if (siIt == storeIndexMap_.end()) return;
    state.SetStore(siIt->second);
    // The C# comment: state is <= currentStateOnException here, so adding
    // the single store bit is the PropagateStateOnException fold.
    currentStateOnException.bits.Set(siIt->second);
}

std::vector<ILInstruction*> ReachingDefinitionsVisitor::GetStoresFor(
    const State& atState, const ILVariable* v) {
    std::vector<ILInstruction*> result;
    if (v == nullptr) return result;
    auto idxIt = variableIndex_.find(const_cast<ILVariable*>(v));
    if (idxIt == variableIndex_.end()) return result;
    auto it = storesByVariable_.find(const_cast<ILVariable*>(v));
    if (it == storesByVariable_.end()) return result;
    const auto& segment = it->second;
    const int first = firstStoreIndexForVariable_[idxIt->second];
    int startIndex = first + 1;
    const int endIndex = first + static_cast<int>(segment.size());
    while (startIndex < endIndex) {
        int next = atState.bits.NextSetBit(startIndex, endIndex);
        if (next < 0) break;
        result.push_back(segment[static_cast<std::size_t>(next - first)]);
        startIndex = next + 1;
    }
    return result;
}

bool ReachingDefinitionsVisitor::IsPotentiallyUninitialized(
    const State& atState, const ILVariable* v) {
    if (v == nullptr) return false;
    auto idxIt = variableIndex_.find(const_cast<ILVariable*>(v));
    if (idxIt == variableIndex_.end()) return false;
    return atState.bits[firstStoreIndexForVariable_[idxIt->second]];
}

ReachingDefinitionsVisitor::State&
ReachingDefinitionsVisitor::GetBlockInputState(Block* block) {
    auto it = stateOnBranch_.find(block);
    if (it != stateOnBranch_.end()) return it->second;
    // Absent entries start at the bottom state (the correct bit-set
    // capacity -- the C# GetBlockInputState clones bottomState).
    return stateOnBranch_.emplace(block, bottomState.Clone()).first->second;
}

void ReachingDefinitionsVisitor::VisitTryCatchBlocks(ILInstruction* inst) {
    // The C# HandleTryBlock (DataFlowVisitor.cs lines 504-530): the try
    // block's visit writes the incoming state into the per-try
    // stateOnException slot (an exception can be thrown at any point inside
    // the try, so every store in it is visible to the handlers); the saved
    // outer slot is joined with the new one after the visit ("an async
    // exception can be thrown immediately in the handler block").
    auto* tryCatch = static_cast<IL::TryCatch*>(inst);
    State* newStateOnException = nullptr;
    auto it = stateOnException_.find(tryCatch);
    if (it != stateOnException_.end()) {
        newStateOnException = &it->second;
        newStateOnException->JoinWith(state);
    } else {
        newStateOnException =
            &stateOnException_.emplace(tryCatch, state.Clone()).first->second;
    }
    State* oldStateOnException = currentStateOnException_;
    currentStateOnException_ = newStateOnException;
    Visit(tryCatch->TryBlock.get());
    currentStateOnException_ = oldStateOnException;
    if (oldStateOnException != nullptr)
        oldStateOnException->JoinWith(*newStateOnException);
    State onException = newStateOnException->Clone();
    // The C# VisitTryCatch (lines 533-551): each handler visits with the
    // on-exception state (the filter first, its mutations joined back in
    // case the filter ran), and the endpoints join into the exit state.
    State endpoint = state.Clone();
    for (auto& handler : tryCatch->Handlers) {
        if (handler == nullptr) continue;
        state.ReplaceWith(onException);
        if (handler->Filter != nullptr) Visit(handler->Filter.get());
        onException.JoinWith(state);
        if (handler->Body != nullptr) Visit(handler->Body.get());
        endpoint.JoinWith(state);
    }
    state = std::move(endpoint);
}
void ReachingDefinitionsVisitor::VisitBlockContainerBlock(
    ILInstruction* inst) {
    auto* container = static_cast<BlockContainer*>(inst);
    std::set<int> worklist;
    blockWorklist_[container] = &worklist;
    // The entry block starts with the incoming state.
    Block* entry = container->EntryPoint();
    State& entryState = GetBlockInputState(entry);
    if (!state.LessThanOrEqual(entryState)) {
        entryState.JoinWith(state);
        worklist.insert(0);
    }
    while (!worklist.empty() && !unsupported_) {
        const int blockIndex = *worklist.begin();
        worklist.erase(worklist.begin());
        Block* block = container->Blocks[static_cast<std::size_t>(blockIndex)].get();
        state = GetBlockInputState(block);
        Visit(block);
        // The port's reader leaves an if-as-final without a false arm (the
        // implicit fall-through), so a state still flowing past the block
        // reaches the next block in the container. The C# reader materializes
        // a Branch to the next block there, and its VisitBranch performs
        // exactly this join; the explicit terminators mark the state
        // unreachable, so only the implicit fall-through reaches here.
        if (state.IsReachable() &&
            blockIndex + 1 < static_cast<int>(container->Blocks.size())) {
            Block* next = container->Blocks[static_cast<std::size_t>(
                                             blockIndex + 1)]
                              .get();
            State& nextState = GetBlockInputState(next);
            if (!state.LessThanOrEqual(nextState)) {
                nextState.JoinWith(state);
                worklist.insert(blockIndex + 1);
            }
        }
    }
    blockWorklist_.erase(container);
    // The container's exit state: the joined leaves, or unreachable.
    auto it = stateOnLeave_.find(container);
    if (it != stateOnLeave_.end()) {
        state = it->second;
    } else {
        MarkUnreachable();
    }
}

} // namespace ILSpy::Decompiler::IL
