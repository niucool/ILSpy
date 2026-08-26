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

// Port of the `MemberLookup` Lookup-region PUBLIC methods (ICSharpCode.Decompiler/CSharp/Resolver/
// MemberLookup.cs, the `#region Lookup` / `#region LookupType` / `#region GetAccessibleMembers` /
// `#region Lookup Indexer`). These are `MemberLookup` instance methods that compose the `Detail::`
// helpers (`AddNestedTypes` / `AddMembers` / `RemoveInterfaceMembersHiddenByClassMembers` /
// `CreateResult`) from `LookupHelpers.hpp`. They use instance state (`IsProtectedAccessAllowed`,
// `IsAccessible`, `IsInvocable`, `IsInEnumMemberInitializer` via `CreateResult`), so they are methods,
// NOT free functions. They are OUT-OF-LINE in this `.cpp` (the `Detail::` helpers in `LookupHelpers.hpp`
// include `MemberLookup.hpp` -- a header cycle if the methods were inline in the header).

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"

#include "Decompiler/CSharp/Resolver/LookupGroup.hpp"  // the per-(type, name) group
#include "Decompiler/CSharp/Resolver/LookupHelpers.hpp"  // the Detail:: helpers
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"  // AmbiguousTypeResolveResult (LookupType)
#include "Decompiler/Semantics/TypeResolveResult.hpp"  // TypeResolveResult (LookupType)
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"  // UnknownMemberResolveResult
#include "Decompiler/TypeSystem/IType.hpp"  // GetMembers / GetMethods / GetNestedTypes / GetNonInterfaceBaseTypes
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (the filter arg)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetNonInterfaceBaseTypes
#include "Decompiler/TypeSystem/SymbolKind.hpp"  // SymbolKind::Indexer / Operator

#include <algorithm>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

namespace Detail {
// `AddNestedTypes`/`AddMembers`/`RemoveInterfaceMembersHiddenByClassMembers`/`CreateResult` are
// declared in `LookupHelpers.hpp` (forward-declared `MemberLookup`; the full definition is available
// here since this `.cpp` includes `MemberLookup.hpp` first).
}

std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> MemberLookup::Lookup(
    const ILSpy::Decompiler::Semantics::ResolveResult& targetResolveResult,
    std::string name,
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
    bool isInvocation) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    bool targetIsTypeParameter = targetResolveResult.Type().Kind() == TypeKind::TypeParameter;

    bool allowProtectedAccess = IsProtectedAccessAllowed(targetResolveResult);
    // The C# `nestedTypeFilter`: `entity.Name == name && IsAccessible(entity, allowProtectedAccess)`.
    auto nestedTypeFilter = [&name, allowProtectedAccess, this](const ITypeDefinition* entity) {
        return entity != nullptr && entity->Name() == name && IsAccessible(*entity, allowProtectedAccess);
    };
    // The C# `memberFilter`: `entity.SymbolKind != Indexer && entity.SymbolKind != Operator &&
    // entity.Name == name`.
    auto memberFilter = [&name](const IMember* entity) {
        return entity != nullptr && entity->SymbolKind() != SymbolKind::Indexer &&
               entity->SymbolKind() != SymbolKind::Operator && entity->Name() == name;
    };

    std::vector<LookupGroup> lookupGroups;
    // This loop handles base types before derived types.
    for (const IType* type : GetNonInterfaceBaseTypes(&targetResolveResult.Type())) {
        std::optional<std::vector<const IType*>> typeBaseTypes;
        std::optional<std::vector<ITypePtr>> newNestedTypes;
        std::optional<std::vector<const IParameterizedMember*>> newMethods;
        const IMember* newNonMethod = nullptr;

        if (!isInvocation && !targetIsTypeParameter) {
            // Consider nested types only if it's not an invocation.
            auto nestedTypes = type->GetNestedTypes(typeArguments, nestedTypeFilter,
                                                     GetMemberOptions::IgnoreInheritedMembers);
            Detail::AddNestedTypes(*type, nestedTypes,
                                   static_cast<int>(typeArguments.size()),
                                   lookupGroups, typeBaseTypes, newNestedTypes);
        }

        std::vector<const IMember*> members;
        if (typeArguments.empty()) {
            // Note: IsInvocable-checking cannot be done as part of the filter (it must be done after
            // type substitution), so it's a post-filter `Where`.
            std::vector<const IMember*> rawMembers =
                type->GetMembers(memberFilter, GetMemberOptions::IgnoreInheritedMembers);
            if (isInvocation) {
                for (const IMember* m : rawMembers)
                    if (IsInvocable(*m)) members.push_back(m);
            } else {
                members = std::move(rawMembers);
            }
        } else {
            // No need to check for isInvocation/isInvocable here: we only fetch methods.
            std::vector<const IMethod*> rawMethods =
                type->GetMethods(typeArguments, memberFilter, GetMemberOptions::IgnoreInheritedMembers);
            for (const IMethod* m : rawMethods)
                members.push_back(m);  // upcast IMethod* -> IMember*
        }
        Detail::AddMembers(*this, *type, members, allowProtectedAccess, lookupGroups, false,
                           typeBaseTypes, newMethods, newNonMethod);

        if (newNestedTypes.has_value() || newMethods.has_value() || newNonMethod != nullptr) {
            lookupGroups.emplace_back(
                type,
                newNestedTypes.has_value() ? &*newNestedTypes : nullptr,
                newMethods.has_value() ? &*newMethods : nullptr,
                newNonMethod);
        }
    }

    // Remove interface members hidden by class members (only for type parameters).
    if (targetIsTypeParameter) {
        Detail::RemoveInterfaceMembersHiddenByClassMembers(lookupGroups);
    }

    return Detail::CreateResult(*this,
        std::shared_ptr<ResolveResult>(const_cast<ResolveResult*>(&targetResolveResult),
                                       [](ResolveResult*){}),  // non-owning alias (the caller owns the target)
        lookupGroups, std::move(name), std::move(typeArguments));
}

std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> MemberLookup::LookupType(
    const ILSpy::Decompiler::TypeSystem::IType& declaringType,
    std::string name,
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
    bool parameterizeResultType) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    int typeArgumentCount = static_cast<int>(typeArguments.size());
    // The C# `filter`: `InnerTypeParameterCount(d) == typeArgumentCount && d.Name == name &&
    // IsAccessible(d, true)`.
    auto filter = [typeArgumentCount, &name, this](const ITypeDefinition* d) {
        return d != nullptr && Detail::InnerTypeParameterCount(*d) == typeArgumentCount &&
               d->Name() == name && IsAccessible(*d, true);
    };

    std::vector<LookupGroup> lookupGroups;
    if (declaringType.Kind() != TypeKind::TypeParameter) {
        for (const IType* type : GetNonInterfaceBaseTypes(&declaringType)) {
            std::optional<std::vector<const IType*>> typeBaseTypes;
            std::optional<std::vector<ITypePtr>> newNestedTypes;

            std::vector<ITypePtr> nestedTypes;
            if (parameterizeResultType) {
                nestedTypes = type->GetNestedTypes(typeArguments, filter,
                                                   GetMemberOptions::IgnoreInheritedMembers);
            } else {
                nestedTypes = type->GetNestedTypes(filter,
                    GetMemberOptions::IgnoreInheritedMembers | GetMemberOptions::ReturnMemberDefinitions);
            }
            Detail::AddNestedTypes(*type, nestedTypes, typeArgumentCount,
                                   lookupGroups, typeBaseTypes, newNestedTypes);

            if (newNestedTypes.has_value()) {
                lookupGroups.emplace_back(type, &*newNestedTypes, nullptr, nullptr);
            }
        }
    }

    // Remove all hidden groups (the C# `lookupGroups.RemoveAll(g => g.AllHidden)`).
    lookupGroups.erase(std::remove_if(lookupGroups.begin(), lookupGroups.end(),
        [](const LookupGroup& g) { return g.AllHidden(); }), lookupGroups.end());

    if (lookupGroups.empty()) {
        // The C# `new UnknownMemberResolveResult(declaringType, name, typeArguments)` -- the declaring
        // type (NOT a target's type; `LookupType` has no `ResolveResult` target).
        return std::make_shared<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>(
            ITypePtr(const_cast<IType*>(&declaringType)->shared_from_this()),
            std::move(name), std::move(typeArguments));
    }

    LookupGroup& resultGroup = lookupGroups.back();
    if (resultGroup.NestedTypes().size() > 1 || lookupGroups.size() > 1) {
        return std::make_shared<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>(
            resultGroup.NestedTypes().front());
    }
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(
        resultGroup.NestedTypes().front());
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
