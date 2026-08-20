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

// Port of ICSharpCode.Decompiler/TypeSystem/ITypeParameter.cs -- `ITypeParameter` is the
// interface for a generic type parameter (the `T` in `class Foo<T>` or `void M<T>()`). The
// C# `interface ITypeParameter : IType, ISymbol` ports to a C++ abstract base
// multiply-inheriting those two independent abstract bases (the established
// C#-interface-to-C++-abstract-base convention, the `IType` D271 / `ISymbol` D372
// precedents). It is the next self-contained leaf toward the member family (`IMethod` /
// `IProperty` are `IParameterizedMember`s whose `Parameters` hold `IParameter`s D382) and
// toward `TypeSystemAstBuilder` (the long-pole remaining blocker of `CSharpAmbience`):
// `TypeSystemAstBuilder` reads `tp.Variance` / `tp.Name` / `tp.TypeConstraints` /
// `tp.HasReferenceTypeConstraint` / ... to render type-parameter declarations and
// `where`-clauses (lines 2604-2664).
//
// It lands now that all its dependencies are ported: `IType` (D271), `ISymbol` (D372),
// `IEntity` (D381 -- the `Owner` return type), `VarianceModifier` (the variance enum,
// already ported), `Nullability` (D380 -- `NullabilityConstraint`), `SymbolKind` (D372 --
// `OwnerType`), and the `TypeConstraint` struct (ported alongside this leaf). The
// not-yet-ported `IAttribute` is only forward-declared (the `GetAttributes` return type is
// `std::vector<const IAttribute*>`, a complete pointer type regardless of the pointee's
// completeness, the `IEntity::GetAttributes` / `IParameter::GetAttributes` precedent).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `new string Name` REDECLARES `Name` (hiding the `IType.Name` and
//      `ISymbol.Name` accessors with a re-statement of the same contract). In C++ this
//      DOES have a counterpart, REQUIRED here: `IType` and `ISymbol` are TWO INDEPENDENT
//      bases each declaring `virtual std::string Name() const = 0` with the same
//      signature, so `ITypeParameter` would otherwise inherit both pure-virtuals and a
//      name lookup through an `ITypeParameter*` (e.g. `tp->Name()`) is AMBIGUOUS (the two
//      base `Name`s are both found, C2385) even though a concrete type parameter's single
//      override IS the final overrider for both. The faithful port therefore REDECLARES
//      `Name()` in `ITypeParameter` -- a pure-virtual override of the same signature that
//      overrides BOTH base `Name`s (the standard C++ way one derived function overrides
//      multiple matching base virtuals), keeping `ITypeParameter` abstract and making
//      `Name` lookup through `ITypeParameter*` unambiguous. This is the D381 `IEntity`
//      multiple-inheritance-diamond `Name()` crux precedent applied to `IType` + `ISymbol`
//      (NOT the D374 `IVariable` single-inheritance "inherited virtual covers the `new`"
//      precedent -- here there are TWO independent bases each declaring `Name`).
//  (b) The C# `SymbolKind OwnerType { get; }` returns a `SymbolKind` (the owner's kind:
//      `SymbolKind.TypeDefinition` for a class type parameter, `SymbolKind.Method` for a
//      method type parameter). The return type is GLOBALLY QUALIFIED in the port because
//      the inherited `ISymbol::SymbolKind()` member function hides the namespace-scope
//      `SymbolKind` enum in this derived class (the D372 cross-scope name-hiding crux), so
//      an unqualified `SymbolKind` in the return type would resolve to the inherited
//      function (a non-type), not the enum.
//  (c) The C# `IEntity? Owner { get; }` (nullable -- null for the dummy type parameters
//      used by `NormalizeTypeVisitor.ReplaceMethodTypeParametersWithDummy`) ports to
//      `const IEntity*` (a nullable raw pointer; a null pointer is the C# `null`). `IEntity`
//      is already ported (D381).
//  (d) The C# `IType EffectiveBaseClass { get; }` and `IReadOnlyCollection<IType>
//      EffectiveInterfaceSet { get; }` port to `ITypePtr` and `std::vector<ITypePtr>`
//      respectively (the D271 shared, cached `IType` handle for a single effective base
//      and a collection of effective interfaces). These are computed by the type system;
//      the concrete implementation holds them as shared `IType`s.
//  (e) The C# `IEnumerable<IAttribute> GetAttributes()` ports to
//      `std::vector<const IAttribute*>` (a non-owning snapshot, the
//      `IEntity::GetAttributes` / `IParameter::GetAttributes` precedent), instantiating with
//      only `IAttribute` forward-declared.
//  (f) The C# `IReadOnlyList<TypeConstraint> TypeConstraints { get; }` ports to
//      `std::vector<TypeConstraint>` (by value -- `TypeConstraint` is a value struct, so the
//      snapshot owns its constraint values directly; the `TypeConstraint.Type()` / `.Attributes()`
//      accessors hold their `IType` / `IAttribute` data by shared/raw pointer respectively).

#pragma once

#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the not-yet-ported `IAttribute` (the attribute interface). The
// `GetAttributes` return type is `std::vector<const IAttribute*>` (a complete pointer type
// regardless of the pointee's completeness), so `IAttribute` needs only a forward
// declaration here (the `IEntity::GetAttributes` / `IParameter::GetAttributes` precedent).
class IAttribute;

// A generic type parameter (the `T` in `class Foo<T>` or `void M<T>()`). `ITypeParameter :
// IType, ISymbol` adds the owner kind / owner / index, the variance, the effective base
// class / interface set, the constraint flags (`new()` / `class` / `struct` / `unmanaged` /
// `allows ref struct`), the nullability constraint, the attributes, and the type
// constraints. The concrete `MetadataTypeParameter` / `DefaultTypeParameter` /
// `SpecializedTypeParameter` implementations land with the rest of the Phase-5 type system.
//
// `Name()` is redeclared here to disambiguate the `IType` + `ISymbol` multiple-inheritance
// diamond (the D381 `IEntity` precedent); a concrete type parameter overrides `Name()` once
// and dispatch through `IType*` / `ISymbol*` / `ITypeParameter*` all reach it.
class ITypeParameter : public IType, public ISymbol {
public:
    // The C# `new string Name` -- redeclared here to disambiguate the multiple-inheritance
    // diamond: both `IType` and `ISymbol` declare `Name()` with the same signature, so a
    // lookup through an `ITypeParameter*` would be ambiguous without this override. This
    // pure-virtual override overrides BOTH base `Name`s and keeps `ITypeParameter` abstract;
    // a concrete type parameter overrides `Name()` once and dispatch through any base
    // pointer reaches it. The D381 `IEntity` multiple-inheritance-diamond precedent.
    virtual std::string Name() const = 0;

    // The C# `SymbolKind OwnerType { get; }` -- the kind of the owner (`TypeDefinition` for a
    // class type parameter, `Method` for a method type parameter). The return type is
    // globally qualified because the inherited `ISymbol::SymbolKind()` hides the
    // namespace-scope `SymbolKind` enum in this derived class (the D372 crux).
    virtual ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const = 0;

    // The C# `IEntity? Owner { get; }` -- the owning method/class, or nullptr for the dummy
    // type parameters used by `NormalizeTypeVisitor.ReplaceMethodTypeParametersWithDummy`.
    // A nullable raw pointer (the type parameter does not own its owner); `IEntity` is
    // ported (D381).
    virtual const IEntity* Owner() const = 0;

    // The C# `int Index { get; }` -- the index of this type parameter in the owner's type
    // parameter list.
    virtual int Index() const = 0;

    // The C# `IEnumerable<IAttribute> GetAttributes()` -- the attributes declared on this
    // type parameter (a non-owning snapshot; the type parameter owns its attributes, the
    // caller holds raw pointers). The `IEntity::GetAttributes` / `IParameter::GetAttributes`
    // precedent.
    virtual std::vector<const IAttribute*> GetAttributes() const = 0;

    // The C# `VarianceModifier Variance { get; }` -- the variance (`Invariant` /
    // `Covariant` / `Contravariant`), the ported `VarianceModifier` enum.
    virtual VarianceModifier Variance() const = 0;

    // The C# `IType EffectiveBaseClass { get; }` -- the effective base class of this type
    // parameter. A nullable `ITypePtr` (the shared, cached `IType` handle); the concrete
    // implementation computes and caches it.
    virtual ITypePtr EffectiveBaseClass() const = 0;

    // The C# `IReadOnlyCollection<IType> EffectiveInterfaceSet { get; }` -- the effective
    // interface set of this type parameter. A vector of shared `ITypePtr`s.
    virtual std::vector<ITypePtr> EffectiveInterfaceSet() const = 0;

    // The C# `bool HasDefaultConstructorConstraint { get; }` -- whether the type parameter
    // has the `new()` constraint.
    virtual bool HasDefaultConstructorConstraint() const = 0;

    // The C# `bool HasReferenceTypeConstraint { get; }` -- whether the type parameter has
    // the `class` constraint.
    virtual bool HasReferenceTypeConstraint() const = 0;

    // The C# `bool HasValueTypeConstraint { get; }` -- whether the type parameter has the
    // `struct` or `unmanaged` constraint.
    virtual bool HasValueTypeConstraint() const = 0;

    // The C# `bool HasUnmanagedConstraint { get; }` -- whether the type parameter has the
    // `unmanaged` constraint.
    virtual bool HasUnmanagedConstraint() const = 0;

    // The C# `bool AllowsRefLikeType { get; }` -- whether the `allows ref struct` constraint
    // is specified (C# 11).
    virtual bool AllowsRefLikeType() const = 0;

    // The C# `Nullability NullabilityConstraint { get; }` -- the nullability of the
    // reference type constraint (e.g. `where T : class?`). The ported `Nullability` enum
    // (D380), returned by value.
    virtual Nullability NullabilityConstraint() const = 0;

    // The C# `IReadOnlyList<TypeConstraint> TypeConstraints { get; }` -- the type
    // constraints on this type parameter (the `Base` / `IInterface` in
    // `where T : Base, IInterface`). A by-value vector of `TypeConstraint` value structs.
    virtual std::vector<TypeConstraint> TypeConstraints() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
