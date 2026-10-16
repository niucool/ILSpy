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

// YieldReturn: yield a value out of an iterator function. `yield.return <value>`
// is the final instruction of an iterator MoveNext's body (the Value is the
// yielded expression). DirectFlags = MayBranch | SideEffect; ResultType = Void.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <memory>

namespace ILSpy::Decompiler::IL {

class YieldReturn : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Value;  // the yielded expression

    explicit YieldReturn(std::unique_ptr<ILInstruction> value = nullptr)
        : ILInstruction(OpCode::YieldReturn), Value(std::move(value)) {
        if (Value) { Value->Parent = this; Value->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayBranch | InstructionFlags::SideEffect;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return Value ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Value.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        out += "yield.return";
        if (Value) { out += ' '; Value->WriteTo(out); }
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
