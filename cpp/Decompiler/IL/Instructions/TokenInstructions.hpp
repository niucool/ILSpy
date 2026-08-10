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

// Minimal nodes for ldftn, sizeof, ldtoken, and mkrefany/refanyval/refanytype.
// These carry a display string (resolved by the IL reader) and have no children
// (the token/type is a reference, not a tree child). They are SimpleInstruction
// subclasses; the full ILAst has richer nodes (LdFtn carries an IMethod, SizeOf
// carries an IType) but the display-string form is enough for the reader to
// build a valid tree and for the ILAst dump. The C# generated nodes in
// Instructions.cs carry the same opcodes.

#pragma once

#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::IL {

// ldftn <method>: push a function pointer. Result I (native int / fn pointer).
class LdFtn : public SimpleInstruction {
public:
    std::string MethodName;
    explicit LdFtn(std::string method = std::string())
        : SimpleInstruction(OpCode::LdFtn), MethodName(std::move(method)) {}
    StackType ResultType() const override { return StackType::I; }
    void WriteTo(std::string& out) const override {
        out += "ldftn("; out += MethodName; out += ')';
    }
};

// ldvirtftn <method>: push a virtual function pointer. Result I.
class LdVirtFtn : public SimpleInstruction {
public:
    std::string MethodName;
    explicit LdVirtFtn(std::string method = std::string())
        : SimpleInstruction(OpCode::LdVirtFtn), MethodName(std::move(method)) {}
    StackType ResultType() const override { return StackType::I; }
    void WriteTo(std::string& out) const override {
        out += "ldvirtftn("; out += MethodName; out += ')';
    }
};

// sizeof <T>: push the size of T in bytes. Result I4.
class SizeOf : public SimpleInstruction {
public:
    std::string TypeName;
    explicit SizeOf(std::string type = std::string())
        : SimpleInstruction(OpCode::SizeOf), TypeName(std::move(type)) {}
    StackType ResultType() const override { return StackType::I4; }
    void WriteTo(std::string& out) const override {
        out += "sizeof("; out += TypeName; out += ')';
    }
};

// ldvirtdelegate <DelegateType> <Method>(<target>): the ILAst normalization of a
// virtual delegate construction `newobj DelegateType(target, ldvirtftn Method(target))`
// -- ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate folds the
// newobj so the delegate target and the virtual method are unified. A UnaryInstruction
// (the Argument slot is the target, inlineable) carrying the delegate type (ITypePtr)
// and the resolved method name string. Result O; MayThrow (a virtual call resolves the
// method). Port of the C# LdVirtDelegate (generated Instructions.cs) -- the C# carries
// an IMethod; this port carries the resolved method name string, matching the
// LdFtn/LdVirtFtn precedent (no IMethod type-system object).
class LdVirtDelegate : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::string MethodName;
    LdVirtDelegate(std::unique_ptr<ILInstruction> argument, TypeSystem::ITypePtr type,
                  std::string method)
        : UnaryInstruction(OpCode::LdVirtDelegate, std::move(argument)),
          Type(std::move(type)), MethodName(std::move(method)) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "ldvirtdelegate ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ' ';
        out += MethodName;
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// ldtoken <T>/<method>/<field>: push a RuntimeTypeHandle/MethodHandle/FieldHandle.
// Result O (a boxed handle).
class LdTypeToken : public SimpleInstruction {
public:
    std::string TokenName;
    explicit LdTypeToken(std::string name = std::string())
        : SimpleInstruction(OpCode::LdTypeToken), TokenName(std::move(name)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "ldtoken("; out += TokenName; out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
