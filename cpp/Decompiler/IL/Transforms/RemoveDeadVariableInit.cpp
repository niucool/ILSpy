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

#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

} // namespace

void RemoveDeadVariableInit::Run(ILFunction& function, ILTransformContext& context) {
    // IsAsync/IsIterator and StateMachineField are not modeled in this port, so
    // removeDeadStores is just the setting (the C# fallback). C# default is
    // false; the CLI leaves it at the default.
    bool removeDeadStores = context.Settings.RemoveDeadStores;

    // Fixpoint: removing a dead store whose value loaded another variable can
    // make that variable dead too (a chain of dead copies). Recompute usage after
    // each pass and repeat until no store is dropped. The C# uses a variable
    // queue with stale counts; a recompute fixpoint is equivalent and avoids
    // reasoning about staleness.
    bool changed = true;
    while (changed) {
        ComputeVariableUsage(function);
        changed = false;

        std::vector<Block*> blocks;
        WalkAll(function.Body.get(), [&](ILInstruction* inst) {
            if (auto* b = dynamic_cast<Block*>(inst)) blocks.push_back(b);
        });

        for (auto& v : function.Variables) {
            if (!v) continue;
            if (v->Kind != VariableKind::Local && v->Kind != VariableKind::StackSlot)
                continue;
            if (!(v->RemoveIfRedundant || removeDeadStores)) continue;
            if (v->LoadCount != 0 || v->AddressCount != 0) continue;
            // Drop every StLoc to v whose parent is a Block. A pure value lets
            // the whole store go; an impure value is unwrapped so its side
            // effect survives the dropped store (mirrors ILInlining's
            // dead-store path and the DetectPinnedRegions leftover-writes pass).
            for (Block* block : blocks) {
                for (std::size_t i = 0; i < block->Instructions.size();) {
                    auto* st = dynamic_cast<StLoc*>(block->Instructions[i].get());
                    if (!st || !st->Variable || st->Variable.get() != v.get()) {
                        ++i;
                        continue;
                    }
                    context.StepOnce("Dead store to variable");
                    if (IsPure(st->Value ? st->Value->Flags() : InstructionFlags::None)) {
                        block->RemoveInstructionAt(i);
                    } else {
                        auto value = std::move(st->Value);
                        if (value) {
                            value->Parent = block;
                            value->ChildIndex = static_cast<int>(i);
                        }
                        block->Instructions[i] = std::move(value);
                        block->RenumberChildren();
                        ++i;
                    }
                    changed = true;
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
