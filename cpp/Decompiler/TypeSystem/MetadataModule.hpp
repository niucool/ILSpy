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
// Implementation/MetadataNamespace.hpp). The TYPE-DEFINITION entity slice
// landed: `GetDefinition(TypeDefinitionHandle)` fills the `typeDefs` cache
// with real `MetadataTypeDefinition` entities (the sibling in
// Implementation/MetadataTypeDefinition.hpp), and `TypeDefinitions` /
// `TopLevelTypeDefinitions` / `MetadataNamespace::Types` enumerate them.
// The four sibling entity classes (`MetadataField` / `MetadataMethod` /
// `MetadataProperty` / `MetadataEvent`) and their `GetDefinition`
// overloads land as the following slices; the members that need them stay
// loud `std::logic_error` deferrals in the meantime.
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
//      (the iteration-61 ports) plus the forwarder-hit arm
//      (`ResolveForwardedType(forwarder).GetDefinition()` over the
//      `ResolveModule` / `GetDeclaringModule` / `FindModuleByReference`
//      resolution family, this slice) are REAL; the miss arm falls through
//      to `GetDefinition(nilHandle)` -> null.
//  (g) DEFERRED members (each loud `std::logic_error` where the ported surface
//      reaches it, otherwise absent with this note): the whole `ResolveType` /
//      `ResolveMethod` / `ResolveEntity` / `ResolveDeclaringType` /
//      `CreateFakeMethod` family (the ApplyAttributeTypeVisitor +
//      CustomAttributeDecoder slices -- the `TypeProvider` field and its class
//      LANDED, so the provider itself is no longer a gate);
//      `GetAssemblyAttributes` / `GetModuleAttributes` /
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
#include "Decompiler/TypeSystem/IType.hpp"
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

// Forward declaration of the type-definition entity (the sibling
// Implementation/MetadataTypeDefinition.hpp): the entity-cache members below
// hold it by `unique_ptr`, complete with the pointee incomplete (the
// destructor and the cache fills are out-of-line in the .cpp where it is
// complete).
namespace Implementation { class MetadataTypeDefinition; }

// Forward declaration of the signature provider (the sibling TypeProvider.hpp):
// the `typeProvider_` member holds it by `unique_ptr`, complete with the
// pointee incomplete (the same out-of-line-destructor convention).
class TypeProvider;

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

    // The C# `internal readonly TypeProvider TypeProvider` (the MetadataModule
    // ctor's `new TypeProvider(this)`): the module-owned signature provider
    // every ResolveType / attribute-decode path drives. An accessor (the
    // port's field-to-accessor convention), owned by `unique_ptr` below
    // (the C# GC root). SELF-NAMED-ACCESSOR TRAP: the `TypeProvider()`
    // accessor hides the CLASS name `TypeProvider` for the rest of this
    // class body, so the member below spells the GLOBAL qualification (the
    // D372 crux).
    const TypeProvider& TypeProvider() const;

    // The C# `public ITypeDefinition GetDefinition(TypeDefinitionHandle handle)`
    // -- the entity-resolving member over a raw TypeDef token (`0x02......`).
    // Convention (e): the nil token returns null; the CACHED arm range-checks
    // (a row past the TypeDef table throws `std::out_of_range("Handle with
    // invalid row number.")`) and lazily fills the `typeDefs` slot per 1-based
    // row; the UNCACHED arm constructs without any range check (the C# shape:
    // the row read inside the ctor throws instead). The returned definition is
    // owned by this module (the cache slot or the keep-alive registry).
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

    // --- Resolve Module (MetadataModule.cs lines 305-368) ---
    // The C# `public IModule ResolveModule(AssemblyReferenceHandle handle)` --
    // the CACHED referenced-assembly resolution over a raw AssemblyRef token
    // (`0x23......`): nil -> null; the Uncached option skips the cache (a
    // fresh resolution per call, no range check -- the raw read throws); the
    // cached arm range-checks (a row past the AssemblyRef table throws
    // `Handle with invalid row number.`) and caches the resolved module
    // per 1-based row (a null resolution is NOT cached -- the C#
    // `LazyInit.GetOrSet` stores but the read side only short-circuits on a
    // non-null slot, so a miss resolves again on every call).
    const IModule* ResolveModule(std::uint32_t assemblyReferenceToken) const;
    // The C# `public IModule ResolveModule(ModuleReferenceHandle handle)` --
    // the by-name module scan over a raw ModuleRef token (`0x1A......`): the
    // row's Name string (through the THROWING raw read -- a corrupt row
    // propagates, the C# `GetString` BadImageFormatException arm), then the
    // ORDINAL (case-sensitive) `mod.Name == name` scan over
    // `Compilation.Modules`; nil -> null; a miss -> null (uncached).
    const IModule* ResolveModuleReference(std::uint32_t moduleReferenceToken) const;
    // The C# `public IModule GetDeclaringModule(TypeReferenceHandle handle)` --
    // the resolution-scope walk over a raw TypeRef token (`0x01......`): a
    // TypeRef-scoped row recurses into its scope's row; an AssemblyRef-scoped
    // row routes `ResolveModule`; a ModuleRef-scoped row routes
    // `ResolveModuleReference`; every other kind (Module/nil) returns `this`.
    // Nil -> null; a row past the TypeRef table propagates the raw read's
    // throw (the C# `Read out of bounds.` BadImageFormatException arm, mapped
    // to the same-message `std::invalid_argument`); an invalid FILE degrades
    // to null (the iteration-61 convention).
    const IModule* GetDeclaringModule(std::uint32_t typeReferenceToken) const;

private:
    // The C# `void HandleOutOfRange(EntityHandle handle)` -- throws the exact
    // message through the port's `std::out_of_range` (convention (e)).
    [[noreturn]] static void HandleOutOfRange();

    // The C# `IType ResolveForwardedType(ExportedType forwarder)` (the private
    // member, MetadataModule.cs lines 890-935): the forwarder's target module
    // (the local `ResolveModule(ExportedType)` walk over the Implementation
    // column), the forwarder's full name, then -- guarded by the BusyManager
    // reentrance lock -- the target module's `GetTypeDefinition(FullTypeName)`
    // nested walk; a null module, a busy lock, or a miss yields an
    // `UnknownType`. The returned `ITypePtr` OWNS a fresh `UnknownType` or
    // aliases the target module's cache-owned definition (the no-op-deleter
    // aliasing convention).
    ITypePtr ResolveForwardedType(std::uint32_t exportedTypeToken) const;
    // The C# local `IModule ResolveModule(ExportedType type)` inside
    // `ResolveForwardedType`: the Implementation column dispatch -- a File
    // row returns `this` (the C# TODO, the gold-pinned behavior), an
    // ExportedType row recurses into the outer row, an AssemblyRef row scans
    // `Compilation.Modules` by the SHORT assembly name (ordinal
    // case-insensitive -- NOTE: a DIFFERENT scan than `FindModuleByReference`'s
    // FullName-first two-pass), and anything else throws the default-arm
    // `BadImageFormatException` (unreachable in the C# through any real or
    // crafted input: a nil column decodes as a nil FILE handle -- the
    // AssemblyFile arm -- and the invalid tag 3 throws at the SRM ctor-time
    // namespace read BEFORE this member runs; the port carries the faithful
    // dead arm).
    const IModule* ResolveForwarderModule(std::uint32_t exportedTypeToken) const;

    const ICompilation& compilation_;
    const Metadata::MetadataFile* metadataFile_;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options_;
    std::string assemblyName_;
    Version assemblyVersion_;
    std::string fullAssemblyName_;
    std::unique_ptr<Implementation::MetadataNamespace> rootNamespace_;
    std::unique_ptr<::ILSpy::Decompiler::TypeSystem::TypeProvider> typeProvider_;

    // The C# `readonly MetadataTypeDefinition[] typeDefs` (allocated in the
    // ctor unless the Uncached option is set; index = the 1-based TypeDef row
    // number, slot 0 unused): each slot OWNS its entity (the C# GC root the
    // port's `unique_ptr` models), nullptr until lazily filled.
    mutable std::vector<std::unique_ptr<Implementation::MetadataTypeDefinition>>
        typeDefs_;
    // The UNCACHED arm's keep-alive registry: every freshly constructed
    // definition stays owned here (the C# GC keeps uncached instances
    // alive; the port's returned raw pointers must not dangle -- the
    // SyntheticWpfModule mutable-registry precedent).
    mutable std::vector<std::unique_ptr<Implementation::MetadataTypeDefinition>>
        uncachedDefs_;

    // The C# `readonly IModule[] referencedAssemblies` (allocated in the ctor
    // unless the Uncached option is set; index = the 1-based AssemblyRef row
    // number, slot 0 unused): the resolved modules are owned by the
    // COMPILATION (non-owning pointers); nullptr until lazily filled, and a
    // resolved-NULL slot is never cached (the read side only short-circuits
    // on non-null). An EMPTY vector is the C# null array -- the Uncached arm.
    mutable std::vector<const IModule*> referencedAssemblies_;
};

} // namespace ILSpy::Decompiler::TypeSystem
