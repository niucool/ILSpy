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

// Port of ICSharpCode.Decompiler/TypeSystem/MetadataModule.cs -- the
// `public class MetadataModule : IModule`, the type-system implementation for a
// `Metadata.PEFile`. This is the SKELETON slice: the ctor (the assembly-identity
// computation + the root namespace over the iteration-62 namespace tree), the
// `IModule` identity surface, and `MetadataNamespace` (the sibling in
// Implementation/MetadataNamespace.hpp); the entity-resolving members
// (`GetDefinition` over the `MetadataTypeDefinition` / `MetadataField` /
// `MetadataMethod` / `MetadataProperty` / `MetadataEvent` family) land as the
// following slices, and the members that need them are loud
// `std::logic_error` deferrals in the meantime.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `internal MetadataModule(ICompilation compilation, MetadataFile
//      peFile, TypeSystemOptions options)` ports to a PUBLIC ctor (the
//      reflection-probe-equivalent access divergence: the tests construct the
//      module directly over a real `MetadataFile`; the C# constructs it from
//      `PEFile.Resolve` with `TypeSystemOptions.Default`). The C#
//      `internal readonly MetadataReader metadata` field has no port counterpart
//      -- every metadata read goes through the `MetadataFile` public surface (the
//      cross-layer convention the rest of the port carries).
//  (b) The C# ctor's assembly-identity arms read through SRM's
//      `GetAssemblyDefinition()`/`GetModuleDefinition()` views; the port reads the
//      same raw columns through the `CorTableRowCount`/`CorTableColumnValue`/
//      `CorString`/`CorTableVersionValue` raw surface (the iteration-63
//      raw-surface convention: the faithful corrupt-row throws REQUIRE it -- the
//      port's `GetAssemblyDefinition` degrades a corrupt row to `nullopt`, which
//      would mask the catch arm as "not an assembly"). The `Assembly`-table row
//      count decides `metadata.IsAssembly` (an assembly manifest carries exactly
//      one row; a netmodule none). The C# `catch (BadImageFormatException)` maps
//      to catching BOTH `std::invalid_argument` (the winmd seek/row/terminator
//      throws) and `std::out_of_range` (the port-reader throws) -- the two-member
//      analog family the `TryGetFullAssemblyName` forms catch.
//  (c) The C# `public Version AssemblyVersion { get; }` is a NULLABLE reference
//      property: null for a netmodule and for the corrupt-assembly catch arm (the
//      auto-property is only assigned inside the try). The port's `IModule::
//      AssemblyVersion()` returns `Version` BY VALUE (the D396 interface shape),
//      which has no null -- the null maps to the default-constructed `Version{}`
//      (`0.0.-1.-1`, the `new System.Version()` shape) as the documented
//      null stand-in. The distinction is observable only through
//      `GetAssemblyAttributes`'s `assembly.Version != null` gate (deferred below),
//      which will need the workaround when it lands.
//  (d) `TypeSystemOptions` (the public property) ports to an accessor named after
//      its enum return type (the `SimpleCompilation::TypeSystemOptions` D421
//      precedent): the return type is GLOBALLY QUALIFIED and every later use of
//      the enum type inside the class body is qualified too (the accessor name
//      hides the namespace-scope `TypeSystemOptions` enum for the rest of the
//      class body, the D372 cross-scope name-hiding crux).
//  (e) `GetDefinition(TypeDefinitionHandle)`: the nil-check and the row-range
//      check land now (the range check throws the C#
//      `BadImageFormatException("Handle with invalid row number.")` mapped to
//      `std::out_of_range` carrying the exact message); the entity construction
//      (`new MetadataTypeDefinition(this, handle)`) and the
//      `LazyInit`-cached `typeDefs` array it fills are the NEXT slice, so every
//      in-range non-nil row hits the loud deferral. The four sibling overloads
//      (`Field`/`Method`/`Property`/`EventDefinitionHandle`) land with their
//      entity classes. The C# `Debug.Assert(row != 0)` rows are compiled out.
//  (f) `GetTypeDefinition(TopLevelTypeName)`: the `MetadataFile` reverse lookups
//      (the iteration-61 ports) land now; the miss arm falls through to
//      `GetDefinition(nilHandle)` -> null, and the forwarder-hit arm
//      (`ResolveForwardedType(forwarder).GetDefinition()`) is a loud deferral
//      (gated on `ResolveModule` / `Compilation.FindModuleByReference`).
//  (g) DEFERRED members (each loud `std::logic_error` where the ported surface
//      reaches it, otherwise absent with this note): the `TypeProvider` field and
//      the whole `ResolveType` / `ResolveMethod` / `ResolveEntity` /
//      `ResolveDeclaringType` / `CreateFakeMethod` family (the TypeProvider.cs +
//      CustomAttributeDecoder + ApplyAttributeTypeVisitor slices);
//      `ResolveModule(AssemblyReferenceHandle)` / `ResolveModule(ModuleReference
//      Handle)` / `GetDeclaringModule(TypeReferenceHandle)` (the compilation-side
//      module resolution); `GetAssemblyAttributes` / `GetModuleAttributes` /
//      `GetInternalsVisibleTo` / `InternalsVisibleTo`'s friend-list decode and the
//      ctor's `NullableContext` / `FindMinimumAccessibilityForNRT` (the
//      CustomAttributeDecoder value-decode machinery); the lazy
//      `typeDefs`/`fieldDefs`/`methodDefs`/`propertyDefs`/`eventDefs`/
//      `referencedAssemblies` entity caches (convention (e));
//      `DecodeMethodSignature`/`DecodeLocalSignature`; the `knownAttributeTypes`
//      / `knownAttributes` attribute-type caches; the `IsVisible(FieldAttributes)`
//      / `IsVisible(MethodAttributes)` / `IncludeInternalMembers` /
//      `ShouldDecodeNullableAttributes` / `OptionsForEntity` visibility filter
//      (consumed only by the entity classes); and the internal
//      `GetString(StringHandle)` helper (the port's NamespaceDefinition::Name
//      already stores the resolved name, so MetadataNamespace needs no such
//      helper).

#pragma once

#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Forward declaration of the cross-layer `MetadataFile` (in
// `ILSpy::Decompiler::Metadata`, already ported) -- the ctor takes and
// `MetadataFile()` returns a nullable pointer to it, so only a forward
// declaration is needed here (the `IModule::MetadataFile` convention).
namespace ILSpy::Decompiler::Metadata { class MetadataFile; }

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the snapshot element / parameter types (already
// ported): `IAttribute` (D386), `ITypeDefinition` (D393) -- both mentioned only
// as pointer / snapshot element types here, complete with the pointee
// incomplete.
class IAttribute;
class ITypeDefinition;

// Forward declaration of the per-module namespace node (the sibling
// Implementation/MetadataNamespace.hpp): the `rootNamespace_` member is a
// `unique_ptr` over the incomplete type, so the destructor is out-of-line
// (`~MetadataModule()` declared, `= default`-defined in the .cpp -- the
// incomplete-type `unique_ptr` convention).
namespace Implementation { class MetadataNamespace; }

// The type-system implementation for a metadata PE file: one resolved module.
// Not `final` (the C# class is unsealed).
class MetadataModule : public IModule {
public:
    // The C# `internal` ctor (convention (a)). `metadataFile` is non-null for
    // every module constructed in valid usage (the C# `peFile` comes from the
    // resolver); the port takes the non-owning pointer and keeps it for the
    // module's lifetime (the caller -- the test or the future resolver
    // integration -- owns the `MetadataFile` and keeps it alive).
    MetadataModule(const ICompilation& compilation,
                   const Metadata::MetadataFile* metadataFile,
                   TypeSystemOptions options);

    // Out-of-line destructor: the `rootNamespace_` member's `unique_ptr` deleter
    // needs `Implementation::MetadataNamespace` complete.
    ~MetadataModule();

    MetadataModule(const MetadataModule&) = delete;
    MetadataModule& operator=(const MetadataModule&) = delete;

    // The C# `public TypeSystemOptions TypeSystemOptions => options` (convention
    // (d): the return type is GLOBALLY QUALIFIED).
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const;

    // The C# `public ITypeDefinition GetDefinition(TypeDefinitionHandle handle)`
    // -- the entity-resolving member over a raw TypeDef token (`0x02......`).
    // Convention (e): the nil token returns null, a row past the TypeDef table
    // throws `std::out_of_range("Handle with invalid row number.")`, and every
    // in-range non-nil row currently hits the loud `MetadataTypeDefinition`
    // deferral (the entity-construction slice lands it for real).
    const ITypeDefinition* GetDefinition(std::uint32_t typeDefinitionToken) const;

    // --- ISymbol ---
    // `SymbolKind` return type GLOBALLY QUALIFIED (the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override;

    // --- IModule ---
    const Metadata::MetadataFile* MetadataFile() const override;
    bool IsMainModule() const override;
    std::string AssemblyName() const override;
    Version AssemblyVersion() const override;
    std::string FullAssemblyName() const override;
    // The AttributeListBuilder + custom-attribute value-decode machinery is
    // deferred (convention (g)): both throw `std::logic_error` naming it.
    std::vector<const IAttribute*> GetAssemblyAttributes() const override;
    std::vector<const IAttribute*> GetModuleAttributes() const override;
    // The C# self arm returns true before the friend-list decode; the list decode
    // (`GetInternalsVisibleTo`) is deferred (convention (g)), so every non-self
    // module throws.
    bool InternalsVisibleTo(const IModule& module) const override;
    const INamespace& RootNamespace() const override;
    const ITypeDefinition* GetTypeDefinition(
        const TopLevelTypeName& topLevelTypeName) const override;
    // The entity-resolving enumerations (convention (e)): both hit the
    // `GetDefinition` deferral at their first row.
    std::vector<const ITypeDefinition*> TopLevelTypeDefinitions() const override;
    std::vector<const ITypeDefinition*> TypeDefinitions() const override;

private:
    // The C# `void HandleOutOfRange(EntityHandle handle)` -- throws the exact
    // message through the port's `std::out_of_range` (convention (e)).
    [[noreturn]] static void HandleOutOfRange();

    const ICompilation& compilation_;
    const Metadata::MetadataFile* metadataFile_;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options_;
    std::string assemblyName_;
    Version assemblyVersion_;
    std::string fullAssemblyName_;
    std::unique_ptr<Implementation::MetadataNamespace> rootNamespace_;
};

} // namespace ILSpy::Decompiler::TypeSystem
