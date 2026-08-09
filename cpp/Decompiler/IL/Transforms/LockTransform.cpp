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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/OpCode.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# checks `call.Method.DeclaringType.FullName == "System.Threading.Monitor"`
// and `call.Method.Name == methodName`. This port's Call carries the resolved
// display name as "Namespace.Type::Method", so the full name is the prefix and
// the method name is the suffix after "::".
bool IsMonitorMethod(const std::string& methodName, const char* shortName) {
    static const std::string prefix = "System.Threading.Monitor::";
    if (methodName.size() <= prefix.size()) return false;
    if (methodName.compare(0, prefix.size(), prefix) != 0) return false;
    std::string suffix = methodName.substr(prefix.size());
    return suffix == shortName;
}

// Match `call Monitor.<methodName>(ldloc v)` (the 1-argument form used by the
// no-flag lock shapes: Enter takes the lock object, Exit takes the lock
// object). Mirrors the C# MatchCall(call, methodName, params ILVariable[]).
bool MatchCall(ILInstruction* inst, const char* methodName, ILVariable* v) {
    auto* call = dynamic_cast<Call*>(inst);
    if (!call) return false;
    if (!IsMonitorMethod(call->MethodName, methodName)) return false;
    if (call->Arguments.size() != 1) return false;
    auto* ld = dynamic_cast<LdLoc*>(call->Arguments[0].get());
    return ld && ld->Variable.get() == v;
}

// Match a LdLoc of a specific variable (the C# MatchLdLoc(ILVariable)). The
// extract form (out ILVariable) is not needed here, so a single match helper
// avoids the lvalue-pointer overload ambiguity (see PatternMatchingTransform).
bool MatchLdLocVar(ILInstruction* inst, ILVariable* v) {
    auto* ld = dynamic_cast<LdLoc*>(inst);
    return ld && ld->Variable.get() == v;
}

// Match the 1-instruction exit block: `call Exit(ldloc obj)` alone (the C#
// `MatchExitBlock(Block exitBlock, ILVariable obj)` checks Count == 1 and the
// call). The endfinally `leave` is the block's FinalInstruction here, not a
// second non-terminal, so only Instructions[0] is checked.
bool MatchExitBlockCall(Block* exitBlock, ILVariable* obj) {
    if (!exitBlock) return false;
    if (exitBlock->Instructions.size() != 1) return false;
    return MatchCall(exitBlock->Instructions[0].get(), "Exit", obj);
}

// Match the finally entry block for the no-flag shapes (flag == nullptr in the
// C# `MatchExitBlock(Block entryPoint, ILVariable flag, ILVariable obj)`):
// `call Exit(ldloc obj)` followed by the endfinally `leave` targeting the
// finally container. In this port `call Exit` is Instructions[0] and the
// endfinally `leave` is the block's FinalInstruction (the C# carries both as
// non-terminals at Instructions[0]/[1]). Requires a single predecessor: the C#
// requires IncomingEdgeCount==1 (the container-entry edge the finally container
// contributes), but this port's RecomputeIncomingEdgeCounts does NOT count that
// edge (D59), so an EH-reached finally entry has IncomingEdgeCount==0 here and
// the check is ==0.
bool MatchExitBlockNoFlag(Block* entryPoint, ILVariable* obj) {
    if (!entryPoint || entryPoint->IncomingEdgeCount != 0) return false;
    if (entryPoint->Instructions.size() != 1) return false;
    if (!MatchCall(entryPoint->Instructions[0].get(), "Exit", obj)) return false;
    auto* finallyContainer = dynamic_cast<BlockContainer*>(entryPoint->Parent);
    if (!finallyContainer) return false;
    auto* leave = dynamic_cast<Leave*>(entryPoint->FinalInstruction.get());
    if (!leave) return false;
    // The endfinally leave targets its own finally container (the BlockBuilder
    // wires endfinally to the innermost finally container) and carries no
    // return value.
    if (leave->TargetContainer != finallyContainer) return false;
    if (leave->Value) return false;
    return true;
}

// The finally-block entry point of a BlockContainer (the first block). nullptr
// if the container is empty. (The C# BlockContainer.EntryPoint.)
Block* EntryPointOf(BlockContainer* container) {
    return (container && !container->Blocks.empty()) ? container->Blocks[0].get() : nullptr;
}

// Build the LockInstruction from the matched stloc (its Value is the lock
// expression) and the TryFinally (its TryBlock is the lock body), and splice it
// into the block in place of the TryFinally, removing the stloc at i-2 and the
// call at i-1. `body` is the TryFinally at block->Instructions[i]; `objectStore`
// is the StLoc at [i-2]. Both are raw pointers into the block's Instructions.
void BuildLockAndRewrite(Block* block, int i, StLoc* objectStore, TryFinally* body) {
    // Detach the lock expression (stloc's Value) and the try block (TryFinally's
    // TryBlock) before the old nodes are destroyed by the slot replacements and
    // removals below.
    std::unique_ptr<ILInstruction> onExpr = objectStore->TakeChild(0);
    std::unique_ptr<ILInstruction> tryBlock = body->TakeChild(0);
    auto lock = std::make_unique<LockInstruction>(std::move(onExpr), std::move(tryBlock));
    lock->Parent = block;
    lock->ChildIndex = i;

    // Replace the TryFinally (Instructions[i]) with the LockInstruction. The
    // old TryFinally shell (its TryBlock was just detached) is destroyed here.
    block->Instructions[static_cast<std::size_t>(i)] = std::move(lock);
    // Drop the call Enter (i-1) then the stloc (i-2). Remove the higher index
    // first so the lower index stays valid; the LockInstruction shifts left on
    // each erase.
    block->Instructions.erase(block->Instructions.begin() + (i - 1));
    block->Instructions.erase(block->Instructions.begin() + (i - 2));
    block->RenumberChildren();
}

// TransformLockMCS: `stloc lockObj(lockExpression); call Enter(ldloc lockObj);
// .try { ... } finally { call Exit(ldloc lockObj); leave }` ->
// `lock (lockExpression) { ... }`. lockObj is single-definition and loaded at
// most twice (the Enter arg and the Exit arg).
bool TransformLockMCS(Block* block, int i) {
    if (i < 2) return false;
    auto* body = dynamic_cast<TryFinally*>(block->Instructions[static_cast<std::size_t>(i)].get());
    if (!body) return false;
    auto* objectStore = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(i - 2)].get());
    if (!objectStore || !objectStore->Variable) return false;
    if (!objectStore->Variable->IsSingleDefinition()) return false;
    if (!MatchCall(block->Instructions[static_cast<std::size_t>(i - 1)].get(), "Enter", objectStore->Variable.get()))
        return false;
    auto* tryContainer = dynamic_cast<BlockContainer*>(body->TryBlock.get());
    if (!tryContainer) return false;
    auto* tryEntry = EntryPointOf(tryContainer);
    // C# requires IncomingEdgeCount==1 (the container-entry edge); this port
    // does not count it (D59), so an EH-reached try entry is ==0 here.
    if (!tryEntry || tryEntry->Instructions.empty() || tryEntry->IncomingEdgeCount != 0) return false;
    auto* finallyContainer = dynamic_cast<BlockContainer*>(body->FinallyBlock.get());
    if (!finallyContainer) return false;
    if (!MatchExitBlockNoFlag(EntryPointOf(finallyContainer), objectStore->Variable.get())) return false;
    if (objectStore->Variable->LoadCount > 2) return false;
    BuildLockAndRewrite(block, i, objectStore, body);
    return true;
}

// TransformLockV2: `stloc lockObj(ldloc tempVar); call Enter(ldloc tempVar);
// .try { ... } finally { call Exit(ldloc lockObj); leave }` ->
// `lock (ldloc tempVar) { ... }`. The Enter argument is the temp (not lockObj);
// lockObj is single-definition and loaded at most once (only the Exit arg).
bool TransformLockV2(Block* block, int i) {
    if (i < 2) return false;
    auto* body = dynamic_cast<TryFinally*>(block->Instructions[static_cast<std::size_t>(i)].get());
    if (!body) return false;
    auto* objectStore = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(i - 2)].get());
    if (!objectStore || !objectStore->Variable) return false;
    if (!objectStore->Variable->IsSingleDefinition()) return false;
    // objectStore.Value is ldloc tempVar.
    ILVariable* tempVar = nullptr;
    auto* ldTemp = dynamic_cast<LdLoc*>(objectStore->Value.get());
    if (!ldTemp || !(tempVar = ldTemp->Variable.get())) return false;
    if (!MatchCall(block->Instructions[static_cast<std::size_t>(i - 1)].get(), "Enter", tempVar))
        return false;
    auto* tryContainer = dynamic_cast<BlockContainer*>(body->TryBlock.get());
    if (!tryContainer) return false;
    auto* tryEntry = EntryPointOf(tryContainer);
    // C# requires IncomingEdgeCount==1 (the container-entry edge); this port
    // does not count it (D59), so an EH-reached try entry is ==0 here.
    if (!tryEntry || tryEntry->Instructions.empty() || tryEntry->IncomingEdgeCount != 0) return false;
    auto* finallyContainer = dynamic_cast<BlockContainer*>(body->FinallyBlock.get());
    if (!finallyContainer) return false;
    if (!MatchExitBlockNoFlag(EntryPointOf(finallyContainer), objectStore->Variable.get())) return false;
    if (objectStore->Variable->LoadCount > 1) return false;
    BuildLockAndRewrite(block, i, objectStore, body);
    return true;
}

void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

} // namespace

void LockTransform::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.LockStatement) return;

    // The IsSingleDefinition / LoadCount guards read fresh usage counts; the
    // C# maintains these incrementally, this port recomputes.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);

    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        for (std::size_t bi = 0; bi < container->Blocks.size(); ++bi) {
            Block* block = container->Blocks[bi].get();
            // Scan the block's non-terminal instructions in reverse (the C#
            // `for i = IndexOfFirstAlreadyTransformedInstruction - 1; i >= 0`).
            // A successful fold removes two instructions and replaces the
            // TryFinally, so re-scan from the new end (the C# sets i = Count).
            int i = static_cast<int>(block->Instructions.size()) - 1;
            while (i >= 0) {
                bool changed = false;
                if (TransformLockMCS(block, i)) changed = true;
                else if (TransformLockV2(block, i)) changed = true;
                if (changed) {
                    // Two instructions were removed and the TryFinally became a
                    // LockInstruction, so the block shrank by 2. Restart the
                    // reverse scan from the new end.
                    i = static_cast<int>(block->Instructions.size()) - 1;
                    context.StepOnce("LockTransform");
                    continue;
                }
                --i;
            }
        }
    }

    // The folds dropped stores/loads of the lock object; recompute so later
    // transforms and the seed read fresh counts.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
