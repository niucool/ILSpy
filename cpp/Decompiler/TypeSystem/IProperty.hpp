// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IProperty.cs -- `IProperty` represents a property or
// indexer. The C# `interface IProperty : IParameterizedMember` ports to a C++ abstract base
// deriving from `IParameterizedMember` (D388) -- single inheritance, so no
// multiple-inheritance-diamond redeclaration is needed (unlike `IEntity` / `ITypeParameter`).
// It adds the `CanGet` / `CanSet` availability flags, the nullable `Getter` / `Setter` accessor
// back-references, the `IsIndexer` flag, and the `ReturnTypeIsRefReadOnly` flag.
//
// It is the next-in-order leaf after `IMethod` (D389). It lands now that ALL its deps are
// ported: `IParameterizedMember` (D388, the `Parameters` accessor this builds on), `IMethod`
// (D389, the `Getter` / `Setter` return type). Through `IProperty`, `IEvent` / `IField`
// (`: IMember` + `IMethod` / `IVariable` D374) and `ITypeDefinition` (which exposes its
// members) advance toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining
// blocker of `CSharpAmbience`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IMethod? Getter` / `IMethod? Setter` port to nullable raw pointers
//      `const IMethod*` (a null pointer is the C# `null`). `IMethod` is forward-declared here
//      (not included): a pointer return to an incomplete type needs only a forward declaration
//      (the `IEntity::ParentModule` / `IAttribute::Constructor` nullable-pointer precedent),
//      and keeping the include minimal preserves include-graph isolation (the `IMember.hpp`
//      forward-declared-`TypeParameterSubstitution` precedent). `IMethod.hpp` is already
//      ported (D389); a real consumer that dereferences the pointer includes it.
//  (b) The C# `[MemberNotNullWhen(true, nameof(Getter))]` / `[MemberNotNullWhen(true,
//      nameof(Setter))]` are compile-time nullable-flow-analysis attributes with NO C++
//      runtime counterpart: they state that when `CanGet` / `CanSet` is true, `Getter` /
//      `Setter` is non-null -- a property of the concrete implementation, not enforced by the
//      interface (the C# `[Obsolete]`-compile-time-only D382 precedent). The flags and the
//      nullable pointers are kept verbatim for API fidelity.
//  (c) NO name-hiding qualification is needed: `IProperty` does NOT redeclare `Name` /
//      `SymbolKind` (single inheritance, so the inherited `IParameterizedMember` -> `IMember`
//      -> `IEntity` -> `ISymbol` / `INamedElement` accessors are inherited unchanged -- the
//      D374 single-inheritance "inherited virtual covers the `new`" precedent, here with no
//      `new` at all), and none of `CanGet` / `CanSet` / `Getter` / `Setter` / `IsIndexer` /
//      `ReturnTypeIsRefReadOnly` collides with a namespace-scope type in the `TypeSystem`
//      namespace -- the D375 `INamedElement` / D380 `Nullability` collision-free-accessor
//      convention applies. (`ReturnTypeIsRefReadOnly` IS a member of `IMethod` D389 too, but
//      `IProperty` does NOT derive from `IMethod` -- they are SEPARATE interface hierarchies
//      sharing only the `IParameterizedMember` / `IMember` / `IEntity` bases -- so the two
//      `ReturnTypeIsRefReadOnly()` virtuals do NOT clash; a concrete property / method overrides
//      its OWN interface's `ReturnTypeIsRefReadOnly()` only.)

#pragma once

#include "Decompiler/TypeSystem/IParameterizedMember.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the method interface (the return type of `Getter` / `Setter`).
// `IMethod` is already ported (D389); it is only forward-declared here because a pointer return
// to `IMethod` needs only a forward declaration (the `IAttribute::Constructor` precedent),
// keeping the include graph minimal. `IParameterizedMember` (the base) is complete via the
// `IParameterizedMember.hpp` include above, which transitively pulls `IMember` / `IEntity` /
// `ISymbol` / `ICompilationProvider` / `INamedElement` / `IType` / `Accessibility` /
// `KnownAttribute` / `IAttribute` and the forward-decls of `ITypeDefinition` / `IModule` /
// `TypeParameterSubstitution` / `TypeVisitor`.
class IMethod;

// A property or indexer. A concrete property (a `MetadataProperty` / `SpecializedProperty` --
// the implementations land later) subclasses `IProperty` and overrides every
// `IParameterizedMember` / `IMember` / `IEntity` / ... accessor plus the `IProperty`-own
// accessors added here. `IProperty` single-inherits `IParameterizedMember` (no diamond), so
// `Name()` / `SymbolKind()` / `Parameters()` are inherited unchanged (the D374 precedent).
//
// `IProperty` is abstract. The `Getter` / `Setter` accessors return non-owning `const IMethod*`
// pointers to the forward-declared `IMethod` (the type system owns the accessor methods; the
// caller holds raw pointers, the `IMember::MemberDefinition` non-owning-pointer precedent).
class IProperty : public IParameterizedMember {
public:
    // The C# `bool CanGet` -- whether this property is readable. `[MemberNotNullWhen(true,
    // nameof(Getter))]` is a compile-time nullable-flow-analysis attribute with NO C++ runtime
    // counterpart (a property of the concrete implementation: when `CanGet` is true, `Getter` is
    // non-null).
    virtual bool CanGet() const = 0;

    // The C# `bool CanSet` -- whether this property is writable. `[MemberNotNullWhen(true,
    // nameof(Setter))]` is a compile-time nullable-flow-analysis attribute with NO C++ runtime
    // counterpart (when `CanSet` is true, `Setter` is non-null).
    virtual bool CanSet() const = 0;

    // The C# `IMethod? Getter` -- the getter accessor method, or null if the property is
    // write-only. A raw nullable pointer to the forward-declared `IMethod` (a pointer return to
    // an incomplete type needs only a forward declaration).
    virtual const IMethod* Getter() const = 0;

    // The C# `IMethod? Setter` -- the setter accessor method, or null if the property is
    // read-only. A raw nullable pointer to the forward-declared `IMethod`.
    virtual const IMethod* Setter() const = 0;

    // The C# `bool IsIndexer` -- whether this property is an indexer (it has parameters, so it
    // is accessed with `this[...]` rather than a bare name).
    virtual bool IsIndexer() const = 0;

    // The C# `bool ReturnTypeIsRefReadOnly` -- whether the return type is 'ref readonly'.
    // (Distinct from `IMethod::ReturnTypeIsRefReadOnly` -- `IProperty` does not derive from
    // `IMethod`; the two are separate virtuals in separate interface hierarchies.)
    virtual bool ReturnTypeIsRefReadOnly() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
