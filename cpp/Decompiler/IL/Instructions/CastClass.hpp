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

// CastClass: castclass <T> -- cast to T, throws InvalidCastException on failure.
// UnaryInstruction; DirectFlags += MayThrow; result O.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class CastClass : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    CastClass(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::CastClass, std::move(argument)), Type(std::move(type)) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "castclass(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
