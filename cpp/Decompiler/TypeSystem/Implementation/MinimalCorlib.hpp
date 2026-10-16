// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MinimalCorlib.cs -- an
// artificial "assembly" that contains all known types (`KnownTypeReference` table
// entries) and no other types, with no members. It is the module the
// `MetadataExtensions.minimalCorlibTypeProvider` builds its `TypeProvider` over (the
// provider `ApplyAttributeTypeVisitor` decodes attribute values through) and the
// fallback `DecompilerTypeSystem` appends when known types are missing from the
// referenced assemblies (`MinimalCorlib.CreateWithTypes(missingKnownTypes)`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public static readonly IModuleReference Instance` (one process-wide
//      reference over `KnownTypeReference.AllKnownTypes`) ports to a `static const
//      IModuleReference& Instance()` -- a function-local static
//      `CorlibModuleReference` (the Meyers-singleton pattern, the
//      `TypeParameterSubstitution::Identity` precedent) returning the reference the
//      `SimpleCompilation` ctor's `const IModuleReference&` parameter binds to. The
//      process-lifetime singleton also keeps every resolved module alive for any
//      compilation built over it (see convention (b)).
//  (b) The C# `CorlibModuleReference.Resolve` news up a FRESH `MinimalCorlib` on every
//      call (gold: two `SimpleCompilation`s over `Instance` yield distinct main
//      modules) and the GC keeps it alive past the compilation, which holds only
//      non-owning references. The port's compilation surface also holds non-owning
//      pointers (the D399 convention), so the port's reference keeps every resolved
//      module alive in a `shared_ptr` registry (the `SyntheticWpfModule::SyntheticModuleReference`
//      mutable-registry precedent, the closest C# sibling of this class).
//  (c) The C# `typeDefinitions` array (materialized eagerly in the ctor over the
//      `IEnumerable<KnownTypeReference>`, in table order) ports to an owning
//      `std::vector<std::shared_ptr<CorlibTypeDefinition>>`; `TopLevelTypeDefinitions`
//      / `TypeDefinitions` return non-owning raw-pointer snapshots (the IModule
//      convention), and `GetTypeDefinition` scans them comparing
//      `td.FullTypeName == topLevelTypeName` (gold: the lookups are IDENTITY-STABLE --
//      two lookups for the same type return the same instance).
//  (d) The C# `CorlibNamespace.childNamespaces` list is NEVER populated (no code in
//      the C# assembly writes it -- verified by grep over the whole C# tree): the
//      root namespace always exposes ZERO children, zero types (no known type lives in
//      the empty namespace), and `GetChildNamespace` always returns null. The port
//      keeps the empty vector member for fidelity and the tests pin the quirk.
//  (e) The C# `CorlibNamespace.GetTypeDefinition` calls the `TypeSystemExtensions`
//      `GetTypeDefinition(module, namespaceName, name, tpc)` extension, which wraps
//      the module's `GetTypeDefinition(TopLevelTypeName)`; the port composes the
//      `TopLevelTypeName` directly (the extension is a thin wrapper).
//  (f) The C# `MinimalCorlib.Instance`/`CreateWithTypes` `IEnumerable<KnownTypeReference>`
//      element type ports to `const KnownTypeReference*` (the port's
//      `KnownTypeReference::Get`/`AllKnownTypes` return non-owning pointers into the
//      process-lifetime static table). A null entry is the C# NRE arm (`ktr.KnownTypeCode`
//      on a null `KnownTypeReference`); the port's ctor dereferences the pointer (UB
//      on null, the documented divergence for the unreachable arm -- `AllKnownTypes`
//      never yields null and `Get` returns null only for `KnownTypeCode.None`, which
//      no caller passes).
//  (g) The C# `readonly Version asmVersion = new Version(0, 0, 0, 0)` is a shared
//      reference instance; the port's `IModule::AssemblyVersion` returns `Version` BY
//      VALUE (the D396 interface contract), so the port constructs `Version{0, 0, 0, 0}`
//      per call -- value-equal, not reference-equal (documented divergence).
//  (h) The C# `TypeParameterSubstitution IType.GetSubstitution() =>
//      TypeParameterSubstitution.Identity` and `ITypeDefinitionOrUnknown
//      IType.GetDefinitionOrUnknown() => this` are members the port's reduced `IType`
//      base does not declare (the D459 "the full IType surface lands with Phase 2"
//      deferral list); they stay deferred on the port (no consumer reaches them
//      through the port's surface yet).
//  (i) `IType.TypeArguments` (the C# returns the same dummy list as `TypeParameters`)
//      is likewise not declared on the port's base `IType` (only `ParameterizedType`
//      carries it); deferred with the same note.
//  (j) `DummyTypeParameter.GetClassTypeParameterList(int)` -- the `TypeParameters`
//      list source -- was deferred from the port's `DummyTypeParameter` at landing
//      time ("no ported consumer yet"); this port LIFTS the deferral (its first
//      consumer): a function-local static list cache grown to the requested length,
//      each entry `GetClassTypeParameter(0..i-1)` (process-lifetime instances, so the
//      `std::vector<const ITypeParameter*>` snapshot pointers stay stable).
//  (k) The C# explicit interface members the port's flattened interface hierarchy
//      covers with inherited defaults (the empty `GetMethods`/`GetFields`/... member
//      enumerations -- the flattened-AbstractType D406 convention) are not
//      re-overridden (the `SyntheticWpfModule::SyntheticTypeDefinition` precedent);
//      only `TypeParameters` (the dummy list) and `DirectBaseTypes` (the
//      `FindType(baseType)` walk) override non-empty behavior.
//  (l) `sealed class` -> `final` for all three nested classes and the module.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// An artificial "assembly" that contains all known types and no other types. It
// does not contain any members.
class MinimalCorlib final : public IModule {
public:
    // The C# `public static readonly IModuleReference Instance` -- a minimal corlib
    // reference containing all known types (the process-lifetime singleton, header
    // convention (a)).
    static const IModuleReference& Instance();

    // The C# `public static IModuleReference CreateWithTypes(
    // IEnumerable<KnownTypeReference> types)` -- a reference over a caller-chosen
    // subset (e.g. only the known types missing from a compilation's referenced
    // assemblies). The C# returns a GC-owned reference; the port transfers a
    // `unique_ptr` to the caller, which must keep it alive for any compilation built
    // over it (the `SyntheticWpfModule::CreateReference` ownership precedent).
    static std::unique_ptr<IModuleReference> CreateWithTypes(
        std::vector<const KnownTypeReference*> types);

    // --- ICompilationProvider ---
    const ::ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;

    // --- IModule ---
    const ::ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override;
    bool IsMainModule() const override;
    std::string AssemblyName() const override;
    ::ILSpy::Decompiler::TypeSystem::Version AssemblyVersion() const override;
    std::string FullAssemblyName() const override;
    std::vector<const IAttribute*> GetAssemblyAttributes() const override;
    std::vector<const IAttribute*> GetModuleAttributes() const override;
    bool InternalsVisibleTo(
        const ::ILSpy::Decompiler::TypeSystem::IModule& module) const override;
    const INamespace& RootNamespace() const override;
    // The C# `public ITypeDefinition GetTypeDefinition(TopLevelTypeName)` -- PUBLIC
    // on the concrete class (the only non-explicit IModule member).
    const ITypeDefinition* GetTypeDefinition(
        const TopLevelTypeName& topLevelTypeName) const override;
    std::vector<const ITypeDefinition*> TopLevelTypeDefinitions() const override;
    std::vector<const ITypeDefinition*> TypeDefinitions() const override;

private:
    // The C# private ctor -- only the nested CorlibModuleReference.Resolve calls it.
    MinimalCorlib(const ::ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                  std::vector<const KnownTypeReference*> types);

public:
    // The nested classes. The C# declares them PRIVATE (implementation details
    // reachable only through the IModuleReference / INamespace / ITypeDefinition
    // interfaces); the port declares the NAMES public so the test suite can pin the
    // nested-class surface directly (the SyntheticWpfModule nested-classes-public
    // precedent), while the module's private ctor keeps the construction path
    // locked-down.
    class CorlibModuleReference;
    class CorlibNamespace;
    class CorlibTypeDefinition;

private:
    // The compilation this module belongs to (non-owning; the compilation outlives
    // its modules -- the C# GC back-reference convention).
    const ::ILSpy::Decompiler::TypeSystem::ICompilation* compilation_;
    // The materialized type definitions, in `KnownTypeCode` table order (the C#
    // `types.Select(ktr => new CorlibTypeDefinition(this, ktr.KnownTypeCode))`
    // array). Owning: the module hands out identity-stable non-owning snapshots.
    std::vector<std::shared_ptr<CorlibTypeDefinition>> typeDefinitions_;
    // The module's root namespace (owning; the C# `new CorlibNamespace(this, null,
    // string.Empty, string.Empty)`).
    std::shared_ptr<CorlibNamespace> rootNamespace_;
};

// The C# `sealed class CorlibModuleReference : IModuleReference` -- the reference
// holding the known-type list; every `Resolve` materializes a fresh module.
class MinimalCorlib::CorlibModuleReference final : public IModuleReference {
public:
    explicit CorlibModuleReference(std::vector<const KnownTypeReference*> types)
        : types_(std::move(types)) {}

    // The C# `IModule IModuleReference.Resolve(ITypeResolveContext context)`.
    const IModule* Resolve(
        const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext& context)
        const override;

private:
    std::vector<const KnownTypeReference*> types_;
    // Every resolved module, kept alive for the compilation's lifetime (header
    // convention (b); `mutable`: `Resolve` is const through the `const
    // IModuleReference&` compilation-construction path).
    mutable std::vector<std::shared_ptr<MinimalCorlib>> resolved_;
};

// The C# `sealed class CorlibNamespace : INamespace` -- the (empty) namespace node.
// `childNamespaces` is never populated (header convention (d)): the root namespace
// exposes zero children and zero types; every type lookup goes through the module's
// `GetTypeDefinition(TopLevelTypeName)` directly.
class MinimalCorlib::CorlibNamespace final : public INamespace {
public:
    CorlibNamespace(const MinimalCorlib* corlib, const INamespace* parentNamespace,
                    std::string fullName, std::string name)
        : corlib_(corlib),
          parentNamespace_(parentNamespace),
          fullName_(std::move(fullName)),
          name_(std::move(name)) {}

    // The C# `internal List<INamespace> childNamespaces` -- never populated (the
    // mutable list shape is kept for fidelity; exposed for the port's tests as the
    // reflection-equivalent access).
    std::vector<const INamespace*>& ChildNamespaceList() const { return childNamespaces_; }

    // --- ICompilationProvider ---
    const ::ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override { return name_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return std::string(); }
    std::string FullName() const override { return fullName_; }
    const INamespace* ParentNamespace() const override { return parentNamespace_; }
    std::vector<const INamespace*> ChildNamespaces() const override;
    std::vector<const ITypeDefinition*> Types() const override;
    std::vector<const IModule*> ContributingModules() const override;
    const INamespace* GetChildNamespace(const std::string& name) const override;
    const ITypeDefinition* GetTypeDefinition(
        const std::string& name, int typeParameterCount) const override;

private:
    const MinimalCorlib* corlib_;
    const INamespace* parentNamespace_;
    std::string fullName_;
    std::string name_;
    mutable std::vector<const INamespace*> childNamespaces_;
};

// The C# `sealed class CorlibTypeDefinition : ITypeDefinition` -- the definition of
// one known type: the full `IType` / `ITypeDefinition` identity surface over the
// `KnownTypeReference` table row, with empty member families, dummy type parameters,
// and `DirectBaseTypes` resolved through the compilation's `FindType`.
class MinimalCorlib::CorlibTypeDefinition final : public ITypeDefinition {
public:
    // The C# ctor reads `KnownTypeReference.Get(typeCode)` (NRE on `KnownTypeCode.None`;
    // the port dereferences the table pointer, convention (f)).
    explicit CorlibTypeDefinition(const MinimalCorlib* corlib,
                                  ::ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode);

    // The C# `public override string ToString() => $"[MinimalCorlibType {typeCode}]"`
    // -- a non-virtual diagnostic member rendering the enum member name.
    std::string ToString() const;

    // --- IType ---
    TypeKind Kind() const override { return typeKind_; }
    // The single `Name()` override is the final overrider for the IType-vs-INamedElement
    // diamond (the ITypeDefinition redeclarations).
    std::string Name() const override;
    std::string ReflectionName() const override;
    int TypeParameterCount() const override;
    std::optional<bool> IsReferenceType() const override;
    bool IsByRefLike() const override { return false; }
    // The C# `Nullability IType.Nullability => Nullability.Oblivious`.
    ::ILSpy::Decompiler::TypeSystem::Nullability Nullability() const override;
    // The C# `IType IType.ChangeNullability(Nullability)`: `Oblivious` returns this;
    // any other annotation wraps in a `NullabilityAnnotatedType`.
    ITypePtr ChangeNullability(
        ::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
    // The C# `ITypeDefinition IType.GetDefinition() => this`.
    const ITypeDefinition* GetDefinition() const override { return this; }
    // The C# `IType IType.AcceptVisitor(TypeVisitor) =>
    // visitor.VisitTypeDefinition(this)` (out-of-line: `TypeVisitor` complete).
    ITypePtr AcceptVisitor(::ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override;
    // The C# `IType IType.VisitChildren(TypeVisitor) => this` -- returns this
    // WITHOUT visiting anything (gold: a recording visitor records nothing).
    ITypePtr VisitChildren(::ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override;
    // The C# `TypeParameters` / `DirectBaseTypes` overrides (conventions (j)/(k)).
    std::vector<const ITypeParameter*> TypeParameters() const override;
    std::vector<ITypePtr> DirectBaseTypes() const override;

    // --- ITypeDefinitionOrUnknown ---
    const ::ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override
    {
        return fullTypeName_;
    }

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // --- INamedElement ---
    // The C# `string INamedElement.FullName => ktr.Namespace + "." + ktr.Name` --
    // NO arity suffix (distinct from `ReflectionName`, which appends `` `N``).
    std::string FullName() const override;
    std::string Namespace() const override;

    // --- ICompilationProvider ---
    const ::ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // --- IEntity ---
    // The C# `EntityHandle IEntity.MetadataToken => MetadataTokens.TypeDefinitionHandle(0)`
    // -- the nil TypeDef handle as the raw token (gold-pinned 0x02000000, the
    // SyntheticWpfModule same-quirk precedent).
    std::uint32_t MetadataToken() const override { return 0x02000000; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return corlib_;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute attribute)
        const override
    {
        (void)attribute;
        return false;
    }
    const IAttribute* GetAttribute(
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        (void)attribute;
        return nullptr;
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    // The C# `bool IEntity.IsAbstract => typeKind == TypeKind.Interface`.
    bool IsAbstract() const override
    {
        return typeKind_ == TypeKind::Interface;
    }
    // The C# `bool IEntity.IsSealed => typeKind == TypeKind.Struct`.
    bool IsSealed() const override { return typeKind_ == TypeKind::Struct; }

    // --- ITypeDefinition-own ---
    std::vector<const ITypeDefinition*> NestedTypes() const override { return {}; }
    std::vector<const IMember*> Members() const override { return {}; }
    std::vector<const IField*> Fields() const override { return {}; }
    std::vector<const IMethod*> Methods() const override { return {}; }
    std::vector<const IProperty*> Properties() const override { return {}; }
    std::vector<const IEvent*> Events() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override
    {
        return typeCode_;
    }
    // The C# `IType ITypeDefinition.EnumUnderlyingType => SpecialType.UnknownType`.
    ITypePtr EnumUnderlyingType() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::UnknownType();
    }
    std::string MetadataName() const override { return metadataName_; }
    bool HasExtensions() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo()
        const override
    {
        return nullptr;
    }
    bool IsReadOnly() const override { return false; }
    // The C# `Nullability ITypeDefinition.NullableContext => Nullability.Oblivious`.
    ::ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    bool IsRecord() const override { return false; }

protected:
    // The C# `bool IEquatable<IType>.Equals(IType other) => this == other` -- the
    // reference equality, reached through `IType::Equals` (the Kind check passes for
    // same-kind instances; distinct kinds short-circuit earlier).
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    const MinimalCorlib* corlib_;
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode_;
    TypeKind typeKind_;
    std::string metadataName_;
    ::ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
