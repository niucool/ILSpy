// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

// UsingInstruction: the ILAst node for a C# `using` statement. Faithful to the
// generated UsingInstruction in ICSharpCode.Decompiler/IL/Instructions.cs and the
// hand-written UsingInstruction.cs. Two children: ResourceExpression (slot 0,
// inlineable -- the IDisposable resource being acquired) and Body (slot 1 -- the
// try block protected by the implicit finally that calls Dispose). Variable is
// the local the resource is stored into (an IStoreInstruction in the C#; this
// port counts it as a store in ComputeVariableUsage). IsAsync marks an
// `await using` (IAsyncDisposable; the async-using transform is deferred). IsRefStruct
// marks a `using` of a ref struct (the Dispose call has no null check). DirectFlags
// = MayWriteLocals | ControlFlow | SideEffect; result Void. The C# CheckInvariant
// also asserts the variable is non-null and the slots' result types; this port's
// CheckInvariant is non-virtual and checks tree consistency only (the other ported
// nodes skip their C# per-node asserts the same way). Per decision D1 the generated
// *output* is the source of truth.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class UsingInstruction : public ILInstruction {
public:
    ILVariablePtr Variable;  // the local the resource is stored into (IStoreInstruction)
    std::unique_ptr<ILInstruction> ResourceExpression;  // slot 0, inlineable
    std::unique_ptr<ILInstruction> Body;                // slot 1 (the try block)

    // Faithful to UsingInstruction.cs. IsAsync marks `await using` (the async-
    // using transform, which needs an Await node, is deferred). IsRefStruct
    // marks a `using` of a ref struct (the finally's Dispose has no null check).
    bool IsAsync = false;
    bool IsRefStruct = false;

    UsingInstruction(ILVariablePtr variable,
                     std::unique_ptr<ILInstruction> resourceExpression,
                     std::unique_ptr<ILInstruction> body)
        : ILInstruction(OpCode::UsingInstruction),
          Variable(std::move(variable)),
          ResourceExpression(std::move(resourceExpression)),
          Body(std::move(body)) {
        if (ResourceExpression) { ResourceExpression->Parent = this; ResourceExpression->ChildIndex = 0; }
        if (Body) { Body->Parent = this; Body->ChildIndex = 1; }
    }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayWriteLocals | InstructionFlags::ControlFlow | InstructionFlags::SideEffect;
    }
    // The base Flags() is DirectFlags() | union(children Flags()), which equals
    // the C# ComputeFlags (resourceExpression.Flags | body.Flags | MayWriteLocals
    // | ControlFlow | SideEffect). No override needed.
    StackType ResultType() const override { return StackType::Void; }

    int ChildCount() const override {
        return (ResourceExpression ? 1 : 0) + (Body ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return ResourceExpression.get();
        if (i == 1) return Body.get();
        return nullptr;
    }

    void WriteTo(std::string& out) const override {
        out += "using";
        if (IsAsync) out += ".async";
        if (IsRefStruct) out += ".ref";
        out += " (";
        out += Variable ? Variable->Name : std::string("?");
        out += " = ";
        if (ResourceExpression) ResourceExpression->WriteTo(out); else out += "(null)";
        out += ") ";
        if (Body) Body->WriteTo(out); else out += "(null)";
    }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 1);
        if (i == 0) { auto old = std::move(ResourceExpression); ResourceExpression = std::move(n); return old; }
        auto old = std::move(Body); Body = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
