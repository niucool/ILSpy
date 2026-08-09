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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/ControlFlow/SwitchAnalysis.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/Util/Interval.hpp"

#include <limits>

namespace ILSpy::Decompiler::IL {

namespace {

// Match an integer constant (LdcI4 or LdcI8). The C# MatchLdcI also unwraps
// Conv sign/zero-extend; this port's reader does not wrap constants in Conv for
// ldc, so the plain LdcI4/LdcI8 cases cover the switch-analysis inputs.
bool MatchLdcI(ILInstruction* inst, long long& val) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdcI4) {
        val = static_cast<LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == OpCode::LdcI8) {
        val = static_cast<LdcI8*>(inst)->Value;
        return true;
    }
    return false;
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

} // namespace

bool SwitchAnalysis::AnalyzeBlock(Block* block) {
    SwitchVariable.reset();
    RootBlock = block;
    targetBlockToSectionIndex_.clear();
    targetContainerToSectionIndex_.clear();
    Sections.clear();
    InnerBlocks.clear();
    ownedBodies_.clear();
    ContainsILSwitch = false;
    if (!block) return false;
    return AnalyzeBlockImpl(block, Util::LongSet::Universe(), /*tailOnly*/ true);
}

bool SwitchAnalysis::AnalyzeBlockImpl(Block* block, Util::LongSet inputValues, bool tailOnly) {
    if (block->Instructions.empty() && !block->FinalInstruction) {
        // might happen if the block was already marked for deletion in SwitchDetection
        return false;
    }
    if (tailOnly) {
        // root block: analyze the tail (the final instruction)
    } else {
        // switchVar should always be determined by the top-level call.
        if (block->IncomingEdgeCount != 1 || block == RootBlock)
            return false;  // only consider if-structures that form a tree
        if (block->Parent != RootBlock->Parent)
            return false;  // all blocks should belong to the same container
    }

    // In this port's block model the IfInstruction is the block's final with an
    // implicit fall-through to the next block; the C# checks Instructions[Count-2]
    // as a non-terminal if with an explicit fall-through Branch as the last
    // instruction. The if must have no else (FalseInst null) so it is the pure
    // `if (cond) br X` case-test shape.
    Util::LongSet trueValues;
    auto* iff = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (iff && !iff->FalseInst && AnalyzeCondition(iff->Condition.get(), trueValues)) {
        if (!(tailOnly || block->Instructions.empty()))
            return false;
        trueValues = trueValues.IntersectWith(inputValues);
        if (trueValues.SetEquals(inputValues) || trueValues.IsEmpty())
            return false;
        // The if's true arm: a Branch to a block (recurse) or another exit
        // instruction (create a section for it).
        auto* trueBr = dynamic_cast<Branch*>(iff->TrueInst.get());
        Block* trueBlock = (trueBr && trueBr->TargetBlock) ? trueBr->TargetBlock : nullptr;
        if (trueBlock && AnalyzeBlockImpl(trueBlock, trueValues)) {
            InnerBlocks.push_back(trueBlock);
        } else {
            AddSection(trueValues, iff->TrueInst.get());
        }
    } else if (block->FinalInstruction && block->FinalInstruction->Op == OpCode::SwitchInstruction) {
        auto* switchInst = static_cast<SwitchInstruction*>(block->FinalInstruction.get());
        if (!(tailOnly || block->Instructions.empty()))
            return false;
        if (AnalyzeSwitch(switchInst, inputValues)) {
            ContainsILSwitch = true;  // OK
            return true;
        }
        return false;  // switch analysis failed (e.g. switchVar mismatch)
    } else {
        return false;  // unknown final instruction
    }

    // The false arm is the fall-through to the next block in the container
    // (the C# reads it as block.Instructions.Last(), an explicit Branch). The
    // fall-through is implicit here, so synthesize a Branch to the next block
    // as the section body and keep it alive in ownedBodies_.
    auto remainingValues = inputValues.ExceptWith(trueValues);
    Block* falseBlock = NextBlockInContainer(block);
    if (falseBlock && AnalyzeBlockImpl(falseBlock, remainingValues)) {
        InnerBlocks.push_back(falseBlock);
    } else if (falseBlock) {
        auto br = std::make_unique<Branch>(falseBlock);
        auto* raw = br.get();
        ownedBodies_.push_back(std::move(br));
        AddSection(std::move(remainingValues), raw);
    } else {
        // No fall-through target: the if's false arm has nowhere to go in this
        // container, so the block does not form a switch.
        return false;
    }
    return true;
}

bool SwitchAnalysis::AnalyzeSwitch(SwitchInstruction* inst, const Util::LongSet& inputValues) {
    // IsLifted is not modeled in this port (no nullable lifting yet); the C#
    // asserts !inst.IsLifted, which holds by construction here.
    long long offset;
    if (MatchSwitchVar(inst->Value.get())) {
        offset = 0;
    } else if (auto* bop = dynamic_cast<BinaryNumericInstruction*>(inst->Value.get())) {
        if (bop->CheckForOverflow)
            return false;
        long long val;
        if (MatchSwitchVar(bop->Left.get()) && MatchLdcI(bop->Right.get(), val)) {
            switch (bop->Operator) {
                case BinaryNumericOperator::Add:
                    // C# unchecked(-val): wrap the negation through uint64.
                    offset = static_cast<long long>(static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(val));
                    break;
                case BinaryNumericOperator::Sub:
                    offset = val;
                    break;
                default:
                    return false;  // unknown bop.Operator
            }
        } else {
            return false;  // unknown bop.Left
        }
    } else {
        return false;  // unknown inst.Value
    }
    for (auto& section : inst->Sections) {
        if (!section) continue;
        auto matchValues = section->Labels.AddOffset(offset).IntersectWith(inputValues);
        if (!AllowUnreachableCases && matchValues.IsEmpty())
            return false;
        if (matchValues.Count() > 1) {
            auto* bodyBr = dynamic_cast<Branch*>(section->Body.get());
            Block* targetBlock = (bodyBr && bodyBr->TargetBlock) ? bodyBr->TargetBlock : nullptr;
            if (targetBlock && AnalyzeBlockImpl(targetBlock, matchValues)) {
                InnerBlocks.push_back(targetBlock);
                continue;
            }
        }
        AddSection(matchValues, section->Body.get());
    }
    return true;
}

void SwitchAnalysis::AddSection(Util::LongSet values, ILInstruction* inst) {
    if (values.IsEmpty()) {
        return;
    }
    if (auto* br = dynamic_cast<Branch*>(inst)) {
        Block* target = br->TargetBlock;
        if (target) {
            auto it = targetBlockToSectionIndex_.find(target);
            if (it != targetBlockToSectionIndex_.end()) {
                auto& primary = Sections[static_cast<std::size_t>(it->second)];
                primary.Labels = primary.Labels.UnionWith(values);
                primary.Body = inst;
            } else {
                targetBlockToSectionIndex_.emplace(target, static_cast<int>(Sections.size()));
                Sections.push_back({std::move(values), inst});
            }
            return;
        }
    } else if (auto* leave = dynamic_cast<Leave*>(inst)) {
        BlockContainer* target = leave->TargetContainer;
        if (target) {
            auto it = targetContainerToSectionIndex_.find(target);
            if (it != targetContainerToSectionIndex_.end()) {
                auto& primary = Sections[static_cast<std::size_t>(it->second)];
                primary.Labels = primary.Labels.UnionWith(values);
                primary.Body = inst;
            } else {
                targetContainerToSectionIndex_.emplace(target, static_cast<int>(Sections.size()));
                Sections.push_back({std::move(values), inst});
            }
            return;
        }
    }
    Sections.push_back({std::move(values), inst});
}

bool SwitchAnalysis::MatchSwitchVar(ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::LdLoc)
        return false;
    auto* ld = static_cast<LdLoc*>(inst);
    if (SwitchVariable) {
        return ld->Variable == SwitchVariable;
    }
    SwitchVariable = ld->Variable;
    return true;
}

bool SwitchAnalysis::MatchSwitchVar(ILInstruction* inst, long long& sub) {
    if (auto* bn = dynamic_cast<BinaryNumericInstruction*>(inst)) {
        // !bn.IsLifted is not modeled; CheckForOverflow is. The C# checks the
        // Sub operator with a constant right operand and recurses into the left.
        if (bn->Operator == BinaryNumericOperator::Sub && !bn->CheckForOverflow) {
            if (MatchLdcI(bn->Right.get(), sub)) {
                return MatchSwitchVar(bn->Left.get());
            }
        }
    }
    sub = 0;
    return MatchSwitchVar(inst);
}

bool SwitchAnalysis::AnalyzeCondition(ILInstruction* condition, Util::LongSet& trueValues) {
    // if (comp((V - sub) OP val))
    if (auto* comp = dynamic_cast<Comp*>(condition)) {
        long long sub;
        long long val;
        if (MatchSwitchVar(comp->Left.get(), sub) && MatchLdcI(comp->Right.get(), val)) {
            trueValues = MakeSetWhereComparisonIsTrue(comp->Kind, val, comp->Unsigned);
            trueValues = trueValues.AddOffset(sub);
            return true;
        }
    }
    // if (ldloc V) --> branch for all values except 0
    if (MatchSwitchVar(condition)) {
        trueValues = Util::LongSet(0).Invert();
        return true;
    }
    // if (logic.not(X)) --> branch for all values where if (X) does not branch.
    // logic.not is comp(Equality, X, ldc.i4 0) in this port (the reader emits
    // brfalse as that comp).
    if (condition && condition->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(condition);
        if (comp->Kind == ComparisonKind::Equality && !comp->Unsigned && comp->Right &&
            comp->Right->Op == OpCode::LdcI4 &&
            static_cast<LdcI4*>(comp->Right.get())->Value == 0) {
            Util::LongSet falseValues;
            bool res = AnalyzeCondition(comp->Left.get(), falseValues);
            trueValues = falseValues.Invert();
            return res;
        }
    }
    trueValues = Util::LongSet::Empty();
    return false;
}

Util::LongSet SwitchAnalysis::MakeSetWhereComparisonIsTrue(ComparisonKind kind, long long val, bool unsigned_) {
    switch (kind) {
        case ComparisonKind::Equality:
            return Util::LongSet(val);
        case ComparisonKind::Inequality:
            return Util::LongSet(val).Invert();
        case ComparisonKind::LessThan:
            return MakeGreaterThanOrEqualSet(val, unsigned_).Invert();
        case ComparisonKind::LessThanOrEqual:
            return MakeLessThanOrEqualSet(val, unsigned_);
        case ComparisonKind::GreaterThan:
            return MakeLessThanOrEqualSet(val, unsigned_).Invert();
        case ComparisonKind::GreaterThanOrEqual:
            return MakeGreaterThanOrEqualSet(val, unsigned_);
    }
    return Util::LongSet::Empty();  // unreachable
}

Util::LongSet SwitchAnalysis::MakeGreaterThanOrEqualSet(long long val, bool unsigned_) {
    const long long Min = std::numeric_limits<long long>::min();
    const long long Max = std::numeric_limits<long long>::max();
    if (!unsigned_) {  // Sign.Signed
        return Util::LongSet(Util::LongInterval::Inclusive(val, Max));
    }
    // Sign.Unsigned (or None, which only arises from logic.not whose kind is
    // Equality and does not reach here): the unsigned range [val..Max] wraps
    // around the signed boundary.
    if (val >= 0) {
        return Util::LongSet(Util::LongInterval::Inclusive(val, Max))
            .UnionWith(Util::LongSet(Util::LongInterval(Min, 0)));
    }
    return Util::LongSet(Util::LongInterval(val, 0));
}

Util::LongSet SwitchAnalysis::MakeLessThanOrEqualSet(long long val, bool unsigned_) {
    const long long Min = std::numeric_limits<long long>::min();
    const long long Max = std::numeric_limits<long long>::max();
    if (!unsigned_) {  // Sign.Signed
        return Util::LongSet(Util::LongInterval::Inclusive(Min, val));
    }
    if (val >= 0) {
        return Util::LongSet(Util::LongInterval::Inclusive(0, val));
    }
    // The range 0 to (ulong)val expressed with signed longs is two ranges.
    return Util::LongSet(Util::LongInterval::Inclusive(0, Max))
        .UnionWith(Util::LongSet(Util::LongInterval(Min, val)));
}

} // namespace ILSpy::Decompiler::IL
