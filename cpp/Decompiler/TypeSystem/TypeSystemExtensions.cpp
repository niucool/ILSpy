// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Out-of-line definitions for the base-type traversal region of
// ICSharpCode.Decompiler/TypeSystem/TypeSystemExtensions.cs (the
// declarations and port notes live in TypeSystemExtensions.hpp).

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include "Decompiler/TypeSystem/Implementation/BaseTypeCollector.hpp"

#include <algorithm>
#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem {

std::vector<const IType*> GetAllBaseTypes(const IType* type)
{
    if (type == nullptr)
        throw std::invalid_argument("GetAllBaseTypes: type must not be null");
    Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*type);
    return collector.Types();
}

std::vector<const IType*> GetNonInterfaceBaseTypes(const IType* type)
{
    if (type == nullptr)
        throw std::invalid_argument("GetNonInterfaceBaseTypes: type must not be null");
    Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*type);
    return collector.Types();
}

std::vector<const ITypeDefinition*> GetAllBaseTypeDefinitions(const IType* type)
{
    if (type == nullptr)
        throw std::invalid_argument("GetAllBaseTypeDefinitions: type must not be null");
    // type.GetAllBaseTypes().Select(t => t.GetDefinition()).Where(d => d != null).Distinct()
    std::vector<const ITypeDefinition*> result;
    for (const IType* baseType : GetAllBaseTypes(type)) {
        const ITypeDefinition* def = baseType->GetDefinition();
        if (def == nullptr)
            continue;
        if (std::find(result.begin(), result.end(), def) == result.end())
            result.push_back(def);
    }
    return result;
}

bool IsDerivedFrom(const ITypeDefinition& type, const ITypeDefinition* baseType)
{
    if (baseType == nullptr)
        return false;
    if (&type.Compilation() != &baseType->Compilation()) {
        throw std::runtime_error(
            "IsDerivedFrom: Both arguments to IsDerivedFrom() must be from the same compilation.");
    }
    auto defs = GetAllBaseTypeDefinitions(&type);
    return std::find(defs.begin(), defs.end(), baseType) != defs.end();
}

bool IsDerivedFrom(const ITypeDefinition& type, KnownTypeCode baseType)
{
    if (baseType == KnownTypeCode::None)
        return false;
    // The C# `IsDerivedFrom(type, type.Compilation.FindType(baseType).GetDefinition())`.
    return IsDerivedFrom(type, type.Compilation().FindType(baseType).GetDefinition());
}

} // namespace ILSpy::Decompiler::TypeSystem
