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

#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Collect every TryCatchHandler in the tree (the catch clauses of every
// TryCatch, however deeply nested in filter/try bodies).
void CollectHandlers(ILInstruction* inst, std::vector<TryCatchHandler*>& out) {
    if (!inst) return;
    if (inst->Op == OpCode::TryCatchHandler) {
        out.push_back(static_cast<TryCatchHandler*>(inst));
    }
    for (int i = 0; i < inst->ChildCount(); ++i) CollectHandlers(inst->GetChild(i), out);
}

// A `comp(a != b)` where one side is ldnull. Returns the non-null side in
// `other` and true if comp.Kind == Inequality and exactly one side is LdNull.
bool MatchCompNotEqualsLdNull(Comp* comp, ILInstruction*& other) {
    other = nullptr;
    if (!comp || comp->Kind != ComparisonKind::Inequality) return false;
    bool leftNull = comp->Left && comp->Left->Op == OpCode::LdNull;
    bool rightNull = comp->Right && comp->Right->Op == OpCode::LdNull;
    if (leftNull == rightNull) return false;  // need exactly one ldnull
    other = leftNull ? comp->Right.get() : comp->Left.get();
    return true;
}

// The false block of the catch-when pattern:
//   Block falseBlock (1 pred) { stloc ret(ldc.i4 0); br exitBlock }
// (our model: Instructions=[stloc ret(ldc.i4 0)], Final=Branch(exitBlock)).
// Returns the exit block target on success.
bool MatchFalseBlock(Block* falseBlock, ILVariable*& returnVar, Block*& exitBlock) {
    returnVar = nullptr;
    exitBlock = nullptr;
    if (!falseBlock || falseBlock->IncomingEdgeCount != 1) return false;
    if (falseBlock->Instructions.size() != 1) return false;
    auto* stloc = dynamic_cast<StLoc*>(falseBlock->Instructions[0].get());
    if (!stloc || !stloc->Variable) return false;
    auto* zero = dynamic_cast<LdcI4*>(stloc->Value.get());
    if (!zero || zero->Value != 0) return false;
    auto* br = dynamic_cast<Branch*>(falseBlock->FinalInstruction.get());
    if (!br || !br->TargetBlock) return false;
    returnVar = stloc->Variable.get();
    exitBlock = br->TargetBlock;
    return true;
}

// The shared exit block of the catch-when pattern:
//   Block exitBlock (2 pred) { leave container(ldloc ret) }
// (our model: Instructions=[], Final=Leave(container, LdLoc(ret))).
bool MatchExitBlock(Block* exitBlock, BlockContainer* container, ILVariable* returnVar) {
    if (!exitBlock || exitBlock->IncomingEdgeCount != 2) return false;
    if (!exitBlock->Instructions.empty()) return false;
    auto* leave = dynamic_cast<Leave*>(exitBlock->FinalInstruction.get());
    if (!leave || leave->TargetContainer != container) return false;
    auto* val = dynamic_cast<LdLoc*>(leave->Value.get());
    return val && val->Variable.get() == returnVar;
}

// The entry point of a catch-when filter:
//   Block entry (1 pred) {
//     stloc temp(isinst T(ldloc ex))     // 3-instruction variant only
//     if (comp(ldloc temp != ldnull)) br whenCond   // block final
//     // fall through to falseBlock
//   }
// or (2-instruction variant, no temp store):
//   Block entry (1 pred) {
//     if (comp(isinst T(ldloc ex) != ldnull)) br whenCond
//     // fall through to falseBlock
//   }
// The `br falseBlock` is this port's positional fall-through (the next block in
// the container) unless the if carries an explicit FalseInst Branch.
//
// On success returns: the caught-exception type (the isinst's type), the
// exception slot (ldloc ex, the value the entry block should store/carry), and
// the when-condition block to branch straight to.
struct EntryPointMatch {
    TypeSystem::ITypePtr ExceptionType;
    Block* WhenConditionBlock = nullptr;
    bool HasExtraStore = false;     // 3-instruction variant: a stloc temp to rewrite
    StLoc* TempStore = nullptr;     // the stloc temp(isinst ...) to rewrite
};

bool MatchCatchWhenEntryPoint(BlockContainer* container, Block* entryPoint, EntryPointMatch& m) {
    m = EntryPointMatch{};
    // The entry is the filter container's first block, reached by the EH
    // mechanism (no explicit Branch in the tree). The C# counts the container
    // entry edge (+1 when a BlockContainer connects) so it requires
    // IncomingEdgeCount == 1; this port's RecomputeIncomingEdgeCounts does not
    // count that edge, so the single-entry filter has IncomingEdgeCount == 0
    // (Blocks[0] gets no positional fall-through, and no Branch targets it).
    if (!entryPoint || entryPoint->IncomingEdgeCount != 0) return false;
    auto* iff = dynamic_cast<IfInstruction*>(entryPoint->FinalInstruction.get());
    if (!iff) return false;
    if (iff->FalseInst && iff->FalseInst->Op != OpCode::Branch) return false;
    auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
    if (!trueBr || !trueBr->TargetBlock) return false;
    auto* cond = dynamic_cast<Comp*>(iff->Condition.get());
    if (!cond) return false;
    ILInstruction* nonNull = nullptr;
    if (!MatchCompNotEqualsLdNull(cond, nonNull)) return false;

    IsInst* isinst = nullptr;
    LdLoc* exLoad = nullptr;
    if (entryPoint->Instructions.empty()) {
        // 2-instruction variant: comp(isinst T(ldloc ex) != ldnull)
        isinst = dynamic_cast<IsInst*>(nonNull);
        if (!isinst) return false;
        exLoad = dynamic_cast<LdLoc*>(isinst->Argument.get());
        if (!exLoad) return false;
        m.HasExtraStore = false;
    } else if (entryPoint->Instructions.size() == 1) {
        // 3-instruction variant: stloc temp(isinst T(ldloc ex)); comp(ldloc temp != ldnull)
        auto* stloc = dynamic_cast<StLoc*>(entryPoint->Instructions[0].get());
        if (!stloc || !stloc->Variable) return false;
        if (stloc->Variable->Kind != VariableKind::StackSlot) return false;
        isinst = dynamic_cast<IsInst*>(stloc->Value.get());
        if (!isinst) return false;
        exLoad = dynamic_cast<LdLoc*>(isinst->Argument.get());
        if (!exLoad) return false;
        // The compared side must be ldloc temp.
        auto* tempLoad = dynamic_cast<LdLoc*>(nonNull);
        if (!tempLoad || tempLoad->Variable.get() != stloc->Variable.get()) return false;
        m.HasExtraStore = true;
        m.TempStore = stloc;
    } else {
        return false;
    }

    // The false path target: explicit FalseInst Branch, else positional next.
    Block* falseBlock = nullptr;
    if (iff->FalseInst) {
        falseBlock = static_cast<Branch*>(iff->FalseInst.get())->TargetBlock;
    } else {
        for (std::size_t i = 0; i + 1 < container->Blocks.size(); ++i) {
            if (container->Blocks[i].get() == entryPoint) {
                falseBlock = container->Blocks[i + 1].get();
                break;
            }
        }
    }
    if (!falseBlock) return false;
    ILVariable* returnVar = nullptr;
    Block* exitBlock = nullptr;
    if (!MatchFalseBlock(falseBlock, returnVar, exitBlock)) return false;
    if (!MatchExitBlock(exitBlock, container, returnVar)) return false;

    m.ExceptionType = isinst->Type;
    m.WhenConditionBlock = trueBr->TargetBlock;
    return true;
}

} // namespace

void DetectCatchWhenConditionBlocks::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    std::vector<TryCatchHandler*> handlers;
    CollectHandlers(function.Body.get(), handlers);

    bool changed = false;
    for (auto* handler : handlers) {
        auto* container = dynamic_cast<BlockContainer*>(handler->Filter.get());
        if (!container || container->Blocks.empty()) continue;
        if (!handler->Variable) continue;
        EntryPointMatch m;
        if (!MatchCatchWhenEntryPoint(container, container->Blocks[0].get(), m)) continue;
        // The isinst type must agree in stack type with the catch variable.
        if (StackTypeOf(m.ExceptionType) != StackTypeOf(handler->Variable->Type)) continue;

        context.StepOnce("DetectCatchWhenConditionBlocks");
        // The catch is already typed T, so the isinst type test is redundant:
        // record the refined type and drop the test, branching straight to the
        // when-condition block.
        handler->Variable->Type = m.ExceptionType;
        Block* entry = container->Blocks[0].get();
        if (m.HasExtraStore) {
            // stloc temp(ldloc ex) -- replace the isinst with the caught exception.
            // The store's value (slot 0) is the isinst; the isinst's argument
            // (slot 0) is `ldloc ex`. Detach ldloc ex from the isinst, then make it
            // the store's value -- SetChild destroys the now-empty isinst.
            auto* isinst = dynamic_cast<IsInst*>(m.TempStore->GetChild(0));
            auto slot = isinst->TakeChild(0);  // ldloc ex, orphaned
            m.TempStore->SetChild(0, std::move(slot));
        }
        // Replace the if-final with a direct branch to the when-condition block.
        entry->SetFinal(std::make_unique<Branch>(m.WhenConditionBlock));
        changed = true;
    }

    if (changed) {
        // The false path (falseBlock -> exitBlock) lost its predecessor; the
        // entry block no longer falls through. Recompute the edge counts. The
        // now-unreachable falseBlock/exitBlock stay in the tree (erasing them is
        // unsafe in this port -- see decision D58).
        RecomputeIncomingEdgeCounts(function);
    }
}

} // namespace ILSpy::Decompiler::IL
