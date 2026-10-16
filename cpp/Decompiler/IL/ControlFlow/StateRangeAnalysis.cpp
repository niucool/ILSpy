// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"

#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"  // StObj
#include "Decompiler/IL/Instructions/LdcConstants.hpp"  // LdcI8/F4/F8 (MatchDefaultOrNullOrZero)
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"  // ILFunction (IsLeavingFunction)
#include "Decompiler/IL/PatternMatching.hpp"

namespace ILSpy::Decompiler::IL::ControlFlow {

StateRangeAnalysis::StateRangeAnalysis(StateRangeAnalysisMode mode,
                                       const TypeSystem::IField* stateField,
                                       ILVariable* cachedStateVar,
                                       bool legacyVisualBasic)
    : mode_(mode), stateField_(stateField),
      legacyVisualBasic_(legacyVisualBasic),
      evalContext_(stateField, legacyVisualBasic) {
    if (cachedStateVar != nullptr)
        evalContext_.AddStateVariable(cachedStateVar);
}

StateRangeAnalysis StateRangeAnalysis::CreateNestedAnalysis() const {
    StateRangeAnalysis sra(mode_, stateField_, /*cachedStateVar=*/nullptr,
                           legacyVisualBasic_);
    sra.doFinallyBodies = doFinallyBodies;
    sra.skipFinallyBodies = skipFinallyBodies;
    for (ILVariable* v : evalContext_.StateVariables()) {
        sra.evalContext_.AddStateVariable(v);
    }
    return sra;
}

Util::LongSet StateRangeAnalysis::AssignStateRanges(
    ILInstruction* inst, Util::LongSet stateRange) {
    if (auto* blockContainer = dynamic_cast<BlockContainer*>(inst)) {
        AddStateRange(blockContainer->EntryPoint(), stateRange);
        for (auto& block : blockContainer->Blocks) {
            // We assume that there are no jumps to blocks already processed.
            // (The C# TODO: is SortBlocks() guaranteeing this, even if the
            // user code has loops?)
            auto it = ranges_.find(block.get());
            if (it != ranges_.end()) {
                Util::LongSet blockRange = it->second;
                AssignStateRanges(block.get(), std::move(blockRange));
            }
        }
        // Since we don't track 'leave' edges, we can only conservatively
        // return LongSet.Empty.
        return Util::LongSet::Empty();
    }
    if (auto* block = dynamic_cast<Block*>(inst)) {
        // The C# iterates `block.Instructions` only -- the C# reader puts
        // the terminators (the dispatch switch, the branches) IN the list.
        // The port's reader convention carries them in the FinalInstruction
        // slot, so the final is processed after the list (the state-range
        // semantics are identical: a terminator consumes the incoming range
        // and returns Empty either way).
        for (auto& instInBlock : block->Instructions) {
            if (stateRange.IsEmpty())
                break;
            Util::LongSet oldStateRange = stateRange;
            stateRange = AssignStateRanges(instInBlock.get(), stateRange);
            // End-point can only be reachable in a subset of the states
            // where the start-point is reachable (the C# Debug.Assert).
            (void)oldStateRange;
        }
        if (block->FinalInstruction != nullptr && !stateRange.IsEmpty()) {
            stateRange =
                AssignStateRanges(block->FinalInstruction.get(), stateRange);
        }
        return stateRange;
    }
    if (auto* tryFinally = dynamic_cast<TryFinally*>(inst)) {
        if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
            Util::LongSet afterTry =
                AssignStateRanges(tryFinally->TryBlock.get(), stateRange);
            // really finally should start with
            // 'stateRange.UnionWith(afterTry)', but that's equal to
            // 'stateRange' (the C# Debug.Assert).
            Util::LongSet afterFinally =
                AssignStateRanges(tryFinally->FinallyBlock.get(), stateRange);
            return afterTry.IntersectWith(afterFinally);
        }
        // The other modes fall through to the default arm.
    }
    if (auto* switchInst = dynamic_cast<SwitchInstruction*>(inst)) {
        SymbolicValue val = evalContext_.Eval(switchInst->Value.get());
        if (val.Type != SymbolicValueType::State) {
            // User code - abort analysis (the C# default arm).
            if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
                
            }
            return Util::LongSet::Empty();
        }
        std::vector<Util::LongInterval> exitIntervals;
        for (auto& section : switchInst->Sections) {
            // switch (state + Constant) matches 'case VALUE:'
            // iff (state + Constant == value)
            // iff (state == value - Constant)
            Util::LongSet effectiveLabels =
                section->Labels.AddOffset(-static_cast<long long>(
                    val.Constant));
            Util::LongSet result = AssignStateRanges(
                section->Body.get(),
                stateRange.IntersectWith(effectiveLabels));
            for (const Util::LongInterval& iv : result.Intervals())
                exitIntervals.push_back(iv);
        }
        // exitIntervals = union of exits of all sections
        return Util::LongSet::FromNormalized(std::move(exitIntervals));
    }
    if (auto* ifInst = dynamic_cast<IfInstruction*>(inst)) {
        SymbolicValue val = evalContext_.Eval(ifInst->Condition.get()).AsBool();
        if (val.Type == SymbolicValueType::StateInSet) {
            Util::LongSet trueRanges = val.ValueSet;
            Util::LongSet afterTrue = AssignStateRanges(
                ifInst->TrueInst.get(),
                stateRange.IntersectWith(trueRanges));
            Util::LongSet afterFalse;
            if (ifInst->FalseInst != nullptr) {
                afterFalse = AssignStateRanges(
                    ifInst->FalseInst.get(),
                    stateRange.ExceptWith(trueRanges));
            } else {
                // The port's reader leaves an if-as-final without a false
                // arm (the implicit fall-through); the C# reader
                // materializes a Branch to the container's next block
                // there, so the false path's range reaches the next block
                // through the same AddStateRange the branch arm performs.
                afterFalse = stateRange.ExceptWith(trueRanges);
                if (auto* parentBlock =
                        dynamic_cast<Block*>(ifInst->Parent)) {
                    auto* parentContainer = dynamic_cast<BlockContainer*>(
                        parentBlock->Parent);
                    if (parentContainer != nullptr) {
                        std::size_t next =
                            static_cast<std::size_t>(parentBlock->ChildIndex) +
                            1;
                        if (next < parentContainer->Blocks.size())
                            AddStateRange(
                                parentContainer->Blocks[next].get(),
                                afterFalse);
                    }
                }
            }
            return afterTrue.UnionWith(afterFalse);
        }
        // Not state-dependent - abort analysis.
        if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
            throw SymbolicAnalysisFailedException(
                "Unexpected instruction in Iterator.Dispose()");
        }
        return Util::LongSet::Empty();
    }
    if (auto* br = dynamic_cast<Branch*>(inst)) {
        AddStateRange(br->TargetBlock, stateRange);
        return Util::LongSet::Empty();
    }
    if (auto* leave = dynamic_cast<Leave*>(inst)) {
        if (mode_ == StateRangeAnalysisMode::AwaitInFinally) {
            AddStateRangeForLeave(leave->TargetContainer, stateRange);
            return Util::LongSet::Empty();
        }
        // The other modes' Leave handling falls through to the default arm.
    }
    if (dynamic_cast<Nop*>(inst) != nullptr) {
        return stateRange;
    }
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        if (stloc->Variable != nullptr &&
            (stloc->Variable.get() == doFinallyBodies ||
             stloc->Variable.get() == skipFinallyBodies)) {
            // pre-roslyn async/await uses a generated 'bool doFinallyBodies';
            // do not treat this as user code. Mono also does this for
            // yield-return.
            return stateRange;
        }
        SymbolicValue val = evalContext_.Eval(stloc->Value.get());
        if (val.Type == SymbolicValueType::State && val.Constant == 0) {
            evalContext_.AddStateVariable(stloc->Variable.get());
            return stateRange;
        }
        // else: user code - abort analysis.
        if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
            throw SymbolicAnalysisFailedException(
                "Unexpected instruction in Iterator.Dispose()");
        }
        return Util::LongSet::Empty();
    }
    if (auto* call = dynamic_cast<Call*>(inst)) {
        if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
            // Call to finally method.
            // Usually these are in finally blocks, but sometimes (e.g.
            // foreach over array), the C# compiler puts the call to a
            // finally method outside the try-finally block.
            const TypeSystem::IMethod* methodDefinition =
                call->Method != nullptr
                    ? dynamic_cast<const TypeSystem::IMethod*>(
                          call->Method->MemberDefinition())
                    : nullptr;
            if (methodDefinition != nullptr) {
                auto it = finallyMethodToStateRange_.find(methodDefinition);
                if (it != finallyMethodToStateRange_.end()) {
                    it->second = it->second.UnionWith(stateRange);
                } else {
                    finallyMethodToStateRange_.emplace(methodDefinition,
                                                       stateRange);
                }
            }
            // return Empty since we executed user code (the finally method)
            return Util::LongSet::Empty();
        }
        // The other modes: user code - abort analysis.
        return Util::LongSet::Empty();
    }
    if (auto* stobj = dynamic_cast<StObj*>(inst)) {
        if (mode_ == StateRangeAnalysisMode::IteratorMoveNext) {
            ILInstruction* target = nullptr;
            const TypeSystem::IField* field = nullptr;
            ILInstruction* value = nullptr;
            if (MatchStFld(stobj, target, field, value) &&
                MatchLdThis(target) && value != nullptr) {
                const TypeSystem::IMember* definition =
                    field != nullptr ? field->MemberDefinition() : nullptr;
                if (definition == stateField_ &&
                    MatchLdcI4(value, -1)) {
                    // Mono resets the state field during MoveNext();
                    // don't consider this user code.
                    return stateRange;
                }
            }
            // else: user code - abort analysis.
            return Util::LongSet::Empty();
        }
        if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
            ILInstruction* target = nullptr;
            const TypeSystem::IField* field = nullptr;
            ILInstruction* value = nullptr;
            if (MatchStFld(stobj, target, field, value) &&
                MatchLdThis(target)) {
                const TypeSystem::IMember* definition =
                    field != nullptr ? field->MemberDefinition() : nullptr;
                if (definition == stateField_ && value != nullptr) {
                    if (MatchLdcI4(value, -2)) {
                        // Roslyn 4.13 sets the state field in Dispose()
                        // to mark the iterator as disposed; don't
                        // consider this user code.
                        return stateRange;
                    }
                }
                if (value != nullptr && MatchDefaultOrNullOrZero(value)) {
                    // Roslyn 4.13 clears any local hoisted local variables
                    // in Dispose(); don't consider this user code.
                    return stateRange;
                }
            }
            // else: user code - abort analysis.
            throw SymbolicAnalysisFailedException(
                "Unexpected instruction in Iterator.Dispose()");
        }
        // The other modes: user code - abort analysis.
        return Util::LongSet::Empty();
    }
    // The C# default arm: user code - abort analysis. (An
    // IsLeavingFunction Leave in IteratorDispose does not throw.)
    if (auto* leave = dynamic_cast<Leave*>(inst)) {
        if (mode_ == StateRangeAnalysisMode::IteratorDispose &&
            leave->TargetContainer != nullptr &&
            dynamic_cast<ILFunction*>(leave->TargetContainer->Parent) !=
                nullptr) {
            // The C# `IsLeavingFunction` (Leave.cs lines 97-99): the leave's
            // target container's parent is the ILFunction itself.
            return Util::LongSet::Empty();
        }
    }
    if (mode_ == StateRangeAnalysisMode::IteratorDispose) {
        throw SymbolicAnalysisFailedException(
            "Unexpected instruction in Iterator.Dispose()");
    }
    return Util::LongSet::Empty();
}

void StateRangeAnalysis::AddStateRange(Block* block,
                                       const Util::LongSet& stateRange) {
    auto it = ranges_.find(block);
    if (it != ranges_.end()) {
        it->second = stateRange.UnionWith(it->second);
    } else {
        ranges_.emplace(block, stateRange);
    }
}

void StateRangeAnalysis::AddStateRangeForLeave(
    BlockContainer* target, const Util::LongSet& stateRange) {
    auto it = rangesForLeave_.find(target);
    if (it != rangesForLeave_.end()) {
        it->second = stateRange.UnionWith(it->second);
    } else {
        rangesForLeave_.emplace(target, stateRange);
    }
}

Util::LongDict<Block*> StateRangeAnalysis::GetBlockStateSetMapping(
    const BlockContainer& container) {
    // First, consider container exits; then blocks within the container
    // (the C# Blocks.Reverse() -- the LAST block wins).
    std::vector<std::pair<Util::LongSet, Block*>> entries;
    for (const auto& [block, states] : ranges_) {
        if (block->Parent != &container)
            entries.emplace_back(states, block);
    }
    for (auto it = container.Blocks.rbegin(); it != container.Blocks.rend();
         ++it) {
        auto found = ranges_.find(it->get());
        if (found != ranges_.end()) {
            entries.emplace_back(found->second, it->get());
        }
    }
    return Util::MakeLongDict(std::move(entries));
}

Util::LongDict<BlockContainer*>
StateRangeAnalysis::GetBlockStateSetMappingForLeave() const {
    std::vector<std::pair<Util::LongSet, BlockContainer*>> entries;
    for (const auto& [target, states] : rangesForLeave_) {
        entries.emplace_back(states, target);
    }
    return Util::MakeLongDict(std::move(entries));
}

} // namespace ILSpy::Decompiler::IL::ControlFlow
