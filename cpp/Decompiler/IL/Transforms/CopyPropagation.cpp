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
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <algorithm>
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
        }
    }
    ComputeVariableUsage(function);
}

} // namespace ILSpy::Decompiler::IL
