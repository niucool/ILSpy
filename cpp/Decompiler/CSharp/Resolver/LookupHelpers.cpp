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

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // GetDefinition + DeclaringType (via IEntity)
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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
