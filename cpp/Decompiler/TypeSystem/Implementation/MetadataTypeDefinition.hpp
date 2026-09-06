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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/
// MetadataTypeDefinition.cs -- the `sealed class MetadataTypeDefinition :
// ITypeDefinition`, the TypeDef-table-backed type definition the
// MetadataModule::GetDefinition entity cache constructs. This slice lands
// the CTOR's eagerly-computed identity surface (the full name, the raw
// attributes, the declaring type, the type parameters, the KnownTypeCode,
// the kind chain over the IsEnum/IsValueType/IsDelegate predicates, the
// enum underlying type, the option-gated IsByRefLike/IsReadOnly/
// HasExtensions attribute flags) plus every flag/string/derived accessor
// and the lazily-cached NestedTypes; the member family, the attribute
// snapshot, and the base-type resolution are the loud deferrals of the
// following slices.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `internal MetadataTypeDefinition(MetadataModule module,
//      TypeDefinitionHandle handle)` ports to a PUBLIC ctor (the
//      reflection-probe-equivalent access divergence, the MetadataModule
//      convention (a)): the tests construct definitions through
//      `MetadataModule::GetDefinition` (the realistic path), and the port's
//      `MetadataModule::GetDefinition` is the other constructor caller.
//      The `handle` is the raw `0x02......` TypeDef token; the row is
//      in-range by GetDefinition's contract (a corrupt row read inside the
//      ctor throws, mirroring the C# BadImageFormatException propagation
//      out of the ctor).
//  (b) Every metadata read goes through the `MetadataFile` public surface
//      (the cross-layer convention -- no `MetadataReader` counterpart):
//      `GetTypeDefAttributes` (the raw TypeAttributes column),
//      `GetFullTypeNameFromDefinition` (the SRMExtensions reader), the
//      name info read (the authored MetadataName + the declaring token),
//      `GetGenericParameters` (the GenericParam rows the
//      MetadataTypeParameter factories consume), and the iteration-65
//      SRMExtensions kind/attribute predicates (`IsEnum` with the
//      underlying-type out, `IsValueType`, `IsDelegate`,
//      `HasKnownAttribute`).
//  (c) The C# `ITypeDefinition DeclaringTypeDefinition` is a GC reference;
//      the port's `const ITypeDefinition*` is NON-OWNING (the module's
//      GetDefinition cache owns the declaring type -- constructed here via
//      `module.GetDefinition(declaringToken)`, and kept alive by the cache
//      for the module's lifetime). The `IReadOnlyList<ITypeParameter>
//      TypeParameters` auto-property ports to an OWNING
//      `std::vector<std::shared_ptr<const ITypeParameter>>` (the fresh
//      parameters are owned here; the copy-from-outer entries alias the
//      outer definition's -- the MetadataTypeParameter convention (a)),
//      projected through the `IType::TypeParameters()` override as the
//      non-owning pointer snapshot.
//  (d) The C# `NullableContext` computation (`GetCustomAttributes().
//      GetNullableContext(metadata) ?? module.NullableContext ?? ...`) needs
//      the custom-attribute VALUE decoder -- deferred with the module's
//      `NullableContext` / `minAccessibilityForNRT` (the MetadataModule
//      convention (g)) -- so the port's `NullableContext()` always returns
//      `Oblivious` (the value the C# computes for every assembly without
//      `[NullableContext]` attributes, e.g. every .NET Framework 4.8
//      fixture; a documented divergence observable only over
//      nullable-annotated assemblies such as CoreLib).
//  (e) DEFERRED members (each a loud `std::logic_error` naming the gating
//      machinery, or absent with this note where the port's interface omits
//      the member): the member families that remain -- `Members`/
//      `Properties`/`Events` (the `MetadataProperty`/`MetadataEvent` classes;
//      the `GetMembers`/`GetProperties`/`GetEvents`/`GetAccessors`
//      enumerations route over them; the Void early-exit arms and the
//      NestedTypes-only short-circuit arm ARE real) and `GetConstructors`
//      (the ComHelper.IsComImport co-class arm); `Methods` LANDED over the
//      `MetadataMethod` family together with `GetMethods` (both overloads,
//      the GetMembersHelper routing included) and `IsRecord` (the raw
//      method-name scan -- note the C# scans the RAW method list, NOT the
//      accessor-dropped `Methods` enumeration: a record class's
//      `get_EqualityContract` is an accessor row the Methods enumeration
//      drops, so the raw scan is the only shape that classifies records
//      correctly); `Fields` landed earlier over the `MetadataField` family;
//      `GetAttributes`/`HasAttribute`/`GetAttribute` (AttributeListBuilder +
//      the custom-attribute value decoder); `ExtensionInfo`'s construction
//      (the null arms are real: false when !HasExtensions or
//      ExtensionMembers is off); `DefaultMemberName` (absent -- the port's
//      ITypeDefinition omits the member) and `GetOverrides`/`HasOverrides`
//      (absent -- internal members consumed by the future
//      MetadataProperty/MetadataEvent slice).

#pragma once

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations (the .cpp includes the full headers): the module
// (a reference member is complete with the pointee incomplete) and the
// TypeParameters element type (the owning vector's shared_ptr instantiates
// with the pointee incomplete).
namespace ILSpy::Decompiler::TypeSystem { class MetadataModule; }

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The TypeDef-table-backed type definition (see the header comment).
class MetadataTypeDefinition final : public ITypeDefinition {
public:
    // The C# `internal` ctor (convention (a)).
    MetadataTypeDefinition(const MetadataModule& module,
                           std::uint32_t typeDefToken);

    MetadataTypeDefinition(const MetadataTypeDefinition&) = delete;
    MetadataTypeDefinition& operator=(const MetadataTypeDefinition&) = delete;

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {fullTypeName}"` -- the raw
    // token in 8-digit uppercase hex plus the full name's reflection form.
    // A plain member (the port has no `object.ToString` virtual).
    std::string ToString() const;

    // The C# `public override int GetHashCode() => 0x2e0520f2 ^
    // module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- a plain
    // member (the port has no `object.GetHashCode` virtual); the C# object
    // hash is the identity hash, the port hashes the MetadataFile pointer
    // and the raw token.
    int GetHashCode() const;

    // --- ISymbol / ICompilationProvider ---
    // The `SymbolKind` return type GLOBALLY QUALIFIED (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    const ICompilation& Compilation() const override;

    // --- INamedElement (via the IType/IEntity diamond; the single Name /
    // ReflectionName override is the final overrider for both paths) ---
    std::string FullName() const override;
    // The C# `string Namespace => fullTypeName.TopLevelTypeName.Namespace`.
    std::string Namespace() const override;

    // --- IType ---
    TypeKind Kind() const override;
    std::string Name() const override;
    std::string ReflectionName() const override;
    int TypeParameterCount() const override;
    // The C# `bool? IsReferenceType` -- the Kind switch (Struct/Enum/Void
    // are value types, everything else reference types).
    std::optional<bool> IsReferenceType() const override;
    bool IsByRefLike() const override;
    // The C# `IType ChangeNullability(Nullability)`: `Oblivious` or a value
    // type is `this`; a reference type with an annotation wraps in
    // NullabilityAnnotatedType.
    ITypePtr ChangeNullability(
        ::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
    // The C# `ITypeDefinition IType.GetDefinition() => this;`
    const ITypeDefinition* GetDefinition() const override;
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    // The C# `IReadOnlyList<ITypeParameter> TypeParameters` (convention
    // (c)): the non-owning projection of the owning vector.
    std::vector<const ITypeParameter*> TypeParameters() const override;

    // The C# `IEnumerable<IType> GetNestedTypes(filter, options)`: the
    // `(IgnoreInheritedMembers | ReturnMemberDefinitions)` arm is the
    // NestedTypes-only short-circuit (real); every other arm routes
    // GetMembersHelper, whose base-type walk is real (DirectBaseTypes
    // landed) but whose member reads hit the member-family deferral
    // (convention (e)).
    std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter,
        GetMemberOptions options) const override;
    std::vector<ITypePtr> GetNestedTypes(
        const std::vector<ITypePtr>& typeArguments,
        std::function<bool(const ITypeDefinition*)> filter,
        GetMemberOptions options) const override;
    // The C# member enumerations: the `Kind == Void` early exit (the empty
    // list, real) precedes every routed arm -- the routed arms need the
    // member family (the deferral, convention (e)); the `GetFields`
    // `IgnoreInheritedMembers` bit-test short-circuit over `Fields` is
    // REAL (the `MetadataField` family landed).
    std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IMethod*> GetMethods(
        const std::vector<ITypePtr>& typeArguments,
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IMember*> GetMembers(
        std::function<bool(const IMember*)> filter,
        GetMemberOptions options) const override;
    std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter,
        GetMemberOptions options) const override;

    // The C# `IEnumerable<IType> DirectBaseTypes` -- the Extends resolution
    // through `module.ResolveType` (the type's OWN attribute rows feed the
    // ApplyAttributeTypeVisitor wrap -- the nullability bytes that annotate
    // the base-type position live on the DERIVED type's [Nullable] rows), the
    // interface->Object fallback over `Compilation.FindType(Object)`, and the
    // InterfaceImpl rows resolved with each row's OWN attributes; the whole
    // list is `LazyInit`-cached (UNCONDITIONALLY -- no `Uncached` bypass, the
    // C# `directBaseTypes` field), and the Extends resolution's
    // `BadImageFormatException` arms are swallowed into
    // `SpecialType.UnknownType` (the port's `std::invalid_argument` /
    // `std::out_of_range` mappings of the raw-surface throws).
    std::vector<ITypePtr> DirectBaseTypes() const override;

protected:
    // The C# `public override bool Equals(object obj)`: `obj is
    // MetadataTypeDefinition td && handle == td.handle && module.MetadataFile
    // == td.module.MetadataFile` (the IEquatable<IType> routes here too).
    bool StructuralEquals(const IType& other) const override;

public:
    // --- ITypeDefinitionOrUnknown ---
    const ::ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName()
        const override;

    // --- IEntity ---
    std::uint32_t MetadataToken() const override;
    const ITypeDefinition* DeclaringTypeDefinition() const override;
    // The C# `IType DeclaringType => DeclaringTypeDefinition` -- the
    // non-owning alias over the module-owned declaring type (convention
    // (c)).
    ITypePtr DeclaringType() const override;
    const IModule* ParentModule() const override;
    // DEFERRED (convention (e)): AttributeListBuilder + the custom-attribute
    // value decoder.
    std::vector<const IAttribute*> GetAttributes() const override;
    bool HasAttribute(KnownAttribute attribute) const override;
    const IAttribute* GetAttribute(KnownAttribute attribute) const override;
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override;
    bool IsStatic() const override;
    bool IsAbstract() const override;
    bool IsSealed() const override;

    // --- ITypeDefinition ---
    std::vector<const ITypeDefinition*> NestedTypes() const override;
    // DEFERRED (convention (e)): the MetadataMethod/MetadataProperty/
    // MetadataEvent family (`Members` concatenates all four lists).
    std::vector<const IMember*> Members() const override;
    // The C# `IEnumerable<IField> Fields`: the TypeDef's Field-list rows
    // in row order, filtered through `module.IsVisible` (the OnlyPublicAPI
    // option drops the non-public rows), each resolved through the
    // module's per-row field entity cache; the `LazyInit` cache with the
    // `Uncached` bypass (the lazy `fields_` member).
    std::vector<const IField*> Fields() const override;
    // The C# `IEnumerable<IMethod> Methods`: the TypeDef's Method-list rows
    // in row order, dropping the accessor rows (the MethodSemanticsLookup
    // `GetSemantics(h).Item2 == 0` test) and the rows the `IsVisible`
    // filter rejects, each resolved through the module's per-row method
    // entity cache, with the FakeMethod dummy constructor appended for a
    // struct/enum declaring no parameterless instance constructor; the
    // `LazyInit` cache with the `Uncached` bypass.
    std::vector<const IMethod*> Methods() const override;
    std::vector<const IProperty*> Properties() const override;
    std::vector<const IEvent*> Events() const override;
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode()
        const override;
    ITypePtr EnumUnderlyingType() const override;
    bool IsReadOnly() const override;
    std::string MetadataName() const override;
    bool HasExtensions() const override;
    // The C# null arms are real (`!HasExtensions` or the ExtensionMembers
    // option off); the construction needs Methods (the deferral,
    // convention (e)).
    const ::ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo()
        const override;
    // The deferred [NullableContext] decode (convention (d)).
    ::ILSpy::Decompiler::TypeSystem::Nullability NullableContext()
        const override;
    // The C# `public bool IsRecord` -- the ThreeState-cached raw method-name
    // scan (convention (e): the scan reads the RAW method list, not the
    // accessor-dropped Methods enumeration).
    bool IsRecord() const override;

private:
    // The C# `internal bool HasOverrides(MethodDefinitionHandle)` /
    // `GetOverrides` (absent, convention (e)) stay unported with their
    // consumers; the private `ComputeIsRecord` is the ThreeState-fed helper
    // behind `IsRecord()`.
    bool ComputeIsRecord() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x02...... TypeDef token

    // The eagerly-loaded fields (the C# readonly block):
    ::ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    std::uint32_t attributes_ = 0;  // the raw TypeAttributes column
    std::string metadataName_;     // the AUTHORED short name (backtick kept)
    TypeKind kind_ = TypeKind::Class;
    bool isByRefLike_ = false;
    bool isReadOnly_ = false;
    bool hasExtensions_ = false;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;  // non-owning
    std::vector<std::shared_ptr<const ITypeParameter>> typeParameters_;
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode knownTypeCode_
        = ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::None;
    ITypePtr enumUnderlyingType_;

    // The lazily-loaded `nestedTypes` cache (the C# `ITypeDefinition[]`,
    // null until the first read; the Uncached option skips the store).
    mutable std::vector<const ITypeDefinition*> nestedTypes_;
    mutable bool nestedTypesLoaded_ = false;

    // The lazily-loaded `directBaseTypes` cache (the C# `List<IType>`
    // `directBaseTypes` field -- `LazyInit.GetOrSet`, NO `Uncached` bypass:
    // the C# caches the list unconditionally); engaged state = read.
    mutable std::optional<std::vector<ITypePtr>> directBaseTypes_;

    // The lazily-loaded `fields` cache (the C# `IField[] fields` field,
    // `LazyInit.GetOrSet` with the `Uncached` BYPASS -- the fresh list
    // per read under the option; the port's by-value vector reuses the
    // engaged-state-as-read shape).
    mutable std::optional<std::vector<const IField*>> fields_;

    // The lazily-loaded `methods` cache (the C# `IMethod[] methods` field,
    // the same `LazyInit.GetOrSet` + `Uncached` bypass shape as `fields_`).
    mutable std::optional<std::vector<const IMethod*>> methods_;

    // The keep-alive registry for the OWNED method instances this type
    // constructs itself: the `FakeMethod` dummy constructors the Methods()
    // enumeration adds to structs/enums without a default constructor, and
    // the fresh `SpecializedMethod` instances the GetMembersHelper routing
    // produces (the C# GC owns both; the port's owning-slot stand-in, the
    // LocalFunctionMethod rewraps precedent -- each read appends, so the
    // `Uncached` option's fresh-per-read entities stay alive).
    mutable std::vector<std::shared_ptr<const IMethod>> methodKeepAlives_;

    // The ThreeState-cached `isRecord` field (the C# `byte isRecord =
    // ThreeState.Unknown`; 0 = Unknown, 1 = False, 2 = True).
    mutable std::uint8_t isRecord_ = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
