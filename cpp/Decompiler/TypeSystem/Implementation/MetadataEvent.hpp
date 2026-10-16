// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files ("the Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataEvent.cs
// (the whole 191-line `sealed class MetadataEvent : IEvent`) -- the
// Event-table-backed event definition the MetadataModule::GetDefinitionEvent
// entity cache constructs (the fifth and last member of the metadata member
// entity family; together with MetadataProperty it lifts the
// MetadataMethod::AccessorOwner deferral and the accessor-search arm of
// ResolveMethodReference).
//
// KEY PORT CONVENTIONS (the MetadataProperty siblings; the class is
// structurally simpler -- events are not IParameterizedMembers, and the
// accessor triple is add/remove/invoke):
//  (a) The C# `readonly MetadataModule module` + `readonly
//      EventDefinitionHandle handle` + `readonly EventAccessors accessors` +
//      `readonly string name` tuple ports to (const MetadataModule&, the raw
//      `0x14......` token) with the eagerly read `name_` member; the
//      accessor triple resolves through the module's method entity cache PER
//      READ (the C# `module.GetDefinition(accessors.Adder)` property access
//      re-resolves every time -- the MetadataModule::GetDefinitionMethod
//      cached entity, so the identity is stable either way; the port stores
//      nothing eagerly).
//  (b) The C# lazy `returnType` ports to a `mutable` member behind the same
//      get-or-compute read (the module.ResolveType walk over the event's
//      EventType column with the declaring type's GenericContext, the
//      declaring type's [NullableContext], and the OptionsForEntity-driven
//      ApplyAttributeTypeVisitor wrap over the event's own custom-attribute
//      rows).
//  (c) `GetAttributes` / `HasAttribute` / `GetAttribute` are REAL (the
//      AttributeListBuilder slice): the SpecialName row over the raw
//      EventAttributes column and the custom-attribute row walk at
//      SymbolKind.Event. The C# rebuilds the list per call; the port caches
//      it (the documented divergence at the MetadataField convention (c)).
//  (d) `Specialize` is REAL: `SpecializedEvent.Create(this, substitution)`
//      with the no-op-deleter alias over `this` and the keep-alive registry
//      (the landed owning-Specialize design).
//  (e) `FullName` / `ReflectionName` interpolate `DeclaringType?.FullName`
//      / `?.ReflectionName` -- the C# null renders as the empty string.
//      `Namespace` is `DeclaringType?.Namespace ?? string.Empty`.
//  (f) `GetHashCode` / `ToString` are PLAIN members (the MetadataField
//      convention (i)).
//  (g) The `ExplicitlyImplementedInterfaceMembers` /
//      `IsExplicitInterfaceImplementation` pair ports through the shared
//      `GetInterfaceMembersFromAccessor` walk (the MetadataProperty sibling).

#pragma once

#include "Decompiler/TypeSystem/IEvent.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

// `sealed class MetadataEvent : IEvent` (MetadataEvent.cs lines 26-191). The
// ctor is PUBLIC (the module's GetDefinition entity cache and the tests
// construct it directly; the C# internal ctor).
class MetadataEvent final : public IEvent {
public:
    // The C# `internal MetadataEvent(MetadataModule module,
    // EventDefinitionHandle handle)`: stores the module + handle and reads
    // the row's name.
    MetadataEvent(const MetadataModule& module, std::uint32_t eventToken);

    // --- IMetadataTokenProvider (IEntity) ---
    std::uint32_t MetadataToken() const { return handle_; }

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {DeclaringType?.ReflectionName}.
    // {Name}"` -- a plain member (convention (f)).
    std::string ToString() const;

    // --- ISymbol / INamedElement ---
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Event`.
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override;
    std::string FullName() const override;
    std::string ReflectionName() const override;
    std::string Namespace() const override;

    // --- IEvent ---
    bool CanAdd() const override;
    bool CanRemove() const override;
    bool CanInvoke() const override;
    const IMethod* AddAccessor() const override;
    const IMethod* RemoveAccessor() const override;
    const IMethod* InvokeAccessor() const override;

    // --- IVariable (the event's delegate type) ---
    const IType& ReturnType() const override;

    // --- IEntity ---
    const ITypeDefinition* DeclaringTypeDefinition() const override;
    ITypePtr DeclaringType() const override;
    const IModule* ParentModule() const override;
    const ICompilation& Compilation() const override;
    std::vector<const IAttribute*> GetAttributes() const override;
    bool HasAttribute(KnownAttribute attribute) const override;
    const IAttribute* GetAttribute(KnownAttribute attribute) const override;

    // --- IMember ---
    bool IsStatic() const override;
    bool IsAbstract() const override;
    bool IsSealed() const override;
    // The shared `GetInterfaceMembersFromAccessor` walk (convention (g)).
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers()
        const override;
    bool IsExplicitInterfaceImplementation() const override;
    bool IsVirtual() const override;
    bool IsOverride() const override;
    bool IsOverridable() const override;
    // `this` (events are never specialized).
    const IMember* MemberDefinition() const override;
    // `TypeParameterSubstitution.Identity`.
    const TypeParameterSubstitution* Substitution() const override;
    // The C# `public IMember Specialize(TypeParameterSubstitution
    // substitution) => SpecializedEvent.Create(this, substitution)`
    // (MetadataEvent.cs lines 183-185). REAL (convention (d)): every fresh
    // result is kept alive in the registry below.
    const IMember* Specialize(
        const TypeParameterSubstitution* substitution) const override;
    // The C# `bool IMember.Equals(IMember obj, TypeVisitor
    // typeNormalization) => Equals(obj)`: the handle + module-file identity.
    bool Equals(const IMember* obj,
        const TypeVisitor* typeNormalization) const override;

    // The C# `public override bool Equals(object obj)` / `public override int
    // GetHashCode()` -- plain members (convention (f)).
    bool Equals(const MetadataEvent* obj) const;
    int GetHashCode() const;

private:
    // The C# `private IMethod AnyAccessor => module.GetDefinition(
    // accessors.GetAny())` -- the EventAccessors.GetAny() selection (adder
    // ?? remover ?? raiser).
    const IMethod* AnyAccessor() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x14...... token
    std::string name_;

    // The keep-alive registry for the `Specialize`-created instances (the
    // `MetadataMethod::SpecializedMethods_` precedent). `mutable`
    // (`Specialize` is const).
    mutable std::vector<std::shared_ptr<IMember>> specializedMembers_;

    // The lazy member (convention (b)).
    mutable ITypePtr returnType_;

    // The attribute snapshot (convention (c)): the port caches the built
    // list once. The found `GetAttribute` results are kept alive per call.
    mutable std::vector<std::shared_ptr<IAttribute>> attributeList_;
    mutable bool attributeListLoaded_ = false;
    mutable std::vector<std::shared_ptr<IAttribute>> foundAttributes_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
} // namespace ILSpy::Decompiler::TypeSystem
