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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/DecimalConstantHelper.cs
// (the whole 76-line static class) -- the [DecimalConstantAttribute] decode behind
// `MetadataField.IsConst` / `MetadataField.GetConstantValue`: the option gate, the
// row classification, and the five-fixed-argument decode through the module's
// TypeProvider (the `CustomAttributeDecoder` value decode).
//
// KEY PORT CONVENTIONS:
//  (a) The C# takes `MetadataModule` + `CustomAttributeHandleCollection` pairs; the
//      port takes the PARENT token (the established GetCustomAttributeTokens
//      composition convention -- the HasKnownAttribute precedent). The module is
//      still taken by reference (the provider access + the options gate).
//  (b) The C# `decimal? TryDecodeDecimalConstantAttribute` boxes the built decimal
//      into the `object?` GetConstantValue returns; the port's `std::any` holds this
//      header's `DecimalConstant` (the 96-bit magnitude, the sign, and the scale --
//      the semantic core of the .NET `decimal`). `GetBits()` renders the exact
//      .NET `decimal.GetBits()` word pair (the flags word packs the scale and the
//      sign) for the gold comparisons.
//  (c) The `new decimal(lo, mid, hi, isNegative, scale)` ctor's scale check throws
//      .NET's `ArgumentOutOfRangeException` -- `ArgumentOutOfRangeException.ThrowIfGreaterThan`
//      ("scale ('29') must be less than or equal to '28'. (Parameter 'scale')\r\nActual
//      value was 29." probed against the real .NET 10). The port maps it to a
//      DISTINCT exception type DERIVED from `std::out_of_range` (the established
//      ArgumentOutOfRange mapping): `MetadataField.GetConstantValue` must swallow the
//      BadImageFormatException family but PROPAGATE this one (the C# catch is
//      `catch (BadImageFormatException)` only -- the scale-29 shape escapes
//      `GetConstantValue` even with `throwOnInvalidMetadata: false`, gold-pinned over
//      the crafted MfSynth manifest).
//  (d) The decode's argument-type matching follows the C# `is` pattern exactly: the
//      FIRST arm requires `byte, byte, uint, uint, uint` (the args' runtime types as
//      the decoder boxed them -- `std::uint8_t` / `std::uint32_t`), the second
//      `byte, byte, int, int, int` (`std::int32_t`); any other shape (including the
//      argument-count mismatch) decodes to NULL (the empty `std::any`), never a
//      throw. The `any` values are MATERIALIZED into named locals before the
//      `any_cast` address takes (the dangling-call-temporary trap).

#pragma once

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {
class MetadataModule;
}

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The .NET `System.ArgumentOutOfRangeException` the decimal ctor's scale check
// throws (convention (c)): a distinct type so GetConstantValue can propagate it
// while swallowing the BadImageFormatException family (`std::invalid_argument` /
// the plain `std::out_of_range`), even though both map the same .NET exception
// families elsewhere in the port.
class ArgumentOutOfRangeException : public std::out_of_range {
public:
    explicit ArgumentOutOfRangeException(const std::string& message)
        : std::out_of_range(message) {}
};

// The .NET `decimal` value stand-in (convention (b)): the 96-bit magnitude
// [lo, mid, hi], the sign bit, and the scale (0..28). `GetBits()` renders the
// exact `decimal.GetBits()` array the gold probe dumps ([lo, mid, hi, flags]
// with the flags word = (scale << 16) | (isNegative ? 0x80000000 : 0)).
struct DecimalConstant {
    std::uint32_t lo = 0;
    std::uint32_t mid = 0;
    std::uint32_t hi = 0;
    bool isNegative = false;
    std::uint8_t scale = 0;

    std::uint32_t Flags() const
    {
        return (static_cast<std::uint32_t>(scale) << 16)
            | (isNegative ? 0x80000000u : 0u);
    }
};

// The C# `new decimal((int)lo, (int)mid, (int)hi, sign != 0, scale)` -- the ctor's
// ThrowIfGreaterThan scale check (convention (c)) with the probed .NET 10 message
// (the two-line ArgumentOutOfRange form). The lo/mid/hi are the UNCHECKED
// bit-preserving int casts of the uint arguments.
DecimalConstant MakeDecimal(std::uint32_t lo, std::uint32_t mid,
    std::uint32_t hi, bool isNegative, std::uint8_t scale);

// The C# `AllowsDecimalConstants(MetadataModule module)`:
// `(module.TypeSystemOptions & TypeSystemOptions.DecimalConstants) ==
// TypeSystemOptions.DecimalConstants`.
bool AllowsDecimalConstants(const MetadataModule& module);

// The C# `IsDecimalConstant(MetadataModule module,
// CustomAttributeHandleCollection attributeHandles)`:
// `attributeHandles.HasKnownAttribute(module.metadata, KnownAttribute.DecimalConstant)`
// -- the port takes the PARENT token (convention (a)).
bool IsDecimalConstant(const MetadataModule& module, std::uint32_t fieldToken);

// The C# `object GetDecimalConstantValue(MetadataModule module,
// CustomAttributeHandleCollection attributeHandles)`: the FIRST attribute row
// classified as [DecimalConstantAttribute] is decoded through
// `attribute.DecodeValue(module.TypeProvider)`; the result is null (the empty
// `std::any`) unless the five fixed arguments match the byte/byte/uint/uint/uint
// or byte/byte/int/int/int shape (the `DecimalConstantAttribute` argument order is
// (scale, sign, hi, mid, low)). The decoder's BadImageFormatException arms
// (`std::invalid_argument`) PROPAGATE to the caller (GetConstantValue's catch
// handles them); the scale-check ArgumentOutOfRange propagates too (convention
// (c)). The port takes the PARENT token (convention (a)).
std::any GetDecimalConstantValue(const MetadataModule& module,
    std::uint32_t fieldToken);

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
