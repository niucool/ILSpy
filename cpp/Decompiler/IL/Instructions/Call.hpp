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
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

// The C# `internal static StackType CallInstruction.ExpectedTypeForThisPointer(
// IType declaringType, IType? constrainedTo)` (CallInstruction.cs lines 107-124):
// the expected stack type for passing the this pointer in a method call.
// Returns StackType.Ref when constrainedTo is not null, StackType.O for
// reference types (the this pointer passed as an object reference), and
// StackType.Ref for type parameters and value types (the this pointer passed
// as a managed reference). Returns StackType.Unknown when the input type is
// unknown (the `IsReferenceType` tri-state carries neither true nor false --
// e.g. a type whose reference-ness is indeterminate).
inline StackType ExpectedTypeForThisPointer(const TypeSystem::IType* declaringType,
                                            const TypeSystem::IType* constrainedTo)
{
    if (constrainedTo != nullptr)
        return StackType::Ref;
    assert(declaringType != nullptr);
    if (declaringType->Kind() == TypeSystem::TypeKind::TypeParameter)
        return StackType::Ref;
    const std::optional<bool> isReferenceType = declaringType->IsReferenceType();
    if (isReferenceType.has_value())
        return *isReferenceType ? StackType::O : StackType::Ref;
    return StackType::Unknown;
}

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
    // The resolved parameter types of the method, as ITypes, in declaration
    // order (no implicit `this`). Set by the IL reader from the method
    // signature's ParameterTypes (the same callSig->ParameterTypes ReturnType
    // is derived from). Empty when the signature could not be resolved. The
    // faithful equivalent of the C# `Method.Parameters[i].Type` (the C# also
    // carries the parameter name/kind; this port's transforms consult only the
    // type -- e.g. EarlyExpressionTransforms.TransformDecimalCtorToConstant
    // dispatches the 1-arg `newobj Decimal(...)` ctor on the first parameter's
    // KnownTypeCode to distinguish the int/uint/long/ulong overloads, which the
    // resolved MethodName `System.Decimal::.ctor` alone cannot). Carried so
    // transforms that need a parameter type can consult it without a
    // MetadataFile handle (per D78).
    std::vector<TypeSystem::ITypePtr> ParameterIType;
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
    // Whether this is a lifted user-defined operator call. Faithful to the C#
    // `CallInstruction.IsLifted` (`Method is CSharp.Resolver.ILiftedOperator`),
    // a resolver-level concept: the C# resolver wraps a lifted operator's
    // method in an `ILiftedOperator` marker, so a `Call` whose `Method` is that
    // marker reports `IsLifted == true`. This port has no resolver / no
    // `ILiftedOperator`, so the field defaults false and the IL reader does not
    // populate it (the common case -- lifted user-defined operators are rare and
    // the C# `UserDefinedCompoundAssign` folds themselves bail on a lifted
    // operator call with `if (operatorCall.IsLifted) return false; // TODO`,
    // so a default-false call never trips that bail). It is a settable field so
    // a future resolver-backed path can mark a lifted operator call.
    bool IsLifted = false;

    // The C# `public readonly IMethod Method` -- the resolved method identity the
    // CSharp back end consumes (the CallBuilder's MemberResolveResult render, the
    // span-based string-concat detection, the accessor/operator checks). Null on
    // the seed path (the IL reader fills the MethodName/flag stand-ins above; the
    // call arms that need the IMethod are not yet wired into the reader), so every
    // consumer must null-check before dereferencing -- the same optional-method
    // shape the UserDefinedCompoundAssign upgrade carried before its second ctor
    // landed. Set directly by tests (the FakeMethod/LookupMethod fixtures) and
    // later by the reader once the type-system plumbing reaches it.
    std::shared_ptr<TypeSystem::IMethod> Method;
    // The C# `public bool IsTail` -- whether the call carries the IL 'tail.'
    // prefix (the C# surfaces it as an inline `/*tail.*/` comment marker).
    bool IsTail = false;
    // The C# `public IType? ConstrainedTo` -- the type operand of the
    // 'constrained.' prefix; null when no prefix exists.
    TypeSystem::ITypePtr ConstrainedTo;
    // The C# `public bool ILStackWasEmpty` (CallInstruction.cs line 61):
    // whether the IL evaluation stack was empty at the point of this call,
    // not counting the call's own arguments/return value (evaluated by the
    // reader AFTER popping the arguments -- the C# PrepareArguments-then-
    // CurrentStackIsEmpty order). The statement-level initializer detection
    // (TransformCollectionAndObjectInitializers) uses it to prefer keeping
    // plain local variables on the statement level. False for calls created
    // outside the reader (the C# default).
    bool ILStackWasEmpty = false;

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
