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

// Port of the `MemberLookup` Lookup-region private helpers. See the header for the design.

#include "Decompiler/CSharp/Resolver/LookupHelpers.hpp"

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"  // CreateResult uses isInEnumMemberInitializer_
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"  // MethodGroupResolveResult + MethodListWithDeclaringType
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"  // AmbiguousTypeResolveResult / AmbiguousMemberResolveResult
#include "Decompiler/Semantics/MemberResolveResult.hpp"  // MemberResolveResult
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult base
#include "Decompiler/Semantics/ThisResolveResult.hpp"  // the static-member retarget
#include "Decompiler/Semantics/TypeResolveResult.hpp"  // TypeResolveResult
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"  // the empty-groups result
#include "Decompiler/TypeSystem/IField.hpp"  // dynamic_cast<IField> (the enum arm)
#include "Decompiler/TypeSystem/IMethod.hpp"  // dynamic_cast<IMethod>
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // dynamic_cast<IParameterizedMember>
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // GetDefinition + DeclaringType (via IEntity)
#include "Decompiler/TypeSystem/ParameterListComparer.hpp"  // SignatureComparer.Ordinal
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetNonInterfaceBaseTypes

#include <algorithm>

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

int InnerTypeParameterCount(const ILSpy::Decompiler::TypeSystem::IType& type) {
    using namespace ILSpy::Decompiler::TypeSystem;
    int total = type.TypeParameterCount();
    const ITypeDefinition* def = type.GetDefinition();
    if (def == nullptr) return total;
    ITypePtr declaringType = def->DeclaringType();  // IEntity::DeclaringType (unspecialized outer)
    if (declaringType == nullptr) return total;
    return total - declaringType->TypeParameterCount();
}

void AddNestedTypes(const ILSpy::Decompiler::TypeSystem::IType& type,
                    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& nestedTypes,
                    int typeArgumentCount,
                    std::vector<LookupGroup>& lookupGroups,
                    std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IType*>>& typeBaseTypes,
                    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& newNestedTypes) {
    using namespace ILSpy::Decompiler::TypeSystem;
    for (const ITypePtr& nestedType : nestedTypes) {
        // Remove all non-types declared in a base type of `type`, and all types with the same number
        // of type parameters declared in a base type of `type`.
        for (LookupGroup& lookupGroup : lookupGroups) {
            if (lookupGroup.AllHidden()) continue;  // everything is already hidden
            if (!typeBaseTypes.has_value()) {
                typeBaseTypes = GetNonInterfaceBaseTypes(&type);
            }
            // The C# `typeBaseTypes.Contains(lookupGroup.DeclaringType)` -- pointer-identity search
            // (the C# reference-equality on `IType`; `GetNonInterfaceBaseTypes` returns `const IType*`).
            const auto& baseTypes = *typeBaseTypes;
            if (std::find(baseTypes.begin(), baseTypes.end(), lookupGroup.DeclaringType()) !=
                baseTypes.end()) {
                lookupGroup.MethodsAreHidden() = true;
                lookupGroup.NonMethodIsHidden() = true;
                // The C# `NestedTypes.RemoveAll(t => InnerTypeParameterCount(t) == typeArgumentCount)`.
                auto& nt = lookupGroup.NestedTypes();
                nt.erase(std::remove_if(nt.begin(), nt.end(),
                    [&](const ITypePtr& t) {
                        return InnerTypeParameterCount(*t) == typeArgumentCount;
                    }), nt.end());
            }
        }
        // Add the new nested type.
        if (!newNestedTypes.has_value()) {
            newNestedTypes = std::vector<ITypePtr>{};
        }
        newNestedTypes->push_back(nestedType);
    }
}

void AddMembers(const MemberLookup& lookup,
                const ILSpy::Decompiler::TypeSystem::IType& type,
                const std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>& members,
                bool allowProtectedAccess,
                std::vector<LookupGroup>& lookupGroups,
                bool treatAllParameterizedMembersAsMethods,
                std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IType*>>& typeBaseTypes,
                std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>>& newMethods,
                const ILSpy::Decompiler::TypeSystem::IMember*& newNonMethod) {
    using namespace ILSpy::Decompiler::TypeSystem;
    for (const IMember* member : members) {
        if (!lookup.IsAccessible(*member, allowProtectedAccess))
            continue;

        // C#: method = treatAllParameterizedMembersAsMethods ? member as IParameterizedMember
        //                                              : member as IMethod;
        const IParameterizedMember* method = nullptr;
        if (treatAllParameterizedMembersAsMethods) {
            method = dynamic_cast<const IParameterizedMember*>(member);
        } else {
            method = dynamic_cast<const IMethod*>(member);
        }

        bool replacedVirtualMemberWithOverride = false;
        if (member->IsOverride()) {
            // Replacing virtual member with override: go backwards to find the most-derived virtual.
            for (int i = static_cast<int>(lookupGroups.size()) - 1; i >= 0 && !replacedVirtualMemberWithOverride; i--) {
                if (!typeBaseTypes.has_value()) {
                    typeBaseTypes = GetNonInterfaceBaseTypes(&type);
                }
                const auto& baseTypes = *typeBaseTypes;
                LookupGroup& lookupGroup = lookupGroups[static_cast<std::size_t>(i)];
                if (std::find(baseTypes.begin(), baseTypes.end(), lookupGroup.DeclaringType()) !=
                    baseTypes.end()) {
                    if (method != nullptr && !lookupGroup.MethodsAreHidden()) {
                        // Find the matching method and replace it with the override.
                        for (auto& baseMethod : lookupGroup.Methods()) {
                            if (SignatureComparer::Ordinal().Equals(method, baseMethod)) {
                                baseMethod = method;
                                replacedVirtualMemberWithOverride = true;
                                break;
                            }
                        }
                    } else {
                        // If the member type matches, replace the non-method with the override.
                        if (lookupGroup.NonMethod() != nullptr &&
                            lookupGroup.NonMethod()->SymbolKind() == member->SymbolKind()) {
                            lookupGroup.NonMethod() = member;
                            replacedVirtualMemberWithOverride = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!replacedVirtualMemberWithOverride) {
            // Make the member hide other members.
            for (LookupGroup& lookupGroup : lookupGroups) {
                if (lookupGroup.AllHidden()) continue;  // everything is already hidden
                if (!typeBaseTypes.has_value()) {
                    typeBaseTypes = GetNonInterfaceBaseTypes(&type);
                }
                const auto& baseTypes = *typeBaseTypes;
                if (std::find(baseTypes.begin(), baseTypes.end(), lookupGroup.DeclaringType()) !=
                    baseTypes.end()) {
                    // Methods hide all non-methods; non-methods hide everything.
                    lookupGroup.NestedTypes().clear();  // C# NestedTypes = null -> clear the vector
                    lookupGroup.NonMethodIsHidden() = true;
                    if (method == nullptr) {  // !(member is IMethod)
                        lookupGroup.MethodsAreHidden() = true;
                    }
                }
            }
            // Add the new member.
            if (method != nullptr) {
                if (!newMethods.has_value()) {
                    newMethods = std::vector<const IParameterizedMember*>{};
                }
                newMethods->push_back(method);
            } else {
                newNonMethod = member;
            }
        }
    }
}

void RemoveInterfaceMembersHiddenByClassMembers(std::vector<LookupGroup>& lookupGroups) {
    using namespace ILSpy::Decompiler::TypeSystem;
    for (LookupGroup& classLookupGroup : lookupGroups) {
        if (IsInterfaceOrSystemObject(*classLookupGroup.DeclaringType()))
            continue;
        // The current lookup group contains class members that might hide interface members.
        bool hasNestedTypes = !classLookupGroup.NestedTypes().empty();  // C# NestedTypes != null && .Count > 0
        if (hasNestedTypes || !classLookupGroup.NonMethodIsHidden()) {
            // Hide all members from interface types.
            for (LookupGroup& interfaceLookupGroup : lookupGroups) {
                if (IsInterfaceOrSystemObject(*interfaceLookupGroup.DeclaringType())) {
                    interfaceLookupGroup.NestedTypes().clear();  // C# NestedTypes = null
                    interfaceLookupGroup.NonMethodIsHidden() = true;
                    interfaceLookupGroup.MethodsAreHidden() = true;
                }
            }
        } else if (!classLookupGroup.MethodsAreHidden()) {
            for (const IParameterizedMember* classMethod : classLookupGroup.Methods()) {
                // Hide all non-methods from interface types, and all methods with the same signature
                // as a method in this class type.
                for (LookupGroup& interfaceLookupGroup : lookupGroups) {
                    if (IsInterfaceOrSystemObject(*interfaceLookupGroup.DeclaringType())) {
                        interfaceLookupGroup.NestedTypes().clear();
                        interfaceLookupGroup.NonMethodIsHidden() = true;
                        // The C# `Methods != null && !MethodsAreHidden` -- the port's `Methods` is
                        // never null (an empty vector is the C# null), so `!MethodsAreHidden`.
                        if (!interfaceLookupGroup.MethodsAreHidden()) {
                            auto& methods = interfaceLookupGroup.Methods();
                            methods.erase(std::remove_if(methods.begin(), methods.end(),
                                [&](const IParameterizedMember* m) {
                                    return SignatureComparer::Ordinal().Equals(classMethod, m);
                                }), methods.end());
                        }
                    }
                }
            }
        }
    }
}

bool IsInterfaceOrSystemObject(const ILSpy::Decompiler::TypeSystem::IType& type) {
    using namespace ILSpy::Decompiler::TypeSystem;
    if (type.Kind() == TypeKind::Interface)
        return true;
    const ITypeDefinition* d = type.GetDefinition();
    return d != nullptr && d->KnownTypeCode() == KnownTypeCode::Object;
}

std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> CreateResult(
    const MemberLookup& lookup,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> targetResolveResult,
    std::vector<LookupGroup>& lookupGroups,
    std::string name,
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    // Remove all hidden groups (the C# `lookupGroups.RemoveAll(g => g.AllHidden)`).
    lookupGroups.erase(std::remove_if(lookupGroups.begin(), lookupGroups.end(),
        [](const LookupGroup& g) { return g.AllHidden(); }), lookupGroups.end());

    if (lookupGroups.empty()) {
        // The C# `new UnknownMemberResolveResult(targetResolveResult.Type, name, typeArguments)` --
        // the target's type (a null target yields an UnknownType; the Lookup methods pass a non-null
        // target, so the null-target fallback is the robustness arm).
        ITypePtr targetType = targetResolveResult
            ? ITypePtr(const_cast<IType*>(&targetResolveResult->Type())->shared_from_this())
            : ILSpy::Decompiler::TypeSystem::UnknownType();
        return std::make_shared<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>(
            std::move(targetType), std::move(name), std::move(typeArguments));
    }

    // If there are methods, make a MethodGroupResolveResult.
    bool anyVisibleMethods = false;
    for (const LookupGroup& g : lookupGroups) {
        if (!g.MethodsAreHidden() && !g.Methods().empty()) { anyVisibleMethods = true; break; }
    }
    if (anyVisibleMethods) {
        std::vector<ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType> methodLists;
        for (const LookupGroup& lookupGroup : lookupGroups) {
            if (!lookupGroup.MethodsAreHidden() && !lookupGroup.Methods().empty()) {
                // `MethodListWithDeclaringType(ITypePtr declaringType)` -- `shared_from_this()` on a
                // `const IType*` returns `shared_ptr<const IType>`; `const_pointer_cast` drops the const
                // (the group's `DeclaringType` is owned by the base-types snapshot, the D477 convention).
                ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType bucket(
                    std::const_pointer_cast<IType>(lookupGroup.DeclaringType()->shared_from_this()));
                for (const IParameterizedMember* method : lookupGroup.Methods()) {
                    bucket.push_back(method);  // MethodListWithDeclaringType inherits std::vector
                }
                methodLists.push_back(std::move(bucket));
            }
        }
        return std::make_shared<ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult>(
            std::move(targetResolveResult), std::move(name), std::move(methodLists),
            std::move(typeArguments));
    }

    // If there are ambiguities, report the most-derived result (last group).
    LookupGroup& resultGroup = lookupGroups.back();
    if (!resultGroup.NestedTypes().empty()) {
        if (resultGroup.NestedTypes().size() > 1 || !resultGroup.NonMethodIsHidden() ||
            lookupGroups.size() > 1) {
            return std::make_shared<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>(
                resultGroup.NestedTypes().front());
        }
        return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(
            resultGroup.NestedTypes().front());
    }

    if (resultGroup.NonMethod() != nullptr && resultGroup.NonMethod()->IsStatic() &&
        dynamic_cast<const ILSpy::Decompiler::Semantics::ThisResolveResult*>(targetResolveResult.get())) {
        targetResolveResult = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(
            std::const_pointer_cast<IType>(
                const_cast<IType&>(targetResolveResult->Type()).shared_from_this()));
    }

    if (lookupGroups.size() > 1) {
        return std::make_shared<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>(
            std::move(targetResolveResult), resultGroup.NonMethod());
    }
    if (lookup.IsInEnumMemberInitializer()) {
        const IField* field = dynamic_cast<const IField*>(resultGroup.NonMethod());
        if (field != nullptr && field->DeclaringTypeDefinition() != nullptr &&
            field->DeclaringTypeDefinition()->Kind() == TypeKind::Enum) {
            return std::make_shared<ILSpy::Decompiler::Semantics::MemberResolveResult>(
                std::move(targetResolveResult), field,
                field->DeclaringTypeDefinition()->EnumUnderlyingType(),
                field->IsConst(), field->GetConstantValue(false));
        }
    }
    return std::make_shared<ILSpy::Decompiler::Semantics::MemberResolveResult>(
        std::move(targetResolveResult), resultGroup.NonMethod());
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
