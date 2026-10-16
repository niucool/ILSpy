// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The C# `public sealed partial class Await : ILInstruction` (Instructions.cs
// lines 6846-6940 + Instructions/Await.cs): the await operation. The single
// child is the awaited value; `GetAwaiterMethod` / `GetResultMethod` record
// the methods the Await was built from (the async-await pattern detection
// fills them; the AST emission renders `await x` and consults them for the
// await's result type).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"

namespace ILSpy::Decompiler::TypeSystem {
class IMethod;
}  // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::IL {

class Await : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Value;  // the awaited expression

    // The C# `public IMethod? GetAwaiterMethod` / `public IMethod?
    // GetResultMethod` (Instructions/Await.cs lines 27-29): the methods the
    // await lowered to. Non-owning shared handles (the type system owns the
    // methods; the AsyncAwaitDecompiler fills them from the decoded calls).
    std::shared_ptr<TypeSystem::IMethod> GetAwaiterMethod;
    std::shared_ptr<TypeSystem::IMethod> GetResultMethod;

    explicit Await(std::unique_ptr<ILInstruction> value = nullptr)
        : ILInstruction(OpCode::Await), Value(std::move(value)) {
        if (Value) { Value->Parent = this; Value->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::SideEffect;
    }
    // The C# `StackType ResultType => GetResultMethod?.ReturnType.GetStackType()
    // ?? StackType.Unknown`.
    StackType ResultType() const override {
        if (GetResultMethod == nullptr) return StackType::Unknown;
        return StackTypeOf(&GetResultMethod->ReturnType());
    }
    int ChildCount() const override { return Value ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Value.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        out += "await";
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
