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

// Port of ICSharpCode.BamlDecompiler/SyntheticWpfModule.cs -- an artificial stand-in
// for one of the well-known BAML assemblies (e.g. PresentationFramework) when the real
// assembly cannot be resolved (for instance when a WPF binary is inspected on a machine
// that has no WPF installed). It mirrors MinimalCorlib: it materializes type definitions
// on request but carries no members. Only the types the BAML decompiler explicitly seeds
// via RegisterType are materialized; every other lookup returns null, so references to
// non-well-known types keep decomposing to UnknownType exactly as they do when the
// assembly is entirely absent.
//
// The module is the piece of the KnownThings resolution chain that lets KnownThings work
// without the full MetadataModule back end: BamlDecompilerTypeSystem substitutes a
// SyntheticModuleReference for every well-known BAML assembly that could not be resolved,
// and KnownThings.InitType seeds the 759 well-known type rows through RegisterType.
//
// C#-to-C++ porting decisions:
//  * Module ownership: the C# `SyntheticModuleReference.Resolve` news up a fresh
//    SyntheticWpfModule on EVERY call (gold: two resolves produce distinct instances,
//    each starting empty) and the GC keeps it alive past the compilation, which holds
//    only non-owning references. The port's compilation interface also holds non-owning
//    pointers (the D399 convention), so the port's SyntheticModuleReference keeps every
//    resolved module alive in a shared_ptr registry (a mutable vector) -- each Resolve
//    appends a fresh make_shared instance and returns the raw pointer, faithfully
//    reproducing the no-caching contract while satisfying C++ ownership.
//  * The C# Dictionary<TopLevelTypeName, SyntheticTypeDefinition> iterates its Values
//    in insertion order for TopLevelTypeDefinitions / TypeDefinitions and the
//    XmlnsDefinitionAttribute namespace set (Distinct over the keys' namespaces
//    preserves first-occurrence order), so the port stores an insertion-ordered
//    vector of (name, shared_ptr) pairs (the MergedNamespace flat-vector convention,
//    the XmlnsDictionary iteration-order precedent) with a linear-scan lookup -- the
//    well-known row counts (759 types max) make the O(n) scan immaterial.
//  * The C# `string presentationXmlnsNamespace` (nullable; null means "no XmlnsDefinition
//    reconstruction") ports to `std::optional<std::string>` -- the null-vs-empty
//    distinction is load-bearing (an empty XML namespace still maps seeded CLR
//    namespaces; the gold pins the empty-namespace attribute).
//  * The C# `ICompilation Compilation` (GC back-reference) ports to a non-owning
//    `const ICompilation*` (the compilation outlives its modules; the D399
//    non-owning-convention). The `IAssemblyReference assemblyName` the reference and
//    module retain ports to a `shared_ptr<const IAssemblyReference>` (the C# GC
//    guarantee mapped to shared ownership; the caller transfers a shared handle in,
//    e.g. a make_shared<AssemblyNameReference>(AssemblyNameReference::Parse(...))).
//  * The C# `IReadOnlyList<IAttribute> assemblyAttributes` cache (null until the
//    first GetAssemblyAttributes, nulled by RegisterType so a new namespace appears)
//    ports to `mutable std::optional<std::vector<std::shared_ptr<DefaultAttribute>>>`
//    (nullopt is the C# null); the cached DefaultAttribute instances are shared-owned
//    so repeated reads return the SAME attribute objects (the gold pins instance
//    identity across calls until invalidation).
//  * The C# `EntityHandle MetadataToken => MetadataTokens.TypeDefinitionHandle(0)` (a
//    nil TypeDef handle) ports to the raw token uint32 0x02000000 (the port's
//    IEntity.MetadataToken raw-token convention, D381; MetadataTokens.GetToken over the
//    nil handle yields exactly 0x02000000 -- gold-pinned).
//  * The C# `bool IEquatable<IType>.Equals(IType other) => this == other` is the
//    AbstractType default (reference equality); the port's StructuralEquals override
//    is `this == &other` (the LookupTypeDefinition stub identity precedent), reached
//    through the non-virtual IType::Equals Kind check.
//  * The C# explicit interface members the port's reduced IType surface does not yet
//    declare (TypeArguments, GetDefinitionOrUnknown, GetSubstitution -- the UnknownType
//    "full IType surface lands with Phase 2" deferral note) stay deferred; no
//    BamlDecompiler consumer reaches them on a synthetic type.
//  * The C# `SyntheticTypeDefinition`'s empty Get* member-enumeration implementations
//    (GetMethods/GetProperties/... => EmptyList) are the IType defaults' behavior in the
//    port (the flattened-AbstractType D406 convention) and are not re-overridden (the
//    LookupTypeDefinition stub convention); the ITypeDefinition-own member lists
//    (NestedTypes/Members/Fields/Methods/Properties/Events) ARE overridden.
//  * `sealed class` -> `final` for all three nested classes and the module.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Forward declarations keeping the include graph minimal. `IAssemblyReference` (D
// AssemblyNameReference.hpp, already ported) is only mentioned through a shared_ptr
// (a shared_ptr member does not require the pointee complete). `ICompilation` (D378) is
// mentioned as a non-owning pointer member / reference parameter only. The nested
// classes are declared inside the class below and defined after it (the enclosing class
// must be complete before their bodies dereference the module back-pointer).
namespace ILSpy::Decompiler::Metadata { class IAssemblyReference; }

namespace ILSpy::Decompiler::TypeSystem { class ICompilation; }

namespace ILSpy::BamlDecompiler {

class SyntheticWpfModule final : public ILSpy::Decompiler::TypeSystem::IModule {
public:
    // The C# `public static IModuleReference CreateReference(IAssemblyReference
    // assemblyName, string presentationXmlnsNamespace)` -- the module reference that
    // resolves to a SyntheticWpfModule standing in for the assembly. When
    // `presentationXmlnsNamespace` is set, the synthesized assembly exposes an
    // XmlnsDefinitionAttribute mapping every seeded CLR namespace to that XML
    // namespace, so known WPF types serialize with the clean presentation xmlns
    // instead of a clr-namespace fallback.
    //
    // The C# returns a GC-owned `IModuleReference`; the port returns a `unique_ptr`
    // transferred to the caller (the C# `new` heap allocation), which must outlive any
    // compilation built over it (the compilation and resolved modules hold non-owning
    // pointers into the reference's registry).
    static std::unique_ptr<ILSpy::Decompiler::TypeSystem::IModuleReference> CreateReference(
        std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName,
        std::optional<std::string> presentationXmlnsNamespace);

    // The C# `public ITypeDefinition RegisterType(string ns, string name)` -- seeds
    // this module with a synthetic definition for the type and returns it. Repeated
    // calls for the same type return the cached instance. This is the only way a type
    // becomes visible on the module; plain GetTypeDefinition(TopLevelTypeName) lookups
    // never create new types. Registering also invalidates the cached xmlns attributes
    // (a namespace may have appeared). Non-owning return: the module owns the type
    // definition (the caller holds a raw pointer).
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* RegisterType(const std::string& ns,
                                                                       const std::string& name);

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;

    // --- IModule ---
    const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override;
    bool IsMainModule() const override;
    std::string AssemblyName() const override;
    ILSpy::Decompiler::TypeSystem::Version AssemblyVersion() const override;
    std::string FullAssemblyName() const override;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAssemblyAttributes() const override;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetModuleAttributes() const override;
    bool InternalsVisibleTo(const ILSpy::Decompiler::TypeSystem::IModule& module) const override;
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName& topLevelTypeName) const override;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TopLevelTypeDefinitions() const override;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TypeDefinitions() const override;

private:
    // The C# private ctor -- only the nested SyntheticModuleReference.Resolve calls it
    // (a nested class has access to the enclosing class's private members).
    SyntheticWpfModule(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                       std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName,
                       std::optional<std::string> presentationXmlnsNamespace);

public:
    // The nested classes. The C# declares them PRIVATE (they are implementation
    // details reachable only through the public IModuleReference / INamespace /
    // ITypeDefinition interfaces; the gold probe drove them through reflection); the
    // port declares the NAMES public so the test suite can pin the nested-class
    // surface directly (the C++-equivalent of the reflection probe's access -- the
    // MetadataTableDumper TableName-hoist precedent), while the module's private ctor
    // keeps the construction path as locked-down as the C#.
    class SyntheticModuleReference;
    class SyntheticNamespace;
    class SyntheticTypeDefinition;

private:
    // The compilation this module belongs to (non-owning; the compilation outlives
    // its modules).
    const ILSpy::Decompiler::TypeSystem::ICompilation* compilation_;
    // The assembly this module stands in for (shared-owned; the C# GC reference).
    std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName_;
    // The presentation XML namespace the seeded CLR namespaces map to (nullopt: no
    // XmlnsDefinitionAttribute reconstruction).
    std::optional<std::string> presentationXmlnsNamespace_;
    // The seeded type definitions, in registration (insertion) order -- the faithful
    // substitute for the C# Dictionary iteration order (the header doc comment). The
    // module owns the definitions (shared_ptr so RegisterType hands back stable
    // instances and the snapshots are non-owning pointers).
    std::vector<std::pair<ILSpy::Decompiler::TypeSystem::TopLevelTypeName,
                          std::shared_ptr<SyntheticTypeDefinition>>> typeDefinitions_;
    // The module's root namespace (owned; the C# `new SyntheticNamespace(this, null,
    // string.Empty, string.Empty)`).
    std::shared_ptr<SyntheticNamespace> rootNamespace_;
    // The cached XmlnsDefinitionAttribute set (nullopt = the C# null cache; `mutable`
    // for the lazy const GetAssemblyAttributes; RegisterType resets it). The cached
    // DefaultAttribute instances are shared-owned so repeated reads return the same
    // objects (gold-pinned instance identity).
    mutable std::optional<std::vector<std::shared_ptr<
        ILSpy::Decompiler::TypeSystem::Implementation::DefaultAttribute>>> assemblyAttributes_;
};

// The C# `sealed class SyntheticModuleReference : IModuleReference` -- the deferred
// resolution handle the compilation-construction path resolves into a fresh
// SyntheticWpfModule. Defined after the enclosing class (its Resolve body constructs
// one); a nested class has access to the enclosing class's private ctor.
class SyntheticWpfModule::SyntheticModuleReference final
    : public ILSpy::Decompiler::TypeSystem::IModuleReference {
public:
    SyntheticModuleReference(
        std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName,
        std::optional<std::string> presentationXmlnsNamespace)
        : assemblyName_(std::move(assemblyName)),
          presentationXmlnsNamespace_(std::move(presentationXmlnsNamespace))
    {
    }

    // The C# `IModule IModuleReference.Resolve(ITypeResolveContext context) => new
    // SyntheticWpfModule(context.Compilation, assemblyName, presentationXmlnsNamespace)`
    // -- a FRESH module per call (no caching). The port keeps every resolved module
    // alive in the registry (the ownership decision in the header doc comment) and
    // returns the non-owning pointer.
    const ILSpy::Decompiler::TypeSystem::IModule* Resolve(
        const ILSpy::Decompiler::TypeSystem::ITypeResolveContext& context) const override;

private:
    std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName_;
    std::optional<std::string> presentationXmlnsNamespace_;
    // The resolved modules, in resolve order (each Resolve appends a fresh instance;
    // `mutable` -- Resolve is const, like the C#). This registry is the port's stand-in
    // for the GC ownership the compilation's non-owning pointers rely on.
    mutable std::vector<std::shared_ptr<SyntheticWpfModule>> resolved_;
};

// The C# `sealed class SyntheticNamespace : INamespace` -- the module's root namespace.
// Only the root ("" / "") is ever constructed (the module ctor), but the class keeps
// the C#'s general (module, parent, fullName, name) shape: the Types enumeration filters
// the module's registered types by namespace, and every other walk is empty / null (the
// synthetic module never builds a namespace tree).
class SyntheticWpfModule::SyntheticNamespace final
    : public ILSpy::Decompiler::TypeSystem::INamespace {
public:
    SyntheticNamespace(const SyntheticWpfModule* module,
                       const ILSpy::Decompiler::TypeSystem::INamespace* parentNamespace,
                       std::string fullName,
                       std::string name)
        : module_(module),
          parentNamespace_(parentNamespace),
          fullName_(std::move(fullName)),
          name_(std::move(name))
    {
    }

    // The C# `string INamespace.ExternAlias => string.Empty`.
    std::string ExternAlias() const override { return {}; }

    // The C# `string INamespace.FullName` / `string ISymbol.Name` -- the stored values.
    std::string FullName() const override { return fullName_; }
    std::string Name() const override { return name_; }

    // The C# `INamespace INamespace.ParentNamespace` -- the stored nullable parent
    // (null for the root).
    const ILSpy::Decompiler::TypeSystem::INamespace* ParentNamespace() const override
    {
        return parentNamespace_;
    }

    // The C# `IEnumerable<INamespace> INamespace.ChildNamespaces => EmptyList`.
    std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> ChildNamespaces() const override
    {
        return {};
    }

    // The C# `IEnumerable<ITypeDefinition> INamespace.Types =>
    // module.TopLevelTypeDefinitions.Where(td => td.Namespace == FullName)` -- the
    // registered types whose namespace matches this namespace's full name.
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> Types() const override;

    // The C# `IEnumerable<IModule> INamespace.ContributingModules => new[] { module }`.
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules() const override;

    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Namespace`.
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }

    // The C# `ICompilation ICompilationProvider.Compilation => module.Compilation`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // The C# `INamespace INamespace.GetChildNamespace(string name) => null` -- always
    // null, even when types exist in the child namespace (the synthetic module builds
    // no namespace tree; gold-pinned).
    const ILSpy::Decompiler::TypeSystem::INamespace* GetChildNamespace(
        const std::string& name) const override;

    // The C# `ITypeDefinition INamespace.GetTypeDefinition(string name, int
    // typeParameterCount)` -- null for any arity other than 0 (a synthetic type is
    // never generic), else the module's lookup under this namespace.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const std::string& name, int typeParameterCount) const override;

private:
    // Non-owning: the module owns the namespace (the C# GC reference).
    const SyntheticWpfModule* module_;
    const ILSpy::Decompiler::TypeSystem::INamespace* parentNamespace_;
    std::string fullName_;
    std::string name_;
};

// The C# `sealed class SyntheticTypeDefinition : ITypeDefinition` -- a materialized
// well-known WPF type: identity and nullability only, no members, no metadata. Treated
// as a (reference-type) class (the BAML decompiler only reads identity and null-guards
// everything else).
class SyntheticWpfModule::SyntheticTypeDefinition final
    : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    SyntheticTypeDefinition(const SyntheticWpfModule* module,
                            ILSpy::Decompiler::TypeSystem::TopLevelTypeName typeName)
        : module_(module),
          typeName_(std::move(typeName)),
          fullTypeName_(typeName_)
    {
    }

    // The C# `public override string ToString() => $"[SyntheticWpfType
    // {typeName.ReflectionName}]"` -- a non-virtual diagnostic member (the port's
    // ToString convention; the C# ToString override has no IType counterpart).
    std::string ToString() const
    {
        return "[SyntheticWpfType " + typeName_.ReflectionName() + "]";
    }

    // --- IType ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeKind::Class;
    }
    // The single `Name()` override is the final overrider for the IType-vs-INamedElement
    // diamond (the ITypeDefinition redeclarations).
    std::string Name() const override { return typeName_.Name(); }
    std::string ReflectionName() const override { return typeName_.ReflectionName(); }
    int TypeParameterCount() const override { return typeName_.TypeParameterCount(); }
    // The C# `bool? IType.IsReferenceType => true` (materialized types are all classes).
    std::optional<bool> IsReferenceType() const override { return std::optional<bool>(true); }
    // The C# `bool IType.IsByRefLike => false`.
    bool IsByRefLike() const override { return false; }
    // The C# `Nullability IType.Nullability => Nullability.Oblivious` (synthetic types
    // are always nullability-oblivious and BAML never annotates them).
    ILSpy::Decompiler::TypeSystem::Nullability Nullability() const override
    {
        return ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    // The C# `IType IType.ChangeNullability(Nullability nullability) => this` -- the
    // reference identity maps to shared_from_this (the instance is always shared-owned;
    // RegisterType / the attribute reconstruction make it shared).
    ILSpy::Decompiler::TypeSystem::ITypePtr ChangeNullability(
        ILSpy::Decompiler::TypeSystem::Nullability nullability) override
    {
        (void)nullability;
        return shared_from_this();
    }
    // The C# `ITypeDefinition IType.GetDefinition() => this`.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetDefinition() const override
    {
        return this;
    }
    // The C# `IType IType.AcceptVisitor(TypeVisitor visitor) =>
    // visitor.VisitTypeDefinition(this)`.
    ILSpy::Decompiler::TypeSystem::ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override;
    // The C# `IType IType.VisitChildren(TypeVisitor visitor) => this`.
    ILSpy::Decompiler::TypeSystem::ITypePtr VisitChildren(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override
    {
        (void)visitor;
        return shared_from_this();
    }
    // The C# `IEnumerable<IType> IType.DirectBaseTypes => EmptyList` (and
    // TypeParameters / TypeArguments -- the defaults' behavior, the header doc comment).
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> DirectBaseTypes() const override { return {}; }

    // --- ITypeDefinitionOrUnknown ---
    const ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override
    {
        return fullTypeName_;
    }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // --- INamedElement ---
    std::string FullName() const override { return typeName_.ReflectionName(); }
    std::string Namespace() const override { return typeName_.Namespace(); }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // --- IEntity ---
    // The C# `EntityHandle IEntity.MetadataToken => MetadataTokens.TypeDefinitionHandle(0)`
    // -- the nil TypeDef handle as the raw token (the header doc comment).
    std::uint32_t MetadataToken() const override { return 0x02000000; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        (void)attribute;
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        (void)attribute;
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- ITypeDefinition-own ---
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> NestedTypes() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> Members() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> Fields() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> Properties() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> Events() const override
    {
        return {};
    }
    // The C# `KnownTypeCode ITypeDefinition.KnownTypeCode => KnownTypeCode.None`.
    ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override
    {
        return ILSpy::Decompiler::TypeSystem::KnownTypeCode::None;
    }
    // The C# `IType ITypeDefinition.EnumUnderlyingType => SpecialType.UnknownType` -- the
    // null object for "not an enum" (its ReflectionName is "?").
    ILSpy::Decompiler::TypeSystem::ITypePtr EnumUnderlyingType() const override
    {
        return ILSpy::Decompiler::TypeSystem::UnknownType();
    }
    bool IsReadOnly() const override { return false; }
    // The C# `string ITypeDefinition.MetadataName => typeName.Name` (synthetic types are
    // never generic, so the metadata name is the plain short name).
    std::string MetadataName() const override { return typeName_.Name(); }
    bool HasExtensions() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo() const override
    {
        return nullptr;
    }
    // The C# `Nullability ITypeDefinition.NullableContext => Nullability.Oblivious`.
    ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const override
    {
        return ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    bool IsRecord() const override { return false; }

protected:
    // The C# `bool IEquatable<IType>.Equals(IType other) => this == other` -- the
    // AbstractType default reference equality, reached through IType::Equals.
    bool StructuralEquals(const IType& other) const override
    {
        return this == &other;
    }

private:
    // Non-owning: the module owns the type definition (the C# GC reference).
    const SyntheticWpfModule* module_;
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName typeName_;
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
};

} // namespace ILSpy::BamlDecompiler
