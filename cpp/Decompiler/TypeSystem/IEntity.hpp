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

// Port of ICSharpCode.Decompiler/TypeSystem/IEntity.cs -- `IEntity` is the root of the
// resolved-entity hierarchy: every resolved type definition, member (method/field/property/
// event), and (transitively) namespace/parameter/type-parameter is an `IEntity`. The C#
// `interface IEntity : ISymbol, ICompilationProvider, INamedElement` ports to a C++ abstract
// base multiply-inheriting those three independent abstract bases (the established
// C#-interface-to-C++-abstract-base convention, the `ISymbol` / `ICompilationProvider` /
// `INamedElement` D372 / D379 / D375 precedents).
//
// It is the central interface of the type system, the next-in-order leaf toward
// `ITypeDefinition` (which `: ITypeDefinitionOrUnknown, IType, IEntity`), `IMember` (which
// `: IEntity`), and through them toward `TypeSystemAstBuilder` (the long-pole remaining
// blocker of `CSharpAmbience`). It lands now that all three of its bases plus its
// `Accessibility` (D373) and `KnownAttribute` (D378) members are ported; the cyclic
// `IEntity.DeclaringTypeDefinition` <-> `ITypeDefinition : IEntity` triangle is resolved
// here with forward declarations and pointer/reference returns (the
// `IAmbience.hpp` / `ICompilationProvider.hpp` precedent), so this header compiles with
// `ITypeDefinition`, `IModule`, and `IAttribute` all incomplete.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `new string Name` redeclares `Name` (hiding the `ISymbol` / `INamedElement`
//      accessors with a re-statement of the same contract). In C++ this DOES have a
//      counterpart, REQUIRED here and NOT covered by the D374 `IVariable` single-inheritance
//      "inherited virtual covers the `new`" precedent: `ISymbol` and `INamedElement` are
//      TWO INDEPENDENT bases each declaring `virtual std::string Name() const = 0` with the
//      same signature, so `IEntity` would otherwise inherit both pure-virtuals and a name
//      lookup through an `IEntity*` (e.g. `entity->Name()`) is AMBIGUOUS (the two base
//      `Name`s are both found, C2385) even though a concrete entity's single override is
//      the final overrider for both. The faithful port therefore REDECLARES `Name()` in
//      `IEntity` -- a pure-virtual override of the same signature that overrides BOTH base
//      `Name`s (the standard C++ way one derived function overrides multiple matching
//      base virtuals), keeping `IEntity` abstract and making `Name` lookup through
//      `IEntity*` unambiguous (only `IEntity::Name` is found). A concrete entity then
//      overrides `Name()` once, and dispatch through `ISymbol*` / `INamedElement*` /
//      `IEntity*` all reach that single override.
//  (b) The C# `System.Reflection.Metadata.EntityHandle MetadataToken` is a typed metadata
//      handle (a discriminated `System.Reflection.Metadata` BCL value struct). The C++
//      metadata layer (the existing `MetadataFile` port) uses RAW `std::uint32_t` tokens
//      throughout (table id in the high byte, 1-based row index in the low three bytes),
//      so the faithful `IEntity.MetadataToken` port mirrors the raw-token convention rather
//      than introducing a typed `EntityHandle` wrapper: `virtual std::uint32_t MetadataToken()
//      const = 0`. This is the documented EntityHandle design decision for the `IEntity`
//      leaf -- the typed-handle ergonomics are deferred to the metadata layer.
//  (c) The C# `IEnumerable<IAttribute> GetAttributes()` (a lazy iterator yielding attribute
//      references) ports to `std::vector<const IAttribute*>` returned by value -- a snapshot
//      of non-owning pointers to the entity's attributes (the AST non-owning model: the
//      entity owns its attributes; the caller holds raw pointers, not ownership). The
//      element type is `const IAttribute*` (a complete pointer type regardless of the
//      pointee's completeness), so `std::vector<const IAttribute*>` instantiates with only
//      `IAttribute` forward-declared.
//  (d) The nullable C# reference-typed slots (`ITypeDefinition? DeclaringTypeDefinition`,
//      `IType? DeclaringType`, `IModule? ParentModule`, `IAttribute? GetAttribute(...)`)
//      port to nullable pointer returns: `const ITypeDefinition*` / `ITypePtr` /
//      `const IModule*` / `const IAttribute*`, where a null pointer is the C# `null`.
//      `DeclaringType` uses `ITypePtr` (`std::shared_ptr<IType>`, the D271 owned-type handle)
//      because an `IType` is a shared, cached, polymorphic object; the others are raw
//      pointers to entities/modules/attributes the caller does not own.

#pragma once

#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/ICompilationProvider.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the not-yet-ported member types `IEntity` references. These
// resolve the cyclic `IEntity` <-> `ITypeDefinition` <-> `IModule` <-> `IAttribute` chain
// for THIS header: a pointer/reference return needs only a forward declaration (the
// `ICompilationProvider.hpp` / `IAmbience.hpp` precedent), so `IEntity` compiles with all
// three incomplete. They land as separate later leaves.
class ITypeDefinition;
class IModule;
class IAttribute;

// The root of the resolved-entity hierarchy. A concrete entity (a `MetadataTypeDefinition`,
// `MetadataMethod`, `MetadataField`, ... -- the implementations land later) subclasses
// `IEntity` and overrides every accessor. `IEntity` multiply-inherits `ISymbol`,
// `ICompilationProvider`, and `INamedElement`; the `Name()` pure-virtual is shared by
// `ISymbol` and `INamedElement` and is overridden ONCE by a concrete entity (the
// `IVariable` "inherited virtual covers the `new`" precedent).
//
// Equality is by identity (the C# interface has no equality contract); concrete entities
// may add their own.
class IEntity : public ISymbol, public ICompilationProvider, public INamedElement {
public:
    // The C# `new string Name` -- redeclared here to disambiguate the multiple-inheritance
    // diamond: both `ISymbol` and `INamedElement` declare `Name()` with the same signature,
    // so a lookup through an `IEntity*` would be ambiguous without this override. This
    // pure-virtual override overrides BOTH base `Name`s (the standard C++ way one derived
    // function overrides multiple matching base virtuals) and keeps `IEntity` abstract; a
    // concrete entity overrides `Name()` once and dispatch through any base pointer reaches
    // it. NOT the D374 single-inheritance "inherited virtual covers the `new`" precedent.
    virtual std::string Name() const = 0;

    // The C# `System.Reflection.Metadata.EntityHandle MetadataToken` -- the metadata token
    // (table id in the high byte, 1-based row index in the low three bytes), or 0 for a
    // generated member. Ported as a raw `std::uint32_t` (the EntityHandle design decision:
    // mirror the `MetadataFile` raw-token convention, not a typed handle).
    virtual std::uint32_t MetadataToken() const = 0;

    // The C# `ITypeDefinition? DeclaringTypeDefinition` -- the declaring class (the outer
    // class for a nested type, the containing class for a member), or null for a top-level
    // entity. A raw nullable pointer (the entity does not own its declaring type).
    virtual const ITypeDefinition* DeclaringTypeDefinition() const = 0;

    // The C# `IType? DeclaringType` -- the declaring type including type arguments (null for
    // a top-level entity; equal to `DeclaringTypeDefinition` for a non-specialized member).
    // A nullable `ITypePtr` (the shared, cached `IType` handle).
    virtual ITypePtr DeclaringType() const = 0;

    // The C# `IModule? ParentModule` -- the module the entity is defined in, or null when
    // the entity was not created from a module. A raw nullable pointer.
    virtual const IModule* ParentModule() const = 0;

    // The C# `IEnumerable<IAttribute> GetAttributes()` -- the attributes declared on this
    // entity (NOT inherited). Ported as a by-value snapshot of non-owning pointers (the AST
    // non-owning model): the entity owns its attributes; the caller holds raw pointers.
    virtual std::vector<const IAttribute*> GetAttributes() const = 0;

    // The C# `bool HasAttribute(KnownAttribute attribute)` -- whether the entity has an
    // attribute of the given known kind (classified by metadata type name, no resolution).
    virtual bool HasAttribute(KnownAttribute attribute) const = 0;

    // The C# `IAttribute? GetAttribute(KnownAttribute attribute)` -- the first attribute of
    // the given known kind, or null. A raw nullable pointer.
    virtual const IAttribute* GetAttribute(KnownAttribute attribute) const = 0;

    // The C# `Accessibility Accessibility` -- the declared accessibility of this entity.
    virtual Accessibility Accessibility() const = 0;

    // The C# `bool IsStatic` -- true if the 'static' or 'const' modifier is set.
    virtual bool IsStatic() const = 0;

    // The C# `bool IsAbstract` -- whether the entity is abstract (static classes count).
    virtual bool IsAbstract() const = 0;

    // The C# `bool IsSealed` -- whether the entity is sealed (static classes count).
    virtual bool IsSealed() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
