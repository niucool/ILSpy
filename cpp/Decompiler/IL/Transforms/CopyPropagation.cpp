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

#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

// Whether `value` is a cheap load whose variable can be copy-propagated: a
// LdLoc of a single-definition, never-assigned parameter, or of another
// single-definition local. (The C# also handles ldloca/ldsFlda and uses a
// virtual Clone; this port handles the common ldloc source, the dominant
// argument-to-local copy case.)
bool CanCopyPropagateLoad(ILVariable* target, ILInstruction* value) {
    if (!value || value->Op != OpCode::LdLoc) return false;
    auto* ld = static_cast<LdLoc*>(value);
    auto* src = ld->Variable.get();
    if (!src) return false;
    // The source must be effectively constant: a single-definition parameter
    // (StoreCount == 1, the caller's init, and never assigned) or another
    // single-definition local.
    if (src->Kind == VariableKind::Parameter)
        return src->IsSingleDefinition();  // StoreCount == 1, AddressCount == 0
    return src->IsSingleDefinition();
}

// Replace every `ldloc V` in the tree with `ldloc src`.
void ReplaceAllLoads(ILInstruction* root, ILVariable* v, ILVariablePtr src) {
    // Collect first (ReplaceWith detaches mid-walk).
    std::vector<LdLoc*> loads;
    WalkAll(root, [&](ILInstruction* inst) {
        if (auto* ld = dynamic_cast<LdLoc*>(inst))
            if (ld->Variable.get() == v) loads.push_back(ld);
    });
    for (LdLoc* ld : loads) {
        ld->Variable = src;
    }
}

// Whether `value` is an address-loading instruction whose result is always the
// same for a given operand (ldloca/ldsflda), so it can be copy-propagated by
// cloning. The C# `CanPerformCopyPropagation` returns true unconditionally for
// ldloca/ldsflda (they never throw and always yield the same address); it also
// handles ldElema/ldFlda, gated on `UseRefLocalsForAccurateOrderOfEvaluation`
// because those may throw and the setting controls whether moving the throw
// site is acceptable. The setting is not modelled here, so ldElema/ldFlda stay
// deferred; this port handles the unconditional ldloca/ldsflda pair.
bool IsAddressLoadSource(ILInstruction* value) {
    if (!value) return false;
    return value->Op == OpCode::LdLoca || value->Op == OpCode::LdsFlda;
}

// Copy-propagate an address-loading source (ldloca/ldsflda): replace every
// `ldloc v` in the function body with a clone of the source, then drop the
// store. The C# `DoPropagate` uses a virtual `copiedExpr.Clone()` per load (and
// un-inlines the source's child-instruction arguments into fresh stack slots --
// a no-op for ldloca/ldsflda, whose only operand is the variable/field, not a
// child instruction). This port's `ILInstruction::Clone` (D147) is the
// foundation; the clone is the same opcode as the source (ldloca/ldsflda), not
// a ldloc, so the load instruction is replaced rather than re-pointed (unlike
// the ldloc-source case above). Clearing the clone's IL range matches the C#
// `clone.SetILRange(new Interval())` -- the expression is copied from afar and
// reusing the source's IL range would mis-locate sequence points. The source
// is cloned before the store is removed, so it stays alive across the clones.
void PropagateAddressSource(ILFunction& function, Block* block,
                             std::size_t storeIndex, StLoc* st, ILVariable* v) {
    // Collect the `ldloc v` loads first: ReplaceWith detaches each mid-loop.
    std::vector<LdLoc*> loads;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (auto* ld = dynamic_cast<LdLoc*>(inst))
            if (ld->Variable.get() == v) loads.push_back(ld);
    });
    for (LdLoc* ld : loads) {
        auto clone = st->Value->Clone();
        clone->SetILRange(0, 0);  // empty: copied from afar
        ld->ReplaceWith(std::move(clone));
    }
    block->RemoveInstructionAt(storeIndex);
}

// The C# `context.TypeSystem.FindType(arg.ResultType)`: the evaluation-stack
// type of an un-inlined argument mapped to an IType. Unknown -> UnknownType;
// Ref -> a ByReferenceType over UnknownType; everything else the StackType's
// ToKnownTypeCode lookup. The port has no compilation in the transform
// context, so the known type is a standalone KnownType -- the same code/name
// an ICompilation.FindType returns (copied next to this second consumer per
// the established convention; NamedArgumentTransform.cpp carries the first).
TypeSystem::ITypePtr FindTypeForStackType(StackType stackType) {
    using namespace TypeSystem;
    if (stackType == StackType::Unknown) return UnknownType();
    if (stackType == StackType::Ref)
        return std::make_shared<ByReferenceType>(UnknownType());
    KnownTypeCode code = ToKnownTypeCode(stackType);
    if (code == KnownTypeCode::None) return UnknownType();
    return std::make_shared<KnownType>(code);
}

// The ILFunction root that owns `inst` (the ILInlining.cpp FunctionOfBlock
// convention, copied here): walks the Parent chain to the root
// (ILFunction::IsRoot). Returns null for a detached subtree.
ILFunction* FunctionOfTree(ILInstruction* inst) {
    for (ILInstruction* p = inst; p != nullptr; p = p->Parent) {
        if (p->IsRoot()) return static_cast<ILFunction*>(p);
    }
    return nullptr;
}

// The C# `static void DoPropagate(ILVariable v, ILInstruction copiedExpr,
// Block block, ref int i, ILTransformContext context)` (CopyPropagation.cs
// lines 154-178): the shared copy-propagation core both the whole-function Run
// and the standalone Propagate entry drive.
//
//  * Un-inlines the copied expression's direct child instructions into fresh
//    stack-slot variables ("C_<StartILOffset>", HasGeneratedName, registered
//    in the function's variable list), each moved into a store inserted at
//    position i (so the expression's operands are evaluated exactly once,
//    before every propagated copy).
//  * Replaces every load of `v` in the whole function with a clone of the
//    copied expression whose direct children are the fresh loads (the C#
//    clones per load and then ReplaceWith's each clone child; the port clones
//    BEFORE moving the children out, since the C# GC aliases the children
//    between the inserted stores and the original while this port's unique
//    ownership cannot -- every direct child of each clone is then replaced
//    with a load, the same net shape).
//  * Drops the store, re-inlines whatever the rewrite made single-use
//    (ILInlining.InlineInto), and adjusts the caller's loop index by
//    -(count + 1) exactly as the C# does.
//
// The C# keeps the usage counts fresh through the instruction events; this
// port recomputes them before the re-inline (the fresh C_ variables start at
// zero counts) and once more after it (the caller reads fresh counts).
void DoPropagate(ILVariable* v, ILInstruction* copiedExpr, Block* block,
                 int& i, ILTransformContext& context) {
    std::string stepDesc = "Copy propagate " + v->Name;
    context.StepOnce(stepDesc.c_str());
    ILFunction* function = FunctionOfTree(block);

    // Snapshot the loads of v across the whole function (the C#
    // v.LoadInstructions.ToArray(); this port has no per-variable load list,
    // so the function body is walked -- the collection must precede the
    // ReplaceWith calls, which destroy each load).
    std::vector<LdLoc*> loads;
    if (function != nullptr) {
        WalkAll(function->Body.get(), [&](ILInstruction* inst) {
            if (auto* ld = dynamic_cast<LdLoc*>(inst))
                if (ld->Variable.get() == v) loads.push_back(ld);
        });
    }

    const int n = copiedExpr->ChildCount();
    // One clone per load, taken before the children are moved out.
    std::vector<std::unique_ptr<ILInstruction>> clones;
    clones.reserve(loads.size());
    for (std::size_t k = 0; k < loads.size(); ++k)
        clones.push_back(copiedExpr->Clone());

    // Un-inline the arguments: take the children out (from the end, so earlier
    // indices stay valid under the null-leaving TakeChild) and insert one
    // store per child at position i, in forward order.
    std::vector<ILVariablePtr> uninlinedArgs;
    std::vector<std::unique_ptr<ILInstruction>> args;
    args.reserve(n);
    for (int j = n - 1; j >= 0; --j)
        args.push_back(copiedExpr->TakeChild(j));
    uninlinedArgs.reserve(n);
    for (int j = 0; j < n; ++j) {
        auto arg = std::move(args[static_cast<std::size_t>(n - 1 - j)]);
        assert(arg != nullptr && "DoPropagate: copied expression has a null child");
        auto variable = std::make_shared<ILVariable>(
            VariableKind::StackSlot, FindTypeForStackType(arg->ResultType()));
        variable->Name = "C_" + std::to_string(arg->StartILOffset);
        variable->HasGeneratedName = true;
        block->InsertAt(static_cast<std::size_t>(i),
                        std::make_unique<StLoc>(variable, std::move(arg)));
        ++i;
        uninlinedArgs.push_back(std::move(variable));
    }
    if (function != nullptr) {
        for (auto& variable : uninlinedArgs)
            function->Variables.push_back(variable);
    }

    // Perform the copy propagation: each load becomes the clone with the fresh
    // loads in place of the original children, carrying an empty IL range (the
    // expression is copied from afar; reusing the source's IL range would
    // mis-locate sequence points).
    for (std::size_t k = 0; k < loads.size(); ++k) {
        for (int j = 0; j < n; ++j)
            clones[k]->SetChild(j, std::make_unique<LdLoc>(uninlinedArgs[static_cast<std::size_t>(j)]));
        clones[k]->SetILRange(0, 0);
        loads[k]->ReplaceWith(std::move(clones[k]));
    }

    block->RemoveInstructionAt(static_cast<std::size_t>(i));
    if (function != nullptr)
        ComputeVariableUsage(*function);
    int c = InlineInto(block, i, InliningOptions::None, context);
    i -= c + 1;
    if (function != nullptr)
        ComputeVariableUsage(*function);
}

} // namespace

void CopyPropagation::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    ComputeVariableUsage(function);
    // Process each block. The C# only runs on ControlFlow blocks (pre-
    // ConditionDetection); our blocks are all pre-ConditionDetection when this
    // runs (after the StatementTransform, before AssignVariableNames).
    std::vector<Block*> blocks;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (auto* b = dynamic_cast<Block*>(inst)) blocks.push_back(b);
    });
    for (Block* block : blocks) {
        for (int i = 0; i < static_cast<int>(block->Instructions.size()); ++i) {
            auto* st = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(i)].get());
            if (!st || !st->Variable) continue;
            ILVariable* v = st->Variable.get();
            if (!v->IsSingleDefinition()) continue;
            if (v->LoadCount == 0 && v->Kind == VariableKind::StackSlot) {
                // Dead store to a stack slot.
                if (IsPure(st->Value ? st->Value->Flags() : InstructionFlags::None)) {
                    block->RemoveInstructionAt(static_cast<std::size_t>(i));
                    --i;
                } else {
                    // Keep the side effects: replace the store with its value.
                    auto value = st->TakeChild(0);
                    st->ReplaceWith(std::move(value));
                    --i;
                }
                continue;
            }
            // Copy propagation of a single-def slot from a cheap load.
            if (v->Kind == VariableKind::StackSlot && CanCopyPropagateLoad(v, st->Value.get())) {
                auto* srcLd = static_cast<LdLoc*>(st->Value.get());
                ILVariablePtr src = srcLd->Variable;
                ReplaceAllLoads(function.Body.get(), v, src);
                block->RemoveInstructionAt(static_cast<std::size_t>(i));
                --i;
            }
            // Copy propagation of a single-def stack slot from an address load
            // (ldloca/ldsflda): the clone-based case the D142 subset deferred
            // until ILInstruction::Clone landed (D147). The C# `CanPerformCopy-
            // Propagation` returns true for ldloca/ldsflda regardless of the
            // target's kind (an address load always yields the same value);
            // this port gates on StackSlot to match the ldloc case's
            // simplification (the byref-local case, `target.StackType == Ref`,
            // is the C#-faithful extension this port's StackSlot gating skips).
            else if (v->Kind == VariableKind::StackSlot && IsAddressLoadSource(st->Value.get())) {
                PropagateAddressSource(function, block, static_cast<std::size_t>(i), st, v);
                --i;
            }
        }
    }
    ComputeVariableUsage(function);
}

void CopyPropagation::Propagate(StLoc* store, ILTransformContext& context) {
    // The C# Debug.Assert(store.Variable.IsSingleDefinition).
    assert(store->Variable && store->Variable->IsSingleDefinition());
    // The C# `(Block)store.Parent` cast: a store's parent is always a block in
    // a connected tree; a non-block parent is a malformed fixture.
    auto* block = dynamic_cast<Block*>(store->Parent);
    assert(block != nullptr && "Propagate: store's parent is not a block");
    int i = store->ChildIndex;
    DoPropagate(store->Variable.get(), store->Value.get(), block, i, context);
}

} // namespace ILSpy::Decompiler::IL
