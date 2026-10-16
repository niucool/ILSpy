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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// LockInstruction: the ILAst node for a C# `lock` statement. Faithful to the
// generated LockInstruction in ICSharpCode.Decompiler/IL/Instructions.cs and
// the hand-written LockInstruction.cs. Two children: OnExpression (slot 0,
// inlineable -- the object whose monitor is entered) and Body (slot 1 -- the
// try block protected by the implicit finally that calls Monitor.Exit).
// DirectFlags = ControlFlow | SideEffect; result Void. The C# CheckInvariant
// also asserts OnExpression.ResultType == StackType.O; this port's
// CheckInvariant is non-virtual and checks tree consistency only (the other
// ported nodes skip their C# per-node asserts the same way), so the
// object-typed assertion is not enforced here. Per decision D1 the generated
// *output* is the source of truth.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class LockInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> OnExpression;  // slot 0, inlineable
    std::unique_ptr<ILInstruction> Body;           // slot 1 (the try block)

    LockInstruction(std::unique_ptr<ILInstruction> onExpression,
                    std::unique_ptr<ILInstruction> body)
        : ILInstruction(OpCode::LockInstruction),
          OnExpression(std::move(onExpression)),
          Body(std::move(body)) {
        if (OnExpression) { OnExpression->Parent = this; OnExpression->ChildIndex = 0; }
        if (Body) { Body->Parent = this; Body->ChildIndex = 1; }
    }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::ControlFlow | InstructionFlags::SideEffect;
    }
    // The base Flags() is DirectFlags() | union(children Flags()), which equals
    // the C# ComputeFlags (onExpression.Flags | body.Flags | ControlFlow |
    // SideEffect). No override needed.
    StackType ResultType() const override { return StackType::Void; }

    int ChildCount() const override {
        return (OnExpression ? 1 : 0) + (Body ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return OnExpression.get();
        if (i == 1) return Body.get();
        return nullptr;
    }

    void WriteTo(std::string& out) const override {
        out += "lock (";
        if (OnExpression) OnExpression->WriteTo(out); else out += "(null)";
        out += ") ";
        if (Body) Body->WriteTo(out); else out += "(null)";
    }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 1);
        if (i == 0) { auto old = std::move(OnExpression); OnExpression = std::move(n); return old; }
        auto old = std::move(Body); Body = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
