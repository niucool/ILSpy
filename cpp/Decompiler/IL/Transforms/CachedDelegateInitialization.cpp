// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). Mirrors the helper in PatternMatchingTransform /
// SwitchAnalysis. nullptr if `block` is not in a container's Blocks list or is
// the last block.
Block* NextBlockInContainer(Block* block) {
    if (!block) return nullptr;
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
        }
    }
    return nullptr;
}

// comp(Equality, X, ldnull) -> X (either side may be the null). Sign-independent
// (the reader's brfalse/brtrue on an object-typed value uses Equality).
bool MatchCompEqualsNull(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (!comp || comp->Kind != ComparisonKind::Equality) return false;
    auto* rhs = comp->Right.get();
    auto* lhs = comp->Left.get();
    if (rhs && rhs->Op == OpCode::LdNull) { arg = lhs; return true; }
    if (lhs && lhs->Op == OpCode::LdNull) { arg = rhs; return true; }
    return false;
}

// Collect every StLoc to `v` (non-owning). This port keeps no per-variable
// StoreInstructions list (the repeatedly deferred infrastructure piece,
// D11/D62/D68), so the stores are gathered by a tree walk instead -- mirroring
// the PatternMatchingTransform.CollectUses precedent.
void CollectStLocsTo(ILInstruction* inst, ILVariable* v, std::vector<StLoc*>& out) {
    if (!inst) return;
    if (inst->Op == OpCode::StLoc) {
        auto* st = static_cast<StLoc*>(inst);
        if (st->Variable.get() == v) out.push_back(st);
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CollectStLocsTo(inst->GetChild(i), v, out);
}

int CountLdLocIn(ILInstruction* inst, ILVariable* v) {
    int n = 0;
    Walk(inst, [&](ILInstruction* i) {
        if (i->Op == OpCode::LdLoc) {
            auto* ld = static_cast<LdLoc*>(i);
            if (ld->Variable.get() == v) ++n;
        }
    });
    return n;
}

// CachedDelegateInitializationWithLocal (subset). See the header for the shape
// and the block-model adaptation. Returns true if the block was rewritten.
bool TryWithLocal(ILFunction& function, Block* block, ILTransformContext& context) {
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    // FalseInst must be null (the C# MatchNop -- the if just falls through).
    if (iff->FalseInst) return false;
    // TrueInst is a Block with exactly one instruction: stloc v(Delegate).
    auto* trueBlock = dynamic_cast<Block*>(iff->TrueInst.get());
    if (!trueBlock || trueBlock->Instructions.size() != 1) return false;
    auto* storeInst = dynamic_cast<StLoc*>(trueBlock->Instructions[0].get());
    if (!storeInst) return false;
    // Condition: comp(ldloc v == ldnull).
    ILInstruction* condArg = nullptr;
    if (!MatchCompEqualsNull(iff->Condition.get(), condArg)) return false;
    auto* condLd = dynamic_cast<LdLoc*>(condArg);
    if (!condLd) return false;
    ILVariable* v = condLd->Variable.get();
    if (!v) return false;
    // The cached variable must be the store's variable (the C# storeInst
    // .MatchStLoc(v, ...)).
    if (v != storeInst->Variable.get()) return false;
    // The stored value must be a delegate construction.
    DelegateConstructionMatch dm;
    if (!DelegateConstruction::MatchDelegateConstruction(storeInst->Value.get(), dm, true))
        return false;
    // Usage guards: exactly two stores, two loads, no addresses.
    if (v->StoreCount != 2 || v->LoadCount != 2 || v->AddressCount != 0) return false;
    // The "next instruction" is the next block in the container (the if's
    // fall-through). The C# requires nextInstruction != null.
    Block* nextBlock = NextBlockInContainer(block);
    if (!nextBlock) return false;
    // Exactly one usage of v in the next block (the C# usages.Length == 1).
    if (CountLdLocIn(nextBlock, v) != 1) return false;
    // The other store must be a single StLoc to v (not storeInst) whose value
    // is ldnull and whose parent is a Block (the C#
    // v.StoreInstructions.OfType<StLoc>().SingleOrDefault(store => store !=
    // storeInst) + otherStore.Value.MatchLdNull() + otherStore.Parent is Block).
    std::vector<StLoc*> stores;
    CollectStLocsTo(function.Body.get(), v, stores);
    StLoc* otherStore = nullptr;
    for (StLoc* s : stores) {
        if (s == storeInst) continue;
        if (otherStore) return false;  // more than one other store
        otherStore = s;
    }
    if (!otherStore) return false;
    if (!otherStore->Value || otherStore->Value->Op != OpCode::LdNull) return false;
    auto* otherBlock = dynamic_cast<Block*>(otherStore->Parent);
    if (!otherBlock) return false;

    context.StepOnce("CachedDelegateInitializationWithLocal");

    // Detach the cache-fill store from the if's TrueInst Block before the if
    // is destroyed (no GC; a raw pointer would dangle).
    auto storeOwned = trueBlock->TakeChild(0);  // the stloc v(Delegate)
    // Drop the null-init store from its block.
    otherBlock->RemoveInstructionAt(static_cast<std::size_t>(otherStore->ChildIndex));
    // Append the cache-fill store to the host block's instructions.
    block->Add(std::move(storeOwned));
    // Replace the if-final with a Branch to the next block (the explicit
    // fall-through the if's null FalseInst represented; the block model requires
    // a final). This destroys the if and the now-empty TrueInst Block.
    block->SetFinal(std::make_unique<Branch>(nextBlock));
    return true;
}

void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

} // namespace

void CachedDelegateInitialization::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.AnonymousMethods) return;

    // The StoreCount / LoadCount / AddressCount guards read fresh usage counts;
    // the C# maintains these incrementally, this port recomputes.
    ComputeVariableUsage(function);

    // The WithLocal rewrite does not destroy any container Block (the host and
    // the null-init block stay in their containers; only the if's TrueInst
    // Block, an if-arm, is destroyed), so a single gathered container list stays
    // valid throughout. Each host's cached variable is distinct, so the
    // pre-rewrite usage snapshot stays valid for the blocks not yet processed.
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        for (auto& block : container->Blocks) {
            if (!block) continue;
            // Re-read the final each iteration: a prior rewrite in this
            // container may have changed a later block's instructions, but a
            // block's own final only changes when that block is the host.
            (void)TryWithLocal(function, block.get(), context);
        }
    }

    // The fold dropped the null-init store and the null-check load; recompute
    // so later transforms and the seed read fresh counts.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
