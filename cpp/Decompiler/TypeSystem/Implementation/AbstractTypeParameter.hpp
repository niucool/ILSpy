// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/AbstractTypeParameter.cs -- the
// abstract base for `ITypeParameter` implementations. It holds the owner (an `IEntity` or
// a `(ICompilation, SymbolKind)` pair), the index, the name, and the variance, and supplies
// the `IType` / `ISymbol` / `ITypeParameter` / `ICompilationProvider` surface that every
// concrete type-parameter impl shares (`SpecializedTypeParameter` [nested in
// `SpecializedMethod`], `MetadataTypeParameter`, ...). The leaf it unblocks is
// `SpecializedMethod`'s nested `SpecializedTypeParameter : AbstractTypeParameter`, which
// delegates the constraint surface to a base type parameter and re-computes `TypeConstraints`
// through the method's substitution.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public abstract class AbstractTypeParameter : ITypeParameter,
//      ICompilationProvider` ports to a C++ class `AbstractTypeParameter : public
//      ITypeParameter, public ICompilationProvider` with PROTECTED ctors (the port's
//      "abstract-by-protected-ctor" mirror of `abstract class` -- the class overrides every
//      concrete `ITypeParameter` / `IType` / `ISymbol` / `ICompilationProvider` member but
//      leaves the C#-`abstract` members pure-virtual, so it is not instantiable directly;
//      only a derived leaf that overrides the abstracts can). `ITypeParameter : IType,
//      ISymbol` is the D383 `Name` diamond (the single `Name()` override is the final
//      overrider for both `IType::Name` and `ISymbol::Name`); `ICompilationProvider` is a
//      fresh independent base (NOT via `ITypeParameter`), so the class has ONE `IType`
//      subobject and `shared_from_this()` works (the `enable_shared_from_this<IType>` via
//      `ITypeParameter`).
//  (b) The C# `readonly ICompilation compilation` / `IEntity owner` / `SymbolKind ownerType`
//      / `int index` / `string name` / `VarianceModifier variance` fields port to by-value /
//      reference / pointer members: `const ICompilation& compilation_` (a REFERENCE member,
//      the `ICompilationProvider` convention -- the `LookupTypeDefinition::compilation_`
//      precedent; non-copyable, but these are `shared_ptr`-managed polymorphic bases, not
//      copied), `const IEntity* owner_` (NON-OWNING, nullable -- null for the
//      compilation-based ctor), `SymbolKind ownerType_`, `int index_`, `std::string name_`,
//      `VarianceModifier variance_`. The owner-based ctor reads `compilation` from
//      `owner->Compilation()` and `ownerType` from `owner->SymbolKind()` (the C# ctor); the
//      compilation-based ctor takes both directly. The C# `name ?? ((OwnerType == Method ?
//      "!!" : "!") + index)` default-name computation ports to an empty-string sentinel (the
//      port's `std::string` has no null; an empty `name` -> the default form -- a documented
//      divergence, minor since the `SpecializedTypeParameter` passes `baseTp.Name()` non-empty).
//  (c) The D372 name-shadowing crux applies to `SymbolKind()` (the inherited `ISymbol::
//      SymbolKind` member name shadows the namespace-scope `SymbolKind` enum in MSVC's
//      complete-class lookup) and `OwnerType()` (the inherited `ITypeParameter::OwnerType`
//      shadows the enum), so both return types are GLOBALLY QUALIFIED (the
//      `DummyTypeParameter` precedent).
//  (d) DEFERRED from the C# file: the computed `EffectiveBaseClass` / `EffectiveInterfaceSet`
//      (the C# `CalculateEffectiveBaseClass` / `CalculateEffectiveInterfaceSet` with the
//      `BusyManager` cyclic-protection + `ICompilation.FindType` + `IsDerivedFrom`). The port
//      has `FindType` (D-ported) and `IsDerivedFrom` (D-ported) but NOT `BusyManager`; the
//      `EffectiveBaseClass` override returns `UnknownType()` and `EffectiveInterfaceSet`
//      returns empty (the `DummyTypeParameter` precedent) -- a documented divergence (the
//      faithful computed versions land with `BusyManager`). The `IsReferenceType` computed
//      property (which reads `EffectiveBaseClass`) inherits the `IType` `nullopt` default
//      (with `EffectiveBaseClass` deferred to `UnknownType`, the computed `IsReferenceType`
//      returns `nullopt` anyway -- the `UnknownType.Kind == Unknown` arm falls through to
//      `return null`). The member-enumeration methods (`GetConstructors` / `GetMethods` /
//      `GetProperties` / `GetFields` / `GetEvents` / `GetMembers` / `GetAccessors` /
//      `GetNestedTypes` -- the C# `IgnoreInheritedMembers` short-circuit + the
//      `FakeMethod.CreateDummyConstructor` dummy-ctor + the `GetMembersHelper` routing) inherit
//      the `IType` empty defaults (DEFERRED -- the routed versions land with `GetMembersHelper`
//      / `FakeMethod`, the blocker this leaf advances toward). `GetDefinitionOrUnknown` /
//      `IType.GetSubstitution` (interface-level) / `IType.TypeArguments` (interface-level) /
//      `IType.DeclaringType` is NOT on the ported `IType` surface (omitted; the port's `IType`
//      does not declare it -- the `DummyTypeParameter` no-such-member precedent). `IType.IsByRefLike`
//      IS on the ported `IType` surface (virtual-WITH-DEFAULT `false`, the `AbstractType.IsByRefLike
//      => false` default -- added for the `IsBoxingConversion` guard that excludes ref structs).
//      `INamedElement.FullName` / `Namespace` are NOT on `ITypeParameter` (the
//      port's `ITypeParameter : IType, ISymbol` does not inherit `INamedElement`; omitted).
//  (e) `AcceptVisitor` / `ChangeNullability` are OUT-OF-LINE in the .cpp (they need
//      `TypeVisitor` / `NullabilityAnnotatedTypeParameter` complete). `ChangeNullability`
//      mirrors `DummyTypeParameter::ChangeNullability`: `Oblivious` -> `shared_from_this()`;
//      else -> `make_shared<NullabilityAnnotatedTypeParameter>(static_pointer_cast<
//      ITypeParameter>(shared_from_this()), nullability)` (the wrapper carries two `IType`
//      subobjects, so it is returned through `ITypeParameter` -- the direct `IType` conversion
//      is ambiguous). `VisitChildren` inherits the `IType` `shared_from_this()` default (a
//      type parameter has no children -- the C# `VisitChildren => this`).
//  (f) `Equals(IType)` (the C# `virtual bool Equals(IType other) => this == other`,
//      reference equality) ports to `StructuralEquals` returning `this == &other` (the port's
//      `IType::Equals(const IType&)` is non-virtual, dispatching to the protected
//      `StructuralEquals`; the `DummyTypeParameter` / test-stub precedent). `GetHashCode()`
//      (the C# `override int GetHashCode() => base.GetHashCode()`, identity hash) and
//      `ToString()` (the C# `override string ToString() => ReflectionName`) are PLAIN members
//      (the port has no `object.GetHashCode` / `object.ToString` virtual; the `DummyTypeParameter.
//      ToString` plain-member precedent). A derived leaf (`SpecializedTypeParameter`) may hide
//      `GetHashCode()` with its own (the C# `override` has no virtual C++ counterpart).
//  (g) `DirectBaseTypes` (the C# `IEnumerable<IType> DirectBaseTypes => TypeConstraints.Select(
//      t => t.Type)`) overrides the `IType` empty default to read the (abstract) `TypeConstraints()`
//      and project each `TypeConstraint::Type()` -- a concrete method calling a pure-virtual
//      (dispatched to the derived override; the C# `DirectBaseTypes` reads the overridden
//      `TypeConstraints`).

#pragma once

#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The abstract base for `ITypeParameter` implementations (see the header comment). Holds the
// owner / index / name / variance and supplies the shared `IType` / `ISymbol` /
// `ITypeParameter` / `ICompilationProvider` surface. The complex members (`AcceptVisitor` /
// `ChangeNullability`) are out-of-line in the .cpp; the rest are inline.
class AbstractTypeParameter : public ITypeParameter, public ICompilationProvider {
public:
    // The C# `Equals(object)` (sealed) -> `Equals(IType)`. The port's `IType::Equals(const
    // IType&)` is non-virtual (dispatches to `StructuralEquals`); the C# `Equals(IType)` is
    // the protected `StructuralEquals` (reference equality). `GetHashCode` / `ToString` are
    // plain members (the port has no `object.GetHashCode` / `object.ToString` virtual).
    int GetHashCode() const { return static_cast<int>(reinterpret_cast<std::uintptr_t>(this)); }
    std::string ToString() const { return ReflectionName(); }

    // --- ISymbol ---
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.TypeParameter`. Globally qualified
    // (D372 crux -- the inherited `ISymbol::SymbolKind` shadows the enum). The single
    // `Name()` override below is the final overrider for the `IType` / `ISymbol` diamond.
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }

    // --- IType ---
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    // The single `Name()` override is the final overrider for the `IType` / `ISymbol` diamond
    // (the D383 convention -- `ITypeParameter : IType, ISymbol` both declare `Name()`).
    std::string Name() const override { return name_; }
    // The C# `ReflectionName => (OwnerType == Method ? "``" : "`") + index`.
    std::string ReflectionName() const override {
        return (ownerType_ == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method ? "``" : "`") +
               std::to_string(index_);
    }
    int TypeParameterCount() const override { return 0; }
    // The C# `virtual bool Equals(IType other) => this == other` (reference equality). The
    // port's `IType::Equals(const IType&)` dispatches to this protected `StructuralEquals`.
    // A derived leaf (`SpecializedTypeParameter`) overrides this with a value-based check.
protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }
public:
    // The C# `IType AcceptVisitor(TypeVisitor) => visitor.VisitTypeParameter(this)`.
    // Out-of-line (needs `TypeVisitor` complete).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    // `VisitChildren` inherits the `IType` `shared_from_this()` default (a type parameter has
    // no children -- the C# `VisitChildren => this`); NOT redeclared here.
    // The C# `IType ChangeNullability(Nullability)`: `Oblivious` -> this; else wrap in
    // `NullabilityAnnotatedTypeParameter`. Out-of-line (needs the wrapper complete).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
    // The C# `IEnumerable<IType> DirectBaseTypes => TypeConstraints.Select(t => t.Type)`.
    // Reads the (abstract) `TypeConstraints()` and projects each `TypeConstraint::Type()`.
    std::vector<ITypePtr> DirectBaseTypes() const override {
        std::vector<ITypePtr> result;
        for (const auto& c : TypeConstraints()) {
            if (c.Type()) {
                result.push_back(c.Type());
            }
        }
        return result;
    }
    // `IsReferenceType` inherits the `IType` `nullopt` default (DEFERRED -- the C# computed
    // version reads `EffectiveBaseClass`, which is deferred to `UnknownType`; with
    // `UnknownType.Kind == Unknown`, the computed `IsReferenceType` returns `nullopt` anyway).
    // `Nullability` inherits the `IType` `Oblivious` default (the C# `IType.Nullability =>
    // Oblivious`). `GetDefinition` inherits the `nullptr` default. `TypeParameters` inherits
    // the empty default (D481). The member-enumeration methods inherit the empty defaults
    // (DEFERRED -- the routed versions land with `GetMembersHelper` / `FakeMethod`).

    // --- ITypeParameter ---
    // The C# `SymbolKind OwnerType => ownerType`. Globally qualified (D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerType_; }
    // The C# `IEntity Owner => owner` (null for the compilation-based ctor).
    const IEntity* Owner() const override { return owner_; }
    // The C# `int Index => index`.
    int Index() const override { return index_; }
    // The C# `abstract IEnumerable<IAttribute> GetAttributes()` -- ABSTRACT (a derived leaf
    // like `SpecializedTypeParameter` overrides it to delegate to the base type parameter).
    std::vector<const IAttribute*> GetAttributes() const override = 0;
    // The C# `VarianceModifier Variance => variance`.
    VarianceModifier Variance() const override { return variance_; }
    // The C# `IType EffectiveBaseClass` (the computed `CalculateEffectiveBaseClass` with
    // `BusyManager` / `FindType` / `IsDerivedFrom`). DEFERRED to `UnknownType()` (the
    // `DummyTypeParameter` precedent); the faithful computed version lands with `BusyManager`.
    ITypePtr EffectiveBaseClass() const override;
    // The C# `IReadOnlyCollection<IType> EffectiveInterfaceSet` (the computed
    // `CalculateEffectiveInterfaceSet` with `BusyManager`). DEFERRED to empty.
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    // The C# `abstract bool HasDefaultConstructorConstraint` / `HasReferenceTypeConstraint` /
    // `HasValueTypeConstraint` / `HasUnmanagedConstraint` / `AllowsRefLikeType` /
    // `NullabilityConstraint` -- ABSTRACT (a derived leaf overrides them to delegate to the
    // base type parameter).
    bool HasDefaultConstructorConstraint() const override = 0;
    bool HasReferenceTypeConstraint() const override = 0;
    bool HasValueTypeConstraint() const override = 0;
    bool HasUnmanagedConstraint() const override = 0;
    bool AllowsRefLikeType() const override = 0;
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override = 0;
    // The C# `abstract IReadOnlyList<TypeConstraint> TypeConstraints` -- ABSTRACT (a derived
    // leaf overrides it, e.g. `SpecializedTypeParameter` re-computes through the substitution).
    std::vector<TypeConstraint> TypeConstraints() const override = 0;

    // --- ICompilationProvider ---
    // The C# `ICompilation Compilation => compilation`. `compilation_` is a non-owning
    // pointer (non-null by contract); returns `*compilation_`.
    const ICompilation& Compilation() const override { return *compilation_; }

protected:
    // The C# `protected AbstractTypeParameter(IEntity owner, int index, string name,
    // VarianceModifier variance)`. Reads `compilation` from `owner->Compilation()` and
    // `ownerType` from `owner->SymbolKind()`; throws on a null `owner` (the C#
    // `ArgumentNullException`). The `name` defaults to the `!`/`!!` form when empty (the C#
    // `name ?? default`; the port's empty-string sentinel).
    AbstractTypeParameter(const IEntity* owner, int index, std::string name,
                         VarianceModifier variance);
    // The C# `protected AbstractTypeParameter(ICompilation compilation, SymbolKind ownerType,
    // int index, string name, VarianceModifier variance)`. `owner` is null (the
    // compilation-based ctor); throws on a null `compilation`.
    AbstractTypeParameter(const ICompilation& compilation,
                         ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
                         int index, std::string name, VarianceModifier variance);

private:
    const ICompilation* compilation_;
    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType_;
    const IEntity* owner_;
    int index_;
    std::string name_;
    VarianceModifier variance_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
