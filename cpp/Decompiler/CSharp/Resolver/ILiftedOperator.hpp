// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.CSharp.Resolver.ILiftedOperator` (CSharpOperators.cs line 1170)
// -- the marker interface the C# resolver uses to tag a lifted operator's method. A lifted
// operator is a synthesized `op_Implicit`/`op_Explicit`/unary/binary operator method that lifts
// a value-type operator to its `Nullable<T>` form; the resolver wraps the underlying method in
// an `ILiftedOperator` so overload resolution's `BetterFunctionMember` tiebreak can prefer a
// non-lifted operator over a lifted one (C# spec draft-v11 section 12.6.4.3 -- "prefer non-lifted
// operators"), and the IL layer's `CallInstruction.IsLifted` can detect it.
//
// The C# `public interface ILiftedOperator : IParameterizedMember` declares two read-only
// properties -- `NonLiftedReturnType` (the underlying method's return type) and
// `NonLiftedParameters` (the underlying method's parameters) -- used by the IL layer to read the
// pre-lifting signature.
//
// PORT DEVIATION (the `: IParameterizedMember` base): the C# interface derives from
// `IParameterizedMember`, but this C++ port makes `ILiftedOperator` a STANDALONE abstract base
// (it does NOT derive from `IParameterizedMember`). The reason is the non-virtual-inheritance
// diamond a faithful `: IParameterizedMember` would create: every concrete implementor (the
// `LiftedUnaryOperatorMethod`/`LiftedBinaryOperatorMethod`/`LiftedEqualityOperatorMethod`/
// `LiftedUserDefinedOperator` classes in CSharpOperators.cs) derives from a `*OperatorMethod`
// base (an `IMethod`, hence an `IParameterizedMember`) AND implements `ILiftedOperator`. In C++
// that is `class LiftedX : public UnaryOperatorMethod, public ILiftedOperator` where BOTH bases
// (transitively) derive from `IParameterizedMember` -- a non-virtual-inheritance diamond. With
// the port's non-virtual-multiple-inheritance convention (the ITypeParameter/IEntity
// redeclare-the-shared-virtual precedent), that diamond makes the `LiftedX*` -> `IParameterizedMember*`
// conversion AMBIGUOUS (two subobjects), forcing an explicit disambiguating cast at every
// construction site (the `OverloadResolutionCandidate` ctor takes a `const IParameterizedMember*`).
// Making `ILiftedOperator` standalone removes the diamond entirely: `LiftedX*` -> `IParameterizedMember*`
// resolves unambiguously through the `UnaryOperatorMethod` base, and a `dynamic_cast` from an
// `IParameterizedMember*` to an `ILiftedOperator*` (the C# `member as ILiftedOperator` /
// `member is ILiftedOperator` checks the resolver and IL layer perform) is a clean cross-cast
// that finds the single `ILiftedOperator` base of the most-derived concrete type.
//
// The deviation is behavior-faithful: the C# contract "an `ILiftedOperator` IS-A
// `IParameterizedMember`" is satisfied structurally by every concrete implementor (each derives
// from an `IMethod` subclass), and no current consumer uses an `ILiftedOperator*` AS a
// `IParameterizedMember*` directly -- every consumer (`CSharpResolver`, `OverloadResolution.
// BetterFunctionMember`, `CallInstruction`) only type-checks (`is`/`as` from `IParameterizedMember*`)
// or reads the two `NonLifted*` properties, never upcasts an `ILiftedOperator*` to an
// `IParameterizedMember*`. Should a future consumer need that upcast, it must go through the
// concrete implementor's `IMethod` base (a `dynamic_cast` to `IMethod*`), not through
// `ILiftedOperator` itself.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IType NonLiftedReturnType` ports to `virtual const IType& NonLiftedReturnType()
//      const = 0` (a non-null reference return, the IVariable::Type() D374 convention). `IType`
//      is only forward-declared here (the pure-virtual declaration needs only a declaration,
//      not the full definition; a concrete implementor includes IType.hpp).
//  (b) The C# `IReadOnlyList<IParameter> NonLiftedParameters` ports to
//      `virtual std::vector<const IParameter*> NonLiftedParameters() const = 0` (a by-value
//      snapshot of non-owning pointers, the IParameterizedMember::Parameters / IEntity::
//      GetAttributes convention). `IParameter` is only forward-declared here (a
//      `std::vector<const IParameter*>` element type is a complete pointer type with `IParameter`
//      incomplete, the IParameterizedMember forward-declared-IParameter precedent).
//  (c) NO name-hiding qualification is needed: `NonLiftedReturnType` / `NonLiftedParameters` do
//      not collide with any namespace-scope type, and the standalone base adds only these two
//      accessors (no inherited virtuals to redeclare, no diamond to disambiguate).
//  (d) The interface is abstract (two pure-virtuals); a concrete implementor overrides both. It
//      is NOT `final` (the C# is an interface, implemented by several classes; the port leaves it
//      open for the eventual CSharpOperators port's concrete `Lifted*OperatorMethod` classes).

#pragma once

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
// Forward-declared (the pure-virtual return types need only a declaration, not the full
// definition; a concrete implementor includes the full headers).
class IType;
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::Resolver {

// The marker interface tagging a lifted operator's method (CSharpOperators.cs line 1170). See
// the file header for the standalone-base deviation from the C# `: IParameterizedMember`.
class ILiftedOperator {
public:
    virtual ~ILiftedOperator() = default;

    // The C# `IType NonLiftedReturnType` -- the underlying (non-lifted) method's return type.
    // A non-null reference (the IVariable::Type() D374 convention). The IL layer reads this to
    // compute the lifted call's stack type (`NonLiftedReturnType.GetStackType()`).
    virtual const ILSpy::Decompiler::TypeSystem::IType& NonLiftedReturnType() const = 0;

    // The C# `IReadOnlyList<IParameter> NonLiftedParameters` -- the underlying (non-lifted)
    // method's parameter list. A by-value snapshot of non-owning pointers (the
    // IParameterizedMember::Parameters convention).
    virtual std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
    NonLiftedParameters() const = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
