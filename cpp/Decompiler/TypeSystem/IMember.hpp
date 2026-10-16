// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IMember.cs -- `IMember` is the root of the
// resolved member hierarchy: every resolved method / field / property / event is an
// `IMember` (and an `IEntity`). The C# `interface IMember : IEntity` ports to a C++
// abstract base deriving from `IEntity` (D381) -- single inheritance, so no
// multiple-inheritance-diamond redeclaration is needed (unlike `IEntity` / `ITypeParameter`).
//
// `IMember` is the long-pole-blocked root of the member family: `IParameterizedMember`
// (which adds `Parameters`), `IMethod` / `IProperty` (which `: IParameterizedMember`),
// `IEvent` / `IField` (which `: IMember`) all build on it, and through them
// `ITypeDefinition` (which exposes its members) and `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// It lands now that `IEntity` (D381) and all of `IEntity`'s deps are ported. Its OWN deps
// are `TypeParameterSubstitution` and `TypeVisitor` (the long-pole chunk: `TypeVisitor`
// needs the concrete `IType` `VisitChildren` + the missing `TupleType` / `ModifiedType` /
// `NullabilityAnnotatedType` / `FunctionPointerType` concrete types). Both are C#
// `class`es (REFERENCE types), so every `IMember` member that uses them references an
// already-constructed, type-system-owned object -- never constructs one inline. They are
// therefore FORWARD-DECLARED here (not yet ported), and the three members that touch them
// (`Substitution`, `Specialize`, `Equals`) use POINTER types so the header compiles with
// both incomplete and a test stub can implement them with `nullptr` stand-ins. This is the
// documented "forward-declared-IMember, deferring the long-pole `TypeParameterSubstitution`
// / `TypeVisitor` deps" shell strategy from the D386 next-in-order targets -- the interface
// is COMPLETE (all members declared); only the two dep TYPES are deferred.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `new IType DeclaringType { get; }` re-states `IEntity.DeclaringType` (which is
//      `IType?`, nullable) with a non-null `IType` contract. For a reference type `IType?`
//      and `IType` are the SAME runtime type (the `?` is a static-analysis annotation
//      only), so the C# `new` is purely a nullability re-statement with NO semantic
//      difference. `IMember : IEntity` is SINGLE inheritance (no diamond), so the D374
//      `IVariable` "inherited virtual covers the `new`" precedent applies: `IMember` does
//      NOT redeclare `DeclaringType` -- it inherits `IEntity::DeclaringType()` (returning
//      `ITypePtr`), and a concrete member's single override returns a non-null `ITypePtr`
//      (a member always has a declaring type). The non-null contract is a property of the
//      concrete implementation, documented here, not enforced by a separate declaration.
//      This is distinct from the D381 `IEntity` / D383 `ITypeParameter` cases, where the
//      `new string Name` REQUIRED a redeclaration to disambiguate a multiple-inheritance
//      diamond -- here there is no diamond.
//  (b) `IMember MemberDefinition` ("Returns `this` if this is not a specialized member")
//      ports to `const IMember* MemberDefinition() const` -- a non-owning raw pointer (the
//      member owns itself / its definition; the caller holds a non-owning handle),
//      non-null in practice. The `IEntity::ParentModule` / `IAttribute::Constructor`
//      non-owning-pointer precedent.
//  (c) `IType ReturnType` ("never returns null") ports to `const IType& ReturnType() const`
//      -- a non-null reference return, the `IVariable::Type()` D374 "non-null owned-type
//      reference" convention (the concrete implementation holds the `IType` via an
//      `ITypePtr` or value member and returns a reference valid for the member's lifetime).
//  (d) `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers` ports to
//      `std::vector<const IMember*>` returned BY VALUE -- a snapshot of non-owning
//      pointers (the `IEntity::GetAttributes` D381 convention: the member owns its
//      implemented-interface references; the caller holds raw pointers). `const IMember*`
//      is a complete pointer type regardless of `IMember`'s own completeness, so the
//      `std::vector` instantiates with `IMember` still being defined.
//  (e) The three long-pole members use POINTER types for the deferred deps:
//      - `TypeParameterSubstitution Substitution` ("Returns `Identity` for not
//        specialized" -- never null) ports to `const TypeParameterSubstitution*` -- a
//        non-owning raw pointer, non-null in practice. DEVIATION from the faithful
//        `const TypeParameterSubstitution&` (the `IVariable::Type()` non-null-reference
//        convention): a reference RETURN to a forward-declared type cannot be implemented
//        by a test stub (it would need a complete `TypeParameterSubstitution` instance to
//        return a reference to), so a pointer is used to keep the dep forward-declared and
//        the stub testable with a `nullptr` stand-in. The non-null contract holds for real
//        implementations (they return `&Identity` or a cached substitution); the pointer is
//        a permanent non-owning-handle representation, consistent with `MemberDefinition`.
//      - `IMember Specialize(TypeParameterSubstitution substitution)` ports to
//        `const IMember* Specialize(const TypeParameterSubstitution* substitution) const`
//        -- the parameter is a pointer (DEVIATION from the faithful `const
//        TypeParameterSubstitution&` for forward-declaration + testability: the test calls
//        `Specialize(nullptr)`; a real implementation receives a non-null pointer) and the
//        return is a non-owning `const IMember*` (the type system owns the newly-specialized
//        member; the caller holds a non-owning handle, the `MemberDefinition` precedent).
//      - `bool Equals(IMember? obj, TypeVisitor typeNormalization)` ports to
//        `bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const` --
//        `obj` is a nullable pointer (the faithful `IMember?` -> `const IMember*`),
//        `typeNormalization` is a pointer (DEVIATION from the faithful `const
//        TypeVisitor&` for forward-declaration + testability). A real implementation
//        receives a non-null `typeNormalization`.
//      These three pointer deviations are uniform and documented; they defer the
//      `TypeParameterSubstitution` / `TypeVisitor` long-pole without omitting any member.
//  (f) NO name-hiding qualification is needed: `IMember` does NOT redeclare `SymbolKind`
//      (it inherits `IEntity`'s / `ISymbol`'s), and none of `MemberDefinition` /
//      `ReturnType` / `ExplicitlyImplementedInterfaceMembers` / `IsExplicitInterfaceImplementation`
//      / `IsVirtual` / `IsOverride` / `IsOverridable` / `Substitution` / `Specialize` /
//      `Equals` collides with a namespace-scope type in the `TypeSystem` namespace -- the
//      D375 `INamedElement` / D380 `Nullability` collision-free-accessor convention
//      applies. `Equals` is a new method (no base `Equals` with this signature exists in
//      the `ISymbol` / `ICompilationProvider` / `INamedElement` / `IEntity` chain), so it
//      introduces a fresh pure-virtual with no shadowing.

#pragma once

#include "Decompiler/TypeSystem/IEntity.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the not-yet-ported long-pole deps. Both are C# `class`es
// (reference types); `IMember` only references already-constructed instances of them
// (never constructs one inline), so a pointer / reference to either needs only a forward
// declaration (the `ICompilationProvider.hpp` / `IAmbience.hpp` precedent). They land as
// separate later leaves, at which point the three pointer-typed members may be revisited
// (the pointer is a valid permanent representation, so no revisit is required).
class TypeParameterSubstitution;
class TypeVisitor;

// The root of the resolved member hierarchy. A concrete member (a `MetadataMethod` /
// `MetadataField` / `MetadataProperty` / `MetadataEvent` -- the implementations land later)
// subclasses `IMember` and overrides every accessor. `IMember` single-inherits `IEntity`
// (no diamond), so `Name()` is inherited unchanged (the D374 precedent) and `DeclaringType`
// is inherited (the `new IType` is a nullability re-statement with no C++ counterpart).
//
// `IMember` is abstract; concrete members also implement `IParameterizedMember` (methods /
// properties, which add `Parameters`) or stay plain `IMember` (fields / events).
class IMember : public IEntity {
public:
    // The C# `IMember MemberDefinition` -- the original member definition for this member
    // (`this` if not specialized; the unspecialized definition otherwise). A non-owning
    // raw pointer, non-null in practice.
    virtual const IMember* MemberDefinition() const = 0;

    // The C# `IType ReturnType` -- the return type of this member (the field type, the
    // method return type, the property type, the event delegate type). Never null. A
    // non-null reference return (the `IVariable::Type()` D374 convention).
    virtual const IType& ReturnType() const = 0;

    // The C# `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers` -- the interface
    // members this member explicitly implements. A by-value snapshot of non-owning
    // pointers (the `IEntity::GetAttributes` convention).
    virtual std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const = 0;

    // The C# `bool IsExplicitInterfaceImplementation` -- whether this member explicitly
    // implements an interface.
    virtual bool IsExplicitInterfaceImplementation() const = 0;

    // The C# `bool IsVirtual` -- whether the 'virtual' modifier was used (non-virtual
    // members can still be overridden if abstract or overriding).
    virtual bool IsVirtual() const = 0;

    // The C# `bool IsOverride` -- whether this member is overriding another member.
    virtual bool IsOverride() const = 0;

    // The C# `bool IsOverridable` -- whether the member can be overridden (true when
    // abstract / virtual / override and not sealed).
    virtual bool IsOverridable() const = 0;

    // The C# `TypeParameterSubstitution Substitution` -- the type-parameter substitution
    // for this specialized member (`TypeParameterSubstitution.Identity` for not
    // specialized). A non-owning raw pointer, non-null in practice; pointer-typed (not
    // `const&`) to keep the `TypeParameterSubstitution` dep forward-declared and a test
    // stub implementable with a `nullptr` stand-in. See the header comment convention (e).
    virtual const TypeParameterSubstitution* Substitution() const = 0;

    // The C# `IMember Specialize(TypeParameterSubstitution substitution)` -- specializes
    // this member with the given substitution (composing with any existing substitution).
    // Returns a non-owning handle to the newly-specialized member (the type system owns
    // it); the parameter is a pointer (non-null for a real call) to keep the dep
    // forward-declared. See the header comment convention (e).
    virtual const IMember* Specialize(const TypeParameterSubstitution* substitution) const = 0;

    // The C# `bool Equals(IMember? obj, TypeVisitor typeNormalization)` -- whether this
    // member equals `obj` under the given type normalization. `obj` is a nullable pointer
    // (the faithful `IMember?`); `typeNormalization` is a pointer (non-null for a real
    // call) to keep the `TypeVisitor` dep forward-declared. See the header comment (e).
    virtual bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
