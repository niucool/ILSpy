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
// OTHERWISE, ARISING FROM, IN OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// PinnedRegion: a `fixed` block. The variable is the pinned local; Init is the
// expression that pins it (e.g. ldelema/ldflda/array.to.pointer); Body is a
// BlockContainer of the region. The variable is a reference (not a tree child),
// so the node has two children: Init (slot 0) and Body (slot 1). DirectFlags =
// MayWriteLocals (it writes the pinned local); result Void. Faithful to the
// generated PinnedRegion in ICSharpCode.Decompiler/IL/Instructions.cs.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class PinnedRegion : public ILInstruction {
public:
    ILVariablePtr Variable;
    std::unique_ptr<ILInstruction> Init;
    std::unique_ptr<ILInstruction> Body;
    PinnedRegion(ILVariablePtr v, std::unique_ptr<ILInstruction> init,
                 std::unique_ptr<ILInstruction> body)
        : ILInstruction(OpCode::PinnedRegion), Variable(std::move(v)),
          Init(std::move(init)), Body(std::move(body)) {
        if (Init) { Init->Parent = this; Init->ChildIndex = 0; }
        if (Body) { Body->Parent = this; Body->ChildIndex = 1; }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayWriteLocals; }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return (Init ? 1 : 0) + (Body ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Init.get();
        if (i == 1) return Body.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "pinned.region ";
        out += Variable ? Variable->Name : std::string("?");
        out += '(';
        if (Init) Init->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Body) Body->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(Init); Init = std::move(n); return old; }
        if (i == 1) { auto old = std::move(Body); Body = std::move(n); return old; }
        return n;
    }
};

} // namespace ILSpy::Decompiler::IL
