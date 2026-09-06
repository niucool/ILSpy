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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataField.cs
// (the whole 322-line `sealed class MetadataField : IField`) -- the
// Field-table-backed field definition the MetadataModule::GetDefinition field
// entity cache constructs, the FIRST member of the member entity family (the
// MetadataMethod / MetadataProperty / MetadataEvent siblings follow).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly MetadataModule module` + `readonly
//      FieldDefinitionHandle handle` pair ports to (const MetadataModule&,
//      the raw `0x04......` token) -- the MetadataTypeDefinition precedent. The
//      `readonly FieldAttributes attributes` (eagerly read in the ctor) ports
//      to the raw uint32 flags member.
//  (b) The C# lazy fields (`declaringType` / `name` / `constantValue` / `type` +
//      `isVolatile` / `decimalConstantState`) port to `mutable` members behind
//      the same get-or-compute reads (`std::optional<std::string>` for the
//      name -- a metadata name may be empty, which is distinct from not-loaded;
//      `std::any` for the constant value, where the C# null never caches -- a
//      null-valued constant recomputes per read, exactly as `LazyInit` does).
//      The `decimalConstantState` byte is the C# `ThreeState` (Unknown=0 /
//      False=1 / True=2; `MetadataField.cs` initializes it to `False` in the
//      ctor unless the flags carry both `Static` and `InitOnly`).
//  (c) `GetAttributes` / `HasAttribute` / `GetAttribute` are the loud
//      `std::logic_error` DEFERRALS gated on the AttributeListBuilder +
//      CustomAttribute machinery (the MetadataTypeDefinition / MetadataTypeParameter
//      convention: the C# builds them through `AttributeListBuilder` over the
//      FieldOffset / NotSerialized / SpecialName flags, the marshalling
//      descriptor, and the custom-attribute rows).
//  (d) `Specialize` is REAL: `SpecializedField.Create(this, substitution)`
//      with the no-op-deleter alias over `this` and the keep-alive registry
//      (the landed owning-Specialize design; the Specialize_Test suite pins
//      the arm matrix byte-exact against the real engine).
//  (e) `module.OptionsForEntity(this)` in `DecodeTypeAndVolatileFlag` ports to
//      `module.TypeSystemOptions()` directly: the `OptionsForEntity` /
//      `ShouldDecodeNullableAttributes` NRT-visibility filter is deferred with
//      the module's `minAccessibilityForNRT` computation (its `Accessibility.None`
//      placeholder makes `OptionsForEntity` the identity until that slice
//      lands -- every local fixture carries no NRT-filter attribute, so the
//      deferral is unobservable; the MetadataModule.hpp convention note).
//  (f) `DeclaringTypeDefinition?.NullableContext ?? Nullability.Oblivious`
//      reads the declaring definition's `NullableContext()` accessor (the
//      deferred [NullableContext] decode returns `Oblivious` until its slice
//      lands -- the MetadataTypeDefinition convention (d)); the null declaring
//      type falls to the explicit `Oblivious`.
//  (g) The C# `catch (BadImageFormatException)` arms port to catching the
//      port's BadImageFormatException family (`std::invalid_argument` /
//      `std::out_of_range`, the iteration-63 convention) EXACTLY where the C#
//      catches; every OTHER exception type PROPAGATES (the
//      `DecimalConstantHelper`'s `ArgumentOutOfRangeException` and the
//      `EnumUnderlyingTypeResolveException` -- the C# lets both escape).
//  (h) `FullName` / `ReflectionName` interpolate `DeclaringType?.FullName` --
//      the C# null renders as the empty string, so the port concatenates the
//      empty string for a null declaring type (".name", the faithful
//      interpolation). `Namespace` is `DeclaringType?.Namespace ??
//      string.Empty`.
//  (i) `GetHashCode` / `ToString` are PLAIN members (the port has no
//      object-model virtuals; the MetadataTypeDefinition precedent). The
//      `module.MetadataFile.GetHashCode()` is the C# object identity hash -- the
//      port uses the pointer (the MetadataTypeDefinition::GetHashCode
//      convention).

#pragma once

#include "Decompiler/TypeSystem/IField.hpp"

#include <any>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

// `sealed class MetadataField : IField` (MetadataField.cs lines 25-322). The
// ctor is PUBLIC (the module's GetDefinition entity cache and the tests
// construct it directly; the C# internal ctor).
class MetadataField final : public IField {
public:
    // The C# `internal MetadataField(MetadataModule module,
    // FieldDefinitionHandle handle)`: stores the module + handle, eagerly
    // reads the row's Attributes, and seeds the decimal-constant state
    // (`ThreeState.False` unless the flags carry both Static and InitOnly --
    // the C# `(attributes & (Static | InitOnly)) != (Static | InitOnly)`).
    MetadataField(const MetadataModule& module, std::uint32_t fieldToken);

    // --- IMetadataTokenProvider (IEntity) ---
    std::uint32_t MetadataToken() const;

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {DeclaringType?.ReflectionName}.
    // {Name}"` -- a plain member (convention (i)).
    std::string ToString() const;

    // --- ISymbol / INamedElement ---
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Field` -- the
    // IField C++-ONLY redeclaration (the shared-`ISymbol`-base diamond
    // disambiguation, the IField.hpp convention (a); a single override is
    // the final overrider for both subobjects). The return type is GLOBALLY
    // qualified (the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    // The lazy `metadata.GetString(fieldDef.Name)` read.
    std::string Name() const override;
    // The C# `switch (attributes & FieldAttributes.FieldAccessMask)` over the
    // raw ECMA visibility bits.
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override;
    std::string FullName() const override;
    std::string ReflectionName() const override;
    std::string Namespace() const override;

    // --- IField ---
    bool IsReadOnly() const override;
    bool IsStatic() const override;
    // The lazy `HasKnownAttribute(fieldDef.GetCustomAttributes(), IsReadOnly)`
    // classification.
    bool ReturnTypeIsRefReadOnly() const override;
    // The modreq(IsVolatile) test over the decoded field type (convention (b):
    // computed together with `type`).
    bool IsVolatile() const override;

    // --- IVariable ---
    // The lazy field-signature decode over the module's TypeProvider (the
    // `DecodeTypeAndVolatileFlag` member below); never null (the catch arm
    // caches `SpecialType.UnknownType`).
    const IType& Type() const override;
    // `(attributes & Literal) != 0 || (IsDecimalConstant &&
    // DecimalConstantHelper.AllowsDecimalConstants(module))`.
    bool IsConst() const override;
    // The lazy Constant-table read (the .NET `BlobReader.ReadConstant`
    // semantics), with the decimal-constant arm and the two catch arms
    // (`throwOnInvalidMetadata: false` swallows the BadImageFormatException
    // family into null; `true` propagates).
    std::any GetConstantValue(bool throwOnInvalidMetadata = false) const override;

    // --- IEntity ---
    // The lazy `module.GetDefinition(def.GetDeclaringType())`.
    const ITypeDefinition* DeclaringTypeDefinition() const override;
    // The C# `IType DeclaringType => DeclaringTypeDefinition` -- the non-owning
    // alias over the module-owned definition (null for a null declaring type).
    ITypePtr DeclaringType() const override;
    const IModule* ParentModule() const override;
    const ICompilation& Compilation() const override;
    // DEFERRED (convention (c)): the AttributeListBuilder machinery.
    std::vector<const IAttribute*> GetAttributes() const override;
    bool HasAttribute(KnownAttribute attribute) const override;
    const IAttribute* GetAttribute(KnownAttribute attribute) const override;
    bool IsAbstract() const override;
    bool IsSealed() const override;

    // --- IMember ---
    // `this` (fields are never specialized).
    const IMember* MemberDefinition() const override;
    const IType& ReturnType() const override;
    // `EmptyList<IMember>.Instance` -- fields cannot implement interfaces.
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers()
        const override;
    bool IsExplicitInterfaceImplementation() const override;
    bool IsVirtual() const override;
    bool IsOverride() const override;
    bool IsOverridable() const override;
    // `TypeParameterSubstitution.Identity`.
    const TypeParameterSubstitution* Substitution() const override;
    // The C# `public IMember Specialize(TypeParameterSubstitution substitution) =>
    // SpecializedField.Create(this, substitution)` (MetadataField.cs lines 317-319). REAL:
    // routes through the landed `SpecializedField::Create` factory with the no-op-deleter
    // alias over `this`; every fresh result is kept alive in the registry below (the
    // `MetadataMethod::Specialize` convention).
    const IMember* Specialize(
        const TypeParameterSubstitution* substitution) const override;
    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)
    // => Equals(obj)`: the handle + module-file identity.
    bool Equals(const IMember* obj,
        const TypeVisitor* typeNormalization) const override;

    // The C# `public override int GetHashCode()` -- a plain member
    // (convention (i)).
    int GetHashCode() const;

private:
    // The C# `private bool IsDecimalConstant` (the ThreeState-cached
    // `HasKnownAttribute(..., DecimalConstant)` classification).
    bool IsDecimalConstant() const;
    // The C# `private IType DecodeTypeAndVolatileFlag()`: the field-signature
    // decode (the walker's bare-type read past the 0x06 header byte, over the
    // GenericContext of the declaring type's type parameters), the
    // modreq(IsVolatile) test, and the ApplyAttributeTypeVisitor wrap (the
    // field's own attribute rows, the OptionsForEntity deferral of convention
    // (e), and the declaring type's NullableContext of convention (f)); the
    // BadImageFormatException catch arm caches `SpecialType.UnknownType`
    // (convention (g)).
    const IType& DecodeTypeAndVolatileFlag() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x04...... token
    std::uint32_t attr_;    // the raw FieldAttributes column

    // The keep-alive registry for the `Specialize`-created instances (the C#
    // GC roots them; the returned `const IMember*` must stay valid while this
    // field is alive -- the `MetadataMethod::SpecializedMethods_` precedent).
    // `mutable` (`Specialize` is const).
    mutable std::vector<std::shared_ptr<IField>> specializedFields_;

    // The lazy members (convention (b)).
    mutable std::optional<std::string> name_;
    mutable const ITypeDefinition* declaringType_ = nullptr;
    mutable bool declaringTypeLoaded_ = false;
    mutable std::any constantValue_;
    mutable ITypePtr type_;
    mutable bool isVolatile_ = false;
    // The C# `byte decimalConstantState` (Unknown=0 / False=1 / True=2).
    mutable std::uint8_t decimalConstantState_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
} // namespace ILSpy::Decompiler::TypeSystem
