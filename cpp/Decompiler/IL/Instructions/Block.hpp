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

// Block: a basic block -- a list of instructions plus a FinalInstruction that is
// an unconditional control flow (Branch/Leave/etc.). Children are the instruction
// list (slots 0..n-1) followed by the final (slot n). Every block must end in
// unconditional control flow; fall-through never survives the front end.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class Block : public ILInstruction {
public:
    std::vector<std::unique_ptr<ILInstruction>> Instructions;
    std::unique_ptr<ILInstruction> FinalInstruction;
    // The IL offset this block starts at (set by the IL reader). The C# carries
    // this via the node's ILRange; the BlockBuilder needs it to sort blocks and
    // assign them to nested containers.
    std::uint32_t StartILOffset = 0;

    Block() : ILInstruction(OpCode::Block) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }

    int ChildCount() const override {
        return static_cast<int>(Instructions.size()) + (FinalInstruction ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i >= 0 && i < static_cast<int>(Instructions.size())) return Instructions[i].get();
        if (i == static_cast<int>(Instructions.size()) && FinalInstruction) return FinalInstruction.get();
        return nullptr;
    }

    // Append a non-final instruction (the caller must still set a final).
    void Add(std::unique_ptr<ILInstruction> inst) {
        if (inst) { inst->Parent = this; inst->ChildIndex = static_cast<int>(Instructions.size()); }
        Instructions.push_back(std::move(inst));
    }
    // Set the block's final control-flow instruction.
    void SetFinal(std::unique_ptr<ILInstruction> inst) {
        if (inst) { inst->Parent = this; inst->ChildIndex = static_cast<int>(Instructions.size()); }
        FinalInstruction = std::move(inst);
    }

    void WriteTo(std::string& out) const override {
        out += "Block {\n";
        for (auto& i : Instructions) {
            out += "    ";
            if (i) i->WriteTo(out); else out += "(null)";
            out += '\n';
        }
        out += "    ";
        if (FinalInstruction) FinalInstruction->WriteTo(out); else out += "(no final)";
        out += "\n  }";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i >= 0 && i < static_cast<int>(Instructions.size())) {
            auto old = std::move(Instructions[i]);
            Instructions[i] = std::move(n);
            return old;
        }
        if (i == static_cast<int>(Instructions.size())) {
            auto old = std::move(FinalInstruction);
            FinalInstruction = std::move(n);
            return old;
        }
        return n;  // out of range: hand it back untouched
    }
};

} // namespace ILSpy::Decompiler::IL
