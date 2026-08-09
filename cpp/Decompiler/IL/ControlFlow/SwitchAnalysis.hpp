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

// SwitchAnalysis: analyses a sequence of conditional branches that all test a
// single integer variable (a C# switch compiled to if-statements, e.g. when
// the case labels are non-contiguous) and reconstructs the value sets that
// drive each branch. The detected sections (a LongSet of case labels plus the
// body instruction each label set jumps to) feed SwitchDetection, which builds
// a single SwitchInstruction from them. Faithful to the
// ICSharpCode.Decompiler/IL/ControlFlow/SwitchAnalysis.cs analysis class.
//
// Adapted to this port's block model: the IfInstruction is the block's
// FinalInstruction with an implicit fall-through to the next block in the
// container (the C# carries the if as a non-terminal with an explicit
// fall-through Branch as the block's last instruction). So the two arms of each
// case-test if are the if's TrueInst (a Branch to the true block) and the next
// block in the container (the implicit fall-through). The fall-through is not
// an instruction in the tree, so the section body for the false arm is a
// synthesized Branch to the next block; SwitchAnalysis owns it in ownedBodies_
// until SwitchDetection consumes the sections.

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ILSpy::Decompiler::IL {

// One detected switch section: the set of case values that dispatch to a body
// instruction (a Branch/Leave that SwitchDetection moves into a SwitchSection).
struct SwitchSectionResult {
    Util::LongSet Labels;
    ILInstruction* Body = nullptr;  // non-owning; points into the tree or ownedBodies_
};

class SwitchAnalysis {
public:
    // The variable that is used to represent the switch expression. Null while
    // analyzing the first block; set by the first successful MatchSwitchVar.
    // Held as a shared_ptr so SwitchDetection can build a LdLoc from it.
    ILVariablePtr SwitchVariable;

    // Whether at least one of the analyzed blocks contained an IL switch
    // instruction.
    bool ContainsILSwitch = false;

    // The sections detected by the previous AnalyzeBlock call.
    std::vector<SwitchSectionResult> Sections;

    // Blocks that can be deleted if the tail of the root block is replaced with
    // a switch instruction (the inner if-chain blocks the switch absorbs).
    std::vector<Block*> InnerBlocks;

    // The block the analysis was seeded with.
    Block* RootBlock = nullptr;

    // Whether to allow unreachable cases in switch instructions.
    bool AllowUnreachableCases = false;

    // Analyze the tail of `block` and see if it can be turned into a switch
    // instruction. Returns true if the block could be analyzed successfully;
    // false otherwise. On success, Sections/InnerBlocks/SwitchVariable are
    // populated.
    bool AnalyzeBlock(Block* block);

    // Create the LongSet that contains a value x iff `x` compared with `val`
    // is true. Port of SwitchAnalysis.MakeSetWhereComparisonIsTrue; the C#
    // `Sign` is collapsed to a bool (true = unsigned), matching this port's
    // Comp::Unsigned field.
    static Util::LongSet MakeSetWhereComparisonIsTrue(ComparisonKind kind, long long val, bool unsigned_);

private:
    // De-duplication maps: a Branch target block -> section index, and a Leave
    // target container -> section index, so two arms that jump to the same
    // place merge into one section.
    std::unordered_map<Block*, int> targetBlockToSectionIndex_;
    std::unordered_map<BlockContainer*, int> targetContainerToSectionIndex_;
    // Synthesized fall-through Branches (not in the tree) kept alive so the
    // section bodies point at valid memory until SwitchDetection consumes them.
    std::vector<std::unique_ptr<ILInstruction>> ownedBodies_;

    bool AnalyzeBlockImpl(Block* block, Util::LongSet inputValues, bool tailOnly = false);
    bool AnalyzeSwitch(SwitchInstruction* inst, const Util::LongSet& inputValues);
    void AddSection(Util::LongSet values, ILInstruction* inst);
    bool MatchSwitchVar(ILInstruction* inst);
    bool MatchSwitchVar(ILInstruction* inst, long long& sub);
    bool AnalyzeCondition(ILInstruction* condition, Util::LongSet& trueValues);
    static Util::LongSet MakeGreaterThanOrEqualSet(long long val, bool unsigned_);
    static Util::LongSet MakeLessThanOrEqualSet(long long val, bool unsigned_);
};

} // namespace ILSpy::Decompiler::IL
