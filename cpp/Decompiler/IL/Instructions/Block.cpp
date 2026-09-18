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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Block.cpp -- the out-of-line Block members that would otherwise drag the
// Disassembler include graph into every IL TU (Label is the DisassemblerHelpers
// OffsetToString of the block's own start offset), plus the out-of-line
// BlockContainer loop-shape matchers (MatchConditionBlock / MatchIncrementBlock
// -- their bodies call the shared PatternMatching.hpp matchers, whose include
// chain reaches BlockContainer.hpp through Branch.hpp, so in-class definitions
// would recurse the include guards).

#include "Decompiler/IL/Instructions/Block.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/PatternMatching.hpp"

namespace ILSpy::Decompiler::IL {

std::string Block::Label() const {
    return Disassembler::OffsetToString(static_cast<int>(StartILOffset));
}

bool Block::MatchInlineAssignBlock(ILInstruction*& call,
                                    ILInstruction*& value) const {
    call = nullptr;
    value = nullptr;
    if (Kind != BlockKind::CallInlineAssign)
        return false;
    if (Instructions.size() != 1)
        return false;
    call = Instructions[0].get();
    auto* callInstruction = dynamic_cast<Call*>(call);
    if (callInstruction == nullptr || callInstruction->Arguments.empty())
        return false;
    ILVariable* tmp = nullptr;
    ILInstruction* storedValue = nullptr;
    if (!MatchStLoc(callInstruction->Arguments.back().get(), tmp)
        || !MatchStLoc(callInstruction->Arguments.back().get(), tmp, storedValue))
        return false;
    if (!(tmp->IsSingleDefinition() && tmp->LoadCount == 1))
        return false;
    value = storedValue;
    return MatchLdLoc(FinalInstruction.get(), tmp);
}

bool BlockContainer::MatchConditionBlock(Block* block, ILInstruction*& condition,
                                          Block*& bodyStartBlock) {
    condition = nullptr;
    bodyStartBlock = nullptr;
    if (block->Instructions.size() != 1)
        return false;
    ILInstruction* cond = nullptr;
    ILInstruction* trueInst = nullptr;
    ILInstruction* falseInst = nullptr;
    if (!MatchIfInstruction(block->Instructions[0].get(), cond, trueInst, falseInst))
        return false;
    condition = cond;
    return MatchLeave(falseInst, this) && MatchBranch(trueInst, bodyStartBlock);
}

bool BlockContainer::MatchIncrementBlock(Block* block) {
    if (block->Instructions.empty())
        return false;
    if (!MatchBranch(block->Instructions.back().get(), EntryPoint()))
        return false;
    return true;
}

}  // namespace ILSpy::Decompiler::IL
