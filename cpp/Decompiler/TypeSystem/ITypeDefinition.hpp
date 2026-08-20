// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/TypeSystem/ITypeDefinition.cs -- `ITypeDefinition` represents a
// class, enum, interface, struct, delegate, record, or VB module (for partial classes, the
// whole class). The C# `interface ITypeDefinition : ITypeDefinitionOrUnknown, IType, IEntity`
// ports to a C++ abstract base multiply-inheriting `ITypeDefinitionOrUnknown` (D377) and
// `IEntity` (D381) -- the established C#-interface-to-C++-abstract-base convention.
//
// It is the next-in-order leaf after `IEvent` (D392) -- the central piece the member family
// (`IMethod` D389 / `IProperty` D390 / `IEvent` D392 / `IField` D391, now complete) builds toward,
// and the direct consumer of `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining
// blocker of `CSharpAmbience`): `TypeSystemAstBuilder` reads `td.Kind` / `td.Name` / `td.Members`
// / `td.TypeParameters` / `td.BaseType` / `td.IsRecord` / ... to render a type declaration. It
// lands now that ALL its deps are ported: `ITypeDefinitionOrUnknown` (D377, the `FullTypeName` +
// `IType` base), `IEntity` (D381, the entity surface), `KnownTypeCode` (D271, the `KnownTypeCode`
// return), `Nullability` (D380, the `NullableContext` return), and the member family (`IMember`
// D387 / `IField` D391 / `IMethod` D389 / `IProperty` D390 / `IEvent` D392, the snapshot element
// types). `ExtensionInfo` is a long-pole dep (a Util-adjacent `class` tied to `MetadataModule` /
// `IMethod` / `ITypeParameter` / `TypeParameterSubstitution`, not yet ported) and is
// FORWARD-DECLARED here; the `ExtensionInfo` accessor returns a nullable raw pointer.
//
// KEY PORT CONVENTIONS:
//  (a) THE REDUNDANT-DIRECT-IType-BASE OMISSION (the key port decision this iteration, a
//      pragmatic deviation from the literal C# base list, the D381 `EntityHandle` raw-`uint32_t`
//      precedent): the C# `ITypeDefinition : ITypeDefinitionOrUnknown, IType, IEntity` lists
//      `IType` EXPLICITLY, but that listing is REDUNDANT -- `ITypeDefinitionOrUnknown : IType`
//      already makes `ITypeTypeDefinition` IS-A `IType`. In C# the redundancy is a no-op
//      (interface flattening unifies the shared `IType` into a single vtable slot). In C++ with
//      non-virtual inheritance (the established convention), listing `IType` directly AND via
//      `ITypeDefinitionOrUnknown` creates a DIRECT-vs-INDIRECT `IType` DIAMOND: two `IType`
//      subobjects, a `C4584` "base-class `IType` is already a base-class of
//      `ITypeDefinitionOrUnknown`" warning, and -- worse -- AMBIGUOUS `IType` conversions
//      (`td` -> `const IType&`, `static_cast<TestTypeDefinition&>(const IType&)`) that would
//      force every consumer (notably `TypeSystemAstBuilder`, which passes type definitions as
//      `const IType&`) to disambiguate with an explicit `static_cast<const
//      ITypeDefinitionOrUnknown&>(td)` upcast. The faithful port therefore OMITS the redundant
//      direct `IType` base: `ITypeDefinition : ITypeDefinitionOrUnknown, IEntity` (the C++
//      counterpart of the C# `: ITypeDefinitionOrUnknown, IType, IEntity`), so `ITypeDefinition`
//      IS-A `IType` UNAMBIGUOUSLY via the single `IType` subobject inside `ITypeDefinitionOrUnknown`,
//      and `td` -> `const IType&` / `IType*` conversions are clean. This changes NOTHING about the
//      observable interface (`ITypeDefinition` IS-A `IType` either way, and `td.Kind` /
//      `td.Name` / ... dispatch identically); it only trims the C#-flattening redundancy that is
//      a no-op in C# but a diamond in non-virtual C++. The virtual-inheritance alternative
//      (`ITypeDefinitionOrUnknown : virtual IType` + `ITypeDefinition : virtual IType`) would
//      unify the subobject while keeping the literal base list, but it is explicitly deferred
//      (the D391 "forward-declare / defer the long-pole" rationale: it would touch
//      `ITypeDefinitionOrUnknown` and every concrete `IType` kind) -- the omission is the
//      cleaner, lower-cost pragmatic choice.
//  (b) THE IType-vs-IEntity NAME/REFLECTIONNAME DIAMOND (the genuine diamond that remains after
//      the omission, the `ITypeParameter` D383 precedent): `ITypeDefinitionOrUnknown : IType`
//      contributes `IType::Name()` / `IType::ReflectionName()`, and `IEntity : ISymbol,
//      ICompilationProvider, INamedElement` contributes `IEntity::Name()` (which overrides
//      `ISymbol::Name()` / `INamedElement::Name()`) and `INamedElement::ReflectionName()`. So
//      `Name` is inherited via TWO independent paths (`IType` and `IEntity`), and `ReflectionName`
//      via TWO independent paths (`IType` and `INamedElement`); both are ambiguous through an
//      `ITypeDefinition*`. The faithful port therefore REDECLARES `Name()` and `ReflectionName()`
//      -- pure-virtual overrides of the same signature that override BOTH base paths, keeping
//      `ITypeDefinition` abstract and making `Name` / `ReflectionName` lookup through an
//      `ITypeDefinition*` unambiguous (the `ITypeParameter` D383 `IType` + `ISymbol` `Name()`
//      diamond precedent, here `IType` + `IEntity`). A concrete type definition overrides
//      `Name()` / `ReflectionName()` once and dispatch through `ITypeDefinition*` /
//      `ITypeDefinitionOrUnknown*` / `IType*` / `IEntity*` / `ISymbol*` / `INamedElement*` all
//      reach it. The REST of the `IType` surface (`Kind` / `TypeParameterCount` /
//      `StructuralEquals` / `Equals`) is inherited UNAMBIGUOUSLY (only via the single `IType`
//      subobject inside `ITypeDefinitionOrUnknown`), so NO redeclaration is needed for them --
//      `td.Kind()` / `td.TypeParameterCount()` / `td.Equals(other)` compile and dispatch
//      naturally, and `td` -> `const IType&` is unambiguous (the ergonomic win of the omission
//      (a)). This is the structural distinction from `IField` D391, whose two bases BOTH derived
//      from a shared `ISymbol` and so redeclared the WHOLE `ISymbol` surface (`Name` +
//      `SymbolKind`): here only `Name` / `ReflectionName` are shared (the `IType` subobject is
//      single), so only those two are redeclared.
//  (c) The C# `new IType? DeclaringType { get; }` redeclares `DeclaringType` to disambiguate
//      `IType.DeclaringType` vs `IEntity.DeclaringType` in C#. The C++ minimal `IType` port (D271)
//      does NOT declare `DeclaringType` (only `Kind` / `Name` / `ReflectionName` /
//      `TypeParameterCount` / `Equals` / `StructuralEquals`), so there is NO `DeclaringType`
//      ambiguity in C++ -- only `IEntity::DeclaringType()` (returning `ITypePtr`) exists. The
//      faithful port therefore does NOT redeclare `DeclaringType`: it inherits
//      `IEntity::DeclaringType()` (the D374 single-inheritance "inherited virtual covers the
//      `new`" precedent, here with no `new` at all because the C# `new` targets a C++-absent
//      member). A concrete type definition overrides `DeclaringType()` once (a top-level type
//      returns an empty `ITypePtr`; a nested type returns its enclosing type).
//  (d) The member-family snapshots (`NestedTypes` / `Members` / `Fields` / `Methods` /
//      `Properties` / `Events`) port the C# `IReadOnlyList` / `IEnumerable` to
//      `std::vector<const T*>` returned BY VALUE -- non-owning snapshots (the
//      `IEntity::GetAttributes` / `IParameterizedMember::Parameters` precedent: the type
//      definition owns its members; the caller holds raw pointers). `NestedTypes` is
//      `std::vector<const ITypeDefinition*>` -- a pointer to the enclosing class
//      `ITypeDefinition` (self-referential, the `IMethod::ReducedFrom` returning `const IMethod*`
//      precedent); a pointer to the enclosing class is valid at the point of the declaration
//      even while the class is incomplete. `IMember` / `IField` / `IMethod` / `IProperty` /
//      `IEvent` are FORWARD-DECLARED here (not included): a pointer element type is complete with
//      the pointee incomplete, so the `std::vector` instantiates (the `IEntity::GetAttributes`
//      forward-declared-`IAttribute` precedent applied to the member family). The real headers
//      are already ported (D387/D391/D389/D390/D392); a real consumer that dereferences the
//      pointers includes them.
//  (e) The C# `KnownTypeCode KnownTypeCode { get; }` is a getter named after its enum return
//      type (the `ISymbol::SymbolKind` D372 precedent): the return type `KnownTypeCode` resolves
//      to the enum (parsed before the declarator), then the method `KnownTypeCode()` is declared.
//      `KnownTypeCode` is NOT inherited from any base, so no name-hiding qualification is needed.
//  (f) The C# `IType? EnumUnderlyingType` (nullable -- null for non-enums) ports to a nullable
//      `ITypePtr` (an empty `shared_ptr` is the C# `null`; the D271 shared, cached `IType` handle).
//  (g) The C# `ExtensionInfo? ExtensionInfo` (nullable -- null for types without extension
//      blocks) ports to `const ExtensionInfo*` (a nullable raw pointer). `ExtensionInfo` is a
//      C# `class` (reference type) FORWARD-DECLARED here (the `IMember::TypeParameterSubstitution`
//      forward-declared-long-pole-dep precedent); a pointer return to an incomplete type needs
//      only a forward declaration. The getter is named after its pointer return type (the
//      `KnownTypeCode KnownTypeCode()` convention (e): the return type's `ExtensionInfo` resolves
//      to the forward-declared class, parsed before the declarator declares the method
//      `ExtensionInfo()`).
//  (h) NO name-hiding qualification is needed beyond the `Name` / `ReflectionName` redeclarations:
//      the ITypeDefinition-own accessors (`NestedTypes` / `Members` / `Fields` / `Methods` /
//      `Properties` / `Events` / `KnownTypeCode` / `EnumUnderlyingType` / `IsReadOnly` /
//      `MetadataName` / `HasExtensions` / `ExtensionInfo` / `NullableContext` / `IsRecord`) do
//      not collide with a namespace-scope type in the `TypeSystem` namespace (the `KnownTypeCode`
//      collision is the getter-named-after-enum case (e), not a shadowing), and `ITypeDefinition`
//      does NOT redeclare `SymbolKind` (it inherits `IEntity`'s / `ISymbol`'s) -- the D375
//      `INamedElement` / D380 `Nullability` collision-free-accessor convention applies. The
//      `IEntity`-owned accessors (`MetadataToken` / `DeclaringTypeDefinition` / `DeclaringType`
//      / `ParentModule` / `GetAttributes` / `HasAttribute` / `GetAttribute` / `Accessibility` /
//      `IsStatic` / `IsAbstract` / `IsSealed`), the `ITypeDefinitionOrUnknown`-own `FullTypeName`,
//      and the `IType`-own `Kind` / `TypeParameterCount` / `Equals` / `StructuralEquals` are
//      inherited UNAMBIGUOUSLY (each name lives on only one base path), so `td->MetadataToken()`
//      / `td->FullTypeName()` / `td->Kind()` / `td->Equals(other)` / ... compile and dispatch
//      without redeclaration.

#pragma once

#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"

#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the member-family types `ITypeDefinition` references (the snapshots'
// element types) and the long-pole `ExtensionInfo`. These resolve the cyclic
// `ITypeDefinition` <-> member chain for THIS header: a pointer element type is complete with
// the pointee incomplete, so `std::vector<const T*>` instantiates with only `T` forward-declared
// (the `IEntity::GetAttributes` forward-declared-`IAttribute` precedent). The member-family
// headers are already ported (D387/D391/D389/D390/D392); a real consumer that dereferences the
// snapshot pointers includes them. `ExtensionInfo` (a Util-adjacent `class` tied to
// `MetadataModule` / `IMethod` / `ITypeParameter` / `TypeParameterSubstitution`, not yet ported)
// is forward-declared so the nullable `ExtensionInfo` accessor returns a pointer (the
// `IMember::TypeParameterSubstitution` forward-declared-long-pole-dep precedent).
class IMember;
class IField;
class IMethod;
class IProperty;
class IEvent;
class ExtensionInfo;

// A class, enum, interface, struct, delegate, record, or VB module. A concrete type definition
// (a `MetadataTypeDefinition` -- the implementation lands later) subclasses `ITypeDefinition`
// and overrides every accessor. `ITypeDefinition` multiply-inherits `ITypeDefinitionOrUnknown`
// (which `: IType`) and `IEntity`; the redundant direct `IType` base of the C# source is OMITTED
// (the header comment crux (a)) so `ITypeDefinition` IS-A `IType` unambiguously via the single
// `IType` subobject inside `ITypeDefinitionOrUnknown`. The genuine `IType`-vs-`IEntity` diamond
// (crux (b)) makes `Name` / `ReflectionName` ambiguous through an `ITypeDefinition*`, so those
// two are redeclared here; the rest of the `IType` surface (`Kind` / `TypeParameterCount` /
// `StructuralEquals` / `Equals`) is inherited unambiguously. A concrete type definition
// overrides `Name()` / `ReflectionName()` once and dispatch through `ITypeDefinition*` /
// `ITypeDefinitionOrUnknown*` / `IType*` / `IEntity*` / `ISymbol*` / `INamedElement*` all reach
// them (every upcast is unambiguous with the redundant direct `IType` base omitted).
//
// `ITypeDefinition` is abstract. The member-family snapshots are non-owning `const T*` pointer
// snapshots (the type definition owns its members; the caller holds raw pointers). The
// `ExtensionInfo` accessor returns a nullable pointer to the forward-declared `ExtensionInfo`.
class ITypeDefinition : public ITypeDefinitionOrUnknown, public IEntity {
public:
    // The C# `string Name` (flattened to one slot in C#) -- redeclared to disambiguate the
    // `IType`-vs-`IEntity` diamond: `IType::Name()` (via `ITypeDefinitionOrUnknown`) and
    // `IEntity::Name()` (which overrides `ISymbol::Name()` / `INamedElement::Name()`) are two
    // independent paths, so a lookup through an `ITypeDefinition*` would be ambiguous without
    // this override. Pure-virtual; a single concrete override is the final overrider for both
    // paths (the `ITypeParameter` D383 `IType` + `ISymbol` `Name()` diamond precedent).
    virtual std::string Name() const = 0;

    // The C# `string ReflectionName` (flattened to one slot in C#) -- redeclared to disambiguate
    // the `IType`-vs-`INamedElement` diamond: `IType::ReflectionName()` (via
    // `ITypeDefinitionOrUnknown`) and `INamedElement::ReflectionName()` (via `IEntity`) are two
    // independent paths, so a lookup through an `ITypeDefinition*` would be ambiguous without
    // this override. Pure-virtual; a single concrete override is the final overrider for both.
    virtual std::string ReflectionName() const = 0;

    // ---- ITypeDefinition-own accessors ----

    // The C# `IReadOnlyList<ITypeDefinition> NestedTypes` -- the nested type definitions declared
    // inside this type. A by-value snapshot of non-owning pointers to the enclosing class
    // `ITypeDefinition` (self-referential; a pointer to the enclosing class is valid here).
    virtual std::vector<const ITypeDefinition*> NestedTypes() const = 0;

    // The C# `IReadOnlyList<IMember> Members` -- every member declared directly on this type
    // (methods, fields, properties, events, nested types, ...). A by-value snapshot of
    // non-owning pointers (the `IEntity::GetAttributes` precedent); `IMember` is forward-declared.
    virtual std::vector<const IMember*> Members() const = 0;

    // The C# `IEnumerable<IField> Fields` -- the fields of this type (a lazy filter over
    // `Members`). A by-value snapshot of non-owning pointers; `IField` is forward-declared.
    virtual std::vector<const IField*> Fields() const = 0;

    // The C# `IEnumerable<IMethod> Methods` -- the methods of this type. A by-value snapshot of
    // non-owning pointers; `IMethod` is forward-declared.
    virtual std::vector<const IMethod*> Methods() const = 0;

    // The C# `IEnumerable<IProperty> Properties` -- the properties of this type. A by-value
    // snapshot of non-owning pointers; `IProperty` is forward-declared.
    virtual std::vector<const IProperty*> Properties() const = 0;

    // The C# `IEnumerable<IEvent> Events` -- the events of this type. A by-value snapshot of
    // non-owning pointers; `IEvent` is forward-declared.
    virtual std::vector<const IEvent*> Events() const = 0;

    // The C# `KnownTypeCode KnownTypeCode` -- the known type code for this type definition
    // (`None` for a non-known type). Returned by value (the `KnownTypeCode` enum, D271). A
    // getter named after its enum return type (the `ISymbol::SymbolKind` precedent).
    virtual KnownTypeCode KnownTypeCode() const = 0;

    // The C# `IType? EnumUnderlyingType` -- for enums, the underlying primitive type; for all
    // other types, null. A nullable `ITypePtr` (an empty `shared_ptr` is the C# `null`).
    virtual ITypePtr EnumUnderlyingType() const = 0;

    // The C# `bool IsReadOnly` -- for structs, whether this is a `readonly struct`; for all
    // other types, false.
    virtual bool IsReadOnly() const = 0;

    // The C# `string MetadataName` -- the short type name as stored in metadata (the short name
    // INCLUDING the generic arity backtick suffix, e.g. "List`1"; distinct from `Name()` which
    // omits the suffix).
    virtual std::string MetadataName() const = 0;

    // The C# `bool HasExtensions` -- whether this type contains extension methods or C# 14
    // extensions (used to speed up the search for extension members).
    virtual bool HasExtensions() const = 0;

    // The C# `ExtensionInfo? ExtensionInfo` -- for types containing extension blocks, a non-null
    // `ExtensionInfo`; for extension blocks, the parent's `ExtensionInfo`; for all other types,
    // null. A nullable raw pointer to the forward-declared `ExtensionInfo` (a pointer return to
    // an incomplete type needs only a forward declaration). The getter is named after its
    // pointer return type (the `KnownTypeCode KnownTypeCode()` convention: the return type's
    // `ExtensionInfo` resolves to the forward-declared class, parsed before the declarator).
    virtual const ExtensionInfo* ExtensionInfo() const = 0;

    // The C# `Nullability NullableContext` -- the nullability specified in the
    // `[NullableContext]` attribute on the type (the default nullability for members of the type
    // without a `[Nullable]` attribute). The ported `Nullability` enum (D380), returned by value.
    virtual Nullability NullableContext() const = 0;

    // The C# `bool IsRecord` -- whether the type has the necessary members to be considered a
    // C# 9 record or C# 10 record struct.
    virtual bool IsRecord() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
