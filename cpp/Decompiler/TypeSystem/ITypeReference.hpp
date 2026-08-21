// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including limitation, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit to whom the Software is furnished to do so,
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

// Port of ICSharpCode.Decompiler/TypeSystem/ITypeReference.cs (the `ITypeReference`
// interface half, lines 27-49). `ITypeReference` is the 1-method interface that a type
// reference -- `KnownTypeReference`, `ArrayTypeReference`, `ByReferenceTypeReference`,
// `ParameterizedTypeReference`, the per-node `ITypeReference` impls ... -- implements to
// resolve itself to an `IType` against an `ITypeResolveContext`. It is the base of
// `KnownTypeReference` (which `TypeSystemAstBuilder` reaches via
// `KnownTypeReference.Get` / `GetCSharpNameByTypeCode` to look up the metadata name of a
// known type), and the reference type the `IType`-producing paths thread before the
// final `Resolve` step.
//
// The C# `interface ITypeReference { IType Resolve(ITypeResolveContext context); }`
// ports to a C++ abstract base with a virtual destructor and one pure-virtual accessor.
// The C# `IType Resolve(ITypeResolveContext)` -- non-null per the C# doc comment
// "Never returns null" -- ports to `const IType& Resolve(const ITypeResolveContext&)
// const`: a non-null reference return (the `ICompilation::FindType` non-null-reference
// precedent), `const` because `Resolve` reads the context without modifying `this`.
// `IType` (the D271 minimal port) and `ITypeResolveContext` (the paired context
// interface, not yet ported) are both FORWARD-DECLARED here -- a reference return and a
// reference parameter each need only a forward declaration (the `IAmbience.hpp`
// `ISymbol` / `IType` reference-parameter precedent, here applied to BOTH a reference
// return and a reference parameter) -- so this header compiles with both incomplete and
// includes NOTHING (the `ICompilationProvider.hpp`-is-include-free precedent extended
// to a 1-method interface whose only dependencies are two forward-declared TypeSystem
// interfaces).
//
// `ITypeResolveContext` itself is the natural next leaf: it extends `ICompilationProvider`
// (D379) with the current-module / current-type-definition / current-member slots and
// two `With*` factory methods, and `KnownTypeReference` (the first concrete
// `ITypeReference`) resolves via `context.Compilation.FindType(knownTypeCode)` -- so
// landing `ITypeResolveContext` next lets `KnownTypeReference` follow. The test for this
// leaf provides a minimal dtor-only stand-in `ITypeResolveContext` (the D379
// forward-declared-dep + test-stand-in precedent: the test's `TestTypeReference::Resolve`
// ignores the context and returns its configured `KnownType` member, so the stand-in
// needs only a virtual destructor); the stand-in is dropped when `ITypeResolveContext.hpp`
// lands (the established stand-in-reconciliation step).
//
// It is a leaf TypeSystem dependency toward `KnownTypeReference` (which `:
// ITypeReference`), toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`), and the paired `ITypeResolveContext`.

#pragma once

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType` (the D271 minimal port, the `Resolve` return type) and
// `ITypeResolveContext` (the paired context interface, the `Resolve` parameter type --
// not yet ported). A reference return and a reference parameter each need only a forward
// declaration (the `IAmbience.hpp` `ISymbol` / `IType` precedent), so this header
// compiles with both incomplete and includes NOTHING (the
// `ICompilationProvider.hpp`-is-include-free precedent extended to a 1-method interface
// whose only dependencies are two forward-declared TypeSystem interfaces).
class IType;
class ITypeResolveContext;

// A reference to a type. Must be resolved (via `Resolve`) against an
// `ITypeResolveContext` before it can be used as an `IType`. A concrete type reference
// (`KnownTypeReference`, not yet ported) subclasses `ITypeReference` and overrides
// `Resolve`, returning the resolved `IType` (never null -- the C# doc comment
// "Never returns null. In case of an error, returns an unknown type
// (`TypeKind.Unknown`)").
class ITypeReference {
public:
    virtual ~ITypeReference() = default;

    // The C# `IType Resolve(ITypeResolveContext context)` -- resolves this type
    // reference against `context`, returning the resolved `IType`. Never null (the C#
    // doc comment; an unresolvable reference returns an `UnknownType`, not null), so a
    // non-null reference return (the `ICompilation::FindType` non-null-reference
    // precedent). `const` because `Resolve` reads the context without modifying `this`.
    virtual const IType& Resolve(const ITypeResolveContext& context) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
