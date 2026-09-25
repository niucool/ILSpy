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

// Port of ICSharpCode.Decompiler/FlowAnalysis/ReachingDefinitionsVisitor.cs
// (with the DataFlowVisitor mechanics it rides on) -- the "reaching
// definitions" analysis: for every load position, the set of stores that
// might have written to the variable without an intervening store.
//
// The C# engine derives the per-instruction walk from the ILVisitor base;
// this port dispatches on the opcode over the same tree walk. The
// unsupported-shape set (try/catch/finally, lock, using, pinned regions,
// switch) marks the analysis unsupported -- consumers must then skip any
// conclusion drawn from it -- instead of approximating, so the bail arms
// stay loud.
//
// The query surface rides the walk itself (the C# GetStores(state, v) /
// IsPotentiallyUninitialized(state, v) are called from the derived class'
// load hooks while the walk holds the current state).

#pragma once

#include "Decompiler/Util/BitSet.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace ILSpy::Decompiler::IL { class TryCatch; }

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;
class ILFunction;
class ILInstruction;
class ILVariable;
class LdLoc;
class LdLoca;


class ReachingDefinitionsVisitor {
public:
    // The C# `struct State : IDataFlowState<State>` over the bit layout:
    // bit 0 = reachable; bit firstStoreIndexForVariable[v] = the
    // uninitialized-store bit; the following bits = the variable's stores.
    struct State {
        State() : bits(0) {}
        explicit State(int capacity) : bits(capacity) {}

        bool LessThanOrEqual(const State& other) const {
            return bits.IsSubsetOf(other.bits);
        }
        State Clone() const { return State(bits); }
        void ReplaceWith(const State& other) { bits = other.bits; }
        void JoinWith(const State& other) { bits.UnionWith(other.bits); }

        bool IsBottom() const { return !bits[0]; }
        void ReplaceWithBottom() { bits.ClearAll(); }
        bool IsReachable() const { return bits[0]; }

        void KillStores(int startIndex, int endIndex) {
            bits.Clear(startIndex, endIndex);
        }
        bool IsReachingStore(int storeIndex) const { return bits[storeIndex]; }
        int NextReachingStore(int startIndex, int endIndex) const {
            return bits.NextSetBit(startIndex, endIndex);
        }
        void SetStore(int storeIndex) { bits.Set(storeIndex); }

    private:
        friend class ReachingDefinitionsVisitor;
        explicit State(Util::BitSet b) : bits(std::move(b)) {}
        Util::BitSet bits;
    };

    // The C# ctor pair: the predicate picks the analyzed variables.
    explicit ReachingDefinitionsVisitor(
        ILFunction& scope,
        const std::function<bool(const ILVariable*)>& isAnalyzed);

    virtual ~ReachingDefinitionsVisitor() = default;

    // The analysis entry (the C# `function.Body.AcceptVisitor(this)` drive).
    // When this returns false the shapes in the function exceeded the
    // supported set and no query results are usable.
    bool Analyze();

    // Whether the variable is analyzed (the C# IsAnalyzedVariable).
    bool IsAnalyzedVariable(const ILVariable* v) const;

protected:
    // The analyzed scope (the derived class' GetNewVariable appends to it;
    // the C# accesses scope.Variables the same way).
    ILFunction* scope() const { return scope_; }

    // The derived-class hooks (the C# overridden Visit methods). OnLdLoc /
    // OnLdLoca fire at the load's position (the C# calls base.VisitLdLoc
    // first, then the derived HandleLoad reads the current state).
    virtual void OnLdLoc(LdLoc* inst) { (void)inst; }
    virtual void OnLdLoca(LdLoca* inst) { (void)inst; }

    // The C# queries over a walk state (the GroupStores calls them from the
    // hooks with the engine's current state). Precondition:
    // IsAnalyzedVariable(v) and a supported analysis.
    std::vector<ILInstruction*> GetStoresFor(const State& atState,
                                             const ILVariable* v);
    bool IsPotentiallyUninitialized(const State& atState, const ILVariable* v);

    State state{0};
    State bottomState{0};

private:
    // The C# HandleTryBlock / VisitTryCatch pair (DataFlowVisitor.cs lines
    // 508-551): the per-try exceptional state and the handler seeding.
    void VisitTryCatchBlocks(ILInstruction* inst);
    std::map<IL::TryCatch*, State> stateOnException_;
    State* currentStateOnException_ = nullptr;

    // --- the walk (the C# ILVisitor dispatch, manual) ---
    void Visit(ILInstruction* inst);
    void VisitDefault(ILInstruction* inst);
    void VisitBlockContainerBlock(ILInstruction* container);
    void VisitIfInstruction(ILInstruction* inst);
    void VisitBranch(ILInstruction* inst);
    void VisitLeave(ILInstruction* inst);
    std::pair<State, State> EvaluateCondition(ILInstruction* inst);
    void EvaluateMatch(ILInstruction* match);
    void HandleStore(ILInstruction* inst, ILVariable* v);

    // The C# GetBlockInputState: absent entries get a bottom-state clone
    // (the correct bit-set capacity), never a default-constructed State.
    State& GetBlockInputState(Block* block);

    void MarkUnreachable() { state.ReplaceWithBottom(); }

    // bit 0 = the reachable bit; the store indices start at 1 (the C#
    // ReachableBit / FirstStoreIndex constants).
    static constexpr int kReachableBit = 0;
    static constexpr int kFirstStoreIndex = 1;

    ILFunction* scope_ = nullptr;
    std::function<bool(const ILVariable*)> isAnalyzed_;
    bool unsupported_ = false;

    // allStores[si] (si > 0) is the store instruction at store index si;
    // per analyzed variable the store list occupies a contiguous segment,
    // preceded by the uninitialized entry (segment front == nullptr).
    std::unordered_map<ILInstruction*, int> storeIndexMap_;
    std::vector<int> firstStoreIndexForVariable_;
    std::unordered_map<ILVariable*, std::vector<ILInstruction*>> storesByVariable_;
    // The analyzed variable's segment index (the C# v.IndexInFunction).
    std::unordered_map<ILVariable*, int> variableIndex_;

    // The per-block incoming states and the per-container worklists (the C#
    // stateOnBranch / workLists dictionaries).
    std::unordered_map<Block*, State> stateOnBranch_;
    std::unordered_map<const BlockContainer*, State> stateOnLeave_;
    std::unordered_map<ILInstruction*, std::set<int>*> blockWorklist_;

    State currentStateOnException{0};
};

} // namespace ILSpy::Decompiler::IL
