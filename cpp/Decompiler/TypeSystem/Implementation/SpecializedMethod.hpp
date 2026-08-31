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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedMethod.cs -- the
// concrete `IMethod` a `GetMembersHelper.GetMethodsImpl` builds for a method on a
// parameterized type (`new SpecializedMethod(m, pt.GetSubstitution(methodTypeArguments))`).
// It derives `SpecializedParameterizedMember, IMethod`: the `SpecializedParameterizedMember`
// base supplies the substituted `Parameters` list (lazily) + the substituted `ReturnType` /
// `DeclaringType` / `Substitution` + the delegated `IMember` surface; this class adds the
// `IMethod`-own surface + the method-type-parameter specialization machinery (a per-base-
// type-parameter `SpecializedTypeParameter` array + the
// `substitutionWithoutSpecializedTypeParameters` the `Equals` / `GetHashCode` / `Specialize`
// use to avoid double-counting the specialized type parameters). The nested
// `SpecializedTypeParameter : AbstractTypeParameter` (D487) delegates the constraint surface
// to a base type parameter and re-computes `TypeConstraints` through the method's substitution.
//
// KEY PORT CONVENTIONS:
//  (a) THE THREE-IMember-SUBOBJECT DIAMOND (the `SpecializedProperty` D486 pattern):
//      `SpecializedMethod : SpecializedParameterizedMember, IMethod` where
//      `SpecializedParameterizedMember : SpecializedMember, IParameterizedMember` and
//      `IMethod : IParameterizedMember`. THREE `IMember` subobjects: `SpecializedMember`'s
//      (sub A), the `IParameterizedMember` inside `SpecializedParameterizedMember` (sub B),
//      and `IParameterizedMember` inside `IMethod` (sub C -- `IMethod`'s OWN
//      `IParameterizedMember`). The `SpecializedProperty` pattern: ONE delegating override
//      per method name (each calls `SpecializedMember::` qualified, sub A's override).
//      `Parameters()` IS redeclared (sub C's `IParameterizedMember::Parameters()` is a
//      separate pure-virtual), delegating to `SpecializedParameterizedMember::Parameters()`.
//  (b) THE COVARIANT `IMethod::Specialize`: `IMethod` redeclares `Specialize` with a covariant
//      `const IMethod*` return (overriding `IMember::Specialize`'s `const IMember*`). A SINGLE
//      `SpecializedMethod::Specialize` override with the covariant `const IMethod*` return is
//      the final overrider for ALL THREE `IMember::Specialize` subobjects + `IMethod::Specialize`
//      (the standard C++ rule: one derived function overrides every matching base virtual across
//      all base subobjects; the covariant return is valid because `IMethod` derives `IMember`).
//      It delegates to `methodDefinition_->Specialize(Compose(newSub,
//      substitutionWithoutSpecializedTypeParameters_))` (the C# `IMethod.Specialize`).
//  (c) The C# `readonly IMethod methodDefinition` (held alongside `baseMember`) ports to an
//      OWNING `std::shared_ptr<IMethod> methodDefinition_` (shared with `baseMember_`; the
//      `SpecializedProperty::propertyDefinition_` precedent). The ctor takes a
//      `shared_ptr<IMethod>` and passes it to `SpecializedParameterizedMember` (upcast to
//      `shared_ptr<IParameterizedMember>`) AND stores it as `methodDefinition_`.
//  (d) THE SPECIALIZED-TYPE-PARAMETER MACHINERY (the crux): the C# ctor builds a
//      `specializedTypeParameters` array (one `SpecializedTypeParameter` per base method type
//      parameter) when `methodDefinition.TypeParameters.Count > 0`, and manages a
//      `substitutionWithoutSpecializedTypeParameters` field (the substitution WITHOUT the
//      specialized type parameters) that `Equals` / `GetHashCode` / `Specialize` use. The
//      `isParameterized` flag is `substitution.MethodTypeArguments != null` (the C# `!= null`;
//      the port's `std::optional::has_value()` -- a present optional, even empty, is
//      "parameterized"). The dance:
//        - if `!isParameterized`: record the current `substitution_` (Identity) as
//          `substitutionWithoutSpecializedTypeParameters_`, then `AddSubstitution(
//          TypeParameterSubstitution(nullopt, specializedTypeParameters))` (substitutes the
//          base method's type parameters with the specialized ones).
//        - `AddSubstitution(substitution)` (the main).
//        - if `!isParameterized`: `substitutionWithoutSpecializedTypeParameters_ =
//          Compose(substitution, substitutionWithoutSpecializedTypeParameters_)` (the main
//          composed with Identity -- the main WITHOUT the specialized type params).
//        - else: `substitutionWithoutSpecializedTypeParameters_ = this->Substitution()` (the
//          whole substitution -- no specialized type params were added).
//        - set each `SpecializedTypeParameter`'s `substitution` to `this->Substitution()` (a
//          pointer to `&substitution_`; stable for the `SpecializedMethod`'s lifetime -- the
//          method owns the `specializedTypeParameters_` which own the `SpecializedTypeParameter`s).
//      The `substitutionWithoutSpecializedTypeParameters_` is a by-value
//      `TypeParameterSubstitution` (the D407 convention); `specializedTypeParameters_` is an
//      owning `std::vector<std::shared_ptr<ITypeParameter>>`.
//  (e) `Equals(IMember*, TypeVisitor*)` / `Equals(const SpecializedMember*)` / `GetHashCode()`
//      OVERRIDE the `SpecializedMember` versions using `substitutionWithoutSpecializedTypeParameters_`
//      instead of `substitution_` (the C# compares `baseMember` + `substitutionWithoutSpecializedTypeParameters`).
//      The `SpecializedMember` versions using `substitution_` are hidden (a `using` would be
//      ambiguous; the overrides here are the final overriders for all three `IMember` subobjects).
//  (f) `TypeArguments()` returns `this->Substitution()->MethodTypeArguments()` (the optional)
//      or an empty vector (the C# `?? EmptyList<IType>.Instance`). `TypeParameters()` returns a
//      non-owning snapshot of `specializedTypeParameters_` (if non-empty) or the base method's
//      `TypeParameters()` (the C# `specializedTypeParameters ?? methodDefinition.TypeParameters`).
//  (g) The `IMethod`-own surface (`GetReturnTypeAttributes` / `ReturnTypeIsRefReadOnly` /
//      `ThisIsRefReadOnly` / `IsInitOnly` / `IsExtensionMethod` / `IsLocalFunction` /
//      `IsConstructor` / `IsDestructor` / `IsOperator` / `HasBody` / `IsAccessor` /
//      `AccessorKind` / `ReducedFrom`) delegate to `methodDefinition_` verbatim (`ReducedFrom`
//      returns `nullptr` -- the C# `=> null`). `AccessorOwner` is DEFERRED (the C# lazy
//      `LazyInit` builds a `SpecializedMethod` for the accessor via `ownerDefinition.Specialize(
//      this.Substitution)` -- the owning-`Specialize` design; the override returns
//      `methodDefinition_->AccessorOwner()` unspecialized, the `SpecializedProperty::Getter`
//      deferral precedent). `ToString` is DEFERRED (needs `IType::ToString`). The C# `internal
//      static IMethod Create(...)` factory is DEFERRED (needs the owning-`Specialize` design).
//  (h) OUT-OF-LINE: the ctor + `Equals` / `GetHashCode` / `Specialize` (covariant) /
//      `TypeArguments` / `TypeParameters` are in the .cpp (they need `TypeParameterSubstitution`
//      / `IMethod` / `SpecializedTypeParameter` complete); the class is added to the ilspy
//      `CMakeLists.txt`. The trivial `IMethod`-own delegations + the three-`IMember` diamond
//      overrides are inline.
//  (i) The nested `SpecializedTypeParameter : AbstractTypeParameter` (D487) -- a `final` class
//      co-located here (the C# nests it in `SpecializedMethod`). It holds the base type
//      parameter (`baseTp_`, non-owning -- the `methodDefinition_` owns the base) + a `const TypeParameterSubstitution* substitution_` (set
//      by the `SpecializedMethod` ctor after construction, a `friend`; non-owning, points to
//      the `SpecializedMethod`'s `substitution_`). The C# `Equals(IType)` (compare `baseTp` +
//      `Owner`, NOT `substitution` -- the substitution may contain this type parameter
//      recursively) ports to `StructuralEquals`; `GetHashCode` (`baseTp.GetHashCode() ^
//      Owner.GetHashCode()`) is a plain member (the `AbstractTypeParameter::GetHashCode`
//      plain-member precedent, hidden). `TypeConstraints` lazy-computes through `substitution_`
//      (each `c.Type.AcceptVisitor(substitution)` -- `const_cast` for the non-const
//      `TypeVisitor&`, the D482/D484 precedent). DEFERRED from the C# `SpecializedTypeParameter`:
//      none material (the `TypeConstraints` lazy IS ported -- it's the crux).

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/AbstractTypeParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedParameterizedMember.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// Forward declaration (defined below -- the `SpecializedMethod` ctor needs it complete).
class SpecializedMethod;

// Port of the C# `sealed class SpecializedTypeParameter : AbstractTypeParameter` (nested in
// `SpecializedMethod`). A specialized method type parameter: delegates the constraint surface
// to a base type parameter, re-computes `TypeConstraints` through the method's substitution.
// `final` (the C# `sealed`). Co-located here (the C# nests it).
class SpecializedTypeParameter final : public AbstractTypeParameter {
public:
    // The C# `SpecializedTypeParameter(ITypeParameter baseTp, IMethod specializedOwner) :
    // base(specializedOwner, baseTp.Index, baseTp.Name, baseTp.Variance)`. `baseTp` is a
    // NON-OWNING non-null pointer (the C# reference; the `methodDefinition` owns the base
    // type parameter -- the `SpecializedMethod` holds `methodDefinition_` owning, so the base
    // type parameters outlive the `SpecializedTypeParameter`s in the
    // `specializedTypeParameters_` cache; the D480 non-owning-`baseParameter` precedent);
    // `specializedOwner` is the owning `SpecializedMethod` (a non-owning `const IMethod*`).
    SpecializedTypeParameter(const ITypeParameter* baseTp, const IMethod* specializedOwner)
        : AbstractTypeParameter(specializedOwner, baseTp->Index(), baseTp->Name(), baseTp->Variance()),
          baseTp_(baseTp) {}

    // The C# `override IEnumerable<IAttribute> GetAttributes() => baseTp.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override { return baseTp_->GetAttributes(); }

    // The C# `override bool HasValueTypeConstraint` / `HasReferenceTypeConstraint` /
    // `HasDefaultConstructorConstraint` / `HasUnmanagedConstraint` / `AllowsRefLikeType` =>
    // `baseTp.*`.
    bool HasValueTypeConstraint() const override { return baseTp_->HasValueTypeConstraint(); }
    bool HasReferenceTypeConstraint() const override { return baseTp_->HasReferenceTypeConstraint(); }
    bool HasDefaultConstructorConstraint() const override {
        return baseTp_->HasDefaultConstructorConstraint();
    }
    bool HasUnmanagedConstraint() const override { return baseTp_->HasUnmanagedConstraint(); }
    bool AllowsRefLikeType() const override { return baseTp_->AllowsRefLikeType(); }

    // The C# `override Nullability NullabilityConstraint => baseTp.NullabilityConstraint`.
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override {
        return baseTp_->NullabilityConstraint();
    }

    // The C# `override IReadOnlyList<TypeConstraint> TypeConstraints` -- lazily
    // `baseTp.TypeConstraints.SelectReadOnlyArray(c => new TypeConstraint(c.Type.AcceptVisitor(
    // substitution), c.Attributes))`. Cached in `mutable typeConstraintsCache_`. Out-of-line
    // (needs `TypeParameterSubstitution` complete for the `const_cast` + `TypeConstraint`).
    std::vector<TypeConstraint> TypeConstraints() const override;

    // The C# `override int GetHashCode() => baseTp.GetHashCode() ^ this.Owner.GetHashCode()`.
    // A plain member (the `AbstractTypeParameter::GetHashCode` plain-member precedent; the C#
    // `override` hides it -- a hiding plain member in C++). The identity hashes: `baseTp_`
    // (the base type-parameter pointer) ^ `this->Owner()` (the entity pointer identity).
    int GetHashCode() const {
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(baseTp_) ^
                                reinterpret_cast<std::uintptr_t>(this->Owner()));
    }

protected:
    // The C# `override bool Equals(IType other) => other is SpecializedTypeParameter o &&
    // baseTp.Equals(o.baseTp) && this.Owner.Equals(o.Owner)`. Compare `baseTp` + `Owner` (NOT
    // `substitution` -- the substitution may contain this type parameter recursively).
    bool StructuralEquals(const IType& other) const override;

private:
    // Allow the `SpecializedMethod` ctor to set `substitution_` (the C# `internal TypeVisitor
    // substitution` is set at the end of the `SpecializedMethod` ctor).
    friend class SpecializedMethod;

    const ITypeParameter* baseTp_;  // non-owning (the `methodDefinition_` owns the base)
    // The substitution to apply to `TypeConstraints`. Set by the `SpecializedMethod` ctor to a
    // pointer to the `SpecializedMethod`'s `substitution_` (stable for the method's lifetime;
    // the method owns the `specializedTypeParameters_` which own this). `mutable` so the `const`
    // `TypeConstraints` lazy can `const_cast` it to a non-const `TypeVisitor&` (the D406
    // non-const-`TypeVisitor` convention -- a substitution does not mutate in `AcceptVisitor`).
    mutable const TypeParameterSubstitution* substitution_ = nullptr;
    mutable std::vector<TypeConstraint> typeConstraintsCache_;
};

// A specialized method (see the header comment). Derives `SpecializedParameterizedMember, IMethod`;
// the three-`IMember`-subobject diamond is resolved by one override per method name delegating
// to the `SpecializedMember::` qualified call (sub A's override). The complex members are
// out-of-line in the .cpp. NOT `final`: the C# class is unsealed (`public class SpecializedMethod`),
// and `CSharpOperators.LiftedUserDefinedOperator` (CSharp/Resolver/CSharpOperators.hpp) derives
// from it -- the C# `sealed class LiftedUserDefinedOperator : SpecializedMethod, ILiftedOperator`.
class SpecializedMethod : public SpecializedParameterizedMember, public IMethod {
public:
    // The C# `SpecializedMethod(IMethod methodDefinition, TypeParameterSubstitution substitution)`.
    // `methodDefinition` is shared with the `SpecializedParameterizedMember` base (its
    // `baseMember_`, upcast to `IParameterizedMember`) AND stored as the typed
    // `methodDefinition_`. Out-of-line (the specialized-type-parameter machinery).
    SpecializedMethod(std::shared_ptr<IMethod> methodDefinition,
                      TypeParameterSubstitution substitution);

    // --- ISymbol (the single override is the final overrider for all three IMember subobjects;
    //     IMethod does NOT redeclare Name/SymbolKind, but the three-IMember diamond makes them
    //     ambiguous without these) ---

    std::string Name() const override { return SpecializedMember::Name(); }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return SpecializedMember::SymbolKind();
    }

    // --- INamedElement (delegated to sub A) ---

    std::string FullName() const override { return SpecializedMember::FullName(); }
    std::string ReflectionName() const override { return SpecializedMember::ReflectionName(); }
    std::string Namespace() const override { return SpecializedMember::Namespace(); }

    // --- ICompilationProvider (delegated to sub A) ---

    const ICompilation& Compilation() const override { return SpecializedMember::Compilation(); }

    // --- IEntity (delegated to sub A) ---

    std::uint32_t MetadataToken() const override { return SpecializedMember::MetadataToken(); }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return SpecializedMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override { return SpecializedMember::DeclaringType(); }
    const IModule* ParentModule() const override { return SpecializedMember::ParentModule(); }
    std::vector<const IAttribute*> GetAttributes() const override {
        return SpecializedMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return SpecializedMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return SpecializedMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return SpecializedMember::Accessibility();
    }
    bool IsStatic() const override { return SpecializedMember::IsStatic(); }
    bool IsAbstract() const override { return SpecializedMember::IsAbstract(); }
    bool IsSealed() const override { return SpecializedMember::IsSealed(); }

    // --- IMember (delegated to sub A, EXCEPT Specialize/Equals which are overridden with the
    //     substitutionWithoutSpecializedTypeParameters -- see the header comment (b)/(e)) ---

    const IMember* MemberDefinition() const override { return SpecializedMember::MemberDefinition(); }
    const IType& ReturnType() const override { return SpecializedMember::ReturnType(); }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return SpecializedMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return SpecializedMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return SpecializedMember::IsVirtual(); }
    bool IsOverride() const override { return SpecializedMember::IsOverride(); }
    bool IsOverridable() const override { return SpecializedMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return SpecializedMember::Substitution();
    }
    // The C# `override IMember Specialize` / `IMethod IMethod.Specialize` -- the COVARIANT
    // `const IMethod*` override is the final overrider for all three `IMember::Specialize` +
    // `IMethod::Specialize` (see the header comment (b)). Out-of-line (uses
    // `substitutionWithoutSpecializedTypeParameters_`).
    const IMethod* Specialize(const TypeParameterSubstitution* newSubstitution) const override;
    // `Equals` / `GetHashCode` use `substitutionWithoutSpecializedTypeParameters_` (the C#
    // `Equals` compares `baseMember` + `substitutionWithoutSpecializedTypeParameters`).
    // Out-of-line.
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override;
    // `GetHashCode` -- a plain hiding member (the port has no virtual `GetHashCode`; the C#
    // `override int GetHashCode()` has no C++ counterpart -- the `SpecializedMember::
    // GetHashCode` / `AbstractTypeParameter::GetHashCode` plain-member precedent). Out-of-line.
    int GetHashCode() const;

    // --- IParameterizedMember (Parameters() redeclared -- sub C's IParameterizedMember is a
    //     separate subobject; delegates to the inherited base) ---

    std::vector<const IParameter*> Parameters() const override {
        return SpecializedParameterizedMember::Parameters();
    }

    // --- IMethod (the C# `bool IsExtensionMethod` / `IsLocalFunction` / `IsConstructor` /
    //     `IsDestructor` / `IsOperator` / `HasBody` / `IsAccessor` / `ReturnTypeIsRefReadOnly` /
    //     `ThisIsRefReadOnly` / `IsInitOnly` => methodDefinition.*`; `GetReturnTypeAttributes`
    //     => methodDefinition.GetReturnTypeAttributes()`; `AccessorKind =>
    //     methodDefinition.AccessorKind()`; `ReducedFrom => null`) ---

    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return methodDefinition_->GetReturnTypeAttributes();
    }
    bool ReturnTypeIsRefReadOnly() const override { return methodDefinition_->ReturnTypeIsRefReadOnly(); }
    bool IsInitOnly() const override { return methodDefinition_->IsInitOnly(); }
    bool ThisIsRefReadOnly() const override { return methodDefinition_->ThisIsRefReadOnly(); }
    // `TypeParameters` / `TypeArguments` -- out-of-line (the specialized-type-parameter
    // machinery; see the header comment (f)).
    std::vector<const ITypeParameter*> TypeParameters() const override;
    std::vector<ITypePtr> TypeArguments() const override;
    bool IsExtensionMethod() const override { return methodDefinition_->IsExtensionMethod(); }
    bool IsLocalFunction() const override { return methodDefinition_->IsLocalFunction(); }
    bool IsConstructor() const override { return methodDefinition_->IsConstructor(); }
    bool IsDestructor() const override { return methodDefinition_->IsDestructor(); }
    bool IsOperator() const override { return methodDefinition_->IsOperator(); }
    bool HasBody() const override { return methodDefinition_->HasBody(); }
    bool IsAccessor() const override { return methodDefinition_->IsAccessor(); }
    // `AccessorOwner` -- DEFERRED (the C# lazy `LazyInit` owning-`Specialize` design; returns the
    // base accessor owner unspecialized). See the header comment (g).
    const IMember* AccessorOwner() const override { return methodDefinition_->AccessorOwner(); }
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind() const override {
        return methodDefinition_->AccessorKind();
    }
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    std::shared_ptr<IMethod> methodDefinition_;
    // The C# `readonly ITypeParameter[] specializedTypeParameters` -- owning, one
    // `SpecializedTypeParameter` per base method type parameter (empty if the base method is
    // not generic).
    std::vector<std::shared_ptr<ITypeParameter>> specializedTypeParameters_;
    // The C# `readonly bool isParameterized` -- `substitution.MethodTypeArguments != null`.
    bool isParameterized_;
    // The C# `readonly TypeParameterSubstitution substitutionWithoutSpecializedTypeParameters` --
    // the substitution WITHOUT the specialized type parameters (by-value; the D407 convention).
    TypeParameterSubstitution substitutionWithoutSpecializedTypeParameters_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
