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

// UnboxAny: unbox.any <T> -- the C# unboxing conversion. SideEffect + MayThrow
// (InvalidCastException); result is T (modeled as O for the foundation).
// UnaryInstruction.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class UnboxAny : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    UnboxAny(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::UnboxAny, std::move(argument)), Type(std::move(type)) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::SideEffect | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackTypeOf(Type); }
    void WriteTo(std::string& out) const override {
        out += "unbox.any(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
