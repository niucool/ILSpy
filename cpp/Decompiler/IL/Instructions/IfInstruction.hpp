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

// IfInstruction: an if/else. Three children -- Condition (inlineable), TrueInst,
// FalseInst (optional, may be a nullptr slot). The IL reader emits
// IfInstruction(condition, Branch(target)) for brtrue/brfalse and the comparison
// branches; the FalseInst is the fall-through. DirectFlags = ControlFlow;
// ResultType = trueInst's, or falseInst's if trueInst is EndPointUnreachable.
// Port of IfInstruction (generated + hand-written partial).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class IfInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Condition;
    std::unique_ptr<ILInstruction> TrueInst;
    std::unique_ptr<ILInstruction> FalseInst;  // optional (may be null)

    IfInstruction(std::unique_ptr<ILInstruction> condition,
                  std::unique_ptr<ILInstruction> trueInst,
                  std::unique_ptr<ILInstruction> falseInst = nullptr)
        : ILInstruction(OpCode::IfInstruction),
          Condition(std::move(condition)),
          TrueInst(std::move(trueInst)),
          FalseInst(std::move(falseInst)) {
        if (Condition) { Condition->Parent = this; Condition->ChildIndex = 0; }
        if (TrueInst) { TrueInst->Parent = this; TrueInst->ChildIndex = 1; }
        if (FalseInst) { FalseInst->Parent = this; FalseInst->ChildIndex = 2; }
    }

    InstructionFlags DirectFlags() const override { return InstructionFlags::ControlFlow; }
    InstructionFlags Flags() const override {
        // ControlFlow | condition | CombineBranches(true, false) -- a missing
        // else arm means fall-through, so the endpoint is always reachable.
        InstructionFlags c = Condition ? Condition->Flags() : InstructionFlags::None;
        InstructionFlags t = TrueInst ? TrueInst->Flags() : InstructionFlags::None;
        InstructionFlags f = FalseInst ? FalseInst->Flags() : InstructionFlags::None;
        return InstructionFlags::ControlFlow | c | CombineBranches(t, f);
    }
    StackType ResultType() const override {
        if (TrueInst && HasFlag(TrueInst->DirectFlags(), InstructionFlags::EndPointUnreachable)) {
            return FalseInst ? FalseInst->ResultType() : StackType::Void;
        }
        return TrueInst ? TrueInst->ResultType() : StackType::Void;
    }

    int ChildCount() const override {
        return (Condition ? 1 : 0) + (TrueInst ? 1 : 0) + (FalseInst ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Condition.get();
        if (i == 1) return TrueInst.get();
        if (i == 2) return FalseInst.get();
        return nullptr;
    }

    void WriteTo(std::string& out) const override {
        out += "if (";
        if (Condition) Condition->WriteTo(out); else out += "(null)";
        out += ") ";
        if (TrueInst) TrueInst->WriteTo(out); else out += "(null)";
        if (FalseInst) {
            out += " else ";
            FalseInst->WriteTo(out);
        }
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 2);
        if (i == 0) { auto old = std::move(Condition); Condition = std::move(n); return old; }
        if (i == 1) { auto old = std::move(TrueInst); TrueInst = std::move(n); return old; }
        auto old = std::move(FalseInst); FalseInst = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
