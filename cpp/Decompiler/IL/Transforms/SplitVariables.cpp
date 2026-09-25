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

#include "Decompiler/IL/Transforms/SplitVariables.hpp"

#include "Decompiler/FlowAnalysis/ReachingDefinitionsVisitor.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/Util/UnionFind.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The variable-carrying instruction nodes (the C# IInstructionWithVariableOperand
// set the descendant swap walks). LdLoca swaps too (the address uses split
// with the variable).
bool TryGetVariable(ILInstruction* inst, ILVariable** out) {
    switch (inst->Op) {
        case OpCode::LdLoc:
            *out = static_cast<LdLoc*>(inst)->Variable.get();
            return true;
        case OpCode::LdLoca:
            *out = static_cast<LdLoca*>(inst)->Variable.get();
            return true;
        case OpCode::StLoc:
            *out = static_cast<StLoc*>(inst)->Variable.get();
            return true;
        case OpCode::MatchInstruction:
            *out = static_cast<MatchInstruction*>(inst)->Variable.get();
            return true;
        case OpCode::TryCatchHandler:
            *out = static_cast<TryCatchHandler*>(inst)->Variable.get();
            return true;
        case OpCode::UsingInstruction:
            *out = static_cast<UsingInstruction*>(inst)->Variable.get();
            return true;
        default:
            return false;
    }
}

void SetVariable(ILInstruction* inst, const ILVariablePtr& replacement) {
    switch (inst->Op) {
        case OpCode::LdLoc:
            static_cast<LdLoc*>(inst)->Variable = replacement;
            break;
        case OpCode::LdLoca:
            static_cast<LdLoca*>(inst)->Variable = replacement;
            break;
        case OpCode::StLoc:
            static_cast<StLoc*>(inst)->Variable = replacement;
            break;
        case OpCode::MatchInstruction:
            static_cast<MatchInstruction*>(inst)->Variable = replacement;
            break;
        case OpCode::TryCatchHandler:
            static_cast<TryCatchHandler*>(inst)->Variable = replacement;
            break;
        case OpCode::UsingInstruction:
            static_cast<UsingInstruction*>(inst)->Variable = replacement;
            break;
        default:
            break;
    }
}

// The C# DetermineAddressUse: whether the ldloca's use is understood well
// enough to allow splitting the target variable. Only the Immediate arm and
// the understood chain shapes are ported; everything else is Unknown (the
// conservative answer: no splitting).
enum class AddressUse { Unknown, Immediate };

AddressUse DetermineAddressUse(ILInstruction* addressLoadingInstruction);

AddressUse DetermineAddressUse(ILInstruction* addressLoadingInstruction) {
    if (addressLoadingInstruction == nullptr ||
        addressLoadingInstruction->Parent == nullptr) {
        return AddressUse::Unknown;
    }
    ILInstruction* parent = addressLoadingInstruction->Parent;
    switch (parent->Op) {
        case OpCode::LdObj:
            return AddressUse::Immediate;
        case OpCode::StObj: {
            // Immediate only when the address feeds the store's target slot
            // (the C# `StObj stobj when stobj.Target == addressLoadingInstruction`).
            auto* stobj = static_cast<StObj*>(parent);
            if (stobj->Target.get() == addressLoadingInstruction)
                return AddressUse::Immediate;
            return AddressUse::Unknown;
        }
        default:
            // The remaining C# arms (the LdFlda chains, the Await receiver,
            // the call-argument shapes, the ref-local stores) need the
            // by-ref-like classification and the resolved method shapes;
            // deferred with those surfaces -- the conservative answer keeps
            // the split away from the ununderstood address uses.
            return AddressUse::Unknown;
    }
}

// The C# IsCandidateVariable: Local variables with only understood address
// uses (the port's AddressInstructions snapshot), plus the async/redundant
// stack slots (the port's ILFunction carries no IsAsync bit -- the
// RemoveIfRedundant arm is the supported one).
bool IsCandidateVariable(const ILVariable& v) {
    switch (v.Kind) {
        case VariableKind::Local:
            // The merged AddressInstructions list stores generic
            // ILInstructions (the C# IReadOnlyList<LdLoca> as the port's
            // non-typed list); every entry is an LdLoca node -- the
            // dynamic_cast recovers the typed view.
            for (ILInstruction* addr : v.AddressInstructions) {
                auto* ldloca = dynamic_cast<LdLoca*>(addr);
                if (ldloca == nullptr)
                    continue;
                if (DetermineAddressUse(ldloca) == AddressUse::Unknown) {
                    return false;
                }
            }
            return true;
        case VariableKind::StackSlot:
            // The C# splits the locals-turned-stackslots of async functions
            // and the infeasible-path transform's slots; the port's IsAsync
            // flag lives on the ILFunction (not reachable from the variable),
            // so only the RemoveIfRedundant arm applies here.
            return v.RemoveIfRedundant;
        default:
            // Parameters must not split; pinned locals must not split (it
            // would extend the pin's life).
            return false;
    }
}

// The C# GroupStores : ReachingDefinitionsVisitor -- the union-find merge of
// the stores and loads that must stay together.
class GroupStores final : public ReachingDefinitionsVisitor {
public:
    GroupStores(ILFunction& scope)
        : ReachingDefinitionsVisitor(
              scope, [](const ILVariable* v) { return IsCandidateVariable(*v); }) {}

    // The C# VisitLdLoc: HandleLoad + the supported-ref-local handling (the
    // ref-local arm needs the single-definition store lookup; the port's
    // StoreInstructions snapshot feeds it).
    void OnLdLoc(LdLoc* inst) override {
        HandleLoad(inst, inst->Variable.get());
        LdLoca* refLocalAddressLoad = GetAddressLoadForRefLocalUse(inst);
        if (refLocalAddressLoad != nullptr)
            HandleLoad(refLocalAddressLoad, refLocalAddressLoad->Variable.get());
    }

    void OnLdLoca(LdLoca* inst) override { HandleLoad(inst, inst->Variable.get()); }

    // The C# GetNewVariable: the split variable for the instruction's
    // group (the C# copies Kind/Type/StackType/Index/Name/
    // HasGeneratedName/StateMachineField/InitialValueIsInitialized/
    // RemoveIfRedundant; the port copies the fields it carries).
    const ILVariablePtr& GetNewVariable(ILInstruction* inst) {
        ILInstruction* representative = unionFind_.Find(inst);
        auto it = newVariables_.find(representative);
        if (it == newVariables_.end()) {
            ILVariable* original = VariableOf(inst);
            auto v = std::make_shared<ILVariable>(original->Kind, original->Type,
                                                  original->Index);
            v->Name = original->Name;
            v->HasGeneratedName = original->HasGeneratedName;
            v->UsesInitialValue = false;  // set below for uninit loads
            v->RemoveIfRedundant = original->RemoveIfRedundant;
            ILVariablePtr owned = v;
            newVariables_.emplace(representative, owned);
            scope()->Variables.push_back(std::move(v));
            return newVariables_[representative];
        }
        return it->second;
    }

private:
    static ILVariable* VariableOf(ILInstruction* inst) {
        ILVariable* v = nullptr;
        if (TryGetVariable(inst, &v)) return v;
        return nullptr;
    }

    // The C# GetAddressLoadForRefLocalUse: `ldloca target; stloc ref_local`
    // -- the ref local's single store's value, unwrapped from the LdFlda
    // chain, must be the LdLoca itself.
    // The per-variable store-list walk (the C# reads
    // `ldloc.Variable.StoreInstructions`; the port does not maintain the
    // list, D11/D62/D68, so the single store is gathered by a tree walk
    // from the function root -- the CachedDelegateInitialization
    // precedent).
    static void CollectStoresTo(ILInstruction* node, const ILVariable* v,
                                std::vector<StLoc*>& stores) {
        if (auto* stloc = dynamic_cast<StLoc*>(node)) {
            if (stloc->Variable != nullptr && stloc->Variable.get() == v)
                stores.push_back(stloc);
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                CollectStoresTo(child, v, stores);
        }
    }

    LdLoca* GetAddressLoadForRefLocalUse(LdLoc* ldloc) {
        if (ldloc->Variable == nullptr ||
            !ldloc->Variable->IsSingleDefinition())
            return nullptr;
        ILInstruction* root = ldloc;
        while (root->Parent != nullptr) root = root->Parent;
        std::vector<StLoc*> stores;
        CollectStoresTo(root, ldloc->Variable.get(), stores);
        if (stores.size() != 1) return nullptr;
        StLoc* stloc = stores[0];
        ILInstruction* value = stloc->Value.get();
        while (value != nullptr && value->Op == OpCode::LdFlda)
            value = value->GetChild(0);
        if (value == nullptr || value->Op != OpCode::LdLoca) return nullptr;
        return static_cast<LdLoca*>(value);
    }

    void HandleLoad(ILInstruction* inst, ILVariable* v) {
        if (!IsAnalyzedVariable(v)) return;
        if (IsPotentiallyUninitialized(state, v)) {
            // Merge all uninit loads together (the C# uninitVariableUsage).
            auto it = uninitVariableUsage_.find(v);
            if (it != uninitVariableUsage_.end()) {
                unionFind_.Merge(inst, it->second);
            } else {
                uninitVariableUsage_.emplace(v, inst);
            }
        }
        for (ILInstruction* store : GetStoresFor(state, v)) {
            unionFind_.Merge(inst, store);
        }
    }

    Util::UnionFind<ILInstruction*> unionFind_;
    std::unordered_map<ILInstruction*, ILVariablePtr> newVariables_;
    std::unordered_map<ILVariable*, ILInstruction*> uninitVariableUsage_;
};

} // namespace

void SplitVariables::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // The use-site lists the candidate check reads are the
    // ComputeVariableUsage snapshots.
    ComputeVariableUsage(function);
    GroupStores groupStores(function);
    if (!groupStores.Analyze()) {
        // The unsupported shapes (try/lock/using/pinned regions, the
        // compound match conditions) stay conservative: no splitting.
        return;
    }
    // Replace the analyzed variables' uses with their split versions (the
    // C# descendant walk over IInstructionWithVariableOperand).
    struct Frame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({function.Body.get(), 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.nextChild >= frame.node->ChildCount()) {
            stack.pop_back();
            continue;
        }
        ILInstruction* child = frame.node->GetChild(frame.nextChild);
        frame.nextChild++;
        if (child == nullptr) continue;
        ILVariable* v = nullptr;
        if (TryGetVariable(child, &v) && groupStores.IsAnalyzedVariable(v)) {
            SetVariable(child, groupStores.GetNewVariable(child));
        }
        stack.push_back({child, 0});
    }
    // The C# `function.Variables.RemoveDead()`: drop the variables the tree
    // no longer references (IsDead: no stores, loads or addresses), keeping
    // the display-class locals and the parameters.
    ComputeVariableUsage(function);
    for (auto it = function.Variables.begin();
         it != function.Variables.end();) {
        ILVariable* v = it->get();
        const bool isDead = v->StoreCount == 0 && v->LoadCount == 0 &&
                            v->AddressCount == 0;
        const bool keep = !isDead || v->Kind == VariableKind::DisplayClassLocal ||
                          v->Kind == VariableKind::Parameter;
        if (keep) {
            ++it;
        } else {
            it = function.Variables.erase(it);
        }
    }
    // The counts ride the next ComputeVariableUsage call (the port's
    // recompute convention); the dead check above already used fresh ones.
}

} // namespace ILSpy::Decompiler::IL
