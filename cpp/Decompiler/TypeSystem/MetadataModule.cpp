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

#include "Decompiler/TypeSystem/MetadataModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"
#include "Decompiler/TypeSystem/Implementation/CustomAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataField.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultTypeParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataNamespace.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/BusyManager.hpp"

#include <any>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The Assembly table's (0x20) Name column in winmd's layout (schema.h
// reader::Assembly): 0 = HashAlgId, 1 = the merged Version column, 2 = Flags,
// 3 = PublicKey, 4 = Name, 5 = Culture -- the same layout MetadataExtensions.cpp
// spells the full-assembly-name builder over (the iteration-63 constants).
constexpr std::uint32_t kAssemblyNameColumn = 4;
// The Module table's (0x00) Name column: 0 = Generation, 1 = Name, 2 = Mvid,
// 3 = EncId, 4 = EncBaseId (II.22.30; the GetModuleDefinition comment in
// MetadataFile.cpp).
constexpr std::uint32_t kModuleNameColumn = 1;

} // namespace

// The ctor: the assembly-identity computation (convention (b) -- the raw-surface
// reads that keep the corrupt-row throws faithful) plus the root namespace over
// the iteration-62 namespace tree. The C#'s TypeProvider / NullableContext /
// minAccessibilityForNRT computations and the entity-cache array allocations
// are deferred (convention (g)).
//
// The third parameter type is GLOBALLY QUALIFIED here (unlike the header
// declaration, which precedes the `TypeSystemOptions()` accessor so its
// unqualified name resolves to the enum): in an out-of-line definition the
// `MetadataModule::` scope INCLUDES the accessor, which hides the
// namespace-scope `TypeSystemOptions` enum for the parameter lookup (the D372
// cross-scope name-hiding crux, in its out-of-line-definition form -- the
// C2061 "syntax error: identifier 'TypeSystemOptions'" otherwise).
MetadataModule::MetadataModule(const ICompilation& compilation,
                               const Metadata::MetadataFile* metadataFile,
                               ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options)
    : compilation_(compilation),
      metadataFile_(metadataFile),
      options_(options)
{
    // The C# `if (metadata.IsAssembly)`: an Assembly-table row exists (an
    // assembly manifest carries exactly one; a netmodule none).
    if (metadataFile_->CorTableRowCount(Metadata::CorTableIndex::Assembly) != 0)
    {
        try
        {
            // `this.AssemblyName = metadata.GetString(asmdef.Name)` -- the Assembly
            // row's Name column as a #Strings offset. The raw-surface read throws
            // for a corrupt index (the winmd seek/terminator throws the port maps
            // to the C# BadImageFormatException the catch below takes).
            assemblyName_ = metadataFile_->CorString(metadataFile_->CorTableColumnValue(
                Metadata::CorTableIndex::Assembly, 0, kAssemblyNameColumn));
            // `this.AssemblyVersion = asmdef.Version` -- the merged 8-byte version
            // column (four UInt16 components) as the four-component Version value.
            Metadata::MetadataFile::CorTableVersion version =
                metadataFile_->CorTableVersionValue(Metadata::CorTableIndex::Assembly, 0);
            assemblyVersion_ = Version(version.MajorVersion, version.MinorVersion,
                                       version.BuildNumber, version.RevisionNumber);
            // `this.FullAssemblyName = metadata.GetFullAssemblyName()` -- the
            // reader extension (the iteration-63 port), which propagates the same
            // raw-surface throws of the corrupt row.
            fullAssemblyName_ = Metadata::GetFullAssemblyName(*metadataFile_);
        }
        catch (const std::invalid_argument&)
        {
            // The C# `catch (BadImageFormatException)`: both rendered names become
            // the error marker; AssemblyVersion keeps its null stand-in (the
            // default-constructed Version{}, convention (c)).
            assemblyName_ = "<ERR: invalid assembly name>";
            fullAssemblyName_ = "<ERR: invalid assembly name>";
        }
        catch (const std::out_of_range&)
        {
            assemblyName_ = "<ERR: invalid assembly name>";
            fullAssemblyName_ = "<ERR: invalid assembly name>";
        }
    }
    else
    {
        try
        {
            // `this.AssemblyName = metadata.GetString(metadata.GetModuleDefinition()
            // .Name)` -- the Module row's Name column. An empty Module table (an
            // unparseable file -- never a constructible module in valid usage)
            // degrades to the empty name (the nil-handle GetString shape).
            assemblyName_ =
                metadataFile_->CorTableRowCount(Metadata::CorTableIndex::Module) != 0
                ? metadataFile_->CorString(metadataFile_->CorTableColumnValue(
                      Metadata::CorTableIndex::Module, 0, kModuleNameColumn))
                : std::string();
        }
        catch (const std::invalid_argument&)
        {
            assemblyName_ = "<ERR: invalid assembly name>";
        }
        catch (const std::out_of_range&)
        {
            assemblyName_ = "<ERR: invalid assembly name>";
        }
        // The C# netmodule arm: `this.FullAssemblyName = this.AssemblyName` (the
        // MODULE name -- NOT GetFullAssemblyName's empty string).
        fullAssemblyName_ = assemblyName_;
    }
    // The C# `this.rootNamespace = new MetadataNamespace(this, null,
    // string.Empty, metadata.GetNamespaceDefinitionRoot())` -- the root namespace
    // over the namespace tree's root node (the port's tree hangs off the
    // MetadataFile pimpl and outlives the module under the caller-owns-the-file
    // contract).
    rootNamespace_ = std::make_unique<Implementation::MetadataNamespace>(
        *this, nullptr, std::string(), metadataFile_->GetNamespaceDefinitionRoot());

    // The C# `if (!options.HasFlag(TypeSystemOptions.Uncached)) { this.typeDefs
    // = new MetadataTypeDefinition[metadata.TypeDefinitions.Count + 1]; ... }` --
    // the type-definition entity cache (index = the 1-based TypeDef row
    // number, slot 0 unused). The sibling entity arrays defer with their
    // entity classes (the MetadataField/... family); `referencedAssemblies`
    // (the ResolveModule cache, this slice) allocates here too.
    if ((options_
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Uncached)
        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
    {
        typeDefs_.resize(metadataFile_->TypeDefCount() + 1);
        // The C# `fieldDefs = new MetadataField[metadata.FieldDefinitions.Count
        // + 1]` (the member-entity caches, this slice).
        fieldDefs_.resize(metadataFile_->FieldCount() + 1);
        // The C# `methodDefs = new MetadataMethod[metadata.MethodDefinitions
        // .Count + 1]` (the method entity cache, the method slice).
        methodDefs_.resize(metadataFile_->MethodCount() + 1);
        // The C# `propertyDefs = new MetadataProperty[metadata
        // .PropertyDefinitions.Count + 1]` (the property entity cache).
        propertyDefs_.resize(
            metadataFile_->CorTableRowCount(
                Metadata::CorTableIndex::Property) + 1);
        // The C# `eventDefs = new MetadataEvent[metadata.EventDefinitions
        // .Count + 1]` (the event entity cache).
        eventDefs_.resize(
            metadataFile_->CorTableRowCount(
                Metadata::CorTableIndex::Event) + 1);
        referencedAssemblies_.resize(metadataFile_->CorTableRowCount(
            Metadata::CorTableIndex::AssemblyRef) + 1);
    }

    // The C# `this.TypeProvider = new TypeProvider(this)` -- the module-owned
    // signature provider (this slice). Constructed in the ctor BODY (after the
    // `compilation_` member is initialized: the provider's ctor reads
    // `module.Compilation()`).
    typeProvider_ = std::make_unique<::ILSpy::Decompiler::TypeSystem::TypeProvider>(*this);

    // The C# `readonly IType[] knownAttributeTypes = new IType[KnownAttributes.
    // Count]` / `readonly IAttribute[] knownAttributes = new IAttribute[
    // KnownAttributes.Count]` (the field initializers -- allocated regardless
    // of the Uncached option, the attribute-slice caches).
    knownAttributeTypes_.resize(KnownAttributeCount);
    knownAttributes_.resize(KnownAttributeCount);

    // The C# `var customAttrs = metadata.GetModuleDefinition().
    // GetCustomAttributes(); this.NullableContext = customAttrs.
    // GetNullableContext(metadata) ?? Nullability.Oblivious;
    // this.minAccessibilityForNRT = FindMinimumAccessibilityForNRT(metadata,
    // customAttrs);` -- the MODULE row's NRT context, computed EAGERLY (every
    // entity context chains onto it; the lazy entities always construct after
    // the module).
    constexpr std::uint32_t kModuleDefinitionToken = 0x00000001;
    nullableContext_ = Metadata::GetNullableContext(
                           *metadataFile_, kModuleDefinitionToken)
                           .value_or(
                               ::ILSpy::Decompiler::TypeSystem::Nullability::
                                   Oblivious);
    minAccessibilityForNRT_ = FindMinimumAccessibilityForNRT();
}

// The C# `static Accessibility FindMinimumAccessibilityForNRT(MetadataReader
// metadata, CustomAttributeHandleCollection customAttributes)`
// (MetadataModule.cs line 997): the module's [NullablePublicOnly] row.
::ILSpy::Decompiler::TypeSystem::Accessibility
MetadataModule::FindMinimumAccessibilityForNRT() const
{
    constexpr std::uint32_t kModuleDefinitionToken = 0x00000001;
    for (std::uint32_t attributeToken :
         metadataFile_->GetCustomAttributeTokens(kModuleDefinitionToken))
    {
        if (!Metadata::IsKnownAttribute(*metadataFile_, attributeToken,
                                        KnownAttribute::NullablePublicOnly))
            continue;
        std::optional<Metadata::CustomAttributeRowInfo> row =
            metadataFile_->GetCustomAttribute(attributeToken);
        if (!row)
            continue;
        try
        {
            // The C# `customAttribute.DecodeValue(Metadata.MetadataExtensions.
            // MinimalAttributeTypeProvider)` with the catch-continue arms.
            Metadata::CustomAttributeDecoder decoder(
                *metadataFile_, Metadata::MinimalAttributeTypeProvider());
            Metadata::CustomAttributeValue value = decoder.DecodeValue(
                row->ConstructorToken,
                row->ValueBlob ? row->ValueBlob->data() : nullptr,
                row->ValueBlob ? row->ValueBlob->size() : 0);
            if (value.FixedArguments.size() == 1)
            {
                // The C# `value.FixedArguments[0].Value is bool
                // includesInternals` -- the bool box.
                std::any boxed = value.FixedArguments[0].Value();
                if (auto includesInternals = std::any_cast<bool>(&boxed))
                {
                    return *includesInternals
                               ? ::ILSpy::Decompiler::TypeSystem::
                                     Accessibility::ProtectedAndInternal
                               : ::ILSpy::Decompiler::TypeSystem::
                                     Accessibility::Protected;
                }
            }
        }
        catch (const Metadata::EnumUnderlyingTypeResolveException&)
        {
            continue;
        }
        catch (const std::invalid_argument&)
        {
            continue;
        }
    }
    return ::ILSpy::Decompiler::TypeSystem::Accessibility::None;
}

::ILSpy::Decompiler::TypeSystem::Nullability
MetadataModule::NullableContext() const
{
    return nullableContext_;
}

// The C# `internal bool ShouldDecodeNullableAttributes(IEntity entity)`
// (MetadataModule.cs line 1027).
bool MetadataModule::ShouldDecodeNullableAttributes(
    const IEntity* entity) const
{
    if ((options_
         & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
             NullabilityAnnotations)
        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        return false;
    if (minAccessibilityForNRT_
            == ::ILSpy::Decompiler::TypeSystem::Accessibility::None
        || entity == nullptr)
        return true;
    return LessThanOrEqual(minAccessibilityForNRT_,
                           EffectiveAccessibility(*entity));
}

// The C# `internal TypeSystemOptions OptionsForEntity(IEntity entity)`
// (MetadataModule.cs line 1036): the NullabilityAnnotations bit stripped when
// the entity is below the [NullablePublicOnly] accessibility threshold.
::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
MetadataModule::OptionsForEntity(const IEntity* entity) const
{
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions opt = options_;
    if ((opt & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                   NullabilityAnnotations)
        != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
    {
        if (!ShouldDecodeNullableAttributes(entity))
        {
            opt = opt
                & ~::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                       NullabilityAnnotations;
        }
    }
    return opt;
}

// The C# `internal readonly TypeProvider TypeProvider` field's accessor
// (the header's self-named-accessor note).
const ::ILSpy::Decompiler::TypeSystem::TypeProvider&
MetadataModule::TypeProvider() const {
    return *typeProvider_;
}

// Out-of-line: the `rootNamespace_` unique_ptr deleter needs
// `Implementation::MetadataNamespace` complete (the header convention).
MetadataModule::~MetadataModule() = default;

// The C# `public TypeSystemOptions TypeSystemOptions => options`.
::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
MetadataModule::TypeSystemOptions() const
{
    return options_;
}

// The C# `public ITypeDefinition GetDefinition(TypeDefinitionHandle handle)`
// (convention (e)): the token's low 24 bits are the 1-based TypeDef row number
// (`MetadataTokens.GetRowNumber`); the callers pass TypeDef tokens (`0x02......`).
// Nil -> null; the cached arm range-checks and lazily fills the `typeDefs`
// slot with a real `MetadataTypeDefinition`; the uncached arm constructs
// without any range check (the row read inside the ctor throws instead).
const ITypeDefinition* MetadataModule::GetDefinition(
    std::uint32_t typeDefinitionToken) const
{
    std::uint32_t row = typeDefinitionToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    // The C# `if (typeDefs == null) return new MetadataTypeDefinition(this,
    // handle);` -- the UNCACHED arm constructs without any range check (the
    // row read inside the ctor throws for a bogus row); the keep-alive
    // registry owns the instance so the returned pointer cannot dangle
    // (the C# GC root).
    if (typeDefs_.empty())
    {
        auto definition =
            std::make_shared<Implementation::MetadataTypeDefinition>(
                *this, typeDefinitionToken);
        const ITypeDefinition* result = definition.get();
        uncachedDefs_.push_back(std::move(definition));
        return result;
    }
    // The C# `int row = MetadataTokens.GetRowNumber(handle); if (row >=
    // typeDefs.Length) HandleOutOfRange(handle);` -- the 1-based row
    // against the count+1-sized array.
    if (row >= typeDefs_.size())
        HandleOutOfRange();
    // The C# `LazyInit.VolatileRead(ref typeDefs[row])` + `GetOrSet`:
    // the empty slot constructs, the filled slot returns (the
    // single-threaded LazyInit convention).
    std::shared_ptr<Implementation::MetadataTypeDefinition>& slot =
        typeDefs_[row];
    if (slot == nullptr)
        slot = std::make_shared<Implementation::MetadataTypeDefinition>(
            *this, typeDefinitionToken);
    return slot.get();
}

// The C# `public IField GetDefinition(FieldDefinitionHandle handle)`
// (MetadataModule.cs lines 236-252): the FIELD entity cache -- the first
// member-entity cache (the MetadataField family). Convention (e): the nil
// token returns null; the CACHED arm range-checks (a row past the Field table
// throws the same `Handle with invalid row number.`) and lazily fills the
// `fieldDefs` slot per 1-based row; the UNCACHED arm constructs without any
// range check (the C# shape: the row read inside the ctor throws instead), the
// keep-alive registry owning the instance. NOTE the C# has NO
// `Debug.Assert(row != 0)` on the field arm (the TypeDef arm's assert exists
// only there).
const IField* MetadataModule::GetDefinitionField(
    std::uint32_t fieldToken) const
{
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    if (fieldDefs_.empty())
    {
        auto field = std::make_shared<Implementation::MetadataField>(
            *this, fieldToken);
        const IField* result = field.get();
        uncachedFieldDefs_.push_back(std::move(field));
        return result;
    }
    if (row >= fieldDefs_.size())
        HandleOutOfRange();
    std::shared_ptr<Implementation::MetadataField>& slot = fieldDefs_[row];
    if (slot == nullptr)
        slot = std::make_shared<Implementation::MetadataField>(
            *this, fieldToken);
    return slot.get();
}

// The C# `public IMethod GetDefinition(MethodDefinitionHandle handle)`
// (MetadataModule.cs lines 252-266) -- the per-row METHOD entity cache: the
// nil token -> null; the UNCACHED arm constructs without a range check (the
// keep-alive registry owning the instance); the CACHED arm range-checks
// against the `methodDefs` slot count and fills the per-row slot (the C#
// `Debug.Assert(row != 0)` is compiled out of the release assembly).
const IMethod* MetadataModule::GetDefinitionMethod(
    std::uint32_t methodToken) const
{
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    if (methodDefs_.empty())
    {
        auto method = std::make_shared<Implementation::MetadataMethod>(
            *this, methodToken);
        const IMethod* result = method.get();
        uncachedMethodDefs_.push_back(std::move(method));
        return result;
    }
    if (row >= methodDefs_.size())
        HandleOutOfRange();
    std::shared_ptr<Implementation::MetadataMethod>& slot =
        methodDefs_[row];
    if (slot == nullptr)
        slot = std::make_shared<Implementation::MetadataMethod>(
            *this, methodToken);
    return slot.get();
}

// The C# `public IProperty GetDefinition(PropertyDefinitionHandle handle)`
// (MetadataModule.cs lines 269-283) -- the per-row property entity cache:
// the nil token -> null; the UNCACHED arm constructs without a range check
// (the keep-alive registry owning the instance); the CACHED arm
// range-checks against the `propertyDefs` slot count and fills the
// per-row slot.
const IProperty* MetadataModule::GetDefinitionProperty(
    std::uint32_t propertyToken) const
{
    std::uint32_t row = propertyToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    if (propertyDefs_.empty())
    {
        auto property = std::make_shared<Implementation::MetadataProperty>(
            *this, propertyToken);
        const IProperty* result = property.get();
        uncachedPropertyDefs_.push_back(std::move(property));
        return result;
    }
    if (row >= propertyDefs_.size())
        HandleOutOfRange();
    std::shared_ptr<Implementation::MetadataProperty>& slot =
        propertyDefs_[row];
    if (slot == nullptr)
        slot = std::make_shared<Implementation::MetadataProperty>(
            *this, propertyToken);
    return slot.get();
}

// The C# `public IEvent GetDefinition(EventDefinitionHandle handle)`
// (MetadataModule.cs lines 286-299) -- the per-row event entity cache (the
// same conventions as the property arm).
const IEvent* MetadataModule::GetDefinitionEvent(
    std::uint32_t eventToken) const
{
    std::uint32_t row = eventToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    if (eventDefs_.empty())
    {
        auto ev = std::make_shared<Implementation::MetadataEvent>(
            *this, eventToken);
        const IEvent* result = ev.get();
        uncachedEventDefs_.push_back(std::move(ev));
        return result;
    }
    if (row >= eventDefs_.size())
        HandleOutOfRange();
    std::shared_ptr<Implementation::MetadataEvent>& slot = eventDefs_[row];
    if (slot == nullptr)
        slot = std::make_shared<Implementation::MetadataEvent>(
            *this, eventToken);
    return slot.get();
}

// --- Visibility Filter (MetadataModule.cs lines 971-993) ---

// The C# `internal bool IncludeInternalMembers =>
// (options & TypeSystemOptions.OnlyPublicAPI) == 0`.
bool MetadataModule::IncludeInternalMembers() const
{
    return (options_
            & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                OnlyPublicAPI)
        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
}

// The C# `internal bool IsVisible(FieldAttributes att)`:
// `att &= FieldAttributes.FieldAccessMask; return IncludeInternalMembers
// || att == FieldAttributes.Public || att == FieldAttributes.Family ||
// att == FieldAttributes.FamORAssem;` -- the raw ECMA visibility bits (the
// port takes the raw masked uint32; the PrivateScope/zero value and the
// Assembly/FamANDAssem kinds are internal-only).
bool MetadataModule::IsFieldVisible(std::uint32_t fieldAttributes) const
{
    std::uint32_t att = fieldAttributes & 0x0007u;  // FieldAccessMask
    return IncludeInternalMembers() || att == 0x0006u   // Public
        || att == 0x0004u                                // Family
        || att == 0x0005u;                               // FamORAssem
}

// The C# `internal bool IsVisible(MethodAttributes att)` (MetadataModule.cs
// lines 985-993): the method half of the visibility filter -- the same
// MemberAccessMask (0x0007, identical bit layout for methods), the same
// IncludeInternalMembers / Public / Family / FamORAssem accept set (the
// `IsFieldVisible` shape).
bool MetadataModule::IsMethodVisible(std::uint32_t methodAttributes) const
{
    std::uint32_t att = methodAttributes & 0x0007u;  // MemberAccessMask
    return IncludeInternalMembers() || att == 0x0006u   // Public
        || att == 0x0004u                                // Family
        || att == 0x0005u;                               // FamORAssem
}

// --- Resolve Module (MetadataModule.cs lines 305-368) ---

namespace {

// The C# `IModule ResolveModuleUncached(AssemblyReferenceHandle handle)`:
// `var asmRef = new Metadata.AssemblyReference(metadata, handle); return
// Compilation.FindModuleByReference(asmRef);` -- the metadata-backed row
// wrapper (the iteration-36 interface's third implementation, this slice)
// feeding the two-pass module lookup (the TypeSystemExtensions port).
const IModule* ResolveModuleUncached(const ICompilation& compilation,
                                     const Metadata::MetadataFile& file,
                                     std::uint32_t assemblyReferenceToken)
{
    Metadata::AssemblyReference asmRef(file, assemblyReferenceToken);
    return FindModuleByReference(compilation, asmRef);
}

// The AssemblyRef table's Name column (the same merged-version layout the
// AssemblyReference class and the iteration-63 GetFullAssemblyName read).
constexpr std::uint32_t kAssemblyRefNameColumn = 3;

} // namespace

// The C# `public IModule ResolveModule(AssemblyReferenceHandle handle)`.
const IModule* MetadataModule::ResolveModule(
    std::uint32_t assemblyReferenceToken) const
{
    std::uint32_t row = assemblyReferenceToken & 0x00FFFFFFu;
    // The C# `if (handle.IsNil) return null;`.
    if (row == 0)
        return nullptr;
    // The C# `if (referencedAssemblies == null) return
    // ResolveModuleUncached(handle);` -- the UNCACHED arm: no range check
    // (the row read inside the uncached resolution throws instead; an
    // out-of-range row reaches the AssemblyReference catch fallbacks and
    // resolves to NULL through the name-scan miss).
    if (referencedAssemblies_.empty())
        return ResolveModuleUncached(Compilation(), *metadataFile_,
                                     assemblyReferenceToken);
    // The C# `if (row >= referencedAssemblies.Length)
    // HandleOutOfRange(handle);` -- the 1-based row against the count+1-sized
    // array.
    if (row >= referencedAssemblies_.size())
        HandleOutOfRange();
    // The C# `LazyInit.VolatileRead` + `GetOrSet`: the non-null slot returns,
    // the null slot resolves again (the null resolution is re-resolved on
    // every call -- storing it is a no-op on the nullptr slot).
    const IModule*& slot = referencedAssemblies_[row];
    if (slot != nullptr)
        return slot;
    slot = ResolveModuleUncached(Compilation(), *metadataFile_,
                                 assemblyReferenceToken);
    return slot;
}

// The C# `public IModule ResolveModule(ModuleReferenceHandle handle)`.
const IModule* MetadataModule::ResolveModuleReference(
    std::uint32_t moduleReferenceToken) const
{
    std::uint32_t row = moduleReferenceToken & 0x00FFFFFFu;
    // The C# `if (handle.IsNil) return null;`.
    if (row == 0)
        return nullptr;
    // The C# cannot construct a module over an invalid file (the MetadataFile
    // ctor throws); the port's degrading reader maps the state to the null
    // miss (the iteration-61 invalid-file convention).
    if (!metadataFile_->IsValid())
        return nullptr;
    // The C# `metadata.GetModuleReference(handle)` + `GetString(modRef.Name)`
    // -- the THROWING raw reads (the corrupt-row BadImageFormatException
    // propagates out of the member; the port's raw-surface exception family
    // is the analog). The ModuleRef row's Name is column 0 (II.22.31).
    std::uint32_t nameOffset = metadataFile_->CorTableColumnValue(
        Metadata::CorTableIndex::ModuleRef, row - 1, 0);
    std::string name = metadataFile_->CorString(nameOffset);
    // The C# ordinal `==` scan over `Compilation.Modules` (case-SENSITIVE,
    // unlike the FindModuleByReference scans).
    for (const IModule* module : Compilation().Modules())
    {
        if (module->Name() == name)
            return module;
    }
    return nullptr;
}

// The C# `public IModule GetDeclaringModule(TypeReferenceHandle handle)` --
// the resolution-scope walk.
const IModule* MetadataModule::GetDeclaringModule(
    std::uint32_t typeReferenceToken) const
{
    std::uint32_t row = typeReferenceToken & 0x00FFFFFFu;
    // The C# `if (handle.IsNil) return null;`.
    if (row == 0)
        return nullptr;
    // The invalid-file degrade (see ResolveModuleReference).
    if (!metadataFile_->IsValid())
        return nullptr;
    // The C# `metadata.GetTypeReference(handle)` -- the THROWING row read (an
    // out-of-range row surfaces the SRM `Read out of bounds.` message; the
    // port reproduces the exact message through std::invalid_argument, the
    // raw-surface family mapped).
    std::optional<Metadata::TypeRefScopeInfo> scope =
        metadataFile_->GetTypeRefScopeInfo(typeReferenceToken);
    if (!scope)
        throw std::invalid_argument("Read out of bounds.");
    switch (scope->Scope)
    {
        case Metadata::TypeRefScopeInfo::Kind::TypeRef:
            // The C# `case HandleKind.TypeReference: return
            // GetDeclaringModule((TypeReferenceHandle)tr.ResolutionScope);`.
            return GetDeclaringModule(scope->ScopeToken);
        case Metadata::TypeRefScopeInfo::Kind::AssemblyRef:
            // The C# `case HandleKind.AssemblyReference: return
            // ResolveModule((AssemblyReferenceHandle)tr.ResolutionScope);`.
            return ResolveModule(scope->ScopeToken);
        case Metadata::TypeRefScopeInfo::Kind::ModuleRef:
            // The C# `case HandleKind.ModuleReference: return
            // ResolveModule((ModuleReferenceHandle)tr.ResolutionScope);`.
            return ResolveModuleReference(scope->ScopeToken);
        default:
            // The C# `default: return this;` -- the Module kind and the nil
            // scope (a nil scope decodes as HandleKind.ModuleDefinition).
            return this;
    }
}

// The C# private `IType ResolveForwardedType(ExportedType forwarder)`.
ITypePtr MetadataModule::ResolveForwardedType(
    std::uint32_t exportedTypeToken) const
{
    const IModule* module = ResolveForwarderModule(exportedTypeToken);
    // The C# `var typeName = forwarder.GetFullTypeName(metadata);` -- the
    // iteration-61 reader (the nested-forwarder declaring-chain walk included).
    FullTypeName typeName =
        Metadata::GetFullTypeNameFromExportedType(*metadataFile_,
                                                   exportedTypeToken);
    if (module == nullptr)
    {
        // The C# `if (module == null) return new UnknownType(typeName);`.
        return std::shared_ptr<IType>(
            new class ::ILSpy::Decompiler::TypeSystem::UnknownType(
                std::move(typeName)));
    }
    {
        // The C# `using (var busyLock = BusyManager.Enter(this))` -- the
        // reentrance guard: a forwarder chain that resolves back into THIS
        // module (a cyclic pair, or the File-implementation TODO resolving
        // within `this`) terminates in the UnknownType below instead of
        // recursing forever.
        Util::BusyLock busyLock = Util::BusyManager::Enter(this);
        if (busyLock.Success())
        {
            // The C# `var td = module.GetTypeDefinition(typeName);` -- the
            // IModule GetTypeDefinition(FullTypeName) extension (the nested-name
            // walk). The call is GLOBALLY QUALIFIED: the member
            // `GetTypeDefinition(TopLevelTypeName)` hides the namespace-scope
            // overload inside the class scope (the self-named-member trap).
            const ITypeDefinition* td =
                ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(
                    *module, typeName);
            if (td != nullptr)
            {
                // The non-owning alias over the target module's cache-owned
                // definition (the no-op-deleter convention; the module owns
                // the definition for the compilation's lifetime).
                return std::shared_ptr<IType>(
                    const_cast<IType*>(static_cast<const IType*>(td)),
                    [](IType*) { /* no-op: the module owns the definition */ });
            }
        }
    }
    return std::shared_ptr<IType>(
        new class ::ILSpy::Decompiler::TypeSystem::UnknownType(
            std::move(typeName)));
}

// The C# local `IModule ResolveModule(ExportedType type)` inside
// ResolveForwardedType (the Implementation-column dispatch).
const IModule* MetadataModule::ResolveForwarderModule(
    std::uint32_t exportedTypeToken) const
{
    // The C# `metadata.GetExportedType(...)` row read -- the token comes from
    // the GetTypeForwarder reverse lookup (an existing row), so the
    // out-of-range arm is unreachable through the public surface; the port
    // maps it to the same `Read out of bounds.` family.
    std::optional<Metadata::MetadataFile::ExportedTypeInfo> row =
        metadataFile_->GetExportedType(exportedTypeToken);
    if (!row)
        throw std::invalid_argument("Read out of bounds.");
    std::uint32_t implementation = row->ImplementationToken;
    std::uint32_t kind = implementation >> 24;
    if (kind == 0x26u)
    {
        // The C# `case HandleKind.AssemblyFile: // TODO : Resolve assembly
        // file (module)... return this;` -- the unresolved TODO: the gold
        // pins the observable behavior (resolving within `this` reaches the
        // busy-lock fallback unless the type is local).
        return this;
    }
    if (kind == 0x27u)
    {
        // The C# `case HandleKind.ExportedType: var outerType =
        // metadata.GetExportedType(...); return ResolveModule(outerType);` --
        // the declaring-row recursion (the nested-forwarder chain walks up
        // to the row whose implementation names the assembly).
        return ResolveForwarderModule(implementation);
    }
    if (kind == 0x23u)
    {
        // The C# `case HandleKind.AssemblyReference:` -- the SHORT-NAME scan
        // (ordinal-ignore-case on `AssemblyName`, NOT the FindModuleByReference
        // FullName-first two-pass). The name read goes through the THROWING
        // raw reads (the corrupt-row arm propagates).
        std::uint32_t refRow = (implementation & 0x00FFFFFFu) - 1;
        std::string shortName = metadataFile_->CorString(
            metadataFile_->CorTableColumnValue(
                Metadata::CorTableIndex::AssemblyRef, refRow,
                kAssemblyRefNameColumn));
        const StringComparer& ignoreCase = StringComparer::OrdinalIgnoreCase();
        for (const IModule* candidate : Compilation().Modules())
        {
            if (ignoreCase.Equals(candidate->AssemblyName(), shortName))
                return candidate;
        }
        return nullptr;
    }
    // The C# `default: throw new BadImageFormatException("Expected
    // implementation to be either an AssemblyFile, ExportedType or
    // AssemblyReference.");` -- UNREACHABLE in the C# (a nil column decodes as
    // a nil FILE handle, and the invalid tag throws at the SRM ctor-time
    // namespace read before this member runs); the port carries the faithful
    // dead arm through its tag-3-to-nil decode.
    throw std::out_of_range(
        "Expected implementation to be either an AssemblyFile, ExportedType "
        "or AssemblyReference.");
}

// --- ISymbol ---

::ILSpy::Decompiler::TypeSystem::SymbolKind MetadataModule::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
}

// The C# `string ISymbol.Name => AssemblyName` (the explicit interface
// implementation over the identity field).
std::string MetadataModule::Name() const
{
    return assemblyName_;
}

// --- ICompilationProvider ---

const ICompilation& MetadataModule::Compilation() const
{
    return compilation_;
}

// --- IModule ---

const Metadata::MetadataFile* MetadataModule::MetadataFile() const
{
    return metadataFile_;
}

// The C# `public bool IsMainModule => this == Compilation.MainModule`.
bool MetadataModule::IsMainModule() const
{
    return this == &Compilation().MainModule();
}

std::string MetadataModule::AssemblyName() const
{
    return assemblyName_;
}

// The null stand-in for the C# nullable property is the default-constructed
// Version{} (convention (c)).
Version MetadataModule::AssemblyVersion() const
{
    return assemblyVersion_;
}

std::string MetadataModule::FullAssemblyName() const
{
    return fullAssemblyName_;
}

// The C# `public IEnumerable<IAttribute> GetAssemblyAttributes()` over the
// AttributeListBuilder (this slice): the assembly row's own custom attributes
// + security declarations, the synthetic [AssemblyVersion] (the raw version
// column's ToString), and the [TypeForwardedTo] rows. The built list is
// CACHED (the C# rebuilds per call; a documented divergence observable only
// through object identity across calls) and projected as raw pointers.
std::vector<const IAttribute*> MetadataModule::GetAssemblyAttributes() const
{
    if (!assemblyAttributesLoaded_)
    {
        Implementation::AttributeListBuilder b(*this);
        // The C# `if (metadata.IsAssembly)` -- the Assembly-table row exists.
        if (metadataFile_->CorTableRowCount(
                Metadata::CorTableIndex::Assembly)
            != 0)
        {
            constexpr std::uint32_t kAssemblyDefinitionToken = 0x20000001;
            b.Add(kAssemblyDefinitionToken,
                  ::ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
            b.AddSecurityAttributes(kAssemblyDefinitionToken);
            // The C# `if (assembly.Version != null)` -- the raw version columns
            // are uint16 (never -1), so the port's default-constructed Version
            // stand-in (Build == -1, the corrupt-row catch arm) is the null
            // test.
            if (assemblyVersion_.Build != -1)
            {
                b.Add(KnownAttribute::AssemblyVersion,
                      KnownTypeCode::String, std::any(assemblyVersion_.ToString()));
            }
            AddTypeForwarderAttributes(b);
        }
        assemblyAttributes_ = b.Build();
        assemblyAttributesLoaded_ = true;
    }
    std::vector<const IAttribute*> result;
    result.reserve(assemblyAttributes_.size());
    for (const auto& attr : assemblyAttributes_)
        result.push_back(attr.get());
    return result;
}

// The C# `public IEnumerable<IAttribute> GetModuleAttributes()`: the Module
// row's own custom attributes + (for a netmodule) the [TypeForwardedTo] rows.
std::vector<const IAttribute*> MetadataModule::GetModuleAttributes() const
{
    if (!moduleAttributesLoaded_)
    {
        Implementation::AttributeListBuilder b(*this);
        // The C# `metadata.GetCustomAttributes(Handle.ModuleDefinition)` --
        // the Module row 1 parent token (table 0x00).
        constexpr std::uint32_t kModuleDefinitionToken = 0x00000001;
        b.Add(kModuleDefinitionToken,
              ::ILSpy::Decompiler::TypeSystem::SymbolKind::Module);
        if (metadataFile_->CorTableRowCount(
                Metadata::CorTableIndex::Assembly)
            == 0)
        {
            AddTypeForwarderAttributes(b);
        }
        moduleAttributes_ = b.Build();
        moduleAttributesLoaded_ = true;
    }
    std::vector<const IAttribute*> result;
    result.reserve(moduleAttributes_.size());
    for (const auto& attr : moduleAttributes_)
        result.push_back(attr.get());
    return result;
}

// The C# `private void AddTypeForwarderAttributes(ref AttributeListBuilder b)`:
// one [TypeForwardedTo] attribute per forwarder ExportedType row. The SRM
// `IsForwarder` is the FUSED predicate (the 0x00200000 flag bit AND an
// AssemblyRef implementation -- iteration 12's WriteModuleHeader learning),
// and the value is the `ResolveForwardedType` result (an IType handle in the
// std::any box).
void MetadataModule::AddTypeForwarderAttributes(
    Implementation::AttributeListBuilder& b) const
{
    for (const auto& row : metadataFile_->GetExportedTypes())
    {
        if ((row.Attributes & 0x00200000u) != 0
            && (row.ImplementationToken >> 24) == 0x23u)
        {
            b.Add(KnownAttribute::TypeForwardedTo, KnownTypeCode::Type,
                  std::any(ResolveForwardedType(row.Token)));
        }
    }
}

// The C# `this == module` early-return; the friend-list decode behind the loop
// is deferred (convention (g)).
bool MetadataModule::InternalsVisibleTo(const IModule& module) const
{
    // The C# `if (this == module) return true;` then the friend-list scan
    // (`string.Equals(module.AssemblyName, shortName, OrdinalIgnoreCase)`).
    if (this == &module)
        return true;
    const StringComparer& ignoreCase = StringComparer::OrdinalIgnoreCase();
    for (const std::string& shortName : GetInternalsVisibleTo())
    {
        if (ignoreCase.Equals(module.AssemblyName(), shortName))
            return true;
    }
    return false;
}

// The C# `private string[] GetInternalsVisibleTo()` (MetadataModule.cs lines
// ~1030-1060): the LazyInit-cached short names of the assembly's
// [InternalsVisibleTo] rows. Each row is classified through the raw
// `IsKnownAttribute` (no IgnoreAttribute gate here -- the C# reads the rows
// directly), its value decoded over the module TypeProvider, and the FIRST
// fixed argument -- when it is a string -- reduced to the SHORT name (the
// portion before the first comma). A non-assembly yields the EMPTY list; the
// per-row decode errors PROPAGATE (the C# has no catch here).
const std::vector<std::string>& MetadataModule::GetInternalsVisibleTo() const
{
    if (!internalsVisibleTo_.has_value())
    {
        std::vector<std::string> list;
        if (metadataFile_->CorTableRowCount(
                Metadata::CorTableIndex::Assembly)
            != 0)
        {
            constexpr std::uint32_t kAssemblyDefinitionToken = 0x20000001;
            for (std::uint32_t attributeToken :
                 metadataFile_->GetCustomAttributeTokens(
                     kAssemblyDefinitionToken))
            {
                if (!Metadata::IsKnownAttribute(*metadataFile_, attributeToken,
                                                KnownAttribute::InternalsVisibleTo))
                    continue;
                std::optional<Metadata::CustomAttributeRowInfo>
                    row = metadataFile_->GetCustomAttribute(attributeToken);
                // An existing row always decodes (the read above succeeded);
                // the C# `attr.DecodeValue(this.TypeProvider)` drives the
                // decoder directly over the row's own constructor token and
                // value blob.
                Metadata::CustomAttributeDecoder decoder(
                    *metadataFile_,
                    const_cast<
                        ::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
                        *typeProvider_));
                Metadata::CustomAttributeValue value = decoder.DecodeValue(
                    row->ConstructorToken,
                    row->ValueBlob ? row->ValueBlob->data() : nullptr,
                    row->ValueBlob ? row->ValueBlob->size() : 0);
                if (value.FixedArguments.size() == 1)
                {
                    // The C# `if (attrValue.FixedArguments[0].Value is string
                    // s) list.Add(GetShortName(s))` -- the null SerString (an
                    // EMPTY std::any) fails the `is string` test, so only a
                    // real string reaches the short-name cut. The any is
                    // MATERIALIZED into a named local (the
                    // dangling-temporary trap: `Value()` returns by value).
                    std::any boxed = value.FixedArguments[0].Value();
                    if (auto s = std::any_cast<std::string>(&boxed))
                    {
                        std::size_t pos = s->find(',');
                        list.push_back(pos == std::string::npos
                                            ? *s
                                            : s->substr(0, pos));
                    }
                }
            }
        }
        internalsVisibleTo_ = std::move(list);
    }
    return *internalsVisibleTo_;
}

const INamespace& MetadataModule::RootNamespace() const
{
    return *rootNamespace_;
}

// The C# `IModule.GetTypeDefinition(TopLevelTypeName)`: the MetadataFile reverse
// lookup (the iteration-61 port, raw token / 0 = nil), the forwarder arm
// deferred, and the hit routed through `GetDefinition` (the real entity).
const ITypeDefinition* MetadataModule::GetTypeDefinition(
    const TopLevelTypeName& topLevelTypeName) const
{
    std::uint32_t typeDefinitionToken = metadataFile_->GetTypeDefinition(topLevelTypeName);
    if (typeDefinitionToken == 0)
    {
        // `GetTypeForwarder(topLevelTypeName)` -- the implicit
        // TopLevelTypeName-to-FullTypeName conversion is the explicit ctor here.
        std::uint32_t forwarderToken =
            metadataFile_->GetTypeForwarder(FullTypeName(topLevelTypeName));
        if (forwarderToken != 0)
        {
            // The C# `return ResolveForwardedType(forwarder).GetDefinition()` --
            // the forwarder resolution (the target module's nested-name walk,
            // an UnknownType for a null module / a busy lock / a miss, whose
            // GetDefinition() is null).
            return ResolveForwardedType(forwarderToken)->GetDefinition();
        }
    }
    return GetDefinition(typeDefinitionToken);
}

// The C# `IEnumerable<ITypeDefinition> TypeDefinitions` -- every TypeDef row
// handle in row order routed through `GetDefinition` (the entity cache
// constructs each definition on first touch).
std::vector<const ITypeDefinition*> MetadataModule::TypeDefinitions() const
{
    std::vector<const ITypeDefinition*> result;
    std::uint32_t count = metadataFile_->TypeDefCount();
    result.reserve(count);
    for (std::uint32_t row = 1; row <= count; row++)
        result.push_back(GetDefinition((0x02u << 24) | row));
    return result;
}

// The C# `TopLevelTypeDefinitions => TypeDefinitions.Where(td =>
// td.DeclaringTypeDefinition == null)` -- the non-nested rows of the full
// TypeDef walk (the entity cache keeps the filter's `DeclaringTypeDefinition`
// read cheap).
std::vector<const ITypeDefinition*> MetadataModule::TopLevelTypeDefinitions() const
{
    std::vector<const ITypeDefinition*> result;
    for (const ITypeDefinition* definition : TypeDefinitions())
    {
        if (definition->DeclaringTypeDefinition() == nullptr)
            result.push_back(definition);
    }
    return result;
}

// The C# `void HandleOutOfRange(EntityHandle handle)` -- the exact message
// through `std::out_of_range` (convention (e)).
void MetadataModule::HandleOutOfRange()
{
    throw std::out_of_range("Handle with invalid row number.");
}

// --- Resolve Type (MetadataModule.cs lines 371-397) ---

// The C# `public IType ResolveType(EntityHandle typeRefDefSpec,
// GenericContext context, CustomAttributeHandleCollection? typeAttributes =
// null, Nullability nullableContext = Nullability.Oblivious)` -- the
// delegating overload over the module's own options.
ITypePtr MetadataModule::ResolveType(
    std::uint32_t typeRefDefSpec, const GenericContext& context,
    const std::optional<std::vector<std::uint32_t>>& typeAttributes,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext) const
{
    return ResolveType(typeRefDefSpec, context, options_, typeAttributes,
                       nullableContext);
}

// The C# `public IType ResolveType(EntityHandle typeRefDefSpec,
// GenericContext context, TypeSystemOptions customOptions,
// CustomAttributeHandleCollection? typeAttributes = null, Nullability
// nullableContext = Nullability.Oblivious)` -- the core overload: the
// top-byte table dispatch, then the ApplyAttributeTypeVisitor wrap.
ITypePtr MetadataModule::ResolveType(
    std::uint32_t typeRefDefSpec, const GenericContext& context,
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions customOptions,
    const std::optional<std::vector<std::uint32_t>>& typeAttributes,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext) const
{
    // The C# `if (typeRefDefSpec.IsNil) return SpecialType.UnknownType;` --
    // the null-object convention (a nil raw token is 0).
    if (typeRefDefSpec == 0)
        return UnknownType();
    ITypePtr ty;
    std::uint32_t kind = typeRefDefSpec >> 24;
    switch (kind)
    {
    case 0x02u:  // HandleKind.TypeDefinition
        // The C# `TypeProvider.GetTypeFromDefinition(metadata,
        // (TypeDefinitionHandle)typeRefDefSpec, 0)` -- the module's entity
        // cache (the CACHED arm range-check throws through the port's
        // `std::out_of_range`). `typeProvider_` is reached directly (the C#
        // field): the accessor returns `const TypeProvider&` while the
        // walker-interface members are non-const (the const `unique_ptr`
        // still hands back the mutable pointee).
        ty = typeProvider_->GetTypeFromDefinition(typeRefDefSpec, 0);
        break;
    case 0x01u:  // HandleKind.TypeReference
        // The C# `TypeProvider.GetTypeFromReference(metadata,
        // (TypeReferenceHandle)typeRefDefSpec, 0)` -- the declaring-module
        // resolution with the UnknownType fallback.
        ty = typeProvider_->GetTypeFromReference(typeRefDefSpec, 0);
        break;
    case 0x1Bu:  // HandleKind.TypeSpecification
        // The C# `var typeSpec = metadata.GetTypeSpecification(...); ty =
        // typeSpec.DecodeSignature(TypeProvider, context);` -- exactly the
        // provider's GetTypeFromSpecification over the row's signature blob
        // (the rawTypeKind is dropped faithfully: the inner
        // CLASS/VALUETYPE marker decides).
        ty = typeProvider_->GetTypeFromSpecification(typeRefDefSpec, 0,
                                                      context);
        break;
    case 0x27u:  // HandleKind.ExportedType
        // The C# `return ResolveForwardedType(metadata.GetExportedType(...))`
        // -- the forwarder arm skips the attribute wrap entirely.
        return ResolveForwardedType(typeRefDefSpec);
    default:
        // The C# `throw new BadImageFormatException("Not a type handle")`
        // (convention (b): the std::invalid_argument mapping).
        throw std::invalid_argument("Not a type handle");
    }
    // The C# `ty = ApplyAttributeTypeVisitor.ApplyAttributesToType(ty,
    // Compilation, typeAttributes, metadata, customOptions, nullableContext)`
    // -- the [Dynamic]/[NativeInteger]/[TupleElementNames]/[Nullable] decode
    // over the resolved type. The `const_cast` carries the C#'s mutable
    // compilation reference through the port's const-module convention (the
    // visitor never mutates the compilation; the parameter is a non-const
    // reference only because the C# property returns the mutable reference).
    return ApplyAttributeTypeVisitor::ApplyAttributesToType(
        std::move(ty), const_cast<ICompilation&>(Compilation()),
        typeAttributes, *metadataFile_, customOptions, nullableContext);
}

// --- Resolve Method (MetadataModule.cs lines 418-753) ---

namespace {

// The C# `static readonly NormalizeTypeVisitor normalizeTypeVisitor = new
// NormalizeTypeVisitor { ReplaceClassTypeParametersWithDummy = true,
// ReplaceMethodTypeParametersWithDummy = true }` -- every option field
// defaults to true, so the default-constructed visitor IS the C# singleton
// (the CompareTypes body is exactly the default visitor's EquivalentTypes).
NormalizeTypeVisitor& CompareTypeNormalizer()
{
    static NormalizeTypeVisitor instance;
    return instance;
}

// A non-owning alias over a method this module (or one of its registries)
// keeps alive -- the `VarArgInstanceMethod` ctor's `shared_ptr<IMethod>`
// parameter over a module-cache-owned `MetadataMethod` (the
// no-op-deleter-alias convention).
std::shared_ptr<IMethod> AliasMethod(IMethod* method)
{
    return std::shared_ptr<IMethod>(method, [](IMethod*) {
        // no-op: the module's cache or registry owns this instance
    });
}

// A non-owning alias over an IType object its owner keeps alive (a
// parameter's type flowing into a fake property's ReturnType, the C#
// reference assignment -- the ReflectionHelper.cpp alias convention).
ITypePtr AliasType(const IType& type)
{
    return ITypePtr(const_cast<IType*>(&type), [](IType*) {
        // no-op: the owner (the parameter / the module registry) keeps it
    });
}

// The decompiled .NET 10 `SignatureKind` ToString spelling for the
// `DecodeFieldSignature` header-check message (Method / Field /
// LocalVariables / Property / MethodSpecification; an unlisted nibble
// renders the decimal value, the .NET enum ToString rule).
std::string SignatureKindName(int lowNibble)
{
    switch (lowNibble)
    {
    case 0: case 1: case 2: case 3: case 4: case 5: case 9:
        return "Method";
    case 6:
        return "Field";
    case 7:
        return "LocalVariables";
    case 8:
        return "Property";
    case 10:
        return "MethodSpecification";
    default:
        return std::to_string(lowNibble);
    }
}

// The C# `IType.TypeArguments` (the AbstractType empty default; the C#
// overrides are ParameterizedType's real list, TupleType's explicit empty,
// and UnknownType's TypeParameters -- the port's UnknownType carries no type
// parameters, so its arm is a documented divergence until that changes).
const std::vector<ITypePtr>& TypeArgumentsOf(const IType& type)
{
    if (const auto* pt = dynamic_cast<const ParameterizedType*>(&type))
        return pt->TypeArguments();
    static const std::vector<ITypePtr> empty;
    return empty;
}

// The C# overload-search loop extracted (the lazy Concat's two arms share
// it): the first candidate matching the generic parameter count, the
// instance/static polarity, and the normalized-signature shape.
const IMethod* SearchOverloads(
    const std::vector<const IMethod*>& candidates,
    const Metadata::ProviderMethodSignature<ITypePtr>& signature,
    const std::vector<ITypePtr>& parameterTypes)
{
    for (const IMethod* m : candidates)
    {
        if (static_cast<int>(m->TypeParameters().size())
            != static_cast<int>(signature.GenericParameterCount))
            continue;
        if (signature.Header.IsInstance() != !m->IsStatic())
            continue;
        if (MetadataModule::CompareSignatures(m->Parameters(), parameterTypes)
            && MetadataModule::CompareTypes(m->ReturnType(),
                                            *signature.ReturnType))
            return m;
    }
    return nullptr;
}

// The C# `IType.GetSubstitution()` (the AbstractType Identity default; the
// ParameterizedType override). Only reached through a non-empty
// TypeArguments guard in the C# call sites.
TypeParameterSubstitution GetSubstitutionOf(const IType& type)
{
    if (const auto* pt = dynamic_cast<const ParameterizedType*>(&type))
        return pt->GetSubstitution();
    return TypeParameterSubstitution::Identity();
}

// A two-lowercase-hex-digit byte render (the SR `0x{0:x2}` format piece).
std::string HexByte2(std::uint8_t value)
{
    static const char* digits = "0123456789abcdef";
    std::string result;
    result += digits[(value >> 4) & 0xF];
    result += digits[value & 0xF];
    return result;
}

} // namespace

// The C# `public IMethod ResolveMethod(EntityHandle methodReference,
// GenericContext context)` -- the top-byte dispatch.
const IMethod* MetadataModule::ResolveMethod(
    std::uint32_t methodReference, const GenericContext& context) const
{
    // The C# `if (methodReference.IsNil) throw new
    // ArgumentNullException(nameof(methodReference))`.
    if (methodReference == 0)
        throw std::invalid_argument(
            "Value cannot be null. (Parameter 'methodReference')");
    switch (methodReference >> 24)
    {
    case 0x06u:  // HandleKind.MethodDefinition
        return ResolveMethodDefinition(methodReference, /*expandVarArgs=*/true);
    case 0x0Au:  // HandleKind.MemberReference
        return ResolveMethodReference(methodReference, context,
                                      std::nullopt, /*expandVarArgs=*/true);
    case 0x2Bu:  // HandleKind.MethodSpecification
        return ResolveMethodSpecification(methodReference, context,
                                          /*expandVarArgs=*/true);
    default:
        // The C# `throw new BadImageFormatException("Metadata token must be
        // either a methoddef, memberref or methodspec")`.
        throw std::invalid_argument(
            "Metadata token must be either a methoddef, memberref or "
            "methodspec");
    }
}

// The C# `IMethod ResolveMethodDefinition(MethodDefinitionHandle
// methodDefHandle, bool expandVarArgs)` -- the entity-cache read plus the
// vararg expansion.
const IMethod* MetadataModule::ResolveMethodDefinition(
    std::uint32_t methodDefToken, bool expandVarArgs) const
{
    const IMethod* method = GetDefinitionMethod(methodDefToken);
    if (expandVarArgs)
    {
        // The C# `if (expandVarArgs &&
        // method.Parameters.LastOrDefault()?.Type.Kind == TypeKind.ArgList)`
        // -- a `?.` over a null LastOrDefault is false; the trailing
        // `__arglist` sentinel marks the vararg method.
        std::vector<const IParameter*> parameters = method->Parameters();
        if (!parameters.empty()
            && parameters.back()->Type().Kind() == TypeKind::ArgList)
        {
            auto wrapper = std::make_shared<VarArgInstanceMethod>(
                AliasMethod(const_cast<IMethod*>(method)),
                std::vector<ITypePtr>{});
            resolvedMethods_.push_back(wrapper);
            method = wrapper.get();
        }
    }
    return method;
}

// The C# `IMethod ResolveMethodSpecification(MethodSpecificationHandle
// methodSpecHandle, GenericContext context, bool expandVarArgs)`.
const IMethod* MetadataModule::ResolveMethodSpecification(
    std::uint32_t methodSpecToken, const GenericContext& context,
    bool expandVarArgs) const
{
    // The C# `metadata.GetMethodSpecification(...)` / `methodSpec.Method`:
    // the port's soft row read maps an unreadable row to the same-mapped
    // `BadImageFormatException` the SRM row read throws (the garbage-read
    // layout an out-of-range token produces in SRM is undefined -- the
    // port's documented clean divergence).
    auto methodSpec = metadataFile_->GetMethodSpecification(methodSpecToken);
    auto blob = metadataFile_->GetMethodSpecificationInstantiationBlob(
        methodSpecToken);
    if (!methodSpec || !blob)
        throw std::invalid_argument("Read out of bounds.");
    // The C# `methodSpec.DecodeSignature(TypeProvider, context)
    // .SelectReadOnlyArray(IntroduceTupleTypes)`.
    std::vector<ITypePtr> methodTypeArgs;
    {
        Metadata::SignatureTypeProviderDecoder<::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
            const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
            TypeProvider()), *metadataFile_);
        std::vector<ITypePtr> decoded = decoder.DecodeMethodSpecSignature(
            blob->data(), blob->size(), context);
        methodTypeArgs.reserve(decoded.size());
        for (ITypePtr& ty : decoded)
            methodTypeArgs.push_back(IntroduceTupleTypes(std::move(ty)));
    }
    const IMethod* method;
    if ((methodSpec->MethodToken >> 24) == 0x06u)
    {
        // The C# `if (methodSpec.Method.Kind == HandleKind.MethodDefinition)`
        // -- the generic instance of a methoddef: resolve the definition,
        // then specialize the method type arguments onto it.
        method = ResolveMethodDefinition(methodSpec->MethodToken,
                                         expandVarArgs);
        TypeParameterSubstitution substitution(std::nullopt,
                                               std::move(methodTypeArgs));
        method = method->Specialize(&substitution);
    }
    else
    {
        // The C# else arm: a MemberRef target carries the method type
        // arguments into the reference resolution.
        method = ResolveMethodReference(methodSpec->MethodToken, context,
                                        std::move(methodTypeArgs),
                                        expandVarArgs);
    }
    return method;
}

// The C# `IMethod ResolveMethodReference(MemberReferenceHandle memberRefHandle,
// GenericContext context, IReadOnlyList<IType> methodTypeArguments = null,
// bool expandVarArgs = true)` -- the member-reference resolution with the
// overload search and the fake-method fallback.
const IMethod* MetadataModule::ResolveMethodReference(
    std::uint32_t memberRefToken, const GenericContext& context,
    const std::optional<std::vector<ITypePtr>>& methodTypeArguments,
    bool expandVarArgs) const
{
    auto memberRef = metadataFile_->GetMemberReference(memberRefToken);
    auto blob = metadataFile_->GetSignatureBlob(memberRefToken);
    if (!memberRef || !blob)
        throw std::invalid_argument("Read out of bounds.");
    // The C# `if (memberRef.GetKind() != MemberReferenceKind.Method) throw
    // new BadImageFormatException($"Member reference must be method, but
    // was: {memberRef.GetKind()}")` -- the GetKind call itself throws the
    // parameterless BadImageFormatException for a signature header that is
    // neither a method nor a field form (the decompiled .NET 10
    // SignatureHeader.Kind rule).
    MemberReferenceKind kind = GetMemberReferenceKind(*blob);
    if (kind != MemberReferenceKind::Method)
    {
        throw std::invalid_argument(
            "Member reference must be method, but was: Field");
    }
    Metadata::ProviderMethodSignature<ITypePtr> signature;
    std::optional<std::vector<ITypePtr>> classTypeArguments;
    const IMethod* method;
    if ((memberRef->ParentToken >> 24) == 0x06u)
    {
        // The C# `if (memberRef.Parent.Kind == HandleKind.MethodDefinition)`
        // -- a memberref whose parent is a methoddef (the vararg-call form
        // emitted inside the defining module): resolve straight to the
        // definition and decode the signature over the CALLER's context.
        method = ResolveMethodDefinition(memberRef->ParentToken,
                                         /*expandVarArgs=*/false);
        Metadata::SignatureTypeProviderDecoder<::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
            const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
            TypeProvider()), *metadataFile_);
        signature = decoder.DecodeMethodSignature(
            blob->data(), blob->size(), context);
    }
    else
    {
        // The C# `var declaringType = ResolveDeclaringType(memberRef.Parent,
        // context); var declaringTypeDefinition = declaringType.GetDefinition();`
        ITypePtr declaringType =
            ResolveDeclaringType(memberRef->ParentToken, context);
        const ITypeDefinition* declaringTypeDefinition =
            declaringType->GetDefinition();
        // The C# `if (declaringType.TypeArguments.Count > 0) classTypeArguments
        // = declaringType.TypeArguments;` -- a REFERENCE the C# GC roots past
        // the declaringType local's block; the port COPIES the vector (the
        // elements are shared_ptr handles to the same IType objects, so the
        // copy is the faithful GC-root equivalent -- a pointer into the
        // short-lived declaringType would dangle at the Specialize below).
        if (!TypeArgumentsOf(*declaringType).empty())
            classTypeArguments = TypeArgumentsOf(*declaringType);
        // The C# `signature = memberRef.DecodeMethodSignature(TypeProvider,
        // new GenericContext(declaringTypeDefinition?.TypeParameters));` --
        // the signature is for the ORIGINAL method definition, decoded over
        // the declaring type's type parameters only.
        std::vector<const ITypeParameter*> declaringTypeParameters;
        if (declaringTypeDefinition != nullptr)
            declaringTypeParameters = declaringTypeDefinition->TypeParameters();
        GenericContext declaringContext(std::move(declaringTypeParameters));
        Metadata::SignatureTypeProviderDecoder<::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
            const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
            TypeProvider()), *metadataFile_);
        signature = decoder.DecodeMethodSignature(
            blob->data(), blob->size(), declaringContext);
        if (declaringTypeDefinition != nullptr)
        {
            // The C# overload-set selection: `.ctor` over the constructors,
            // `.cctor` over the static constructors, and the plain name over
            // the declared methods CONCATENATED with the accessors (the
            // accessor methods are dropped from the Methods enumeration, so
            // the concat is what makes accessor memberrefs resolvable --
            // the accessor arm resolves through the Properties/Events
            // enumerations since the MetadataProperty/MetadataEvent slice
            // landed).
            std::vector<const IMethod*> methods;
            if (memberRef->Name == ".ctor")
            {
                methods = declaringTypeDefinition->GetConstructors(
                    nullptr,
                    GetMemberOptions::IgnoreInheritedMembers);
            }
            else if (memberRef->Name == ".cctor")
            {
                // The C# `declaringTypeDefinition.Methods.Where(m =>
                // m.IsConstructor && m.IsStatic)` -- the raw Methods
                // property (the declared, accessor-dropped enumeration).
                for (const IMethod* m : declaringTypeDefinition->Methods())
                {
                    if (m->IsConstructor() && m->IsStatic())
                        methods.push_back(m);
                }
            }
            else
            {
                // The C# `GetMethods(...).Concat(GetAccessors(...))` is a
                // LAZY concat: the accessor enumeration evaluates only when
                // the methods search is exhausted without a match (a
                // same-name plain method resolves without ever touching the
                // accessor path -- only the accessor-shaped misses reach
                // it).
                const std::string& name = memberRef->Name;
                methods = declaringTypeDefinition->GetMethods(
                    [&name](const IMethod* m) {
                        return m->Name() == name;
                    },
                    GetMemberOptions::IgnoreInheritedMembers);
            }
            // The C# vararg expected-parameters: the required prefix plus
            // the `__arglist` sentinel.
            std::vector<ITypePtr> parameterTypes;
            if (signature.Header.CallingConvention
                == Metadata::SignatureCallingConvention::VarArgs)
            {
                parameterTypes.reserve(signature.RequiredParameterCount + 1);
                for (std::uint32_t i = 0; i < signature.RequiredParameterCount;
                     i++)
                    parameterTypes.push_back(signature.ParameterTypes[i]);
                parameterTypes.push_back(
                    std::make_shared<SpecialType>(TypeKind::ArgList));
            }
            else
            {
                parameterTypes = signature.ParameterTypes;
            }
            // The C# search loop: the first overload matching the generic
            // parameter count, the instance/static polarity, and the
            // normalized-signature shape. The lazy Concat's second arm --
            // the accessor search -- runs only when the methods search is
            // exhausted without a match.
            const std::string& name = memberRef->Name;
            method = SearchOverloads(methods, signature, parameterTypes);
            if (method == nullptr)
            {
                std::vector<const IMethod*> accessors =
                    declaringTypeDefinition->GetAccessors(
                        [&name](const IMethod* m) {
                            return m->Name() == name;
                        },
                        GetMemberOptions::IgnoreInheritedMembers);
                method = SearchOverloads(accessors, signature, parameterTypes);
            }
        }
        else
        {
            method = nullptr;
        }
        if (method == nullptr)
        {
            method = CreateFakeMethod(std::move(declaringType),
                                      memberRef->Name, signature);
        }
    }
    if (classTypeArguments.has_value() || methodTypeArguments.has_value())
    {
        // The C# `method.Specialize(new TypeParameterSubstitution(
        // classTypeArguments, methodTypeArguments))` -- a null list keeps
        // that kind of type parameter unmodified (the std::nullopt state).
        TypeParameterSubstitution substitution(classTypeArguments,
                                               methodTypeArguments);
        method = method->Specialize(&substitution);
    }
    if (expandVarArgs
        && signature.Header.CallingConvention
            == Metadata::SignatureCallingConvention::VarArgs)
    {
        // The C# `new VarArgInstanceMethod(method,
        // signature.ParameterTypes.Skip(signature.RequiredParameterCount))`.
        std::vector<ITypePtr> varArgTypes(
            signature.ParameterTypes.begin() + signature.RequiredParameterCount,
            signature.ParameterTypes.end());
        auto wrapper = std::make_shared<VarArgInstanceMethod>(
            AliasMethod(const_cast<IMethod*>(method)),
            std::move(varArgTypes));
        resolvedMethods_.push_back(wrapper);
        method = wrapper.get();
    }
    return method;
}

// The C# `IType ResolveDeclaringType(EntityHandle declaringTypeReference,
// GenericContext context)` -- resolve WITHOUT the annotation options, then
// introduce tuple types in the type arguments only.
ITypePtr MetadataModule::ResolveDeclaringType(
    std::uint32_t declaringTypeReference, const GenericContext& context) const
{
    // The C# `const TypeSystemOptions removedOptions = ...` -- the annotation
    // options stripped for the resolution itself (the raw shape reaches the
    // overload search, so the signature comparisons see the same types on
    // both sides).
    const ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions removedOptions =
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Dynamic
        | ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Tuple
        | ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::NullabilityAnnotations
        | ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::NativeIntegers
        | ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::NativeIntegersWithoutAttribute;
    ITypePtr ty = ResolveType(
        declaringTypeReference, context,
        static_cast<::ILSpy::Decompiler::TypeSystem::TypeSystemOptions>(
            options_ & ~removedOptions));
    return ApplyAttributeTypeVisitor::ApplyAttributesToType(
        std::move(ty), const_cast<ICompilation&>(Compilation()),
        std::nullopt, *metadataFile_, options_,
        Nullability::Oblivious,
        /*typeChildrenOnly=*/true);
}

// The C# `IType IntroduceTupleTypes(IType ty)`.
ITypePtr MetadataModule::IntroduceTupleTypes(ITypePtr ty) const
{
    return ApplyAttributeTypeVisitor::ApplyAttributesToType(
        std::move(ty), const_cast<ICompilation&>(Compilation()),
        std::nullopt, *metadataFile_, options_,
        Nullability::Oblivious);
}

// The C# `IField ResolveFieldReference(MemberReferenceHandle
// memberReferenceHandle, GenericContext context)`.
const IField* MetadataModule::ResolveFieldReference(
    std::uint32_t memberReferenceToken, const GenericContext& context) const
{
    auto memberRef = metadataFile_->GetMemberReference(memberReferenceToken);
    auto blob = metadataFile_->GetSignatureBlob(memberReferenceToken);
    if (!memberRef || !blob)
        throw std::invalid_argument("Read out of bounds.");
    ITypePtr declaringType =
        ResolveDeclaringType(memberRef->ParentToken, context);
    const ITypeDefinition* declaringTypeDefinition =
        declaringType->GetDefinition();
    // The C# `memberRef.DecodeFieldSignature(...)` runs the SRM
    // `CheckHeader(header, SignatureKind.Field)` first: the SR-formatted
    // message for a non-field header.
    std::uint8_t raw = (*blob)[0];
    int lowNibble = raw & 0x0F;
    if (lowNibble != 6)
        throw std::invalid_argument(
            "Expected signature header for 'Field', but found '"
            + SignatureKindName(lowNibble) + "' (0x" + HexByte2(raw) + ").");
    // The field signature is for the definition, not the generic instance.
    std::vector<const ITypeParameter*> declaringTypeParameters;
    if (declaringTypeDefinition != nullptr)
        declaringTypeParameters = declaringTypeDefinition->TypeParameters();
    GenericContext declaringContext(std::move(declaringTypeParameters));
    Metadata::SignatureTypeProviderDecoder<::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
        const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
            TypeProvider()), *metadataFile_);
    ITypePtr signature = decoder.DecodeType(
        blob->data() + 1, blob->size() - 1, declaringContext);
    // The C# `declaringType.GetFields(f => f.Name == name &&
    // CompareTypes(f.ReturnType, signature), IgnoreInheritedMembers)
    // .FirstOrDefault()`.
    const IField* field = nullptr;
    {
        const std::string& name = memberRef->Name;
        std::vector<const IField*> fields = declaringType->GetFields(
            [&name, &signature](const IField* f) {
                return f->Name() == name
                    && CompareTypes(f->ReturnType(), *signature);
            },
            GetMemberOptions::IgnoreInheritedMembers);
        if (!fields.empty())
            field = fields.front();
    }
    if (field == nullptr)
    {
        // The C# fallback: substitute the type arguments of a generic
        // declaring type into the signature, then build the `FakeField`.
        ITypePtr substituted = std::move(signature);
        if (!TypeArgumentsOf(*declaringType).empty())
        {
            TypeParameterSubstitution substitution =
                GetSubstitutionOf(*declaringType);
            substituted = substituted->AcceptVisitor(substitution);
        }
        auto fake = std::make_shared<Implementation::FakeField>(
            Compilation());
        fake->SetReturnType(std::move(substituted));
        fake->SetName(memberRef->Name);
        fake->SetDeclaringType(std::move(declaringType));
        resolvedFields_.push_back(std::move(fake));
        field = resolvedFields_.back().get();
    }
    return field;
}

// The C# `IMethod CreateFakeMethod(IType declaringType, string name,
// MethodSignature<IType> signature)`.
const IMethod* MetadataModule::CreateFakeMethod(
    ITypePtr declaringType, const std::string& name,
    const Metadata::ProviderMethodSignature<ITypePtr>& signature) const
{
    ::ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind =
        ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    if (name == ".ctor" || name == ".cctor")
        symbolKind = ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor;
    auto m = std::make_shared<Implementation::FakeMethod>(Compilation(),
                                                          symbolKind);
    m->SetDeclaringType(std::move(declaringType));
    m->SetName(name);
    m->SetReturnType(signature.ReturnType);
    m->SetIsStatic(!signature.Header.IsInstance());
    // The C# `TypeParameterSubstitution substitution = null;` -- the
    // optional state: the owned method type parameters for a generic
    // signature, else the declaring type's own substitution.
    std::optional<TypeParameterSubstitution> substitution;
    if (signature.GenericParameterCount > 0)
    {
        std::vector<std::shared_ptr<const ITypeParameter>> typeParameters;
        std::vector<ITypePtr> methodArgs;
        typeParameters.reserve(signature.GenericParameterCount);
        methodArgs.reserve(signature.GenericParameterCount);
        for (std::uint32_t i = 0; i < signature.GenericParameterCount; i++)
        {
            auto tp = std::make_shared<Implementation::DefaultTypeParameter>(
                static_cast<const IMethod*>(m.get()), static_cast<int>(i));
            methodArgs.push_back(tp);
            typeParameters.push_back(std::move(tp));
        }
        m->SetTypeParameters(std::move(typeParameters));
        substitution.emplace(TypeArgumentsOf(*m->DeclaringType()),
                             std::move(methodArgs));
    }
    else if (!TypeArgumentsOf(*m->DeclaringType()).empty())
    {
        substitution.emplace(
            GetSubstitutionOf(*m->DeclaringType()));
    }
    std::vector<std::shared_ptr<const IParameter>> parameters;
    parameters.reserve(signature.RequiredParameterCount);
    for (std::uint32_t i = 0; i < signature.RequiredParameterCount; i++)
    {
        ITypePtr type = signature.ParameterTypes[i];
        if (substitution.has_value())
        {
            // The C# `type.AcceptVisitor(substitution)` -- replace the
            // dummy method type parameters with the owned instances.
            type = type->AcceptVisitor(*substitution);
        }
        parameters.push_back(
            std::make_shared<Implementation::DefaultParameter>(
                std::move(type), ""));
    }
    m->SetParameters(parameters);
    GuessFakeMethodAccessor(m->DeclaringType(), name, signature, m,
                            parameters);
    resolvedMethods_.push_back(std::move(m));
    return resolvedMethods_.back().get();
}

// The C# `void GuessFakeMethodAccessor(...)`: the get_/set_/add_/remove_/
// raise_ name-forms guess the accessor kind and build the owning
// FakeProperty / FakeEvent.
void MetadataModule::GuessFakeMethodAccessor(
    ITypePtr declaringType, const std::string& name,
    const Metadata::ProviderMethodSignature<ITypePtr>& signature,
    const std::shared_ptr<Implementation::FakeMethod>& m,
    const std::vector<std::shared_ptr<const IParameter>>& parameters) const
{
    if (signature.GenericParameterCount > 0)
        return;
    const bool guessedGetter = name.rfind("get_", 0) == 0;
    const bool guessedSetter = name.rfind("set_", 0) == 0;
    if (guessedGetter || guessedSetter)
    {
        std::string propertyName = name.substr(4);
        auto fakeProperty = std::make_shared<Implementation::FakeProperty>(
            Compilation());
        fakeProperty->SetName(propertyName);
        fakeProperty->SetDeclaringType(declaringType);
        fakeProperty->SetIsStatic(m->IsStatic());
        if (guessedGetter)
        {
            if (signature.ReturnType->Kind() == TypeKind::Void)
                return;
            m->SetAccessorKind(MethodSemanticsAttributes::Getter);
            m->SetAccessorOwner(
                static_cast<const IProperty*>(fakeProperty.get()));
            fakeProperty->SetGetter(m.get());
            fakeProperty->SetReturnType(signature.ReturnType);
            fakeProperty->SetIsIndexer(!parameters.empty());
            fakeProperty->SetParameters(parameters);
            resolvedMethodAux_.push_back(std::move(fakeProperty));
            return;
        }
        if (guessedSetter)
        {
            if (parameters.empty()
                || signature.ReturnType->Kind() != TypeKind::Void)
                return;
            m->SetAccessorKind(MethodSemanticsAttributes::Setter);
            m->SetAccessorOwner(
                static_cast<const IProperty*>(fakeProperty.get()));
            fakeProperty->SetSetter(m.get());
            fakeProperty->SetReturnType(
                AliasType(parameters.back()->Type()));
            fakeProperty->SetIsIndexer(parameters.size() > 1);
            fakeProperty->SetParameters(
                std::vector<std::shared_ptr<const IParameter>>(
                    parameters.begin(), parameters.end() - 1));
            resolvedMethodAux_.push_back(std::move(fakeProperty));
            return;
        }
    }
    const bool guessedAdd = name.rfind("add_", 0) == 0;
    const bool guessedRemove = name.rfind("remove_", 0) == 0;
    const bool guessedRaise = name.rfind("raise_", 0) == 0;
    if (guessedAdd || guessedRemove || guessedRaise)
    {
        auto fakeEvent = std::make_shared<Implementation::FakeEvent>(
            Compilation());
        fakeEvent->SetDeclaringType(declaringType);
        fakeEvent->SetIsStatic(m->IsStatic());
        if (guessedAdd)
        {
            if (parameters.size() != 1)
                return;
            m->SetAccessorKind(MethodSemanticsAttributes::Adder);
            m->SetAccessorOwner(
                static_cast<const IEvent*>(fakeEvent.get()));
            fakeEvent->SetName(name.substr(4));
            fakeEvent->SetAddAccessor(m.get());
            fakeEvent->SetReturnType(AliasType(parameters.front()->Type()));
            resolvedMethodAux_.push_back(std::move(fakeEvent));
            return;
        }
        if (guessedRemove)
        {
            if (parameters.size() != 1)
                return;
            m->SetAccessorKind(MethodSemanticsAttributes::Remover);
            m->SetAccessorOwner(
                static_cast<const IEvent*>(fakeEvent.get()));
            fakeEvent->SetName(name.substr(7));
            fakeEvent->SetRemoveAccessor(m.get());
            fakeEvent->SetReturnType(AliasType(parameters.front()->Type()));
            resolvedMethodAux_.push_back(std::move(fakeEvent));
            return;
        }
        if (guessedRaise)
        {
            fakeEvent->SetName(name.substr(6));
            fakeEvent->SetInvokeAccessor(m.get());
            m->SetAccessorKind(MethodSemanticsAttributes::Raiser);
            m->SetAccessorOwner(
                static_cast<const IEvent*>(fakeEvent.get()));
            resolvedMethodAux_.push_back(std::move(fakeEvent));
            return;
        }
    }
}

// The C# `static bool CompareTypes(IType a, IType b)` -- the default
// NormalizeTypeVisitor's EquivalentTypes (all option fields true).
bool MetadataModule::CompareTypes(const IType& a, const IType& b)
{
    return CompareTypeNormalizer().EquivalentTypes(
        const_cast<IType&>(a), const_cast<IType&>(b));
}

// The C# `static bool CompareSignatures(IReadOnlyList<IParameter> parameters,
// ImmutableArray<IType> parameterTypes)`.
bool MetadataModule::CompareSignatures(
    const std::vector<const IParameter*>& parameters,
    const std::vector<ITypePtr>& parameterTypes)
{
    if (parameterTypes.size() != parameters.size())
        return false;
    for (std::size_t i = 0; i < parameterTypes.size(); i++)
    {
        if (!CompareTypes(parameters[i]->Type(), *parameterTypes[i]))
            return false;
    }
    return true;
}

// The decompiled .NET 10 `SignatureHeader.Kind` rule behind
// `MemberReference.GetKind()`: the low nibble <= 5 or == 9 is a method
// signature, 6 is a field signature, anything else is an invalid header
// (the parameterless BadImageFormatException).
MetadataModule::MemberReferenceKind MetadataModule::GetMemberReferenceKind(
    const std::vector<std::uint8_t>& signatureBlob)
{
    if (signatureBlob.empty())
        throw std::invalid_argument(
            "Format of the executable (.exe) or library (.dll) is invalid.");
    int lowNibble = signatureBlob[0] & 0x0F;
    if (lowNibble <= 5 || lowNibble == 9)
        return MemberReferenceKind::Method;
    if (lowNibble == 6)
        return MemberReferenceKind::Field;
    throw std::invalid_argument(
        "Format of the executable (.exe) or library (.dll) is invalid.");
}

// --- Decode Standalone Signature (MetadataModule.cs lines 820-838) ---

// The C# `standaloneSignature.GetKind() != StandaloneSignatureKind.X`
// gate both decode entries run: the raw row read (the nil/out-of-range
// token and the empty blob both throw "Read out of bounds." -- the SRM
// table read and the BlobReader.ReadByte-at-EOF arms), then the header
// nibble (<= 5 or == 9 is Method, 7 is LocalVariables, anything else the
// `GetKind` parameterless BadImageFormatException), then the mismatch
// message ("Expected Method signature" / "Expected LocalVariables
// signature" -- convention (b), the std::invalid_argument mapping).
namespace {
// The blob of the StandaloneSig row, or the "Read out of bounds." throw
// (the nil/out-of-range row and the empty blob share the arm). Returned BY
// VALUE: `GetStandaloneSignatureBlob` hands back a by-value optional, so a
// reference into it would dangle at the return.
std::vector<std::uint8_t> StandaloneSignatureBlobOrThrow(
    const Metadata::MetadataFile* metadataFile, std::uint32_t token)
{
    auto blob = metadataFile->GetStandaloneSignatureBlob(token);
    if (!blob)
        throw std::invalid_argument("Read out of bounds.");
    if (blob->empty())
        throw std::invalid_argument("Read out of bounds.");
    return std::move(*blob);
}
} // namespace

MetadataModule::DecodedStandaloneMethodSignature
MetadataModule::DecodeMethodSignature(
    std::uint32_t standaloneSignatureToken,
    const GenericContext& genericContext) const
{
    auto blob = StandaloneSignatureBlobOrThrow(
        metadataFile_, standaloneSignatureToken);
    int lowNibble = blob[0] & 0x0F;
    if (!(lowNibble <= 5 || lowNibble == 9))
    {
        if (lowNibble == 7)
            throw std::invalid_argument("Expected Method signature");
        // The `GetKind` parameterless form (the field/property-kind
        // header).
        throw std::invalid_argument(
            "Format of the executable (.exe) or library (.dll) is invalid.");
    }
    // The C# `standaloneSignature.DecodeMethodSignature(TypeProvider,
    // genericContext)` -- the walker over the row's blob (the malformed-
    // blob std::logic_error propagates, the documented walker divergence).
    Metadata::SignatureTypeProviderDecoder<
        ::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
            const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
                TypeProvider()),
            *metadataFile_);
    auto signature = decoder.DecodeMethodSignature(
        blob.data(), blob.size(), genericContext);
    // The C# `FunctionPointerType.FromSignature(sig, this)` then the
    // `(FunctionPointerType)IntroduceTupleTypes(fpt)` cast (the visitor
    // returns the same instance unless the walk rebuilds it).
    auto fpt = FunctionPointerType::FromSignature(signature);
    return {signature.Header,
            std::static_pointer_cast<FunctionPointerType>(
                IntroduceTupleTypes(std::move(fpt)))};
}

std::vector<ITypePtr> MetadataModule::DecodeLocalSignature(
    std::uint32_t standaloneSignatureToken,
    const GenericContext& genericContext) const
{
    auto blob = StandaloneSignatureBlobOrThrow(
        metadataFile_, standaloneSignatureToken);
    int lowNibble = blob[0] & 0x0F;
    if (lowNibble != 7)
    {
        if (lowNibble <= 5 || lowNibble == 9)
            throw std::invalid_argument(
                "Expected LocalVariables signature");
        // The `GetKind` parameterless form.
        throw std::invalid_argument(
            "Format of the executable (.exe) or library (.dll) is invalid.");
    }
    Metadata::SignatureTypeProviderDecoder<
        ::ILSpy::Decompiler::TypeSystem::TypeProvider> decoder(
            const_cast<::ILSpy::Decompiler::TypeSystem::TypeProvider&>(
                TypeProvider()),
            *metadataFile_);
    auto types = decoder.DecodeLocalSignature(
        blob.data(), blob.size(), genericContext);
    // The C# `ImmutableArray.CreateRange(types, IntroduceTupleTypes)`.
    std::vector<ITypePtr> result;
    result.reserve(types.size());
    for (auto& t : types)
        result.push_back(IntroduceTupleTypes(std::move(t)));
    return result;
}

// --- Resolve Entity (MetadataModule.cs lines 755-787) ---

// The C# `public IEntity ResolveEntity(EntityHandle entityHandle,
// GenericContext context = default)` -- the any-entity resolution.
const IEntity* MetadataModule::ResolveEntity(
    std::uint32_t entityHandle, const GenericContext& context) const
{
    switch (entityHandle >> 24)
    {
    case 0x01u:  // HandleKind.TypeReference
    case 0x02u:  // HandleKind.TypeDefinition
    case 0x1Bu:  // HandleKind.TypeSpecification
    case 0x27u:  // HandleKind.ExportedType
        // The C# `ResolveDeclaringType(entityHandle, context).GetDefinition()`
        // -- a type without a definition resolves to null.
        return ResolveDeclaringType(entityHandle, context)->GetDefinition();
    case 0x0Au:  // HandleKind.MemberReference
    {
        auto memberRef = metadataFile_->GetMemberReference(entityHandle);
        auto blob = metadataFile_->GetSignatureBlob(entityHandle);
        if (!memberRef || !blob)
            throw std::invalid_argument("Read out of bounds.");
        MemberReferenceKind kind = GetMemberReferenceKind(*blob);
        if (kind == MemberReferenceKind::Method)
        {
            // The C# `for consistency with the MethodDefinition case, never
            // expand varargs`.
            return ResolveMethodReference(entityHandle, context, std::nullopt,
                                          /*expandVarArgs=*/false);
        }
        if (kind == MemberReferenceKind::Field)
        {
            return ResolveFieldReference(entityHandle, context);
        }
        // The C# `default: throw new BadImageFormatException("Unknown
        // MemberReferenceKind")` -- unreachable (GetMemberReferenceKind
        // throws first for any other header), carried faithfully.
        throw std::invalid_argument("Unknown MemberReferenceKind");
    }
    case 0x06u:  // HandleKind.MethodDefinition
        return GetDefinitionMethod(entityHandle);
    case 0x2Bu:  // HandleKind.MethodSpecification
        return ResolveMethodSpecification(entityHandle, context,
                                          /*expandVarArgs=*/false);
    case 0x04u:  // HandleKind.FieldDefinition
        return GetDefinitionField(entityHandle);
    case 0x14u:  // HandleKind.EventDefinition
        return GetDefinitionEvent(entityHandle);
    case 0x17u:  // HandleKind.PropertyDefinition
        return GetDefinitionProperty(entityHandle);
    default:
        return nullptr;
    }
}

// --- The attribute helpers (MetadataModule.cs lines 938-968) ---

// The C# `internal IType GetAttributeType(KnownAttribute attr)`: the
// per-slot LazyInit cache over `Compilation.FindType(attr.GetTypeName())`
// (the implicit TopLevelTypeName -> FullTypeName conversion drives the
// modules-scan FindType extension).
ITypePtr MetadataModule::GetAttributeType(KnownAttribute attr) const
{
    int index = static_cast<int>(attr);
    if (index < 0 || index >= KnownAttributeCount)
        throw std::out_of_range("Index was outside the bounds of the array.");
    ITypePtr& slot = knownAttributeTypes_[static_cast<std::size_t>(index)];
    if (!slot)
        slot = FindType(Compilation(),
                        ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                            GetTypeName(attr)));
    return slot;
}

// The C# `internal IAttribute MakeAttribute(KnownAttribute type)`: the
// per-slot LazyInit cache of the parameterless known-attribute instance (the
// C# `LazyInit.GetOrSet` identity -- the same instance every call).
std::shared_ptr<IAttribute> MetadataModule::MakeAttribute(
    KnownAttribute type) const
{
    int index = static_cast<int>(type);
    if (index < 0 || index >= KnownAttributeCount)
        throw std::out_of_range("Index was outside the bounds of the array.");
    std::shared_ptr<IAttribute>& slot =
        knownAttributes_[static_cast<std::size_t>(index)];
    if (!slot)
    {
        slot = std::make_shared<Implementation::DefaultAttribute>(
            GetAttributeType(type),
            std::vector<CustomAttributeTypedArgument>(),
            std::vector<CustomAttributeNamedArgument>());
    }
    return slot;
}


} // namespace ILSpy::Decompiler::TypeSystem
