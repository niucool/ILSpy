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
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
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

void RemoveRedundantReturn::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    if (!function.Body || function.Body->Blocks.empty()) return;
    BlockContainer* body = function.Body.get();
    Block* last = body->Blocks.back().get();
    if (!last->FinalInstruction) return;
    // A value-less Leave of the function body on the last block is a redundant
    // `return;` -- the block falls through to the implicit function exit.
    auto* leave = dynamic_cast<Leave*>(last->FinalInstruction.get());
    if (!leave || leave->TargetContainer != body) return;
    if (leave->Value) return;  // value return: not redundant
    // Do not remove a switch case body's exit `return;` -- it is the case
    // body's terminator, not a method-level trailing return. Removing it
    // makes the body fall through and breaks the seed's switch-inline analysis.
    if (IsSwitchCaseBody(body, last)) return;
    last->FinalInstruction.reset();
}

} // namespace ILSpy::Decompiler::IL
