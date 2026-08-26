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

// Port of ICSharpCode.Decompiler/TypeSystem/InheritanceHelper.cs. See the header for the design.

#include "Decompiler/TypeSystem/InheritanceHelper.hpp"

#include "Decompiler/TypeSystem/Accessibility.hpp"  // Accessibility::Private (the > Private filter)
#include "Decompiler/TypeSystem/IEvent.hpp"  // dynamic_cast<IEvent> (GetDerivedMember)
#include "Decompiler/TypeSystem/IField.hpp"  // dynamic_cast<IField> (GetDerivedMember)
#include "Decompiler/TypeSystem/IMethod.hpp"  // (not needed directly, but the GetAccessors return)
#include "Decompiler/TypeSystem/IProperty.hpp"  // dynamic_cast<IProperty> (GetDerivedMember)
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (the DeclaringTypeDefinition)
#include "Decompiler/TypeSystem/ParameterListComparer.hpp"  // SignatureComparer::Ordinal
#include "Decompiler/TypeSystem/SymbolKind.hpp"  // SymbolKind::Accessor
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetNonInterfaceBaseTypes / GetAllBaseTypes

#include <algorithm>
#include <unordered_set>

namespace ILSpy::Decompiler::TypeSystem::InheritanceHelper {

const IMember* GetBaseMember(const IMember& member) {
    auto baseMembers = GetBaseMembers(member, false);
    return baseMembers.empty() ? nullptr : baseMembers.front();
}

std::vector<const IMember*> GetBaseMembers(const IMember& member, bool includeImplementedInterfaces) {
    std::vector<const IMember*> result;
    const IMember* m = &member;

    if (includeImplementedInterfaces) {
        // C#-style explicit interface implementation: if the member is an explicit interface impl with
        // exactly one explicitly-implemented member, switch to that member (and yield it).
        if (m->IsExplicitInterfaceImplementation() && m->ExplicitlyImplementedInterfaceMembers().size() == 1) {
            m = m->ExplicitlyImplementedInterfaceMembers().front();
            result.push_back(m);
        }
    }

    // Remove generic specialization (it's sufficient to look at the definitions).
    const TypeParameterSubstitution* substitution = m->Substitution();
    m = m->MemberDefinition();

    if (m->DeclaringTypeDefinition() == nullptr) {
        // For global methods, return empty list (prevent SharpDevelop UDC crash 4524).
        return result;
    }

    // The base types (interfaces too if `includeImplementedInterfaces`).
    std::vector<const IType*> allBaseTypes;
    if (includeImplementedInterfaces) {
        allBaseTypes = GetAllBaseTypes(m->DeclaringTypeDefinition());
    } else {
        allBaseTypes = GetNonInterfaceBaseTypes(m->DeclaringTypeDefinition());
    }
    // The C# `allBaseTypes.Reverse()` -- iterate derived-last (the derived-most base first in the
    // yield order). The C# `GetNonInterfaceBaseTypes`/`GetAllBaseTypes` return base-first, so reverse
    // to derived-last; `result` appends in that order, so the derived-most base member is first.
    std::reverse(allBaseTypes.begin(), allBaseTypes.end());

    auto nameFilter = [m](const IMember* candidate) {
        return candidate != nullptr && candidate->Name() == m->Name() &&
               candidate->Accessibility() > Accessibility::Private;
    };

    for (const IType* baseType : allBaseTypes) {
        if (baseType == m->DeclaringTypeDefinition())
            continue;

        std::vector<const IMember*> baseMembers;
        if (m->SymbolKind() == SymbolKind::Accessor) {
            // The accessor path: `GetAccessors` returns `const IMethod*`; upcast to `const IMember*`.
            auto accessors = baseType->GetAccessors(
                [](const IMethod* cand) {
                    return cand != nullptr;  // name + accessibility applied below (IMethod is IMember)
                },
                GetMemberOptions::IgnoreInheritedMembers);
            for (const IMethod* a : accessors) {
                if (nameFilter(a)) baseMembers.push_back(a);
            }
        } else {
            auto members = baseType->GetMembers(nameFilter, GetMemberOptions::IgnoreInheritedMembers);
            for (const IMember* mem : members) {
                baseMembers.push_back(mem);
            }
        }

        for (const IMember* baseMember : baseMembers) {
            // `Debug.Assert(baseMember.Accessibility != Accessibility::Private)` -- the filter
            // (`> Private`) already excludes Private, so this holds.
            if (SignatureComparer::Ordinal().Equals(m, baseMember)) {
                result.push_back(baseMember->Specialize(substitution));
            }
        }
    }
    return result;
}

const IMember* GetDerivedMember(const IMember& baseMember, const ITypeDefinition& derivedType) {
    const IMember* baseDef = baseMember.MemberDefinition();
    bool includeInterfaces = baseDef->DeclaringTypeDefinition() != nullptr &&
                             baseDef->DeclaringTypeDefinition()->Kind() == TypeKind::Interface;
    // The C# `baseMember is IMethod method` / `is IProperty property` / `is IEvent` / `is IField` arms.
    if (const auto* method = dynamic_cast<const IMethod*>(baseDef)) {
        for (const IMethod* derivedMethod : derivedType.Methods()) {
            if (derivedMethod->Name() == method->Name() &&
                derivedMethod->Parameters().size() == method->Parameters().size() &&
                derivedMethod->TypeParameters().size() == method->TypeParameters().size()) {
                // The method could override the base method.
                auto derivedBases = GetBaseMembers(*derivedMethod, includeInterfaces);
                for (const IMember* m : derivedBases) {
                    if (m->MemberDefinition() == baseDef) return derivedMethod;
                }
            }
        }
    }
    if (const auto* property = dynamic_cast<const IProperty*>(baseDef)) {
        for (const IProperty* derivedProperty : derivedType.Properties()) {
            if (derivedProperty->Name() == property->Name() &&
                derivedProperty->Parameters().size() == property->Parameters().size()) {
                // The property could override the base property.
                auto derivedBases = GetBaseMembers(*derivedProperty, includeInterfaces);
                for (const IMember* m : derivedBases) {
                    if (m->MemberDefinition() == baseDef) return derivedProperty;
                }
            }
        }
    }
    if (dynamic_cast<const IEvent*>(baseDef) != nullptr) {
        for (const IEvent* derivedEvent : derivedType.Events()) {
            if (derivedEvent->Name() == baseDef->Name()) return derivedEvent;
        }
    }
    if (dynamic_cast<const IField*>(baseDef) != nullptr) {
        for (const IField* derivedField : derivedType.Fields()) {
            if (derivedField->Name() == baseDef->Name()) return derivedField;
        }
    }
    return nullptr;
}

std::vector<const IAttribute*> GetAttributes(const ITypeDefinition& typeDef) {
    std::vector<const IAttribute*> result;
    auto baseTypes = GetNonInterfaceBaseTypes(&typeDef);
    std::reverse(baseTypes.begin(), baseTypes.end());  // C# Reverse -- derived-first
    for (const IType* baseType : baseTypes) {
        const ITypeDefinition* baseTypeDef = baseType->GetDefinition();
        if (baseTypeDef == nullptr) continue;
        for (const IAttribute* attr : baseTypeDef->GetAttributes()) {
            result.push_back(attr);
        }
    }
    return result;
}

const IAttribute* GetAttribute(const ITypeDefinition& typeDef, KnownAttribute attributeType) {
    auto baseTypes = GetNonInterfaceBaseTypes(&typeDef);
    std::reverse(baseTypes.begin(), baseTypes.end());
    for (const IType* baseType : baseTypes) {
        const ITypeDefinition* baseTypeDef = baseType->GetDefinition();
        if (baseTypeDef == nullptr) continue;
        const IAttribute* attr = baseTypeDef->GetAttribute(attributeType);
        if (attr != nullptr) return attr;
    }
    return nullptr;
}

std::vector<const IAttribute*> GetAttributes(const IMember& member) {
    std::vector<const IAttribute*> result;
    std::unordered_set<const IMember*> visitedMembers;
    const IMember* m = &member;
    while (true) {
        m = m->MemberDefinition();  // it's sufficient to look at the definitions
        if (!visitedMembers.insert(m).second) {
            // Abort if we seem to be in an infinite loop (cyclic inheritance).
            break;
        }
        for (const IAttribute* attr : m->GetAttributes()) {
            result.push_back(attr);
        }
        if (!m->IsOverride()) break;
        const IMember* baseMember = GetBaseMember(*m);
        if (baseMember == nullptr) break;
        m = baseMember;
    }
    return result;
}

const IAttribute* GetAttribute(const IMember& member, KnownAttribute attributeType) {
    std::unordered_set<const IMember*> visitedMembers;
    const IMember* m = &member;
    while (true) {
        m = m->MemberDefinition();
        if (!visitedMembers.insert(m).second) break;
        const IAttribute* attr = m->GetAttribute(attributeType);
        if (attr != nullptr) return attr;
        if (!m->IsOverride()) break;
        const IMember* baseMember = GetBaseMember(*m);
        if (baseMember == nullptr) break;
        m = baseMember;
    }
    return nullptr;
}

} // namespace ILSpy::Decompiler::TypeSystem::InheritanceHelper
