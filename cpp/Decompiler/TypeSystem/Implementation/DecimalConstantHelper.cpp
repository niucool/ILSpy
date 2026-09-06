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

// DecimalConstantHelper.cs -- see the header's port notes.

#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `new decimal(...)` ctor's `ArgumentOutOfRangeException.ThrowIfGreaterThan`
// scale check (the probed .NET 10 message, the two-line form; convention (c) of the
// header). `scale` is a byte so only 29..255 reach the throw.
DecimalConstant MakeDecimal(std::uint32_t lo, std::uint32_t mid,
    std::uint32_t hi, bool isNegative, std::uint8_t scale)
{
    if (scale > 28) {
        throw ArgumentOutOfRangeException(
            "scale ('" + std::to_string(static_cast<int>(scale))
            + "') must be less than or equal to '28'. (Parameter 'scale')\r\n"
              "Actual value was "
            + std::to_string(static_cast<int>(scale)) + ".");
    }
    return DecimalConstant{lo, mid, hi, isNegative, scale};
}

// The C# `AllowsDecimalConstants`.
bool AllowsDecimalConstants(const MetadataModule& module)
{
    return (module.TypeSystemOptions()
            & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::DecimalConstants)
        == ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::DecimalConstants;
}

// The C# `IsDecimalConstant` (the HasKnownAttribute classification over the
// entity's attribute rows; the C# takes the collection, the port the parent
// token -- the HasKnownAttribute composition convention).
bool IsDecimalConstant(const MetadataModule& module, std::uint32_t fieldToken)
{
    return Metadata::HasKnownAttribute(*module.MetadataFile(), fieldToken,
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute::DecimalConstant);
}

namespace {

// The C# `decimal? TryDecodeDecimalConstantAttribute(MetadataModule module,
// CustomAttribute attribute)`: the five-fixed-argument decode through the module's
// TypeProvider, then the byte/byte/uint-uint-uint or byte/byte/int-int-int pattern
// match (`FixedArguments[2]` is hi, `[3]` mid, `[4]` low -- the attribute's ctor
// argument order). Null (the empty any) for any other shape; the decode errors
// PROPAGATE (the C# does not catch here).
std::any TryDecodeDecimalConstantAttribute(const MetadataModule& module,
    std::uint32_t attributeToken)
{
    const Metadata::MetadataFile* metadata = module.MetadataFile();
    auto row = metadata->GetCustomAttribute(attributeToken);
    if (!row)
        return {};
    // The C# `attribute.DecodeValue(module.TypeProvider)`. The decoder takes a
    // NON-CONST provider reference (the port's ISignatureTypeProvider contract
    // mirrors the C#'s non-readonly surface); the module accessor returns const&,
    // so the const_cast is the probe-equivalent access (no state is mutated -- the
    // TypeProvider_Test precedent). A nil value blob (unreachable through real
    // producers) feeds the empty reader, whose prolog read throws the
    // BadImageFormatException family.
    Metadata::CustomAttributeDecoder decoder(*metadata,
        const_cast<TypeProvider&>(module.TypeProvider()));
    const std::uint8_t* valueData = nullptr;
    std::size_t valueSize = 0;
    if (row->ValueBlob) {
        valueData = row->ValueBlob->data();
        valueSize = row->ValueBlob->size();
    }
    auto value = decoder.DecodeValue(row->ConstructorToken, valueData, valueSize);
    // The C# `if (attrValue.FixedArguments.Length != 5) return null;`.
    if (value.FixedArguments.size() != 5)
        return {};
    // The anys are MATERIALIZED into named locals before the any_cast address
    // takes (the dangling-call-temporary trap; CustomAttributeTypedArgument::
    // Value() returns std::any BY VALUE).
    std::any scaleAny = value.FixedArguments[0].Value();
    std::any signAny = value.FixedArguments[1].Value();
    std::any hiAny = value.FixedArguments[2].Value();
    std::any midAny = value.FixedArguments[3].Value();
    std::any loAny = value.FixedArguments[4].Value();
    auto* scale = std::any_cast<std::uint8_t>(&scaleAny);
    auto* sign = std::any_cast<std::uint8_t>(&signAny);
    if (scale == nullptr || sign == nullptr)
        return {};
    // The C# first arm: `unchecked { if ([2] is uint hi && [3] is uint mid &&
    // [4] is uint lo) return new decimal((int)lo, (int)mid, (int)hi, sign != 0,
    // scale); }` -- the unchecked int casts are bit-preserving.
    auto* hiU = std::any_cast<std::uint32_t>(&hiAny);
    auto* midU = std::any_cast<std::uint32_t>(&midAny);
    auto* loU = std::any_cast<std::uint32_t>(&loAny);
    if (hiU != nullptr && midU != nullptr && loU != nullptr)
    {
        return std::any{MakeDecimal(*loU, *midU, *hiU, *sign != 0, *scale)};
    }
    // The C# second arm: the int-int-int form.
    auto* hiI = std::any_cast<std::int32_t>(&hiAny);
    auto* midI = std::any_cast<std::int32_t>(&midAny);
    auto* loI = std::any_cast<std::int32_t>(&loAny);
    if (hiI != nullptr && midI != nullptr && loI != nullptr)
    {
        return std::any{MakeDecimal(
            static_cast<std::uint32_t>(*loI),
            static_cast<std::uint32_t>(*midI),
            static_cast<std::uint32_t>(*hiI), *sign != 0, *scale)};
    }
    return {};
}

} // namespace

// The C# `object GetDecimalConstantValue(module, attributeHandles)`: the FIRST
// [DecimalConstantAttribute] row decodes (the C# returns TryDecode's result for
// that row immediately -- a found-but-malformed row yields null without scanning
// further rows).
std::any GetDecimalConstantValue(const MetadataModule& module,
    std::uint32_t fieldToken)
{
    const Metadata::MetadataFile* metadata = module.MetadataFile();
    for (std::uint32_t attributeToken :
        metadata->GetCustomAttributeTokens(fieldToken))
    {
        if (Metadata::IsKnownAttribute(*metadata, attributeToken,
            ::ILSpy::Decompiler::TypeSystem::KnownAttribute::DecimalConstant))
        {
            return TryDecodeDecimalConstantAttribute(module, attributeToken);
        }
    }
    return {};
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
