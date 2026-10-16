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

#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowGraph.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

using TypeSystem::IType;
using TypeSystem::ITypePtr;
using TypeSystem::IsReferenceType;
using TypeSystem::TypeKind;

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). nullptr if `block` is the last block or has no
// container. Mirrors the helper in SwitchAnalysis / SwitchOnNullableTransform.
Block* NextBlockInContainer(Block* block) {
    if (!block) return nullptr;
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
        }
    }
    return nullptr;
}

BlockContainer* ParentContainerOf(Block* b) {
    return b ? dynamic_cast<BlockContainer*>(b->Parent) : nullptr;
}

// ---- Small structural match helpers (PatternMatching.cs) ----

bool MatchLdLoc(ILInstruction* inst, ILVariable*& v) {
    auto* ld = dynamic_cast<LdLoc*>(inst);
    if (!ld) { v = nullptr; return false; }
    v = ld->Variable.get();
    return v != nullptr;
}

// Match a LdLoc of a specific variable (the C# MatchLdLoc(ILVariable)). Renamed
// from an overload to avoid the lvalue-pointer ambiguity with the extract form
// (an ILVariable* lvalue binds to both ILVariable*& and const ILVariable* at the
// same rank, so the extract form would be chosen and clobber the variable).
bool MatchLdLocVar(ILInstruction* inst, ILVariable* v) {
    auto* ld = dynamic_cast<LdLoc*>(inst);
    return ld && ld->Variable.get() == v;
}

bool MatchStLoc(ILInstruction* inst, ILVariable*& v, ILInstruction*& value) {
    auto* st = dynamic_cast<StLoc*>(inst);
    if (!st) { v = nullptr; value = nullptr; return false; }
    v = st->Variable.get();
    value = st->Value.get();
    return v != nullptr;
}

// Match a StLoc of a specific variable (the C# MatchStLoc(ILVariable, out value)).
// Renamed from an overload to avoid the lvalue-pointer ambiguity with the
// extract form above (an ILVariable* lvalue binds to both ILVariable*& and
// ILVariable* at the same rank).
bool MatchStLocVar(ILInstruction* inst, ILVariable* v, ILInstruction*& value) {
    auto* st = dynamic_cast<StLoc*>(inst);
    if (!st || st->Variable.get() != v) { value = nullptr; return false; }
    value = st->Value.get();
    return true;
}

bool MatchBranch(ILInstruction* inst, Block*& target) {
    auto* br = dynamic_cast<Branch*>(inst);
    if (!br) { target = nullptr; return false; }
    target = br->TargetBlock;
    return target != nullptr;
}

bool MatchLdNull(ILInstruction* inst) {
    return inst && inst->Op == OpCode::LdNull;
}

bool MatchIsInst(ILInstruction* inst, ILInstruction*& argument, ITypePtr& type) {
    auto* isinst = dynamic_cast<IsInst*>(inst);
    if (!isinst) { argument = nullptr; type = nullptr; return false; }
    argument = isinst->Argument.get();
    type = isinst->Type;
    return true;
}

bool MatchBox(ILInstruction* inst, ILInstruction*& argument, ITypePtr& type) {
    auto* box = dynamic_cast<Box*>(inst);
    if (!box) { argument = nullptr; type = nullptr; return false; }
    argument = box->Argument.get();
    type = box->Type;
    return true;
}

bool MatchUnboxAny(ILInstruction* inst, ILInstruction*& argument, ITypePtr& type) {
    auto* unbox = dynamic_cast<UnboxAny*>(inst);
    if (!unbox) { argument = nullptr; type = nullptr; return false; }
    argument = unbox->Argument.get();
    type = unbox->Type;
    return true;
}

// logic.not(X) == comp(Equality, X, LdcI4(0)) (the reader's brfalse shape;
// see SwitchAnalysis / ConditionDetection). Sign-independent.
bool MatchLogicNot(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (!comp || comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
    auto* rhs = dynamic_cast<LdcI4*>(comp->Right.get());
    if (!rhs || rhs->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

// comp(arg == ldnull) or comp(ldnull == arg). Returns the non-null side.
bool MatchCompEqualsNull(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (!comp || comp->Kind != ComparisonKind::Equality) return false;
    if (MatchLdNull(comp->Right.get())) { arg = comp->Left.get(); return true; }
    if (MatchLdNull(comp->Left.get())) { arg = comp->Right.get(); return true; }
    return false;
}

// comp(arg != ldnull) or comp(ldnull != arg).
bool MatchCompNotEqualsNull(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (!comp || comp->Kind != ComparisonKind::Inequality) return false;
    if (MatchLdNull(comp->Right.get())) { arg = comp->Left.get(); return true; }
    if (MatchLdNull(comp->Left.get())) { arg = comp->Right.get(); return true; }
    return false;
}

// MatchIfAtEndOfBlock adapted to this port's if-as-final block model (the
// IfInstruction is the block's FinalInstruction; the false arm is the next
// block in the container, or an explicit FalseInst Branch if one is set).
// Unwraps logic.not in the condition, swapping the two arms each time.
// Returns the if (for later mutation), the unwrapped condition, and the two
// target blocks (trueBlock = condition-true path, falseBlock = condition-false
// path), after the logic.not swaps.
bool MatchIfAtEndOfBlock(Block* block, IfInstruction*& iff, ILInstruction*& condition,
                         Block*& trueBlock, Block*& falseBlock) {
    iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
    if (!trueBr || !trueBr->TargetBlock) return false;
    trueBlock = trueBr->TargetBlock;
    if (iff->FalseInst) {
        auto* falseBr = dynamic_cast<Branch*>(iff->FalseInst.get());
        if (!falseBr || !falseBr->TargetBlock) return false;
        falseBlock = falseBr->TargetBlock;
    } else {
        falseBlock = NextBlockInContainer(block);
        if (!falseBlock) return false;  // a null path is required
    }
    condition = iff->Condition.get();
    ILInstruction* inner = nullptr;
    while (MatchLogicNot(condition, inner)) {
        condition = inner;
        std::swap(trueBlock, falseBlock);
    }
    return true;
}

// Collect every use (load / address / store) of `v` in the subtree, as
// non-owning pointers. Mirrors the C# v.LoadInstructions.Concat(v.AddressInstructions)
// .Concat(v.StoreInstructions) -- this port keeps no per-variable use lists, so
// the uses are gathered by a tree walk instead.
void CollectUses(ILInstruction* inst, ILVariable* v, std::vector<ILInstruction*>& uses) {
    if (!inst) return;
    switch (inst->Op) {
        case OpCode::LdLoc:
            if (static_cast<LdLoc*>(inst)->Variable.get() == v) uses.push_back(inst);
            break;
        case OpCode::LdLoca:
            if (static_cast<LdLoca*>(inst)->Variable.get() == v) uses.push_back(inst);
            break;
        case OpCode::StLoc:
            if (static_cast<StLoc*>(inst)->Variable.get() == v) uses.push_back(inst);
            break;
        case OpCode::MatchInstruction:
            if (static_cast<MatchInstruction*>(inst)->Variable.get() == v) uses.push_back(inst);
            break;
        default:
            break;
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CollectUses(inst->GetChild(i), v, uses);
}

// CheckAllUsesDominatedBy: every use of `v` (except `storeToV` and
// `loadInNullCheck`) must be in a block of `container` that is dominated by
// `targetBlock`. A use outside `container` (no enclosing block in it) rejects.
// Builds a fresh CFG for `container` so the dominance reflects the current
// (possibly already-rewritten) container state.
bool CheckAllUsesDominatedBy(ILFunction& function, ILVariable* v, BlockContainer* container,
                             Block* targetBlock, ILInstruction* storeToV,
                             ILInstruction* loadInNullCheck) {
    if (!v || !container || !targetBlock) return false;
    ControlFlowGraph cfg(container);
    auto* targetNode = cfg.GetNode(targetBlock);
    if (!targetNode) return false;
    std::vector<ILInstruction*> uses;
    CollectUses(function.Body.get(), v, uses);
    for (ILInstruction* use : uses) {
        if (use == storeToV || use == loadInNullCheck) continue;
        Block* found = nullptr;
        for (ILInstruction* cur = use; cur != nullptr; cur = cur->Parent) {
            if (cur->Parent == container) { found = static_cast<Block*>(cur); break; }
        }
        if (!found) return false;
        auto* node = cfg.GetNode(found);
        if (!node || !targetNode->Dominates(node)) return false;
    }
    return true;
}

// Reconstruct the if after the pattern match: the matched path (condition
// true) becomes the if's true arm; the null path becomes the false arm,
// kept positional when it is the block's fall-through, materialised as an
// explicit Branch otherwise. `matchedOwned` is the freshly-built MatchInstruction
// (already owning the detached testedOperand); the old condition (the comp with
// the isinst) is destroyed when iff->Condition is replaced.
void RebuildIf(IfInstruction* iff, Block* block, Block* matchedBlock, Block* nullBlock,
               std::unique_ptr<ILInstruction> matchedOwned) {
    iff->Condition = std::move(matchedOwned);
    if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
    iff->TrueInst = std::make_unique<Branch>(matchedBlock);
    iff->TrueInst->Parent = iff;
    iff->TrueInst->ChildIndex = 1;
    Block* nextBlock = NextBlockInContainer(block);
    if (nullBlock && nullBlock != nextBlock) {
        iff->FalseInst = std::make_unique<Branch>(nullBlock);
        iff->FalseInst->Parent = iff;
        iff->FalseInst->ChildIndex = 2;
    } else {
        // The null path is the positional fall-through; drop any prior else arm.
        iff->FalseInst = nullptr;
    }
}

// ---- PatternMatchValueTypes ----

//	if (comp.o(isinst T(testedOperand) == ldnull)) br falseBlock
//	br unboxBlock
// - or - the testedOperand may be `box ``0(ldloc testedVariable)` (a boxed
// generic), in which case the real tested value is the ldloc inside the box.
// Returns type, testedOperand (non-owning, inside the if's condition), the
// testedVariable (the ldloc), boxType (the generic box type or null), unboxBlock
// (the matched-path block = trueBlock after the null-direction swap), and
// nullBlock (the null-path block).
bool MatchIsInstBlock(Block* block, IfInstruction*& iff, ITypePtr& type,
                      ILInstruction*& testedOperand, ILVariable*& testedVariable,
                      ITypePtr& boxType, Block*& unboxBlock, Block*& nullBlock) {
    ILInstruction* condition = nullptr;
    Block* trueBlock = nullptr;
    Block* falseBlock = nullptr;
    if (!MatchIfAtEndOfBlock(block, iff, condition, trueBlock, falseBlock)) return false;
    ILInstruction* arg = nullptr;
    if (MatchCompEqualsNull(condition, arg)) {
        std::swap(trueBlock, falseBlock);
    } else if (MatchCompNotEqualsNull(condition, arg)) {
        // matched path is the if-true
    } else {
        return false;
    }
    if (!MatchIsInst(arg, testedOperand, type)) return false;
    ILInstruction* boxArg = testedOperand;
    boxType = nullptr;
    ITypePtr boxT;
    if (MatchBox(testedOperand, boxArg, boxT) && boxT && boxT->Kind() == TypeKind::TypeParameter) {
        boxType = boxT;
        // boxArg is now the box's argument (the real tested value)
    } else {
        boxArg = testedOperand;
    }
    if (!MatchLdLoc(boxArg, testedVariable)) return false;
    unboxBlock = trueBlock;   // the matched path
    nullBlock = falseBlock;   // the null path
    return unboxBlock && unboxBlock->Parent == block->Parent;
}

// Block unboxBlock (1 pred) { stloc V(unbox.any T(arg)) ... } where arg is
// ldloc testedVariable, or isinst T(ldloc testedVariable), or
// box Tparam(ldloc testedVariable). Returns the unbox operand variable, the
// box type (if any), and the store (stloc V).
bool MatchUnboxBlock(Block* unboxBlock, const IType* type, ILVariable*& unboxOperand,
                     ITypePtr& boxType, StLoc*& storeToV) {
    unboxOperand = nullptr;
    boxType = nullptr;
    storeToV = nullptr;
    if (unboxBlock->IncomingEdgeCount != 1) return false;
    if (unboxBlock->Instructions.empty()) return false;
    storeToV = dynamic_cast<StLoc*>(unboxBlock->Instructions[0].get());
    if (!storeToV || !storeToV->Value) return false;
    ILInstruction* arg = nullptr;
    ITypePtr t;
    if (!MatchUnboxAny(storeToV->Value.get(), arg, t)) return false;
    if (!t || !t->Equals(*type)) return false;
    // arg may be isinst T(...) -- unwrap to its argument.
    ILInstruction* isinstArg = nullptr;
    ITypePtr isinstType;
    if (MatchIsInst(arg, isinstArg, isinstType) && isinstType && isinstType->Equals(*type)) {
        arg = isinstArg;
    }
    // arg may be box Tparam(...) -- unwrap to its argument, record the box type.
    ILInstruction* boxArg = nullptr;
    ITypePtr boxT;
    if (MatchBox(arg, boxArg, boxT) && boxT && boxT->Kind() == TypeKind::TypeParameter) {
        boxType = boxT;
        arg = boxArg;
    }
    if (!MatchLdLoc(arg, unboxOperand)) return false;
    if (boxType && unboxOperand->Type && !boxType->Equals(*unboxOperand->Type)) return false;
    return true;
}

bool PatternMatchValueTypes(Block* block, BlockContainer* container, ILFunction& function,
                            ILTransformContext& context) {
    IfInstruction* iff = nullptr;
    ITypePtr type;
    ILInstruction* testedOperand = nullptr;
    ILVariable* testedVariable = nullptr;
    ITypePtr boxType1;
    Block* unboxBlock = nullptr;
    Block* nullBlock = nullptr;
    if (!MatchIsInstBlock(block, iff, type, testedOperand, testedVariable, boxType1, unboxBlock, nullBlock))
        return false;

    // tempStore: the optional `stloc temp(ldloc testedVariable)` right before
    // the if (the last non-terminal instruction). Mirrors the C#
    // `block.Instructions.ElementAtOrDefault(Count - 3) as StLoc`.
    StLoc* tempStore = nullptr;
    int tempStoreIndex = -1;
    if (!block->Instructions.empty()) {
        auto* st = dynamic_cast<StLoc*>(block->Instructions.back().get());
        if (st && st->Value && MatchLdLocVar(st->Value.get(), testedVariable)) {
            tempStore = st;
            tempStoreIndex = static_cast<int>(block->Instructions.size()) - 1;
        }
    }

    ILVariable* unboxOperand = nullptr;
    ITypePtr boxType2;
    StLoc* storeToV = nullptr;
    if (!MatchUnboxBlock(unboxBlock, type.get(), unboxOperand, boxType2, storeToV)) return false;
    if (boxType1 && boxType2) { if (!boxType1->Equals(*boxType2)) return false; }
    else if (boxType1 || boxType2) return false;  // one boxed, the other not

    if (unboxOperand == testedVariable) {
        // do nothing
    } else if (tempStore && unboxOperand == tempStore->Variable.get()) {
        if (!(tempStore->Variable->IsSingleDefinition() && tempStore->Variable->LoadCount == 1))
            return false;
    } else {
        return false;
    }
    if (!CheckAllUsesDominatedBy(function, storeToV->Variable.get(), container, unboxBlock,
                                 storeToV, nullptr))
        return false;

    context.StepOnce("Pattern matching (value type)");

    // Capture the capture variable's shared_ptr before any rewrite: the stloc
    // V(unbox.any) that owns it is in unboxBlock and is destroyed when we drop
    // it below (no GC to keep it alive), so the raw storeToV pointer would
    // dangle. The ILVariable itself lives on the function's Variables list.
    ILVariablePtr storeVar = storeToV->Variable;

    // Detach testedOperand from the isinst (inside iff->Condition) before the
    // old condition is destroyed by the replacement.
    ILInstruction* isinst = testedOperand ? testedOperand->Parent : nullptr;
    std::unique_ptr<ILInstruction> testedOwned;
    if (auto* un = dynamic_cast<UnboxAny*>(isinst)) {
        testedOwned = un->TakeChild(0);
    } else if (auto* isI = dynamic_cast<IsInst*>(isinst)) {
        testedOwned = isI->TakeChild(0);
    } else if (auto* bx = dynamic_cast<Box*>(isinst)) {
        testedOwned = bx->TakeChild(0);
    } else if (testedOperand) {
        // testedOperand is the box/isinst argument itself; detach it from its
        // parent slot. (Rare; the isinst's Argument is the direct tested value.)
        if (isinst) testedOwned = isinst->TakeChild(testedOperand->ChildIndex);
    }
    auto match = std::make_unique<MatchInstruction>(storeVar, std::move(testedOwned));
    match->CheckNotNull = true;
    match->CheckType = true;
    RebuildIf(iff, block, /*matchedBlock=*/unboxBlock, /*nullBlock=*/nullBlock, std::move(match));

    // Drop the stloc V(unbox.any ...) that is now folded into the pattern.
    // (storeToV is destroyed here; use the captured storeVar below.)
    unboxBlock->RemoveInstructionAt(0);
    // Drop the temp copy if it was used.
    if (tempStore && unboxOperand == tempStore->Variable.get() && tempStoreIndex >= 0) {
        block->RemoveInstructionAt(static_cast<std::size_t>(tempStoreIndex));
    }
    block->RenumberChildren();
    storeVar->Kind = VariableKind::PatternLocal;
    return true;
}

// ---- PatternMatchRefTypes ----

bool PatternMatchRefTypes(Block* block, BlockContainer* container, ILFunction& function,
                          ILTransformContext& context) {
    IfInstruction* iff = nullptr;
    ILInstruction* condition = nullptr;
    Block* trueBlock = nullptr;
    Block* falseBlock = nullptr;
    if (!MatchIfAtEndOfBlock(block, iff, condition, trueBlock, falseBlock)) return false;

    // pos = index in block->Instructions of the stloc V(isinst ...) (the last
    // non-terminal before the if-final). If the condition is a bare ldloc of a
    // single-def stack-slot temp, that temp's store (stloc condVar(comp ...))
    // is the last non-terminal and the real condition lives in it; step back.
    int pos = static_cast<int>(block->Instructions.size()) - 1;
    ILVariable* condVar = nullptr;
    if (MatchLdLoc(condition, condVar)) {
        if (pos < 0) return false;
        if (!(condVar->IsSingleDefinition() && condVar->LoadCount == 1 &&
              condVar->Kind == VariableKind::StackSlot))
            return false;
        ILInstruction* condValue = nullptr;
        if (!MatchStLocVar(block->Instructions[static_cast<std::size_t>(pos)].get(), condVar, condValue))
            return false;
        condition = condValue;
        pos--;
    }

    ILInstruction* loadInNullCheck = nullptr;
    if (MatchCompEqualsNull(condition, loadInNullCheck)) {
        std::swap(trueBlock, falseBlock);
    } else if (MatchCompNotEqualsNull(condition, loadInNullCheck)) {
        // matched path is the if-true
    } else {
        return false;
    }
    ILVariable* s = nullptr;
    if (!MatchLdLoc(loadInNullCheck, s)) return false;
    if (!s->IsSingleDefinition()) return false;
    if (s->Kind != VariableKind::Local && s->Kind != VariableKind::StackSlot) return false;
    if (pos < 0) return false;

    StLoc* storeToV = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(pos)].get());
    ILVariable* v = nullptr;
    ILInstruction* value = nullptr;
    if (!MatchStLoc(storeToV, v, value)) return false;
    if (MatchLdLocVar(value, s)) {
        // The double-store form: stloc v(ldloc s); stloc s(isinst ...). Step back
        // to the stloc s and read its value.
        pos--;
        if (pos < 0) return false;
        ILInstruction* isinstValue = nullptr;
        if (!MatchStLocVar(block->Instructions[static_cast<std::size_t>(pos)].get(), s, isinstValue))
            return false;
        value = isinstValue;
        if (v->Kind != VariableKind::Local && v->Kind != VariableKind::StackSlot) return false;
        if (s->LoadCount != 2) return false;
    } else {
        if (v != s) return false;
    }

    ITypePtr unboxType;
    if (auto* unboxAny = dynamic_cast<UnboxAny*>(value)) {
        unboxType = unboxAny->Type;
        value = unboxAny->Argument.get();
    }
    ILInstruction* testedOperand = nullptr;
    ITypePtr type;
    if (!MatchIsInst(value, testedOperand, type)) return false;
    if (IsReferenceType(type.get()) != true) return false;
    if (unboxType && !type->Equals(*unboxType)) return false;
    if (!v->Type || !v->Type->Equals(*type)) return false;
    if (!CheckAllUsesDominatedBy(function, v, container, trueBlock, storeToV, loadInNullCheck))
        return false;

    context.StepOnce("Type pattern matching (ref type)");

    // Detach testedOperand from the isinst before the old condition is destroyed.
    std::unique_ptr<ILInstruction> testedOwned;
    if (auto* isI = dynamic_cast<IsInst*>(testedOperand->Parent)) {
        testedOwned = isI->TakeChild(0);
    } else if (auto* un = dynamic_cast<UnboxAny*>(testedOperand->Parent)) {
        testedOwned = un->TakeChild(0);
    } else if (testedOperand->Parent) {
        testedOwned = testedOperand->Parent->TakeChild(testedOperand->ChildIndex);
    }
    auto match = std::make_unique<MatchInstruction>(storeToV->Variable, std::move(testedOwned));
    match->CheckNotNull = true;
    match->CheckType = true;
    RebuildIf(iff, block, /*matchedBlock=*/trueBlock, /*nullBlock=*/falseBlock, std::move(match));

    // Remove the stloc V(isinst ...) (and the temp store / double-store stloc s)
    // that the pattern absorbed: everything from `pos` to the end of the
    // block's non-terminal instructions.
    if (pos < static_cast<int>(block->Instructions.size())) {
        block->Instructions.erase(block->Instructions.begin() + pos, block->Instructions.end());
    }
    block->RenumberChildren();
    v->Kind = VariableKind::PatternLocal;
    return true;
}

void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

} // namespace

void PatternMatchingTransform::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.PatternMatching) return;
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        // Iterate the container's blocks in reverse (the C# Blocks.Reverse()).
        // The rewrite never adds or removes blocks (the dead blocks stay, per
        // D58), so non-owning pointers collected up front stay valid.
        std::vector<Block*> blocks;
        for (auto& b : container->Blocks) blocks.push_back(b.get());
        for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
            Block* block = *it;
            if (PatternMatchValueTypes(block, container, function, context)) continue;
            if (PatternMatchRefTypes(block, container, function, context)) continue;
        }
        // The C# removes blocks with empty Instructions; skipped here (a block
        // with empty Instructions still carries its FinalInstruction, and
        // removing it would dangle branches targeting it -- the D58 hazard).
    }
    // Usage counts moved with the rewrites; recompute so later transforms and
    // the seed read fresh Load/Store counts (the pattern variable is now a
    // PatternLocal stored once by the MatchInstruction, etc.).
    ComputeVariableUsage(function);
    RecomputeIncomingEdgeCounts(function);
}

} // namespace ILSpy::Decompiler::IL
