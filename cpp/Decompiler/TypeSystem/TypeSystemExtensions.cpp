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
#include "Decompiler/TypeSystem/IMethod.hpp"

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

bool IsKnownType(const IType& type, KnownTypeCode knownType) {
    const ITypeDefinition* def = type.GetDefinition();
    return def != nullptr && def->KnownTypeCode() == knownType;
}

bool IsArrayInterfaceType(const IType& type) {
    if (type.TypeParameterCount() != 1)
        return false;
    const ITypeDefinition* def = type.GetDefinition();
    if (def == nullptr)
        return false;
    switch (def->KnownTypeCode()) {
        case KnownTypeCode::IEnumerableOfT:
        case KnownTypeCode::ICollectionOfT:
        case KnownTypeCode::IListOfT:
        case KnownTypeCode::IReadOnlyCollectionOfT:
        case KnownTypeCode::IReadOnlyListOfT:
            return true;
        default:
            return false;
    }
}

bool IsAnyPointer(TypeKind typeKind)
{
    // C# `typeKind switch { TypeKind.Pointer => true, TypeKind.FunctionPointer => true, _ => false }`.
    switch (typeKind) {
        case TypeKind::Pointer:
        case TypeKind::FunctionPointer:
            return true;
        default:
            return false;
    }
}

const IType* SkipModifiers(const IType& type)
{
    // C# `while (ty is ModifiedType mt) ty = mt.ElementType; return ty;` -- the `ModifiedType` (a
    // TypeWithElementType decorator) carries its element as a `shared_ptr<IType>` member; the loop
    // follows the `Element()` handle until a non-modifier type is reached (or a degenerate null element
    // short-circuits the walk to null, matching the C# null return).
    const IType* t = &type;
    while (const ModifiedType* mt = dynamic_cast<const ModifiedType*>(t)) {
        const ITypePtr& element = mt->Element();
        t = element.get();
    }
    return t;
}

const IMethod* GetDelegateInvokeMethod(const IType& type)
{
    // C# `if (type.Kind == TypeKind.Delegate) return type.GetMethods(m => m.Name == "Invoke",
    // GetMemberOptions.IgnoreInheritedMembers).FirstOrDefault(); else return null;` -- only a
    // delegate kind carries an `Invoke` method; every other kind short-circuits to null. The
    // `GetMethods` snapshot is filtered to methods named `"Invoke"` (the delegate's single
    // entry-point method, declared directly on the delegate -- hence `IgnoreInheritedMembers`);
    // `.FirstOrDefault()` returns the first match or null for an empty snapshot. The returned
    // pointer is non-owning (the method is owned by the type system / the concrete type
    // definition whose `GetMethods` produced the snapshot).
    if (type.Kind() != TypeKind::Delegate)
        return nullptr;
    auto methods = type.GetMethods(
        [](const IMethod* m) { return m->Name() == "Invoke"; },
        GetMemberOptions::IgnoreInheritedMembers);
    return methods.empty() ? nullptr : methods.front();
}

// The C# `public static IType WithoutNullability(this IType type)` (TypeSystemExtensions.cs
// line 799) -- `type.ChangeNullability(Nullability.Oblivious)`.
ITypePtr WithoutNullability(IType& type)
{
    return type.ChangeNullability(Nullability::Oblivious);
}

} // namespace ILSpy::Decompiler::TypeSystem
