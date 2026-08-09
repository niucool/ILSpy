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

// DefaultValue: the default value for a type (the ILAst model of `initobj`/the
// `default(T)` expression). A SimpleInstruction leaf carrying the type operand;
// the result stack type is the type's stack type. Port of the C# DefaultValue
// (generated Instructions.cs) -- the type is data, not a tree child.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <string>

namespace ILSpy::Decompiler::IL {

class DefaultValue : public SimpleInstruction {
public:
    TypeSystem::ITypePtr Type;
    explicit DefaultValue(TypeSystem::ITypePtr type)
        : SimpleInstruction(OpCode::DefaultValue), Type(std::move(type)) {}
    StackType ResultType() const override { return StackTypeOf(Type); }
    void WriteTo(std::string& out) const override {
        out += "default.value(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
