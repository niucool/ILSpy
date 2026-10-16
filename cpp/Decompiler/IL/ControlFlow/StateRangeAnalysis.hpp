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

// Port of ICSharpCode.Decompiler/IL/ControlFlow/StateRangeAnalysis.cs: the
// symbolic execution that determines, for each block, the set of 'state'
// values for which the block is reachable. The YieldReturnDecompiler and the
// AsyncAwaitDecompiler drive it over the compiler-generated state machine to
// invert the state dispatch back into structured code.

#pragma once

#include "Decompiler/IL/ControlFlow/SymbolicExecution.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/Util/LongDict.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <map>
#include <vector>

namespace ILSpy::Decompiler::IL::ControlFlow {

// The C# `enum StateRangeAnalysisMode`.
enum class StateRangeAnalysisMode {
    IteratorMoveNext,
    IteratorDispose,
    AsyncMoveNext,
    AwaitInFinally,
};

// The C# `class StateRangeAnalysis`.
class StateRangeAnalysis {
public:
    // The C# ctor: the mode selects the instruction arms (the dispose-mode
    // finally-method table, the await-in-finally leave table); the state
    // field and the optional pre-registered cached-state variable feed the
    // symbolic evaluator.
    StateRangeAnalysis(StateRangeAnalysisMode mode,
                       const TypeSystem::IField* stateField,
                       ILVariable* cachedStateVar = nullptr,
                       bool legacyVisualBasic = false);

    // The C# `CreateNestedAnalysis()`: the same settings (including the
    // cached state vars this analysis discovered) with fresh result ranges.
    StateRangeAnalysis CreateNestedAnalysis() const;

    const std::vector<ILVariable*>& CachedStateVars() const {
        return evalContext_.StateVariables();
    }

    // The C# `LongSet AssignStateRanges(ILInstruction inst, LongSet
    // stateRange)`: assign state ranges for all blocks within `inst`;
    // returns the set of states for which the exit point is reached (a
    // subset of the input; empty for unsupported instructions -- user code
    // aborts the analysis).
    Util::LongSet AssignStateRanges(ILInstruction* inst,
                                    Util::LongSet stateRange);

    // The C# `LongDict<Block> GetBlockStateSetMapping(BlockContainer
    // container)`: the state-to-block mapping; container exits are preferred
    // over blocks within the container, and within the container the LAST
    // block wins.
    Util::LongDict<Block*> GetBlockStateSetMapping(
        const BlockContainer& container);

    // The C# `LongDict<BlockContainer> GetBlockStateSetMappingForLeave()`
    // (AwaitInFinally only): the state-to-leave-target mapping.
    Util::LongDict<BlockContainer*> GetBlockStateSetMappingForLeave() const;

    ILVariable* doFinallyBodies = nullptr;
    ILVariable* skipFinallyBodies = nullptr;

    // The C# `finallyMethodToStateRange` (IteratorDispose only): for each
    // "finally method", the set of states for which it is called.
    const std::map<const TypeSystem::IMethod*, Util::LongSet>&
    FinallyMethodToStateRange() const {
        return finallyMethodToStateRange_;
    }

private:
    void AddStateRange(Block* block, const Util::LongSet& stateRange);
    void AddStateRangeForLeave(BlockContainer* target,
                               const Util::LongSet& stateRange);

    StateRangeAnalysisMode mode_;
    const TypeSystem::IField* stateField_;
    bool legacyVisualBasic_;
    SymbolicEvaluationContext evalContext_;

    std::map<Block*, Util::LongSet> ranges_;
    std::map<BlockContainer*, Util::LongSet> rangesForLeave_;  // AwaitInFinally
    std::map<const TypeSystem::IMethod*, Util::LongSet>
        finallyMethodToStateRange_;  // IteratorDispose
};

} // namespace ILSpy::Decompiler::IL::ControlFlow
