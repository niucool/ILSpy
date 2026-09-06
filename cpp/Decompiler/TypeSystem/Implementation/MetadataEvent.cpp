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

// MetadataEvent.cs -- see the header's port notes.

#include "Decompiler/TypeSystem/Implementation/MetadataEvent.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedEvent.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `internal MetadataEvent(MetadataModule module,
// EventDefinitionHandle handle)`.
MetadataEvent::MetadataEvent(const MetadataModule& module,
                             std::uint32_t eventToken)
    : module_(module), handle_(eventToken)
{
    // The C# `name = metadata.GetString(ev.Name);` -- the accessors resolve
    // per read through the method entity cache (convention (a)).
    name_ = module_.MetadataFile()->GetEventName(handle_);
}

// The C# `public override string ToString()`.
std::string MetadataEvent::ToString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", handle_);
    std::string decl = DeclaringType() != nullptr
        ? DeclaringType()->ReflectionName()
        : std::string();
    return std::string(buffer) + " " + decl + "." + name_;
}

// The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Event`.
::ILSpy::Decompiler::TypeSystem::SymbolKind MetadataEvent::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event;
}

std::string MetadataEvent::Name() const
{
    return name_;
}

// The C# `public Accessibility Accessibility => AnyAccessor?.Accessibility
// ?? Accessibility.None` -- no caching (the property sibling's
// ComputeAccessibility walk does not apply to events).
::ILSpy::Decompiler::TypeSystem::Accessibility
MetadataEvent::Accessibility() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr
        ? any->Accessibility()
        : ::ILSpy::Decompiler::TypeSystem::Accessibility::None;
}

std::string MetadataEvent::FullName() const
{
    // The C# `DeclaringType?.FullName` over the definition (the
    // MetadataProperty sibling's convention note).
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->FullName() : std::string()) + "."
        + name_;
}

std::string MetadataEvent::ReflectionName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->ReflectionName() : std::string()) + "."
        + name_;
}

std::string MetadataEvent::Namespace() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return decl != nullptr ? decl->Namespace() : std::string();
}

bool MetadataEvent::CanAdd() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    return metadata->GetEventAccessors(handle_).AdderToken != 0;
}

bool MetadataEvent::CanRemove() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    return metadata->GetEventAccessors(handle_).RemoverToken != 0;
}

bool MetadataEvent::CanInvoke() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    return metadata->GetEventAccessors(handle_).RaiserToken != 0;
}

const IMethod* MetadataEvent::AddAccessor() const
{
    // The C# `module.GetDefinition(accessors.Adder)` -- the per-read
    // re-resolution (convention (a); the entity cache makes the identity
    // stable).
    return module_.GetDefinitionMethod(
        module_.MetadataFile()->GetEventAccessors(handle_).AdderToken);
}

const IMethod* MetadataEvent::RemoveAccessor() const
{
    return module_.GetDefinitionMethod(
        module_.MetadataFile()->GetEventAccessors(handle_).RemoverToken);
}

const IMethod* MetadataEvent::InvokeAccessor() const
{
    return module_.GetDefinitionMethod(
        module_.MetadataFile()->GetEventAccessors(handle_).RaiserToken);
}

// The C# `private IMethod AnyAccessor => module.GetDefinition(
// accessors.GetAny())` -- the C# EventAccessors.GetAny() is adder ??
// remover ?? raiser.
const IMethod* MetadataEvent::AnyAccessor() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    Metadata::MetadataFile::EventAccessorsInfo accessors
        = metadata->GetEventAccessors(handle_);
    std::uint32_t any = accessors.AdderToken != 0
        ? accessors.AdderToken
        : (accessors.RemoverToken != 0
            ? accessors.RemoverToken
            : accessors.RaiserToken);
    return module_.GetDefinitionMethod(any);
}

// The C# `public IType ReturnType` -- the lazy module.ResolveType walk
// (convention (b)).
const IType& MetadataEvent::ReturnType() const
{
    if (returnType_ == nullptr)
    {
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        std::uint32_t eventType = metadata->GetEventTypeToken(handle_);
        const ITypeDefinition* declaringTypeDef = DeclaringTypeDefinition();
        // The C# `var context = new GenericContext(
        // declaringTypeDef?.TypeParameters);` -- the null declaring type
        // feeds the empty class list (the C# null list).
        std::vector<const ITypeParameter*> typeParameters;
        if (declaringTypeDef != nullptr)
            typeParameters = declaringTypeDef->TypeParameters();
        GenericContext context(std::move(typeParameters));
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext
            = declaringTypeDef != nullptr
                ? declaringTypeDef->NullableContext()
                : ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
        // The event does not have explicit accessibility in metadata, so use
        // its containing type to determine whether nullability applies to
        // this type.
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeOptions
            = module_.OptionsForEntity(declaringTypeDef);
        returnType_ = module_.ResolveType(
            eventType, context, typeOptions,
            std::optional<std::vector<std::uint32_t>>(
                metadata->GetCustomAttributeTokens(handle_)),
            nullableContext);
        if (returnType_ == nullptr)
            returnType_ = UnknownType();
    }
    return *returnType_;
}

const ITypeDefinition* MetadataEvent::DeclaringTypeDefinition() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr ? any->DeclaringTypeDefinition() : nullptr;
}

ITypePtr MetadataEvent::DeclaringType() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr ? any->DeclaringType() : ITypePtr();
}

const IModule* MetadataEvent::ParentModule() const
{
    return &module_;
}

const ICompilation& MetadataEvent::Compilation() const
{
    return module_.Compilation();
}

// The attribute members (convention (c)).
std::vector<const IAttribute*> MetadataEvent::GetAttributes() const
{
    if (!attributeListLoaded_)
    {
        AttributeListBuilder b(module_);
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        // The C# `if ((eventDef.Attributes & (EventAttributes.SpecialName |
        // EventAttributes.RTSpecialName)) == EventAttributes.SpecialName)
        // b.Add(KnownAttribute.SpecialName);` -- the ECMA II.23.1 event
        // flags (SpecialName 0x0200 / RTSpecialName 0x0400, the
        // ReflectionAttributes enum).
        constexpr std::uint32_t kSpecialName = 0x0200;
        constexpr std::uint32_t kRTSpecialName = 0x0400;
        std::uint32_t attributes = metadata->GetEventAttributes(handle_);
        if ((attributes & (kSpecialName | kRTSpecialName)) == kSpecialName)
        {
            b.Add(KnownAttribute::SpecialName);
        }
        // The C# `b.Add(eventDef.GetCustomAttributes(), SymbolKind.Event);`.
        b.Add(handle_,
              ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event);
        attributeList_ = b.Build();
        attributeListLoaded_ = true;
    }
    std::vector<const IAttribute*> result;
    result.reserve(attributeList_.size());
    for (const std::shared_ptr<IAttribute>& a : attributeList_)
        result.push_back(a.get());
    return result;
}

bool MetadataEvent::HasAttribute(KnownAttribute attribute) const
{
    // The C# split (the AttributeListBuilder convention (d)).
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
                          ::ILSpy::Decompiler::TypeSystem::SymbolKind::
                              Event);
}

const IAttribute* MetadataEvent::GetAttribute(
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
        *module_.MetadataFile(), handle_, attribute,
        ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event);
    if (found == nullptr)
        return nullptr;
    foundAttributes_.push_back(std::move(found));
    return foundAttributes_.back().get();
}

// The flags: the AnyAccessor delegation (`AnyAccessor?.IsXxx ?? false`).
bool MetadataEvent::IsStatic() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsStatic();
}

bool MetadataEvent::IsAbstract() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsAbstract();
}

bool MetadataEvent::IsSealed() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsSealed();
}

std::vector<const IMember*>
MetadataEvent::ExplicitlyImplementedInterfaceMembers() const
{
    return GetInterfaceMembersFromAccessor(AnyAccessor());
}

bool MetadataEvent::IsExplicitInterfaceImplementation() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsExplicitInterfaceImplementation();
}

bool MetadataEvent::IsVirtual() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsVirtual();
}

bool MetadataEvent::IsOverride() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsOverride();
}

bool MetadataEvent::IsOverridable() const
{
    const IMethod* any = AnyAccessor();
    return any != nullptr && any->IsOverridable();
}

const IMember* MetadataEvent::MemberDefinition() const
{
    return this;
}

const TypeParameterSubstitution* MetadataEvent::Substitution() const
{
    return &TypeParameterSubstitution::Identity();
}

const IMember* MetadataEvent::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    // A null pointer is the Identity (the IMember::Specialize
    // nullable-parameter convention).
    TypeParameterSubstitution sub = (substitution != nullptr)
        ? *substitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    std::shared_ptr<IEvent> alias(
        static_cast<IEvent*>(const_cast<MetadataEvent*>(this)),
        [](IEvent*) {
            // no-op: the module's eventDefs_ cache owns this instance
        });
    std::shared_ptr<IEvent> result
        = SpecializedEvent::Create(std::move(alias), std::move(sub));
    const IMember* raw = result.get();
    if (raw != static_cast<const IEvent*>(this))
    {
        // A fresh `SpecializedEvent` (the Identity / declaring-tpc-0 arms
        // return `this` itself, needing no registry slot).
        specializedMembers_.push_back(std::move(result));
    }
    return raw;
}

bool MetadataEvent::Equals(const IMember* obj,
    const TypeVisitor* /*typeNormalization*/) const
{
    return Equals(dynamic_cast<const MetadataEvent*>(obj));
}

bool MetadataEvent::Equals(const MetadataEvent* other) const
{
    if (other == nullptr)
        return false;
    return handle_ == other->handle_
        && module_.MetadataFile() == other->module_.MetadataFile();
}

// The C# `public override int GetHashCode() => 0x7937039a ^
// module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- the
// MetadataFile identity is the pointer (the MetadataField convention (f)).
int MetadataEvent::GetHashCode() const
{
    return static_cast<int>(0x7937039au
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
