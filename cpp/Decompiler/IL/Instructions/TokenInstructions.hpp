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
#include "Decompiler/TypeSystem/IMethod.hpp"
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
    // The C# `IMethod Method { get; }` -- the resolved method the CallBuilder
    // delegate-construction arm reads. Null on reader-built nodes (the IL
    // reader discards the token target; the stand-in convention); the
    // CallBuilder path asserts it (the C# node is only ever created with a
    // method).
    std::shared_ptr<const TypeSystem::IMethod> Method;
    explicit LdFtn(std::string method = std::string())
        : SimpleInstruction(OpCode::LdFtn), MethodName(std::move(method)) {}
    // The IMethod-bearing ctor (the C# `new LdFtn(method)` -- the resolved
    // method the CallBuilder consumes; the MethodName derives from it).
    LdFtn(std::shared_ptr<const TypeSystem::IMethod> method)
        : SimpleInstruction(OpCode::LdFtn),
          MethodName(std::string(method->Name())), Method(std::move(method)) {}
    StackType ResultType() const override { return StackType::I; }
    void WriteTo(std::string& out) const override {
        out += "ldftn("; out += MethodName; out += ')';
    }
};

// ldvirtftn <method>: push a virtual function pointer. Result I.
class LdVirtFtn : public SimpleInstruction {
public:
    std::string MethodName;
    // The C# `IMethod Method { get; }` -- the resolved virtual method the
    // CallBuilder delegate-construction arm reads. Null on reader-built nodes
    // (the IL reader discards the token target; the stand-in convention).
    std::shared_ptr<const TypeSystem::IMethod> Method;
    explicit LdVirtFtn(std::string method = std::string())
        : SimpleInstruction(OpCode::LdVirtFtn), MethodName(std::move(method)) {}
    // The IMethod-bearing ctor (the C# `new LdVirtFtn(method)` -- the resolved
    // virtual method the CallBuilder consumes; the MethodName derives from it).
    LdVirtFtn(std::shared_ptr<const TypeSystem::IMethod> method)
        : SimpleInstruction(OpCode::LdVirtFtn),
          MethodName(std::string(method->Name())), Method(std::move(method)) {}
    StackType ResultType() const override { return StackType::I; }
    void WriteTo(std::string& out) const override {
        out += "ldvirtftn("; out += MethodName; out += ')';
    }
};

// sizeof <T>: push the size of T in bytes. Result I4. The C# `SizeOf(IType type)`
// carries an `IType` field (the C# node's `type` field -- the type operand the
// ExpressionBuilder's VisitSizeOf arm consumes); the port additionally keeps the
// resolved display string the seed renders (`sizeof(TypeName)`) -- the C# WriteToCore
// writes the IType through `type.WriteTo`, which the port's display string approximates
// for the seed (the LdFtn/LdVirtFtn precedent). The Type may be null when the token did
// not resolve (the reader's never-throw ResolveTypeToken convention).
class SizeOf : public SimpleInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::string TypeName;
    explicit SizeOf(std::string type = std::string())
        : SimpleInstruction(OpCode::SizeOf), TypeName(std::move(type)) {}
    // The C# `SizeOf(IType type)` ctor -- the type-bearing form the IL reader builds.
    SizeOf(TypeSystem::ITypePtr type, std::string typeName)
        : SimpleInstruction(OpCode::SizeOf), Type(std::move(type)), TypeName(std::move(typeName)) {}
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
    // The C# `IMethod Method { get; }` -- the resolved virtual method the
    // CallBuilder delegate-reference family reads (BuildLdVirtDelegate). The
    // IL reader discards the ldvirtftn target method (the stand-in
    // convention), so this field is null on reader-built nodes; the
    // CallBuilder path asserts it (the C# node is only ever created with a
    // method).
    std::shared_ptr<const TypeSystem::IMethod> Method;
    LdVirtDelegate(std::unique_ptr<ILInstruction> argument, TypeSystem::ITypePtr type,
                  std::string method)
        : UnaryInstruction(OpCode::LdVirtDelegate, std::move(argument)),
          Type(std::move(type)), MethodName(std::move(method)) {}
    // The IMethod-bearing ctor (the C# `new LdVirtDelegate(argument, method)` --
    // the delegate type/method pair the CallBuilder consumes).
    LdVirtDelegate(std::unique_ptr<ILInstruction> argument, TypeSystem::ITypePtr type,
                  std::string methodName,
                  std::shared_ptr<const TypeSystem::IMethod> resolvedMethod)
        : UnaryInstruction(OpCode::LdVirtDelegate, std::move(argument)),
          Type(std::move(type)), MethodName(std::move(methodName)),
          Method(std::move(resolvedMethod)) {}
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
// Result O (a boxed handle). The C# `LdTypeToken(IType type)` carries an `IType` field
// (the type operand VisitLdTypeToken renders through `typeof(T).TypeHandle`); the port
// additionally keeps the resolved display string the seed renders -- the C#
// WriteToCore writes the IType, which the port's display string approximates (the
// LdFtn/LdVirtFtn precedent). The Type may be null: the `arglist` opcode decodes as
// this node with only a display name (the port's degenerate `Arglist` stand-in, the
// C# `Arglist` node being a separate SimpleInstruction the port has not landed), and
// an unresolved token leaves the type null per the reader's never-throw convention.
class LdTypeToken : public SimpleInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::string TokenName;
    explicit LdTypeToken(std::string name = std::string())
        : SimpleInstruction(OpCode::LdTypeToken), TokenName(std::move(name)) {}
    // The C# `LdTypeToken(IType type)` ctor -- the type-bearing form the IL reader builds.
    LdTypeToken(TypeSystem::ITypePtr type, std::string name)
        : SimpleInstruction(OpCode::LdTypeToken), Type(std::move(type)), TokenName(std::move(name)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "ldtoken("; out += TokenName; out += ')';
    }
};

// ldmembertoken <member>: push a runtime member handle (the C#
// `LdMemberToken : SimpleInstruction` carrying the resolved IMember -- the
// MethodBase.GetMethodFromHandle argument the expression-tree call sites
// build). Result O. The member is the resolved-method handle (non-owning:
// the metadata owns it); TokenName is the display form.
class LdMemberToken : public SimpleInstruction {
public:
    // The C# `LdMemberToken(IMember member)`: the member handle carries
    // any member kind (the method-handle shape the call sites build, the
    // field-handle shape FieldInfo.GetFieldFromHandle consumes).
    std::shared_ptr<const TypeSystem::IMember> Member;
    std::string TokenName;
    explicit LdMemberToken(std::string name = std::string())
        : SimpleInstruction(OpCode::LdMemberToken), TokenName(std::move(name)) {}
    LdMemberToken(std::shared_ptr<const TypeSystem::IMember> member,
                  std::string name)
        : SimpleInstruction(OpCode::LdMemberToken), Member(std::move(member)),
          TokenName(std::move(name)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "ldtoken "; out += TokenName;
    }
};

} // namespace ILSpy::Decompiler::IL
