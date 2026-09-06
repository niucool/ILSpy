// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files ("the Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// MetadataProperty.cs -- see the header's port notes.

#include "Decompiler/TypeSystem/Implementation/MetadataProperty.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedProperty.hpp"
#include "Decompiler/TypeSystem/InheritanceHelper.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `internal MetadataProperty(MetadataModule module,
// PropertyDefinitionHandle handle)`.
MetadataProperty::MetadataProperty(const MetadataModule& module,
                                   std::uint32_t propertyToken)
    : module_(module),
      handle_(propertyToken),
      getter_(nullptr),
      setter_(nullptr),
      symbolKind_(::ILSpy::Decompiler::TypeSystem::SymbolKind::Property)
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    // The C# `var accessors = prop.GetAccessors(); getter =
    // module.GetDefinition(accessors.Getter); setter =
    // module.GetDefinition(accessors.Setter); name =
    // metadata.GetString(prop.Name);`
    Metadata::MetadataFile::PropertyAccessorsInfo accessors
        = metadata->GetPropertyAccessors(handle_);
    getter_ = module_.GetDefinitionMethod(accessors.GetterToken);
    setter_ = module_.GetDefinitionMethod(accessors.SetterToken);
    name_ = metadata->GetPropertyName(handle_);
    // The C# symbolKind chain: `if (DetermineIsIndexer(name))
    // symbolKind = SymbolKind.Indexer; else if (name.IndexOf('.') >= 0)
    // { var interfaceProp = this.ExplicitlyImplementedInterfaceMembers
    // .FirstOrDefault() as IProperty; symbolKind = interfaceProp?.SymbolKind
    // ?? SymbolKind.Property; } else symbolKind = SymbolKind.Property;`
    if (DetermineIsIndexer(name_))
    {
        symbolKind_ = ::ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer;
    }
    else if (name_.find('.') != std::string::npos)
    {
        // explicit interface implementation
        std::vector<const IMember*> eii
            = ExplicitlyImplementedInterfaceMembers();
        const IProperty* interfaceProp
            = eii.empty() ? nullptr
                          : dynamic_cast<const IProperty*>(eii.front());
        symbolKind_ = interfaceProp != nullptr
            ? interfaceProp->SymbolKind()
            : ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
}

// The C# `private bool DetermineIsIndexer(string name)`.
bool MetadataProperty::DetermineIsIndexer(const std::string& name) const
{
    // The C# `if (name != (DeclaringTypeDefinition as
    // MetadataTypeDefinition)?.DefaultMemberName) return false;` -- a null
    // DefaultMemberName (no [DefaultMember] row) never matches.
    const auto* mtd
        = dynamic_cast<const MetadataTypeDefinition*>(
            DeclaringTypeDefinition());
    if (mtd == nullptr)
        return false;
    std::optional<std::string> dmn = mtd->DefaultMemberName();
    if (!dmn.has_value() || name != *dmn)
        return false;
    // The C# `return Parameters.Count > 0;`.
    return !Parameters().empty();
}

// The C# `public override string ToString()`.
std::string MetadataProperty::ToString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", handle_);
    std::string decl = DeclaringType() != nullptr
        ? DeclaringType()->ReflectionName()
        : std::string();
    return std::string(buffer) + " " + decl + "." + name_;
}

::ILSpy::Decompiler::TypeSystem::SymbolKind
MetadataProperty::SymbolKind() const
{
    return symbolKind_;
}

std::string MetadataProperty::Name() const
{
    return name_;
}

// The C# `public Accessibility Accessibility`: the InvalidAccessibility
// sentinel read, the ComputeAccessibility walk on the first miss
// (convention (d)).
::ILSpy::Decompiler::TypeSystem::Accessibility
MetadataProperty::Accessibility() const
{
    if (!cachedAccessibility_.has_value())
        cachedAccessibility_ = ComputeAccessibility();
    return *cachedAccessibility_;
}

std::string MetadataProperty::FullName() const
{
    // The C# `DeclaringType?.FullName` -- the accessor's DeclaringType is
    // always the DEFINITION for metadata members, so the definition's
    // FullName carries the render (the port's IType omits FullName; the
    // MetadataField convention (h) null -> empty string).
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->FullName() : std::string()) + "."
        + name_;
}

std::string MetadataProperty::ReflectionName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->ReflectionName() : std::string()) + "."
        + name_;
}

std::string MetadataProperty::Namespace() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return decl != nullptr ? decl->Namespace() : std::string();
}

bool MetadataProperty::CanGet() const
{
    return getter_ != nullptr;
}

bool MetadataProperty::CanSet() const
{
    return setter_ != nullptr;
}

const IMethod* MetadataProperty::Getter() const
{
    return getter_;
}

const IMethod* MetadataProperty::Setter() const
{
    return setter_;
}

bool MetadataProperty::IsIndexer() const
{
    return symbolKind_
        == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer;
}

// The C# `public bool ReturnTypeIsRefReadOnly =>
// propertyDef.GetCustomAttributes().HasKnownAttribute(module.metadata,
// KnownAttribute.IsReadOnly)`.
bool MetadataProperty::ReturnTypeIsRefReadOnly() const
{
    return Metadata::HasKnownAttribute(*module_.MetadataFile(), handle_,
                                       KnownAttribute::IsReadOnly);
}

std::vector<const IParameter*> MetadataProperty::Parameters() const
{
    if (!signatureDecoded_)
        DecodeSignature();
    std::vector<const IParameter*> result;
    result.reserve(parameters_.size());
    for (const std::shared_ptr<const IParameter>& p : parameters_)
        result.push_back(p.get());
    return result;
}

const IType& MetadataProperty::ReturnType() const
{
    if (!signatureDecoded_)
        DecodeSignature();
    return *returnType_;
}

// The C# `private void DecodeSignature()`.
void MetadataProperty::DecodeSignature() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    ITypePtr returnType;
    std::vector<std::shared_ptr<const IParameter>> parameters;
    try
    {
        // The C# `var genericContext = new GenericContext(
        // DeclaringType.TypeParameters);` -- the null declaring type feeds
        // the empty class list (the C# null list).
        std::vector<const ITypeParameter*> typeParameters;
        if (DeclaringType() != nullptr)
            typeParameters = DeclaringType()->TypeParameters();
        GenericContext genericContext(std::move(typeParameters));
        // The C# `var signature = propertyDef.DecodeSignature(
        // module.TypeProvider, genericContext);` -- the property blob
        // decodes as a METHOD signature over its 0x08 header byte (the
        // SRM PropertyDefinition.DecodeSignature == DecodeMethodSignature
        // contract).
        auto blob = metadata->GetSignatureBlob(handle_);
        Metadata::SignatureTypeProviderDecoder<TypeProvider> decoder(
            const_cast<TypeProvider&>(module_.TypeProvider()), *metadata);
        Metadata::ProviderMethodSignature<ITypePtr> signature
            = decoder.DecodeMethodSignature(
                blob ? blob->data() : nullptr,
                blob ? blob->size() : 0, genericContext);
        Metadata::MetadataFile::PropertyAccessorsInfo accessors
            = metadata->GetPropertyAccessors(handle_);
        const ITypeDefinition* declTypeDef = DeclaringTypeDefinition();
        // The C# parameterHandles / nullableContext selection: the getter's
        // Param rows and [NullableContext], else the setter's, else null /
        // the declaring type's context.
        const std::vector<Metadata::ParameterInfo>* parameterHandles
            = nullptr;
        std::vector<Metadata::ParameterInfo> accessorParams;
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext
            = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
        if (accessors.GetterToken != 0)
        {
            accessorParams
                = metadata->GetParameters(accessors.GetterToken);
            parameterHandles = &accessorParams;
            std::optional<::ILSpy::Decompiler::TypeSystem::Nullability> ctx
                = Metadata::GetNullableContext(
                    *metadata, accessors.GetterToken);
            nullableContext = ctx.value_or(
                declTypeDef != nullptr
                    ? declTypeDef->NullableContext()
                    : ::ILSpy::Decompiler::TypeSystem::Nullability::
                        Oblivious);
        }
        else if (accessors.SetterToken != 0)
        {
            accessorParams
                = metadata->GetParameters(accessors.SetterToken);
            parameterHandles = &accessorParams;
            std::optional<::ILSpy::Decompiler::TypeSystem::Nullability> ctx
                = Metadata::GetNullableContext(
                    *metadata, accessors.SetterToken);
            nullableContext = ctx.value_or(
                declTypeDef != nullptr
                    ? declTypeDef->NullableContext()
                    : ::ILSpy::Decompiler::TypeSystem::Nullability::
                        Oblivious);
        }
        else
        {
            nullableContext
                = declTypeDef != nullptr
                    ? declTypeDef->NullableContext()
                    : ::ILSpy::Decompiler::TypeSystem::Nullability::
                        Oblivious;
        }
        // The C# `var typeOptions = module.OptionsForEntity(declTypeDef);`
        // -- the declaring type, not the property (the accessibility
        // recursion the C# comment documents; Roslyn's PEPropertySymbol
        // workaround).
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeOptions
            = module_.OptionsForEntity(declTypeDef);
        MetadataMethod::DecodedSignature decoded
            = MetadataMethod::DecodeSignature(
                module_, this, signature, parameterHandles, nullableContext,
                typeOptions,
                std::optional<std::vector<std::uint32_t>>(
                    metadata->GetCustomAttributeTokens(handle_)));
        returnType = std::move(decoded.ReturnType);
        parameters = std::move(decoded.Parameters);
    }
    catch (const std::invalid_argument&)
    {
        // The C# `catch (BadImageFormatException)` -- the family mapping
        // (convention (i)); every other type propagates.
        returnType = UnknownType();
        parameters.clear();
    }
    catch (const std::out_of_range&)
    {
        returnType = UnknownType();
        parameters.clear();
    }
    returnType_ = std::move(returnType);
    parameters_ = std::move(parameters);
    signatureDecoded_ = true;
}

const ITypeDefinition* MetadataProperty::DeclaringTypeDefinition() const
{
    // The C# `AnyAccessor?.DeclaringTypeDefinition`.
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr ? any->DeclaringTypeDefinition() : nullptr;
}

ITypePtr MetadataProperty::DeclaringType() const
{
    // The C# `AnyAccessor?.DeclaringType`.
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr ? any->DeclaringType() : ITypePtr();
}

const IModule* MetadataProperty::ParentModule() const
{
    return &module_;
}

const ICompilation& MetadataProperty::Compilation() const
{
    return module_.Compilation();
}

// The attribute members (convention (c)).
std::vector<const IAttribute*> MetadataProperty::GetAttributes() const
{
    if (!attributeListLoaded_)
    {
        AttributeListBuilder b(module_);
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        // The C# `if (IsIndexer && Name != "Item" &&
        // !IsExplicitInterfaceImplementation) b.Add(KnownAttribute.IndexerName,
        // KnownTypeCode.String, Name);`
        if (IsIndexer() && name_ != "Item"
            && !IsExplicitInterfaceImplementation())
        {
            b.Add(KnownAttribute::IndexerName, KnownTypeCode::String,
                  std::any(name_));
        }
        // The C# `if ((propertyDef.Attributes & (PropertyAttributes
        // .SpecialName | PropertyAttributes.RTSpecialName)) ==
        // PropertyAttributes.SpecialName) b.Add(KnownAttribute.SpecialName);`
        // -- the ECMA II.23.1 property flags (SpecialName 0x0200 /
        // RTSpecialName 0x0400, the ReflectionAttributes enum).
        constexpr std::uint32_t kSpecialName = 0x0200;
        constexpr std::uint32_t kRTSpecialName = 0x0400;
        std::uint32_t attributes = metadata->GetPropertyAttributes(handle_);
        if ((attributes & (kSpecialName | kRTSpecialName)) == kSpecialName)
        {
            b.Add(KnownAttribute::SpecialName);
        }
        // The C# `b.Add(propertyDef.GetCustomAttributes(), symbolKind);`.
        b.Add(handle_, symbolKind_);
        attributeList_ = b.Build();
        attributeListLoaded_ = true;
    }
    std::vector<const IAttribute*> result;
    result.reserve(attributeList_.size());
    for (const std::shared_ptr<IAttribute>& a : attributeList_)
        result.push_back(a.get());
    return result;
}

bool MetadataProperty::HasAttribute(KnownAttribute attribute) const
{
    // The C# split: the non-custom kinds consult the built list; the custom
    // kinds scan the rows (the AttributeListBuilder convention (d)).
    if (!IsCustomAttribute(attribute))
    {
        for (const IAttribute* attr : GetAttributes())
        {
            if (IsKnownType(attr->AttributeType(), attribute))
                return true;
        }
        return false;
    }
    AttributeListBuilder b(module_);
    return b.HasAttribute(*module_.MetadataFile(), handle_, attribute,
                          symbolKind_);
}

const IAttribute* MetadataProperty::GetAttribute(
    KnownAttribute attribute) const
{
    if (!IsCustomAttribute(attribute))
    {
        for (const IAttribute* attr : GetAttributes())
        {
            if (IsKnownType(attr->AttributeType(), attribute))
                return attr;
        }
        return nullptr;
    }
    AttributeListBuilder b(module_);
    std::shared_ptr<IAttribute> found = b.GetAttribute(
        *module_.MetadataFile(), handle_, attribute, symbolKind_);
    if (found == nullptr)
        return nullptr;
    foundAttributes_.push_back(std::move(found));
    return foundAttributes_.back().get();
}

// The flags: the AnyAccessor delegation (`AnyAccessor?.IsXxx ?? false`).
bool MetadataProperty::IsStatic() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsStatic();
}

bool MetadataProperty::IsAbstract() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsAbstract();
}

bool MetadataProperty::IsSealed() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsSealed();
}

std::vector<const IMember*>
MetadataProperty::ExplicitlyImplementedInterfaceMembers() const
{
    return GetInterfaceMembersFromAccessor(
        getter_ != nullptr ? getter_ : setter_);
}

bool MetadataProperty::IsExplicitInterfaceImplementation() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsExplicitInterfaceImplementation();
}

bool MetadataProperty::IsVirtual() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsVirtual();
}

bool MetadataProperty::IsOverride() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsOverride();
}

bool MetadataProperty::IsOverridable() const
{
    const IMethod* any = getter_ != nullptr ? getter_ : setter_;
    return any != nullptr && any->IsOverridable();
}

const IMember* MetadataProperty::MemberDefinition() const
{
    return this;
}

const TypeParameterSubstitution* MetadataProperty::Substitution() const
{
    return &TypeParameterSubstitution::Identity();
}

const IMember* MetadataProperty::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    // A null pointer is the Identity (the IMember::Specialize
    // nullable-parameter convention).
    TypeParameterSubstitution sub = (substitution != nullptr)
        ? *substitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    std::shared_ptr<IProperty> alias(
        static_cast<IProperty*>(const_cast<MetadataProperty*>(this)),
        [](IProperty*) {
            // no-op: the module's propertyDefs_ cache owns this instance
        });
    std::shared_ptr<IProperty> result
        = SpecializedProperty::Create(std::move(alias), std::move(sub));
    const IMember* raw = result.get();
    if (raw != static_cast<const IProperty*>(this))
    {
        // A fresh `SpecializedProperty` (the Identity / declaring-tpc-0
        // arms return `this` itself, needing no registry slot).
        specializedMembers_.push_back(std::move(result));
    }
    return raw;
}

bool MetadataProperty::Equals(const IMember* obj,
    const TypeVisitor* /*typeNormalization*/) const
{
    return Equals(dynamic_cast<const MetadataProperty*>(obj));
}

bool MetadataProperty::Equals(const MetadataProperty* other) const
{
    if (other == nullptr)
        return false;
    return handle_ == other->handle_
        && module_.MetadataFile() == other->module_.MetadataFile();
}

// The C# `public override int GetHashCode() => 0x32b6a76c ^
// module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- the
// MetadataFile identity is the pointer (the MetadataField convention (g)).
int MetadataProperty::GetHashCode() const
{
    return static_cast<int>(0x32b6a76cu
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

// The C# `private Accessibility ComputeAccessibility()` (convention (d)).
::ILSpy::Decompiler::TypeSystem::Accessibility
MetadataProperty::ComputeAccessibility() const
{
    if (IsOverride() && (getter_ == nullptr || setter_ == nullptr))
    {
        // Overrides may override only one of the accessors, hence
        // calculating the accessibility from the declared accessors is not
        // sufficient. We need to "copy" accessibility from the
        // baseMember.
        for (const IMember* baseMember :
             InheritanceHelper::GetBaseMembers(*this, false))
        {
            if (!baseMember->IsOverride())
            {
                // See https://github.com/icsharpcode/ILSpy/issues/2653
                // "protected internal" (ProtectedOrInternal) accessibility
                // is "reduced" to "protected" accessibility across assembly
                // boundaries.
                if (baseMember->Accessibility()
                        == ::ILSpy::Decompiler::TypeSystem::Accessibility::
                            ProtectedOrInternal
                    && ParentModule()->MetadataFile()
                        != baseMember->ParentModule()->MetadataFile())
                {
                    return ::ILSpy::Decompiler::TypeSystem::Accessibility::
                        Protected;
                }
                return baseMember->Accessibility();
            }
        }
    }
    return Union(
        getter_ != nullptr
            ? getter_->Accessibility()
            : ::ILSpy::Decompiler::TypeSystem::Accessibility::None,
        setter_ != nullptr
            ? setter_->Accessibility()
            : ::ILSpy::Decompiler::TypeSystem::Accessibility::None);
}

// The C# `internal static IEnumerable<IMember>
// GetInterfaceMembersFromAccessor(IMethod method)` -- the shared EII
// projection (the MetadataProperty.hpp convention (h)).
std::vector<const IMember*> GetInterfaceMembersFromAccessor(
    const IMethod* method)
{
    if (method == nullptr)
        return {};
    std::vector<const IMember*> result;
    for (const IMember* m : method->ExplicitlyImplementedInterfaceMembers())
    {
        // The C# `((IMethod)m).AccessorOwner` -- the hard cast throws the
        // .NET InvalidCastException for a non-method member; unreachable
        // through the GetOverrides walk (every entry is a resolved method).
        const IMethod* im = dynamic_cast<const IMethod*>(m);
        if (im == nullptr)
        {
            throw std::runtime_error("Specified cast is not valid.");
        }
        const IMember* owner = im->AccessorOwner();
        if (owner != nullptr)
            result.push_back(owner);
    }
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
