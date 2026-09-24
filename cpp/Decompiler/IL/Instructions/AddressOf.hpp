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

// Port of the C# `AddressOf` node (ICSharpCode.Decompiler/IL/Instructions.cs,
// the `public sealed partial class AddressOf : ILInstruction`): takes the
// managed reference (`ref`/`out`/`in` argument or the operand of a
// `readonly.`-typed load) of the wrapped expression. One Value child (the C#
// ValueSlot, canInlineInto) plus the IType type operand (a plain field, not a
// child -- the C# GetChildCount is 1 and the type is carried as `IType type`).
// ResultType is StackType::Ref. The C# AcceptVisitor/ILVisitor surface has no
// port counterpart (the port dispatches through dynamic_cast), and the
// C# PerformMatch/WriteILRange pieces have no port counterpart (the port has
// no pattern-match or IL-range infrastructure on ILInstruction).
//
// Consumed by the CallBuilder span-based string-concat shape
// (`NewObj { Arguments: [AddressOf addressOf] }` with a ReadOnlySpan<char>
// ctor) and the `ExpressionBuilder::VisitUserDefinedCompoundAssign` span arm,
// both of which previously probed `Op == OpCode::AddressOf` generically.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>

namespace ILSpy::Decompiler::IL {

class AddressOf final : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Value;
    TypeSystem::ITypePtr Type;
    AddressOf(std::unique_ptr<ILInstruction> value, TypeSystem::ITypePtr type)
        : ILInstruction(OpCode::AddressOf), Value(std::move(value)), Type(std::move(type)) {
        if (Value) { Value->Parent = this; Value->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Ref; }
    int ChildCount() const override { return Value ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Value.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        out += "addressof(";
        out += Type ? Type->ReflectionName() : std::string("?");
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