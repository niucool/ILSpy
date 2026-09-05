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
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataNamespace.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/BusyManager.hpp"

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
        referencedAssemblies_.resize(metadataFile_->CorTableRowCount(
            Metadata::CorTableIndex::AssemblyRef) + 1);
    }

    // The C# `this.TypeProvider = new TypeProvider(this)` -- the module-owned
    // signature provider (this slice). Constructed in the ctor BODY (after the
    // `compilation_` member is initialized: the provider's ctor reads
    // `module.Compilation()`).
    typeProvider_ = std::make_unique<::ILSpy::Decompiler::TypeSystem::TypeProvider>(*this);
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
            std::make_unique<Implementation::MetadataTypeDefinition>(
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
    std::unique_ptr<Implementation::MetadataTypeDefinition>& slot =
        typeDefs_[row];
    if (slot == nullptr)
        slot = std::make_unique<Implementation::MetadataTypeDefinition>(
            *this, typeDefinitionToken);
    return slot.get();
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

// Deferred (convention (g)): the AttributeListBuilder + the custom-attribute
// value decode it drives.
std::vector<const IAttribute*> MetadataModule::GetAssemblyAttributes() const
{
    throw std::logic_error(
        "MetadataModule::GetAssemblyAttributes: AttributeListBuilder is not yet "
        "ported (gated on the custom-attribute value decoder)");
}

std::vector<const IAttribute*> MetadataModule::GetModuleAttributes() const
{
    throw std::logic_error(
        "MetadataModule::GetModuleAttributes: AttributeListBuilder is not yet "
        "ported (gated on the custom-attribute value decoder)");
}

// The C# `this == module` early-return; the friend-list decode behind the loop
// is deferred (convention (g)).
bool MetadataModule::InternalsVisibleTo(const IModule& module) const
{
    if (this == &module)
        return true;
    throw std::logic_error(
        "MetadataModule::InternalsVisibleTo: GetInternalsVisibleTo is not yet "
        "ported (gated on the custom-attribute value decoder)");
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

} // namespace ILSpy::Decompiler::TypeSystem
