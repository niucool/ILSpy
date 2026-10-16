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

// Unbox: `unbox <T>` -- the managed-pointer unboxing operation, distinct from
// `unbox.any <T>` (UnboxAny). The CIL `unbox` instruction yields a managed
// pointer (ref T) to the boxed data, so the result type is StackType.Ref, and it
// only throws (InvalidCastException) -- it does NOT have a side effect (the C#
// Unbox DirectFlags is base | MayThrow, unlike UnboxAny's SideEffect | MayThrow).
// The argument is the boxed value on the eval stack. UnaryInstruction.
//
// The C# reader maps ILOpCode.Unbox to Unbox and ILOpCode.Unbox_any to UnboxAny
// (ILReader.cs lines 1270-1273); the port's reader previously collapsed both onto
// UnboxAny, so the unbox consumer arms (ExpressionBuilder.VisitUnbox, the
// TryCatchHandler inline-unbox arm, the `IsUnboxAny` pattern matches) never saw a
// dedicated node.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class Unbox : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    Unbox(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::Unbox, std::move(argument)), Type(std::move(type)) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayThrow;
    }
    // Faithful to the C# `public override StackType ResultType => StackType.Ref`:
    // `unbox T` pushes a managed pointer to the boxed T, not T itself.
    StackType ResultType() const override { return StackType::Ref; }
    void WriteTo(std::string& out) const override {
        out += "unbox(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
