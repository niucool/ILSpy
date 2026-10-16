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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
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
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). Mirrors the helper in CachedDelegateInitialization
// / PatternMatchingTransform / SwitchAnalysis. nullptr if `block` is not in a
// container's Blocks list or is the last block.
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

// Two LdsFlda nodes refer to the same field when their FieldToken matches (the
// reader populates it) or, failing that, their FieldName matches.
bool SameCacheField(const LdsFlda* a, const LdsFlda* b) {
    if (!a || !b) return false;
    if (a->FieldToken != 0 && a->FieldToken == b->FieldToken) return true;
    return a->FieldName == b->FieldName;
}

// See the header for the shape and the block-model adaptation. Returns true if
// the block was rewritten.
bool DoTransform(Block* block, ILTransformContext& context) {
    // The cache-load store is the block's last non-terminal instruction; the if
    // (the cache null-check) is the block's FinalInstruction. There is nothing
    // to match when the block has no non-terminal instructions.
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    if (block->Instructions.empty()) return false;
    auto* storeBeforeIf = dynamic_cast<StLoc*>(block->Instructions.back().get());
    if (!storeBeforeIf) return false;
    // storeBeforeIf: stloc V(ldobj(ldsflda cacheField)), cacheField a
    // compiler-generated static field (the D78 gate).
    auto* load = dynamic_cast<LdObj*>(storeBeforeIf->Value.get());
    if (!load) return false;
    auto* cacheFlda = dynamic_cast<LdsFlda*>(load->Target.get());
    if (!cacheFlda || !cacheFlda->IsCompilerGeneratedField) return false;
    auto v = storeBeforeIf->Variable;
    if (!v) return false;
    // V is assigned exactly twice (the cache load + the in-if init) and read
    // exactly three times (the null check + the cache write-back + the one real
    // downstream usage), with no address-of.
    if (v->StoreCount != 2 || v->LoadCount != 3 || v->AddressCount != 0) return false;
    // The if must be a simple `if (...) { ... }` (no else) with a two-instruction
    // body (the init store + the cache write-back).
    if (iff->FalseInst) return false;
    auto* trueBlock = dynamic_cast<Block*>(iff->TrueInst.get());
    if (!trueBlock || trueBlock->Instructions.size() != 2) return false;
    // condition: V == null (MatchCompEqualsNull also accepts null == V).
    ILInstruction* condArg = nullptr;
    if (!MatchCompEqualsNull(iff->Condition.get(), condArg)) return false;
    auto* condLd = dynamic_cast<LdLoc*>(condArg);
    if (!condLd || condLd->Variable.get() != v.get()) return false;
    // trueBlock[0]: stloc V(value) -- the array initializer stored into V.
    auto* storeValue = dynamic_cast<StLoc*>(trueBlock->Instructions[0].get());
    if (!storeValue || storeValue->Variable.get() != v.get()) return false;
    // trueBlock[1]: stobj(ldsflda cacheField2, ldloc V) -- the write-back to the
    // same cache field.
    auto* stobj = dynamic_cast<StObj*>(trueBlock->Instructions[1].get());
    if (!stobj) return false;
    auto* cacheFlda2 = dynamic_cast<LdsFlda*>(stobj->Target.get());
    if (!cacheFlda2 || !SameCacheField(cacheFlda, cacheFlda2)) return false;
    auto* stobjValue = dynamic_cast<LdLoc*>(stobj->Value.get());
    if (!stobjValue || stobjValue->Variable.get() != v.get()) return false;

    context.StepOnce("CachedReadOnlySpanInitialization");

    // Rewrite: replace the cache load with the initializer value, then drop the
    // if. Detach the initializer (storeValue's Value) before the if is destroyed
    // (no GC; a raw pointer would dangle).
    auto initValue = storeValue->TakeChild(0);  // the array initializer
    storeBeforeIf->SetChild(0, std::move(initValue));  // destroys the old ldobj(load)

    // Preserve the body's trailing control flow as the host block's new final.
    // The body Block (the if's TrueInst) carries a trailing Branch to the usage
    // block (the reader makes fall-through explicit); the C# body has no final.
    // When the body falls through to the next block (the common cache-pattern
    // case), drop the goto -- the block falls through positionally, so no
    // spurious `goto IL_XXXX` survives (matching the C#, which just removes the
    // if). Otherwise keep the body's exit so the control flow is preserved. A
    // body with no trailing control flow (the C# shape) just drops the if.
    Block* nextBlock = NextBlockInContainer(block);
    if (trueBlock->FinalInstruction) {
        auto bodyFinal = trueBlock->TakeChild(trueBlock->ChildCount() - 1);
        auto* bodyFinalBr = dynamic_cast<Branch*>(bodyFinal.get());
        if (bodyFinalBr && bodyFinalBr->TargetBlock && bodyFinalBr->TargetBlock == nextBlock) {
            // Body falls through to the next block; drop the goto. This destroys
            // the if and the now-final-less body Block (and its two instructions).
            block->FinalInstruction.reset();
        } else {
            block->SetFinal(std::move(bodyFinal));  // destroys the if + body Block
        }
    } else {
        block->FinalInstruction.reset();  // destroys the if + body Block
    }
    return true;
}

void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

} // namespace

void CachedReadOnlySpanInitialization::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.ArrayInitializers) return;

    // The StoreCount / LoadCount / AddressCount guards read fresh usage counts;
    // the C# maintains these incrementally, this port recomputes.
    ComputeVariableUsage(function);

    // The fold nulls a block's final (destroying the if and the body Block, an
    // if-arm that holds no nested BlockContainer), so no container is destroyed
    // and a single gathered container list stays valid throughout -- the same
    // structure as CachedDelegateInitialization. Each host's cached variable is
    // distinct, so the pre-rewrite usage snapshot stays valid for the blocks
    // not yet processed.
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        for (auto& block : container->Blocks) {
            if (!block) continue;
            (void)DoTransform(block.get(), context);
        }
    }

    // The fold dropped the cache-load store's ldobj (and its load of V), the
    // null-check load, the cache write-back store, and its load of V; recompute
    // so later transforms and the seed read fresh counts and edge counts.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
