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

#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"

#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
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
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <functional>
#include <unordered_set>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Walk every Block in the tree (the C# function.Descendants.OfType<Block>()).
// Collects non-owning pointers up front; the transform never erases blocks, so
// the pointers stay valid across the pass.
void CollectBlocks(ILInstruction* inst, std::vector<Block*>& out) {
    if (!inst) return;
    if (inst->Op == OpCode::Block) out.push_back(static_cast<Block*>(inst));
    for (int i = 0; i < inst->ChildCount(); ++i) CollectBlocks(inst->GetChild(i), out);
}

// The next block in `block`'s container (the implicit fall-through target in
// this port's block model). nullptr if `block` is the last block or has no
// container.
Block* NextBlockInContainer(Block* block) {
    if (!block) return nullptr;
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            if (i + 1 < container->Blocks.size())
                return container->Blocks[i + 1].get();
            return nullptr;
        }
    }
    return nullptr;
}

// The fall-through target of `block`'s IfInstruction final: an explicit FalseInst
// Branch's target when present, else the positional next block in the container.
// nullptr when there is no fall-through (the C# requires an explicit br
// switchBlock as the block's last instruction).
Block* IfFallThroughTarget(IfInstruction* iff) {
    if (!iff) return nullptr;
    if (iff->FalseInst) {
        auto* br = dynamic_cast<Branch*>(iff->FalseInst.get());
        return (br && br->TargetBlock) ? br->TargetBlock : nullptr;
    }
    // No else arm: the fall-through is positional. Reconstruct the if's block
    // from its parent to find the next block in the container.
    auto* block = dynamic_cast<Block*>(iff->Parent);
    return NextBlockInContainer(block);
}

// The SwitchInstruction that is `block`'s final, or nullptr.
SwitchInstruction* BlockSwitch(Block* block) {
    if (!block || !block->FinalInstruction) return nullptr;
    if (block->FinalInstruction->Op != OpCode::SwitchInstruction) return nullptr;
    return static_cast<SwitchInstruction*>(block->FinalInstruction.get());
}

// Match logic.not(X): comp(Equality, X, LdcI4(0)) -> X. The C# also requires
// comp.LiftingKind == None; this port does not model nullable lifting, so no
// Comp is ever lifted (see D61). get_HasValue returns bool (I4), so the reader
// emits Comp(Equality, get_HasValue, LdcI4(0)) for brfalse, which is the shape
// this recognises.
bool MatchLogicNot(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Comp) return false;
    auto* comp = static_cast<Comp*>(inst);
    if (comp->Kind != ComparisonKind::Equality) return false;
    if (!comp->Right || comp->Right->Op != OpCode::LdcI4) return false;
    if (static_cast<LdcI4*>(comp->Right.get())->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

// Clone a value instruction (the kinds that appear as get_HasValue /
// GetValueOrDefault arguments and as a switch value): a variable load
// (ldloca/ldloc), a constant (ldc.i4/ldnull), or a call. Returns nullptr for
// an unhandled kind so the caller can bail (leave the block as-is). Used for
// the rare Roslyn case where the HasValue target is not an address (ldloca)
// and the switch value becomes `ldobj(target, Nullable<T>)` -- the target is
// cloned so the switch owns its value independently of the if's condition,
// which the caller discards by replacing the block final.
std::unique_ptr<ILInstruction> CloneValue(ILInstruction* v) {
    if (!v) return nullptr;
    switch (v->Op) {
        case OpCode::LdLoca:
            return std::make_unique<LdLoca>(static_cast<LdLoca*>(v)->Variable);
        case OpCode::LdLoc:
            return std::make_unique<LdLoc>(static_cast<LdLoc*>(v)->Variable);
        case OpCode::LdcI4:
            return std::make_unique<LdcI4>(static_cast<LdcI4*>(v)->Value);
        case OpCode::LdNull:
            return std::make_unique<LdNull>();
        case OpCode::Call: {
            auto* c = static_cast<Call*>(v);
            auto clone = std::make_unique<Call>(c->MethodName);
            clone->DeclaringType = c->DeclaringType;
            for (auto& a : c->Arguments) {
                auto argClone = CloneValue(a.get());
                if (!argClone) return nullptr;
                clone->AddArg(std::move(argClone));
            }
            return clone;
        }
        default:
            return nullptr;
    }
}

// Structural equality between two instructions (the C# target2.Match(target).
// Success). Covers the node kinds that appear as get_HasValue/GetValueOrDefault
// arguments -- typically a variable load (ldloca/ldloc) -- plus the constants
// and Call so the inlined-Roslyn and edge cases compare correctly. Two nodes
// of different kinds, or a kind this does not handle, compare unequal
// (conservative: the transform leaves the block as-is rather than mis-folding).
bool StructurallyEquals(ILInstruction* a, ILInstruction* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->Op != b->Op) return false;
    switch (a->Op) {
        case OpCode::LdLoca: {
            auto* la = static_cast<LdLoca*>(a);
            auto* lb = static_cast<LdLoca*>(b);
            return la->Variable.get() == lb->Variable.get();
        }
        case OpCode::LdLoc: {
            auto* la = static_cast<LdLoc*>(a);
            auto* lb = static_cast<LdLoc*>(b);
            return la->Variable.get() == lb->Variable.get();
        }
        case OpCode::LdcI4:
            return static_cast<LdcI4*>(a)->Value == static_cast<LdcI4*>(b)->Value;
        case OpCode::LdNull:
            return true;
        case OpCode::Call: {
            auto* ca = static_cast<Call*>(a);
            auto* cb = static_cast<Call*>(b);
            if (ca->MethodName != cb->MethodName) return false;
            if (ca->Arguments.size() != cb->Arguments.size()) return false;
            for (std::size_t i = 0; i < ca->Arguments.size(); ++i) {
                if (!StructurallyEquals(ca->Arguments[i].get(), cb->Arguments[i].get()))
                    return false;
            }
            return true;
        }
        default:
            return false;  // unhandled kind: compare unequal
    }
}

// Build the lifted SwitchInstruction from switchBlock's switch sections, the
// null-case block, the switch value, and the nullable type. Moves the source
// switch's sections into the new switch (the source switchBlock becomes
// unreachable; per D58 it stays in the tree with an empty switch -- harmless).
// Mirrors the C# BuildLiftedSwitch.
std::unique_ptr<SwitchInstruction> BuildLiftedSwitch(
    Block* nullCaseBlock, SwitchInstruction* switchInst,
    std::unique_ptr<ILInstruction> switchValue,
    TypeSystem::ITypePtr nullableType) {
    auto newSwitch = std::make_unique<SwitchInstruction>(std::move(switchValue));
    newSwitch->IsLifted = true;
    newSwitch->Type = std::move(nullableType);
    // Move the source switch's sections into the new switch (the source switch
    // is left with an empty section vector; its block is unreachable).
    for (auto& section : switchInst->Sections) {
        newSwitch->AddSection(std::move(section));
    }
    switchInst->Sections.clear();
    // The `case null:` arm: no integer labels, HasNullLabel set, body a Branch
    // to the null-case block.
    auto nullSection = std::make_unique<SwitchSection>();
    nullSection->HasNullLabel = true;
    nullSection->SetBody(std::make_unique<Branch>(nullCaseBlock));
    newSwitch->AddSection(std::move(nullSection));
    return newSwitch;
}

} // namespace

void SwitchOnNullableTransform::Run(ILFunction& function, ILTransformContext& context) {
    if (!context.Settings.LiftNullables) return;

    // The matchers consult IsSingleDefinition / LoadCount, so refresh the
    // per-variable usage counts first (the preceding transforms may have
    // mutated the tree).
    ComputeVariableUsage(function);

    // Collect every block up front; the transform never erases blocks, so the
    // pointers stay valid across the pass.
    std::vector<Block*> blocks;
    CollectBlocks(function.Body.get(), blocks);

    std::unordered_set<BlockContainer*> changedContainers;

    for (Block* block : blocks) {
        if (!block) continue;
        std::unique_ptr<SwitchInstruction> newSwitch;
        bool matched = false;
        if (MatchSwitchOnNullable(block, context, newSwitch)) {
            matched = true;
        } else if (MatchRoslynSwitchOnNullable(block, context, newSwitch)) {
            matched = true;
        }
        if (!matched || !newSwitch) continue;

        context.StepOnce("SwitchOnNullableTransform");
        // Replace the block's final (the if) with the lifted switch. The
        // C# replaces the fall-through `br switchBlock` instruction (the block's
        // last); in this port that fall-through is positional, so replacing the
        // FinalInstruction is the whole operation.
        block->SetFinal(std::move(newSwitch));

        // De-dup / adjust-labels / sort the new switch (the C# calls
        // SwitchDetection.SimplifySwitchInstruction on the modified block).
        SwitchDetection::SimplifySwitchInstruction(block, context);
        if (auto* container = dynamic_cast<BlockContainer*>(block->Parent))
            changedContainers.insert(container);
    }

    // The C# SortBlocks(deleteUnreachableBlocks: true) cleanup is unsafe in
    // this port (D58); the dead switchBlock stays in the tree (unreachable,
    // harmless). Refresh the edge counts so downstream transforms see the new
    // switch's section-body branches and the switchBlock's lost predecessor.
    if (!changedContainers.empty()) {
        RecomputeIncomingEdgeCounts(function);
        function.CheckInvariant(ILPhase::Normal);
    }
}

bool SwitchOnNullableTransform::MatchSwitchOnNullable(
    Block* block, ILTransformContext& /*context*/,
    std::unique_ptr<SwitchInstruction>& newSwitch) {
    newSwitch.reset();
    // Legacy shape (this port's block model):
    //   Instructions[size-2] = stloc tmp(ldloca switchValueVar)
    //   Instructions[size-1] = stloc switchVar(call GetValueOrDefault(ldloc tmp))
    //   FinalInstruction     = if (logic.not(call get_HasValue(ldloc tmp))) br nullCase
    //   fall-through (next block) = switchBlock
    if (!block) return false;
    if (block->Instructions.size() < 2) return false;
    auto* stTmp = dynamic_cast<StLoc*>(block->Instructions[block->Instructions.size() - 2].get());
    auto* stSwitchVar = dynamic_cast<StLoc*>(block->Instructions[block->Instructions.size() - 1].get());
    if (!stTmp || !stTmp->Variable || !stSwitchVar || !stSwitchVar->Variable) return false;
    ILVariable* tmp = stTmp->Variable.get();
    ILVariable* switchVariable = stSwitchVar->Variable.get();
    // tmp is a single-definition temp loaded exactly twice (the GetValueOrDefault
    // and get_HasValue calls); switchVariable is single-def loaded once (the
    // switch). Mirrors the C# IsSingleDefinition + LoadCount checks.
    if (!tmp->IsSingleDefinition() || tmp->LoadCount != 2) return false;
    if (!switchVariable->IsSingleDefinition() || switchVariable->LoadCount != 1) return false;

    // stloc tmp(ldloca switchValueVar)
    auto* ldloca = dynamic_cast<LdLoca*>(stTmp->Value.get());
    if (!ldloca || !ldloca->Variable) return false;
    ILVariablePtr switchValueVar = ldloca->Variable;

    // stloc switchVar(call GetValueOrDefault(ldloc tmp))
    ILInstruction* getValueOrDefaultArg = nullptr;
    if (!NullableLiftingTransform::MatchGetValueOrDefault(stSwitchVar->Value.get(),
                                                          getValueOrDefaultArg))
        return false;

    // if (logic.not(call get_HasValue(ldloc tmp))) br nullCase
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    ILInstruction* getHasValue = nullptr;
    if (!MatchLogicNot(iff->Condition.get(), getHasValue)) return false;
    ILInstruction* getHasValueArg = nullptr;
    if (!NullableLiftingTransform::MatchHasValueCall(getHasValue, getHasValueArg)) return false;

    // Both calls receive `ldloc tmp`.
    auto* hasArg = dynamic_cast<LdLoc*>(getHasValueArg);
    auto* gvoArg = dynamic_cast<LdLoc*>(getValueOrDefaultArg);
    if (!hasArg || hasArg->Variable.get() != tmp) return false;
    if (!gvoArg || gvoArg->Variable.get() != tmp) return false;

    // if.TrueInst = br nullCase; fall-through = switchBlock.
    auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
    if (!trueBr || !trueBr->TargetBlock) return false;
    Block* nullCaseBlock = trueBr->TargetBlock;
    Block* switchBlock = IfFallThroughTarget(iff);
    if (!switchBlock) return false;

    // switchBlock: empty Instructions (just the switch final), single
    // predecessor, and a SwitchInstruction final. In the C# the switch is at
    // Instructions[0] with Count==1; here it is the FinalInstruction with
    // Instructions empty.
    if (!switchBlock->Instructions.empty()) return false;
    if (switchBlock->IncomingEdgeCount != 1) return false;
    auto* switchInst = BlockSwitch(switchBlock);
    if (!switchInst) return false;

    auto* hasValueCall = static_cast<Call*>(getHasValue);
    auto nullableType = hasValueCall->DeclaringType;
    newSwitch = BuildLiftedSwitch(nullCaseBlock, switchInst,
                                  std::make_unique<LdLoc>(switchValueVar),
                                  std::move(nullableType));

    // Drop the two stloc instructions (tmp and switchVariable). The if is
    // already replaced by the caller (SetFinal with newSwitch). Renumber so
    // any remaining instructions keep consistent ChildIndex.
    block->Instructions.pop_back();  // stloc switchVariable
    block->Instructions.pop_back();  // stloc tmp
    block->RenumberChildren();
    return true;
}

bool SwitchOnNullableTransform::MatchRoslynSwitchOnNullable(
    Block* block, ILTransformContext& /*context*/,
    std::unique_ptr<SwitchInstruction>& newSwitch) {
    newSwitch.reset();
    // Roslyn shape (this port's block model):
    //   FinalInstruction = if (logic.not(call get_HasValue(target))) br nullCase
    //   fall-through (next block) = switchBlock
    // where switchBlock holds either
    //   (a) stloc sw(call GetValueOrDefault(target)); switch (ldloc sw) { ... }
    //       -- Instructions=[stloc sw], FinalInstruction=switch(ldloc sw)
    //   (b) switch (call GetValueOrDefault(target)) { ... }   (inlined)
    //       -- Instructions empty, FinalInstruction=switch(call GetValueOrDefault)
    if (!block) return false;
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff) return false;
    ILInstruction* getHasValue = nullptr;
    if (!MatchLogicNot(iff->Condition.get(), getHasValue)) return false;
    ILInstruction* target = nullptr;
    if (!NullableLiftingTransform::MatchHasValueCall(getHasValue, target)) return false;
    // The C# requires the HasValue target to be pure (no side effects / throws /
    // branches) so folding it into the switch value is safe.
    if (!IsPure(target->Flags())) return false;

    auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
    if (!trueBr || !trueBr->TargetBlock) return false;
    Block* nullCaseBlock = trueBr->TargetBlock;
    Block* switchBlock = IfFallThroughTarget(iff);
    if (!switchBlock) return false;
    if (switchBlock->IncomingEdgeCount != 1) return false;

    SwitchInstruction* switchInst = nullptr;
    if (switchBlock->Instructions.size() == 1) {
        // Case (a): stloc sw(call GetValueOrDefault(target)); switch (ldloc sw).
        auto* stSwitchVar = dynamic_cast<StLoc*>(switchBlock->Instructions[0].get());
        if (!stSwitchVar || !stSwitchVar->Variable) return false;
        ILVariable* switchVar = stSwitchVar->Variable.get();
        if (!switchVar->IsSingleDefinition() || switchVar->LoadCount != 1) return false;
        ILInstruction* target2 = nullptr;
        if (!NullableLiftingTransform::MatchGetValueOrDefault(stSwitchVar->Value.get(),
                                                              target2))
            return false;
        if (!StructurallyEquals(target2, target)) return false;
        switchInst = BlockSwitch(switchBlock);
        if (!switchInst) return false;
    } else if (switchBlock->Instructions.empty()) {
        // Case (b): switch (call GetValueOrDefault(target)) -- inlined.
        switchInst = BlockSwitch(switchBlock);
        if (!switchInst) return false;
        ILInstruction* target2 = nullptr;
        if (!NullableLiftingTransform::MatchGetValueOrDefault(switchInst->Value.get(),
                                                              target2))
            return false;
        if (!StructurallyEquals(target2, target)) return false;
    } else {
        return false;
    }

    // The switch value: ldloc v when target is ldloca v (the common case --
    // HasValue/GetValueOrDefault are called on &nullable); otherwise a typed
    // load of the nullable value (ldobj target, Nullable<T>).
    std::unique_ptr<ILInstruction> switchValue;
    if (auto* ldloca = dynamic_cast<LdLoca*>(target)) {
        if (ldloca->Variable)
            switchValue = std::make_unique<LdLoc>(ldloca->Variable);
        else
            return false;
    } else {
        auto* hasValueCall = static_cast<Call*>(getHasValue);
        if (!hasValueCall->DeclaringType) return false;
        // Clone `target` so the switch owns its value independently (the
        // original target stays in the if's condition, which the caller is
        // about to discard, but cloning avoids any aliasing surprise).
        auto targetClone = CloneValue(target);
        if (!targetClone) return false;  // uncloneable target: leave the block
        switchValue = std::make_unique<LdObj>(std::move(targetClone),
                                              hasValueCall->DeclaringType);
    }

    auto* hasValueCall = static_cast<Call*>(getHasValue);
    newSwitch = BuildLiftedSwitch(nullCaseBlock, switchInst, std::move(switchValue),
                                  hasValueCall->DeclaringType);
    // The if (the block's final) is replaced by the caller's SetFinal. The
    // Roslyn block has no non-terminal instructions to drop (the stloc sw in
    // case (a) lives in switchBlock, which is now unreachable).
    return true;
}

} // namespace ILSpy::Decompiler::IL
