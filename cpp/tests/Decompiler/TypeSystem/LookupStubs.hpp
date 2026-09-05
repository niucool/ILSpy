// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Shared test stubs for the MemberLookup / TypeSystemExtensions surface (the
// D399 TestCompilationStubs.hpp precedent). The member-lookup accessibility
// tests and the base-type-traversal tests both need a small but COMPLETE type
// universe: a compilation with a FindType(KnownTypeCode) registry, type
// definitions wired into hand-built DirectBaseTypes / DeclaringTypeDefinition
// graphs, plain entities (for the IsAccessible / IsProtectedAccessible
// arguments the C# declares as IEntity), events/methods for IsInvocable, a
// type parameter for the EffectiveBaseClass unwrap, and a friend-aware module
// for the InternalsVisibleTo accessibility arm. Rather than duplicate the ~40
// trivial overrides per test file, this header provides the universe once in
// the `TestSupport` namespace (not the anonymous namespace), the
// TestCompilationStubs.hpp ODR-avoidance precedent.
//
// All wiring is mutable (Set/Add methods) and all instances default to the
// trivial return; a stub is valid for its lifetime without external wiring
// beyond what a test sets.

#pragma once

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"
#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::TestSupport {

// MSVC hides an enum/class type behind a member accessor of the SAME name
// (`SymbolKind SymbolKind()` poisons `SymbolKind` for the rest of the class),
// so every such self-named accessor's return type -- and any member declared
// after it -- uses the qualified `TS::` form (the ITypeDefinition_Test /
// IEntity_Test stub convention). A local alias keeps that noise down.
namespace TS = ILSpy::Decompiler::TypeSystem;

// A friend-aware `IModule` for the InternalsVisibleTo accessibility arm: the
// friend list is a set of assembly names the module grants internals access to
// (`InternalsVisibleTo(module)` consults the callee's own list against the
// argument's AssemblyName, plus the reflexive same-module rule the real
// metadata module implements).
class LookupModule : public IModule {
public:
    explicit LookupModule(const ICompilation& compilation, std::string assemblyName)
        : compilation_(compilation), assemblyName_(std::move(assemblyName)),
          rootNamespace_(compilation) {}

    void AddFriendAssembly(std::string name) { friendAssemblies_.push_back(std::move(name)); }
    // Configurable assembly name (the BamlContext main-module-match fixture
    // renames a module to "mscorlib": the C# ResolveAssembly arm compares the
    // parsed reference Name against `MainModule.AssemblyName`). The default
    // ctor name is unchanged for every existing test (the additive-setter
    // convention).
    void SetAssemblyName(std::string name) { assemblyName_ = std::move(name); }
    // Configurable `MetadataFile` for the ILAmbience `ConvertSymbol` tests (the
    // metadata-driven flag prefixes read `entity.ParentModule.MetadataFile`'s
    // per-row attribute flags; a real mscorlib `MetadataFile` supplies the rows).
    // The default nullptr preserves the prior behavior (the stale-entity
    // fallback), so existing tests that do not call the setter are unaffected
    // (the additive-setter convention); the pointer is non-owning (the caller
    // keeps the `MetadataFile` alive).
    void SetMetadataFile(const ILSpy::Decompiler::Metadata::MetadataFile* f) {
        metadataFile_ = f;
    }
    // Configurable type tables for the compilation-level scans (the TypeSystemExtensions
    // `GetAllTypeDefinitions` / `GetTopLevelTypeDefinitions` SelectMany, and the TypeInference
    // Improved `FindTypesInBounds` refinement's compilation-wide candidate scan). The
    // defaults preserve the original behavior (empty tables -- the additive-setter
    // convention); the stored pointers are non-owning (the caller keeps the
    // `ITypeDefinition` stubs alive).
    void AddTypeDefinition(const ITypeDefinition* d) { typeDefinitions_.push_back(d); }
    void AddTopLevelTypeDefinition(const ITypeDefinition* d)
    {
        topLevelTypeDefinitions_.push_back(d);
    }
    // Configurable `GetTypeDefinition` lookups (the non-synthetic `KnownThings.InitType`
    // arm resolves each well-known row through `IModule.GetTypeDefinition` over the
    // row's `TopLevelTypeName`). The default (an empty table -> always null) preserves
    // the original hardcoded behavior so existing tests are unaffected (the
    // additive-setter convention); the stored pointer is non-owning.
    void SetTypeDefinition(const TopLevelTypeName& name, const ITypeDefinition* d)
    {
        typeMap_.push_back({ name, d });
    }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Module; }
    std::string Name() const override { return assemblyName_; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IModule ---
    const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override
    {
        return metadataFile_;
    }
    bool IsMainModule() const override { return true; }
    std::string AssemblyName() const override { return assemblyName_; }
    Version AssemblyVersion() const override { return {}; }
    std::string FullAssemblyName() const override { return assemblyName_; }
    std::vector<const IAttribute*> GetAssemblyAttributes() const override { return {}; }
    std::vector<const IAttribute*> GetModuleAttributes() const override { return {}; }
    bool InternalsVisibleTo(const IModule& module) const override
    {
        if (&module == this)
            return true;
        return std::find(friendAssemblies_.begin(), friendAssemblies_.end(), module.AssemblyName())
            != friendAssemblies_.end();
    }
    const INamespace& RootNamespace() const override { return rootNamespace_; }
    const ITypeDefinition* GetTypeDefinition(const TopLevelTypeName& name) const override
    {
        for (const auto& entry : typeMap_)
            if (entry.first == name)
                return entry.second;
        return nullptr;
    }
    std::vector<const ITypeDefinition*> TopLevelTypeDefinitions() const override
    {
        return topLevelTypeDefinitions_;
    }
    std::vector<const ITypeDefinition*> TypeDefinitions() const override
    {
        return typeDefinitions_;
    }

private:
    const ICompilation& compilation_;
    std::string assemblyName_;
    std::vector<std::string> friendAssemblies_;
    const ILSpy::Decompiler::Metadata::MetadataFile* metadataFile_ = nullptr;
    std::vector<const ITypeDefinition*> typeDefinitions_;
    std::vector<const ITypeDefinition*> topLevelTypeDefinitions_;
    std::vector<std::pair<TopLevelTypeName, const ITypeDefinition*>> typeMap_;
    TestNamespace rootNamespace_;
};

// A compililation whose `FindType(KnownTypeCode)` consults a known-type
// registry (RegisterKnownType stubs the framework type the
// `IsDerivedFrom(type, KnownTypeCode)` overload looks up through
// `Compilation.FindType(...)`); unregistered codes fall back to a shared
// unknown-type (the C# would return a real unknown type; the tests only ever
// exercise registered codes).
class LookupCompilation : public ICompilation {
public:
    LookupCompilation() : mainModule_(*this, "LookupTests") {}

    void RegisterKnownType(KnownTypeCode code, const IType* type) { knownTypes_[code] = type; }
    // Forwarding registrations into the MAIN module's type tables (the multi-module
    // `Modules()` list keeps the main module first; these configure the tables the
    // TypeSystemExtensions `GetAllTypeDefinitions` / `GetTopLevelTypeDefinitions`
    // SelectMany reads). Defaults preserve the original behavior (empty tables).
    void AddTypeDefinition(const ITypeDefinition* d) { mainModule_.AddTypeDefinition(d); }
    void AddTopLevelTypeDefinition(const ITypeDefinition* d)
    {
        mainModule_.AddTopLevelTypeDefinition(d);
    }
    // An extra (referenced) module appended AFTER the main module (the C#
    // `IReadOnlyList<IModule> Modules` lists the main module first). The caller keeps
    // the module alive (the type system owns the entities -- the compilation stores
    // non-owning pointers).
    void AddModule(const IModule* module) { extraModules_.push_back(module); }
    // Renames the MAIN module (the BamlContext main-module-match fixture:
    // a main module named "mscorlib" standing in for the probe's real
    // mscorlib PEFile main module). Existing tests keep the ctor default
    // (the additive-setter convention).
    void SetMainModuleAssemblyName(std::string name)
    {
        mainModule_.SetAssemblyName(std::move(name));
    }
    // A module appended to `ReferencedModules` (the BamlContext
    // FindMatchingReference fixture: the WindowsBase version trio picking
    // the highest-version / last-of-equal reference). Defaults preserve the
    // original behavior (an empty list) so existing tests are unaffected;
    // the caller keeps the module alive.
    void AddReferencedModule(const IModule* module) { referencedModules_.push_back(module); }

    // --- ICompilation ---
    const IModule& MainModule() const override { return mainModule_; }
    std::vector<const IModule*> Modules() const override
    {
        std::vector<const IModule*> modules{ &mainModule_ };
        modules.insert(modules.end(), extraModules_.begin(), extraModules_.end());
        return modules;
    }
    std::vector<const IModule*> ReferencedModules() const override
    {
        return referencedModules_;
    }
    const INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const IType& FindType(KnownTypeCode code) const override
    {
        auto it = knownTypes_.find(code);
        if (it != knownTypes_.end())
            return *it->second;
        return unknownType_;
    }
    const StringComparer& NameComparer() const override { return StringComparer::Ordinal(); }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    TS::TypeSystemOptions TypeSystemOptions() const override { return TS::TypeSystemOptions::None; }

private:
    LookupModule mainModule_;
    std::vector<const IModule*> extraModules_;
    // The configurable `ReferencedModules` list (AddReferencedModule;
    // empty by default -- the original behavior).
    std::vector<const IModule*> referencedModules_;
    SpecialType unknownType_{ TypeKind::Unknown };
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    std::map<KnownTypeCode, const IType*> knownTypes_;
};

// A fully wired `ITypeDefinition` stub: the DirectBaseTypes graph (for the
// base-type traversal / IsDerivedFrom), the DeclaringTypeDefinition chain
// (for the private/protected outer-class walks), the accessibility +
// ParentModule + Compilation (for the IsAccessible switch), and the name /
// FullTypeName identity accessors. `GetDefinition()` returns `this` (a type
// definition IS its own definition, faithful to the real
// MetadataTypeDefinition) and `DirectBaseTypes()` returns the hand-wired
// shared_ptr list. Instances are shared_ptr-owned (DirectBaseTypes yields
// `ITypePtr` handles, so the stubs participate in the shared ownership the
// IType interface is built on).
class LookupTypeDefinition : public ITypeDefinition {
public:
    LookupTypeDefinition(std::string name,
                         std::string ns,
                         TS::FullTypeName fullTypeName,
                         TypeKind kind,
                         TS::Accessibility accessibility,
                         const ICompilation& compilation,
                         const IModule* parentModule,
                         TS::KnownTypeCode knownTypeCode = TS::KnownTypeCode::None)
        : fullName_(std::move(name)), namespace_(std::move(ns)),
          fullTypeName_(std::move(fullTypeName)), kind_(kind),
          accessibility_(accessibility), compilation_(compilation),
          parentModule_(parentModule), knownTypeCode_(knownTypeCode) {}

    void AddDirectBaseType(ITypePtr base) { directBaseTypes_.push_back(std::move(base)); }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) { declaringTypeDefinition_ = d; }
    // Configurable `MetadataToken` for the ILAmbience `ConvertSymbol` tests (the
    // metadata-driven flag prefixes read the entity's row by token). The default 0
    // preserves the prior hardcoded behavior so existing tests that do not call
    // the setter are unaffected (the additive-setter convention).
    void SetMetadataToken(std::uint32_t t) { metadataToken_ = t; }
    void SetStatic(bool v) { isStatic_ = v; }
    // Configurable `Methods` / `HasExtensions` for the extension-method scan tests
    // (the resolver's `GetExtensionMethods(lookup, ns)` namespace scan reads both plus
    // `IsStatic` and `TypeParameters`). The defaults preserve the original hardcoded
    // behavior (empty `Methods`, `HasExtensions` false), so existing tests that do not
    // call the setters are unaffected (the additive-setter convention). The stored
    // `Methods` pointers are non-owning (the caller keeps the `IMethod` stubs alive).
    void SetMethods(std::vector<const IMethod*> methods) { methods_ = std::move(methods); }
    void SetHasExtensions(bool v) { hasExtensions_ = v; }
    // Configurable `Accessibility` for the extension-method scan tests (the namespace
    // scan's `lookup.IsAccessible(c, false)` host filter). The default (the ctor's
    // accessibility) preserves the original behavior so existing tests that do not call
    // the setter are unaffected (the additive-setter convention, the LookupMethod
    // `SetAccessibility` precedent).
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    // The variance-conversion tests need a definition that declares its own type parameters with a
    // configurable `Variance`; the default `IType::TypeParameters()` returns `{}`. The stored
    // pointers are non-owning (the caller keeps the `ITypeParameter` stubs alive).
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
    // The GetEnumUnderlyingType tests need a definition with a configurable
    // `EnumUnderlyingType`; the default (an empty `ITypePtr`) preserves the
    // prior always-null behavior.
    void SetEnumUnderlyingType(ITypePtr underlying) { enumUnderlyingType_ = std::move(underlying); }
    // Configurable `IsAbstract` / `IsSealed` / `IsReadOnly` / `IsRecord` for the
    // TypeSystemAstBuilder type-definition renderer tests (the `static`/
    // `abstract`/`sealed` modifier chain, the struct/enum/interface bit clears,
    // and the record-class/record-struct shapes). The defaults (all false)
    // preserve the original hardcoded behavior so existing tests that do not
    // call the setters are unaffected (the additive-setter convention).
    void SetIsAbstract(bool v) { isAbstract_ = v; }
    void SetIsSealed(bool v) { isSealed_ = v; }
    void SetIsReadOnly(bool v) { isReadOnly_ = v; }
    void SetIsRecord(bool v) { isRecord_ = v; }
    // Configurable `IsByRefLike` (the `IType` virtual with the false default)
    // for the `readonly ref struct` modifier pair the struct kind renders.
    void SetIsByRefLike(bool v) { isByRefLike_ = v; }
    // Configurable `GetAttributes` for the attribute-section tests (the
    // delegate/typedef renderers wrap each attribute in its own section). The
    // default (empty) preserves the prior always-empty behavior; the stored
    // pointers are non-owning (the caller keeps the `IAttribute` stubs alive).
    void SetAttributes(std::vector<const IAttribute*> attributes) {
        attributes_ = std::move(attributes);
    }
    // Configurable `Properties` for the `KnownThings.KnownMember` property lookup
    // (the ctor's `GetProperties(p => p.Name == name, IgnoreInheritedMembers)` walks the
    // stub's own list, and the no-flag arm collects the DirectBaseTypes walk like the
    // real `GetMembersHelper`). The default (empty) preserves the prior always-empty
    // behavior so existing tests are unaffected (the additive-setter convention); the
    // stored pointers are non-owning (the caller keeps the `LookupProperty` stubs
    // alive).
    void SetProperties(std::vector<const IProperty*> properties) {
        properties_ = std::move(properties);
    }
    // Configurable `Fields` / `Events` for the `XamlProperty.TryResolve` field
    // and event arms (the `<name>Property` / `<name>Event` static-field lookups
    // and the event lookup walk the stub's own lists through the same filtered
    // `GetMembersHelper` shape as `GetProperties`). The defaults (empty)
    // preserve the prior always-empty behavior so existing tests are
    // unaffected (the additive-setter convention); the stored pointers are
    // non-owning (the caller keeps the `LookupField` / `LookupEvent` stubs
    // alive).
    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }
    void SetEvents(std::vector<const IEvent*> events) { events_ = std::move(events); }

    // --- IType ---
    TypeKind Kind() const override { return kind_; }
    // The single `Name()` override is the final overrider for the
    // IType-vs-INamedElement diamond (the ITypeDefinition redeclarations).
    std::string Name() const override { return fullTypeName_.Name(); }
    std::string ReflectionName() const override { return fullTypeName_.ReflectionName(); }
    int TypeParameterCount() const override { return fullTypeName_.TypeParameterCount(); }
    const ITypeDefinition* GetDefinition() const override { return this; }
    std::vector<ITypePtr> DirectBaseTypes() const override { return directBaseTypes_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
    // The `IType::GetProperties` member-enumeration virtual (the `AbstractType` empty
    // default is replaced by the stub's own filtered list + the `GetMembersHelper`
    // base-type walk when `IgnoreInheritedMembers` is absent).
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        std::vector<const IProperty*> result;
        for (const IProperty* p : properties_)
            if (!filter || filter(p))
                result.push_back(p);
        if ((options & GetMemberOptions::IgnoreInheritedMembers) == GetMemberOptions::None)
        {
            // The real `GetMembersHelper.GetProperties` walks the non-interface base
            // types, each with the declared-members flags added (so each base returns
            // only its own properties and the walk terminates).
            const GetMemberOptions declared = options | GetMemberOptions::IgnoreInheritedMembers
                | GetMemberOptions::ReturnMemberDefinitions;
            for (const ITypePtr& base : directBaseTypes_)
                for (const IProperty* p : base->GetProperties(filter, declared))
                    result.push_back(p);
        }
        return result;
    }

    // The `IType::GetFields` member-enumeration virtual over the stub's own
    // filtered list + the `GetMembersHelper` base-type walk when
    // `IgnoreInheritedMembers` is absent (the `GetProperties` override shape).
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        std::vector<const IField*> result;
        for (const IField* f : fields_)
            if (!filter || filter(f))
                result.push_back(f);
        if ((options & GetMemberOptions::IgnoreInheritedMembers) == GetMemberOptions::None)
        {
            const GetMemberOptions declared = options | GetMemberOptions::IgnoreInheritedMembers
                | GetMemberOptions::ReturnMemberDefinitions;
            for (const ITypePtr& base : directBaseTypes_)
                for (const IField* f : base->GetFields(filter, declared))
                    result.push_back(f);
        }
        return result;
    }

    // The `IType::GetEvents` member-enumeration virtual over the stub's own
    // filtered list + the `GetMembersHelper` base-type walk (the `GetProperties`
    // override shape).
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        std::vector<const IEvent*> result;
        for (const IEvent* e : events_)
            if (!filter || filter(e))
                result.push_back(e);
        if ((options & GetMemberOptions::IgnoreInheritedMembers) == GetMemberOptions::None)
        {
            const GetMemberOptions declared = options | GetMemberOptions::IgnoreInheritedMembers
                | GetMemberOptions::ReturnMemberDefinitions;
            for (const ITypePtr& base : directBaseTypes_)
                for (const IEvent* e : base->GetEvents(filter, declared))
                    result.push_back(e);
        }
        return result;
    }

    // --- ITypeDefinitionOrUnknown ---
    const TS::FullTypeName& FullTypeName() const override { return fullTypeName_; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::TypeDefinition; }

    // --- INamedElement ---
    std::string FullName() const override { return fullName_; }
    std::string Namespace() const override { return namespace_; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return parentModule_; }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }

    // --- ITypeDefinition-own ---
    std::vector<const ITypeDefinition*> NestedTypes() const override { return {}; }
    std::vector<const IMember*> Members() const override { return {}; }
    std::vector<const IField*> Fields() const override { return fields_; }
    std::vector<const IMethod*> Methods() const override { return methods_; }
    std::vector<const IProperty*> Properties() const override { return properties_; }
    std::vector<const IEvent*> Events() const override { return events_; }
    TS::KnownTypeCode KnownTypeCode() const override { return knownTypeCode_; }
    ITypePtr EnumUnderlyingType() const override { return enumUnderlyingType_; }
    bool IsReadOnly() const override { return isReadOnly_; }
    std::string MetadataName() const override { return fullTypeName_.Name(); }
    bool HasExtensions() const override { return hasExtensions_; }
    const TS::ExtensionInfo* ExtensionInfo() const override { return nullptr; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const override { return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious; }
    bool IsRecord() const override { return isRecord_; }
    // The `IType::IsByRefLike` virtual (the false default) made configurable
    // (the `SetIsByRefLike` additive-setter arm).
    bool IsByRefLike() const override { return isByRefLike_; }

protected:
    bool StructuralEquals(const IType& other) const override
    {
        return this == &other; // identity equality for the test stub
    }

private:
    std::string fullName_, namespace_;
    TS::FullTypeName fullTypeName_;
    TypeKind kind_;
    TS::Accessibility accessibility_;
    const ICompilation& compilation_;
    const IModule* parentModule_;
    TS::KnownTypeCode knownTypeCode_;
    std::uint32_t metadataToken_ = 0;
    bool isStatic_ = false;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    std::vector<ITypePtr> directBaseTypes_;
    std::vector<const ITypeParameter*> typeParameters_;
    ITypePtr enumUnderlyingType_;
    std::vector<const IMethod*> methods_;
    std::vector<const IProperty*> properties_;
    std::vector<const IField*> fields_;
    std::vector<const IEvent*> events_;
    bool hasExtensions_ = false;
    bool isAbstract_ = false;
    bool isSealed_ = false;
    bool isReadOnly_ = false;
    bool isRecord_ = false;
    bool isByRefLike_ = false;
    std::vector<const IAttribute*> attributes_;
};

// A plain `IEntity` stub (NOT an IMember) -- the smallest concrete entity for
// the C#-`IEntity`-typed MemberLookup arguments (IsAccessible /
// IsProtectedAccessible) where the tests vary the accessibility, the static
// flag, the declaring type definition, and the parent module.
class LookupEntity : public IEntity {
public:
    LookupEntity(std::string name,
                 TS::SymbolKind kind,
                 TS::Accessibility accessibility,
                 bool isStatic,
                 const ITypeDefinition* declaringTypeDefinition,
                 const IModule* parentModule,
                 const ICompilation& compilation)
        : name_(std::move(name)), kind_(kind), accessibility_(accessibility),
          isStatic_(isStatic), declaringTypeDefinition_(declaringTypeDefinition),
          parentModule_(parentModule), compilation_(compilation) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return kind_; }
    // The single `Name()` override is the final overrider for the
    // ISymbol-vs-INamedElement `Name()` declarations.
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return parentModule_; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

private:
    std::string name_;
    TS::SymbolKind kind_;
    TS::Accessibility accessibility_;
    bool isStatic_;
    const ITypeDefinition* declaringTypeDefinition_;
    const IModule* parentModule_;
    const ICompilation& compilation_;
};

// A minimal `IMember` stub for the IsInvocable return-type arms: a non-event,
// non-method member whose SymbolKind (Field / Property / Accessor) and
// ReturnType (Struct / Dynamic / Delegate / FunctionPointer kinds) the test
// configures. Serves the `member is IEvent || member is IMethod` FALSE side of
// IsInvocable; the TRUE side is covered by LookupEvent / LookupMethod.
class LookupMember : public IMember {
public:
    LookupMember(std::string name,
                 TS::SymbolKind kind,
                 ITypePtr returnType,
                 const ICompilation& compilation)
        : name_(std::move(name)), kind_(kind), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    // Configurable `DeclaringType` for the `XamlProperty.IsAttachedTo` base-chain
    // walk (the resolved member's declaring type drives the attached-property
    // decision). The default (the empty `ITypePtr`, the prior behavior) leaves
    // the NRE arm reachable for tests that do not call the setter (the
    // additive-setter convention).
    void SetDeclaringType(ITypePtr declaringType) { declaringType_ = std::move(declaringType); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

private:
    std::string name_;
    TS::SymbolKind kind_;
    ITypePtr returnType_;
    ITypePtr declaringType_;
    const ICompilation& compilation_;
};

// A minimal `IField` for the `XamlProperty.TryResolve` field arms (the
// `<name>Property` / `<name>Event` static-field lookups) -- the `LookupEvent`
// shape over the `IField` surface (`IVariable::Type` / `IsConst` /
// `GetConstantValue`), with the name the lookup filters on.
class LookupField : public IField {
public:
    LookupField(std::string name, ITypePtr fieldType, const ICompilation& compilation)
        : name_(std::move(name)), fieldType_(std::move(fieldType)),
          compilation_(compilation) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }

    // The `IField::Name` disambiguation override (the shared-`ISymbol`-base
    // diamond -- one override serves both base `Name` slots).
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *fieldType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IVariable ---
    const IType& Type() const override { return *fieldType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }

    // --- IField ---
    bool IsReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
    ITypePtr fieldType_;
    const ICompilation& compilation_;
};

// A minimal `IMethod` for the `member is IMethod` TRUE side of IsInvocable.
// The covariant `const IMethod* Specialize` override covers the inherited
// `IMember::Specialize` slot (the MethodGroupResolveResult_Test precedent).
class LookupMethod : public IMethod {
public:
    LookupMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    // A configurable `DeclaringTypeDefinition` for the `InheritanceHelper` base-member-matching tests
    // (the default null exercises the null-short-circuit path; a real `GetDerivedMember` test sets this
    // so the derived method's `GetBaseMembers` finds the base method).
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) { declaringTypeDefinition_ = d; }
    // Configurable `IsStatic` / `IsOperator` / `ReturnType` / `Parameters` for the user-defined
    // conversion-operator scan (the `GetApplicableConversionOperators` tests). The defaults
    // preserve the original hardcoded behavior (`IsStatic` false, `IsOperator` false, `ReturnType`
    // the `KnownType(Object)` member, `Parameters` empty), so existing tests that do not call the
    // setters are unaffected (the additive-setter convention).
    void SetStatic(bool v) { isStatic_ = v; }
    void SetIsOperator(bool v) { isOperator_ = v; }
    void SetReturnType(ITypePtr rt) { returnTypeOverride_ = std::move(rt); }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    // Configurable `TypeParameters` for the CSharpResolver simple-name-lookup tests (the
    // `LookupSimpleNameOrTypeName` method-type-parameter arm reads `m.TypeParameters`). The
    // default empty preserves the original behavior (the additive-setter convention); the
    // stored pointers are non-owning (the caller keeps the `ITypeParameter` stubs alive).
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
    // Configurable `ReturnTypeIsRefReadOnly` for the delegate-compatibility tests (the
    // `m.ReturnTypeIsRefReadOnly != d.ReturnTypeIsRefReadOnly` mismatch crux). The default
    // `false` preserves the original behavior so existing tests that do not call the setter
    // are unaffected (the additive-setter convention).
    void SetReturnTypeIsRefReadOnly(bool v) { returnTypeIsRefReadOnly_ = v; }
    // Configurable `Accessibility` for the constraint-validation ctor filter
    // (`ValidateConstraints`'s `m.Accessibility == Accessibility.Public` check). The default
    // `Public` preserves the original behavior so existing tests that do not call the setter
    // are unaffected (the additive-setter convention).
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    // Configurable `IsOverridable` for the method-group-conversion `isVirtual` flag tests
    // (`MethodGroupConversion`'s `method.IsOverridable && ...` virtual-lookup conjunction).
    // The default `false` preserves the original behavior so existing tests that do not call
    // the setter are unaffected (the additive-setter convention).
    void SetIsOverridable(bool v) { isOverridable_ = v; }
    // Configurable `IsExtensionMethod` for the extension-method eligibility tests (the
    // resolver's `GetExtensionMethods` scans filter `m.IsExtensionMethod`). The default
    // `false` preserves the original behavior so existing tests that do not call the
    // setter are unaffected (the additive-setter convention).
    void SetIsExtensionMethod(bool v) { isExtensionMethod_ = v; }
    // Configurable `TypeArguments` for the `CanTransformToExtensionMethodCall` tests
    // (the convenience overload's `ignoreTypeArguments: false` arm reads
    // `method.TypeArguments` -- a SPECIALIZED generic method carries the substituted
    // arguments there, while an unspecialized method's list is empty). The default empty
    // preserves the original behavior so existing tests that do not call the setter are
    // unaffected (the additive-setter convention).
    void SetTypeArguments(std::vector<ITypePtr> ta) { typeArguments_ = std::move(ta); }
    // Configurable `ThisIsRefReadOnly` for the readonly-modifier tests
    // (`TypeSystemAstBuilder::ConvertAccessor`'s `HasReadonlyModifier` gate reads
    // it through the TypeSystemExtensions helper). The default `false` preserves
    // the original behavior so existing tests that do not call the setter are
    // unaffected (the additive-setter convention).
    void SetThisIsRefReadOnly(bool v) { thisIsRefReadOnly_ = v; }
    // Configurable `GetReturnTypeAttributes` for the TypeSystemAstBuilder
    // delegate renderer's `[return: ...]` attribute sections. The default
    // (empty) preserves the prior always-empty behavior (the additive-setter
    // convention); the stored pointers are non-owning (the caller keeps the
    // `IAttribute` stubs alive).
    void SetReturnTypeAttributes(std::vector<const IAttribute*> a) {
        returnTypeAttributes_ = std::move(a);
    }
    // Configurable `MetadataToken` / `ParentModule` for the ILAmbience
    // `ConvertSymbol` tests (the metadata-driven `.method` flag prefixes read the
    // method's row by token through its module's MetadataFile). The defaults
    // (0 / nullptr) preserve the prior hardcoded behavior so existing tests that do
    // not call the setters are unaffected (the additive-setter convention); the
    // module pointer is non-owning (the caller keeps the `IModule` alive).
    void SetMetadataToken(std::uint32_t t) { metadataToken_ = t; }
    void SetParentModule(const IModule* m) { parentModule_ = m; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return name_; }

    // Configurable `DeclaringType` (the `IEntity.DeclaringType` IType reference) for the
    // `DefaultAttribute` ctor-overload tests (the constructor-backed attribute derives
    // its `AttributeType` from `constructor.DeclaringType`). The default empty preserves
    // the original always-null behavior so existing tests that do not call the setter
    // are unaffected (the additive-setter convention).
    void SetDeclaringType(ITypePtr d) { declaringType_ = std::move(d); }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return declaringTypeDefinition_; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return parentModule_; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    // The override (set via `SetReturnType`) wins; otherwise the default `KnownType(Object)`
    // member (the original behavior). An if/else (NOT a ternary) avoids slicing the `KnownType`
    // member to a temporary `IType` (the two branches have different types `IType&` vs
    // `KnownType&`, so a ternary would form a temporary `IType` by conversion).
    const IType& ReturnType() const override
    {
        if (returnTypeOverride_)
            return *returnTypeOverride_;
        return returnType_;
    }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return isOverridable_; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IMethod ---
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return returnTypeAttributes_;
    }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
    std::vector<ITypePtr> TypeArguments() const override { return typeArguments_; }
    bool IsExtensionMethod() const override { return isExtensionMethod_; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return isOperator_; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return false; }
    const IMember* AccessorOwner() const override { return nullptr; }
    MethodSemanticsAttributes AccessorKind() const override
    {
        return MethodSemanticsAttributes::None;
    }
    const IMethod* ReducedFrom() const override { return nullptr; }
    const IMethod* Specialize(const TypeParameterSubstitution*) const override { return this; }

private:
    std::string name_;
    const ICompilation& compilation_;
    std::uint32_t metadataToken_ = 0;
    const IModule* parentModule_ = nullptr;
    KnownType returnType_{ KnownTypeCode::Object };
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    ITypePtr declaringType_;
    bool isStatic_ = false;
    bool isOperator_ = false;
    bool returnTypeIsRefReadOnly_ = false;
    bool isOverridable_ = false;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    ITypePtr returnTypeOverride_;
    std::vector<const IParameter*> parameters_;
    std::vector<const ITypeParameter*> typeParameters_;
    std::vector<ITypePtr> typeArguments_;
    bool thisIsRefReadOnly_ = false;
    bool isExtensionMethod_ = false;
    std::vector<const IAttribute*> returnTypeAttributes_;
};

// A minimal `IEvent` for the `member is IEvent` TRUE side of IsInvocable.
class LookupEvent : public IEvent {
public:
    LookupEvent(std::string name, ITypePtr handlerType, const ICompilation& compilation)
        : name_(std::move(name)), handlerType_(std::move(handlerType)),
          compilation_(compilation) {}

    // Configurable `MetadataToken` / `ParentModule` for the ILAmbience
    // `ConvertSymbol` tests (the metadata-driven `.event` flag prefixes read the
    // event's row by token through its module's MetadataFile). The defaults
    // (0 / nullptr) preserve the prior hardcoded behavior so existing tests that do
    // not call the setters are unaffected (the additive-setter convention); the
    // module pointer is non-owning (the caller keeps the `IModule` alive).
    void SetMetadataToken(std::uint32_t t) { metadataToken_ = t; }
    void SetParentModule(const IModule* m) { parentModule_ = m; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Event; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return parentModule_; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *handlerType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IEvent ---
    bool CanAdd() const override { return true; }
    bool CanRemove() const override { return true; }
    bool CanInvoke() const override { return false; }
    const IMethod* AddAccessor() const override { return nullptr; }
    const IMethod* RemoveAccessor() const override { return nullptr; }
    const IMethod* InvokeAccessor() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr handlerType_;
    const ICompilation& compilation_;
    std::uint32_t metadataToken_ = 0;
    const IModule* parentModule_ = nullptr;
};

// A minimal `IProperty` for the `KnownThings.KnownMember` property lookup (and
// future member-lookup property tests) -- the `LookupEvent` shape over the
// `IProperty` surface (CanGet/CanSet/Getter/Setter/IsIndexer/
// ReturnTypeIsRefReadOnly), with the name the lookup filters on.
class LookupProperty : public IProperty {
public:
    LookupProperty(std::string name, ITypePtr propertyType, const ICompilation& compilation)
        : name_(std::move(name)), propertyType_(std::move(propertyType)), compilation_(compilation)
    {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *propertyType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return {}; }

    // --- IProperty ---
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
    ITypePtr propertyType_;
    const ICompilation& compilation_;
};

// A minimal `ITypeParameter` for the IsProtectedAccessAllowed type-parameter
// unwrap (EffectiveBaseClass is hand-wired; an un-set effective base class is
// a null promise, which the port treats as not-protected-accessible rather
// than the C#'s would-be NRE).
class LookupTypeParameter : public ITypeParameter {
public:
    explicit LookupTypeParameter(std::string name, VarianceModifier variance = VarianceModifier::Invariant)
        : name_(std::move(name)), variance_(variance) {}

    void SetEffectiveBaseClass(ITypePtr t) { effectiveBaseClass_ = std::move(t); }
    // Configurable index for the TypeInference region tests (the `Detail::OccursInVisitor`
    // indexes the `TP` state vector by the visited type parameter's `Index` -- the real
    // `InferTypeArguments` contract is `typeParameters[i].Index == i`). The default 0
    // preserves the original behavior (the additive-setter convention).
    void SetIndex(int index) { index_ = index; }
    // Configurable constraint flags / direct base types for the constraint-validation tests
    // (`Detail::ValidateConstraints` -- the `where T : class` / `where T : struct` / `where T : new()`
    // / `where T : Base` checks). The defaults preserve the original behavior (all flags false,
    // `DirectBaseTypes()` empty -- the inherited `IType` default), so existing tests that do not
    // call the setters are unaffected (the additive-setter convention).
    void SetHasReferenceTypeConstraint(bool v) { hasReferenceTypeConstraint_ = v; }
    void SetHasValueTypeConstraint(bool v) { hasValueTypeConstraint_ = v; }
    void SetHasDefaultConstructorConstraint(bool v) { hasDefaultConstructorConstraint_ = v; }
    void SetDirectBaseTypes(std::vector<ITypePtr> bases) { directBaseTypes_ = std::move(bases); }
    // Configurable unmanaged / allows-ref-struct flags, nullability constraint, type
    // constraints, and declared attributes for the TypeSystemAstBuilder Convert Type
    // Parameter region tests (`ConvertTypeParameterConstraint` reads every flag plus
    // `NullabilityConstraint` / `TypeConstraints`, and `ConvertTypeParameter` reads
    // `GetAttributes` under `ShowAttributes`). The defaults preserve the original
    // behavior (false / Oblivious / empty / empty -- the additive-setter convention).
    void SetHasUnmanagedConstraint(bool v) { hasUnmanagedConstraint_ = v; }
    void SetAllowsRefLikeType(bool v) { allowsRefLikeType_ = v; }
    void SetNullabilityConstraint(::ILSpy::Decompiler::TypeSystem::Nullability v) {
        nullabilityConstraint_ = v;
    }
    void SetTypeConstraints(std::vector<TypeConstraint> constraints) {
        typeConstraints_ = std::move(constraints);
    }
    void SetAttributes(std::vector<const IAttribute*> attributes) {
        attributes_ = std::move(attributes);
    }

    // --- IType ---
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    // The single `Name()` override is the final overrider for IType::Name /
    // ISymbol::Name / ITypeParameter::Name (the D381 diamond disambiguation).
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::TypeParameter; }

    // --- ITypeParameter ---
    TS::SymbolKind OwnerType() const override { return TS::SymbolKind::Method; }
    const IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    VarianceModifier Variance() const override { return variance_; }
    ITypePtr EffectiveBaseClass() const override { return effectiveBaseClass_; }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return hasDefaultConstructorConstraint_; }
    bool HasReferenceTypeConstraint() const override { return hasReferenceTypeConstraint_; }
    bool HasValueTypeConstraint() const override { return hasValueTypeConstraint_; }
    bool HasUnmanagedConstraint() const override { return hasUnmanagedConstraint_; }
    bool AllowsRefLikeType() const override { return allowsRefLikeType_; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override { return nullabilityConstraint_; }
    std::vector<TypeConstraint> TypeConstraints() const override { return typeConstraints_; }
    // The declared base-type constraints (`where T : Base`); the `IType` default returns empty,
    // the setter supplies the hand-wired list (the `LookupTypeDefinition::AddDirectBaseType`
    // convention). Returns the hand-wired shared_ptr list so the entries stay alive.
    std::vector<ITypePtr> DirectBaseTypes() const override { return directBaseTypes_; }

protected:
    bool StructuralEquals(const IType& other) const override
    {
        return this == &other; // identity equality for the test stub
    }

private:
    std::string name_;
    VarianceModifier variance_;
    ITypePtr effectiveBaseClass_;
    int index_ = 0;
    bool hasDefaultConstructorConstraint_ = false;
    bool hasReferenceTypeConstraint_ = false;
    bool hasValueTypeConstraint_ = false;
    bool hasUnmanagedConstraint_ = false;
    bool allowsRefLikeType_ = false;
    ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint_ =
        ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    std::vector<TypeConstraint> typeConstraints_;
    std::vector<const IAttribute*> attributes_;
    std::vector<ITypePtr> directBaseTypes_;
};

} // namespace ILSpy::Decompiler::TypeSystem::TestSupport
