// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/UsingTransform.hpp"
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
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The canonical Dispose method name this port's Call carries (the reader
// resolves it to "Namespace.Type::Method"). The C# default
// disposeMethodFullName is "System.IDisposable.Dispose".
const char* const kDisposeMethod = "System.IDisposable::Dispose";

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

bool MatchLdLocVar(ILInstruction* inst, ILVariable* v) {
    auto* ld = dynamic_cast<LdLoc*>(inst);
    return ld && ld->Variable.get() == v;
}

bool MatchLdLocaVar(ILInstruction* inst, ILVariable* v) {
    auto* la = dynamic_cast<LdLoca*>(inst);
    return la && la->Variable.get() == v;
}

bool MatchLdNullInst(ILInstruction* inst) {
    return dynamic_cast<LdNull*>(inst) != nullptr;
}

// Unwrap a ParameterizedType to its generic definition's KnownTypeCode (a
// resolved type may be the parameterized instantiation rather than the generic
// definition). Mirrors NullableLiftingTransform's KnownTypeCodeOf.
TypeSystem::KnownTypeCode KnownTypeCodeOf(const TypeSystem::IType* type) {
    if (!type) return TypeSystem::KnownTypeCode::None;
    if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(type)) return kt->Code();
    if (auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(type)) {
        return KnownTypeCodeOf(pt->GenericType().get());
    }
    return TypeSystem::KnownTypeCode::None;
}

// The C# checks `disposableType.IsKnownType(KnownTypeCode.IDisposable)`. This
// port unwraps a ParameterizedType before reading the KnownType's code; if the
// type did not resolve to a KnownType, fall back to the ReflectionName (the
// isinst's type renders as "System.IDisposable").
bool IsIDisposableType(const TypeSystem::IType* type) {
    if (KnownTypeCodeOf(type) == TypeSystem::KnownTypeCode::IDisposable) return true;
    if (type && type->ReflectionName() == "System.IDisposable") return true;
    return false;
}

// Permissive (D74): this port's minimal type system cannot check
// GetAllBaseTypes().Any(IDisposable), so the structural MatchDisposeBlock (the
// `System.IDisposable::Dispose` call name) is the real proof. The non-generic
// IEnumerator / foreach-pattern special case is deferred.
bool CheckResourceType(const TypeSystem::IType* type) {
    (void)type;
    return true;
}

// A leave targeting `fin` with no value (the endfinally leave, or the skip-leave
// in the null-check if's TrueInst). Mirrors the C# `MatchLeave(container, out
// returnValue)` + `returnValue.MatchNop()`.
bool MatchLeaveFin(ILInstruction* inst, BlockContainer* fin) {
    auto* leave = dynamic_cast<Leave*>(inst);
    return leave && leave->TargetContainer == fin && !leave->Value;
}

// The null-check if as it appears in this port after ConditionDetection:
// `if (comp(eq, ldloc obj, ldnull)) leave(fin)` -- empty Instructions, if-final,
// TrueInst = the skip-leave (targeting fin, no value), FalseInst = nullptr
// (fall-through to the Dispose block). The condition is the reader's brfalse
// form (`obj == null` -> skip), inverted vs the C# `obj != null` but
// equivalent. Either operand order is accepted (ldloc/ldnull on either side).
bool MatchIfNullCheck(ILInstruction* inst, BlockContainer* fin, ILVariable* objVar) {
    auto* iff = dynamic_cast<IfInstruction*>(inst);
    if (!iff || iff->FalseInst) return false;  // FalseInst must be nullptr (fall-through)
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    if (!cond || cond->Kind != ComparisonKind::Equality || cond->Unsigned) return false;
    ILInstruction* left = cond->Left.get();
    ILInstruction* right = cond->Right.get();
    bool ok = (MatchLdLocVar(left, objVar) && MatchLdNullInst(right))
           || (MatchLdLocVar(right, objVar) && MatchLdNullInst(left));
    if (!ok) return false;
    return MatchLeaveFin(iff->TrueInst.get(), fin);
}

// A Dispose block: exactly `call System.IDisposable::Dispose(arg)` then the
// endfinally `leave(fin)`. The argument is `ldloc obj`, `ldloca obj` (a struct
// disposed by address), or -- for `using (null)` -- `ldnull`.
bool MatchDisposeCallBlock(Block* b, BlockContainer* fin, ILVariable* objVar, bool usingNull) {
    if (!b || b->Instructions.size() != 1) return false;
    auto* call = dynamic_cast<Call*>(b->Instructions[0].get());
    if (!call || call->MethodName != kDisposeMethod) return false;
    if (call->Arguments.size() != 1) return false;
    ILInstruction* arg = call->Arguments[0].get();
    bool argOk = MatchLdLocVar(arg, objVar) || MatchLdLocaVar(arg, objVar)
              || (usingNull && MatchLdNullInst(arg));
    if (!argOk) return false;
    return MatchLeaveFin(b->FinalInstruction.get(), fin);
}

// MatchDisposeBlock: validate `fin` (the finally container) as a using-finally
// that disposes `objVar` (the resource). Returns true and sets *checkObjOut to
// the variable actually disposed (== objVar, or the isinst-temp for Shape C).
// Handles the three corpus shapes (see UsingTransform.hpp). The C# requires the
// finally entry IncomingEdgeCount == 1 (it counts the container-entry edge);
// this port does not count it (D59), so an EH-reached finally entry is == 0.
bool MatchDisposeBlock(BlockContainer* fin, ILVariable* objVar, bool usingNull,
                       ILVariable** checkObjOut) {
    if (!fin || fin->Blocks.empty()) return false;
    Block* entry = fin->Blocks[0].get();
    if (entry->IncomingEdgeCount != 0) return false;

    // Shape C: optional isinst-temp at entry->Instructions[0].
    ILVariable* checkObj = objVar;
    bool isinstTemp = false;
    if (entry->Instructions.size() >= 1) {
        auto* st = dynamic_cast<StLoc*>(entry->Instructions[0].get());
        auto* isinst = st ? dynamic_cast<IsInst*>(st->Value.get()) : nullptr;
        if (isinst && MatchLdLocVar(isinst->Argument.get(), objVar) && IsIDisposableType(isinst->Type.get())) {
            if (!st->Variable || !st->Variable->IsSingleDefinition()) return false;
            checkObj = st->Variable.get();
            isinstTemp = true;
        }
    }

    if (isinstTemp) {
        // Shape C: two-block. The if is entry's FinalInstruction; fin[1] is Dispose.
        if (fin->Blocks.size() != 2) return false;
        if (!MatchIfNullCheck(entry->FinalInstruction.get(), fin, checkObj)) return false;
        if (!MatchDisposeCallBlock(fin->Blocks[1].get(), fin, checkObj, usingNull)) return false;
        // The temp is loaded only in the null check and the Dispose (== 2).
        if (checkObj->LoadCount != 2) return false;
        if (checkObjOut) *checkObjOut = checkObj;
        return true;
    }

    if (entry->Instructions.empty() && entry->FinalInstruction
        && entry->FinalInstruction->Op == OpCode::IfInstruction) {
        // Shape A: two-block. entry is the if (FinalInstruction); fin[1] is Dispose.
        if (fin->Blocks.size() != 2) return false;
        if (!MatchIfNullCheck(entry->FinalInstruction.get(), fin, objVar)) return false;
        if (!MatchDisposeCallBlock(fin->Blocks[1].get(), fin, objVar, usingNull)) return false;
        if (checkObjOut) *checkObjOut = objVar;
        return true;
    }

    if (fin->Blocks.size() == 1 && entry->Instructions.size() == 1) {
        // Shape B: one-block. entry->Instructions[0] is the Dispose call (ldloca
        // obj), final is leave. No null check (struct / ref-struct).
        if (!MatchDisposeCallBlock(entry, fin, objVar, usingNull)) return false;
        if (checkObjOut) *checkObjOut = objVar;
        return true;
    }

    return false;
}

// All loads (LdLoc) of `v` are descendants of `ancestor` (the TryFinally). The
// C# uses per-variable LoadInstructions lists (deferred); this port walks the
// tree. A load outside the TryFinally means the resource escapes the using.
bool CheckAllLoadsInside(ILVariable* v, ILInstruction* root, const ILInstruction* ancestor) {
    bool ok = true;
    Walk(root, [&](ILInstruction* n) {
        auto* ld = dynamic_cast<LdLoc*>(n);
        if (ld && ld->Variable.get() == v && !ld->IsDescendantOf(ancestor)) ok = false;
    });
    return ok;
}

// All addresses (LdLoca) of `v` are descendants of `ancestor` (the TryFinally).
// The C# ValidateAddressUse additionally checks TryBlock addresses are
// this-pointer/in-parameter uses -- a rendering nicety deferred here.
bool CheckAllAddressesInside(ILVariable* v, ILInstruction* root, const ILInstruction* ancestor) {
    bool ok = true;
    Walk(root, [&](ILInstruction* n) {
        auto* la = dynamic_cast<LdLoca*>(n);
        if (la && la->Variable.get() == v && !la->IsDescendantOf(ancestor)) ok = false;
    });
    return ok;
}

// Find the resource stloc for a TryFinally at block->Instructions[tfIdx]: either
// same-block (Instructions[tfIdx-1]) or, when the TryFinally is alone in its
// block (tfIdx == 0), the preceding block's last instruction (the dominant
// mscorlib case, requiring the preceding block's final to branch into this
// block). Sets storeInst/stlocBlock/stlocIdx/prevBlock/sameBlock on success.
struct ResourceStore {
    StLoc* storeInst = nullptr;
    Block* stlocBlock = nullptr;
    int stlocIdx = -1;
    Block* prevBlock = nullptr;
    bool sameBlock = false;
};
bool FindResourceStore(BlockContainer* container, std::size_t bi, Block* block, int tfIdx,
                       TryFinally* tf, ResourceStore& out) {
    if (tfIdx >= 1) {
        auto* s = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(tfIdx - 1)].get());
        if (s) {
            out.storeInst = s; out.stlocBlock = block; out.stlocIdx = tfIdx - 1; out.sameBlock = true;
            return true;
        }
    }
    if (tfIdx == 0 && bi >= 1) {
        Block* prev = container->Blocks[bi - 1].get();
        if (!prev->Instructions.empty()) {
            auto* s = dynamic_cast<StLoc*>(prev->Instructions.back().get());
            auto* br = dynamic_cast<Branch*>(prev->FinalInstruction.get());
            // The preceding block's final must branch into the TryFinally. The
            // BlockBuilder quirk (D73) resolves that branch's TargetBlock to the
            // try ENTRY -- a block inside the TryFinally's try container sharing
            // the .try start offset -- instead of the TryFinally wrapper block
            // (this block). So accept a target that is this block (the normal
            // case) OR a block inside the TryFinally (the quirk case).
            if (s && br && br->TargetBlock
                && (br->TargetBlock == block || br->TargetBlock->IsDescendantOf(tf))) {
                out.storeInst = s; out.stlocBlock = prev;
                out.stlocIdx = static_cast<int>(prev->Instructions.size()) - 1;
                out.prevBlock = prev;
                return true;
            }
        }
    }
    return false;
}

// Try to fold a single TryFinally (at block->Instructions[tfIdx]) into a
// UsingInstruction. Returns true on a fold; sets *dropped when a block was
// dropped (the preceding-block case -- the caller restarts the container scan).
bool TryFoldUsing(ILInstruction* root, BlockContainer* container, std::size_t bi,
                  Block* block, int tfIdx, TryFinally* tf,
                  std::vector<std::unique_ptr<Block>>& graveyard, bool* dropped) {
    *dropped = false;
    auto* fin = dynamic_cast<BlockContainer*>(tf->FinallyBlock.get());
    if (!fin) return false;

    ResourceStore rs;
    if (!FindResourceStore(container, bi, block, tfIdx, tf, rs)) return false;
    StLoc* storeInst = rs.storeInst;
    if (!storeInst || !storeInst->Variable) return false;
    ILVariable* v = storeInst->Variable.get();

    // Validate the resource (mirrors the C# TransformUsing guards).
    if (v->Kind != VariableKind::Local) return false;
    if (v->StoreCount > 1) return false;  // single store (the stloc itself)
    bool usingNull = MatchLdNullInst(storeInst->Value.get());
    if (!usingNull && !CheckResourceType(v->Type.get())) return false;
    if (!CheckAllLoadsInside(v, root, tf)) return false;
    if (!CheckAllAddressesInside(v, root, tf)) return false;
    ILVariable* checkObj = nullptr;
    if (!MatchDisposeBlock(fin, v, usingNull, &checkObj)) return false;

    // Build the UsingInstruction. Detach the resource expression (stloc's
    // Value) and the try block (TryFinally's TryBlock) before the old nodes are
    // destroyed by the slot replacements below (no GC).
    v->Kind = VariableKind::UsingLocal;
    std::unique_ptr<ILInstruction> resource = storeInst->TakeChild(0);
    std::unique_ptr<ILInstruction> tryBlock = tf->TakeChild(0);
    auto usingInst = std::make_unique<UsingInstruction>(storeInst->Variable,
                                                        std::move(resource), std::move(tryBlock));

    if (rs.sameBlock) {
        // Replace the stloc (Instructions[stlocIdx] == tfIdx-1) with the
        // UsingInstruction and erase the TryFinally (Instructions[tfIdx]). The
        // stloc shell (Value detached) and the TryFinally shell (TryBlock
        // detached, FinallyBlock discarded) are destroyed here.
        usingInst->Parent = block;
        usingInst->ChildIndex = rs.stlocIdx;
        block->Instructions[static_cast<std::size_t>(rs.stlocIdx)] = std::move(usingInst);
        block->Instructions.erase(block->Instructions.begin() + tfIdx);
        block->RenumberChildren();
        return true;
    }

    // Preceding-block fold: put the UsingInstruction in the preceding block
    // (replacing the stloc), absorb the TryFinally's block final (the
    // after-using continuation) into it, and drop the now-empty TryFinally
    // block. The preceding block's `br` into the try (which would dangle into
    // the using body once the TryFinally becomes a UsingInstruction) is
    // discarded -- the UsingInstruction subsumes the try entry.
    Block* prev = rs.stlocBlock;  // == rs.prevBlock
    prev->Instructions.erase(prev->Instructions.begin() + rs.stlocIdx);
    prev->RenumberChildren();
    usingInst->Parent = prev;
    usingInst->ChildIndex = static_cast<int>(prev->Instructions.size());
    prev->Instructions.push_back(std::move(usingInst));
    block->FinalInstruction->Parent = prev;
    block->FinalInstruction->ChildIndex = static_cast<int>(prev->Instructions.size());
    prev->FinalInstruction = std::move(block->FinalInstruction);
    block->Instructions.clear();  // destroys the TryFinally shell + FinallyBlock
    block->RenumberChildren();
    auto it = container->Blocks.begin() + static_cast<std::ptrdiff_t>(bi);
    graveyard.push_back(std::move(*it));
    container->Blocks.erase(it);
    for (std::size_t k = 0; k < container->Blocks.size(); ++k) {
        container->Blocks[k]->ChildIndex = static_cast<int>(k);
        container->Blocks[k]->Parent = container;
    }
    *dropped = true;
    return true;
}

// Process every TryFinally in one container, folding each into a
// UsingInstruction. Returns true if any fold happened. A fold destroys the
// TryFinally's nested FinallyBlock container (and moves the TryBlock container
// into the UsingInstruction), so the caller must re-gather containers before
// processing any other container -- a pre-gathered container list would hold a
// dangling pointer to the destroyed FinallyBlock. This routine only touches
// `container` itself (never the nested containers), so it is safe to run to
// fixpoint within one container; the caller re-gathers after it returns true.
bool ProcessContainerFully(ILInstruction* root, BlockContainer* container,
                           std::vector<std::unique_ptr<Block>>& graveyard,
                           ILTransformContext& context) {
    bool anyFold = false;
    bool restart = true;
    while (restart) {
        restart = false;
        for (std::size_t bi = 0; bi < container->Blocks.size() && !restart; ++bi) {
            Block* block = container->Blocks[bi].get();
            int tfIdx = static_cast<int>(block->Instructions.size()) - 1;
            while (tfIdx >= 0) {
                auto* tf = dynamic_cast<TryFinally*>(block->Instructions[static_cast<std::size_t>(tfIdx)].get());
                if (!tf) { --tfIdx; continue; }
                bool dropped = false;
                if (TryFoldUsing(root, container, bi, block, tfIdx, tf, graveyard, &dropped)) {
                    anyFold = true;
                    if (dropped) { restart = true; break; }
                    // same-block fold: re-scan this block from the new end.
                    tfIdx = static_cast<int>(block->Instructions.size()) - 1;
                    context.StepOnce("UsingTransform");
                    continue;
                }
                --tfIdx;
            }
            if (restart) break;
        }
    }
    return anyFold;
}

} // namespace

void UsingTransform::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.UsingStatement) return;

    // The IsSingleDefinition / LoadCount / StoreCount guards read fresh usage
    // counts; the C# maintains these incrementally, this port recomputes.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);

    ILInstruction* root = function.Body.get();
    std::vector<std::unique_ptr<Block>> graveyard;
    // Re-gather containers after each container that produced a fold: a fold
    // destroys the TryFinally's nested FinallyBlock container, so a
    // pre-gathered list would dangle. Each pass processes one container (to
    // fixpoint) and re-gathers; nested containers (the moved TryBlock, any
    // nested usings) are picked up in later passes. Terminates because each
    // pass folds at least one using (or returns when none remain).
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<BlockContainer*> containers;
        CollectContainers(root, containers);
        for (BlockContainer* container : containers) {
            if (ProcessContainerFully(root, container, graveyard, context)) {
                changed = true;
                break;  // re-gather before touching any other container
            }
        }
    }

    // The folds dropped stores/loads of the resource; recompute so later
    // transforms and the seed read fresh counts.
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
