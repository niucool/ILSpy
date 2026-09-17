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

// MakeRefAny (`mkrefany <T>`) and RefAnyValue (`refanyval <T>`): the two
// typed-reference accessors beside RefAnyType (`refanytype`). Both are
// UnaryInstruction subclasses carrying the type operand (the C# generated
// `MakeRefAny(ILInstruction argument, IType type)` /
// `RefAnyValue(ILInstruction argument, IType type)`). MakeRefAny boxes the
// argument's address into a `TypedReference` (result O); RefAnyValue reads the
// value back out of a typed reference as a managed pointer (result Ref,
// MayThrow). The type may be null when the token did not resolve (the reader's
// never-throw ResolveTypeToken convention).

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// mkrefany <T>: push a TypedReference over the argument. UnaryInstruction;
// result O.
class MakeRefAny : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    MakeRefAny(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::MakeRefAny, std::move(argument)), Type(std::move(type)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "makerefany ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// refanyval <T>: push the address stored in the argument's TypedReference.
// UnaryInstruction; result Ref, MayThrow.
class RefAnyValue : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    RefAnyValue(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::RefAnyValue, std::move(argument)), Type(std::move(type)) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackType::Ref; }
    void WriteTo(std::string& out) const override {
        out += "refanyval ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
