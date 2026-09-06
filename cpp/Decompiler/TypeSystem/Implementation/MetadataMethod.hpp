// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataMethod.cs
// (the whole 655-line `sealed class MetadataMethod : IMethod`) -- the
// MethodDef-table-backed method/constructor/destructor/operator the
// MetadataModule::GetDefinitionMethod entity cache constructs (the third
// member-family entity after MetadataField, and the one that unblocks the
// generic-method signature arms and the BamlDecompilerTypeSystem ctors).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly MetadataModule module` + `readonly
//      MethodDefinitionHandle handle` + `readonly MethodAttributes attributes`
//      tuple ports to (const MetadataModule&, the raw `0x06......` token, the
//      raw uint32 flags member) -- the MetadataField precedent. The eagerly
//      loaded `symbolKind` / `typeParameters` / `accessorOwner` / `AccessorKind`
//      / `IsExtensionMethod` set ports to members initialized in the ctor.
//  (b) The C# lazy fields (`declaringType` / `name` / `parameters` /
//      `returnType` / `isInitOnly` / `returnTypeIsRefReadonly` /
//      `thisIsRefReadonly`) port to `mutable` members behind the same
//      get-or-compute reads (`std::optional<std::string>` for the name; the
//      signature-decoded `parameters`/`returnType` pair computed together by
//      `DecodeSignature()`; the ThreeState bytes for the ref-readonly pair).
//      `parameters` stores OWNING `std::shared_ptr<const IParameter>`
//      (the member owns its parameters, the `Parameters()` snapshot returns
//      raw pointers -- the IParameterizedMember contract).
//  (c) `GetAttributes` / `HasAttribute` / `GetAttribute` /
//      `GetReturnTypeAttributes` remain the loud `std::logic_error`
//      DEFERRALS -- the AttributeListBuilder machinery they compose LANDED
//      (the builder's HasAttribute/GetAttribute row scans, the
//      MakeAttribute/GetAttributeType caches), so the named follow-up slice
//      is the MetadataMethod GetAttributes body itself (the DllImport /
//      PreserveSig / MethodImpl synthetic rows) landing the three members
//      together (the MetadataField convention (c) for what landed).
//  (d) `IsExplicitInterfaceImplementation` / `ExplicitlyImplementedInterfaceMembers`
//      are REAL (the resolve-method slice): the declaring type's
//      `HasOverrides`/`GetOverrides` MethodImpl-table walk over the landed
//      `MetadataModule::ResolveMethod`. A cross-assembly MethodDeclaration
//      pointing at an ACCESSOR (a get_/set_/add_/remove_/raise_ name form)
//      still throws through `MetadataTypeDefinition::GetAccessors` (the loud
//      MetadataProperty/MetadataEvent deferral) until that slice lands.
//  (e) `AccessorOwner` is the loud DEFERRAL gated on the
//      `GetDefinition(PropertyDefinitionHandle/EventDefinitionHandle)` entity
//      caches (the MetadataProperty/MetadataEvent siblings -- the ctor still
//      records the accessorOwner token, so `IsAccessor`/`AccessorKind` are
//      real).
//  (f) `Specialize` is REAL: `SpecializedMethod.Create(this, substitution)`
//      with the no-op-deleter alias over `this` and the keep-alive registry
//      (the landed owning-Specialize design; the Specialize_Test suite pins
//      the arm matrix byte-exact against the real engine).
//  (g) `module.OptionsForEntity(this)` ports to
//      `module.TypeSystemOptions()` directly (the MetadataField convention
//      (e): the OptionsForEntity NRT-visibility filter is deferred with the
//      module's `minAccessibilityForNRT` computation).
//  (h) The C# `catch (BadImageFormatException)` arm in DecodeSignature ports
//      to catching the port's BadImageFormatException family
//      (`std::invalid_argument` / `std::out_of_range`) EXACTLY where the C#
//      catches (the MetadataField convention (g)).
//  (i) `FullName` / `ReflectionName` interpolate `DeclaringType?.FullName` /
//      `?.ReflectionName` -- the C# null renders as the empty string
//      (convention (h) of MetadataField). `Namespace` is
//      `DeclaringType?.Namespace ?? string.Empty`.
//  (j) `GetHashCode` / `ToString` are PLAIN members (the MetadataField
//      convention (i)); the `module.MetadataFile.GetHashCode()` is the
//      pointer identity hash. `MetadataTokens.GetToken(handle)` renders
//      `%08X`.
//  (k) `IMethod.TypeArguments` is the C# `IReadOnlyList<IType>
//      IMethod.TypeArguments => typeParameters` -- the type parameters
//      themselves (a method is never pre-parameterized), snapshotted as the
//      shared `ITypePtr` handles over the owning type-parameter instances.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"

#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
struct ParameterInfo;
template <typename TType>
struct ProviderMethodSignature;
}

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

// `sealed class MetadataMethod : IMethod` (MetadataMethod.cs lines 34-655).
// The ctor is PUBLIC (the module's GetDefinition entity cache and the tests
// construct it directly; the C# internal ctor).
class MetadataMethod final : public IMethod {
public:
    // The C# `internal MetadataMethod(MetadataModule module,
    // MethodDefinitionHandle handle)`: stores the module + handle, eagerly
    // reads the row's Attributes, consults the MethodSemanticsLookup for the
    // accessor pair, creates the type parameters, and resolves the
    // symbolKind chain (Accessor / Constructor / Operator / Destructor /
    // Method, including the static-explicit-interface-operator arm).
    MetadataMethod(const MetadataModule& module, std::uint32_t methodToken);

    // The shared static `DecodeSignature` (MetadataMethod.cs lines 238-313)
    // -- the result struct (the C# value tuple):
    struct DecodedSignature {
        ITypePtr ReturnType;
        std::vector<std::shared_ptr<const IParameter>> Parameters;
        ITypePtr ReturnTypeModifier;  // the raw signature.ReturnType when it
                                      // is a ModifiedType; null otherwise
    };
    // The C# `internal static (IType returnType, IParameter[] parameters,
    // ModifiedType returnTypeModifier) DecodeSignature(MetadataModule module,
    // IParameterizedMember owner, MethodSignature<IType> signature,
    // ParameterHandleCollection? parameterHandles, Nullability nullableContext,
    // TypeSystemOptions typeSystemOptions, CustomAttributeHandleCollection?
    // additionalReturnTypeAttributes = null)` -- the Param-row walk filling
    // the sequence gaps with named DefaultParameters, applying the
    // return-type attributes, and appending the sentinel ArgList parameter
    // for vararg methods. `parameterHandles` is null (nullptr) when the owner
    // has no Param rows to consult (the C# `ParameterHandleCollection?`).
    static DecodedSignature DecodeSignature(
        const MetadataModule& module, const IParameterizedMember* owner,
        const Metadata::ProviderMethodSignature<ITypePtr>& signature,
        const std::vector<Metadata::ParameterInfo>* parameterHandles,
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext,
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions,
        const std::optional<std::vector<std::uint32_t>>&
            additionalReturnTypeAttributes = std::nullopt);

    // The C# `internal static Accessibility GetAccessibility(
    // MethodAttributes attr)` -- the MemberAccessMask switch (the shared
    // helper the MetadataProperty/MetadataEvent siblings reuse).
    static ::ILSpy::Decompiler::TypeSystem::Accessibility GetAccessibility(
        std::uint32_t attr);

    // --- IMetadataTokenProvider (IEntity) ---
    std::uint32_t MetadataToken() const { return handle_; }

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {DeclaringType?.ReflectionName}.
    // {Name}"` -- a plain member (convention (j)).
    std::string ToString() const;

    // --- ISymbol / INamedElement ---
    // The C# `SymbolKind ISymbol.SymbolKind => symbolKind` -- the return type
    // GLOBALLY qualified (the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    // The lazy `metadata.GetString(methodDef.Name)` read.
    std::string Name() const override;
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override;
    std::string FullName() const override;
    std::string ReflectionName() const override;
    std::string Namespace() const override;

    // --- IMethod ---
    // The C# `IReadOnlyList<ITypeParameter> TypeParameters` -- the snapshot
    // over the owning `typeParameters` array (convention (a)).
    std::vector<const ITypeParameter*> TypeParameters() const override;
    // The C# `IReadOnlyList<IType> IMethod.TypeArguments => typeParameters`
    // -- the shared-handle snapshot (convention (k)).
    std::vector<ITypePtr> TypeArguments() const override;
    bool IsConstructor() const override;
    bool IsDestructor() const override;
    bool IsOperator() const override;
    bool IsAccessor() const override;
    // The C# `bool HasBody` -- the SRMExtensions HasBody extension (the
    // Attributes/ImplAttributes/RVA triple test).
    bool HasBody() const override;
    // DEFERRED (convention (e)): the property/event entity caches.
    const IMember* AccessorOwner() const override;
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind()
        const override;
    bool IsExtensionMethod() const override;
    // The C# `bool IMethod.IsLocalFunction => false`.
    bool IsLocalFunction() const override { return false; }
    // The C# `IMethod IMethod.ReducedFrom => null`.
    const IMethod* ReducedFrom() const override { return nullptr; }
    // DEFERRED (convention (c)): the AttributeListBuilder machinery.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override;
    // The ThreeState-cached seq-0-Param-row [IsReadOnly] classification.
    bool ReturnTypeIsRefReadOnly() const override;
    // The C# `bool IsInitOnly`: computed with the signature decode (the
    // `modreq(IsExternalInit)` return-type test).
    bool IsInitOnly() const override;
    // The ThreeState-cached declaring-type-IsReadOnly | [IsReadOnly] test
    // under the ReadOnlyMethods option.
    bool ThisIsRefReadOnly() const override;

    // --- IParameterizedMember ---
    // The C# `IReadOnlyList<IParameter> Parameters` -- the snapshot over the
    // signature-decoded owning array (convention (b)).
    std::vector<const IParameter*> Parameters() const override;

    // --- IMember ---
    // The C# `IType ReturnType` -- the signature-decoded return type (never
    // null: the catch arm caches `SpecialType.UnknownType`).
    const IType& ReturnType() const override;
    // `this` (methods are never reduced/specialized here).
    const IMember* MemberDefinition() const override;
    // DEFERRED (convention (d)): the MethodImpl-table walk.
    std::vector<const IMember*>
        ExplicitlyImplementedInterfaceMembers() const override;
    // DEFERRED (convention (d)): the MethodImpl-table walk.
    bool IsExplicitInterfaceImplementation() const override;
    bool IsVirtual() const override;
    bool IsOverride() const override;
    bool IsOverridable() const override;
    // `TypeParameterSubstitution.Identity`.
    const TypeParameterSubstitution* Substitution() const override;
    // The C# `public IMethod Specialize(TypeParameterSubstitution substitution)
    // => SpecializedMethod.Create(this, substitution)` (+ the `IMember` explicit
    // interface form, the same body). REAL: routes through the landed
    // `SpecializedMethod::Create` factory with the no-op-deleter alias over `this`
    // (the module's `methodDefs_` cache owns this instance); every fresh result is
    // kept alive in the registry below, the Identity / declaring-tpc-0 arms return
    // `this` itself (convention (f) resolved).
    const IMethod* Specialize(
        const TypeParameterSubstitution* substitution) const override;
    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)
    // => Equals(obj)`: the handle + module-file identity.
    bool Equals(const IMember* obj,
        const TypeVisitor* typeNormalization) const override;

    // --- IEntity ---
    // The lazy `module.GetDefinition(def.GetDeclaringType())`.
    const ITypeDefinition* DeclaringTypeDefinition() const override;
    // The C# `IType DeclaringType => DeclaringTypeDefinition` -- the
    // non-owning alias over the module-owned definition (null for a null
    // declaring type).
    ITypePtr DeclaringType() const override;
    const IModule* ParentModule() const override;
    const ICompilation& Compilation() const override;
    // DEFERRED (convention (c)): the AttributeListBuilder machinery.
    std::vector<const IAttribute*> GetAttributes() const override;
    bool HasAttribute(KnownAttribute attribute) const override;
    const IAttribute* GetAttribute(KnownAttribute attribute) const override;
    bool IsStatic() const override;
    bool IsAbstract() const override;
    bool IsSealed() const override;

    // The C# `internal Nullability NullableContext` -- the method's own
    // [NullableContext] ?? the declaring type's (the deferred
    // [NullableContext] decode reads `Oblivious`, the MetadataField
    // convention (f)).
    ::ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const;

    // The C# `public override int GetHashCode()` -- a plain member
    // (convention (j)).
    int GetHashCode() const;
    // The C# `public override bool Equals(object obj)` -- a plain member
    // (the IMember::Equals delegation targets it).
    bool Equals(const MetadataMethod* other) const;

private:
    // The C# `private void DecodeSignature()` (the instance member): the
    // method-signature decode over the module's TypeProvider + the
    // GenericContext of the declaring type's and this method's type
    // parameters, through the shared static, with the
    // `modreq(IsExternalInit)` IsInitOnly test and the BadImageFormatException
    // catch arm (convention (h)).
    // (the C# member is non-const; the port marks it const because every
    // member it writes is `mutable` lazy state -- the C++ artifact over the
    // LazyInit pattern).
    void DecodeSignature() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x06000000-form token
    std::uint32_t attr_;    // the raw MethodAttributes column

    // The keep-alive registry for the `Specialize`-created instances (the C#
    // GC roots them; the returned `const IMethod*` must stay valid while this
    // method is alive -- the `VarArgInstanceMethod` rewrap-registry
    // precedent). `mutable` (`Specialize` is const).
    mutable std::vector<std::shared_ptr<IMethod>> specializedMethods_;

    // The eagerly loaded ctor set (convention (a)).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind_;
    std::vector<std::shared_ptr<const ITypeParameter>> typeParameters_;
    std::uint32_t accessorOwner_;  // the raw Property/Event token (0 for nil)
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes accessorKind_;
    bool isExtensionMethod_;

    // The lazy members (convention (b)).
    mutable std::optional<std::string> name_;
    mutable const ITypeDefinition* declaringType_ = nullptr;
    mutable bool declaringTypeLoaded_ = false;
    mutable std::vector<std::shared_ptr<const IParameter>> parameters_;
    mutable ITypePtr returnType_;
    mutable bool signatureDecoded_ = false;
    mutable bool isInitOnly_ = false;
    mutable std::uint8_t returnTypeIsRefReadonly_ = 0;  // ThreeState
    mutable std::uint8_t thisIsRefReadonly_ = 0;        // ThreeState
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

} // namespace ILSpy::Decompiler::TypeSystem
