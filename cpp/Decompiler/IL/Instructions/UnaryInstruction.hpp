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

// UnaryInstruction: abstract base for nodes with a single Argument child
// (castclass, isinst, box, unbox.any, conv, throw, ...). The Argument slot is
// inlineable (CanInlineInto = true). DirectFlags is None; subclasses add flags
// (e.g. CastClass adds MayThrow) and supply the result type. Port of the C#
// UnaryInstruction base in the generated Instructions.cs.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class UnaryInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Argument;
protected:
    UnaryInstruction(OpCode op, std::unique_ptr<ILInstruction> argument)
        : ILInstruction(op), Argument(std::move(argument)) {
        if (Argument) { Argument->Parent = this; Argument->ChildIndex = 0; }
    }
public:
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    int ChildCount() const override { return Argument ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Argument.get() : nullptr; }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Argument);
        Argument = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
