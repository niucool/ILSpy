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

// Call: a method invocation. Children are the arguments (in order); the method
// is identified by a display name the IL reader fills in from the resolved
// MethodDef/MemberRef token. DirectFlags = SideEffect | MayThrow.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class Call : public ILInstruction {
public:
    std::string MethodName;  // "Namespace.Type::Method" (resolved by the IL reader)
    std::vector<std::unique_ptr<ILInstruction>> Arguments;
    StackType ReturnType = StackType::Unknown;
    // The return type of the resolved method, as an IType. Set by the IL reader
    // from the method signature's ReturnType (the same callSig->ReturnType
    // ReturnType is derived from via StackTypeOf). Null for a void method or
    // when the signature could not be resolved; for `newobj` the declared void
    // return is stored here (ReturnType is overridden to O for the constructed
    // object, but newobj is excluded by `!IsNewObj` in the access-chain check).
    // Carried so transforms that need the full return IType (e.g. NullPropagation
    // TryNullPropagation's NullCoalescing output case, which checks
    // NullableType.IsNonNullableValueType on the access chain's result type)
    // can consult it without a MetadataFile handle (the transforms carry only
    // Settings + Step, per D78).
    TypeSystem::ITypePtr ReturnIType;
    // The declaring type of the resolved method, as an IType. Set by the IL
    // reader from the method token (MethodDef parent TypeDef, MemberRef parent
    // TypeRef/TypeDef/TypeSpec, MethodSpec unwrapped). Null when the token
    // could not be resolved (e.g. an out-of-range or unsupported parent); null
    // is treated like the C# `Method.DeclaringTypeDefinition == null`. Used by
    // the nullable-lifting helpers to recognise Nullable<T>.get_HasValue /
    // GetValueOrDefault via the type's KnownTypeCode.
    TypeSystem::ITypePtr DeclaringType;
    // True for call/callvirt on an instance method (Arguments[0] is the
    // receiver); false for static calls and newobj. Set by the IL reader.
    bool IsInstanceCall = false;
    // True for a `newobj` call (the C# models this as a separate NewObj node;
    // this port reuses Call with this flag, matching the IsInstanceCall
    // precedent). The IL reader sets it from the decoded opcode. Consumed by
    // DelegateConstruction.MatchDelegateConstruction (the C# `case NewObj call:`)
    // and the seed's `new Type(args)` rendering path.
    bool IsNewObj = false;
    // True when the resolved method is a C# operator overload -- a method
    // whose name (the part after '::') is one of the recognised `op_*` names
    // (op_Equality, op_Addition, op_Implicit, ...). The C# models this as
    // `IMethod.IsOperator` (a SymbolKind derived from the MethodDef's
    // SpecialName/RTSpecialName flag + the `op_` name prefix recognised by
    // `OperatorDeclaration.GetOperatorType`); this port approximates it by the
    // name alone, since every C#-compiled operator overload carries SpecialName
    // (the attribute check would need the MethodDef flags column for in-module
    // MethodDefs and the full type system to resolve cross-assembly MemberRefs,
    // neither worth the cost for the name-only signal). Set by the IL reader from
    // the resolved MethodName. Consumed by NullableLiftingTransform::
    // MatchCompOrDecimal's Decimal branch (the 6 comparison operators on
    // System.Decimal, which have no IL Comp instruction and lower to op_* calls).
    bool IsOperator = false;
    // The number of generic type arguments this call's method-spec
    // instantiation supplies (0 for a non-generic MethodDef/MemberRef call, N
    // for a MethodSpec call like `Activator.CreateInstance<T>()` where N == 1).
    // Populated by the IL reader from the MethodSpec Instantiation blob (the
    // MethodSpecSig). The C# carries this as `Method.TypeArguments.Count`;
    // transforms consult it to distinguish a generic-instantiation call from a
    // non-generic overload (e.g. NullableLiftingTransform.IsGenericNewPattern
    // checks `Activator.CreateInstance` with exactly one type argument).
    int TypeArgumentsCount = 0;

    explicit Call(std::string method = std::string()) : ILInstruction(OpCode::Call), MethodName(std::move(method)) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::SideEffect | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return ReturnType; }

    int ChildCount() const override { return static_cast<int>(Arguments.size()); }
    ILInstruction* GetChild(int i) const override {
        return (i >= 0 && i < static_cast<int>(Arguments.size())) ? Arguments[i].get() : nullptr;
    }

    void AddArg(std::unique_ptr<ILInstruction> a) {
        if (a) { a->Parent = this; a->ChildIndex = static_cast<int>(Arguments.size()); }
        Arguments.push_back(std::move(a));
    }

    void WriteTo(std::string& out) const override {
        out += "call ";
        out += MethodName;
        out += '(';
        for (std::size_t i = 0; i < Arguments.size(); ++i) {
            if (i) out += ", ";
            if (Arguments[i]) Arguments[i]->WriteTo(out); else out += "(null)";
        }
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i < 0 || i >= static_cast<int>(Arguments.size())) return n;
        auto old = std::move(Arguments[i]);
        Arguments[i] = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
