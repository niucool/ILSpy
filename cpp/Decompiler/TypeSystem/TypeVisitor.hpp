// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/TypeVisitor.cs -- the base class
// for the visitor pattern on IType. Each Visit* method defaults to
// `type.VisitChildren(this)` (the C# `return type.VisitChildren(this);`), so a
// derived visitor overrides only the node kinds it cares about; the rest recurse
// through the children and return the (possibly reconstructed) type. The first
// concrete derived visitor is TypeParameterSubstitution (the class/method type
// parameter substituter), which overrides VisitTypeParameter to swap in the
// substituted type and inherits the defaults for the structural types.
//
// This lands the TypeVisitor dispatch infrastructure the four concrete IType
// VisitChildren types (ModifiedType D401 / NullabilityAnnotatedType D402 /
// FunctionPointerType D404 / TupleType D405) were ported toward: IType now
// exposes AcceptVisitor / VisitChildren (D406), each concrete IType dispatches
// AcceptVisitor to the matching Visit* method, and the Visit* defaults recurse
// via VisitChildren. TypeParameterSubstitution itself lands next.
//
// KEY PORT CONVENTIONS:
//  (a) The return type is ITypePtr (std::shared_ptr<IType>): the C# IType uses
//      reference semantics and `return this` returns the same object; the C++
//      minimal port owns IType via shared_ptr throughout, and IType derives
//      from std::enable_shared_from_this<IType> so VisitChildren returns
//      shared_from_this() for the no-change case (the C# `return this`
//      reference-identity), and a freshly made shared_ptr for the reconstructed
//      case. The "child changed" test is pointer-identity (`r.get() !=
//      stored.get()`), mirroring the C# `r != typeArguments[i]` reference
//      equality.
//  (b) VisitTypeDefinition / VisitTypeParameter take ITypeDefinition& /
//      ITypeParameter& (the interfaces, forward-declared here) -- their defaults
//      are out-of-line in TypeVisitor.cpp (the inline `type.VisitChildren(this)`
//      needs the type complete for the virtual dispatch), unlike the concrete
//      IType.hpp types whose inline defaults compile with IType complete.
//  (c) The C++-only minimal-port types (KnownType / SimpleType / SpecialType /
//      TypeParameter) and the not-yet-concrete interfaces (ITypeDefinition /
//      ITypeParameter) have no dedicated Visit* method and no children to
//      reconstruct, so they inherit IType's AcceptVisitor default
//      (VisitOtherType) and VisitChildren default (shared_from_this) -- the
//      faithful AbstractType `return visitor.VisitOtherType(this)` /
//      `return this` defaults flattened onto IType.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::TypeSystem {

class ITypeDefinition;
class ITypeParameter;

// Base class for the visitor pattern on IType (faithful port of TypeVisitor.cs).
class TypeVisitor {
public:
    virtual ~TypeVisitor() = default;

    // The Visit* methods default to `type.VisitChildren(this)` (the C# default).
    // A derived visitor overrides the node kinds it cares about.
    virtual ITypePtr VisitTypeDefinition(ITypeDefinition& type);
    virtual ITypePtr VisitTypeParameter(ITypeParameter& type);
    virtual ITypePtr VisitParameterizedType(ParameterizedType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitArrayType(ArrayType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitPointerType(PointerType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitByReferenceType(ByReferenceType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitTupleType(TupleType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitOtherType(IType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitModReq(ModifiedType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitModOpt(ModifiedType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitNullabilityAnnotatedType(NullabilityAnnotatedType& type) { return type.VisitChildren(*this); }
    virtual ITypePtr VisitFunctionPointerType(FunctionPointerType& type) { return type.VisitChildren(*this); }
};

} // namespace ILSpy::Decompiler::TypeSystem
