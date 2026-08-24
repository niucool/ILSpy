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

// Out-of-line members of `SpecializedMember` (the header comment in
// `SpecializedMember.hpp` documents the port conventions + deferrals).

#include "Decompiler/TypeSystem/Implementation/SpecializedMember.hpp"

#include <cstdint>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `protected SpecializedMember(IMember memberDefinition)`. Asserts non-null and
// not-a-`SpecializedMember`; initializes `substitution_` to `Identity` (both lists
// absent). The port checks BEFORE moving the base member into `baseMember_` (the C#
// throws before assigning; the port mirrors that to avoid holding a null base).
SpecializedMember::SpecializedMember(std::shared_ptr<IMember> memberDefinition)
    : substitution_(TypeParameterSubstitution(std::nullopt, std::nullopt)) {
    if (!memberDefinition) {
        throw std::invalid_argument(
            "SpecializedMember: member definition must not be null");
    }
    if (dynamic_cast<const SpecializedMember*>(memberDefinition.get()) != nullptr) {
        throw std::invalid_argument(
            "SpecializedMember: member definition cannot be specialized; "
            "use IMember::Specialize instead of directly constructing SpecializedMember instances");
    }
    baseMember_ = std::move(memberDefinition);
}

// The C# `protected void AddSubstitution(TypeParameterSubstitution newSubstitution)` --
// `this.substitution = TypeParameterSubstitution.Compose(newSubstitution, this.substitution)`
// (function composition: the new substitution applied AFTER the existing). Constructor-only.
void SpecializedMember::AddSubstitution(TypeParameterSubstitution newSubstitution) {
    substitution_ = TypeParameterSubstitution::Compose(&newSubstitution, &substitution_);
}

// The C# `IType DeclaringType` -- lazily re-parameterized / substituted, cached.
// `LazyInit.VolatileRead` / `GetOrSet` port to the free functions in `ILSpy::Decompiler::Util`
// (the C# `static class LazyInit` -> a namespace of free functions); qualified `Util::` from
// the `TypeSystem::Implementation` context. The `mutable ITypePtr declaringType_` field is
// the atomic storage.
ITypePtr SpecializedMember::DeclaringType() const {
    auto result = Util::VolatileRead(&declaringType_);
    if (result) {
        return result;
    }
    ITypePtr definitionDeclaringType = baseMember_->DeclaringType();
    if (definitionDeclaringType) {
        const ITypeDefinition* definitionDeclaringTypeDef =
            dynamic_cast<const ITypeDefinition*>(definitionDeclaringType.get());
        if (definitionDeclaringTypeDef != nullptr &&
            definitionDeclaringType->TypeParameterCount() > 0) {
            const auto& classArgs = substitution_.ClassTypeArguments();
            if (classArgs.has_value() &&
                static_cast<int>(classArgs->size()) == definitionDeclaringType->TypeParameterCount()) {
                // C# arm 1: `new ParameterizedType(definitionDeclaringTypeDef, substitution.ClassTypeArguments)`.
                // The port passes the OWNING `definitionDeclaringType` (the `ITypeDefinition`
                // viewed as `IType` -- it IS-an `IType` via `ITypeDefinitionOrUnknown`, no cast
                // needed; the `ParameterizedType` ctor takes `ITypePtr`).
                result = std::make_shared<ParameterizedType>(definitionDeclaringType, *classArgs);
            } else {
                // C# arm 2 (the `else`): `new ParameterizedType(def, def.TypeParameters).AcceptVisitor(substitution)`.
                // DEFERRED: the port's `IType::TypeParameters()` returns NON-OWNING `const
                // ITypeParameter*`, but the `ParameterizedType` ctor needs OWNING `ITypePtr`. Fall
                // through to the `AcceptVisitor` arm (a reasonable fallback that substitutes what it
                // can). The `GetMembersHelper` routing always supplies matching class args (arm 1),
                // so this arm is never hit there. See the header comment (f).
                result = definitionDeclaringType->AcceptVisitor(substitution_);
            }
        } else {
            // C# arm 3 (`else if definitionDeclaringType != null`):
            // `definitionDeclaringType.AcceptVisitor(substitution)`.
            result = definitionDeclaringType->AcceptVisitor(substitution_);
        }
    }
    // A null `definitionDeclaringType` leaves `result` empty; `GetOrSet` with an empty
    // shared_ptr caches the null (the C# `LazyInit.GetOrSet(ref declaringType, result)` with
    // a null `result` stores null, re-computed next call -- faithful).
    return Util::GetOrSet(&declaringType_, result);
}

// The C# `IType ReturnType` -- lazily `baseMember.ReturnType.AcceptVisitor(substitution)`,
// cached. Returns `const IType&` (the `IMember` contract) -- `*returnType_`.
const IType& SpecializedMember::ReturnType() const {
    auto result = Util::VolatileRead(&returnType_);
    if (result) {
        return *result;
    }
    result = const_cast<IType&>(baseMember_->ReturnType()).AcceptVisitor(substitution_);
    result = Util::GetOrSet(&returnType_, result);
    return *result;
}

// The C# `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers` -- DEFERRED to
// forward the base list UNSPECIALIZED (the C# `Select(m => m.Specialize(substitution))`
// needs the owning-`Specialize` design). The common empty case is faithful. See the
// header comment (g).
std::vector<const IMember*> SpecializedMember::ExplicitlyImplementedInterfaceMembers() const {
    return baseMember_->ExplicitlyImplementedInterfaceMembers();
}

// The C# `virtual IMember Specialize(TypeParameterSubstitution newSubstitution)` --
// `return baseMember.Specialize(TypeParameterSubstitution.Compose(newSubstitution, this.substitution))`.
const IMember* SpecializedMember::Specialize(const TypeParameterSubstitution* newSubstitution) const {
    // `newSubstitution` is nullable (the `IMember::Specialize` test convention); a null
    // pointer is treated as `Identity` (the C# `Specialize` is never-null by value).
    TypeParameterSubstitution newSub = (newSubstitution != nullptr)
        ? *newSubstitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    TypeParameterSubstitution composed = TypeParameterSubstitution::Compose(&newSub, &substitution_);
    return baseMember_->Specialize(&composed);
}

// The C# `virtual bool Equals(IMember obj, TypeVisitor typeNormalization)` -- same base
// member and same substitution (under the normalization) => equal.
bool SpecializedMember::Equals(const IMember* obj, const TypeVisitor* typeNormalization) const {
    const auto* other = dynamic_cast<const SpecializedMember*>(obj);
    if (other == nullptr) {
        return false;
    }
    if (!baseMember_->Equals(other->baseMember_.get(), typeNormalization)) {
        return false;
    }
    if (typeNormalization != nullptr) {
        // `const_cast`: the port's `IMember::Equals` takes a `const TypeVisitor*` (the
        // forward-decl/testability convention), but `TypeParameterSubstitution::Equals` takes
        // a non-const `TypeVisitor&` (the D406 non-const-`TypeVisitor` convention). A real
        // normalizer does not mutate in `Equals` (it only reads its option fields), so the
        // cast is safe. See the header comment (j).
        return substitution_.Equals(&other->substitution_,
                                    *const_cast<TypeVisitor*>(typeNormalization));
    }
    return substitution_.Equals(&other->substitution_);
}

// The C# `override bool Equals(object obj)` -- no C++ `object` base; a standalone (the
// D407 `TypeParameterSubstitution::Equals(const TPS*)` precedent). No normalization.
bool SpecializedMember::Equals(const SpecializedMember* other) const {
    if (other == nullptr) {
        return false;
    }
    return baseMember_->Equals(other->baseMember_.get(), nullptr) &&
           substitution_.Equals(&other->substitution_);
}

// The C# `override int GetHashCode()` -- `unchecked { 1000000007 * baseMember.GetHashCode()
// + 1000000009 * substitution.GetHashCode() }`. `baseMember.GetHashCode()` is the C#
// `object.GetHashCode` identity hash (no `IMember` override) -> port pointer-identity.
int SpecializedMember::GetHashCode() const {
    unsigned int baseHash = static_cast<unsigned int>(
        reinterpret_cast<std::uintptr_t>(baseMember_.get()));
    unsigned int subHash = static_cast<unsigned int>(substitution_.GetHashCode());
    unsigned int h = 1000000007u * baseHash + 1000000009u * subHash;
    return static_cast<int>(h);
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
