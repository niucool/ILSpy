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

// Port of ICSharpCode.Decompiler/TypeSystem/IEvent.cs -- `IEvent` represents an event. The C#
// `interface IEvent : IMember` ports to a C++ abstract base deriving from `IMember` (D387) --
// single inheritance, so no multiple-inheritance-diamond redeclaration is needed (unlike
// `IEntity` / `ITypeParameter`). It adds the `CanAdd` / `CanRemove` / `CanInvoke` availability
// flags and the nullable `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` accessor
// back-references.
//
// It is the next-in-order leaf after `IField` (D391) -- the LAST remaining `IMember` derivation
// before the member family is complete. It lands now that ALL its deps are ported: `IMember`
// (D387, the base), `IMethod` (D389, the `AddAccessor` / `RemoveAccessor` / `InvokeAccessor`
// return type). Through `IEvent`, `ITypeDefinition` (which exposes its members: the now-complete
// `IMethod` / `IProperty` / `IEvent` / `IField` family) advances toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// `IEvent` is structurally the `IProperty` (D390) twin, but SIMPLER: `IEvent : IMember` directly
// (events have no parameter list, so they are NOT `IParameterizedMember`s), so there is no
// `Parameters` accessor and no `IsIndexer` flag. Where `IProperty` has the read/write pair
// (`CanGet` / `CanSet` + `Getter` / `Setter`), `IEvent` has the add/remove/invoke triple
// (`CanAdd` / `CanRemove` / `CanInvoke` + `AddAccessor` / `RemoveAccessor` / `InvokeAccessor`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IMethod? AddAccessor` / `IMethod? RemoveAccessor` / `IMethod? InvokeAccessor`
//      port to nullable raw pointers `const IMethod*` (a null pointer is the C# `null`).
//      `IMethod` is FORWARD-DECLARED here (not included): a pointer return to an incomplete type
//      needs only a forward declaration (the `IEntity::ParentModule` / `IAttribute::Constructor`
//      / `IProperty::Getter` nullable-pointer precedent), and keeping the include minimal
//      preserves include-graph isolation (the `IMember.hpp` forward-declared-
//      `TypeParameterSubstitution` precedent). `IMethod.hpp` is already ported (D389); a real
//      consumer that dereferences the pointer includes it.
//  (b) The C# `[MemberNotNullWhen(true, nameof(AddAccessor))]` / `[MemberNotNullWhen(true,
//      nameof(RemoveAccessor))]` / `[MemberNotNullWhen(true, nameof(InvokeAccessor))]` are
//      compile-time nullable-flow-analysis attributes with NO C++ runtime counterpart: they
//      state that when `CanAdd` / `CanRemove` / `CanInvoke` is true, the corresponding accessor
//      is non-null -- a property of the concrete implementation, not enforced by the interface
//      (the C# `[Obsolete]`-compile-time-only D382 / `[MemberNotNullWhen]` D390 precedent). The
//      flags and the nullable pointers are kept verbatim for API fidelity.
//  (c) NO name-hiding qualification is needed: `IEvent` does NOT redeclare `Name` / `SymbolKind`
//      (single inheritance, so the inherited `IMember` -> `IEntity` -> `ISymbol` /
//      `INamedElement` accessors are inherited unchanged -- the D374 single-inheritance
//      "inherited virtual covers the `new`" precedent, here with no `new` at all), and none of
//      `CanAdd` / `CanRemove` / `CanInvoke` / `AddAccessor` / `RemoveAccessor` / `InvokeAccessor`
//      collides with a namespace-scope type in the `TypeSystem` namespace -- the D375
//      `INamedElement` / D380 `Nullability` collision-free-accessor convention applies.

#pragma once

#include "Decompiler/TypeSystem/IMember.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the method interface (the return type of `AddAccessor` /
// `RemoveAccessor` / `InvokeAccessor`). `IMethod` is already ported (D389); it is only
// forward-declared here because a pointer return to `IMethod` needs only a forward declaration
// (the `IProperty::Getter` precedent), keeping the include graph minimal. `IMember` (the base)
// is complete via the `IMember.hpp` include above, which transitively pulls `IEntity` /
// `ISymbol` / `ICompilationProvider` / `INamedElement` / `IType` / `Accessibility` /
// `KnownAttribute` / `IAttribute` and the forward-decls of `ITypeDefinition` / `IModule` /
// `TypeParameterSubstitution` / `TypeVisitor`.
class IMethod;

// An event. A concrete event (a `MetadataEvent` / `SpecializedEvent` -- the implementations land
// later) subclasses `IEvent` and overrides every `IMember` / `IEntity` / ... accessor plus the
// `IEvent`-own accessors added here. `IEvent` single-inherits `IMember` (no diamond), so
// `Name()` / `SymbolKind()` are inherited unchanged (the D374 precedent).
//
// `IEvent` is abstract. The `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` accessors return
// non-owning `const IMethod*` pointers to the forward-declared `IMethod` (the type system owns
// the accessor methods; the caller holds raw pointers, the `IMember::MemberDefinition`
// non-owning-pointer precedent). Unlike `IProperty` (which derives from `IParameterizedMember`
// and so carries a `Parameters` snapshot), `IEvent` derives from `IMember` directly -- events
// have no parameter list.
class IEvent : public IMember {
public:
    // The C# `bool CanAdd` -- whether handlers can be added to this event.
    // `[MemberNotNullWhen(true, nameof(AddAccessor))]` is a compile-time nullable-flow-analysis
    // attribute with NO C++ runtime counterpart (when `CanAdd` is true, `AddAccessor` is
    // non-null).
    virtual bool CanAdd() const = 0;

    // The C# `bool CanRemove` -- whether handlers can be removed from this event.
    // `[MemberNotNullWhen(true, nameof(RemoveAccessor))]` is a compile-time nullable-flow-analysis
    // attribute with NO C++ runtime counterpart (when `CanRemove` is true, `RemoveAccessor` is
    // non-null).
    virtual bool CanRemove() const = 0;

    // The C# `bool CanInvoke` -- whether this event can be invoked.
    // `[MemberNotNullWhen(true, nameof(InvokeAccessor))]` is a compile-time nullable-flow-analysis
    // attribute with NO C++ runtime counterpart (when `CanInvoke` is true, `InvokeAccessor` is
    // non-null).
    virtual bool CanInvoke() const = 0;

    // The C# `IMethod? AddAccessor` -- the `add` accessor method, or null if the event cannot be
    // added to. A raw nullable pointer to the forward-declared `IMethod` (a pointer return to
    // an incomplete type needs only a forward declaration).
    virtual const IMethod* AddAccessor() const = 0;

    // The C# `IMethod? RemoveAccessor` -- the `remove` accessor method, or null if the event
    // cannot be removed from. A raw nullable pointer to the forward-declared `IMethod`.
    virtual const IMethod* RemoveAccessor() const = 0;

    // The C# `IMethod? InvokeAccessor` -- the `raise` / `invoke` accessor method, or null if the
    // event cannot be invoked. A raw nullable pointer to the forward-declared `IMethod`.
    virtual const IMethod* InvokeAccessor() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
