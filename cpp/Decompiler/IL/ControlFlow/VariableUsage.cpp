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

#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"

#include <unordered_set>

namespace ILSpy::Decompiler::IL {

namespace {

void CountUsage(ILInstruction* inst) {
    if (!inst) return;
    switch (inst->Op) {
        case OpCode::LdLoc: {
            auto* ld = static_cast<LdLoc*>(inst);
            if (ld->Variable) ++ld->Variable->LoadCount;
            break;
        }
        case OpCode::StLoc: {
            auto* st = static_cast<StLoc*>(inst);
            if (st->Variable) ++st->Variable->StoreCount;
            break;
        }
        // MatchInstruction is an IStoreInstruction (it captures the matched
        // value into Variable), so it counts as a store -- mirroring the C#
        // Connected() hook that calls variable.AddStoreInstruction(this).
        case OpCode::MatchInstruction: {
            auto* m = static_cast<MatchInstruction*>(inst);
            if (m->Variable) ++m->Variable->StoreCount;
            break;
        }
        // UsingInstruction is an IStoreInstruction (it stores the resource into
        // Variable), so it counts as a store -- mirroring the C# Connected() hook.
        case OpCode::UsingInstruction: {
            auto* u = static_cast<UsingInstruction*>(inst);
            if (u->Variable) ++u->Variable->StoreCount;
            break;
        }
        case OpCode::LdLoca: {
            auto* lda = static_cast<LdLoca*>(inst);
            if (lda->Variable) ++lda->Variable->AddressCount;
            break;
        }
        case OpCode::TryCatchHandler: {
            // The runtime stores the exception into the handler variable.
            auto* h = static_cast<TryCatchHandler*>(inst);
            if (h->Variable) ++h->Variable->StoreCount;
            break;
        }
        default:
            break;
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CountUsage(inst->GetChild(i));
}

void CountEdges(ILInstruction* inst, std::unordered_set<Block*>& seen) {
    if (!inst) return;
    if (auto* block = dynamic_cast<Block*>(inst)) seen.insert(block);
    if (auto* br = dynamic_cast<Branch*>(inst)) {
        if (br->TargetBlock) ++br->TargetBlock->IncomingEdgeCount;
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CountEdges(inst->GetChild(i), seen);
}

void ZeroBlocks(ILInstruction* inst) {
    if (!inst) return;
    if (auto* block = dynamic_cast<Block*>(inst)) block->IncomingEdgeCount = 0;
    for (int i = 0; i < inst->ChildCount(); ++i) ZeroBlocks(inst->GetChild(i));
}

// Also count positional fall-through edges: a block whose final is not
// EndPointUnreachable falls through to the next block in its container.
void CountFallThroughEdges(ILInstruction* inst) {
    if (!inst) return;
    if (auto* container = dynamic_cast<BlockContainer*>(inst)) {
        auto& blocks = container->Blocks;
        for (std::size_t i = 0; i + 1 < blocks.size(); ++i) {
            ILInstruction* fin = blocks[i]->FinalInstruction.get();
            if (fin && !HasFlag(fin->Flags(), InstructionFlags::EndPointUnreachable))
                ++blocks[i + 1]->IncomingEdgeCount;
        }
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CountFallThroughEdges(inst->GetChild(i));
}

} // namespace

void ComputeVariableUsage(ILFunction& function) {
    std::unordered_set<ILVariable*> all;
    for (auto& v : function.Variables) {
        if (!v) continue;
        v->LoadCount = 0;
        v->AddressCount = 0;
        v->StoreCount = (v->Kind == VariableKind::Parameter) ? 1 : 0;
        all.insert(v.get());
    }
    CountUsage(function.Body.get());
    // Variables referenced by the tree but not listed on the function (should
    // not happen) are counted but unreachable; the counts still land on them.
}

void RecomputeIncomingEdgeCounts(ILFunction& function) {
    ZeroBlocks(function.Body.get());
    std::unordered_set<Block*> seen;
    CountEdges(function.Body.get(), seen);
    CountFallThroughEdges(function.Body.get());
}

} // namespace ILSpy::Decompiler::IL
