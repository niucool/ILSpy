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

// BinaryInstruction: abstract base for nodes with Left + Right children (both
// inlineable). DirectFlags is None; subclasses (Comp, BinaryNumericInstruction)
// supply the result type and any added flags. Port of the C# BinaryInstruction
// base in the generated Instructions.cs.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class BinaryInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Left;
    std::unique_ptr<ILInstruction> Right;
protected:
    BinaryInstruction(OpCode op, std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right)
        : ILInstruction(op), Left(std::move(left)), Right(std::move(right)) {
        if (Left) { Left->Parent = this; Left->ChildIndex = 0; }
        if (Right) { Right->Parent = this; Right->ChildIndex = 1; }
    }
public:
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    int ChildCount() const override { return (Left ? 1 : 0) + (Right ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Left.get();
        if (i == 1) return Right.get();
        return nullptr;
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0 || i == 1);
        if (i == 0) { auto old = std::move(Left); Left = std::move(n); return old; }
        auto old = std::move(Right); Right = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
