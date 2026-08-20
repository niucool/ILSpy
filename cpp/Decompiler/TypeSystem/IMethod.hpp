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

// Port of ICSharpCode.Decompiler/TypeSystem/IMethod.cs -- `IMethod` represents a method,
// constructor, destructor, or operator. The C# `interface IMethod : IParameterizedMember`
// ports to a C++ abstract base deriving from `IParameterizedMember` (D388) -- single
// inheritance, so no multiple-inheritance-diamond redeclaration is needed (unlike `IEntity` /
// `ITypeParameter`). It adds the return-type attributes, the ref-readonly / init-only /
// readonly-struct flags, the method's own type parameters and type arguments, the
// extension-method / local-function / constructor / destructor / operator / has-body flags,
// the accessor triple (`IsAccessor` / `AccessorOwner` / `AccessorKind`), the
// reduced-from pointer, and a covariant `Specialize` override.
//
// It is the next-in-order leaf after `IParameterizedMember` (D388). It lands now that ALL its
// deps are ported: `IParameterizedMember` (D388, the `Parameters` accessor this builds on),
// `ITypeParameter` (D383, the `TypeParameters` element type), `IAttribute` (D386, the
// `GetReturnTypeAttributes` element type), `MethodSemanticsAttributes` (D384, the
// `AccessorKind` return type). Through `IMethod`, `IProperty` (`: IParameterizedMember` +
// `IMethod`), `IEvent` / `IField` (`: IMember` + `IMethod` / `IVariable` D374), and
// `ITypeDefinition` (which exposes its members) advance toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IEnumerable<IAttribute> GetReturnTypeAttributes()` ports to
//      `std::vector<const IAttribute*>` returned BY VALUE -- a snapshot of non-owning
//      pointers (the `IEntity::GetAttributes` / `IParameterizedMember::Parameters`
//      precedent: the method owns its return-type attributes; the caller holds raw
//      pointers). `IAttribute` is forward-declared transitively (via `IEntity.hpp` pulled
//      by the `IMember` -> `IParameterizedMember` chain), so no new include is needed; the
//      `std::vector<const IAttribute*>` element type is a complete pointer type with
//      `IAttribute` incomplete.
//  (b) The C# `IReadOnlyList<ITypeParameter> TypeParameters` ports to
//      `std::vector<const ITypeParameter*>` returned BY VALUE -- a snapshot of non-owning
//      pointers (the `GetAttributes` / `Parameters` precedent). `ITypeParameter` is
//      forward-declared here (not included): the `std::vector<const ITypeParameter*>`
//      element type is a complete pointer type with `ITypeParameter` incomplete (the
//      `IEntity::GetAttributes` forward-declared-`IAttribute` precedent).
//      `ITypeParameter.hpp` is already ported (D383); a real consumer that dereferences the
//      pointers includes it.
//  (c) The C# `IReadOnlyList<IType> TypeArguments` ports to `std::vector<ITypePtr>`
//      returned BY VALUE -- a snapshot of the shared, cached `IType` handles (the D271
//      `ITypePtr` convention; `IType` is complete via the `IMember` -> `IEntity` -> `IType`
//      chain, so the snapshot owns shared handles to the type-argument types).
//  (d) The nullable C# reference-typed slots (`IMember? AccessorOwner`, `IMethod?
//      ReducedFrom`) port to nullable raw pointers: `const IMember*` / `const IMethod*`
//      (a null pointer is the C# `null`). `IMember` is complete (included via the
//      `IParameterizedMember` -> `IMember` chain); `IMethod` is the enclosing class (a
//      member-function declaration may return a pointer to the enclosing class even while
//      it is incomplete, the standard self-referential-member idiom).
//  (e) The C# `new IMethod Specialize(TypeParameterSubstitution substitution)` is a
//      covariant-return redeclaration of the inherited `IMember Specialize(...)`. In C# a
//      concrete `IMethod` (e.g. `MetadataMethod`) implements BOTH vtable slots -- the
//      `IMethod.Specialize` slot (public, returns `IMethod`) and the `IMember.Specialize`
//      slot (explicit-interface, returns `IMember`) -- both returning the same specialized
//      method object. The faithful C++ mapping is a SINGLE virtual override with a
//      COVARIANT return type: `virtual const IMethod* Specialize(const
//      TypeParameterSubstitution* substitution) const override`. Dispatch through an
//      `IMethod*` returns `const IMethod*`; dispatch through an `IMember*` reaches the SAME
//      override and the returned `const IMethod*` implicitly converts to `const IMember*`
//      (the derived-to-base pointer conversion) -- the observable behavior of the C# two
//      slots, realized as one C++ slot. The covariant return is legal because `IMethod`
//      derives from `IMember` and `IMethod` is the enclosing class (the standard allows a
//      covariant return that is a pointer to the enclosing class even while the class is
//      incomplete at the point of the override declaration). The parameter is a pointer
//      (the D387 `IMember` pointer-for-the-`TypeParameterSubstitution`-long-pole deviation,
//      for forward-declaration + testability); the return is a non-owning `const IMethod*`
//      (the type system owns the newly-specialized method, the `MemberDefinition` /
//      `IMember::Specialize` precedent). This is the FIRST ported interface member that
//      uses a covariant return type to mirror a C# `new`-with-covariant-return.
//  (f) NO name-hiding qualification is needed: `IMethod` does NOT redeclare `Name` /
//      `SymbolKind` (single inheritance, so the inherited `IParameterizedMember` -> `IMember`
//      -> `IEntity` -> `ISymbol` / `INamedElement` accessors are inherited unchanged -- the
//      D374 single-inheritance "inherited virtual covers the `new`" precedent, here with no
//      `new` at all), and none of `GetReturnTypeAttributes` / `ReturnTypeIsRefReadOnly` /
//      `IsInitOnly` / `ThisIsRefReadOnly` / `TypeParameters` / `TypeArguments` /
//      `IsExtensionMethod` / `IsLocalFunction` / `IsConstructor` / `IsDestructor` /
//      `IsOperator` / `HasBody` / `IsAccessor` / `AccessorOwner` / `AccessorKind` /
//      `ReducedFrom` collides with a namespace-scope type in the `TypeSystem` namespace -- the
//      D375 `INamedElement` / D380 `Nullability` collision-free-accessor convention applies.
//      `Specialize` is an override (covariant) of the inherited `IMember::Specialize`, not a
//      fresh name, so it introduces no shadowing.

#pragma once

#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the type-parameter interface (the element type of `TypeParameters`).
// `ITypeParameter` is already ported (D383); it is only forward-declared here because a
// `std::vector<const ITypeParameter*>` element type is a complete pointer type with
// `ITypeParameter` incomplete (the `IEntity::GetAttributes` forward-declared-`IAttribute`
// precedent), keeping the include graph minimal. `IAttribute` (the `GetReturnTypeAttributes`
// element type) is forward-declared transitively via the `IEntity.hpp` pulled by the
// `IMember` -> `IParameterizedMember` chain; `IMember` (the `AccessorOwner` return type) is
// complete via the `IParameterizedMember.hpp` -> `IMember.hpp` include; `TypeParameterSubstitution`
// (the `Specialize` parameter type) is forward-declared transitively via `IMember.hpp`.
class ITypeParameter;

// A method, constructor, destructor, or operator. A concrete method (a `MetadataMethod` /
// `SpecializedMethod` / `LocalFunctionMethod` -- the implementations land later) subclasses
// `IMethod` and overrides every `IParameterizedMember` / `IMember` / `IEntity` / ...
// accessor plus the `IMethod`-own accessors added here. `IMethod` single-inherits
// `IParameterizedMember` (no diamond), so `Name()` / `SymbolKind()` are inherited unchanged
// (the D374 precedent) and `Parameters` (the `IParameterizedMember`-own accessor) is
// inherited unchanged.
//
// `IMethod` is abstract. The `Specialize` override has a covariant return type (`const
// IMethod*`, overriding `IMember::Specialize`'s `const IMember*`), the standard C++ mirror
// of the C# `new IMethod Specialize(...)` -- see the header comment convention (e).
class IMethod : public IParameterizedMember {
public:
    // The C# `IEnumerable<IAttribute> GetReturnTypeAttributes()` -- the attributes on the
    // return type (e.g. `[return: MarshalAs(...)]`), NOT inherited. A by-value snapshot of
    // non-owning pointers (the `IEntity::GetAttributes` convention).
    virtual std::vector<const IAttribute*> GetReturnTypeAttributes() const = 0;

    // The C# `bool ReturnTypeIsRefReadOnly` -- whether the return type is 'ref readonly'.
    virtual bool ReturnTypeIsRefReadOnly() const = 0;

    // The C# `bool IsInitOnly` -- whether the method may only be called on fresh instances
    // (used with C# 9 `init;` property setters).
    virtual bool IsInitOnly() const = 0;

    // The C# `bool ThisIsRefReadOnly` -- whether the method accepts the 'this' reference as
    // ref readonly (C# 8 'readonly' method, or within a C# 7.2 'readonly struct').
    virtual bool ThisIsRefReadOnly() const = 0;

    // The C# `IReadOnlyList<ITypeParameter> TypeParameters` -- the type parameters of this
    // method (empty if the method is not generic). A by-value snapshot of non-owning
    // pointers (the `GetAttributes` / `Parameters` convention).
    virtual std::vector<const ITypeParameter*> TypeParameters() const = 0;

    // The C# `IReadOnlyList<IType> TypeArguments` -- the type arguments passed to this
    // method (the type parameters themselves when not yet parameterized). A by-value
    // snapshot of shared `IType` handles (the D271 `ITypePtr` convention).
    virtual std::vector<ITypePtr> TypeArguments() const = 0;

    // The C# `bool IsExtensionMethod` -- true for classic extension methods (where the
    // extension method IS the implementation method).
    virtual bool IsExtensionMethod() const = 0;

    // The C# `bool IsLocalFunction` -- whether this method is a local function.
    virtual bool IsLocalFunction() const = 0;

    // The C# `bool IsConstructor` -- whether this method is a constructor (`.ctor`/`.cctor`).
    virtual bool IsConstructor() const = 0;

    // The C# `bool IsDestructor` -- whether this method is a destructor (`Finalize`).
    virtual bool IsDestructor() const = 0;

    // The C# `bool IsOperator` -- whether this method is an operator / conversion.
    virtual bool IsOperator() const = 0;

    // The C# `bool HasBody` -- whether the method has a body (false for abstract / extern /
    // partial-without-implementation methods).
    virtual bool HasBody() const = 0;

    // The C# `bool IsAccessor` -- whether this method is a property/event accessor.
    // ([MemberNotNullWhen(true, nameof(AccessorOwner))]: when true, `AccessorOwner` is
    // non-null; that invariant is a property of the concrete implementation.)
    virtual bool IsAccessor() const = 0;

    // The C# `IMember? AccessorOwner` -- the property/event this method is an accessor of,
    // or null if this method is not an accessor. A raw nullable pointer.
    virtual const IMember* AccessorOwner() const = 0;

    // The C# `MethodSemanticsAttributes AccessorKind` -- the kind of accessor this is
    // (Setter / Getter / Adder / Remover / Raiser / Other, or None for a non-accessor).
    virtual MethodSemanticsAttributes AccessorKind() const = 0;

    // The C# `IMethod? ReducedFrom` -- the original method if this method is reduced from an
    // extension method or a local function, null otherwise. A raw nullable pointer to the
    // enclosing class (a member-function may return a pointer to the enclosing class even
    // while it is incomplete).
    virtual const IMethod* ReducedFrom() const = 0;

    // The C# `new IMethod Specialize(TypeParameterSubstitution substitution)` -- a
    // covariant-return override of the inherited `IMember::Specialize`. Returns a non-owning
    // handle to the newly-specialized method (the type system owns it); the parameter is a
    // pointer (the D387 long-pole-dep deviation). See the header comment convention (e).
    virtual const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
