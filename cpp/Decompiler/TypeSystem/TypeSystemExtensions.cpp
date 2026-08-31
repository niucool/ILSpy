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
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"

#include <algorithm>
#include <any>
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

std::vector<const ITypeDefinition*> GetAllTypeDefinitions(const ICompilation& compilation)
{
    // C# `compilation.Modules.SelectMany(a => a.TypeDefinitions)` -- the concatenation of
    // every module's `TypeDefinitions()` snapshot, in module-list order (the main module
    // first, then the referenced modules; the C# deferred SelectMany materializes in the
    // same order).
    std::vector<const ITypeDefinition*> result;
    for (const IModule* module : compilation.Modules()) {
        if (module == nullptr) // degenerate null module entry (the D516 convention)
            continue;
        for (const ITypeDefinition* def : module->TypeDefinitions())
            result.push_back(def);
    }
    return result;
}

std::vector<const ITypeDefinition*> GetTopLevelTypeDefinitions(const ICompilation& compilation)
{
    // C# `compilation.Modules.SelectMany(a => a.TopLevelTypeDefinitions)` -- the same
    // concatenation over the NON-NESTED types.
    std::vector<const ITypeDefinition*> result;
    for (const IModule* module : compilation.Modules()) {
        if (module == nullptr) // degenerate null module entry (the D516 convention)
            continue;
        for (const ITypeDefinition* def : module->TopLevelTypeDefinitions())
            result.push_back(def);
    }
    return result;
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

bool IsInlineArrayType(const IType& type)
{
    // C# `if (type.Kind != TypeKind.Struct) return false; var td = type.GetDefinition();
    // if (td == null) return false; return td.HasAttribute(KnownAttribute.InlineArray);` -- the
    // kind guard short-circuits before the definition lookup, and a definitionless type (a
    // `KnownType` placeholder, `GetDefinition() == nullptr`) returns false even for a struct
    // kind. `HasAttribute` on the definition is the [InlineArray] presence check.
    if (type.Kind() != TypeKind::Struct)
        return false;
    const ITypeDefinition* td = type.GetDefinition();
    if (td == nullptr)
        return false;
    return td->HasAttribute(KnownAttribute::InlineArray);
}

std::optional<int> GetInlineArrayLength(const IType& type)
{
    // C# `if (type.Kind != TypeKind.Struct) return null; var td = type.GetDefinition();
    // if (td == null) return null; var attr = td.GetAttribute(KnownAttribute.InlineArray);
    // return attr?.FixedArguments.FirstOrDefault().Value as int?;` -- the same kind + definition
    // guards as `IsInlineArrayType`, then the attribute's FIRST fixed argument (the positional
    // `[InlineArray(N)]` length) unboxed as an int.
    if (type.Kind() != TypeKind::Struct)
        return std::nullopt;
    const ITypeDefinition* td = type.GetDefinition();
    if (td == nullptr)
        return std::nullopt;
    const IAttribute* attr = td->GetAttribute(KnownAttribute::InlineArray);
    if (attr == nullptr)
        return std::nullopt;
    std::vector<CustomAttributeTypedArgument> fixedArgs = attr->FixedArguments();
    // C# `.FirstOrDefault()` on an empty list yields the default (null), whose `.Value` would
    // NRE -- but the C# `?.` chain makes a null `FirstOrDefault()` result skip the `.Value` and
    // the whole `as int?` yields null. The port short-circuits to nullopt for the empty list.
    if (fixedArgs.empty())
        return std::nullopt;
    // `Value()` returns the `std::any` BY VALUE; bind it to a local so the pointer-form
    // `any_cast` points into a live object (a temporary would dangle at the end of the
    // full expression).
    std::any value = fixedArgs.front().Value();
    // C# `.Value as int?` -- null when the boxed value is not an int. The pointer-form
    // `std::any_cast<int>` returns null on a type mismatch (the safe faithful fallback; the C#
    // would instead NRE-skip via the `?.` or throw InvalidCastException for a non-null
    // non-int value -- both unreachable for a decoded `[InlineArray(N)]` argument, which the
    // real decoder always boxes as int).
    const int* length = std::any_cast<int>(&value);
    if (length == nullptr)
        return std::nullopt;
    return *length;
}

ITypePtr GetInlineArrayElementType(const IType& arrayType)
{
    // C# `arrayType?.GetFields(f => !f.IsStatic).SingleOrDefault()?.Type ??
    // SpecialType.UnknownType` -- the instance fields (the `!f.IsStatic` filter) reduced with
    // `SingleOrDefault`: exactly one instance field yields its type, none yields the
    // `SpecialType.UnknownType` null object, and more than one throws. The filter is applied
    // by `IType::GetFields` itself (the port passes the same predicate); the reduction runs
    // over the returned snapshot. The C# receiver `?.` is unrepresentable with a `const IType&`
    // (a reference cannot bind to null; the callers always pass a live type).
    std::vector<const IField*> fields = arrayType.GetFields(
        [](const IField* f) { return !f->IsStatic(); });
    if (fields.size() > 1) {
        // C# `SingleOrDefault()` throws `InvalidOperationException` ("Sequence contains more
        // than one matching element") when the filtered list has more than one element; the
        // port throws the `std::runtime_error` analog (the `SimpleCompilation` /
        // `CreateResolveResult` InvalidOperationException convention). A real inline-array
        // struct carries exactly one instance field, so the throw guards the same metadata
        // invariant the C# does.
        throw std::runtime_error("Sequence contains more than one matching element");
    }
    if (fields.empty()) {
        // No instance field: the C# `?.Type` chain yields null and the `??` coalesces to
        // `SpecialType.UnknownType`; the port returns the `UnknownType()` null object.
        return UnknownType();
    }
    // The single instance field's type: an owning handle from the const `Type()` accessor via
    // `shared_from_this()` + `std::const_pointer_cast` (the field's type is a shared-managed
    // type-system object; the accessor's `const` is the contract, the `NullableType.Create`
    // precedent).
    const IType& fieldType = fields.front()->Type();
    return std::const_pointer_cast<IType>(fieldType.shared_from_this());
}

ITypePtr GetElementTypeFromIEnumerable(const IType& collectionType,
                                       const ICompilation& compilation,
                                       bool allowIEnumerator,
                                       std::optional<bool>& isGeneric)
{
    bool foundNonGenericIEnumerable = false;
    for (const IType* baseType : GetAllBaseTypes(collectionType)) {
        // The port's base-type snapshot may carry null entries for a degenerate
        // DirectBaseTypes graph; the C# would NRE on `baseType.GetDefinition()` -- the
        // D516 null-guard convention.
        if (baseType == nullptr)
            continue;
        const ITypeDefinition* baseTypeDef = baseType->GetDefinition();
        if (baseTypeDef != nullptr) {
            KnownTypeCode typeCode = baseTypeDef->KnownTypeCode();
            if (typeCode == KnownTypeCode::IEnumerableOfT
                || (allowIEnumerator && typeCode == KnownTypeCode::IEnumeratorOfT)) {
                // C# `ParameterizedType pt = baseType as ParameterizedType; if (pt != null)
                // { isGeneric = true; return pt.GetTypeArgument(0); }`. The bare
                // open-generic definition (not a ParameterizedType) falls through and
                // continues the walk; the non-empty-TypeArguments guard keeps the
                // GetTypeArgument(0) index in bounds for a degenerate zero-argument
                // ParameterizedType (the D516 safe-fallback convention).
                const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(baseType);
                if (pt != nullptr && !pt->TypeArguments().empty()) {
                    isGeneric = std::optional<bool>(true);
                    return pt->GetTypeArgument(0);
                }
            }
            if (typeCode == KnownTypeCode::IEnumerable
                || (allowIEnumerator && typeCode == KnownTypeCode::IEnumerator))
                foundNonGenericIEnumerable = true;
        }
    }
    // System.Collections.IEnumerable found in type hierarchy -> Object is element type.
    if (foundNonGenericIEnumerable) {
        isGeneric = std::optional<bool>(false);
        const IType& objectType = compilation.FindType(KnownTypeCode::Object);
        return std::const_pointer_cast<IType>(objectType.shared_from_this());
    }
    isGeneric = std::nullopt;
    return UnknownType();
}

} // namespace ILSpy::Decompiler::TypeSystem
