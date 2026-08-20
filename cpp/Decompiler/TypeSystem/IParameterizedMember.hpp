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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IParameterizedMember.cs -- the
// `IParameterizedMember` interface marks a member that carries a parameter list (a method
// or a property). The C# `interface IParameterizedMember : IMember` ports to a C++ abstract
// base deriving from `IMember` (D387) -- single inheritance, so no
// multiple-inheritance-diamond redeclaration is needed (unlike `IEntity` / `ITypeParameter`).
// It adds a single accessor: the read-only `Parameters` list.
//
// It is the next-in-order leaf after `IMember` (D387), now that `IMember` and `IParameter`
// (D382) are both ported. It lands the interface whose single `Parameters` accessor
// `IMethod` and `IProperty` build on (`IMethod : IParameterizedMember`, `IProperty :
// IParameterizedMember`), and through them `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker of `CSharpAmbience`) read a member's parameters. `IEvent` /
// `IField` stay plain `IMember`s (they carry no parameter list).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IReadOnlyList<IParameter> Parameters` is a read-only sequence of
//      `IParameter` references (the parameterized member owns its parameters; the caller
//      holds a read-only view). It ports to `std::vector<const IParameter*>` returned BY
//      VALUE -- a snapshot of non-owning pointers (the `IEntity::GetAttributes` /
//      `IMember::ExplicitlyImplementedInterfaceMembers` precedent: a snapshot of non-owning
//      raw pointers, the caller does not own the pointees). `const IParameter*` is a
//      complete pointer type regardless of `IParameter`'s own completeness, so the
//      `std::vector` instantiates with only `IParameter` forward-declared (the
//      `IEntity::GetAttributes` forward-declared-`IAttribute` precedent).
//  (b) `IParameter` is forward-declared (not included): the only use is the
//      `std::vector<const IParameter*>` element type, which is a complete pointer type with
//      `IParameter` incomplete, so no include is needed (keeping the include graph minimal,
//      the `IEntity.hpp` forward-declared-`IAttribute` / -`IModule` / -`ITypeDefinition`
//      pattern). `IParameter.hpp` is already ported (D382); a real consumer that dereferences
//      the pointers includes it.
//  (c) NO name-hiding qualification is needed: `IParameterizedMember` adds only
//      `Parameters`, which does not collide with a namespace-scope type in the `TypeSystem`
//      namespace, and it does NOT redeclare `Name` / `SymbolKind` (single inheritance, so
//      the inherited `IMember` / `IEntity` / `ISymbol` / `INamedElement` accessors are
//      inherited unchanged -- the D374 single-inheritance "inherited virtual covers the
//      `new`" precedent, here with no `new` at all). The D375 `INamedElement` /
//      D380 `Nullability` collision-free-accessor convention applies.

#pragma once

#include "Decompiler/TypeSystem/IMember.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the parameter interface (the element type of `Parameters`).
// `IParameter` is already ported (D382); it is only forward-declared here because a
// `std::vector<const IParameter*>` element type is a complete pointer type with
// `IParameter` incomplete (the `IEntity::GetAttributes` forward-declared-`IAttribute`
// precedent), keeping the include graph minimal.
class IParameter;

// A member that carries a parameter list (a method or a property). A concrete
// parameterized member (a `MetadataMethod` / `MetadataProperty` -- the implementations land
// later) subclasses `IParameterizedMember` and overrides every `IMember` / `IEntity` / ...
// accessor plus the `Parameters` accessor added here. `IParameterizedMember` single-inherits
// `IMember` (no diamond), so `Name()` is inherited unchanged (the D374 precedent).
//
// `IParameterizedMember` is abstract; a concrete member that is NOT parameterized (a field
// or an event) subclasses `IMember` directly, not `IParameterizedMember`.
class IParameterizedMember : public IMember {
public:
    // The C# `IReadOnlyList<IParameter> Parameters` -- the parameter list of this method or
    // property (empty for a parameterless member). A by-value snapshot of non-owning
    // pointers (the `IEntity::GetAttributes` convention: the member owns its parameters,
    // the caller holds raw pointers). Read-only access: the caller must not modify the
    // pointees through this snapshot.
    virtual std::vector<const IParameter*> Parameters() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
