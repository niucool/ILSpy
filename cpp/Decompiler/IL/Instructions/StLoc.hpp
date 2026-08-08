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

// StLoc: store a value into a variable. One child (Value); the variable is a
// reference. DirectFlags = MayWriteLocals (a direct store is not a SideEffect per
// the InstructionFlags convention). Result Void.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class StLoc : public ILInstruction {
public:
    ILVariablePtr Variable;
    std::unique_ptr<ILInstruction> Value;
    StLoc(ILVariablePtr v, std::unique_ptr<ILInstruction> value)
        : ILInstruction(OpCode::StLoc), Variable(std::move(v)), Value(std::move(value)) {
        if (Value) { Value->Parent = this; Value->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayWriteLocals; }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return Value ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Value.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        out += "stloc(";
        out += Variable ? Variable->Name : std::string("?");
        out += ", ";
        if (Value) Value->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Value);
        Value = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
