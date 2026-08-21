// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/ITypeReference.cs (the `ITypeResolveContext`
// interface half, lines 51-73). `ITypeResolveContext` is the resolution context a type
// reference resolves itself against (`ITypeReference.Resolve(ITypeResolveContext)`): it
// bundles the parent `ICompilation` (inherited from `ICompilationProvider` D379) with the
// current `IModule` / `ITypeDefinition` / `IMember` slots the resolution may depend on
// (e.g. `KnownTypeReference` resolves via `context.Compilation.FindType(...)`, and
// per-node references resolve relative to the current module / type definition / member).
// The default implementation `SimpleTypeResolveContext` (not yet ported) holds the four
// slots and provides the two `With*` factory methods that return a NEW context with one
// slot replaced (the others carried over).
//
// The C# `interface ITypeResolveContext : ICompilationProvider` ports to a C++ abstract
// base deriving from `ICompilationProvider` (D379) -- SINGLE inheritance (no diamond), so
// no `Name` / `SymbolKind` redeclaration is needed (the D374 single-inheritance "inherited
// virtual covers the `new`" precedent; `ICompilationProvider` does not declare those). The
// inherited `Compilation()` pure-virtual covers the C# `ICompilation Compilation` (no
// redeclaration).
//
// KEY PORT CONVENTIONS:
//  (a) The three C# nullable slots `IModule? CurrentModule` / `ITypeDefinition?
//      CurrentTypeDefinition` / `IMember? CurrentMember` port to nullable raw pointers
//      `const IModule*` / `const ITypeDefinition*` / `const IMember*` (the
//      `IEntity::ParentModule` / `IAttribute::Constructor` nullable-pointer precedent). A
//      pointer return needs only a forward declaration, so `IModule` / `ITypeDefinition` /
//      `IMember` are FORWARD-DECLARED here (not included) -- keeping the include graph
//      minimal (the `INamespace` forward-declared-already-ported-leaf precedent applied to
//      three deps that are each already ported but only needed as pointer return types
//      here). `const T*` is a complete pointer type regardless of `T`'s own completeness.
//  (b) The two C# `With*` factory methods (`ITypeResolveContext WithCurrentTypeDefinition
//      (ITypeDefinition? typeDefinition)` / `WithCurrentMember(IMember? member)`) create a
//      NEW context with one slot replaced, returning it to the caller. The C# returns a
//      reference-type `ITypeResolveContext` (heap-allocated via `new SimpleTypeResolveContext
//      (...)`); the faithful C++ counterpart of that heap allocation transferred to the
//      caller is `std::unique_ptr<ITypeResolveContext>` (transfers ownership; the caller
//      owns the new context). Declaring a member function returning `std::unique_ptr` of
//      the ENCLOSING (incomplete) class is allowed -- the pure-virtual declaration needs no
//      complete type (the concrete override, defined where the type is complete, constructs
//      it). The nullable C# parameter ports to a nullable pointer parameter.
//  (c) The `With*` methods are `const` -- they read `this` (the carried-over slots) without
//      modifying it, producing a new context (the `ITypeReference::Resolve` const-reads-
//      without-modifying precedent, applied to the factory methods that build a new context
//      from the current slots). A `const ITypeResolveContext&` can call them.
//  (d) NO name-hiding qualification is needed: `CurrentModule` / `CurrentTypeDefinition` /
//      `CurrentMember` / `WithCurrentTypeDefinition` / `WithCurrentMember` do not collide
//      with namespace-scope types, and the interface does not redeclare an inherited
//      member (the D375 / D380 collision-free-accessor convention). `Compilation` is
//      inherited from `ICompilationProvider` unchanged.
//
// It is a leaf TypeSystem dependency toward `KnownTypeReference` (the first concrete
// `ITypeReference`, which resolves via `context.Compilation.FindType`), toward
// `SimpleTypeResolveContext` (the default impl, which holds the four slots), and toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`). It is the natural next increment after the D408 `ITypeReference`
// port, which forward-declared `ITypeResolveContext` and provided a dtor-only test stand-in
// that this real header now replaces (the established stand-in-reconciliation step).

#pragma once

#include "Decompiler/TypeSystem/ICompilationProvider.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the three nullable-slot return types. Each is already ported
// (`IModule` D396, `ITypeDefinition` D393, `IMember` D387) but only needed here as a
// pointer return type, so they are forward-declared (not included) to keep the include
// graph minimal (a pointer return needs only a forward declaration, the
// `IEntity::ParentModule` precedent). `const T*` is a complete pointer type regardless of
// `T`'s own completeness, so this header compiles with all three incomplete.
class IModule;
class ITypeDefinition;
class IMember;

// The resolution context a type reference resolves itself against. Bundles the parent
// `ICompilation` (inherited from `ICompilationProvider`) with the current module / type
// definition / member slots. A concrete context (`SimpleTypeResolveContext`, not yet
// ported) subclasses `ITypeResolveContext`, overrides the three nullable accessors and the
// two `With*` factories, and (via the inherited `Compilation()`) returns the parent
// compilation. `ITypeResolveContext` single-inherits `ICompilationProvider` (no diamond),
// so `Compilation()` is inherited unchanged (the D374 precedent) and no redeclaration is
// needed.
class ITypeResolveContext : public ICompilationProvider {
public:
    // The C# `IModule? CurrentModule` -- the current module, or null if this context does
    // not specify any module. A nullable raw pointer (the `IEntity::ParentModule`
    // nullable-pointer precedent).
    virtual const IModule* CurrentModule() const = 0;

    // The C# `ITypeDefinition? CurrentTypeDefinition` -- the current type definition, or
    // null. A nullable raw pointer.
    virtual const ITypeDefinition* CurrentTypeDefinition() const = 0;

    // The C# `IMember? CurrentMember` -- the current member, or null. A nullable raw
    // pointer.
    virtual const IMember* CurrentMember() const = 0;

    // The C# `ITypeResolveContext WithCurrentTypeDefinition(ITypeDefinition?
    // typeDefinition)` -- returns a NEW context with the current-type-definition slot
    // replaced by `typeDefinition` and the other slots (module / member / compilation)
    // carried over. The C# heap allocation (`new SimpleTypeResolveContext(...)`) ports to
    // a `std::unique_ptr<ITypeResolveContext>` transferred to the caller. `const` because
    // the factory reads `this` without modifying it. The nullable C# parameter ports to a
    // nullable pointer (pass `nullptr` to clear the slot).
    virtual std::unique_ptr<ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition* typeDefinition) const = 0;

    // The C# `ITypeResolveContext WithCurrentMember(IMember? member)` -- returns a NEW
    // context with the current-member slot replaced by `member` and the other slots
    // (module / type definition / compilation) carried over. `std::unique_ptr` (the C#
    // heap allocation); `const`; nullable pointer parameter.
    virtual std::unique_ptr<ITypeResolveContext> WithCurrentMember(
        const IMember* member) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
