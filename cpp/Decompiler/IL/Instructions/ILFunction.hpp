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

// ILFunction: the root of an ILAst tree for one method body. Owns the body
// BlockContainer and the function's variables. Transforms grow the tree downward
// by grafting in nested ILFunctions (lambdas, local functions, expression trees).

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class ILFunction : public ILInstruction {
public:
    std::unique_ptr<BlockContainer> Body;
    std::vector<ILVariablePtr> Variables;

    // The constructor/static status of this function's method, the pre-resolved
    // subset of the C# ILFunction.Method handle the transforms consult. Defaults
    // false (a null Method, matching the C# `function?.Method is not {...}` bail)
    // and is populated by the IL reader from the MethodDef flags/name. The gate
    // the NullCoalescingTransform hoisted-constructor-argument null-guard fold
    // consults is `IsConstructor && !IsStatic` (an instance constructor).
    bool IsConstructor = false;
    bool IsStatic = false;

    ILFunction() : ILInstruction(OpCode::ILFunction) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }
    bool IsRoot() const override { return true; }

    int ChildCount() const override { return 1; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? static_cast<ILInstruction*>(Body.get()) : nullptr; }

    void WriteTo(std::string& out) const override {
        out += "ILFunction {\n  ";
        if (Body) Body->WriteTo(out); else out += "(no body)";
        out += "\n}";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Body);
        // The function body slot always holds a BlockContainer.
        Body.reset(static_cast<BlockContainer*>(n.release()));
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
