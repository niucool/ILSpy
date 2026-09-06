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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataParameter.cs
// (the whole 236-line `sealed class MetadataParameter : IParameter`) -- the
// Param-table-backed parameter the MetadataMethod/MetadataProperty
// `DecodeSignature` walk constructs over the accessor's Param rows (the
// second member-family entity after MetadataField).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly MetadataModule module` + `readonly ParameterHandle
//      handle` + `readonly ParameterAttributes attributes` + `readonly IType
//      Type` + `readonly IParameterizedMember Owner` tuple ports to (const
//      MetadataModule&, the raw `0x08......` token, the raw uint16 flags
//      member, an OWNING `ITypePtr` -- the DefaultParameter convention (d),
//      `Type()` returns `*type_`; a non-owning `const IParameterizedMember*`
//      -- the DefaultParameter convention (g), the owning member owns the
//      parameter, the back-reference is non-owning).
//  (b) The C# lazy `name` ports to `std::optional<std::string>` (a metadata
//      name may be empty, which is distinct from not-loaded -- the
//      MetadataField convention (b)). The `constantValueInSignatureState` /
//      `decimalConstantState` bytes are the C# `ThreeState` (Unknown=0 /
//      False=1 / True=2; the ctor seeds `decimalConstantState` to False
//      unless the row carries the Optional flag -- "only optional parameters
//      can be constants").
//  (c) `GetAttributes` is the loud `std::logic_error` DEFERRAL gated on the
//      AttributeListBuilder + custom-attribute machinery (the MetadataField
//      convention (c): the C# builds it through AttributeListBuilder over the
//      Optional / DefaultParameterValue / In / Out flags, the custom
//      attribute rows, and the marshalling descriptor).
//  (d) The C# `catch (BadImageFormatException) when (!throwOnInvalidMetadata)`
//      arm in GetConstantValue ports to catching the port's
//      BadImageFormatException family (`std::invalid_argument` /
//      `std::out_of_range`, the iteration-63 convention); the inner
//      `ArgumentOutOfRangeException` -> `BadImageFormatException($"Constant
//      with invalid typecode: {constant.TypeCode}")` rethrow maps to
//      `std::invalid_argument` with the interpolated decimal value EXACTLY
//      where the C# rethrows it (inside the outer try, so the outer catch
//      swallows it when !throwOnInvalidMetadata). The DecimalConstantHelper's
//      `ArgumentOutOfRangeException` (a DISTINCT type derived from
//      std::out_of_range) deliberately is NOT one and escapes both arms --
//      the MetadataField convention (g).
//  (e) `MetadataToken` / `ToString` are PLAIN members (the port's
//      IParameter/IEntity surface has no virtual MetadataToken; the
//      MetadataField convention (i)). The `ToString` interpolates
//      `DefaultParameter.ToString(this)` -- the static render helper.
//  (f) `ISymbol.SymbolKind => SymbolKind.Parameter` is a C#-ONLY
//      redeclaration (the port's ISymbol virtual covers it -- a single
//      override is the final overrider).

#pragma once

#include "Decompiler/TypeSystem/IParameter.hpp"

#include <any>
#include <cstdint>
#include <optional>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

// `sealed class MetadataParameter : IParameter` (MetadataParameter.cs lines
// 20-236). The ctor is PUBLIC (the DecodeSignature walk and the tests
// construct it directly; the C# internal ctor).
class MetadataParameter final : public IParameter {
public:
    // The C# `internal MetadataParameter(MetadataModule module,
    // IParameterizedMember owner, IType type, ParameterHandle handle)`:
    // stores the module/owner/type/handle and eagerly reads the row's
    // Attributes, seeding `decimalConstantState` to False unless the row
    // carries the Optional flag ("only optional parameters can be constants").
    MetadataParameter(const MetadataModule& module,
        const IParameterizedMember* owner, ITypePtr type,
        std::uint32_t paramToken);

    // --- IMetadataTokenProvider (the C# `EntityHandle MetadataToken`) ---
    std::uint32_t MetadataToken() const { return handle_; }

    // The C# `public override string ToString() =>
    // $"{MetadataTokens.GetToken(handle):X8} {DefaultParameter.ToString(this)}"`
    // -- a plain member (convention (e)).
    std::string ToString() const;

    // --- IVariable ---
    // The C# `public IType Type { get; }` -- the owning handle (convention
    // (a)); `Type()` returns `*type_` (the reference is stable for the
    // parameter's lifetime).
    const IType& Type() const override { return *type_; }
    // The C# `bool IVariable.IsConst => false`.
    bool IsConst() const override { return false; }
    // The C# `object GetConstantValue(bool throwOnInvalidMetadata)` (the
    // boxes are below the lazy-state helpers).
    std::any GetConstantValue(bool throwOnInvalidMetadata) const override;

    // --- IParameter ---
    // The C# `public IEnumerable<IAttribute> GetAttributes()` -- the loud
    // DEFERRAL gated on the AttributeListBuilder (convention (c)).
    std::vector<const IAttribute*> GetAttributes() const override;
    // The C# `ReferenceKind ReferenceKind => DetectRefKind()` -- the return
    // type GLOBALLY qualified (the inherited `IParameter::ReferenceKind`
    // member name shadows the enum for the rest of the class body, the D372
    // crux -- the DefaultParameter convention (k)).
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind()
        const override;
    // The C# `LifetimeAnnotation Lifetime`.
    LifetimeAnnotation Lifetime() const override;
    // The C# `bool IsParams`.
    bool IsParams() const override;
    // The C# `bool IsOptional => (attributes & Optional) != 0`.
    bool IsOptional() const override;
    // The C# `bool HasConstantValueInSignature`.
    bool HasConstantValueInSignature() const override;
    // The C# `public IParameterizedMember Owner { get; }` -- the nullable
    // non-owning pointer (convention (a)).
    const IParameterizedMember* Owner() const override { return owner_; }

    // --- ISymbol ---
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Parameter` -- the
    // return type GLOBALLY qualified (the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override;
    std::string Name() const override;

private:
    // The C# `ReferenceKind DetectRefKind()` (private) -- the return type
    // GLOBALLY qualified (the `ReferenceKind` accessor name shadows the enum
    // for the rest of the class body, the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind DetectRefKind() const;
    // The C# `bool IsDecimalConstant` (private): the ThreeState-cached
    // DecimalConstantAttribute classification over the row's custom
    // attributes.
    bool IsDecimalConstant() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x08000000-form token
    std::uint32_t attributes_;  // the raw Flags column (the C# ushort enum)
    ITypePtr type_;
    const IParameterizedMember* owner_;

    // The lazy name (convention (b)).
    mutable std::optional<std::string> name_;
    // The C# `byte constantValueInSignatureState` / `byte decimalConstantState`
    // (ThreeState: 0=Unknown / 1=False / 2=True).
    mutable std::uint8_t constantValueInSignatureState_ = 0;
    mutable std::uint8_t decimalConstantState_ = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

} // namespace ILSpy::Decompiler::TypeSystem
