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

// The MetadataParameter.cpp half of the MetadataParameter port (the header
// carries the conventions).

#include "Decompiler/TypeSystem/Implementation/MetadataParameter.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstring>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The raw ECMA-335 II.22.25 Param-table flag bits the entity consults (the
// System.Reflection.ParameterAttributes enum; the Disassembler layer's
// ReflectionAttributes.hpp copy is not included from the TypeSystem layer --
// the bits are spelled here as constants, the MetadataModule raw-flags
// convention).
constexpr std::uint32_t kParamIn = 0x0001;
constexpr std::uint32_t kParamOut = 0x0002;
constexpr std::uint32_t kParamOptional = 0x0010;

// The SRM `BlobReader.ReadConstant(ConstantTypeCode)` over the constant's
// value blob -- the MetadataField.cpp ReadConstantValue precedent, copied
// next to its third consumer (identical semantics: the 0x12 nonzero-payload
// arm throws the BadImageFormatException family, the unknown codes throw the
// ArgumentOutOfRange the GetConstantValue inner catch wraps).
std::any ReadConstantValue(std::uint8_t typeCode, const std::uint8_t* base,
    std::size_t size)
{
    auto need = [&](std::size_t n) {
        if (n > size) {
            throw std::out_of_range("constant blob read past end");
        }
    };
    switch (typeCode) {
        case 0x02: {  // Boolean
            need(1);
            return std::any{static_cast<bool>(base[0] != 0)};
        }
        case 0x03: {  // Char
            need(2);
            return std::any{static_cast<char16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x04: {  // SByte
            need(1);
            return std::any{static_cast<std::int8_t>(base[0])};
        }
        case 0x05: {  // Byte
            need(1);
            return std::any{static_cast<std::uint8_t>(base[0])};
        }
        case 0x06: {  // Int16
            need(2);
            return std::any{static_cast<std::int16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x07: {  // UInt16
            need(2);
            return std::any{static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x08: {  // Int32
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            return std::any{static_cast<std::int32_t>(v)};
        }
        case 0x09: {  // UInt32
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            return std::any{v};
        }
        case 0x0A: {  // Int64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{static_cast<std::int64_t>(v)};
        }
        case 0x0B: {  // UInt64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{v};
        }
        case 0x0C: {  // Single
            need(4);
            std::uint32_t bits = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return std::any{f};
        }
        case 0x0D: {  // Double
            need(8);
            std::uint64_t bits = 0;
            for (int i = 7; i >= 0; --i)
                bits = (bits << 8) | static_cast<std::uint64_t>(base[i]);
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            return std::any{d};
        }
        case 0x0E: {  // String: the remaining blob as UTF-16.
            std::size_t chars = size / 2;
            std::u16string utf16(chars, u'\0');
            for (std::size_t i = 0; i < chars; ++i) {
                utf16[i] = static_cast<char16_t>(
                    static_cast<std::uint16_t>(base[2 * i])
                    | (static_cast<std::uint16_t>(base[2 * i + 1]) << 8));
            }
            return std::any{Util::Utf16ToUtf8(utf16)};
        }
        case 0x12: {  // NullReference (the ELEMENT_TYPE_CLASS slot)
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            if (v != 0) {
                // The C# `throw new BadImageFormatException(SR.InvalidConstantValue)`
                // (the decompiled SRM ReadConstant) -- caught by the outer catch.
                throw std::invalid_argument("Invalid constant value.");
            }
            return std::any{};  // the C# null
        }
        default:
            // The C# `throw new ArgumentOutOfRangeException("typeCode")` --
            // wrapped by GetConstantValue's inner catch.
            throw std::out_of_range("invalid constant type code");
    }
}

} // namespace

// The ctor (the header conventions (a)/(b)): the eager Attributes read and
// the decimal-constant seed. The C# `metadata.GetParameter(handle)` throws
// BadImageFormatException for a corrupt row read; the port's never-throw
// facade degrades to flags 0 (a documented divergence reachable only through
// crafted metadata -- every real construction walks rows the file already
// parsed).
MetadataParameter::MetadataParameter(const MetadataModule& module,
    const IParameterizedMember* owner, ITypePtr type,
    std::uint32_t paramToken)
    : module_(module),
      handle_(paramToken),
      attributes_(0),
      type_(std::move(type)),
      owner_(owner)
{
    auto param = module.MetadataFile()->GetParameter(paramToken);
    if (param)
        attributes_ = param->Attributes;
    // The C# `if (!IsOptional) decimalConstantState = ThreeState.False;`
    // ("only optional parameters can be constants").
    if ((attributes_ & kParamOptional) == 0)
        decimalConstantState_ = 1;  // ThreeState.False
}

// The C# `public override string ToString() =>
// $"{MetadataTokens.GetToken(handle):X8} {DefaultParameter.ToString(this)}"`.
std::string MetadataParameter::ToString() const
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", handle_);
    return std::string(buf) + " " + DefaultParameter::ToString(*this);
}

// The C# `ReferenceKind ReferenceKind => DetectRefKind()`.
ReferenceKind MetadataParameter::ReferenceKind() const
{
    return DetectRefKind();
}

// The C# `private ReferenceKind DetectRefKind()`:
//  1. a non-byref type is a plain value parameter;
//  2. the Out flag (alone of the In|Out pair) is `out`;
//  3. under the ReadOnlyStructsAndParameters option, a byref parameter
//     carrying [IsReadOnly] is `in`;
//  4. under the RefReadOnlyParameters option, an In-flagged byref carrying
//     [RequiresLocation] is `ref readonly`;
//  5. anything else is `ref`.
ReferenceKind MetadataParameter::DetectRefKind() const
{
    if (type_->Kind() != TypeKind::ByReference)
        return ReferenceKind::None;
    constexpr std::uint32_t inOut = kParamIn | kParamOut;
    if ((attributes_ & inOut) == kParamOut)
        return ReferenceKind::Out;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    if ((module_.TypeSystemOptions()
         & TypeSystemOptions::ReadOnlyStructsAndParameters)
        == TypeSystemOptions::ReadOnlyStructsAndParameters
        && Metadata::HasKnownAttribute(*metadata, handle_,
            KnownAttribute::IsReadOnly))
    {
        return ReferenceKind::In;
    }
    if ((module_.TypeSystemOptions()
         & TypeSystemOptions::RefReadOnlyParameters)
            == TypeSystemOptions::RefReadOnlyParameters
        && (attributes_ & inOut) == kParamIn
        && Metadata::HasKnownAttribute(*metadata, handle_,
            KnownAttribute::RequiresLocation))
    {
        return ReferenceKind::RefReadOnly;
    }
    return ReferenceKind::Ref;
}

// The C# `public LifetimeAnnotation Lifetime`: the default (no scoped
// annotation) unless the ScopedRef option is on and the row carries
// [ScopedRef]; a [ParamCollection] parameter is implicitly scoped.
LifetimeAnnotation MetadataParameter::Lifetime() const
{
    if ((module_.TypeSystemOptions() & TypeSystemOptions::ScopedRef)
        == TypeSystemOptions::None)
    {
        return {};
    }
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    if ((module_.TypeSystemOptions() & TypeSystemOptions::ParamsCollections)
            == TypeSystemOptions::ParamsCollections
        && Metadata::HasKnownAttribute(*metadata, handle_,
            KnownAttribute::ParamCollection))
    {
        // params collections are implicitly scoped
        return {};
    }
    if (Metadata::HasKnownAttribute(*metadata, handle_,
        KnownAttribute::ScopedRef))
    {
        LifetimeAnnotation annotation;
        annotation.ScopedRef(true);
        return annotation;
    }
    return {};
}

// The C# `public bool IsParams`: an array-typed parameter carrying
// [ParamArray], or (under the ParamsCollections option) any parameter
// carrying [ParamCollection].
bool MetadataParameter::IsParams() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    if (type_->Kind() == TypeKind::Array)
    {
        return Metadata::HasKnownAttribute(*metadata, handle_,
            KnownAttribute::ParamArray);
    }
    if ((module_.TypeSystemOptions() & TypeSystemOptions::ParamsCollections)
        == TypeSystemOptions::ParamsCollections)
    {
        return Metadata::HasKnownAttribute(*metadata, handle_,
            KnownAttribute::ParamCollection);
    }
    return false;
}

// The C# `public bool IsOptional => (attributes & Optional) != 0`.
bool MetadataParameter::IsOptional() const
{
    return (attributes_ & kParamOptional) != 0;
}

// The C# `public string Name` -- the lazy per-row read (convention (b)).
std::string MetadataParameter::Name() const
{
    if (name_)
        return *name_;
    auto param = module_.MetadataFile()->GetParameter(handle_);
    name_ = param ? param->Name : std::string();
    return *name_;
}

// The C# `public object GetConstantValue(bool throwOnInvalidMetadata)`:
// the decimal arm decodes the [DecimalConstantAttribute] (WITHOUT the
// AllowsDecimalConstants option gate -- the MetadataField arm's asymmetry);
// otherwise the row's Constant parent read; the inner
// ArgumentOutOfRange->BadImageFormatException wrap and the outer
// `catch (BadImageFormatException) when (!throwOnInvalidMetadata)` swallow.
std::any MetadataParameter::GetConstantValue(
    bool throwOnInvalidMetadata) const
{
    try
    {
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        if (IsDecimalConstant())
        {
            // The C# `return DecimalConstantHelper.GetDecimalConstantValue(
            // module, parameterDef.GetCustomAttributes())` -- the port's
            // parent-token convention (the helper takes any entity token; the
            // C# parameter name `fieldToken` is the FIELD call site's name).
            return Implementation::GetDecimalConstantValue(module_, handle_);
        }
        // The C# `var constantHandle = parameterDef.GetDefaultValue();
        // if (constantHandle.IsNil) return null;` -- the GetConstant
        // parent-token read (nullopt for the nil handle AND the missing-row
        // arms alike).
        auto constant = metadata->GetConstant(handle_);
        if (!constant)
            return {};
        try
        {
            return ReadConstantValue(constant->TypeCode,
                constant->Value.data(), constant->Value.size());
        }
        catch (const std::out_of_range&)
        {
            // The C# `catch (ArgumentOutOfRangeException) { throw new
            // BadImageFormatException($"Constant with invalid typecode:
            // {constant.TypeCode}"); }` -- INSIDE the outer try, so the
            // outer catch swallows it when !throwOnInvalidMetadata.
            throw std::invalid_argument(
                "Constant with invalid typecode: "
                + std::to_string(static_cast<int>(constant->TypeCode)));
        }
    }
    catch (const ArgumentOutOfRangeException&)
    {
        // The C# ArgumentOutOfRangeException (the decimal ctor's scale
        // check) is NOT a BadImageFormatException: it escapes
        // GetConstantValue even with throwOnInvalidMetadata == false (the
        // MetadataField convention (g)).
        throw;
    }
    catch (const std::invalid_argument&)
    {
        if (!throwOnInvalidMetadata)
            return {};
        throw;
    }
    catch (const std::out_of_range&)
    {
        if (!throwOnInvalidMetadata)
            return {};
        throw;
    }
}

// The C# `public bool HasConstantValueInSignature`: a decimal-constant
// parameter reports the AllowsDecimalConstants option; anything else reports
// the row's non-nil DefaultValue.
bool MetadataParameter::HasConstantValueInSignature() const
{
    if (constantValueInSignatureState_ == 0)  // ThreeState.Unknown
    {
        if (IsDecimalConstant())
        {
            constantValueInSignatureState_ =
                AllowsDecimalConstants(module_) ? 2 : 1;
        }
        else
        {
            constantValueInSignatureState_ =
                module_.MetadataFile()->GetConstant(handle_).has_value()
                    ? 2
                    : 1;
        }
    }
    return constantValueInSignatureState_ == 2;
}

// The C# `private bool IsDecimalConstant`: the ThreeState-cached
// [DecimalConstantAttribute] classification over the row's attribute rows.
bool MetadataParameter::IsDecimalConstant() const
{
    if (decimalConstantState_ == 0)  // ThreeState.Unknown
    {
        decimalConstantState_ =
            Implementation::IsDecimalConstant(module_, handle_) ? 2 : 1;
    }
    return decimalConstantState_ == 2;
}

// The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Parameter` (the
// GLOBALLY qualified return type, the D372 crux).
::ILSpy::Decompiler::TypeSystem::SymbolKind MetadataParameter::SymbolKind()
    const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
}

// The C# `public IEnumerable<IAttribute> GetAttributes()` -- the loud
// DEFERRAL gated on the AttributeListBuilder (the header convention (c)).
std::vector<const IAttribute*> MetadataParameter::GetAttributes() const
{
    throw std::logic_error(
        "MetadataParameter::GetAttributes: the AttributeListBuilder is not "
        "yet ported");
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
