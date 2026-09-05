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

#include "Decompiler/Metadata/AssemblyNameInfo.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/Implementation/BaseTypeCollector.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"  // GetTypeName (IsKnownType)
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

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

// The C# `internal static bool IsKnownType(this IType type, KnownAttribute
// knownType)` (line 380): `type.GetDefinition()?.FullTypeName.IsKnownType(
// knownType)`, where the FullTypeName form compares against the attribute's
// `GetTypeName()` table row (a TopLevelTypeName -- compared as a
// NON-NESTED FullTypeName, the implicit conversion the C# applies).
bool IsKnownType(const IType& type, KnownAttribute knownType) {
    const ITypeDefinition* def = type.GetDefinition();
    if (def == nullptr) return false;
    return def->FullTypeName() == FullTypeName(GetTypeName(knownType));
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

bool IsUnbound(const IType& type)
{
    // The C# `(type is ITypeDefinition || type is UnknownType) && type.TypeParameterCount > 0`.
    // An `UnknownType` is NOT an `ITypeDefinition`, so both casts are needed; the
    // port's `SpecialType(TypeKind::Unknown)` null object matches neither cast, but
    // its `TypeParameterCount()` is 0 so the conjunction is false either way.
    const bool isDefinitionOrUnknown =
        dynamic_cast<const ITypeDefinition*>(&type) != nullptr
        || dynamic_cast<const class UnknownType*>(&type) != nullptr;
    return isDefinitionOrUnknown && type.TypeParameterCount() > 0;
}

namespace {

// The C# `static ITypeDefinition FindNestedType(ITypeDefinition typeDef, string name,
// int typeParameterCount)` (TypeSystemExtensions.cs line 536) -- the first nested
// type whose name AND type-parameter count match.
const ITypeDefinition* FindNestedType(const ITypeDefinition& typeDef,
                                      const std::string& name,
                                      int typeParameterCount)
{
    for (const ITypeDefinition* nestedType : typeDef.NestedTypes()) {
        if (nestedType != nullptr && nestedType->Name() == name
            && nestedType->TypeParameterCount() == typeParameterCount) {
            return nestedType;
        }
    }
    return nullptr;
}

} // namespace

const ITypeDefinition* GetTypeDefinition(const IModule& module, const FullTypeName& fullTypeName)
{
    const TopLevelTypeName& topLevelTypeName = fullTypeName.GetTopLevelTypeName();
    const ITypeDefinition* typeDef = module.GetTypeDefinition(topLevelTypeName);
    if (typeDef == nullptr)
        return nullptr;
    int typeParameterCount = topLevelTypeName.TypeParameterCount();
    for (int i = 0; i < fullTypeName.NestingLevel(); i++) {
        const std::string name = fullTypeName.GetNestedTypeName(i);
        typeParameterCount += fullTypeName.GetNestedTypeAdditionalTypeParameterCount(i);
        typeDef = FindNestedType(*typeDef, name, typeParameterCount);
        if (typeDef == nullptr)
            break;
    }
    return typeDef;
}

const IModule* FindModuleByAssemblyNameInfo(
    const ICompilation& compilation,
    const ::ILSpy::Decompiler::Metadata::AssemblyNameInfo& assemblyName)
{
    // The C# first pass: `string.Equals(module.FullAssemblyName, assemblyName.FullName,
    // StringComparison.OrdinalIgnoreCase)` over every module; the first match wins.
    const StringComparer& ignoreCase = StringComparer::OrdinalIgnoreCase();
    for (const IModule* module : compilation.Modules()) {
        if (ignoreCase.Equals(module->FullAssemblyName(), assemblyName.FullName()))
            return module;
    }
    // The C# second pass: the same scan over the short `Name`. Only reached when the
    // FullName pass missed every module (a FullName match takes precedence even when a
    // LATER module's short name would also match).
    for (const IModule* module : compilation.Modules()) {
        if (ignoreCase.Equals(module->Name(), assemblyName.Name()))
            return module;
    }
    return nullptr;
}

const IModule* FindModuleByReference(
    const ICompilation& compilation,
    const ::ILSpy::Decompiler::Metadata::IAssemblyReference& assemblyName)
{
    // The C# first pass: `string.Equals(module.FullAssemblyName, assemblyName.FullName,
    // StringComparison.OrdinalIgnoreCase)` over every module; the first match wins.
    const StringComparer& ignoreCase = StringComparer::OrdinalIgnoreCase();
    for (const IModule* module : compilation.Modules()) {
        if (ignoreCase.Equals(module->FullAssemblyName(), assemblyName.FullName()))
            return module;
    }
    // The C# second pass: the same scan over the short `Name`. Only reached when the
    // FullName pass missed every module (a FullName match takes precedence even when a
    // LATER module's short name would also match).
    for (const IModule* module : compilation.Modules()) {
        if (ignoreCase.Equals(module->Name(), assemblyName.Name()))
            return module;
    }
    return nullptr;
}

// The C# `IsCompilerGeneratedOrIsInCompilerGeneratedClass` (NRExtensions.cs lines
// 26-46): the entity's own `[CompilerGenerated]`, else (recursively up the nesting
// chain) its declaring type definition's.
bool IsCompilerGeneratedOrIsInCompilerGeneratedClass(const IEntity* entity)
{
    if (entity == nullptr)
        return false;
    // The C# private `IsCompilerGenerated` sub-extension: `HasAttribute(CompilerGenerated)`.
    if (entity->HasAttribute(KnownAttribute::CompilerGenerated))
        return true;
    return IsCompilerGeneratedOrIsInCompilerGeneratedClass(entity->DeclaringTypeDefinition());
}

// The C# `IsPotentialClosure` (TransformDisplayClassUsage.cs): the display-class
// shape + compiler-generated + same-nesting-tree checks.
bool IsPotentialClosure(const ITypeDefinition* decompiledTypeDefinition,
                        const ITypeDefinition* potentialDisplayClass,
                        bool allowTypeImplementingInterfaces)
{
    if (potentialDisplayClass == nullptr
        || !IsCompilerGeneratedOrIsInCompilerGeneratedClass(potentialDisplayClass))
        return false;
    switch (potentialDisplayClass->Kind()) {
        case TypeKind::Struct:
            break;
        case TypeKind::Class:
            if (!allowTypeImplementingInterfaces) {
                // The C# `!potentialDisplayClass.DirectBaseTypes.All(t =>
                // t.IsKnownType(KnownTypeCode.Object))` -- a display class extends
                // nothing but `object`. A null base-type entry is not `object`, so the
                // unguarded `IsKnownType` read is safe (the `&*` of a null entry would
                // be UB; the D516 null-guard convention keeps the linear scan skip
                // instead).
                for (const ITypePtr& base : potentialDisplayClass->DirectBaseTypes()) {
                    if (!base || !IsKnownType(*base, KnownTypeCode::Object))
                        return false;
                }
            }
            break;
        default:
            return false;
    }

    // C# comment: "Make sure that potentialDisplayClass and decompiledTypeDefinition
    // are part of the same type tree. Either decompiledTypeDefinition is an ancestor
    // type of potentialDisplayClass or both have at least one common ancestor."
    // The C# collects the display class's STRICT ancestors into a
    // `HashSet<ITypeDefinition>` (reference equality), then walks
    // `decompiledTypeDefinition` and its ancestors looking for a set member -- the
    // walk INCLUDES `decompiledTypeDefinition` itself.
    std::vector<const ITypeDefinition*> potentialDisplayClassAncestors;
    const ITypeDefinition* potentialDisplayClassParent =
        potentialDisplayClass->DeclaringTypeDefinition();
    while (potentialDisplayClassParent != nullptr) {
        // The `HashSet.Add` dedup (reference equality) -- the linear scan is the
        // vector `Contains` (the chains are nesting-depth sized).
        if (std::find(potentialDisplayClassAncestors.begin(),
                      potentialDisplayClassAncestors.end(),
                      potentialDisplayClassParent)
            == potentialDisplayClassAncestors.end())
            potentialDisplayClassAncestors.push_back(potentialDisplayClassParent);
        potentialDisplayClassParent = potentialDisplayClassParent->DeclaringTypeDefinition();
    }

    const ITypeDefinition* decompiledTypeDefinitionOrAncestor = decompiledTypeDefinition;
    while (decompiledTypeDefinitionOrAncestor != nullptr) {
        if (std::find(potentialDisplayClassAncestors.begin(),
                      potentialDisplayClassAncestors.end(),
                      decompiledTypeDefinitionOrAncestor)
            != potentialDisplayClassAncestors.end())
            return true;
        decompiledTypeDefinitionOrAncestor =
            decompiledTypeDefinitionOrAncestor->DeclaringTypeDefinition();
    }
    return false;
}

// The C# `IsClosureParameter` (LocalFunctionDecompiler.cs line 575): a by-reference
// parameter whose element type resolves to a Struct-kind potential closure of the
// current type.
bool IsClosureParameter(const IParameter* parameter,
                        const ITypeDefinition* currentTypeDefinition)
{
    // The C# `parameter.Type is not ByReferenceType brt` -- a plain RTTI test (no
    // modifier unwrap).
    const ByReferenceType* brt = dynamic_cast<const ByReferenceType*>(&parameter->Type());
    if (brt == nullptr)
        return false;
    // The C# `brt.ElementType.GetDefinition()` -- a null element (a degenerate
    // ByReferenceType that does not occur in practice) would NRE in the C#; the
    // guard yields a null definition and the null check below returns false (the
    // D516 convention).
    const ITypeDefinition* type =
        brt->Element() ? brt->Element()->GetDefinition() : nullptr;
    return type != nullptr
        && type->Kind() == TypeKind::Struct
        && IsPotentialClosure(currentTypeDefinition, type);
}

namespace {

// The C# local function `DefaultValueAssignmentAllowedIndividual`
// (TypeSystemExtensions.cs lines 695-698): optional + constant-in-signature + a
// by-value / `in` / `ref readonly` reference kind.
bool DefaultValueAssignmentAllowedIndividual(const IParameter& parameter)
{
    return parameter.IsOptional() && parameter.HasConstantValueInSignature()
        && (parameter.ReferenceKind() == ReferenceKind::None
            || parameter.ReferenceKind() == ReferenceKind::In
            || parameter.ReferenceKind() == ReferenceKind::RefReadOnly);
}

} // namespace

// The C# `IsDefaultValueAssignmentAllowed` (TypeSystemExtensions.cs line 681).
bool IsDefaultValueAssignmentAllowed(const IParameter& parameter)
{
    if (!DefaultValueAssignmentAllowedIndividual(parameter))
        return false;

    const IParameterizedMember* owner = parameter.Owner();
    if (owner == nullptr)
        return true; // Shouldn't happen, but we need to check for it.

    const std::vector<const IParameter*> parameters = owner->Parameters();
    for (int i = static_cast<int>(parameters.size()) - 1; i >= 0; i--) {
        const IParameter* otherParameter = parameters[i];
        // Reached the parameter itself -- every subsequent (later-position)
        // parameter has been checked.
        if (otherParameter == &parameter)
            break;

        // The C# `LocalFunctionDecompiler.IsClosureParameter(otherParameter,
        // otherParameter.Owner.DeclaringTypeDefinition)` derefs the subsequent
        // parameter's owner unconditionally (the parameter comes from the owner's
        // own list, so the owner is non-null in practice); the port guards the
        // degenerate stub shape (a null owner or declaring type skips the closure
        // check and falls through to the individual / params tests -- the D516
        // convention).
        if (otherParameter->Owner() != nullptr
            && otherParameter->Owner()->DeclaringTypeDefinition() != nullptr
            && IsClosureParameter(otherParameter,
                                  otherParameter->Owner()->DeclaringTypeDefinition()))
            continue;

        if (DefaultValueAssignmentAllowedIndividual(*otherParameter)
            || otherParameter->IsParams())
            continue;

        return false;
    }
    return true;
}

// The C# `IsParameterizedProperty` (TypeSystemExtensions.cs line 180).
bool IsParameterizedProperty(const IProperty& property)
{
    return property.SymbolKind() == SymbolKind::Property
        && !property.Parameters().empty();
}

// The C# `HasReadonlyModifier` (TypeSystemExtensions.cs line 443).
bool HasReadonlyModifier(const IMethod& accessor)
{
    // The C# `accessor.DeclaringTypeDefinition?.IsReadOnly == false` is the
    // lifted-bool `==` (true only for a definite false): a null declaring type
    // definition fails the comparison, so the port is the null check plus the
    // negation (a `readonly struct` definition also yields false).
    const ITypeDefinition* declaringTypeDefinition = accessor.DeclaringTypeDefinition();
    return accessor.ThisIsRefReadOnly() && declaringTypeDefinition != nullptr
        && !declaringTypeDefinition->IsReadOnly();
}

// The C# `public static IType AsParameterizedType(this ITypeDefinition td)`
// (TypeSystemExtensions.cs line 864) -- the self-parameterized type of a
// generic type definition (the type of `this` within the type definition),
// or the definition itself when non-generic.
ITypePtr AsParameterizedType(const ITypeDefinition& td)
{
    if (td.TypeParameterCount() == 0) {
        // The C# `return td;` -- the definition unchanged; the owning handle
        // through the D529 shared_from_this + const_pointer_cast convention.
        return std::const_pointer_cast<IType>(td.shared_from_this());
    }
    // The C# `new ParameterizedType(td, td.TypeArguments)` -- the definition's
    // own type parameters as the type arguments (the C#
    // `ITypeDefinition.TypeArguments => TypeParameters`).
    std::vector<ITypePtr> typeArguments;
    const std::vector<const ITypeParameter*> typeParameters = td.TypeParameters();
    typeArguments.reserve(typeParameters.size());
    for (const ITypeParameter* tp : typeParameters) {
        if (tp == nullptr)
            continue; // the D516 null-entry guard
        typeArguments.push_back(std::const_pointer_cast<IType>(tp->shared_from_this()));
    }
    return std::make_shared<ParameterizedType>(
        std::const_pointer_cast<IType>(td.shared_from_this()),
        std::move(typeArguments));
}

} // namespace ILSpy::Decompiler::TypeSystem
