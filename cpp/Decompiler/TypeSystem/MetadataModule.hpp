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
// The FIELD entity slice landed: `GetDefinitionField` fills the `fieldDefs`
// cache with real `MetadataField` entities (the sibling in
// Implementation/MetadataField.hpp, with its DecimalConstantHelper), and
// the `IsFieldVisible` / `IncludeInternalMembers` visibility filter the
// `MetadataTypeDefinition::Fields` enumeration consumes. The METHOD entity
// slice landed: `GetDefinitionMethod` fills the `methodDefs` cache with
// real `MetadataMethod` entities (the sibling in
// Implementation/MetadataMethod.hpp, with its `MetadataParameter` companion
// in Implementation/MetadataParameter.hpp), and `IsMethodVisible` is the
// method half of the visibility filter. The remaining sibling entity
// classes (`MetadataProperty` / `MetadataEvent`) and their `GetDefinition`
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
//  (g) LANDED members (each a loud `std::logic_error` before its slice, now
//      real): `ResolveMethod` / `ResolveEntity` / `ResolveDeclaringType` /
//      `CreateFakeMethod` (the resolve-method slice); `GetAssemblyAttributes`
//      / `GetModuleAttributes` / `GetInternalsVisibleTo` (the friend-list
//      decode behind `InternalsVisibleTo`) / the `MakeAttribute` /
//      `GetAttributeType` caches and the NRT-visibility context
//      (`NullableContext`, `FindMinimumAccessibilityForNRT`,
//      `ShouldDecodeNullableAttributes`, `OptionsForEntity` -- the
//      AttributeListBuilder slice over the CustomAttributeDecoder); the
//      `knownAttributeTypes` / `knownAttributes` caches and the entity
//      classes' attribute members (MetadataTypeDefinition / MetadataField /
//      MetadataParameter / MetadataMethod GetAttributes / HasAttribute /
//      GetAttribute / GetReturnTypeAttributes -- the MetadataMethod body's
//      DllImport / PreserveSig / MethodImpl / SpecialName synthetic rows
//      LANDED); the
//      `IsVisible(MethodAttributes)` filter; the `methodDefs` /
//      `referencedAssemblies` / `typeDefs` / `fieldDefs` entity caches.
//      The `propertyDefs`/`eventDefs` entity caches and the accessor-search
//      arm of `ResolveMethodReference` (over
//      `MetadataTypeDefinition::GetAccessors`) LANDED with the
//      MetadataProperty/MetadataEvent slice; the
//      `DecodeMethodSignature`/`DecodeLocalSignature` surface forms LANDED
//      (the StandaloneSig decode entries over the walker +
//      `IntroduceTupleTypes`); the internal
//      `GetString(StringHandle)` helper (the port's NamespaceDefinition::Name
//      already stores the resolved name, so MetadataNamespace needs no such
//      helper).

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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

// Forward declaration of the VAR/MVAR scope (the sibling GenericContext.hpp,
// already ported): `ResolveType` takes it by const reference only, so the
// incomplete type suffices in the declarations (the full include lives in
// the .cpp).
class GenericContext;

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
namespace Implementation { class MetadataField; }
namespace Implementation { class MetadataMethod; }
namespace Implementation { class MetadataProperty; }
namespace Implementation { class MetadataEvent; }
namespace Implementation { class FakeMethod; }

// Forward declarations of the member/result types the resolve-method slice
// returns by raw pointer while the registries own them (`IMethod` / `IField`
// are complete through the includes above; `IEntity` is complete through
// IModule.hpp).
class IMethod;
class IField;
class IEntity;
class IParameter;

// Forward declaration of the signature provider (the sibling TypeProvider.hpp):
// the `typeProvider_` member holds it by `unique_ptr`, complete with the
// pointee incomplete (the same out-of-line-destructor convention).
class TypeProvider;

namespace Implementation {
// Forward declaration of the attribute-list builder (the sibling
// Implementation/AttributeListBuilder.hpp): the AddTypeForwarderAttributes
// parameter needs only the incomplete type (a reference parameter).
class AttributeListBuilder;
} // namespace Implementation

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

    // The C# `internal IType GetAttributeType(KnownAttribute attr)`
    // (MetadataModule.cs lines 943-951): the per-slot LazyInit cache of the
    // known attribute's `Compilation.FindType(GetTypeName(attr))` result
    // (the C# `knownAttributeTypes` array). The returned handle OWNS or
    // ALIASES the cached type (the FindType result is the compilation's).
    ITypePtr GetAttributeType(KnownAttribute attr) const;

    // The C# `internal IAttribute MakeAttribute(KnownAttribute type)`
    // (MetadataModule.cs lines 960-968): the per-slot LazyInit cache of the
    // parameterless known-attribute instance (a `DefaultAttribute` over
    // `GetAttributeType(type)` with no arguments -- the C# `knownAttributes`
    // array). The returned shared_ptr SHARES the module-cached instance
    // (the C# `LazyInit.GetOrSet` identity).
    std::shared_ptr<IAttribute> MakeAttribute(KnownAttribute type) const;

    // --- The NRT-visibility context (MetadataModule.cs lines 84-1060) ---
    // The C# `internal readonly Nullability NullableContext` -- the MODULE
    // row's own [NullableContext] byte, or Oblivious when the module carries
    // none (the ctor computation; the entity contexts chain onto it).
    ::ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const;
    // The C# `internal bool ShouldDecodeNullableAttributes(IEntity entity)`
    // -- the NullabilityAnnotations option gate plus the [NullablePublicOnly]
    // minimum-effective-accessibility filter (a null entity passes; an
    // assembly without [NullablePublicOnly] decodes for every entity).
    bool ShouldDecodeNullableAttributes(const IEntity* entity) const;
    // The C# `internal TypeSystemOptions OptionsForEntity(IEntity entity)` --
    // the per-entity options with NullabilityAnnotations stripped when the
    // entity is below the [NullablePublicOnly] accessibility threshold.
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions OptionsForEntity(
        const IEntity* entity) const;

    // The C# `public ITypeDefinition GetDefinition(TypeDefinitionHandle handle)`
    // -- the entity-resolving member over a raw TypeDef token (`0x02......`).
    // Convention (e): the nil token returns null; the CACHED arm range-checks
    // (a row past the TypeDef table throws `std::out_of_range("Handle with
    // invalid row number.")`) and lazily fills the `typeDefs` slot per 1-based
    // row; the UNCACHED arm constructs without any range check (the C# shape:
    // the row read inside the ctor throws instead). The returned definition is
    // owned by this module (the cache slot or the keep-alive registry).
    const ITypeDefinition* GetDefinition(std::uint32_t typeDefinitionToken) const;

    // The C# `public IField GetDefinition(FieldDefinitionHandle handle)`
    // (MetadataModule.cs lines 236-252) -- the FIELD entity cache, the first
    // member-entity cache (the MetadataField family; the MetadataMethod /
    // MetadataProperty / MetadataEvent siblings follow). The C++ name is
    // DISTINCT (a C++-only disambiguation with no C# counterpart): the C#
    // overloads `GetDefinition` on the HANDLE TYPE, but the port's raw-token
    // convention would give both overloads the same `(std::uint32_t)` signature.
    // Same conventions as the TypeDef arm (the nil token -> null; the CACHED arm
    // range-checks against the `fieldDefs` slot count; the UNCACHED arm
    // constructs without a range check, the keep-alive registry owning the
    // instance); the returned field is owned by this module.
    const IField* GetDefinitionField(std::uint32_t fieldToken) const;

    // The C# `public IMethod GetDefinition(MethodDefinitionHandle handle)`
    // (MetadataModule.cs lines 252-266) -- the per-row METHOD entity cache
    // (the `methodDefs` slots, index = the 1-based MethodDef row number).
    // Same conventions as the field arm (the nil token -> null; the CACHED
    // arm range-checks against the `methodDefs` slot count with the
    // `HandleOutOfRange` throw; the UNCACHED arm constructs without a range
    // check, the keep-alive registry owning the instance); the returned
    // method is owned by this module. The distinct C++ name follows the
    // `GetDefinitionField` disambiguation convention.
    const IMethod* GetDefinitionMethod(std::uint32_t methodToken) const;

    // The C# `public IProperty GetDefinition(PropertyDefinitionHandle
    // handle)` (MetadataModule.cs lines 269-283) -- the per-row PROPERTY
    // entity cache (the `propertyDefs` slots, index = the 1-based Property
    // row number). Same conventions as the field arm (the nil token ->
    // null; the CACHED arm range-checks against the `propertyDefs` slot
    // count with the `HandleOutOfRange` throw; the UNCACHED arm constructs
    // without a range check, the keep-alive registry owning the instance);
    // the returned property is owned by this module. The distinct C++ name
    // follows the `GetDefinitionField` disambiguation convention.
    const IProperty* GetDefinitionProperty(
        std::uint32_t propertyToken) const;

    // The C# `public IEvent GetDefinition(EventDefinitionHandle handle)`
    // (MetadataModule.cs lines 286-299) -- the per-row EVENT entity cache
    // (the `eventDefs` slots, index = the 1-based Event row number). Same
    // conventions as the property arm; the returned event is owned by
    // this module.
    const IEvent* GetDefinitionEvent(std::uint32_t eventToken) const;

    // --- Visibility Filter (MetadataModule.cs lines 971-993) ---
    // The C# `internal bool IncludeInternalMembers`.
    bool IncludeInternalMembers() const;
    // The C# `internal bool IsVisible(FieldAttributes att)` -- the port takes
    // the RAW flags column (masked internally over the FieldAccessMask); the
    // name carries the `Field` qualifier because the C#'s MethodAttributes twin
    // (the MetadataMethod slice) will need the distinct C++ spelling too.
    bool IsFieldVisible(std::uint32_t fieldAttributes) const;
    // The C# `internal bool IsVisible(MethodAttributes att)` (MetadataModule.cs
    // lines 985-993) -- the method half of the visibility filter (the port
    // takes the RAW flags column, masked internally over the
    // MemberAccessMask; the `IsFieldVisible` naming convention).
    bool IsMethodVisible(std::uint32_t methodAttributes) const;

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
    // LANDED over the AttributeListBuilder + CustomAttribute classes: the
    // assembly's own custom attributes + security declarations + the
    // [AssemblyVersion] synthetic row + the `AddTypeForwarderAttributes`
    // walk (the [TypeForwardedTo] rows over the ExportedType table through
    // `ResolveForwardedType`); the module attributes add the module row's
    // own attributes (+ the forwarder walk for a netmodule).
    std::vector<const IAttribute*> GetAssemblyAttributes() const override;
    std::vector<const IAttribute*> GetModuleAttributes() const override;
    // LANDED: the friend-list decode (the C# private `GetInternalsVisibleTo`
    // over the assembly's [InternalsVisibleTo] rows -- exposed for the tests,
    // the internal-access convention). Each entry is the SHORT name (the
    // portion before the first ',').
    const std::vector<std::string>& GetInternalsVisibleTo() const;
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

    // --- Resolve Type (MetadataModule.cs lines 371-397) ---
    // The C# `public IType ResolveType(EntityHandle typeRefDefSpec,
    // GenericContext context, CustomAttributeHandleCollection? typeAttributes
    // = null, Nullability nullableContext = Nullability.Oblivious)` -- the
    // delegating overload over the module's own options. The handle is a raw
    // token whose top byte names the table: `0x02` TypeDefinition (the
    // module's entity cache), `0x01` TypeReference (the declaring-module
    // resolution), `0x1B` TypeSpecification (the blob decode over the
    // GenericContext), `0x27` ExportedType (the forwarder); nil (0) returns
    // the `SpecialType.UnknownType` null object; any other top byte throws
    // the C# `BadImageFormatException("Not a type handle")` mapped to
    // `std::invalid_argument`. The resolved type then flows through
    // `ApplyAttributeTypeVisitor.ApplyAttributesToType` over the (optional)
    // attribute rows. The `typeAttributes` parameter carries the C#
    // `CustomAttributeHandleCollection?` as raw row tokens (the null state
    // is `std::nullopt`).
    ITypePtr ResolveType(
        std::uint32_t typeRefDefSpec, const GenericContext& context,
        const std::optional<std::vector<std::uint32_t>>& typeAttributes
            = std::nullopt,
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext
            = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious) const;
    // The C# `public IType ResolveType(EntityHandle typeRefDefSpec,
    // GenericContext context, TypeSystemOptions customOptions,
    // CustomAttributeHandleCollection? typeAttributes = null, Nullability
    // nullableContext = Nullability.Oblivious)` -- the core overload every
    // arm routes through (the options carry into the
    // ApplyAttributeTypeVisitor walk). The `customOptions` parameter is
    // GLOBALLY QUALIFIED (the `TypeSystemOptions()` accessor hides the enum
    // name for the rest of the class body -- the D372 crux).
    ITypePtr ResolveType(
        std::uint32_t typeRefDefSpec, const GenericContext& context,
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions customOptions,
        const std::optional<std::vector<std::uint32_t>>& typeAttributes
            = std::nullopt,
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext
            = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious) const;

    // The C# `static bool CompareTypes(IType a, IType b)` (a PRIVATE static
    // in the C#; PUBLIC in the port -- the test pin seam, the
    // public-nested-names precedent): normalize both types through the
    // static `NormalizeTypeVisitor` (all options default) and compare --
    // exactly the default visitor's `EquivalentTypes`.
    static bool CompareTypes(const IType& a, const IType& b);
    // The C# `static bool CompareSignatures(IReadOnlyList<IParameter>
    // parameters, ImmutableArray<IType> parameterTypes)` -- the length check
    // then the per-position `CompareTypes`. Same public seam.
    static bool CompareSignatures(
        const std::vector<const IParameter*>& parameters,
        const std::vector<ITypePtr>& parameterTypes);

    // --- Resolve Method (MetadataModule.cs lines 418-753) ---
    // The C# `public IMethod ResolveMethod(EntityHandle methodReference,
    // GenericContext context)` -- resolves a method-def / member-ref /
    // method-spec token to an `IMethod` (varargs expanded): the dispatch over
    // the top byte (0x06 MethodDef -> the entity cache, 0x0A MemberRef -> the
    // overload search with the fake-method fallback, 0x2B MethodSpec -> the
    // generic instantiation specialized onto its target). A nil token throws
    // the `ArgumentNullException` message (mapped to `std::invalid_argument`);
    // any other top byte throws the C# `BadImageFormatException("Metadata
    // token must be either a methoddef, memberref or methodspec")` mapped to
    // `std::invalid_argument` carrying the same text. The returned method is
    // owned by this module (the cache slots, the per-entity Specialize
    // registries, or the `resolvedMethods_` registry below) -- a non-owning
    // pointer the module outlives (the whole type-system lifetime contract).
    const IMethod* ResolveMethod(
        std::uint32_t methodReference,
        const GenericContext& context) const;

    // --- Resolve Entity (MetadataModule.cs lines 755-787) ---
    // The C# `public IEntity ResolveEntity(EntityHandle entityHandle,
    // GenericContext context = default)` -- resolves any entity token to an
    // `IEntity`: the type handles through `ResolveDeclaringType(...).
    // GetDefinition()` (types without a definition resolve to null), a
    // MemberRef through its kind (method -> `ResolveMethodReference` with
    // `expandVarArgs: false`, field -> `ResolveFieldReference`; the kind read
    // itself throws the parameterless `BadImageFormatException` for a
    // non-field/non-method signature header, so the `"Unknown
    // MemberReferenceKind"` default arm is unreachable dead code the port
    // carries faithfully), MethodDef/FieldDef through the entity caches,
    // MethodSpec through `ResolveMethodSpecification(expandVarArgs: false)`,
    // and Property/Event rows through the `GetDefinitionProperty` /
    // `GetDefinitionEvent` caches (the MetadataProperty/MetadataEvent
    // slice). Any other top byte returns null.
    const IEntity* ResolveEntity(
        std::uint32_t entityHandle,
        const GenericContext& context) const;

    // The C# `(SignatureHeader, FunctionPointerType) DecodeMethodSignature(
    // StandaloneSignatureHandle handle, GenericContext genericContext)`
    // (MetadataModule.cs lines 820-829, the "#region Decode Standalone
    // Signature"): the kind check (`GetKind() !=
    // StandaloneSignatureKind.Method` -> the "Expected Method signature"
    // BadImageFormatException; `GetKind` itself throws the parameterless
    // form over a field/property-kind header and "Read out of bounds."
    // over an empty blob or a nil/out-of-range handle -- convention (b),
    // std::invalid_argument), then the walker's method-signature decode
    // over the module TypeProvider and `FunctionPointerType::FromSignature`,
    // the result passed through `IntroduceTupleTypes`. The handle ports as
    // the raw 0x11...... token.
    struct DecodedStandaloneMethodSignature {
        Metadata::SignatureHeader Header;
        std::shared_ptr<FunctionPointerType> Type;
    };
    DecodedStandaloneMethodSignature DecodeMethodSignature(
        std::uint32_t standaloneSignatureToken,
        const GenericContext& genericContext) const;
    // The C# `ImmutableArray<IType> DecodeLocalSignature(
    // StandaloneSignatureHandle handle, GenericContext genericContext)`
    // (MetadataModule.cs lines 830-838): the same kind check against
    // `StandaloneSignatureKind.LocalVariables` ("Expected LocalVariables
    // signature"), then the walker's local-signature decode with each
    // element passed through `IntroduceTupleTypes`.
    std::vector<ITypePtr> DecodeLocalSignature(
        std::uint32_t standaloneSignatureToken,
        const GenericContext& genericContext) const;

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

    // The C# `private void AddTypeForwarderAttributes(ref AttributeListBuilder
    // b)` (MetadataModule.cs, the GetAssemblyAttributes/GetModuleAttributes
    // composition): one [TypeForwardedTo] per forwarder ExportedType row.
    void AddTypeForwarderAttributes(
        Implementation::AttributeListBuilder& b) const;

    // The C# `static Accessibility FindMinimumAccessibilityForNRT(
    // MetadataReader metadata, CustomAttributeHandleCollection
    // customAttributes)` (MetadataModule.cs line 997): the module's
    // [NullablePublicOnly(bool includesInternals)] row -- ProtectedAndInternal
    // when internals are included, Protected otherwise, None without the
    // row.
    ::ILSpy::Decompiler::TypeSystem::Accessibility
    FindMinimumAccessibilityForNRT() const;
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

    // --- Resolve Method internals (MetadataModule.cs lines 438-753) ---
    // The C# `IMethod ResolveMethodDefinition(MethodDefinitionHandle
    // methodDefHandle, bool expandVarArgs)`: the entity cache read, plus the
    // vararg expansion (`Parameters.LastOrDefault()?.Type.Kind ==
    // TypeKind.ArgList` -> the `VarArgInstanceMethod` wrap over the empty
    // vararg-type list).
    const IMethod* ResolveMethodDefinition(
        std::uint32_t methodDefToken, bool expandVarArgs) const;
    // The C# `IMethod ResolveMethodSpecification(MethodSpecificationHandle
    // methodSpecHandle, GenericContext context, bool expandVarArgs)`: the
    // instantiation blob decode (the tuple-type-introducing walk over each
    // type argument), then the target resolution -- a MethodDef target
    // resolves the definition and `Specialize`s the method type arguments
    // onto it; a MemberRef target passes them through `ResolveMethodReference`.
    const IMethod* ResolveMethodSpecification(
        std::uint32_t methodSpecToken, const GenericContext& context,
        bool expandVarArgs) const;
    // The C# `IMethod ResolveMethodReference(MemberReferenceHandle
    // memberRefHandle, GenericContext context, IReadOnlyList<IType>
    // methodTypeArguments = null, bool expandVarArgs = true)`: the
    // MemberRefParent dispatch -- a MethodDef parent resolves straight to the
    // definition (the memberref signature decoded for the vararg check);
    // anything else resolves the DECLARING TYPE (the un-annotated form, then
    // the type-children-only tuple pass), decodes the signature over the
    // declaring type's type parameters, and searches the overloads
    // (`.ctor` over the constructors, `.cctor` over the static constructors,
    // the plain name over `GetMethods` concatenated with `GetAccessors`,
    // both REAL since the MetadataProperty/MetadataEvent slice), matching
    // by the normalized-type signature
    // comparison; a miss builds the `CreateFakeMethod` fallback. The resolved
    // method is then `Specialize`d over the declaring type's / the supplied
    // method type arguments and wrapped in `VarArgInstanceMethod` for a
    // vararg signature when `expandVarArgs`.
    const IMethod* ResolveMethodReference(
        std::uint32_t memberRefToken, const GenericContext& context,
        const std::optional<std::vector<ITypePtr>>& methodTypeArguments
            = std::nullopt,
        bool expandVarArgs = true) const;
    // The C# `IType ResolveDeclaringType(EntityHandle declaringTypeReference,
    // GenericContext context)`: `ResolveType` with the annotation options
    // REMOVED (Dynamic / Tuple / NullabilityAnnotations / NativeIntegers /
    // NativeIntegersWithoutAttribute), then the type-children-only
    // `ApplyAttributeTypeVisitor` pass introducing tuple types in the type
    // arguments (the nullability annotations at the top level stay off).
    ITypePtr ResolveDeclaringType(
        std::uint32_t declaringTypeReference,
        const GenericContext& context) const;
    // The C# `IType IntroduceTupleTypes(IType ty)`: the plain
    // `ApplyAttributeTypeVisitor` pass over the module's own options (no
    // attribute rows).
    ITypePtr IntroduceTupleTypes(ITypePtr ty) const;
    // The C# `IField ResolveFieldReference(MemberReferenceHandle
    // memberReferenceHandle, GenericContext context)`: the declaring type
    // resolution, the FIELD-signature decode over the declaring type's type
    // parameters (the signature is for the definition), and the
    // `GetFields` name-and-type search (`IgnoreInheritedMembers`); a miss
    // builds the `FakeField` fallback (substituted when the declaring type
    // is a generic instance).
    const IField* ResolveFieldReference(
        std::uint32_t memberReferenceToken,
        const GenericContext& context) const;
    // The C# `IMethod CreateFakeMethod(IType declaringType, string name,
    // MethodSignature<IType> signature)`: the `FakeMethod` with the
    // symbolKind ctor/name split, the owned `DefaultTypeParameter` list for a
    // generic signature, the parameter list (substituted over the declaring
    // type's / the owned method type parameters), and the
    // `GuessFakeMethodAccessor` accessor-kind guess. The created method (and
    // the guessed property/event + its parameters + type parameters) are
    // kept alive in the registries below (the C# GC root).
    const IMethod* CreateFakeMethod(
        ITypePtr declaringType, const std::string& name,
        const Metadata::ProviderMethodSignature<ITypePtr>& signature) const;
    // The C# `void GuessFakeMethodAccessor(IType declaringType, string name,
    // MethodSignature<IType> signature, FakeMethod m, List<IParameter>
    // parameters)`: the get_/set_/add_/remove_/raise_ name-forms guess the
    // accessor kind and build the owning `FakeProperty` / `FakeEvent`
    // (non-generic signatures only; a wrong return/parameter shape leaves
    // the method unannotated).
    void GuessFakeMethodAccessor(
        ITypePtr declaringType, const std::string& name,
        const Metadata::ProviderMethodSignature<ITypePtr>& signature,
        const std::shared_ptr<Implementation::FakeMethod>& m,
        const std::vector<std::shared_ptr<const IParameter>>& parameters)
        const;
    // The C# `MemberReference.GetKind()` over the decompiled .NET 10
    // `SignatureHeader.Kind` rule: the signature blob's low nibble <= 5 or
    // == 9 is Method, 6 is Field, anything else is the parameterless
    // `BadImageFormatException` (the `GetKind` call itself throws -- the C#
    // `ResolveMethodReference` interpolation evaluates it inside the
    // `!= MemberReferenceKind.Method` message only for Field).
    enum class MemberReferenceKind { Method, Field };
    static MemberReferenceKind GetMemberReferenceKind(
        const std::vector<std::uint8_t>& signatureBlob);


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
    // port's `shared_ptr` models -- SHARED, not `unique_ptr`, because the
    // C# `this` handle flows through `ChangeNullability` /
    // `AcceptVisitor`/`VisitChildren` over these entities, which the port
    // expresses with `shared_from_this` -- the entity must be
    // shared-managed for the walk/annotation paths to hand the same object
    // back), nullptr until lazily filled.
    mutable std::vector<std::shared_ptr<Implementation::MetadataTypeDefinition>>
        typeDefs_;
    // The UNCACHED arm's keep-alive registry: every freshly constructed
    // definition stays owned here (the C# GC keeps uncached instances
    // alive; the port's returned raw pointers must not dangle -- the
    // SyntheticWpfModule mutable-registry precedent).
    mutable std::vector<std::shared_ptr<Implementation::MetadataTypeDefinition>>
        uncachedDefs_;

    // The C# `readonly MetadataField[] fieldDefs` (allocated in the ctor unless
    // the Uncached option is set; index = the 1-based Field row number, slot 0
    // unused): each slot OWNS its entity (the same `shared_ptr` convention as
    // `typeDefs_` -- the C# GC root), nullptr until lazily filled.
    mutable std::vector<std::shared_ptr<Implementation::MetadataField>>
        fieldDefs_;
    // The field cache's UNCACHED-arm keep-alive registry (the `uncachedDefs_`
    // precedent).
    mutable std::vector<std::shared_ptr<Implementation::MetadataField>>
        uncachedFieldDefs_;

    // The C# `readonly MetadataMethod[] methodDefs` (allocated in the ctor
    // unless the Uncached option is set; index = the 1-based MethodDef row
    // number, slot 0 unused): each slot OWNS its entity (the same
    // `shared_ptr` convention as `fieldDefs_`).
    mutable std::vector<std::shared_ptr<Implementation::MetadataMethod>>
        methodDefs_;
    // The method cache's UNCACHED-arm keep-alive registry.
    mutable std::vector<std::shared_ptr<Implementation::MetadataMethod>>
        uncachedMethodDefs_;

    // The C# `readonly MetadataProperty[] propertyDefs` (allocated in the
    // ctor unless the Uncached option is set; index = the 1-based Property
    // row number, slot 0 unused): each slot OWNS its entity (the same
    // `shared_ptr` convention as `fieldDefs_`).
    mutable std::vector<std::shared_ptr<Implementation::MetadataProperty>>
        propertyDefs_;
    // The property cache's UNCACHED-arm keep-alive registry.
    mutable std::vector<std::shared_ptr<Implementation::MetadataProperty>>
        uncachedPropertyDefs_;

    // The C# `readonly MetadataEvent[] eventDefs` (allocated in the ctor
    // unless the Uncached option is set; index = the 1-based Event row
    // number, slot 0 unused): each slot OWNS its entity (the same
    // `shared_ptr` convention as `fieldDefs_`).
    mutable std::vector<std::shared_ptr<Implementation::MetadataEvent>>
        eventDefs_;
    // The event cache's UNCACHED-arm keep-alive registry.
    mutable std::vector<std::shared_ptr<Implementation::MetadataEvent>>
        uncachedEventDefs_;

    // The C# `readonly IModule[] referencedAssemblies` (allocated in the ctor
    // unless the Uncached option is set; index = the 1-based AssemblyRef row
    // number, slot 0 unused): the resolved modules are owned by the
    // COMPILATION (non-owning pointers); nullptr until lazily filled, and a
    // resolved-NULL slot is never cached (the read side only short-circuits
    // on non-null). An EMPTY vector is the C# null array -- the Uncached arm.
    mutable std::vector<const IModule*> referencedAssemblies_;

    // --- The resolve-method slice's keep-alive registries (the C# GC roots
    // for the freshly built `VarArgInstanceMethod` / `FakeMethod` /
    // `FakeField` results `ResolveMethod` returns) ---
    // Every freshly built IMethod a `ResolveMethod` arm hands back (the C# GC
    // keeps `new VarArgInstanceMethod(...)` / the `CreateFakeMethod` product
    // alive while the caller holds the reference).
    mutable std::vector<std::shared_ptr<IMethod>> resolvedMethods_;
    // Every freshly built IField a `ResolveEntity`/`ResolveFieldReference`
    // arm hands back (the `FakeField` fallback).
    mutable std::vector<std::shared_ptr<IField>> resolvedFields_;
    // The auxiliary objects the fake methods reference -- the guessed
    // `FakeProperty`/`FakeEvent` (the `AccessorOwner` back-pointers) and their
    // freshly built members -- kept alive beside the fake method (the C# GC
    // roots the whole reachable graph through; a cycle of raw back-pointers,
    // so BOTH objects live in module registries instead of owning each
    // other).
    mutable std::vector<std::shared_ptr<void>> resolvedMethodAux_;

    // --- The attribute helpers' caches (the C# `knownAttributeTypes` /
    // `knownAttributes` arrays and the `internalsVisibleTo` LazyInit field,
    // MetadataModule.cs lines 938-968) ---
    // `knownAttributeTypes`: one slot per `KnownAttribute` (sized
    // `KnownAttributeCount` in the ctor), nullptr until lazily filled with
    // the compilation's FindType result.
    mutable std::vector<ITypePtr> knownAttributeTypes_;
    // `knownAttributes`: one slot per `KnownAttribute`, the parameterless
    // instance cache.
    mutable std::vector<std::shared_ptr<IAttribute>> knownAttributes_;
    // `internalsVisibleTo`: nullopt until the first `GetInternalsVisibleTo()`
    // call fills it (the C# LazyInit null-vs-loaded distinction).
    mutable std::optional<std::vector<std::string>> internalsVisibleTo_;
    // The C# `readonly Nullability NullableContext` / `readonly Accessibility
    // minAccessibilityForNRT` fields (the ctor computations over the MODULE
    // row's custom attributes).
    ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext_
        = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    ::ILSpy::Decompiler::TypeSystem::Accessibility
        minAccessibilityForNRT_
        = ::ILSpy::Decompiler::TypeSystem::Accessibility::None;
    // The module-level attribute snapshots (the C# rebuilds them per call;
    // the port caches the built list -- a documented divergence observable
    // only through object identity across calls).
    mutable std::vector<std::shared_ptr<IAttribute>> assemblyAttributes_;
    mutable bool assemblyAttributesLoaded_ = false;
    mutable std::vector<std::shared_ptr<IAttribute>> moduleAttributes_;
    mutable bool moduleAttributesLoaded_ = false;
};

} // namespace ILSpy::Decompiler::TypeSystem
