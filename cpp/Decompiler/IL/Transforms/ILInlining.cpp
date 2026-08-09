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

#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

bool VariableCanBeUsedForInlining(const ILVariable* v) {
    if (!v) return false;
    if (v->Kind == VariableKind::PinnedLocal) return false;
    if (v->StoreCount != 1) return false;
    if (v->LoadCount + v->AddressCount != 1) return false;
    return true;
}

// Result of searching for the load of `v` inside an instruction subtree.
enum class FindResultType { Found, Stop, Continue };
struct FindResult {
    FindResultType type;
    LdLoc* loadInst;  // valid when type == Found
};

// Find the single LdLoc(v) inside `expr` that can be replaced by
// `expressionBeingMoved`. Mirrors ILInlining.FindLoadInNext (subset: no SlotInfo
// restrictions, no named-argument handling, no ldloca inlining).
FindResult FindLoadInNext(ILInstruction* expr, ILVariable* v,
                          ILInstruction* expressionBeingMoved) {
    if (!expr) return {FindResultType::Stop, nullptr};
    if (expr->Op == OpCode::LdLoc) {
        auto* ld = static_cast<LdLoc*>(expr);
        if (ld->Variable.get() == v) return {FindResultType::Found, ld};
        if (MayReorder(expressionBeingMoved->Flags(), expr->Flags()))
            return {FindResultType::Continue, nullptr};
        return {FindResultType::Stop, nullptr};
    }
    if (expr->Op == OpCode::LdLoca) {
        auto* lda = static_cast<LdLoca*>(expr);
        if (lda->Variable.get() == v) return {FindResultType::Stop, nullptr};
        if (MayReorder(expressionBeingMoved->Flags(), expr->Flags()))
            return {FindResultType::Continue, nullptr};
        return {FindResultType::Stop, nullptr};
    }
    for (int i = 0; i < expr->ChildCount(); ++i) {
        FindResult r = FindLoadInNext(expr->GetChild(i), v, expressionBeingMoved);
        if (r.type != FindResultType::Continue) return r;
    }
    if (MayReorder(expressionBeingMoved->Flags(), expr->Flags()))
        return {FindResultType::Continue, nullptr};
    return {FindResultType::Stop, nullptr};
}

// Try to inline the StLoc at `pos` into the next instruction's load of its
// variable, or remove it if dead. Returns true if the stloc was consumed.
// `pos` may be out of range after a prior removal shrank the block (the
// per-statement driver loops at one position); guard and return false so the
// caller's while-loop terminates without reading out of bounds.
bool InlineOneIfPossible(Block* block, int pos, ILTransformContext& ctx) {
    if (pos < 0 || static_cast<std::size_t>(pos) >= block->Instructions.size()) return false;
    auto* stloc = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(pos)].get());
    if (!stloc) return false;
    ILVariable* v = stloc->Variable.get();
    if (!v) return false;

    if (VariableCanBeUsedForInlining(v)) {
        // The "next" instruction is the one after the stloc in the block, or
        // the block's final if the stloc is last (the C# counts the final in
        // its instruction list).
        ILInstruction* next = nullptr;
        if (static_cast<std::size_t>(pos) + 1 < block->Instructions.size())
            next = block->Instructions[static_cast<std::size_t>(pos) + 1].get();
        else
            next = block->FinalInstruction.get();
        auto value = stloc->TakeChild(0);
        FindResult r = FindLoadInNext(next, v, value.get());
        if (r.type == FindResultType::Found && r.loadInst) {
            ctx.StepOnce("Inline variable");
            r.loadInst->ReplaceWith(std::move(value));
            block->RemoveInstructionAt(static_cast<std::size_t>(pos));
            return true;
        }
        stloc->SetChild(0, std::move(value));  // restore, try dead-store below
    }

    // Dead store: variable never loaded. CFS handles stack slots too; this
    // catches locals and the case where the single load was in another block.
    if (v->LoadCount == 0 && v->AddressCount == 0) {
        auto value = stloc->TakeChild(0);
        if (IsPure(value->Flags())) {
            ctx.StepOnce("Remove dead store");
            block->RemoveInstructionAt(static_cast<std::size_t>(pos));
            return true;
        }
        if (v->Kind == VariableKind::StackSlot) {
            ctx.StepOnce("Remove dead store, keep expression");
            stloc->ReplaceWith(std::move(value));
            return true;
        }
        stloc->SetChild(0, std::move(value));
    }

    return false;
}

bool InlineAllInBlock(Block* block, ILTransformContext& ctx) {
    bool modified = false;
    for (int i = static_cast<int>(block->Instructions.size()) - 1; i >= 0; --i) {
        if (InlineOneIfPossible(block, i, ctx))
            modified = true;
    }
    return modified;
}

void Walk(ILInstruction* inst, const std::function<void(Block*)>& visit) {
    if (!inst) return;
    if (auto* b = dynamic_cast<Block*>(inst)) { visit(b); }
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

void ILInlining::Run(ILFunction& function, ILTransformContext& context) {
    ComputeVariableUsage(function);
    Walk(function.Body.get(), [&](Block* block) {
        InlineAllInBlock(block, context);
    });
    // Recompute usage so dead variables (inlined away) can be dropped from the
    // function's variable list (mirrors function.Variables.RemoveDead()).
    ComputeVariableUsage(function);
    auto& vars = function.Variables;
    vars.erase(
        std::remove_if(vars.begin(), vars.end(),
                        [](const ILVariablePtr& v) {
                            return v && v->StoreCount == 0 && v->LoadCount == 0 &&
                                   v->AddressCount == 0;
                        }),
        vars.end());
}

// IStatementTransform entry: the per-statement inlining pass the
// StatementTransform runs interleaved with the other per-statement transforms.
// Loops InlineOneIfPossible at `pos` until no change, mirroring the C#
// ILInlining.Run(Block, pos, ctx) overload. After a successful inline at the
// last position the block shrank (the final is separate in this port, so the
// last non-terminal is at Instructions.size()-1; RemoveInstructionAt drops the
// size by one), so the while guard re-checks `pos < size` and stops without
// reading out of bounds (the C# avoids this because the final lives in
// Instructions, so the last non-terminal is at Count-2 and the shifted-in
// instruction stays in range).
void ILInlining::Run(Block& block, int pos, StatementTransformContext& context) {
    while (pos >= 0 && static_cast<std::size_t>(pos) < block.Instructions.size()
           && InlineOneIfPossible(&block, pos, context.Base)) {
        // repeat inlining until nothing changes
    }
}

} // namespace ILSpy::Decompiler::IL
