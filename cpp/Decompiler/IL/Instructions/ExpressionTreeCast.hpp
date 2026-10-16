// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/IL/Instructions/ExpressionTreeCast.cs -- the
// transform-local cast node the expression-tree conversion emits for an
// explicit `Expression.Convert`/`Expression.TypeAs` shape (and the boxed
// enum/bool constant arm). The C# `partial class ExpressionTreeCast :
// UnaryInstruction, ILiftableInstruction` ports to a header-only UnaryInstruction
// subclass carrying the target Type and the IsChecked flag; the result stack
// type derives from the target type (TypeUtils.GetStackType, the C#
// WithType-derived ResultType). The C# `IsLifted`/`UnderlyingResultType`
// liftable half is deferred with the nullable-lifting surface (the
// expression-tree converter never lifts).

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class ExpressionTreeCast final : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    bool IsChecked = false;

    // The C# `ExpressionTreeCast(IType type, ILInstruction argument,
    // bool isChecked)`.
    ExpressionTreeCast(TypeSystem::ITypePtr type,
                       std::unique_ptr<ILInstruction> argument, bool isChecked)
        : UnaryInstruction(OpCode::ExpressionTreeCast, std::move(argument)),
          Type(std::move(type)), IsChecked(isChecked) {}

    // The C# result type flows from the target type's stack type (the
    // WithType-derived base; the port reads it through TypeUtils.GetStackType).
    StackType ResultType() const override {
        return Type != nullptr ? TypeSystem::GetStackType(*Type) : StackType::Unknown;
    }
    int ChildCount() const override { return Argument ? 1 : 0; }
    ILInstruction* GetChild(int i) const override {
        return i == 0 ? Argument.get() : nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "expressiontreecast";
        if (IsChecked) out += ".checked";
        out += '(';
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i,
                                               std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Argument);
        Argument = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
