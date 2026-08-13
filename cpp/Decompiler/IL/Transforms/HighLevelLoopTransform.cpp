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

// Implementation of the HighLevelLoopTransform static helpers. See the header
// for the block-model adaptation notes.

#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <cassert>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

// Count the edges into `entry` that originate from a Branch somewhere in the
// function plus positional fall-through from any preceding block in its
// container (the same rules VariableUsage's RecomputeIncomingEdgeCounts uses,
// restricted to a single target).
static int IncomingEdgesTo(ILInstruction* root, Block* entry) {
    int count = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* br = dynamic_cast<Branch*>(inst))
            if (br->TargetBlock == entry) ++count;
        if (auto* c = dynamic_cast<BlockContainer*>(inst)) {
            for (std::size_t i = 0; i + 1 < c->Blocks.size(); ++i) {
                if (c->Blocks[i + 1].get() == entry) {
                    ILInstruction* fin = c->Blocks[i]->FinalInstruction.get();
                    if (fin && !HasFlag(fin->Flags(), InstructionFlags::EndPointUnreachable)) ++count;
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(root);
    return count;
}

// Whether `inst` or any descendant is an assignment (StLoc or a compound
// assign) -- the C# IsAssignment gate on a for-condition sub-expression.
static bool TreeHasAssignment(ILInstruction* inst) {
    if (!inst) return false;
    if (inst->Op == OpCode::StLoc || inst->Op == OpCode::NumericCompoundAssign ||
        inst->Op == OpCode::UserDefinedCompoundAssign) return true;
    for (int i = 0; i < inst->ChildCount(); ++i)
        if (TreeHasAssignment(inst->GetChild(i))) return true;
    return false;
}

// Whether `inst` or any descendant loads `variable` -- the C# use-in-condition
// gate (`condition.Descendants.Any(inst.MatchLdLoc(incrementVariable))`).
static bool TreeLoadsVariable(ILInstruction* inst, ILVariable* variable) {
    if (!inst) return false;
    if (auto* ld = dynamic_cast<LdLoc*>(inst))
        if (ld->Variable.get() == variable) return true;
    for (int i = 0; i < inst->ChildCount(); ++i)
        if (TreeLoadsVariable(inst->GetChild(i), variable)) return true;
    return false;
}

// The C# MatchForLoop "increment block" case: a While-shaped loop whose body
// state-machine has a trailing block of simple statements (an `i++` block --
// the block every `continue` targets) ends in a back-edge Branch to the loop
// head. Move that block to the container's end and remark the loop For, so
// the emitter can render `for (...; cond; incr) { body }`.
static bool TryMatchFor(BlockContainer* loop, ILInstruction* functionRoot) {
    if (loop->Kind != ContainerKind::While || loop->Blocks.size() < 2) return false;
    Block* entry = loop->Blocks.front().get();
    // The C# requires exactly two incoming edges at the entry point (pre-header
    // + the increment back-edge). In this port's model the pre-header never
    // branches to the loop entry -- the container itself is the statement --
    // so the only counted edge to the entry block is the increment back-edge;
    // anything else (a second back-edge skipping the increment, an external
    // goto) makes the for shape unsafe.
    if (IncomingEdgesTo(functionRoot, entry) != 1) return false;
    // Find the increment block: all simple statements + Branch entry.
    std::size_t incIndex = std::string::npos;
    for (std::size_t i = 1; i < loop->Blocks.size(); ++i) {
        Block* b = loop->Blocks[i].get();
        Block* head = nullptr;
        if (!HighLevelLoopTransform::MatchIncrementBlock(b, head) || head != entry) continue;
        if (b->Instructions.empty()) continue;   // C#: Instructions.Count <= 1 (i.e. no work)
        // A dedicated increment block still needs a real body between it and
        // the head (the C# 3-block minimum), so skip it for a 2-block loop.
        if (loop->Blocks.size() < 3) continue;
        incIndex = i;
        break;
    }
    if (incIndex != std::string::npos) {
        // Move the increment block to the end of the container
        // (MoveElementToEnd), preserving ChildIndex/Parent bookkeeping.
        if (incIndex != loop->Blocks.size() - 1) {
            auto hold = std::move(loop->Blocks[incIndex]);
            loop->Blocks.erase(loop->Blocks.begin() + incIndex);
            loop->Blocks.push_back(std::move(hold));
            for (std::size_t i = 0; i < loop->Blocks.size(); ++i) {
                loop->Blocks[i]->ChildIndex = static_cast<int>(i);
                loop->Blocks[i]->Parent = loop;
            }
        }
        loop->Kind = ContainerKind::For;
        return true;
    }
    // No dedicated increment block: the C#'s no-dedicated-block case splits a
    // trailing `stloc V(add(ldloc V, k)); br entry` tail off the body block
    // into a new increment block appended to the container. The while
    // condition must use the increment variable and be assignment-free (the
    // C# SplitConditions gates). This port limits the shape to a single
    // condition (no `&&`/`||` decomposition, which would need the three-valued
    // condition nodes, deferred): a non-atomic condition (not a Comp single
    // comparison) conservatively rejects the split.
    Block* lastBody = loop->Blocks.back().get();
    auto* backBr = dynamic_cast<Branch*>(lastBody->FinalInstruction.get());
    if (!backBr || backBr->TargetBlock != entry) return false;
    if (lastBody->Instructions.empty()) return false;
    // A While-condition-in-entry with a separate empty trailing condition
    // block (a csc 2-block for body) is outside the transform's scope -- the
    // header must directly branch into the body (handled by the dedicated-
    // increment case upstream).
    if (auto* entryIf = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get())) {
        if (entryIf->TrueInst && entryIf->TrueInst->Op == OpCode::Branch &&
            static_cast<Branch*>(entryIf->TrueInst.get())->TargetBlock != lastBody)
            return false;
    }
    ILVariablePtr incrVar;
    if (!HighLevelLoopTransform::MatchIncrement(lastBody->Instructions.back().get(), incrVar) || !incrVar)
        return false;
    if (incrVar->Kind == VariableKind::Parameter) return false;
    auto* cond = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get());
    if (!cond || !cond->Condition) return false;
    if (!TreeLoadsVariable(cond->Condition.get(), incrVar.get())) return false;
    if (TreeHasAssignment(cond->Condition.get())) return false;
    // A single-compound condition the C# would `&&`- or `||`-split is outside
    // this port: only a single Comp (or a variable) condition is safe to split,
    // since only one `if` guard would remain correct for it.
    if (cond->Condition->Op != OpCode::Comp && cond->Condition->Op != OpCode::LdLoc)
        return false;
    // Split: move the increment stloc and the back-edge branch into a new
    // block appended at the end; the body block now branches to the increment
    // block.
    auto newIncr = std::make_unique<Block>();
    Block* newIncrPtr = newIncr.get();
    newIncr->Add(std::move(lastBody->Instructions.back()));
    lastBody->Instructions.pop_back();
    newIncr->SetFinal(std::make_unique<Branch>(entry));
    lastBody->SetFinal(std::make_unique<Branch>(newIncrPtr));
    // Re-parent the moved increment into the new block: Add sets
    // Parent/ChildIndex; SetFinal the same for the branch.
    loop->AddBlock(std::move(newIncr));
    loop->Kind = ContainerKind::For;
    return true;
}

// The block that follows `block` in its container (the implicit fall-through
// target of a non-EndPointUnreachable final), or nullptr when `block` is the
// last block. Mirrors SwitchAnalysis's local helper.
static Block* NextBlockInContainer(Block* block) {
    auto* container = dynamic_cast<BlockContainer*>(block->Parent);
    if (!container) return nullptr;
    for (std::size_t i = 0; i < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == block) {
            return (i + 1 < container->Blocks.size()) ? container->Blocks[i + 1].get() : nullptr;
        }
    }
    return nullptr;
}

// Whether `leave` exits the function body (Port of Leave.IsLeavingFunction):
// the target container's parent is the ILFunction. Used to recognize a `return`
// where the C# MatchReturn would.
static bool IsLeavingFunction(Leave* leave) {
    return leave && leave->TargetContainer && leave->TargetContainer->Parent &&
           leave->TargetContainer->Parent->Op == OpCode::ILFunction;
}

bool HighLevelLoopTransform::IsSimpleStatement(ILInstruction* inst) {
    if (!inst) return false;
    // call/callvirt/newobj all emit a Call node in this port; the compound-assign
    // opcodes (NumericCompoundAssign/UserDefinedCompoundAssign) have no node
    // class yet, so no instruction carries them. Recognize the call and the
    // store kinds that exist today.
    return inst->Op == OpCode::Call || inst->Op == OpCode::StLoc || inst->Op == OpCode::StObj;
}

bool HighLevelLoopTransform::MatchIncrement(ILInstruction* inst, ILVariablePtr& variable) {
    if (!inst || inst->Op != OpCode::StLoc) return false;
    auto* st = static_cast<StLoc*>(inst);
    ILInstruction* value = st->Value.get();
    if (!value || value->Op != OpCode::BinaryNumericInstruction) return false;
    auto* bop = static_cast<BinaryNumericInstruction*>(value);
    if (bop->Operator != BinaryNumericOperator::Add) return false;
    ILInstruction* left = bop->Left.get();
    if (!left || left->Op != OpCode::LdLoc) return false;
    auto* ld = static_cast<LdLoc*>(left);
    if (ld->Variable.get() != st->Variable.get()) return false;
    variable = st->Variable;
    return true;
}

bool HighLevelLoopTransform::MatchIncrementBlock(Block* block, Block*& loopHead) {
    loopHead = nullptr;
    if (!block) return false;
    // The block's final must be a Branch to the loop head (the C# reads
    // Instructions.Last()); the other instructions (the C# SkipLast(1)) must all
    // be simple statements.
    auto* br = dynamic_cast<Branch*>(block->FinalInstruction.get());
    if (!br || !br->TargetBlock) return false;
    for (auto& inst : block->Instructions) {
        if (!IsSimpleStatement(inst.get())) return false;
    }
    loopHead = br->TargetBlock;
    return true;
}

bool HighLevelLoopTransform::MatchDoWhileConditionBlock(Block* block, Block*& target1, Block*& target2) {
    target1 = target2 = nullptr;
    if (!block) return false;
    // The if is the block's final (the C# reads Instructions[Count-2]); it must
    // have no else (FalseInst null -- the C# checks FalseInst.MatchNop()).
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (!iff || iff->FalseInst) return false;

    // True arm: a Branch to target1, or a return (a Leave exiting the function).
    if (auto* br = dynamic_cast<Branch*>(iff->TrueInst.get())) {
        target1 = br->TargetBlock;
    } else if (auto* leave = dynamic_cast<Leave*>(iff->TrueInst.get())) {
        if (!IsLeavingFunction(leave)) return false;
        target1 = nullptr;  // return: no block target
    } else {
        return false;
    }

    // Fall-through (false arm): the next block in the container (the C# reads
    // Instructions.Last() as an explicit fall-through Branch). When there is no
    // next block the fall-through exits the container; treat it as a return
    // (target2 = null) only when that exit leaves the function body, matching
    // the C# last.MatchReturn.
    Block* next = NextBlockInContainer(block);
    if (next) {
        target2 = next;
    } else {
        auto* container = dynamic_cast<BlockContainer*>(block->Parent);
        if (!container || !container->Parent || container->Parent->Op != OpCode::ILFunction)
            return false;  // falls out of a nested container: a leave, not a return
        target2 = nullptr;  // return fall-through
    }
    return true;
}

// Negate a condition (port of Comp.LogicNot): fold into a Comp when possible.
static std::unique_ptr<ILInstruction> NegateCondition(std::unique_ptr<ILInstruction> cond) {
    if (!cond) return cond;
    if (auto* comp = dynamic_cast<Comp*>(cond.get())) {
        if (comp->Kind == ComparisonKind::Equality && comp->Right &&
            comp->Right->Op == OpCode::LdcI4 &&
            static_cast<LdcI4*>(comp->Right.get())->Value == 0)
            return std::move(comp->Left);  // logic.not(x == 0) -> x
        comp->Kind = NegateComparison(comp->Kind);
        return cond;
    }
    if (auto* iff = dynamic_cast<IfInstruction*>(cond.get())) {
        auto t = std::move(iff->TrueInst);
        iff->TrueInst = std::move(iff->FalseInst);
        iff->FalseInst = std::move(t);
        if (iff->TrueInst) iff->TrueInst->ChildIndex = 1;
        if (iff->FalseInst) iff->FalseInst->ChildIndex = 2;
        return cond;
    }
    auto zero = (cond->ResultType() == StackType::O)
        ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
        : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
    return std::make_unique<Comp>(std::move(cond), std::move(zero), ComparisonKind::Equality);
}

void HighLevelLoopTransform::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Walk every Loop-kind container. (In our model the if is the block's
    // FinalInstruction; the C# reads it from Instructions[0] because the C#
    // block carries the if as a non-terminal with an explicit fall-through
    // Branch. Our entry point's if-as-final with no else (FalseInst null) is the
    // same shape.)
    std::vector<BlockContainer*> loops;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (auto* c = dynamic_cast<BlockContainer*>(inst))
            if (c->Kind == ContainerKind::Loop) loops.push_back(c);
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(function.Body.get());
    for (BlockContainer* loop : loops) {
        if (loop->Blocks.empty()) continue;
        Block* entry = loop->Blocks.front().get();
        auto* iff = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get());
        if (!iff) continue;
        // Shape 1 (C#): `if (cond) leave loop` (TrueInst a Leave, no else).
        // The while condition is the negated cond. Negate, the leave becomes the
        // false arm (break), a branch to the body becomes the true arm.
        if (!iff->FalseInst && iff->TrueInst && iff->TrueInst->Op == OpCode::Leave) {
            auto* leave = static_cast<Leave*>(iff->TrueInst.get());
            if (leave->TargetContainer != loop) continue;
            iff->Condition = NegateCondition(std::move(iff->Condition));
            if (iff->Condition) { iff->Condition->Parent = iff; iff->Condition->ChildIndex = 0; }
            iff->FalseInst = std::move(iff->TrueInst);
            if (iff->FalseInst) { iff->FalseInst->Parent = iff; iff->FalseInst->ChildIndex = 2; }
            iff->TrueInst = std::make_unique<Branch>(loop->Blocks.size() > 1 ? loop->Blocks[1].get() : entry);
            iff->TrueInst->Parent = iff;
            iff->TrueInst->ChildIndex = 1;
            loop->Kind = ContainerKind::While;
            if (TryMatchFor(loop, function.Body.get())) continue;
            continue;
        }
        // Shape 2 (this port's LoopDetection): `if (cond) br body else leave loop`
        // (TrueInst a Branch to a body block, FalseInst a Leave(loop)). The while
        // condition is cond (no negation). Just mark as While -- the seed extracts
        // the condition and renders the body blocks.
        if (iff->FalseInst && iff->FalseInst->Op == OpCode::Leave &&
            iff->TrueInst && iff->TrueInst->Op == OpCode::Branch) {
            auto* leave = static_cast<Leave*>(iff->FalseInst.get());
            if (leave->TargetContainer != loop) continue;
            loop->Kind = ContainerKind::While;
            if (TryMatchFor(loop, function.Body.get())) continue;
            continue;
        }
        // MatchDoWhileLoop: a loop whose last block is a do-while condition --
        // an if with a true-arm Branch to the loop header (continue) and a
        // fall-through that exits the loop (break). The entry point has no
        // while-condition if (it falls straight into the body). Mark as DoWhile
        // so the seed renders `do { ... } while (cond)`.
        if (loop->Blocks.size() >= 2) {
            Block* last = loop->Blocks.back().get();
            Block* header = loop->Blocks.front().get();
            if (auto* condIf = dynamic_cast<IfInstruction*>(last->FinalInstruction.get())) {
                if (!condIf->FalseInst && condIf->TrueInst &&
                    condIf->TrueInst->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(condIf->TrueInst.get());
                    // The true arm branches to the header (the do-while
                    // continue); the fall-through exits the loop (the entry
                    // point's final is NOT a while-condition if).
                    if (br->TargetBlock == header) {
                        // The entry must not itself be a while-condition (that's
                        // the While shape, already handled above).
                        auto* entryIf = dynamic_cast<IfInstruction*>(entry->FinalInstruction.get());
                        bool entryIsWhileCond = entryIf &&
                            ((entryIf->TrueInst && entryIf->TrueInst->Op == OpCode::Leave) ||
                             (entryIf->FalseInst && entryIf->FalseInst->Op == OpCode::Leave));
                        if (!entryIsWhileCond) {
                            loop->Kind = ContainerKind::DoWhile;
                            continue;
                        }
                    }
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
