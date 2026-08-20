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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IField.cs -- `IField` is the interface for a field
// or a constant. The C# `interface IField : IMember, IVariable` ports to a C++ abstract base
// multiply-inheriting those two independent abstract bases (the established
// C#-interface-to-C++-abstract-base convention, the `IMember` D387 / `IVariable` D374
// precedents). It is the next self-contained leaf toward `ITypeDefinition` (which exposes its
// fields) and `TypeSystemAstBuilder` (the long-pole remaining blocker of `CSharpAmbience`):
// `TypeSystemAstBuilder` reads `field.Name` / `field.ReturnType` / `field.IsReadOnly` /
// `field.IsVolatile` / `field.GetConstantValue()` to render a field declaration with its
// initializer (the `IVariable.GetConstantValue` field-initializer path).
//
// It lands now that ALL its deps are ported: `IMember` (D387, the `ReturnType` / member
// definition / virtuality / specialization surface), `IVariable` (D374, the `Type` /
// `IsConst` / `GetConstantValue` surface), and through them `IEntity` (D381), `ISymbol`
// (D372), `INamedElement` (D375), `ICompilationProvider` (D379), `IType` (D271),
// `Accessibility` (D373), `KnownAttribute` (D378), `IAttribute` (D386). The three own
// accessors are plain bools, so no further deps are pulled in.
//
// KEY PORT CONVENTIONS:
//  (a) THE SHARED-ISymbol-BASE DIAMOND (the key NEW structural crux this iteration, distinct
//      from every prior ported diamond): `IField : IMember, IVariable` is the FIRST ported
//      interface whose TWO bases BOTH derive (transitively) from a SHARED base -- `IMember` :
//      `IEntity` : `ISymbol` and `IVariable` : `ISymbol`. `ISymbol` therefore appears in BOTH
//      branches of the inheritance graph. With the NON-VIRTUAL inheritance the established
//      convention uses (every prior TypeSystem interface port uses non-virtual `public`
//      inheritance), `IField` has TWO independent `ISymbol` subobjects -- one inside the
//      `IMember` subobject (via `IEntity`) and one inside the `IVariable` subobject. This has
//      TWO consequences the prior diamonds did NOT have:
//        - The D381 `IEntity` (ISymbol + ICompilationProvider + INamedElement) and D383
//          `ITypeParameter` (IType + ISymbol) diamonds each had only ONE `ISymbol` subobject
//          (the other base did not derive from `ISymbol`), so only the `Name` accessor (which
//          `ISymbol` shares with `INamedElement` in the `IEntity` case, and with `IType` in
//          the `ITypeParameter` case) was ambiguous through the interface pointer, and the C#
//          `new string Name` redeclaration was the ONLY disambiguation needed.
//        - Here, BOTH `SymbolKind` and `Name` (the entire `ISymbol` surface) are inherited via
//          TWO paths, so BOTH are ambiguous through an `IField*`. The C# `IField` redeclares
//          ONLY `Name` (`new string Name { get; }` -- "solve ambiguity between IMember.Name
//          and IVariable.Name"); it does NOT redeclare `SymbolKind`, because C# INTERFACE
//          FLATTENING unifies the shared `ISymbol` interface into a SINGLE vtable slot (one
//          `ISymbol.SymbolKind`), so `field.SymbolKind` is unambiguous in C#. The non-virtual
//          C++ port CANNOT flatten -- it has two `ISymbol` subobjects -- so a lookup of
//          `SymbolKind` through an `IField*` is ambiguous (MSVC C2385) WITHOUT a C++-specific
//          redeclaration. The faithful port therefore REDECLARES BOTH `Name()` (mirroring the
//          C# `new string Name`) AND `SymbolKind()` (a C++-ONLY disambiguation with NO C#
//          source counterpart, present purely so `field->SymbolKind()` compiles and dispatches
//          the same single override `field.SymbolKind` reaches in C#). A single
//          `IField::SymbolKind()` override is the final overrider for BOTH `ISymbol`
//          subobjects (the standard rule: one derived function overrides every matching base
//          virtual across all base subobjects), so dispatch is uniform; the redeclaration
//      fixes the LOOKUP, not the dispatch. This is the documented C++-vs-C# divergence for the
//      shared-base diamond; a future virtual-inheritance refactor of `ISymbol` (making
//      `IEntity` / `IVariable` derive `public virtual ISymbol`) would unify the subobject and
//      make the `SymbolKind` redeclaration redundant -- deferred, the same "forward-declare /
//      defer the long-pole" shell strategy, since it would touch every existing TypeSystem
//      interface header and the existing concrete test stubs.
//      A SECOND consequence of the two-`ISymbol`-subobject layout: an UPCAST from `IField*` to
//      `ISymbol*` is AMBIGUOUS (which `ISymbol` subobject?) -- `ISymbol* s = field;` does NOT
//      compile (C2594 / C2385). A consumer that needs `ISymbol` dispatch through an `IField*`
//      must first upcast to ONE of the unambiguous intermediate bases (`IMember*` or
//      `IVariable*`, each of which appears once and each of which IS-A `ISymbol`), then to
//      `ISymbol*` from there. The test exercises dispatch through `IMember*` and
//      `IVariable*` (unambiguous) and does NOT attempt the ambiguous `ISymbol*` upcast.
//  (b) The C# `new string Name { get; }` redeclares `Name` (hiding the `IMember.Name` /
//      `IVariable.Name` accessors with a re-statement of the same contract). In C++ this
//      DOES have a counterpart, REQUIRED here (the D381 `IEntity` / D383 `ITypeParameter`
//      multiple-inheritance-diamond `Name()` crux): `IMember` (via `IEntity`) and `IVariable`
//      (via `ISymbol`) are TWO INDEPENDENT bases each declaring `virtual std::string Name()
//      const = 0` with the same signature, so `IField` would otherwise inherit both
//      pure-virtuals and a name lookup through an `IField*` is AMBIGUOUS (C2385). The faithful
//      port therefore REDECLARES `Name()` -- a pure-virtual override overriding BOTH base
//      `Name`s, keeping `IField` abstract; a concrete field overrides `Name()` once and
//      dispatch through `IMember*` / `IVariable*` / `IField*` all reach it.
//  (c) The `SymbolKind()` return type is GLOBALLY QUALIFIED because the inherited
//      `ISymbol::SymbolKind()` member function hides the namespace-scope `SymbolKind` enum in
//      this derived class (the D372 / D383 cross-scope name-hiding crux), so an unqualified
//      `SymbolKind` in the return type would resolve to the inherited function (a non-type),
//      not the enum.
//  (d) The three IField-own accessors (`IsReadOnly` / `ReturnTypeIsRefReadOnly` / `IsVolatile`)
//      are plain bools with no namespace-scope collision and no base redeclaration -- the
//      D375 `INamedElement` / D380 `Nullability` collision-free-accessor convention applies.
//      `ReturnTypeIsRefReadOnly` IS a member of `IMethod` (D389) and `IProperty` (D390) too,
//      but `IField` does NOT derive from either -- they are SEPARATE interface hierarchies
//      sharing only the `IMember` / `IEntity` bases -- so the `ReturnTypeIsRefReadOnly()`
//      virtuals do NOT clash (the D390 `IProperty`-vs-`IMethod` non-clash precedent).
//  (e) The `IVariable`-own accessors (`Type` / `IsConst` / `GetConstantValue`) and the
//      `IMember`-own accessors (`ReturnType` / `MemberDefinition` / ...) are inherited
//      UNAMBIGUOUSLY: each name lives on only ONE of the two bases, so `field->Type()`,
//      `field->ReturnType()`, `field->IsConst()`, `field->GetConstantValue()` all compile and
//      dispatch without redeclaration. (`IMember.ReturnType` and `IVariable.Type` are DIFFERENT
//      names that both denote the field's type -- the C# `MetadataField` implements both to
//      return the same `IType`; a concrete `TestField` does the same.)

#pragma once

#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// A field or constant. `IField : IMember, IVariable` adds whether the field is `readonly`
// (`IsReadOnly`), whether the field type is `ref readonly` (`ReturnTypeIsRefReadOnly`), and
// whether the field is `volatile` (`IsVolatile`). The concrete `MetadataField` /
// `SpecializedField` implementations land with the rest of the Phase-5 type system.
//
// `Name()` AND `SymbolKind()` are redeclared here to disambiguate the shared-`ISymbol`-base
// multiple-inheritance diamond (the header comment crux): `IMember` (via `IEntity`) and
// `IVariable` (via `ISymbol`) each contribute an `ISymbol` subobject, so both `Name` and
// `SymbolKind` lookups through an `IField*` are ambiguous without the redeclarations. A
// concrete field overrides `Name()` and `SymbolKind()` once and dispatch through `IMember*` /
// `IVariable*` / `IField*` all reach them. An upcast to `ISymbol*` from an `IField*` is
// AMBIGUOUS (two `ISymbol` subobjects) -- dispatch `ISymbol` through `IMember*` or `IVariable*`.
class IField : public IMember, public IVariable {
public:
    // The C# `new string Name` -- redeclared to disambiguate the `IMember.Name` (via `IEntity`)
    // vs `IVariable.Name` (via `ISymbol`) ambiguity of the shared-`ISymbol`-base diamond. This
    // pure-virtual override overrides BOTH base `Name`s and keeps `IField` abstract; a concrete
    // field overrides `Name()` once and dispatch through any base pointer reaches it. The D381
    // `IEntity` / D383 `ITypeParameter` multiple-inheritance-diamond `Name()` precedent.
    virtual std::string Name() const = 0;

    // C++-ONLY disambiguation of the shared-`ISymbol`-base diamond (NO C# source counterpart --
    // C# interface flattening unifies the single `ISymbol.SymbolKind`, so `IField` does not
    // redeclare `SymbolKind`; the non-virtual C++ port has two `ISymbol` subobjects and would be
    // ambiguous through an `IField*` without this). The return type is globally qualified because
    // the inherited `ISymbol::SymbolKind()` hides the namespace-scope `SymbolKind` enum (the D372
    // crux). A single concrete override is the final overrider for both `ISymbol` subobjects.
    virtual ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const = 0;

    // The C# `bool IsReadOnly` -- whether this field is `readonly` (the `InitOnly` flag).
    virtual bool IsReadOnly() const = 0;

    // The C# `bool ReturnTypeIsRefReadOnly` -- whether the field type is `ref readonly`.
    virtual bool ReturnTypeIsRefReadOnly() const = 0;

    // The C# `bool IsVolatile` -- whether this field is `volatile`.
    virtual bool IsVolatile() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
