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

// Out-of-line members of `SpecializedMethod` / `SpecializedTypeParameter` (the header comment
// in `SpecializedMethod.hpp` documents the port conventions + deferrals).

#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `internal static IMethod Create(IMethod methodDefinition,
// TypeParameterSubstitution substitution)` (SpecializedMethod.cs lines 34-49) -- the factory
// every `IMethod::Specialize` implementation routes through. The ownership design: the
// Identity / declaring-tpc-0 arms return the caller-supplied handle back (an alias over the
// caller-owned instance), the ArrayType-declaring-type and general arms a fresh owning
// `SpecializedMethod`.
std::shared_ptr<IMethod> SpecializedMethod::Create(
    std::shared_ptr<IMethod> methodDefinition,
    TypeParameterSubstitution substitution)
{
    // The C# `if (TypeParameterSubstitution.Identity.Equals(substitution))
    // return methodDefinition;` -- evaluated FIRST, so a null declaring type never
    // reaches the reads below.
    if (TypeParameterSubstitution::Identity().Equals(&substitution)) {
        return methodDefinition;
    }
    // The C# `if (methodDefinition.DeclaringType is ArrayType) return new
    // SpecializedMethod(methodDefinition, substitution);` -- `DeclaringType()` returns the
    // nullable `ITypePtr` handle; the `dynamic_cast` over a null handle is the C# `is` over
    // null (false), and a null declaring type NREs at the reads below (mapped to the .NET
    // message).
    ITypePtr declaringType = methodDefinition->DeclaringType();
    if (declaringType == nullptr) {
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
    }
    if (dynamic_cast<const ArrayType*>(declaringType.get()) != nullptr) {
        return std::make_shared<SpecializedMethod>(std::move(methodDefinition),
                                                   std::move(substitution));
    }
    // The C# `if (methodDefinition.TypeParameters.Count == 0) { ... }`.
    if (methodDefinition->TypeParameters().empty()) {
        if (declaringType->TypeParameterCount() == 0) {
            return methodDefinition;
        }
        // The C# `if (substitution.MethodTypeArguments != null &&
        // substitution.MethodTypeArguments.Count > 0) substitution = new
        // TypeParameterSubstitution(substitution.ClassTypeArguments, EmptyList<IType>.Instance);`
        // -- the method type args on a non-generic method are DROPPED (replaced by the EMPTY
        // list, which the substitution render spells `[]`).
        const auto& methodArgs = substitution.MethodTypeArguments();
        if (methodArgs.has_value() && !methodArgs->empty()) {
            substitution = TypeParameterSubstitution(substitution.ClassTypeArguments(),
                                                     std::vector<ITypePtr>{});
        }
    }
    return std::make_shared<SpecializedMethod>(std::move(methodDefinition),
                                               std::move(substitution));
}

// The C# `SpecializedMethod(IMethod methodDefinition, TypeParameterSubstitution substitution)`.
// The specialized-type-parameter machinery (see the header comment (d)).
SpecializedMethod::SpecializedMethod(std::shared_ptr<IMethod> methodDefinition,
                                     TypeParameterSubstitution substitution)
    : SpecializedParameterizedMember(methodDefinition),  // upcast -> IParameterizedMember
      methodDefinition_(std::move(methodDefinition)),
      isParameterized_(substitution.MethodTypeArguments().has_value()),
      substitutionWithoutSpecializedTypeParameters_(std::nullopt, std::nullopt) {
    // Keep a copy of `substitution` for the `Compose` below (the C# passes it by value, so it
    // is still available after `AddSubstitution`; the port's `AddSubstitution` moves, so copy).
    TypeParameterSubstitution substitutionCopy = substitution;
    // The C# `if (methodDefinition.TypeParameters.Count > 0)` -- build the specialized type
    // parameters (one `SpecializedTypeParameter` per base method type parameter).
    auto baseTypeParameters = methodDefinition_->TypeParameters();  // non-owning snapshot
    if (!baseTypeParameters.empty()) {
        specializedTypeParameters_.reserve(baseTypeParameters.size());
        for (const ITypeParameter* baseTp : baseTypeParameters) {
            specializedTypeParameters_.push_back(
                std::make_shared<SpecializedTypeParameter>(baseTp,
                                                          static_cast<const IMethod*>(this)));
        }
        if (!isParameterized_) {
            // The C# `substitutionWithoutSpecializedTypeParameters = this.Substitution` (Identity
            // at this point -- before any AddSubstitution); then `AddSubstitution(new
            // TypeParameterSubstitution(null, specializedTypeParameters))` (substitutes the base
            // method's type parameters with the specialized ones).
            substitutionWithoutSpecializedTypeParameters_ = *Substitution();
            AddSubstitution(TypeParameterSubstitution(
                std::nullopt,
                std::optional<std::vector<ITypePtr>>(  // the specialized type parameters as
                    std::vector<ITypePtr>(specializedTypeParameters_.begin(),
                                          specializedTypeParameters_.end()))));
        }
    }
    // The C# `AddSubstitution(substitution)` (the main).
    AddSubstitution(std::move(substitution));
    // The C# `if (substitutionWithoutSpecializedTypeParameters != null) ... else ...`:
    if (!isParameterized_ && !specializedTypeParameters_.empty()) {
        // `substitutionWithoutSpecializedTypeParameters = Compose(substitution,
        // substitutionWithoutSpecializedTypeParameters)` -- the main composed with Identity
        // (the main WITHOUT the specialized type params). Uses the copy (the original was moved).
        substitutionWithoutSpecializedTypeParameters_ = TypeParameterSubstitution::Compose(
            &substitutionCopy, &substitutionWithoutSpecializedTypeParameters_);
    } else {
        // Otherwise just use the whole substitution (no specialized type params in this case).
        substitutionWithoutSpecializedTypeParameters_ = *Substitution();
    }
    // The C# `foreach (var tp in specializedTypeParameters.OfType<SpecializedTypeParameter>())
    // { if (tp.Owner == this) tp.substitution = base.Substitution; }` -- set each specialized
    // type parameter's `substitution` to a pointer to the final composed `substitution_`. All
    // were built with `this` as the owner, so the `tp.Owner == this` guard is always true
    // (dropped -- avoids the ambiguous `SpecializedMethod*` -> `IEntity*` upcast the three-`IMember`
    // diamond would force).
    const TypeParameterSubstitution* finalSubstitution = Substitution();
    for (const auto& tp : specializedTypeParameters_) {
        auto* stp = static_cast<SpecializedTypeParameter*>(tp.get());
        stp->substitution_ = finalSubstitution;
    }
}

// The C# `IReadOnlyList<ITypeParameter> TypeParameters => specializedTypeParameters ??
// methodDefinition.TypeParameters`. Returns a non-owning snapshot.
std::vector<const ITypeParameter*> SpecializedMethod::TypeParameters() const {
    if (!specializedTypeParameters_.empty()) {
        std::vector<const ITypeParameter*> result;
        result.reserve(specializedTypeParameters_.size());
        for (const auto& tp : specializedTypeParameters_) {
            result.push_back(tp.get());
        }
        return result;
    }
    return methodDefinition_->TypeParameters();
}

// The C# `IReadOnlyList<IType> TypeArguments => this.Substitution.MethodTypeArguments ??
// EmptyList<IType>.Instance`.
std::vector<ITypePtr> SpecializedMethod::TypeArguments() const {
    const auto& methodArgs = Substitution()->MethodTypeArguments();
    if (methodArgs.has_value()) {
        return *methodArgs;
    }
    return {};
}

// The C# `override IMember Specialize` / `IMethod IMethod.Specialize` -- the covariant override;
// delegates to `methodDefinition.Specialize(Compose(newSubstitution,
// substitutionWithoutSpecializedTypeParameters))`.
const IMethod* SpecializedMethod::Specialize(const TypeParameterSubstitution* newSubstitution) const {
    TypeParameterSubstitution newSub = (newSubstitution != nullptr)
        ? *newSubstitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    TypeParameterSubstitution composed = TypeParameterSubstitution::Compose(
        &newSub, const_cast<TypeParameterSubstitution*>(&substitutionWithoutSpecializedTypeParameters_));
    return methodDefinition_->Specialize(&composed);
}

// The C# `override bool Equals(IMember obj, TypeVisitor typeNormalization)` -- compares
// `baseMember` + `substitutionWithoutSpecializedTypeParameters` (under the normalization).
bool SpecializedMethod::Equals(const IMember* obj, const TypeVisitor* typeNormalization) const {
    const auto* other = dynamic_cast<const SpecializedMethod*>(obj);
    if (other == nullptr) {
        return false;
    }
    if (!methodDefinition_->Equals(other->methodDefinition_.get(), typeNormalization)) {
        return false;
    }
    if (typeNormalization != nullptr) {
        return substitutionWithoutSpecializedTypeParameters_.Equals(
            &other->substitutionWithoutSpecializedTypeParameters_,
            *const_cast<TypeVisitor*>(typeNormalization));
    }
    return substitutionWithoutSpecializedTypeParameters_.Equals(
        &other->substitutionWithoutSpecializedTypeParameters_);
}

// The C# `override int GetHashCode() => 1000000013 * baseMember.GetHashCode() +
// 1000000009 * substitutionWithoutSpecializedTypeParameters.GetHashCode()` (the `unchecked`
// wraparound). `baseMember.GetHashCode()` is the identity hash (no `IMember` override) ->
// pointer identity; `substitutionWithoutSpecializedTypeParameters.GetHashCode()` is the
// `TypeParameterSubstitution::GetHashCode` (the D407 value-semantic hash).
int SpecializedMethod::GetHashCode() const {
    unsigned int baseHash = static_cast<unsigned int>(
        reinterpret_cast<std::uintptr_t>(methodDefinition_.get()));
    unsigned int subHash = static_cast<unsigned int>(
        substitutionWithoutSpecializedTypeParameters_.GetHashCode());
    unsigned int h = 1000000013u * baseHash + 1000000009u * subHash;
    return static_cast<int>(h);
}

// ---- SpecializedTypeParameter ----

// The C# `override bool Equals(IType other) => other is SpecializedTypeParameter o &&
// baseTp.Equals(o.baseTp) && this.Owner.Equals(o.Owner)`. Compare `baseTp` + `Owner` (NOT
// `substitution` -- the substitution may contain this type parameter recursively).
bool SpecializedTypeParameter::StructuralEquals(const IType& other) const {
    const auto* o = dynamic_cast<const SpecializedTypeParameter*>(&other);
    if (o == nullptr) {
        return false;
    }
    // `baseTp.Equals(o.baseTp)` -- `IType::Equals(const IType&)` (non-virtual -> StructuralEquals).
    return baseTp_->Equals(*o->baseTp_) && this->Owner() == o->Owner();
}

// The C# `override IReadOnlyList<TypeConstraint> TypeConstraints` -- lazily
// `baseTp.TypeConstraints.SelectReadOnlyArray(c => new TypeConstraint(c.Type.AcceptVisitor(
// substitution), c.Attributes))`. Cached in `mutable typeConstraintsCache_`.
std::vector<TypeConstraint> SpecializedTypeParameter::TypeConstraints() const {
    if (!typeConstraintsCache_.empty()) {
        return typeConstraintsCache_;
    }
    std::vector<TypeConstraint> result;
    auto baseConstraints = baseTp_->TypeConstraints();
    result.reserve(baseConstraints.size());
    for (const auto& c : baseConstraints) {
        ITypePtr substitutedType;
        if (c.Type() && substitution_) {
            // `c.Type.AcceptVisitor(substitution)` -- `AcceptVisitor` is non-const (the D406
            // convention); `const_cast` both the `IType&` and the `TypeVisitor&` (a substitution
            // does not mutate in `AcceptVisitor` reads; the D482/D484 precedent).
            substitutedType = const_cast<IType&>(*c.Type()).AcceptVisitor(
                const_cast<TypeParameterSubstitution&>(*substitution_));
        } else {
            substitutedType = c.Type();
        }
        result.emplace_back(std::move(substitutedType), c.Attributes());
    }
    typeConstraintsCache_ = result;
    return typeConstraintsCache_;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
