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

#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataNamespace.hpp"

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
// Nil -> null; a row past the TypeDef table throws the exact message; the entity
// construction is the next slice, so every in-range non-nil row hits the loud
// deferral (the entity-cache array the C# fills around it lands with it).
const ITypeDefinition* MetadataModule::GetDefinition(
    std::uint32_t typeDefinitionToken) const
{
    std::uint32_t row = typeDefinitionToken & 0x00FFFFFFu;
    if (row == 0)
        return nullptr;
    if (row > metadataFile_->TypeDefCount())
        HandleOutOfRange();
    throw std::logic_error(
        "MetadataModule::GetDefinition: MetadataTypeDefinition is not yet ported");
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
// deferred, and the hit routed through `GetDefinition` (the entity deferral).
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
            // The C# `ResolveForwardedType(forwarder).GetDefinition()` -- the
            // forwarder resolution through the referenced module
            // (`ResolveModule` / `Compilation.FindModuleByReference`) is deferred.
            throw std::logic_error(
                "MetadataModule::GetTypeDefinition: ResolveForwardedType is not "
                "yet ported (gated on ResolveModule / FindModuleByReference)");
        }
    }
    return GetDefinition(typeDefinitionToken);
}

// The C# `IEnumerable<ITypeDefinition> TypeDefinitions` -- every TypeDef row
// handle in row order routed through `GetDefinition`: the deferral fires at
// the first token for every non-empty TypeDef table (an empty table yields the
// empty snapshot).
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
// td.DeclaringTypeDefinition == null)`: the filter reads each definition's
// declaring type, so the enumeration runs the `GetDefinition` deferral before
// any filter can apply -- `TypeDefinitions()` throws at its first token. The
// filter itself lands with the entity slice (DeclaringTypeDefinition is a
// MetadataTypeDefinition-own member).
std::vector<const ITypeDefinition*> MetadataModule::TopLevelTypeDefinitions() const
{
    return TypeDefinitions();
}

// The C# `void HandleOutOfRange(EntityHandle handle)` -- the exact message
// through `std::out_of_range` (convention (e)).
void MetadataModule::HandleOutOfRange()
{
    throw std::out_of_range("Handle with invalid row number.");
}

} // namespace ILSpy::Decompiler::TypeSystem
