// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// AddressOf: `addressof <type>(<value>)` -- the managed-reference wrapper the
// IL reader emits for the `ldloca`/`ldsflda` address-of forms and the transforms
// rewrap when an inlined expression needs an lvalue address. Port of the C#
// generated `AddressOf` node (IL/Instructions.cs): a single `Value` child
// (canInlineInto), a `Type` operand, `ResultType` Ref, `DirectFlags` None with
// `ComputeFlags` delegating to the child.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class AddressOf final : public UnaryInstruction {
public:
    // The C# `IType type` operand (the type the reference points at).
    TypeSystem::ITypePtr Type;

    AddressOf(std::unique_ptr<ILInstruction> value, TypeSystem::ITypePtr type)
        : UnaryInstruction(OpCode::AddressOf, std::move(value)), Type(std::move(type)) {}

    // The C# `public override StackType ResultType { get { return StackType.Ref; } }`.
    StackType ResultType() const override { return StackType::Ref; }

    // The C# `DirectFlags { get { return InstructionFlags.None; } }` with
    // `ComputeFlags()` delegating to the child's flags: the port folds the two
    // into one override (every existing port node carries only the direct
    // flags; no consumer walks ComputeFlags separately).
    InstructionFlags DirectFlags() const override {
        return Argument ? Argument->Flags() : InstructionFlags::None;
    }

    void WriteTo(std::string& out) const override {
        out += "addressof ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
