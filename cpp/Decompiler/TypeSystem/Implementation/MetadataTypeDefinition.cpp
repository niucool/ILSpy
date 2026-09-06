// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"

#include "Decompiler/Metadata/MetadataExtensions.hpp"  // ToKnownTypeCode
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/GetMembersHelper.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeParameter.hpp"

#include <cstdio>
#include <optional>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The non-owning snapshot of a module-owned type (the C# `return this;` /
// `return baseType;` over the GC-owned reference): the modules own their
// entities, so the returned `ITypePtr` aliases with a no-op deleter -- the
// KnownTypeCache / ReflectionHelper convention.
ITypePtr SnapshotType(const IType* type) {
    return ITypePtr(const_cast<IType*>(type), [](IType*) {
        // no-op: the compilation's type system owns the type
    });
}

} // namespace

// The ctor (convention (b)): the eagerly-computed identity surface. The
// member-init order matters: `module_` initializes first (declaration
// order), so the MetadataTypeParameter factories can read
// `owner->Compilation()` / `owner->SymbolKind()` through the owner (`this`)
// mid-construction -- the vtable is MetadataTypeDefinition's from ctor entry
// and `module_` is set (the C# has the same `this`-as-owner shape).
MetadataTypeDefinition::MetadataTypeDefinition(const MetadataModule& module,
                                               std::uint32_t typeDefToken)
    : module_(module), handle_(typeDefToken)
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    // `var td = metadata.GetTypeDefinition(handle); this.attributes =
    // td.Attributes;` -- the raw TypeAttributes column.
    attributes_ = metadata->GetTypeDefAttributes(handle_);
    // `this.fullTypeName = td.GetFullTypeName(metadata);`
    fullTypeName_ = Metadata::GetFullTypeNameFromDefinition(*metadata, handle_);
    // `this.MetadataName = metadata.GetString(td.Name);` -- the AUTHORED
    // short name (the backtick suffix kept, distinct from Name()).
    auto nameInfo = metadata->GetTypeDefNameInfo(handle_);
    metadataName_ = nameInfo ? nameInfo->Name : std::string();

    // Find DeclaringType + KnownTypeCode:
    if (fullTypeName_.IsNested())
    {
        // `this.DeclaringTypeDefinition = module.GetDefinition(
        // td.GetDeclaringType());` -- the NestedClass-table declaring token
        // the name info read carries (0 only for corrupt metadata that the
        // full-name walk already rejected).
        std::uint32_t declaringToken =
            nameInfo ? nameInfo->DeclaringTypeToken : 0;
        declaringTypeDefinition_ = module_.GetDefinition(declaringToken);
        // `this.TypeParameters = MetadataTypeParameter.Create(module,
        // this.DeclaringTypeDefinition, this, td.GetGenericParameters());`
        typeParameters_ = MetadataTypeParameter::Create(
            module_, declaringTypeDefinition_, this,
            metadata->GetGenericParameters(handle_));
        // `this.NullableContext = ... ?? this.DeclaringTypeDefinition
        // .NullableContext` -- DEFERRED (convention (d)): Oblivious.
    }
    else
    {
        // `this.TypeParameters = MetadataTypeParameter.Create(module, this,
        // td.GetGenericParameters());`
        typeParameters_ = MetadataTypeParameter::Create(
            module_, this, metadata->GetGenericParameters(handle_));
        // `module.NullableContext` -- DEFERRED (convention (d)): Oblivious.

        // `var topLevelTypeName = fullTypeName.TopLevelTypeName; for (int i =
        // 0; i < KnownTypeReference.KnownTypeCodeCount; i++) { var ktr =
        // KnownTypeReference.Get((KnownTypeCode)i); if (ktr != null &&
        // ktr.TypeName == topLevelTypeName) { this.KnownTypeCode =
        // (KnownTypeCode)i; break; } }`
        const TopLevelTypeName& topLevelTypeName =
            fullTypeName_.GetTopLevelTypeName();
        for (std::size_t i = 0; i < KnownTypeTableSize(); i++)
        {
            const KnownTypeReference* reference =
                KnownTypeReference::Get(static_cast<
                    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode>(i));
            if (reference != nullptr && reference->TypeName() == topLevelTypeName)
            {
                knownTypeCode_ = static_cast<
                    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode>(i);
                break;
            }
        }
    }

    // Find type kind:
    if ((attributes_ & 0x20u) == 0x20u)  // ClassSemanticsMask == Interface
    {
        kind_ = TypeKind::Interface;
    }
    else
    {
        Metadata::PrimitiveTypeCode underlyingType;
        if (Metadata::IsEnum(*metadata, handle_, underlyingType))
        {
            kind_ = TypeKind::Enum;
            // `this.EnumUnderlyingType = module.Compilation.FindType(
            // underlyingType.ToKnownTypeCode());` -- the FindType result
            // aliased non-owning (the compilation's type system owns it).
            enumUnderlyingType_ = SnapshotType(
                &module_.Compilation().FindType(
                    Metadata::ToKnownTypeCode(underlyingType)));
        }
        else if (Metadata::IsValueType(*metadata, handle_))
        {
            if (knownTypeCode_
                == ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void)
            {
                kind_ = TypeKind::Void;
            }
            else
            {
                kind_ = TypeKind::Struct;
                // `this.IsByRefLike = (module.TypeSystemOptions &
                // RefStructs) == RefStructs && td.GetCustomAttributes()
                // .HasKnownAttribute(metadata, KnownAttribute.IsByRefLike);`
                isByRefLike_ =
                    (module_.TypeSystemOptions()
                     & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                         RefStructs)
                        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                            RefStructs
                    && Metadata::HasKnownAttribute(
                        *metadata, handle_, KnownAttribute::IsByRefLike);
                isReadOnly_ =
                    (module_.TypeSystemOptions()
                     & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                         ReadOnlyStructsAndParameters)
                        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                            ReadOnlyStructsAndParameters
                    && Metadata::HasKnownAttribute(
                        *metadata, handle_, KnownAttribute::IsReadOnly);
            }
        }
        else if (Metadata::IsDelegate(*metadata, handle_))
        {
            kind_ = TypeKind::Delegate;
        }
        else
        {
            kind_ = TypeKind::Class;
            // `this.HasExtensions = this.IsStatic && (module.TypeSystemOptions
            // & ExtensionMethods) == ExtensionMethods && ...HasKnownAttribute(
            // ..., KnownAttribute.Extension);`
            hasExtensions_ =
                IsStatic()
                && (module_.TypeSystemOptions()
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        ExtensionMethods)
                       == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                           ExtensionMethods
                && Metadata::HasKnownAttribute(
                    *metadata, handle_, KnownAttribute::Extension);
        }
    }
}

// The C# `public override string ToString()`.
std::string MetadataTypeDefinition::ToString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", handle_);
    // The C# interpolation renders fullTypeName.ToString() == ReflectionName.
    return std::string(buffer) + " " + fullTypeName_.ReflectionName();
}

// The C# `public override int GetHashCode()` (out-of-line: the header
// declaration keeps `MetadataModule` incomplete).
int MetadataTypeDefinition::GetHashCode() const {
    return static_cast<int>(0x2e0520f2u
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

// --- ISymbol / ICompilationProvider ---

::ILSpy::Decompiler::TypeSystem::SymbolKind
MetadataTypeDefinition::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
}

const ICompilation& MetadataTypeDefinition::Compilation() const
{
    return module_.Compilation();
}

// --- INamedElement ---

// The C# `public string FullName`: the declaring type's FullName + "." +
// Name for nested types, else Namespace + "." + Name, else Name.
std::string MetadataTypeDefinition::FullName() const
{
    if (declaringTypeDefinition_ != nullptr)
        return declaringTypeDefinition_->FullName() + "." + Name();
    if (!Namespace().empty())
        return Namespace() + "." + Name();
    return Name();
}

std::string MetadataTypeDefinition::Namespace() const
{
    return fullTypeName_.GetTopLevelTypeName().Namespace();
}

// --- IType ---

TypeKind MetadataTypeDefinition::Kind() const
{
    return kind_;
}

std::string MetadataTypeDefinition::Name() const
{
    return fullTypeName_.Name();
}

std::string MetadataTypeDefinition::ReflectionName() const
{
    return fullTypeName_.ReflectionName();
}

int MetadataTypeDefinition::TypeParameterCount() const
{
    return static_cast<int>(typeParameters_.size());
}

// The C# `public bool? IsReferenceType`.
std::optional<bool> MetadataTypeDefinition::IsReferenceType() const
{
    switch (kind_)
    {
        case TypeKind::Struct:
        case TypeKind::Enum:
        case TypeKind::Void:
            return std::optional<bool>(false);
        default:
            return std::optional<bool>(true);
    }
}

bool MetadataTypeDefinition::IsByRefLike() const
{
    return isByRefLike_;
}

ITypePtr MetadataTypeDefinition::ChangeNullability(
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability)
{
    // The C# `if (nullability == Nullability.Oblivious || IsReferenceType ==
    // false) return this; else return new NullabilityAnnotatedType(this,
    // nullability);`
    if (nullability == ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious
        || IsReferenceType() == std::optional<bool>(false))
    {
        return shared_from_this();
    }
    return std::make_shared<NullabilityAnnotatedType>(shared_from_this(),
                                                      nullability);
}

const ITypeDefinition* MetadataTypeDefinition::GetDefinition() const
{
    return this;
}

ITypePtr MetadataTypeDefinition::AcceptVisitor(TypeVisitor& visitor)
{
    // The C# `visitor.VisitTypeDefinition(this);`
    return visitor.VisitTypeDefinition(*this);
}

std::vector<const ITypeParameter*> MetadataTypeDefinition::TypeParameters()
    const
{
    std::vector<const ITypeParameter*> result;
    result.reserve(typeParameters_.size());
    for (const std::shared_ptr<const ITypeParameter>& parameter :
         typeParameters_)
        result.push_back(parameter.get());
    return result;
}

// The C# `GetNestedTypes(filter, options)` -- the
// `(IgnoreInheritedMembers | ReturnMemberDefinitions)` arm is the
// NestedTypes-only short-circuit (the C# GetFiltered(this.NestedTypes,
// filter): the null filter passes everything).
std::vector<ITypePtr> MetadataTypeDefinition::GetNestedTypes(
    std::function<bool(const ITypeDefinition*)> filter,
    GetMemberOptions options) const
{
    const GetMemberOptions opt = GetMemberOptions::IgnoreInheritedMembers
        | GetMemberOptions::ReturnMemberDefinitions;
    if ((options & opt) == opt)
    {
        std::vector<ITypePtr> result;
        for (const ITypeDefinition* nested : NestedTypes())
        {
            if (!filter || filter(nested))
                result.push_back(SnapshotType(nested));
        }
        return result;
    }
    throw std::logic_error(
        "MetadataTypeDefinition::GetNestedTypes: GetMembersHelper is not yet "
        "routed (gated on the member entity family)");
}

std::vector<ITypePtr> MetadataTypeDefinition::GetNestedTypes(
    const std::vector<ITypePtr>& typeArguments,
    std::function<bool(const ITypeDefinition*)> filter,
    GetMemberOptions options) const
{
    (void)typeArguments;
    (void)filter;
    (void)options;
    throw std::logic_error(
        "MetadataTypeDefinition::GetNestedTypes(typeArguments): "
        "GetMembersHelper is not yet routed (gated on the member entity "
        "family)");
}

// The Void early-exit arms (the C# `if (Kind == TypeKind.Void) return
// EmptyList<...>.Instance;`) run before every routed arm; the routed arms
// need the member family (convention (e)).
std::vector<const IMethod*> MetadataTypeDefinition::GetConstructors(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    (void)filter;
    (void)options;
    if (kind_ == TypeKind::Void)
        return {};
    throw std::logic_error(
        "MetadataTypeDefinition::GetConstructors: the MetadataMethod entity "
        "family is not yet ported");
}

std::vector<const IMethod*> MetadataTypeDefinition::GetMethods(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    // The C# `if (Kind == TypeKind.Void) return EmptyList<IMethod>.Instance;`.
    if (kind_ == TypeKind::Void)
        return {};
    // The C# `if ((options & GetMemberOptions.IgnoreInheritedMembers) ==
    // GetMemberOptions.IgnoreInheritedMembers) return GetFiltered(this.Methods,
    // ExtensionMethods.And(m => !m.IsConstructor, filter));` -- a BIT TEST
    // (the GetFields precedent).
    if ((options
         & ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::
             IgnoreInheritedMembers)
        == ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::
            IgnoreInheritedMembers)
    {
        std::vector<const IMethod*> result;
        for (const IMethod* method : Methods())
        {
            if (!method->IsConstructor() && (!filter || filter(method)))
                result.push_back(method);
        }
        return result;
    }
    // The C# `return GetMembersHelper.GetMethods(this, filter, options);`.
    // The helper's owning results (the fresh `SpecializedMethod` instances a
    // parameterized base produces) are kept alive in the methodKeepAlives_
    // registry (the port's GC stand-in); the unspecialized definitions in
    // the definitions arm are module-owned.
    std::vector<std::shared_ptr<const IMethod>> owned
        = GetMembersHelper::GetMethods(this, filter, options);
    std::vector<const IMethod*> result;
    result.reserve(owned.size());
    for (std::shared_ptr<const IMethod>& m : owned)
    {
        result.push_back(m.get());
        methodKeepAlives_.push_back(std::move(m));
    }
    return result;
}

std::vector<const IMethod*> MetadataTypeDefinition::GetMethods(
    const std::vector<ITypePtr>& typeArguments,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    // The C# `if (Kind == TypeKind.Void) return EmptyList<IMethod>.Instance;
    // return GetMembersHelper.GetMethods(this, typeArguments, filter,
    // options);`.
    if (kind_ == TypeKind::Void)
        return {};
    std::vector<std::shared_ptr<const IMethod>> owned
        = GetMembersHelper::GetMethods(this, &typeArguments, filter, options);
    std::vector<const IMethod*> result;
    result.reserve(owned.size());
    for (std::shared_ptr<const IMethod>& m : owned)
    {
        result.push_back(m.get());
        methodKeepAlives_.push_back(std::move(m));
    }
    return result;
}

std::vector<const IProperty*> MetadataTypeDefinition::GetProperties(
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options) const
{
    (void)filter;
    (void)options;
    if (kind_ == TypeKind::Void)
        return {};
    throw std::logic_error(
        "MetadataTypeDefinition::GetProperties: the MetadataProperty entity "
        "family is not yet ported");
}

std::vector<const IField*> MetadataTypeDefinition::GetFields(
    std::function<bool(const IField*)> filter,
    GetMemberOptions options) const
{
    // The C# `if (Kind == TypeKind.Void) return EmptyList<IField>.Instance;`.
    if (kind_ == TypeKind::Void)
        return {};
    // The C# `if ((options & GetMemberOptions.IgnoreInheritedMembers) ==
    // GetMemberOptions.IgnoreInheritedMembers) return GetFiltered(this.Fields,
    // filter);` -- a BIT TEST (not the NestedTypes equality form), so
    // `IgnoreInheritedMembers | ReturnMemberDefinitions` takes this arm too.
    // `GetFiltered` is `filter == null ? input : ApplyFilter(input, filter)`.
    if ((options
         & ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::
             IgnoreInheritedMembers)
        == ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::
            IgnoreInheritedMembers)
    {
        std::vector<const IField*> result;
        for (const IField* field : Fields())
        {
            if (!filter || filter(field))
                result.push_back(field);
        }
        return result;
    }
    throw std::logic_error(
        "MetadataTypeDefinition::GetFields: GetMembersHelper is not yet "
        "routed (gated on the member entity family)");
}

std::vector<const IEvent*> MetadataTypeDefinition::GetEvents(
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options) const
{
    (void)filter;
    (void)options;
    if (kind_ == TypeKind::Void)
        return {};
    throw std::logic_error(
        "MetadataTypeDefinition::GetEvents: the MetadataEvent entity family "
        "is not yet ported");
}

std::vector<const IMember*> MetadataTypeDefinition::GetMembers(
    std::function<bool(const IMember*)> filter,
    GetMemberOptions options) const
{
    (void)filter;
    (void)options;
    if (kind_ == TypeKind::Void)
        return {};
    throw std::logic_error(
        "MetadataTypeDefinition::GetMembers: the member entity family is "
        "not yet ported");
}

std::vector<const IMethod*> MetadataTypeDefinition::GetAccessors(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    (void)filter;
    (void)options;
    if (kind_ == TypeKind::Void)
        return {};
    throw std::logic_error(
        "MetadataTypeDefinition::GetAccessors: the member entity family is "
        "not yet ported");
}

// The C# `IEnumerable<IType> DirectBaseTypes` (MetadataTypeDefinition.cs
// lines 340-380): the LazyInit-cached Extends + InterfaceImpl resolution
// through `module.ResolveType`.
std::vector<ITypePtr> MetadataTypeDefinition::DirectBaseTypes() const
{
    // The C# `LazyInit.VolatileRead(ref this.directBaseTypes)` -- the
    // unconditional cache (NO Uncached bypass, unlike the member lists).
    if (directBaseTypes_)
        return *directBaseTypes_;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    // The C# `var context = new GenericContext(TypeParameters);`.
    GenericContext context(TypeParameters());
    std::vector<Metadata::MetadataFile::InterfaceImplementationInfo>
        interfaceImplCollection = metadata->GetInterfaceImplementations(
            handle_);
    std::vector<ITypePtr> baseTypes;
    baseTypes.reserve(1 + interfaceImplCollection.size());
    ITypePtr baseType;
    // The C# `try { ... } catch (BadImageFormatException) { baseType =
    // SpecialType.UnknownType; }` -- the Extends read plus its resolution;
    // the port's BadImageFormatException family is the raw-surface
    // `std::invalid_argument` / `std::out_of_range` pair (the iteration-63
    // convention), caught here exactly where the C# catches its exception.
    try
    {
        // The C# `EntityHandle baseTypeHandle = td.BaseType;` -- the port's
        // read (0 for the nil column; never throws).
        std::uint32_t baseTypeHandle = metadata->GetBaseTypeToken(handle_);
        if (baseTypeHandle != 0)
        {
            // The C# `module.ResolveType(baseTypeHandle, context,
            // metadata.GetCustomAttributes(this.handle),
            // Nullability.Oblivious)` -- NOTE the DERIVED type's own
            // attribute rows: the nullability bytes annotating the base-type
            // position are encoded on the referencing type.
            baseType = module_.ResolveType(
                baseTypeHandle, context,
                std::optional<std::vector<std::uint32_t>>(
                    metadata->GetCustomAttributeTokens(handle_)),
                ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious);
        }
    }
    catch (const std::invalid_argument&)
    {
        baseType = UnknownType();
    }
    catch (const std::out_of_range&)
    {
        baseType = UnknownType();
    }
    if (baseType != nullptr)
    {
        baseTypes.push_back(std::move(baseType));
    }
    else if (kind_ == TypeKind::Interface)
    {
        // The C# `td.BaseType.IsNil is always true for interfaces, but the
        // type system expects every interface to derive from System.Object
        // as well` -- `Compilation.FindType(KnownTypeCode.Object)` aliased
        // non-owning (the compilation's KnownTypeCache owns it).
        baseTypes.push_back(SnapshotType(
            &module_.Compilation().FindType(
                ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object)));
    }
    for (const auto& h : interfaceImplCollection)
    {
        // The C# `module.ResolveType(iface.Interface, context,
        // iface.GetCustomAttributes(), Nullability.Oblivious)` -- each
        // InterfaceImpl row's OWN attribute rows (the nullability bytes over
        // the interface positions).
        baseTypes.push_back(module_.ResolveType(
            h.InterfaceToken, context,
            std::optional<std::vector<std::uint32_t>>(
                metadata->GetCustomAttributeTokens(h.Token)),
            ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious));
    }
    // The C# `LazyInit.GetOrSet(ref this.directBaseTypes, baseTypes)`.
    directBaseTypes_ = std::move(baseTypes);
    return *directBaseTypes_;
}

// The C# `public override bool Equals(object obj)`.
bool MetadataTypeDefinition::StructuralEquals(const IType& other) const
{
    auto definition = dynamic_cast<const MetadataTypeDefinition*>(&other);
    return definition != nullptr && handle_ == definition->handle_
        && module_.MetadataFile() == definition->module_.MetadataFile();
}

// --- ITypeDefinitionOrUnknown ---

const FullTypeName& MetadataTypeDefinition::FullTypeName() const
{
    return fullTypeName_;
}

// --- IEntity ---

std::uint32_t MetadataTypeDefinition::MetadataToken() const
{
    return handle_;
}

const ITypeDefinition* MetadataTypeDefinition::DeclaringTypeDefinition() const
{
    return declaringTypeDefinition_;
}

ITypePtr MetadataTypeDefinition::DeclaringType() const
{
    if (declaringTypeDefinition_ == nullptr)
        return ITypePtr();
    return SnapshotType(declaringTypeDefinition_);
}

const IModule* MetadataTypeDefinition::ParentModule() const
{
    return &module_;
}

std::vector<const IAttribute*> MetadataTypeDefinition::GetAttributes() const
{
    throw std::logic_error(
        "MetadataTypeDefinition::GetAttributes: AttributeListBuilder is not "
        "yet ported (gated on the custom-attribute value decoder)");
}

bool MetadataTypeDefinition::HasAttribute(KnownAttribute attribute) const
{
    (void)attribute;
    throw std::logic_error(
        "MetadataTypeDefinition::HasAttribute: AttributeListBuilder is not "
        "yet ported (gated on the custom-attribute value decoder)");
}

const IAttribute* MetadataTypeDefinition::GetAttribute(
    KnownAttribute attribute) const
{
    (void)attribute;
    throw std::logic_error(
        "MetadataTypeDefinition::GetAttribute: AttributeListBuilder is not "
        "yet ported (gated on the custom-attribute value decoder)");
}

// The C# `public Accessibility Accessibility` -- the VisibilityMask switch
// over the raw TypeAttributes column (II.23.1.15: NotPublic=0, Public=1,
// NestedPublic=2, NestedPrivate=3, NestedFamily=4, NestedAssembly=5,
// NestedFamANDAssem=6, NestedFamORAssem=7).
Accessibility MetadataTypeDefinition::Accessibility() const
{
    switch (attributes_ & 0x7u)
    {
        case 0:  // NotPublic
        case 5:  // NestedAssembly
            return Accessibility::Internal;
        case 1:  // Public
        case 2:  // NestedPublic
            return Accessibility::Public;
        case 3:  // NestedPrivate
            return Accessibility::Private;
        case 4:  // NestedFamily
            return Accessibility::Protected;
        case 6:  // NestedFamANDAssem
            return Accessibility::ProtectedAndInternal;
        case 7:  // NestedFamORAssem
            return Accessibility::ProtectedOrInternal;
        default:
            return Accessibility::None;  // unreachable (the mask is 3 bits)
    }
}

// The C# `IsStatic => (attributes & (Abstract | Sealed)) == (Abstract |
// Sealed)` -- Abstract=0x80, Sealed=0x100.
bool MetadataTypeDefinition::IsStatic() const
{
    return (attributes_ & (0x80u | 0x100u)) == (0x80u | 0x100u);
}

bool MetadataTypeDefinition::IsAbstract() const
{
    return (attributes_ & 0x80u) != 0;
}

bool MetadataTypeDefinition::IsSealed() const
{
    return (attributes_ & 0x100u) != 0;
}

// --- ITypeDefinition ---

// The C# lazily-cached `NestedTypes`: the C# LazyInit store is skipped under
// the Uncached option (every read rebuilds the list).
std::vector<const ITypeDefinition*> MetadataTypeDefinition::NestedTypes()
    const
{
    bool uncached =
        (module_.TypeSystemOptions()
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Uncached)
            != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
    if (!uncached && nestedTypesLoaded_)
        return nestedTypes_;
    std::vector<const ITypeDefinition*> list;
    for (std::uint32_t token :
         module_.MetadataFile()->GetNestedTypes(handle_))
    {
        const ITypeDefinition* definition = module_.GetDefinition(token);
        if (definition != nullptr)
            list.push_back(definition);
    }
    if (uncached)
        return list;
    nestedTypes_ = std::move(list);
    nestedTypesLoaded_ = true;
    return nestedTypes_;
}

// DEFERRED (convention (e)): the member family.
std::vector<const IMember*> MetadataTypeDefinition::Members() const
{
    throw std::logic_error(
        "MetadataTypeDefinition::Members: the member entity family is not "
        "yet ported");
}

std::vector<const IField*> MetadataTypeDefinition::Fields() const
{
    // The C# `var fields = LazyInit.VolatileRead(ref this.fields); if (fields
    // != null) return fields;` (the Uncached option bypasses the cache -- the
    // fresh list per read).
    if (fields_)
        return *fields_;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    // The C# `var fieldCollection = metadata.GetTypeDefinition(handle)
    // .GetFields();` -- the TypeDef's Field-list range in row order.
    std::vector<Metadata::FieldInfo> fieldCollection =
        metadata->GetFields(handle_);
    std::vector<const IField*> fieldList;
    fieldList.reserve(fieldCollection.size());
    for (const auto& h : fieldCollection)
    {
        // The C# `var @field = metadata.GetFieldDefinition(h); var attr =
        // @field.Attributes; if (module.IsVisible(attr)) fieldList.Add(
        // module.GetDefinition(h));` -- the per-row visibility filter, then
        // the module's per-row entity cache.
        if (module_.IsFieldVisible(
                metadata->GetFieldAttributes(h.Token)))
        {
            fieldList.push_back(module_.GetDefinitionField(h.Token));
        }
    }
    // The C# `if ((module.TypeSystemOptions & TypeSystemOptions.Uncached)
    // != 0) return fieldList;`.
    if ((module_.TypeSystemOptions()
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Uncached)
        != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        return fieldList;
    fields_ = std::move(fieldList);
    return *fields_;
}

std::vector<const IMethod*> MetadataTypeDefinition::Methods() const
{
    // The C# `LazyInit.VolatileRead(ref this.methods)`.
    if (methods_.has_value())
        return *methods_;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    std::vector<Metadata::MethodInfo> methodCollection
        = metadata->GetMethods(handle_);
    std::vector<const IMethod*> methodList;
    methodList.reserve(methodCollection.size());
    const Metadata::MethodSemanticsLookup& methodSemantics
        = metadata->GetMethodSemanticsLookup();
    bool hasDefaultCtor = false;
    for (const Metadata::MethodInfo& methodInfo : methodCollection)
    {
        // The C# `if (methodSemantics.GetSemantics(h).Item2 == 0 &&
        // module.IsVisible(md.Attributes))` -- the accessor grouping drops
        // every accessor row (the lookup answers None for a non-accessor).
        const std::uint32_t attributes
            = metadata->GetMethodAttributes(methodInfo.Token);
        if (methodSemantics.GetSemantics(methodInfo.Token).Semantics
                == ::ILSpy::Decompiler::TypeSystem::
                    MethodSemanticsAttributes::None
            && module_.IsMethodVisible(attributes))
        {
            const IMethod* method = module_.GetDefinitionMethod(methodInfo.Token);
            if (method->SymbolKind()
                    == ::ILSpy::Decompiler::TypeSystem::SymbolKind::
                        Constructor
                && !method->IsStatic() && method->Parameters().empty())
            {
                hasDefaultCtor = true;
            }
            methodList.push_back(method);
        }
    }
    // The C# `if (!hasDefaultCtor && (this.Kind == TypeKind.Struct ||
    // this.Kind == TypeKind.Enum)) methodsList.Add(FakeMethod.
    // CreateDummyConstructor(Compilation, this, Accessibility.Public));` --
    // the dummy's keep-alive slot is the registry (the GC stand-in).
    if (!hasDefaultCtor
        && (kind_ == TypeKind::Struct || kind_ == TypeKind::Enum))
    {
        std::shared_ptr<IMethod> dummy = FakeMethod::CreateDummyConstructor(
            Compilation(), SnapshotType(this),
            ::ILSpy::Decompiler::TypeSystem::Accessibility::Public);
        methodKeepAlives_.push_back(std::move(dummy));
        methodList.push_back(methodKeepAlives_.back().get());
    }
    // The C# `if ((module.TypeSystemOptions & TypeSystemOptions.Uncached)
    // != 0) return methodsList; return LazyInit.GetOrSet(ref this.methods,
    // methodsList.ToArray());`.
    if ((module_.TypeSystemOptions()
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Uncached)
        != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        return methodList;
    methods_ = std::move(methodList);
    return *methods_;
}

std::vector<const IProperty*> MetadataTypeDefinition::Properties() const
{
    throw std::logic_error(
        "MetadataTypeDefinition::Properties: the MetadataProperty entity "
        "family is not yet ported");
}

std::vector<const IEvent*> MetadataTypeDefinition::Events() const
{
    throw std::logic_error(
        "MetadataTypeDefinition::Events: the MetadataEvent entity family is "
        "not yet ported");
}

KnownTypeCode MetadataTypeDefinition::KnownTypeCode() const
{
    return knownTypeCode_;
}

ITypePtr MetadataTypeDefinition::EnumUnderlyingType() const
{
    return enumUnderlyingType_;
}

bool MetadataTypeDefinition::IsReadOnly() const
{
    return isReadOnly_;
}

std::string MetadataTypeDefinition::MetadataName() const
{
    return metadataName_;
}

bool MetadataTypeDefinition::HasExtensions() const
{
    return hasExtensions_;
}

// The C# null arms (`!HasExtensions` or the ExtensionMembers option off)
// return null without touching the member family; the construction is the
// deferral (convention (e)).
const ::ILSpy::Decompiler::TypeSystem::ExtensionInfo*
MetadataTypeDefinition::ExtensionInfo() const
{
    if (!hasExtensions_)
        return nullptr;
    if ((module_.TypeSystemOptions()
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
             ExtensionMembers)
            == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        return nullptr;
    throw std::logic_error(
        "MetadataTypeDefinition::ExtensionInfo: the ExtensionInfo "
        "construction is not yet ported (gated on the MetadataMethod family)");
}

// The deferred [NullableContext] decode (convention (d)): Oblivious.
::ILSpy::Decompiler::TypeSystem::Nullability
MetadataTypeDefinition::NullableContext() const
{
    return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
}

// The C# `public bool IsRecord` -- the ThreeState-cached raw method-name
// scan (convention (e): the scan reads the RAW method list through
// `MetadataFile::GetMethods`, NOT the accessor-dropped Methods()
// enumeration -- a record class's `get_EqualityContract` is an accessor row
// the enumeration drops, so the raw scan is the only shape that classifies
// records correctly).
bool MetadataTypeDefinition::IsRecord() const
{
    if (isRecord_ != 0) {
        return isRecord_ == 2;  // ThreeState: 1 = False, 2 = True
    }
    isRecord_ = ComputeIsRecord() ? 2 : 1;
    return isRecord_ == 2;
}

// The C# `private bool ComputeIsRecord()` (the file-local helper): the
// eight-method-signature test over the raw method names -- a record
// requires get_EqualityContract/PrintMembers/GetHashCode/Equals/
// op_Equality/op_Inequality/<Clone>$ (the struct arms relax: clone and
// getEqualityContract start true, but toString is required), and ANY method
// literally named `Clone` disqualifies (CS8859: members named 'Clone' are
// disallowed in records).
bool MetadataTypeDefinition::ComputeIsRecord() const
{
    if (kind_ != TypeKind::Class && kind_ != TypeKind::Struct)
        return false;
    bool isStruct = kind_ == TypeKind::Struct;

    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    bool getEqualityContract = isStruct;
    bool toString = false;
    bool printMembers = false;
    bool getHashCode = false;
    bool equals = false;
    bool opEquality = false;
    bool opInequality = false;
    bool clone = isStruct;
    for (const Metadata::MethodInfo& methodInfo : metadata->GetMethods(handle_))
    {
        // The C# `metadata.StringComparer.Equals(method.Name, "...")` --
        // ordinal string equality.
        const std::string& name = methodInfo.Name;
        if (name == "Clone") {
            // error CS8859: Members named 'Clone' are disallowed in records.
            return false;
        }
        getEqualityContract |= name == "get_EqualityContract";
        toString |= name == "ToString";
        printMembers |= name == "PrintMembers";
        getHashCode |= name == "GetHashCode";
        equals |= name == "Equals";
        opEquality |= name == "op_Equality";
        opInequality |= name == "op_Inequality";
        clone |= name == "<Clone>$";
    }
    // Relaxed check for toString: record classes may have their ToString
    // implementation only in the base class, so the type hierarchy is not
    // yet known here; in record structs we require a ToString implementation
    // because the PrintMembers method needs to be called.
    if (isStruct && !toString) {
        return false;
    }
    return getEqualityContract & printMembers & getHashCode & equals
        & opEquality & opInequality & clone;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
