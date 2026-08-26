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
#include "Decompiler/TypeSystem/IMethod.hpp"  // (not needed directly, but the GetAccessors return)
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (the DeclaringTypeDefinition)
#include "Decompiler/TypeSystem/ParameterListComparer.hpp"  // SignatureComparer::Ordinal
#include "Decompiler/TypeSystem/SymbolKind.hpp"  // SymbolKind::Accessor
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetNonInterfaceBaseTypes / GetAllBaseTypes

#include <algorithm>

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

} // namespace ILSpy::Decompiler::TypeSystem::InheritanceHelper
