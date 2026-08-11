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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

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

void FixRemainingIncrements::Run(ILFunction& function, ILTransformContext& context) {
    // Collect the calls to fix first (the C# builds a List<Call> then mutates,
    // to avoid mutating the tree while walking it). A call is fixable when it is
    // a 1-arg user-defined op_Increment/op_Decrement (gated on CheckedOperators
    // for the checked variants) whose declaring type is not System.Decimal
    // (Decimal is handled in the C# ReplaceMethodCallsWithOperators, a resolver
    // path this port does not model -- the call stays as a call).
    std::vector<Call*> callsToFix;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (!inst || inst->Op != OpCode::Call) return;
        auto* call = static_cast<Call*>(inst);
        if (!UserDefinedCompoundAssign::IsIncrementOrDecrement(call, &context.Settings))
            return;
        if (call->Arguments.size() != 1) return;
        if (NullableLiftingTransform::IsKnownType(call->DeclaringType.get(),
                                                  TypeSystem::KnownTypeCode::Decimal))
            return;
        callsToFix.push_back(call);
    });

    for (Call* call : callsToFix) {
        // The call may have been invalidated by an earlier iteration's rewrite
        // (it is a non-owning view). Re-check it is still connected and a Call.
        if (!call || !call->IsConnected() || call->Op != OpCode::Call) continue;

        // The primary branch: the call is a StLoc's Value (StLoc.ValueSlot) and
        // the StLoc is a non-terminal instruction in a Block (Block.Instruction-
        // Slot). This port does not model per-slot SlotInfo, so the check is
        // structural: call->Parent is a StLoc at ChildIndex 0 (the Value child),
        // and call->Parent->Parent is a Block whose Instructions[store->Child-
        // Index] is the store (the store is a non-terminal, not the final -- a
        // StLoc cannot be a block final, but the guard is faithful).
        ILInstruction* parent = call->Parent;
        if (parent && parent->Op == OpCode::StLoc && call->ChildIndex == 0) {
            auto* store = static_cast<StLoc*>(parent);
            ILInstruction* grandParent = store->Parent;
            if (grandParent && grandParent->Op == OpCode::Block) {
                auto* block = static_cast<Block*>(grandParent);
                // The store must be a non-terminal instruction (not the final).
                if (store->ChildIndex >= 0 &&
                    store->ChildIndex < static_cast<int>(block->Instructions.size()) &&
                    block->Instructions[static_cast<std::size_t>(store->ChildIndex)].get() == store) {
                    // stloc V(call op_Increment(expr))  ->  stloc V(expr); ++V
                    // Capture the method metadata before the call is destroyed by
                    // ReplaceWith (no GC; the non-owning views would dangle).
                    auto methodName = call->MethodName;
                    auto declaringType = call->DeclaringType;
                    auto returnType = call->ReturnType;
                    // Detach the argument, then replace the call with it (the
                    // call's Value slot becomes the argument).
                    auto arg = call->TakeChild(0);
                    call->ReplaceWith(std::move(arg));
                    // Build the compound assign and insert it after the store.
                    auto compoundAssign = std::make_unique<UserDefinedCompoundAssign>(
                        std::move(methodName), declaringType, returnType,
                        CompoundEvalMode::EvaluatesToNewValue,
                        std::make_unique<LdLoca>(store->Variable),
                        CompoundTargetKind::Address,
                        std::make_unique<LdcI4>(1));
                    block->InsertAt(static_cast<std::size_t>(store->ChildIndex) + 1,
                                    std::move(compoundAssign));
                    context.StepOnce("Fix remaining increment/decrement");
                    continue;
                }
            }
        }
        // The else branch (the call is in any other position) needs
        // ILInstruction.Extract (ILExtraction.cs -- the fresh-temporary-for-
        // expression machinery) and is deferred. The call stays as a call.
    }
}

} // namespace ILSpy::Decompiler::IL
