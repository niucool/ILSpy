// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The C# `public sealed partial class LdElemaInlineArray : ILInstruction`
// (Instructions.cs line 4996): the folded inline-array element reference
// (`ldelema.inlinearray`), built by InlineArrayTransform from the
// compiler-generated helper calls. Children: the Array slot (slot 0) + the
// Indices (slots 1..N; the transform produces exactly one). The type operand
// is the inline-array struct type (a plain field, not a child -- the LdElema
// convention).
#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class LdElemaInlineArray final : public ILInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::unique_ptr<ILInstruction> Array;
    std::vector<std::unique_ptr<ILInstruction>> Indices;
    // The C# `public bool IsReadOnly`: the 'readonly.' prefix was applied
    // (the ReadOnlySpan/ElementRefReadOnly helpers read without writing).
    bool IsReadOnly = false;

    LdElemaInlineArray(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> array,
                       std::vector<std::unique_ptr<ILInstruction>> indices)
        : ILInstruction(OpCode::LdElemaInlineArray), Type(std::move(type)),
          Array(std::move(array)), Indices(std::move(indices)) {
        if (Array) { Array->Parent = this; Array->ChildIndex = 0; }
        for (std::size_t i = 0; i < Indices.size(); ++i) {
            if (Indices[i]) {
                Indices[i]->Parent = this;
                Indices[i]->ChildIndex = static_cast<int>(i + 1);
            }
        }
    }

    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override { return StackType::Ref; }
    int ChildCount() const override {
        return (Array ? 1 : 0) + static_cast<int>(Indices.size());
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Array.get();
        int s = i - 1;
        return (s >= 0 && s < static_cast<int>(Indices.size())) ? Indices[s].get() : nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "ldelema.inlinearray";
        if (IsReadOnly) out += ".readonly";
        out += '(';
        if (Type) out += Type->ReflectionName(); else out += "(null)";
        out += ", ";
        if (Array) Array->WriteTo(out); else out += "(null)";
        for (const auto& idx : Indices) {
            out += ", ";
            if (idx) idx->WriteTo(out); else out += "(null)";
        }
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) {
            auto old = std::move(Array);
            Array = std::move(n);
            return old;
        }
        int s = i - 1;
        std::unique_ptr<ILInstruction> old;
        if (s >= 0 && s < static_cast<int>(Indices.size())) {
            old = std::move(Indices[s]);
            Indices[s] = std::move(n);
        }
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
