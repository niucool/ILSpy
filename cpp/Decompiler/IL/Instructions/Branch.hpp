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

// Branch: an unconditional goto to a Block in the current or an enclosing
// BlockContainer. TargetBlock is a reference (not a tree child), so the node has
// no children. DirectFlags = MayBranch | EndPointUnreachable.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class Block;  // forward declaration; TargetBlock is a non-owning reference

class Branch : public ILInstruction {
public:
    // The IL reader emits a Branch carrying TargetOffset (the IL offset of the
    // target block); BlockBuilder resolves TargetOffset -> TargetBlock later.
    // A resolved Branch (post-BlockBuilder) carries TargetBlock instead.
    std::uint32_t TargetOffset = 0;
    Block* TargetBlock = nullptr;
    bool HasOffset = false;  // true while this is an unresolved offset branch

    explicit Branch(std::uint32_t targetOffset = 0)
        : ILInstruction(OpCode::Branch), TargetOffset(targetOffset), HasOffset(true) {}
    explicit Branch(Block* target) : ILInstruction(OpCode::Branch), TargetBlock(target) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayBranch | InstructionFlags::EndPointUnreachable;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return 0; }
    ILInstruction* GetChild(int) const override { return nullptr; }
    void WriteTo(std::string& out) const override {
        out += "br IL_";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04X", TargetOffset);
        out += buf;
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int, std::unique_ptr<ILInstruction> n) override { return n; }
};

} // namespace ILSpy::Decompiler::IL
