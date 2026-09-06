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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataProperty.cs
// (the whole 325-line `sealed class MetadataProperty : IProperty`) -- the
// Property-table-backed property/indexer definition the
// MetadataModule::GetDefinitionProperty entity cache constructs (the
// fourth member of the member entity family after MetadataField /
// MetadataMethod, together with the MetadataEvent sibling).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly MetadataModule module` + `readonly
//      PropertyDefinitionHandle handle` + the eagerly constructed
//      `readonly IMethod getter` / `readonly IMethod setter` / `readonly
//      string name` / `readonly SymbolKind symbolKind` tuple ports to (const
//      MetadataModule&, the raw `0x17......` token) with the eagerly loaded
//      members (`getter_` / `setter_` / `name_` / `symbolKind_`) initialized
//      in the ctor -- the MetadataField/MetadataMethod precedent. The
//      accessors resolve through the module's method entity cache (a nil
//      accessor stays null).
//  (b) The C# lazy fields (`parameters` / `returnType` computed together by
//      `DecodeSignature()`, `cachedAccessiblity` seeded with the
//      InvalidAccessibility sentinel) port to `mutable` members behind the
//      same get-or-compute reads. `parameters` stores OWNING
//      `std::shared_ptr<const IParameter>` (the member owns its parameters,
//      the `Parameters()` snapshot returns raw pointers -- the
//      IParameterizedMember contract).
//  (c) `GetAttributes` / `HasAttribute` / `GetAttribute` are REAL (the
//      AttributeListBuilder slice): the [IndexerName] synthetic row for a
//      non-"Item" non-explicit indexer, the SpecialName row over the raw
//      PropertyAttributes column, and the custom-attribute row walk at
//      `symbolKind_`. The C# rebuilds the list per call; the port caches it
//      (the documented divergence at the MetadataField convention (c)).
//  (d) `Accessibility` is the ComputeAccessibility walk: the IsOverride
//      single-accessor arm copying the accessibility from the first
//      non-override base member through `InheritanceHelper.GetBaseMembers`
//      (the ProtectedOrInternal cross-assembly reduction to Protected
//      included), else the `AccessibilityExtensions.Union` of the accessor
//      accessibilities; the result is cached behind the (Accessibility)0xff
//      sentinel exactly as the C# `cachedAccessiblity` caches it.
//  (e) `Specialize` is REAL: `SpecializedProperty.Create(this, substitution)`
//      with the no-op-deleter alias over `this` and the keep-alive registry
//      (the landed owning-Specialize design; the Specialize_Test suite pins
//      the arm matrix byte-exact against the real engine).
//  (f) `FullName` / `ReflectionName` interpolate `DeclaringType?.FullName` /
//      `?.ReflectionName` -- the C# null renders as the empty string
//      (convention (h) of MetadataField). `Namespace` is
//      `DeclaringType?.Namespace ?? string.Empty`.
//  (g) `GetHashCode` / `ToString` are PLAIN members (the MetadataField
//      convention (i)); the `module.MetadataFile.GetHashCode()` is the
//      pointer identity hash. `MetadataTokens.GetToken(handle)` renders
//      `%08X`.
//  (h) The `ExplicitlyImplementedInterfaceMembers` /
//      `IsExplicitInterfaceImplementation` pair ports through the shared
//      `GetInterfaceMembersFromAccessor` walk (the AnyAccessor's explicit
//      members projected to their AccessorOwners) -- the static helper both
//      MetadataProperty and MetadataEvent consume.
//  (i) The C# `catch (BadImageFormatException)` arm in DecodeSignature ports
//      to catching the port's BadImageFormatException family
//      (`std::invalid_argument` / `std::out_of_range`) EXACTLY where the C#
//      catches (the MetadataMethod convention (h)).

#pragma once

#include "Decompiler/TypeSystem/IProperty.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

// `sealed class MetadataProperty : IProperty` (MetadataProperty.cs lines
// 23-325). The ctor is PUBLIC (the module's GetDefinition entity cache and
// the tests construct it directly; the C# internal ctor).
class MetadataProperty final : public IProperty {
public:
    // The C# `internal MetadataProperty(MetadataModule module,
    // PropertyDefinitionHandle handle)`: stores the module + handle,
    // resolves the accessor pair through the method entity cache, reads the
    // name, and resolves the symbolKind chain (the DetermineIsIndexer
    // [DefaultMember] comparison, the dotted-name explicit-interface arm
    // over the first EII member's SymbolKind, else Property).
    MetadataProperty(const MetadataModule& module,
                     std::uint32_t propertyToken);

    // --- IMetadataTokenProvider (IEntity) ---
    std::uint32_t MetadataToken() const { return handle_; }

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {DeclaringType?.ReflectionName}.
    // {Name}"` -- a plain member (convention (g)).
    std::string ToString() const;

    // --- ISymbol / INamedElement ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override;
    std::string FullName() const override;
    std::string ReflectionName() const override;
    std::string Namespace() const override;

    // --- IProperty ---
    bool CanGet() const override;
    bool CanSet() const override;
    const IMethod* Getter() const override;
    const IMethod* Setter() const override;
    bool IsIndexer() const override;
    // The `HasKnownAttribute(..., IsReadOnly)` classification over the
    // property's own custom-attribute rows.
    bool ReturnTypeIsRefReadOnly() const override;

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override;

    // --- IVariable (the property's return type) ---
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
    // The shared `GetInterfaceMembersFromAccessor` walk (convention (h)).
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers()
        const override;
    bool IsExplicitInterfaceImplementation() const override;
    bool IsVirtual() const override;
    bool IsOverride() const override;
    bool IsOverridable() const override;
    // `this` (properties are never specialized).
    const IMember* MemberDefinition() const override;
    // `TypeParameterSubstitution.Identity`.
    const TypeParameterSubstitution* Substitution() const override;
    // The C# `public IMember Specialize(TypeParameterSubstitution
    // substitution) => SpecializedProperty.Create(this, substitution)`
    // (MetadataProperty.cs lines 317-319). REAL (convention (e)): every
    // fresh result is kept alive in the registry below (the
    // `MetadataMethod::Specialize` convention).
    const IMember* Specialize(
        const TypeParameterSubstitution* substitution) const override;
    // The C# `bool IMember.Equals(IMember obj, TypeVisitor
    // typeNormalization) => Equals(obj)`: the handle + module-file identity.
    bool Equals(const IMember* obj,
        const TypeVisitor* typeNormalization) const override;

    // The C# `public override bool Equals(object obj)` / `public override int
    // GetHashCode()` -- plain members (convention (g)).
    bool Equals(const MetadataProperty* obj) const;
    int GetHashCode() const;

private:
    // The C# `private bool DetermineIsIndexer(string name)`: the
    // [DefaultMember] comparison against the declaring type's
    // DefaultMemberName, then the Parameters.Count > 0 test.
    bool DetermineIsIndexer(const std::string& name) const;

    // The C# `private void DecodeSignature()`: the property-signature decode
    // (the 0x08-header blob decoded as a method signature over the declaring
    // type's GenericContext) feeding the shared static
    // `MetadataMethod::DecodeSignature` with the accessor's Param rows (the
    // getter's, else the setter's), the accessor-declared [NullableContext],
    // and the property's own custom-attribute rows as the additional
    // return-type attributes; the BadImageFormatException catch arm caches
    // `SpecialType.UnknownType` + the empty parameter list (convention (i)).
    void DecodeSignature() const;

    // The C# `private Accessibility ComputeAccessibility()` (convention
    // (d)).
    ::ILSpy::Decompiler::TypeSystem::Accessibility ComputeAccessibility()
        const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x17...... token
    const IMethod* getter_;  // eagerly resolved (a nil accessor stays null)
    const IMethod* setter_;
    std::string name_;
    ::ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind_;

    // The keep-alive registry for the `Specialize`-created instances (the
    // `MetadataMethod::SpecializedMethods_` precedent). `mutable`
    // (`Specialize` is const).
    mutable std::vector<std::shared_ptr<IMember>> specializedMembers_;

    // The lazy members (convention (b)).
    mutable std::vector<std::shared_ptr<const IParameter>> parameters_;
    mutable ITypePtr returnType_;
    mutable bool signatureDecoded_ = false;
    // The (Accessibility)0xff sentinel: nullopt until computed.
    mutable std::optional<::ILSpy::Decompiler::TypeSystem::Accessibility>
        cachedAccessibility_;

    // The attribute snapshot (convention (c)): the port caches the built
    // list once. The found `GetAttribute` results are kept alive per call
    // (the C# GC root; a fresh instance per call, like the C#).
    mutable std::vector<std::shared_ptr<IAttribute>> attributeList_;
    mutable bool attributeListLoaded_ = false;
    mutable std::vector<std::shared_ptr<IAttribute>> foundAttributes_;
};

// The C# `internal static IEnumerable<IMember>
// GetInterfaceMembersFromAccessor(IMethod method)` -- the shared EII
// projection both MetadataProperty and MetadataEvent consume: the accessor's
// explicitly implemented interface members, each projected to its
// AccessorOwner (a null owner is dropped).
std::vector<const IMember*> GetInterfaceMembersFromAccessor(
    const IMethod* method);

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
} // namespace ILSpy::Decompiler::TypeSystem
