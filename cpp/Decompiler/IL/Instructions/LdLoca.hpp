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

// LdLoca: load the address of a variable (&v). The result is a managed ref
// (StackType::Ref). SimpleInstruction-like: the variable is a reference, no
// children. DirectFlags = MayReadLocals.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class LdLoca : public ILInstruction {
public:
    ILVariablePtr Variable;
    explicit LdLoca(ILVariablePtr v) : ILInstruction(OpCode::LdLoca), Variable(std::move(v)) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayReadLocals; }
    StackType ResultType() const override { return StackType::Ref; }
    int ChildCount() const override { return 0; }
    ILInstruction* GetChild(int) const override { return nullptr; }
    void WriteTo(std::string& out) const override {
        out += "ldloca(";
        out += Variable ? Variable->Name : std::string("?");
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int, std::unique_ptr<ILInstruction> n) override { return n; }
};

} // namespace ILSpy::Decompiler::IL
