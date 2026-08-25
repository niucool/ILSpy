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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/GetMembersHelper.cs. See the header
// for the owning-return model and the bounded mutual recursion with `ParameterizedType`'s
// `ReturnMemberDefinitions` arm (D489).

#include "Decompiler/TypeSystem/Implementation/GetMembersHelper.hpp"

#include "Decompiler/TypeSystem/Implementation/SpecializedEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"  // ParameterizedType (dynamic_cast) + the family Get*
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetNonInterfaceBaseTypes

#include <optional>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation::GetMembersHelper {

namespace {

// The C# `const GetMemberOptions declaredMembers = IgnoreInheritedMembers |
// ReturnMemberDefinitions`. `GetMembersHelper` calls back into `IType.Get*` with this added,
// which (per the D489 `ParameterizedType` arm) routes a `ParameterizedType` to its generic
// definition's declared members and bounds the recursion.
constexpr GetMemberOptions declaredMembers =
    GetMemberOptions::IgnoreInheritedMembers | GetMemberOptions::ReturnMemberDefinitions;

// `(options & ReturnMemberDefinitions) == ReturnMemberDefinitions` -- the definitions arm
// (return the unspecialized members verbatim).
inline bool returningDefinitions(GetMemberOptions options) {
    return (static_cast<std::int32_t>(options) &
            static_cast<std::int32_t>(GetMemberOptions::ReturnMemberDefinitions)) != 0;
}

// `(options & IgnoreInheritedMembers) == IgnoreInheritedMembers` -- the declared-only arm
// (no base-type traversal).
inline bool ignoringInherited(GetMemberOptions options) {
    return (static_cast<std::int32_t>(options) &
            static_cast<std::int32_t>(GetMemberOptions::IgnoreInheritedMembers)) != 0;
}

// The C# `static Predicate<IMethod> FilterTypeParameterCount(int expectedTypeParameterCount)
// => m => m.TypeParameters.Count == expectedTypeParameterCount`.
std::function<bool(const IMethod*)> FilterTypeParameterCount(int expectedTypeParameterCount) {
    return [expectedTypeParameterCount](const IMethod* m) {
        return static_cast<int>(m->TypeParameters().size()) == expectedTypeParameterCount;
    };
}

// The C# `PredicateExtensions.And<T>(Predicate<T> filter1, Predicate<T> filter2)`: both must
// pass; a null filter is "no filter" (passes everything).
template <typename T>
std::function<bool(const T*)> And(std::function<bool(const T*)> a,
                                  std::function<bool(const T*)> b) {
    if (!a) return b;
    if (!b) return a;
    return [a, b](const T* m) { return a(m) && b(m); };
}

// Alias a non-owning `const T*` (from the D477 `IType::Get*` virtuals) into an owning
// `std::shared_ptr<T>` tied to `baseType->shared_from_this()`. The base type (a
// `ParameterizedType` or `ITypeDefinition`) transitively owns its declared members -- a
// `ParameterizedType` owns `genericType_`, which owns the `ITypeDefinition`, which owns the
// members; the aliasing `shared_ptr` keeps `baseType` alive, keeping the members alive. The
// `const_cast` reconciles the port's const-correct `const T*` (D477) with the non-const
// `std::shared_ptr<IMethod>` / `IProperty` / `IField` / `IEvent` the `Specialized*` ctors take
// (the C# `IMethod m` is non-const).
template <typename T>
std::shared_ptr<T> aliasMember(const IType* baseType, const T* m) {
    return std::shared_ptr<T>(baseType->shared_from_this(), const_cast<T*>(m));
}

// `methodTypeArguments` (nullable) -> the `std::optional<std::vector<ITypePtr>>` the port's
// `ParameterizedType::GetSubstitution` / `TypeParameterSubstitution` ctor take (a null pointer
// is the C# `null` -- "keep this kind of type parameter unmodified").
std::optional<std::vector<ITypePtr>> toOptionalArgs(const std::vector<ITypePtr>* args) {
    return args ? std::optional<std::vector<ITypePtr>>(*args) : std::nullopt;
}

// ---- GetMethodsImpl ----
// The C# `GetMethodsImpl(IType baseType, IReadOnlyList<IType> methodTypeArguments,
// Predicate<IMethod> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IMethod>> GetMethodsImpl(
    const IType* baseType,
    const std::vector<ITypePtr>* methodTypeArguments,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {

    std::vector<const IMethod*> declaredMethods =
        baseType->GetMethods(filter, options | declaredMembers);

    const auto* pt = dynamic_cast<const ParameterizedType*>(baseType);
    bool hasMethodTypeArguments = (methodTypeArguments != nullptr) && !methodTypeArguments->empty();

    std::vector<std::shared_ptr<const IMethod>> result;
    if (!returningDefinitions(options) && (pt != nullptr || hasMethodTypeArguments)) {
        // Specialization arm: build a `SpecializedMethod` per declared method.
        std::optional<TypeParameterSubstitution> substitution;
        for (const IMethod* m : declaredMethods) {
            if (hasMethodTypeArguments &&
                static_cast<int>(m->TypeParameters().size()) !=
                    static_cast<int>(methodTypeArguments->size())) {
                continue;
            }
            if (!substitution.has_value()) {
                if (pt != nullptr) {
                    substitution = pt->GetSubstitution(toOptionalArgs(methodTypeArguments));
                } else {
                    substitution = TypeParameterSubstitution(std::nullopt,
                                                             toOptionalArgs(methodTypeArguments));
                }
            }
            result.push_back(std::make_shared<SpecializedMethod>(
                aliasMember(baseType, m), *substitution));
        }
    } else {
        // Definitions arm: the unspecialized declared methods, aliased to the base type.
        for (const IMethod* m : declaredMethods) {
            result.push_back(aliasMember(baseType, m));
        }
    }
    return result;
}

// ---- GetConstructors / GetAccessors ----
// The C# `GetConstructorsOrAccessorsImpl(IType baseType, IEnumerable<IMethod> declaredMembers,
// GetMemberOptions options)` (shared by `GetConstructorsImpl` and `GetAccessorsImpl`).
std::vector<std::shared_ptr<const IMethod>> GetConstructorsOrAccessorsImpl(
    const IType* baseType,
    const std::vector<const IMethod*>& declaredMembers,
    GetMemberOptions options) {

    std::vector<std::shared_ptr<const IMethod>> result;
    if (returningDefinitions(options)) {
        for (const IMethod* m : declaredMembers) {
            result.push_back(aliasMember(baseType, m));
        }
        return result;
    }
    const auto* pt = dynamic_cast<const ParameterizedType*>(baseType);
    if (pt != nullptr) {
        TypeParameterSubstitution substitution = pt->GetSubstitution();
        for (const IMethod* m : declaredMembers) {
            // The C# `{ DeclaringType = pt }` setter is deferred -- the `SpecializedMember`
            // lazy `DeclaringType` getter computes the parameterized declaring type from the
            // substitution, so the explicit set is unnecessary (the header-comment deferral).
            result.push_back(std::make_shared<SpecializedMethod>(
                aliasMember(baseType, m), substitution));
        }
    } else {
        for (const IMethod* m : declaredMembers) {
            result.push_back(aliasMember(baseType, m));
        }
    }
    return result;
}

// The C# `GetConstructorsImpl(IType baseType, Predicate<IMethod> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IMethod>> GetConstructorsImpl(
    const IType* baseType,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    return GetConstructorsOrAccessorsImpl(baseType,
        baseType->GetConstructors(filter, options | declaredMembers), options);
}

// The C# `GetAccessorsImpl(IType baseType, Predicate<IMethod> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IMethod>> GetAccessorsImpl(
    const IType* baseType,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    return GetConstructorsOrAccessorsImpl(baseType,
        baseType->GetAccessors(filter, options | declaredMembers), options);
}

// ---- GetPropertiesImpl ----
// The C# `GetPropertiesImpl(IType baseType, Predicate<IProperty> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IProperty>> GetPropertiesImpl(
    const IType* baseType,
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options) {

    std::vector<const IProperty*> declaredProperties =
        baseType->GetProperties(filter, options | declaredMembers);

    std::vector<std::shared_ptr<const IProperty>> result;
    if (returningDefinitions(options)) {
        for (const IProperty* m : declaredProperties) {
            result.push_back(aliasMember(baseType, m));
        }
        return result;
    }
    const auto* pt = dynamic_cast<const ParameterizedType*>(baseType);
    if (pt != nullptr) {
        TypeParameterSubstitution substitution = pt->GetSubstitution();
        for (const IProperty* m : declaredProperties) {
            result.push_back(std::make_shared<SpecializedProperty>(
                aliasMember(baseType, m), substitution));
        }
    } else {
        for (const IProperty* m : declaredProperties) {
            result.push_back(aliasMember(baseType, m));
        }
    }
    return result;
}

// ---- GetFieldsImpl ----
// The C# `GetFieldsImpl(IType baseType, Predicate<IField> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IField>> GetFieldsImpl(
    const IType* baseType,
    std::function<bool(const IField*)> filter,
    GetMemberOptions options) {

    std::vector<const IField*> declaredFields =
        baseType->GetFields(filter, options | declaredMembers);

    std::vector<std::shared_ptr<const IField>> result;
    if (returningDefinitions(options)) {
        for (const IField* m : declaredFields) {
            result.push_back(aliasMember(baseType, m));
        }
        return result;
    }
    const auto* pt = dynamic_cast<const ParameterizedType*>(baseType);
    if (pt != nullptr) {
        TypeParameterSubstitution substitution = pt->GetSubstitution();
        for (const IField* m : declaredFields) {
            result.push_back(std::make_shared<SpecializedField>(
                aliasMember(baseType, m), substitution));
        }
    } else {
        for (const IField* m : declaredFields) {
            result.push_back(aliasMember(baseType, m));
        }
    }
    return result;
}

// ---- GetEventsImpl ----
// The C# `GetEventsImpl(IType baseType, Predicate<IEvent> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IEvent>> GetEventsImpl(
    const IType* baseType,
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options) {

    std::vector<const IEvent*> declaredEvents =
        baseType->GetEvents(filter, options | declaredMembers);

    std::vector<std::shared_ptr<const IEvent>> result;
    if (returningDefinitions(options)) {
        for (const IEvent* m : declaredEvents) {
            result.push_back(aliasMember(baseType, m));
        }
        return result;
    }
    const auto* pt = dynamic_cast<const ParameterizedType*>(baseType);
    if (pt != nullptr) {
        TypeParameterSubstitution substitution = pt->GetSubstitution();
        for (const IEvent* m : declaredEvents) {
            result.push_back(std::make_shared<SpecializedEvent>(
                aliasMember(baseType, m), substitution));
        }
    } else {
        for (const IEvent* m : declaredEvents) {
            result.push_back(aliasMember(baseType, m));
        }
    }
    return result;
}

// ---- GetMembersImpl ----
// The C# `GetMembersImpl(IType baseType, Predicate<IMember> filter, GetMemberOptions options)`.
// Composes the four families; the `IMember` filter is passed to each family `*Impl` (the
// `std::function`-invocable-with-derived-arg conversion -- the C# `Predicate<in T>` contravariance
// analogue -- so a single member filter selects across methods / properties / fields / events).
std::vector<std::shared_ptr<const IMember>> GetMembersImpl(
    const IType* baseType,
    std::function<bool(const IMember*)> filter,
    GetMemberOptions options) {

    std::vector<std::shared_ptr<const IMember>> result;
    auto append = [&](auto&& family) {
        for (auto& m : family) {
            result.push_back(std::shared_ptr<const IMember>(std::move(m)));
        }
    };
    // `GetMethodsImpl` with `null` method type arguments (the C# `GetMethodsImpl(baseType, null, ...)`).
    append(GetMethodsImpl(baseType, nullptr, filter, options));
    append(GetPropertiesImpl(baseType, filter, options));
    append(GetFieldsImpl(baseType, filter, options));
    append(GetEventsImpl(baseType, filter, options));
    return result;
}

} // namespace

// ---- Public entries ----

std::vector<std::shared_ptr<const IMethod>> GetMethods(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    return GetMethods(type, nullptr, filter, options);
}

std::vector<std::shared_ptr<const IMethod>> GetMethods(
    const IType* type,
    const std::vector<ITypePtr>* typeArguments,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    if (typeArguments != nullptr && !typeArguments->empty()) {
        filter = And(FilterTypeParameterCount(static_cast<int>(typeArguments->size())), filter);
    }
    if (ignoringInherited(options)) {
        return GetMethodsImpl(type, typeArguments, filter, options);
    }
    std::vector<std::shared_ptr<const IMethod>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetMethodsImpl(t, typeArguments, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IMethod>> GetConstructors(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetConstructorsImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IMethod>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetConstructorsImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IMethod>> GetAccessors(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetAccessorsImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IMethod>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetAccessorsImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IProperty>> GetProperties(
    const IType* type,
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetPropertiesImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IProperty>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetPropertiesImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IField>> GetFields(
    const IType* type,
    std::function<bool(const IField*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetFieldsImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IField>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetFieldsImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IEvent>> GetEvents(
    const IType* type,
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetEventsImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IEvent>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetEventsImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::shared_ptr<const IMember>> GetMembers(
    const IType* type,
    std::function<bool(const IMember*)> filter,
    GetMemberOptions options) {
    if (ignoringInherited(options)) {
        return GetMembersImpl(type, filter, options);
    }
    std::vector<std::shared_ptr<const IMember>> result;
    for (const IType* t : GetNonInterfaceBaseTypes(type)) {
        auto part = GetMembersImpl(t, filter, options);
        for (auto& m : part) {
            result.push_back(std::move(m));
        }
    }
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation::GetMembersHelper
