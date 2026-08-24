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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedMember.cs --
// the abstract `IMember` base a specialized method / property / field / event derives
// from. It wraps a base (unspecialized) member and a `TypeParameterSubstitution`,
// delegating the whole `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` /
// `ISymbol` surface to the base member EXCEPT the three it re-computes through the
// substitution: `ReturnType` (the base return type run through the substitution),
// `DeclaringType` (the base declaring type re-parameterized / run through the
// substitution), and `Substitution` (the composed substitution itself). The four
// concrete leaves `SpecializedMethod` / `SpecializedProperty` / `SpecializedField` /
// `SpecializedEvent` (and the `SpecializedParameterizedMember` base for the
// parameterized two) build on this; `GetMembersHelper` constructs them, which the
// `MemberLookup.LookupGroup` routing consumes.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public abstract class SpecializedMember : IMember` ports to a C++ class
//      `SpecializedMember : public IMember` with a PROTECTED ctor (the port's "abstract-
//      by-protected-ctor" mirror of `abstract class` -- the class overrides every
//      `IMember` pure-virtual, so it is technically concrete; the protected ctor makes
//      it non-instantiable directly, only via a derived class, faithful to the C#
//      `abstract class`). The test exercises it through a test-only public-ctor
//      subclass.
//  (b) The C# `readonly IMember baseMember` (a held reference) ports to an OWNING
//      `std::shared_ptr<IMember> baseMember_` (keeps the base member alive for the
//      specialized member's lifetime, the `NullabilityAnnotatedType::baseType_` /
//      `SpecializedParameter::baseParameter_` precedent). The ctor asserts the base is
//      non-null and NOT itself a `SpecializedMember` (the C# `throw new
//      ArgumentNullException` / `ArgumentException`), ported as `std::invalid_argument`.
//  (c) The C# `TypeParameterSubstitution substitution` field (a reference-type field
//      initialized to `Identity`, composed by `AddSubstitution`) ports to a BY-VALUE
//      `mutable TypeParameterSubstitution substitution_` (the D407 "C# heap allocation
//      realized as a value" convention; `mutable` so the `const` lazy accessors can pass
//      it as a non-const `TypeVisitor&` to `IType::AcceptVisitor` -- the port's
//      `AcceptVisitor(TypeVisitor&)` is non-const, the D406 convention). The C# field
//      SHARES the `Identity` singleton before any `AddSubstitution` (reference equality
//      with `TypeParameterSubstitution.Identity`); the port's by-value field holds a
//      COPY of `Identity` (value-equal, not reference-equal -- a documented divergence
//      the value-semantic port accepts; `Equals` compares by value, so it is benign).
//  (d) The C# `IType declaringType` / `IType returnType` lazy-cached fields (read via
//      `LazyInit.VolatileRead` / written via `LazyInit.GetOrSet`) port to
//      `mutable ITypePtr declaringType_` / `mutable ITypePtr returnType_` (owning
//      `shared_ptr<IType>`, lazily initialized in the `const` accessors; `mutable` for
//      the lazy write, the `ArrayTypeReference::resolved_` / `NestedTypeReference::
//      resolved_` precedent). `ReturnType()` returns `const IType&` (the `IMember`
//      contract) -- `*returnType_`; `DeclaringType()` returns `ITypePtr` (the `IEntity`
//      contract) -- the cached `declaringType_`.
//  (e) The D372 name-shadowing crux applies to the `SymbolKind()` / `Accessibility()`
//      overrides (the inherited `ISymbol::SymbolKind` / `IEntity::Accessibility` member
//      names shadow the namespace-scope enums in MSVC's complete-class lookup), so both
//      return types are GLOBALLY QUALIFIED (the `DummyTypeParameter::SymbolKind` /
//      `LookupMember::SymbolKind` precedent).
//  (f) `DeclaringType`'s `else` sub-branch (the C# `new ParameterizedType(def,
//      def.TypeParameters).AcceptVisitor(substitution)`, fired when the base declaring
//      type is a generic `ITypeDefinition` AND the substitution's class type arguments
//      do NOT match the declaring type's type-parameter count) is DEFERRED: the port's
//      `IType::TypeParameters()` returns NON-OWNING `const ITypeParameter*`, but the
//      `ParameterizedType` ctor needs OWNING `ITypePtr`; the owning-`TypeParameters`
//      accessor lands with the `MetadataTypeDefinition` ownership work. The deferred
//      arm falls through to the `definitionDeclaringType.AcceptVisitor(substitution)`
//      arm (a reasonable fallback that substitutes what it can). The `GetMembersHelper`
//      routing always supplies MATCHING class type arguments (the `if` sub-branch), so
//      the deferred arm is never hit there.
//  (g) `ExplicitlyImplementedInterfaceMembers()` is DEFERRED to FORWARD the base
//      member's list UNSPECIALIZED (the C# `Select(m => m.Specialize(substitution))`
//      needs the owning-`Specialize` design -- `IMember::Specialize` returns a
//      non-owning `const IMember*`, so the specialized results cannot be owned and
//      returned as non-owning pointers; the owning design lands with the concrete
//      `Specialized*` leaves / `MetadataMember` ownership work). The common case (a
//      member with no explicit interface implementation) returns empty from the base,
//      so the deferred forwarding returns empty -- no divergence there.
//  (h) `WrapAccessor` (the C# `internal` lazy accessor-wrapper `SpecializedProperty` /
//      `SpecializedEvent` use) is OMITTED -- it needs `accessorDefinition.Specialize(
//      substitution)` to return an OWNING handle for the `LazyInit` cache, which the
//      non-owning `IMember::Specialize` cannot supply; it lands with the
//      `SpecializedProperty` / `SpecializedEvent` leaves once the owning-`Specialize`
//      design is in place. Not needed by `SpecializedMember` itself, nor by
//      `SpecializedParameterizedMember` / `SpecializedField` / `SpecializedMethod`.
//  (i) `Specialize()` DELEGATES to `baseMember_->Specialize(Compose(newSubstitution,
//      substitution))`, returning the non-owning `const IMember*` the base's
//      `Specialize` returns (the `IMember` "type system owns" convention -- the base
//      member owns the result; the `SpecializedMember` holds `baseMember_`, so the
//      handle is valid for the `SpecializedMember`'s lifetime). The `newSubstitution`
//      pointer is nullable (the `IMember::Specialize` test convention); a null pointer
//      is treated as `Identity` (the C# `Specialize` is never-null by value).
//  (j) `Equals(IMember*, TypeVisitor*)` and the standalone `Equals(const
//      SpecializedMember*)` (the C# `Equals(object)`, no C++ base -- the D407
//      `TypeParameterSubstitution::Equals` standalone precedent) compare the base
//      member and the substitution. The `TypeVisitor` normalization pointer is
//      nullable; a null pointer selects the no-normalization `TypeParameterSubstitution
//      ::Equals` overload. A non-null pointer is `const_cast` to a non-const
//      `TypeVisitor&` for `TypeParameterSubstitution::Equals(other, TypeVisitor&)` --
//      the `const`-pointer is the port's forward-decl/testability convention, but a
//      real normalizer is conceptually non-const (the port's `AcceptVisitor` /
//      `TypeVisitor&` non-const convention); the `const_cast` is safe (a normalizing
//      `TypeVisitor` does not mutate in `Equals` -- it only reads its option fields).
//  (k) `GetHashCode()` ports the C# `unchecked { 1000000007 * baseMember.GetHashCode()
//      + 1000000009 * substitution.GetHashCode(); }`. `baseMember.GetHashCode()` is the
//      C# `object.GetHashCode` identity hash (no `IMember` override); the port has no
//      `IMember::GetHashCode` virtual, so the faithful identity hash is the pointer
//      identity (`reinterpret_cast<uintptr_t>(baseMember_.get())`), the
//      `TypeParameterSubstitution::GetHashCode` `std::hash<IType*>` precedent. The
//      `unchecked` wraparound ports to `unsigned int` arithmetic + `static_cast<int>`.
//  (l) `ToString()` is DEFERRED (the C# `DeclaringType.ToString()` / `ReturnType.
//      ToString()` need `IType::ToString`, which the port has not ported -- the
//      `IType`-has-no-`ToString` situation, the `SpecializedParameter::ToString`
//      deferral precedent); lands with the shared `IType::ToString` design.

#pragma once

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The abstract `IMember` base (see the header comment). Holds the base member (owning
// `shared_ptr`) and the composed substitution (by value); lazily computes
// `DeclaringType` / `ReturnType` through the substitution. The complex members are
// out-of-line in the .cpp (they need `TypeParameterSubstitution` / `ParameterizedType`
// / `ITypeDefinition` complete); the trivial delegations are inline.
class SpecializedMember : public IMember {
public:
    // The C# `TypeParameterSubstitution Substitution` -- the composed substitution.
    // Returns a non-owning pointer to the by-value `substitution_` field.
    const TypeParameterSubstitution* Substitution() const override {
        return &substitution_;
    }

    // The C# `IMember MemberDefinition => baseMember.MemberDefinition`.
    const IMember* MemberDefinition() const override {
        return baseMember_->MemberDefinition();
    }

    // The C# `IType ReturnType` -- lazily `baseMember.ReturnType.AcceptVisitor(
    // substitution)`, cached. Out-of-line (lazy + AcceptVisitor).
    const IType& ReturnType() const override;

    // The C# `IType DeclaringType` -- lazily re-parameterized / substituted declaring
    // type, cached. Out-of-line (the 3-arm + the deferred sub-branch).
    ITypePtr DeclaringType() const override;

    // The C# `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers` -- DEFERRED
    // (forwards the base list un-specialized; see the header comment (g)).
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override;

    // The C# `virtual IMember Specialize(TypeParameterSubstitution)` -- delegates to
    // `baseMember.Specialize(Compose(newSubstitution, this.substitution))`. Out-of-line.
    const IMember* Specialize(const TypeParameterSubstitution* newSubstitution) const override;

    // The C# `virtual bool Equals(IMember obj, TypeVisitor typeNormalization)`.
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override;

    // The C# `override bool Equals(object obj)` -- no C++ `object` base; a standalone
    // (the D407 `TypeParameterSubstitution::Equals(const TPS*)` precedent). Compares the
    // base member and the substitution WITHOUT normalization.
    bool Equals(const SpecializedMember* other) const;

    // The C# `override int GetHashCode()`.
    int GetHashCode() const;

    // --- The trivial delegations (inline) ---

    // The C# `EntityHandle MetadataToken => baseMember.MetadataToken`.
    std::uint32_t MetadataToken() const override { return baseMember_->MetadataToken(); }

    // The C# `ITypeDefinition DeclaringTypeDefinition => baseMember.DeclaringTypeDefinition`.
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return baseMember_->DeclaringTypeDefinition();
    }

    // The C# `bool IsVirtual => baseMember.IsVirtual` / `IsOverride` / `IsOverridable`.
    bool IsVirtual() const override { return baseMember_->IsVirtual(); }
    bool IsOverride() const override { return baseMember_->IsOverride(); }
    bool IsOverridable() const override { return baseMember_->IsOverridable(); }

    // The C# `SymbolKind SymbolKind => baseMember.SymbolKind`. Globally qualified
    // (the D372 crux -- the inherited `ISymbol::SymbolKind` shadows the enum).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return baseMember_->SymbolKind();
    }

    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // baseMember.GetAttributes()` / `HasAttribute` / `GetAttribute`.
    std::vector<const IAttribute*> GetAttributes() const override {
        return baseMember_->GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return baseMember_->HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return baseMember_->GetAttribute(attribute);
    }

    // The C# `bool IsExplicitInterfaceImplementation =>
    // baseMember.IsExplicitInterfaceImplementation`.
    bool IsExplicitInterfaceImplementation() const override {
        return baseMember_->IsExplicitInterfaceImplementation();
    }

    // The C# `Accessibility Accessibility => baseMember.Accessibility`. Globally
    // qualified (the D372 crux -- the inherited `IEntity::Accessibility` shadows the enum).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return baseMember_->Accessibility();
    }

    // The C# `bool IsStatic / IsAbstract / IsSealed => baseMember.*`.
    bool IsStatic() const override { return baseMember_->IsStatic(); }
    bool IsAbstract() const override { return baseMember_->IsAbstract(); }
    bool IsSealed() const override { return baseMember_->IsSealed(); }

    // The C# `string FullName / Name / Namespace / ReflectionName => baseMember.*`.
    // `Name` is the single final overrider for the `ISymbol` / `INamedElement` /
    // `IEntity` diamond (the D374 inherited-virtual precedent).
    std::string FullName() const override { return baseMember_->FullName(); }
    std::string Name() const override { return baseMember_->Name(); }
    std::string Namespace() const override { return baseMember_->Namespace(); }
    std::string ReflectionName() const override { return baseMember_->ReflectionName(); }

    // The C# `ICompilation Compilation => baseMember.Compilation` /
    // `IModule ParentModule => baseMember.ParentModule`.
    const ICompilation& Compilation() const override { return baseMember_->Compilation(); }
    const IModule* ParentModule() const override { return baseMember_->ParentModule(); }

protected:
    // The C# `protected SpecializedMember(IMember memberDefinition)`. Asserts non-null
    // and not-a-`SpecializedMember`; initializes `substitution_` to `Identity`. The port
    // takes an OWNING `shared_ptr<IMember>` (the base member is held by value). Out-of-line.
    explicit SpecializedMember(std::shared_ptr<IMember> memberDefinition);

    // The C# `protected void AddSubstitution(TypeParameterSubstitution newSubstitution)`
    // -- composes `newSubstitution` with the current `substitution_` (function
    // composition: the new substitution applied AFTER the existing). Constructor-only
    // (the C# `Debug.Assert(declaringType == null)`). Out-of-line (Compose).
    void AddSubstitution(TypeParameterSubstitution newSubstitution);

    // The wrapped base member (owning). `protected` so the derived concrete leaves can
    // read it (e.g. `SpecializedMethod` reads `methodDefinition.ReturnTypeIsRefReadOnly`).
    std::shared_ptr<IMember> baseMember_;

private:
    // `mutable`: the `const` lazy accessors (`ReturnType` / `DeclaringType`) pass
    // `substitution_` as a non-const `TypeVisitor&` to `AcceptVisitor` (the D406
    // non-const-`TypeVisitor` convention), and lazily write `returnType_` / `declaringType_`.
    mutable TypeParameterSubstitution substitution_;
    mutable ITypePtr returnType_;
    mutable ITypePtr declaringType_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
