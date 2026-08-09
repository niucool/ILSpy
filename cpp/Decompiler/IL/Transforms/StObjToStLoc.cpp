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

#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

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

void StObjToStLoc::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Collect first (ReplaceWith detaches mid-walk), then convert each
    // stobj(ldloca V, value) into stloc(V, value).
    std::vector<StObj*> stobjs;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (inst->Op == OpCode::StObj) stobjs.push_back(static_cast<StObj*>(inst));
    });
    for (StObj* st : stobjs) {
        if (!st->Target || st->Target->Op != OpCode::LdLoca) continue;
        auto* ldloca = static_cast<LdLoca*>(st->Target.get());
        if (!ldloca->Variable) continue;
        auto value = st->TakeChild(1);  // detach Value before replacing the StObj
        auto stloc = std::make_unique<StLoc>(ldloca->Variable, std::move(value));
        st->ReplaceWith(std::move(stloc));
    }
}

} // namespace ILSpy::Decompiler::IL
