// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

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

// Match `ldloca v` (the C# MatchLdLoca(ILVariable)).
bool MatchLdLocaVar(ILInstruction* inst, ILVariable* v) {
    auto* la = dynamic_cast<LdLoca*>(inst);
    return la && la->Variable.get() == v;
}

// Unwrap a ParameterizedType to its generic definition's KnownTypeCode (a
// resolved declaring type may be the parameterized instantiation, e.g.
// Nullable<int>, rather than the generic definition). Mirrors
// NullableLiftingTransform's KnownTypeCodeOf.
TypeSystem::KnownTypeCode KnownTypeCodeOf(const TypeSystem::IType* type) {
    if (!type) return TypeSystem::KnownTypeCode::None;
    if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(type))
        return kt->Code();
    if (auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(type)) {
        const auto& g = pt->GenericType();
        return KnownTypeCodeOf(g.get());
    }
    return TypeSystem::KnownTypeCode::None;
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

// Match the flag-based finally as it appears in this port after
// ConditionDetection: TWO blocks, not the C# single block. The C# carries
// `if (ldloc flag) Block { call Exit } ; leave` as one block (the if at
// Instructions[0], the leave at Instructions[1]); this port's if-as-final block
// model and ConditionDetection leave the brfalse-skip as a separate first
// block, so the finally is:
//   block[0] (incoming 0): Instructions=[], Final = if (comp(eq, flag, 0)) leave
//     -- the `if (!flag) leave` skip (TrueInst = leave targeting the finally
//     container, FalseInst = nullptr, fall-through to block[1]).
//   block[1] (incoming 1): Instructions = [call Exit(ldloc obj)],
//     Final = leave (finally container).
// (ConditionDetection's TryInlineIfFallThrough gate blocks inlining the leave
// into the if's FalseInst -- the true arm's trailing Branch and the leave are
// not a compatible common exit -- so the leave stays the next block.) Sets
// objOut to the Exit's argument variable.
bool MatchExitBlockFlag(BlockContainer* fin, ILVariable* flag, ILVariable** objOut) {
    if (!fin || fin->Blocks.size() != 2) return false;
    Block* b0 = fin->Blocks[0].get();
    Block* b1 = fin->Blocks[1].get();
    if (!b0->Instructions.empty()) return false;
    auto* iff = dynamic_cast<IfInstruction*>(b0->FinalInstruction.get());
    if (!iff || iff->FalseInst) return false;
    // Condition: comp(eq, ldloc flag, ldc.i4 0)  (the reader's brfalse shape).
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    if (!cond || cond->Kind != ComparisonKind::Equality) return false;
    if (!MatchLdLocVar(cond->Left.get(), flag)) return false;
    auto* zero = dynamic_cast<LdcI4*>(cond->Right.get());
    if (!zero || zero->Value != 0) return false;
    // TrueInst: the skip -- a leave targeting the finally container, no value.
    auto* skipLeave = dynamic_cast<Leave*>(iff->TrueInst.get());
    if (!skipLeave || skipLeave->TargetContainer != fin || skipLeave->Value) return false;
    // block[1]: exactly `call Exit(ldloc obj)` then the endfinally leave.
    if (b1->Instructions.size() != 1) return false;
    auto* exitCall = dynamic_cast<Call*>(b1->Instructions[0].get());
    if (!exitCall || !IsMonitorMethod(exitCall->MethodName, "Exit")) return false;
    if (exitCall->Arguments.size() != 1) return false;
    auto* ldObj = dynamic_cast<LdLoc*>(exitCall->Arguments[0].get());
    if (!ldObj || !ldObj->Variable) return false;
    auto* leave2 = dynamic_cast<Leave*>(b1->FinalInstruction.get());
    if (!leave2 || leave2->TargetContainer != fin || leave2->Value) return false;
    *objOut = ldObj->Variable.get();
    return true;
}

// Match `call Monitor.Enter(ldloc obj, ldloca flag)` (the 2-argument Roslyn/V4
// shape). Mirrors the C# MatchCall(call, "Enter", obj, flag) params overload.
// Sets objOut to the lock-object variable (the Enter's first argument).
bool MatchEnter2(ILInstruction* inst, ILVariable** objOut, ILVariable* flag) {
    auto* call = dynamic_cast<Call*>(inst);
    if (!call) return false;
    if (!IsMonitorMethod(call->MethodName, "Enter")) return false;
    if (call->Arguments.size() != 2) return false;
    auto* ld = dynamic_cast<LdLoc*>(call->Arguments[0].get());
    if (!ld || !ld->Variable) return false;
    if (!MatchLdLocaVar(call->Arguments[1].get(), flag)) return false;
    *objOut = ld->Variable.get();
    return true;
}

// The finally-block entry point of a BlockContainer (the first block). nullptr
// if the container is empty. (The C# BlockContainer.EntryPoint.)
Block* EntryPointOf(BlockContainer* container) {
    return (container && !container->Blocks.empty()) ? container->Blocks[0].get() : nullptr;
}

// Validate a TryFinally as a Roslyn flag-lock: the try entry's first
// instruction is `call Enter(ldloc obj, ldloca flag)` and the finally matches
// the two-block flag shape; the Enter and Exit operate on the same object.
// Sets objOut to that object. Returns false if any check fails.
bool ValidateRoslynTryFinally(TryFinally* tf, ILVariable* flag, ILVariable** objOut) {
    auto* trc = dynamic_cast<BlockContainer*>(tf->TryBlock.get());
    if (!trc || trc->Blocks.empty()) return false;
    Block* te = trc->Blocks[0].get();
    if (te->Instructions.empty()) return false;
    ILVariable* enterObj = nullptr;
    if (!MatchEnter2(te->Instructions[0].get(), &enterObj, flag)) return false;
    auto* fin = dynamic_cast<BlockContainer*>(tf->FinallyBlock.get());
    ILVariable* exitObj = nullptr;
    if (!MatchExitBlockFlag(fin, flag, &exitObj)) return false;
    if (enterObj != exitObj) return false;
    *objOut = enterObj;
    return true;
}

// Build the LockInstruction from the matched stloc (its Value is the lock
// expression) and the TryFinally (its TryBlock is the lock body), and splice it
// into the block in place of the TryFinally, removing the stloc at i-2 and the
// call at i-1. `body` is the TryFinally at block->Instructions[i]; `objectStore`
// is the StLoc at [i-2]. Both are raw pointers into the block's Instructions.
// (Shared by the no-flag MCS/V2 shapes, where the stloc/call/TryFinally sit in
// one block after CFS merges the EH wrapper.)
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

// TransformLockRoslyn: `stloc obj(lockExpression); stloc flag(ldc.i4 0);
// .try { call Enter(ldloc obj, ldloca flag); body } finally { if (!flag) leave;
// call Exit(ldloc obj); leave }` -> `lock (lockExpression) { body }`.
//
// The stloc obj / stloc flag may sit in the SAME block as the TryFinally
// (immediately before it, the C# shape -- Instructions[i-2]/[i-1]/[i]) OR, when
// CFS did not merge the EH wrapper with the preceding block (the dominant
// mscorlib case -- the fall-through branch into the TryFinally resolves to the
// try entry inside the try container, not the wrapper, so the wrapper has
// IncomingEdgeCount==0 and CFS leaves the stlocs in a separate preceding
// block), in the PRECEDING block in the same container (its last two
// instructions). The flag must be Boolean and initialised to 0; obj must be
// single-definition and loaded at most twice (the Enter arg and the Exit arg).
//
// For the preceding-block case the fold absorbs the TryFinally's block final
// (the after-lock continuation) into the preceding block and drops the now-empty
// TryFinally block: the preceding block's `br` into the try (which would dangle
// into the lock body once the TryFinally becomes a LockInstruction) is
// discarded, since the LockInstruction subsumes the try entry. The dropped
// block is moved to `graveyard` (kept alive until Run returns) so the container
// iteration stays valid. Sets *droppedBlock when a block was dropped (the
// caller restarts the container scan).
bool TransformLockRoslyn(BlockContainer* container, std::size_t bi, Block* block, int i,
                         std::vector<std::unique_ptr<Block>>& graveyard, bool* droppedBlock) {
    *droppedBlock = false;
    auto* tf = dynamic_cast<TryFinally*>(block->Instructions[static_cast<std::size_t>(i)].get());
    if (!tf) return false;

    // Locate the stloc flag(ldc.i4 0) and stloc obj(lockExpression): either in
    // the same block (immediately before the TryFinally) or in the preceding
    // block in this container (its last two instructions).
    StLoc* flagStore = nullptr;
    StLoc* objStore = nullptr;
    Block* stlocBlock = nullptr;
    int flagIdx = -1, objIdx = -1;
    bool sameBlock = false;
    if (i >= 2) {
        auto* fs = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(i - 1)].get());
        auto* os = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(i - 2)].get());
        if (fs && os) {
            flagStore = fs; objStore = os; stlocBlock = block;
            flagIdx = i - 1; objIdx = i - 2; sameBlock = true;
        }
    }
    if (!sameBlock) {
        if (bi == 0) return false;
        Block* prev = container->Blocks[bi - 1].get();
        if (prev->Instructions.size() < 2) return false;
        auto* fs = dynamic_cast<StLoc*>(prev->Instructions[prev->Instructions.size() - 1].get());
        auto* os = dynamic_cast<StLoc*>(prev->Instructions[prev->Instructions.size() - 2].get());
        if (!fs || !os) return false;
        flagStore = fs; objStore = os; stlocBlock = prev;
        flagIdx = static_cast<int>(prev->Instructions.size()) - 1;
        objIdx = static_cast<int>(prev->Instructions.size()) - 2;
    }
    if (!flagStore->Variable || !objStore->Variable) return false;
    // flag is Boolean and initialised to 0.
    if (KnownTypeCodeOf(flagStore->Variable->Type.get()) != TypeSystem::KnownTypeCode::Boolean) return false;
    auto* zero = dynamic_cast<LdcI4*>(flagStore->Value.get());
    if (!zero || zero->Value != 0) return false;
    // try entry = call Enter(ldloc obj, ldloca flag); finally = two-block flag.
    ILVariable* obj = nullptr;
    if (!ValidateRoslynTryFinally(tf, flagStore->Variable.get(), &obj)) return false;
    // The stloc obj is the lock object Enter/Exit operate on.
    if (obj != objStore->Variable.get()) return false;
    if (!objStore->Variable->IsSingleDefinition()) return false;
    if (objStore->Variable->LoadCount > 2) return false;

    // Erase the `call Enter` from the try entry (do this before detaching the
    // try block, since the try entry lives inside it).
    auto* trc = static_cast<BlockContainer*>(tf->TryBlock.get());
    trc->Blocks[0]->RemoveInstructionAt(0);

    // Detach the lock expression (stloc obj's Value) and the try block.
    std::unique_ptr<ILInstruction> lockExpr = objStore->TakeChild(0);
    std::unique_ptr<ILInstruction> tryBlock = tf->TakeChild(0);
    auto lock = std::make_unique<LockInstruction>(std::move(lockExpr), std::move(tryBlock));

    if (sameBlock) {
        // Replace the TryFinally (Instructions[i]) with the LockInstruction and
        // drop the stloc flag (i-1) and stloc obj (i-2).
        lock->Parent = block;
        lock->ChildIndex = i;
        block->Instructions[static_cast<std::size_t>(i)] = std::move(lock);  // destroys tf shell
        block->Instructions.erase(block->Instructions.begin() + (i - 1));
        block->Instructions.erase(block->Instructions.begin() + (i - 2));
        block->RenumberChildren();
        return true;
    }

    // Preceding-block case: put the LockInstruction in the preceding block
    // (replacing the two stlocs) and absorb the TryFinally's block final into
    // it; drop the now-empty TryFinally block.
    Block* prev = stlocBlock;  // == container->Blocks[bi - 1]
    // Erase the stloc flag (flagIdx) then the stloc obj (objIdx); the higher
    // index first so the lower stays valid.
    prev->Instructions.erase(prev->Instructions.begin() + flagIdx);
    prev->Instructions.erase(prev->Instructions.begin() + objIdx);
    prev->RenumberChildren();
    // Append the LockInstruction to the preceding block.
    lock->Parent = prev;
    lock->ChildIndex = static_cast<int>(prev->Instructions.size());
    prev->Instructions.push_back(std::move(lock));
    // The preceding block's old final (the `br` into the try) is discarded --
    // the LockInstruction subsumes the try entry -- and replaced by the
    // TryFinally's block final (the after-lock continuation).
    block->FinalInstruction->Parent = prev;
    block->FinalInstruction->ChildIndex = static_cast<int>(prev->Instructions.size());
    prev->FinalInstruction = std::move(block->FinalInstruction);
    // The TryFinally shell (TryBlock detached, FinallyBlock still owned) is
    // destroyed when its slot is cleared.
    block->Instructions.clear();
    block->RenumberChildren();
    // Drop the now-empty TryFinally block from the container, keeping it alive
    // in the graveyard so the caller's iteration stays valid.
    auto it = container->Blocks.begin() + static_cast<std::ptrdiff_t>(bi);
    graveyard.push_back(std::move(*it));
    container->Blocks.erase(it);
    for (std::size_t k = 0; k < container->Blocks.size(); ++k) {
        container->Blocks[k]->ChildIndex = static_cast<int>(k);
        container->Blocks[k]->Parent = container;
    }
    *droppedBlock = true;
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

    // Dropped blocks (the preceding-block Roslyn fold) are kept alive here
    // until Run returns so container iteration over them stays valid.
    std::vector<std::unique_ptr<Block>> graveyard;
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        // A fold that drops a block shifts the container's block indices, so
        // restart the container scan whenever that happens.
        bool restart = true;
        while (restart) {
            restart = false;
            for (std::size_t bi = 0; bi < container->Blocks.size() && !restart; ++bi) {
                Block* block = container->Blocks[bi].get();
                // Scan the block's non-terminal instructions in reverse (the C#
                // `for i = IndexOfFirstAlreadyTransformedInstruction - 1; i >= 0`).
                int i = static_cast<int>(block->Instructions.size()) - 1;
                while (i >= 0) {
                    bool changed = false;
                    bool dropped = false;
                    if (TransformLockRoslyn(container, bi, block, i, graveyard, &dropped)) {
                        changed = true;
                        if (dropped) { restart = true; break; }
                    } else if (TransformLockMCS(block, i)) {
                        changed = true;
                    } else if (TransformLockV2(block, i)) {
                        changed = true;
                    }
                    if (changed && !restart) {
                        // Two instructions were removed and the TryFinally
                        // became a LockInstruction (or the stlocs were folded),
                        // so re-scan this block from the new end.
                        i = static_cast<int>(block->Instructions.size()) - 1;
                        context.StepOnce("LockTransform");
                        continue;
                    }
                    if (restart) break;
                    --i;
                }
            }
        }
    }

    // The folds dropped stores/loads of the lock object; recompute so later
    // transforms and the seed read fresh counts.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
