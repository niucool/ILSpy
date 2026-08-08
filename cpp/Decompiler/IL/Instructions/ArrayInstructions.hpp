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

// Array construction and element-address nodes, faithful to the generated
// Instructions.cs. newarr <T> is NewArr(T, [count]); ldelema/ldelem/stelem use
// LdElema(T, array, [index]) as the address, with LdObj/StObj doing the typed
// load/store (the C# LdElem/StElem helpers compose the same way).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

// newarr <T>: allocate a zero-based, one-dimensional array. Children: the index
// (length) expressions (one for SZ arrays). DirectFlags = MayThrow; result O.
class NewArr : public ILInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::vector<std::unique_ptr<ILInstruction>> Indices;
    NewArr(TypeSystem::ITypePtr type, std::vector<std::unique_ptr<ILInstruction>> indices)
        : ILInstruction(OpCode::NewArr), Type(std::move(type)), Indices(std::move(indices)) {
        for (std::size_t i = 0; i < Indices.size(); ++i) {
            if (Indices[i]) { Indices[i]->Parent = this; Indices[i]->ChildIndex = static_cast<int>(i); }
        }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override { return StackType::O; }
    int ChildCount() const override { return static_cast<int>(Indices.size()); }
    ILInstruction* GetChild(int i) const override {
        return (i >= 0 && i < static_cast<int>(Indices.size())) ? Indices[i].get() : nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "newarr(";
        out += Type ? Type->ReflectionName() : std::string("?");
        for (auto& idx : Indices) { out += ", "; if (idx) idx->WriteTo(out); else out += "(null)"; }
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i < 0 || i >= static_cast<int>(Indices.size())) return n;
        auto old = std::move(Indices[i]);
        Indices[i] = std::move(n);
        return old;
    }
};

// ldelema <T>: &array[index]. Children: Array (slot 0) then the index expressions
// (slot 1+, a collection). Result is I (unmanaged pointer) if Array is an integer
// type, else Ref. DirectFlags = MayThrow.
class LdElema : public ILInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::unique_ptr<ILInstruction> Array;
    std::vector<std::unique_ptr<ILInstruction>> Indices;
    LdElema(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> array,
            std::vector<std::unique_ptr<ILInstruction>> indices)
        : ILInstruction(OpCode::LdElema), Type(std::move(type)), Array(std::move(array)),
          Indices(std::move(indices)) {
        if (Array) { Array->Parent = this; Array->ChildIndex = 0; }
        for (std::size_t i = 0; i < Indices.size(); ++i) {
            if (Indices[i]) { Indices[i]->Parent = this; Indices[i]->ChildIndex = static_cast<int>(i + 1); }
        }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override {
        if (Array && Array->ResultType() == StackType::I) return StackType::I;
        return StackType::Ref;
    }
    int ChildCount() const override { return (Array ? 1 : 0) + static_cast<int>(Indices.size()); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Array.get();
        int idx = i - 1;
        return (idx >= 0 && idx < static_cast<int>(Indices.size())) ? Indices[idx].get() : nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "ldelema(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Array) Array->WriteTo(out); else out += "(null)";
        for (auto& idx : Indices) { out += ", "; if (idx) idx->WriteTo(out); else out += "(null)"; }
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(Array); Array = std::move(n); return old; }
        int idx = i - 1;
        if (idx < 0 || idx >= static_cast<int>(Indices.size())) return n;
        auto old = std::move(Indices[idx]);
        Indices[idx] = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
