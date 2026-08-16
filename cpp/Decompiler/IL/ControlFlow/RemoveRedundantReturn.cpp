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

#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include <functional>

namespace ILSpy::Decompiler::IL {

namespace {

// Whether `block` is a switch case body: some SwitchInstruction in the same
// container has a section whose Body is a Branch to `block`. Such a block's
// trailing `return;` is the case body's exit, NOT the method-level trailing
// return -- removing it would change the body from self-terminating to
// fall-through and break the seed's switch-inline analysis (the body would
// need to flow into the next section but doesn't). The C# RemoveRedundantReturn
// avoids this by only recursing into try/lock/using/if bodies (NOT switch), so
// it never touches a case body's return; this gate is the port's structural
// equivalent.
bool IsSwitchCaseBody(BlockContainer* container, Block* block) {
    if (!container || !block) return false;
    // Walk the whole container subtree for SwitchInstructions (a switch may
    // be nested in an if-arm -- the `no-outer` shape -- with its case bodies in
    // this container). A section whose Body is a Branch to `block` makes
    // `block` a case body.
    std::function<bool(const ILInstruction*)> walk = [&](const ILInstruction* inst) -> bool {
        if (!inst) return false;
        if (auto* sw = dynamic_cast<const SwitchInstruction*>(inst)) {
            for (const auto& section : sw->Sections) {
                if (!section || !section->Body) continue;
                auto* br = dynamic_cast<const Branch*>(section->Body.get());
                if (br && br->TargetBlock == block) return true;
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i)
            if (walk(inst->GetChild(i))) return true;
        return false;
    };
    return walk(container);
}

} // namespace

// Whether `inst` is a construct/if whose body/arms ConvertReturnToFallthrough
// recurses into (try/catch/lock/using/if, or a BlockContainer body).
bool IsConstructOrIf(const ILInstruction* inst) {
    return inst && (dynamic_cast<const BlockContainer*>(inst) ||
                    dynamic_cast<const TryFinally*>(inst) ||
                    dynamic_cast<const TryCatch*>(inst) ||
                    dynamic_cast<const LockInstruction*>(inst) ||
                    dynamic_cast<const UsingInstruction*>(inst) ||
                    dynamic_cast<const IfInstruction*>(inst));
}

void ConvertReturnToFallthrough(ILInstruction* inst, BlockContainer* fnBody);

// Handle a block's trailing return (the C# RemoveRedundantReturn `case Block` +
// `case BlockContainer`, adapted to the port's block model). The block's
// `FinalInstruction` is its control flow; its `Instructions` are the straight-
// line non-terminals BEFORE the final. A trailing `return;` (a value-less Leave
// of `fnBody`) is redundant when the block is the last reachable block of its
// container (recursively the method's last statement): drop it. Then recurse
// into the block's "last instruction" -- the construct/if final, OR (when the
// return was the final and got dropped) the last non-terminal (a construct like
// a TryFinally that sat before the return). `switchGateContainer` is the
// container for the IsSwitchCaseBody gate (the method body / a nested
// container); null for an if-arm (arms are not switch case bodies).
void ConvertBlockTrailingReturn(Block* b, BlockContainer* fnBody,
                                 BlockContainer* switchGateContainer) {
    if (!b) return;
    ILInstruction* fin = b->FinalInstruction.get();
    bool removedReturn = false;
    if (fin) {
        auto* leave = dynamic_cast<Leave*>(fin);
        if (leave && leave->TargetContainer == fnBody && !leave->Value &&
            (!switchGateContainer || !IsSwitchCaseBody(switchGateContainer, b))) {
            b->FinalInstruction.reset();  // `return;` -> fallthrough
            removedReturn = true;
            fin = nullptr;
        }
    }
    // Recurse into the block's "last instruction": the construct/if final, or
    // (when the return final was dropped) the last non-terminal -- a construct
    // (TryFinally/TryCatch/Lock/Using/If/BlockContainer) that sat before the
    // return. The port puts constructs as block FINALS (control flow), but a
    // method body's last block can carry a TryFinally as a NON-TERMINAL with a
    // Leave final (the `try {...; return;} finally {...}` shape: the block is
    // [TryFinally]; Leave(fnBody)), so the construct is the last non-terminal,
    // not the final.
    ILInstruction* recurseTarget = nullptr;
    if (fin && IsConstructOrIf(fin))
        recurseTarget = fin;
    else if (removedReturn && !b->Instructions.empty())
        recurseTarget = b->Instructions.back().get();
    if (recurseTarget && IsConstructOrIf(recurseTarget))
        ConvertReturnToFallthrough(recurseTarget, fnBody);
}

// ConvertReturnToFallthrough (the C# RemoveRedundantReturn.ConvertReturnToFallthrough):
// recurse into a construct/if body and convert a trailing `return;` to a
// fallthrough. Only recurses into try/catch/lock/using/if bodies (NOT switch --
// a switch case body's return is its terminator, gated by IsSwitchCaseBody).
void ConvertReturnToFallthrough(ILInstruction* inst, BlockContainer* fnBody) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
        if (!c->Blocks.empty())
            ConvertBlockTrailingReturn(c->Blocks.back().get(), fnBody, c);
        return;
    }
    if (auto* tf = dynamic_cast<TryFinally*>(inst)) {
        ConvertReturnToFallthrough(tf->TryBlock.get(), fnBody);
        return;
    }
    if (auto* tc = dynamic_cast<TryCatch*>(inst)) {
        ConvertReturnToFallthrough(tc->TryBlock.get(), fnBody);
        for (const auto& h : tc->Handlers)
            if (h) ConvertReturnToFallthrough(h->Body.get(), fnBody);
        return;
    }
    if (auto* lk = dynamic_cast<LockInstruction*>(inst)) {
        ConvertReturnToFallthrough(lk->Body.get(), fnBody);
        return;
    }
    if (auto* us = dynamic_cast<UsingInstruction*>(inst)) {
        ConvertReturnToFallthrough(us->Body.get(), fnBody);
        return;
    }
    if (auto* iff = dynamic_cast<IfInstruction*>(inst)) {
        // Both arms lead to the method end (the if is the last statement), so a
        // trailing return in either arm is redundant.
        ConvertReturnToFallthrough(iff->TrueInst.get(), fnBody);
        ConvertReturnToFallthrough(iff->FalseInst.get(), fnBody);
        return;
    }
    if (auto* b = dynamic_cast<Block*>(inst)) {
        // An if-arm Block: handle its trailing return (no switch gate -- an arm
        // is not a switch case body).
        ConvertBlockTrailingReturn(b, fnBody, nullptr);
        return;
    }
}

void RemoveRedundantReturn::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    if (!function.Body || function.Body->Blocks.empty()) return;
    // D214 + the ConvertReturnToFallthrough recursion: handle the method body's
    // last block -- drop a redundant trailing `return;` (value-less Leave of the
    // function body, not a switch case body), then recurse into the block's
    // construct/if (the final, or the last non-terminal when the return was the
    // final) to convert trailing returns inside its body/arms.
    ConvertBlockTrailingReturn(function.Body->Blocks.back().get(),
                                function.Body.get(), function.Body.get());
}

} // namespace ILSpy::Decompiler::IL
