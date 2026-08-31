// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The C# CSharpPrimitiveCast.cs spells the 12-target x 12-source matrix out as ~288
// literal case arms (twice -- the checked and unchecked functions), because C# has no
// generic way to express "the primitive cast (T)(S)v". The port implements the SAME
// semantics factored per conversion class: the integral-to-integral wrap/range check,
// the floating-to-integral truncation, the never-throwing floating targets, and the
// decimal arms (which throw on overflow in BOTH contexts -- the System.Decimal
// op_Explicit operators throw regardless of the caller's checked/unchecked context).
// The C# checked/unchecked function pair folds into the `isChecked` flag threaded
// through the per-class helpers -- the only place the two contexts diverge.
//
// Semantics references: ECMA-334 section 10.3.2 (explicit numeric conversions).
// Notable pinned behaviors:
//   * checked integral-to-integral: OverflowException when the mathematical value
//     leaves the target range; unchecked: the two's-complement wrap (the low bits).
//   * checked floating-to-integral: the operand is rounded toward zero FIRST, then
//     the resulting integral value is range-checked (NaN always throws). The C# spec
//     leaves the unchecked result unspecified; the port truncates toward zero and
//     saturates (documented deviation -- a deterministic choice).
//   * floating targets (Single/Double) never throw in either context: double->float
//     out of range yields +/-Infinity (the checked context does not apply to
//     floating-point narrowing).
//   * decimal: Decimal stand-in (Util/Decimal.hpp) -- the 64-bit stand-in mantissa
//     cannot hold the full 96-bit System.Decimal range (values above 2^63-1 throw
//     OverflowException where a real System.Decimal would succeed; values above the
//     real 7.9e28 max throw in both), and double->decimal packs the rounded integer at
//     scale 0 (a real System.Decimal preserves up to ~16 significant digits at a
//     fractional scale).

#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include "Decompiler/Util/Decimal.hpp"

#include <cmath>
#include <limits>
#include <type_traits>
#include <typeinfo>

namespace ILSpy::Decompiler::Util {

// The C# `Type.GetTypeCode(input.GetType())` over the port's boxed constant-value types
// (the C# boxed object becomes the std::any holding the C++ counterpart of each C#
// primitive). A held type without a primitive counterpart maps to Object -- the C#
// GetTypeCode fallback -- so any such operand falls through to the default
// InvalidCastException arm. Exposed for the CSharpResolver's enum-operator
// constant-folding arm (see the header declaration).
ILSpy::Decompiler::TypeSystem::TypeCode TypeCodeOfBoxedValue(const std::any& value)
{
    using TypeCode = ILSpy::Decompiler::TypeSystem::TypeCode;

    const std::type_info& type = value.type();
    if (type == typeid(bool))
        return TypeCode::Boolean;
    if (type == typeid(char16_t))
        return TypeCode::Char;
    if (type == typeid(std::int8_t))
        return TypeCode::SByte;
    if (type == typeid(std::uint8_t))
        return TypeCode::Byte;
    if (type == typeid(std::int16_t))
        return TypeCode::Int16;
    if (type == typeid(std::uint16_t))
        return TypeCode::UInt16;
    if (type == typeid(std::int32_t))
        return TypeCode::Int32;
    if (type == typeid(std::uint32_t))
        return TypeCode::UInt32;
    if (type == typeid(std::int64_t))
        return TypeCode::Int64;
    if (type == typeid(std::uint64_t))
        return TypeCode::UInt64;
    if (type == typeid(float))
        return TypeCode::Single;
    if (type == typeid(double))
        return TypeCode::Double;
    if (type == typeid(Decimal))
        return TypeCode::Decimal;
    if (type == typeid(std::string))
        return TypeCode::String;
    return TypeCode::Object;
}

namespace {

using TypeCode = ILSpy::Decompiler::TypeSystem::TypeCode;
[[noreturn]] void ThrowInvalidCast(TypeCode sourceType, TypeCode targetType)
{
    // The C# `throw new InvalidCastException("Cast from " + sourceType + " to " +
    // targetType + " not supported.")` -- the message text is a debug aid; the port
    // keeps the type distinct so the catch arms discriminate.
    (void)sourceType;
    (void)targetType;
    throw InvalidCastException();
}

// The integral sources' value split into the raw two's-complement bits (feeding the
// unchecked wrap) and the signedness (feeding the checked range test): every integral
// hold type is unwrapped to this pair so one template serves all nine sources plus
// char16_t.
struct IntegralSource {
    std::uint64_t bits;
    bool isSigned;
};

IntegralSource UnwrapIntegral(TypeCode sourceType, const std::any& input)
{
    switch (sourceType)
    {
        case TypeCode::Char:
            return {static_cast<std::uint64_t>(std::any_cast<char16_t>(input)), false};
        case TypeCode::SByte:
            return {static_cast<std::uint64_t>(std::any_cast<std::int8_t>(input)), true};
        case TypeCode::Byte:
            return {static_cast<std::uint64_t>(std::any_cast<std::uint8_t>(input)), false};
        case TypeCode::Int16:
            return {static_cast<std::uint64_t>(std::any_cast<std::int16_t>(input)), true};
        case TypeCode::UInt16:
            return {static_cast<std::uint64_t>(std::any_cast<std::uint16_t>(input)), false};
        case TypeCode::Int32:
            return {static_cast<std::uint64_t>(std::any_cast<std::int32_t>(input)), true};
        case TypeCode::UInt32:
            return {static_cast<std::uint64_t>(std::any_cast<std::uint32_t>(input)), false};
        case TypeCode::Int64:
            return {static_cast<std::uint64_t>(std::any_cast<std::int64_t>(input)), true};
        case TypeCode::UInt64:
            return {std::any_cast<std::uint64_t>(input), false};
        default:
            // Unreachable: the caller switches on an integral TypeCodeOfBoxedValue result.
            return {0, false};
    }
}

// The C# `(T)(S)input` for every integral S (incl. char) to every integral T (incl.
// char16_t -- the C# char). Checked: OverflowException when the source's mathematical
// value leaves T's range. Unchecked: the two's-complement wrap -- the low sizeof(T)
// bytes of the source's two's-complement representation, routed through T's unsigned
// counterpart so no C++ signed-overflow/implementation-defined narrowing occurs.
template <typename T>
T IntegralToIntegral(IntegralSource source, bool isChecked)
{
    if (isChecked)
    {
        bool inRange;
        if (source.isSigned)
        {
            const std::int64_t value = static_cast<std::int64_t>(source.bits);
            if constexpr (std::is_signed_v<T>)
            {
                inRange = value >= std::numeric_limits<T>::min()
                          && value <= std::numeric_limits<T>::max();
            }
            else
            {
                inRange = value >= 0
                          && static_cast<std::uint64_t>(value) <= std::numeric_limits<T>::max();
            }
        }
        else
        {
            if constexpr (std::is_signed_v<T>)
            {
                inRange = source.bits
                          <= static_cast<std::uint64_t>(std::numeric_limits<T>::max());
            }
            else
            {
                inRange = source.bits <= std::numeric_limits<T>::max();
            }
        }
        if (!inRange)
            throw OverflowException();
    }
    using UnsignedT = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<UnsignedT>(source.bits));
}

// The C# `(T)(float/double)input` for integral T. Checked (ECMA-334 10.3.2): the operand
// is rounded toward zero to the nearest integral value, and an OverflowException is
// thrown when that resulting integral value leaves T's range (NaN always throws); the
// range bounds are exact powers of two, so the double comparisons at the boundary are
// exact. Unchecked: the C# result is an unspecified value; the port truncates toward
// zero and saturates at the target bounds (NaN maps to 0) -- a documented deterministic
// reading of the unspecified semantics.
template <typename T>
T FloatingToIntegral(double value, bool isChecked)
{
    if (isChecked)
    {
        if (std::isnan(value))
            throw OverflowException();
        const double truncated = std::trunc(value);
        if constexpr (std::is_signed_v<T>)
        {
            // [min, max] == [-2^(N-1), 2^(N-1) - 1]: both bounds are exact in double.
            const double lower = static_cast<double>(std::numeric_limits<T>::min());
            if (!(truncated >= lower && truncated < -lower))
                throw OverflowException();
        }
        else
        {
            // [0, 2^N - 1]: 2^N is exact in double.
            const double upperExclusive = static_cast<double>(std::numeric_limits<T>::max())
                                          + 1.0;
            if (!(truncated >= 0.0 && truncated < upperExclusive))
                throw OverflowException();
        }
        return static_cast<T>(truncated);
    }
    if (std::isnan(value))
        return static_cast<T>(0);
    double clamped = std::trunc(value);
    if constexpr (std::is_signed_v<T>)
    {
        const double lower = static_cast<double>(std::numeric_limits<T>::min());
        const double upper = -lower;  // 2^(N-1): the exclusive bound; saturate below it
        if (clamped < lower)
            return std::numeric_limits<T>::min();
        if (clamped >= upper)
            return std::numeric_limits<T>::max();
    }
    else
    {
        const double upper = static_cast<double>(std::numeric_limits<T>::max()) + 1.0;
        if (clamped < 0.0)
            return static_cast<T>(0);
        if (clamped >= upper)
            return std::numeric_limits<T>::max();
    }
    return static_cast<T>(clamped);
}

// The stand-in's mathematical magnitude/scale reduction: the mantissa as a non-negative
// magnitude with the authoritative sign flag (the NormalizeDecimal fold -- a negative
// mantissa never occurs in intended use), split so the two decimal converters below can
// combine them.
std::uint64_t DecimalMagnitude(const Decimal& value, bool& isNegative)
{
    isNegative = value.isNegative;
    std::uint64_t magnitude = static_cast<std::uint64_t>(value.mantissa);
    if (value.mantissa < 0)
    {
        magnitude = 0ull - magnitude;
        isNegative = !isNegative;
    }
    return magnitude;
}

// The C# `(T)(decimal)input` for integral T (incl. char16_t): the System.Decimal
// op_Explicit conversion -- the value rounds toward zero, and an OverflowException is
// thrown when the result leaves T's range. The throw fires in BOTH the checked and the
// unchecked context (the System.Decimal operator itself throws, regardless of the
// caller's overflow-checking context) -- the reason this helper takes no isChecked
// flag.
template <typename T>
T DecimalToIntegral(const Decimal& value)
{
    bool negative;
    std::uint64_t magnitude = DecimalMagnitude(value, negative);
    // The value is magnitude / 10^scale rounded toward zero. A stand-in magnitude is
    // always below 2^64, so a scale of 20+ (10^20 > 2^64) truncates to zero.
    const std::uint64_t truncated =
        value.scale >= 20 ? 0 : magnitude / DecimalPow10(value.scale);
    if constexpr (std::is_signed_v<T>)
    {
        // [-2^(N-1), 2^(N-1) - 1]: the negative bound as an exclusive magnitude.
        const std::uint64_t negativeBound = 1ull << (sizeof(T) * 8 - 1);
        if (negative)
        {
            if (truncated > negativeBound)
                throw OverflowException();
            // 0ull - truncated: at the exact -2^(N-1) bound this is the two's-complement
            // wrap (the MSVC narrowing convention).
            return static_cast<T>(0ull - truncated);
        }
        if (truncated > negativeBound - 1ull)
            throw OverflowException();
        return static_cast<T>(truncated);
    }
    else
    {
        if (negative && truncated != 0)
            throw OverflowException();
        if (truncated > std::numeric_limits<T>::max())
            throw OverflowException();
        return static_cast<T>(truncated);
    }
}

// The stand-in's value as a double (the C# `(double)(decimal)input` /
// `(float)(decimal)input` -- System.Decimal's op_Explicit to the floating types never
// throws). Computed as sign * mantissa / 10^scale in double precision (the divisor's own
// double rounding above 10^22 is a documented stand-in fidelity limit; every stand-in
// magnitude at such a scale is below 1.0, so the relative error is a last-ulp effect).
double DecimalToDouble(const Decimal& value)
{
    bool negative;
    const std::uint64_t magnitude = DecimalMagnitude(value, negative);
    double divisor = 1.0;
    for (std::uint8_t i = 0; i < value.scale; i++)
        divisor *= 10.0;
    const double result = static_cast<double>(magnitude) / divisor;
    return negative ? -result : result;
}

// The C# `(decimal)(S)input` for every source S. A real System.Decimal holds every
// integral source exactly (its 96-bit mantissa spans +-7.9e28) and throws
// OverflowException in BOTH contexts when a floating source leaves its range; the
// stand-in's 64-bit mantissa narrows that exact range to +-2^63 (documented stand-in
// fidelity limit: an integral magnitude above 2^63-1 or a floating value at/above 2^63
// throws where the real System.Decimal would succeed), and a floating source packs the
// rounded integer at scale 0 (the real preserves up to ~16 significant digits at a
// fractional scale).
Decimal IntegralOrFloatingToDecimal(TypeCode sourceType, const std::any& input)
{
    Decimal result;
    result.scale = 0;
    switch (sourceType)
    {
        case TypeCode::Char:
            result.mantissa = static_cast<std::int64_t>(std::any_cast<char16_t>(input));
            result.isNegative = false;
            return result;
        case TypeCode::SByte:
        {
            const std::int8_t value = std::any_cast<std::int8_t>(input);
            result.mantissa = value < 0 ? -static_cast<std::int64_t>(value) : value;
            result.isNegative = value < 0;
            return result;
        }
        case TypeCode::Byte:
            result.mantissa = static_cast<std::int64_t>(std::any_cast<std::uint8_t>(input));
            result.isNegative = false;
            return result;
        case TypeCode::Int16:
        {
            const std::int16_t value = std::any_cast<std::int16_t>(input);
            result.mantissa = value < 0 ? -static_cast<std::int64_t>(value) : value;
            result.isNegative = value < 0;
            return result;
        }
        case TypeCode::UInt16:
            result.mantissa = static_cast<std::int64_t>(std::any_cast<std::uint16_t>(input));
            result.isNegative = false;
            return result;
        case TypeCode::Int32:
        {
            const std::int32_t value = std::any_cast<std::int32_t>(input);
            result.mantissa = value < 0 ? -static_cast<std::int64_t>(value) : value;
            result.isNegative = value < 0;
            return result;
        }
        case TypeCode::UInt32:
            result.mantissa = static_cast<std::int64_t>(std::any_cast<std::uint32_t>(input));
            result.isNegative = false;
            return result;
        case TypeCode::Int64:
        {
            const std::int64_t value = std::any_cast<std::int64_t>(input);
            if (value == std::numeric_limits<std::int64_t>::min())
                throw OverflowException();  // the stand-in's magnitude cannot hold 2^63
            // The pack keeps the mantissa non-negative with the separate sign flag (the
            // NormalizeDecimal invariant -- a negative value folds to the flag).
            result.mantissa = value < 0 ? -value : value;
            result.isNegative = value < 0;
            return result;
        }
        case TypeCode::UInt64:
        {
            const std::uint64_t value = std::any_cast<std::uint64_t>(input);
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw OverflowException();  // the stand-in's 64-bit mantissa range limit
            result.mantissa = static_cast<std::int64_t>(value);
            result.isNegative = false;
            return result;
        }
        case TypeCode::Single:
        case TypeCode::Double:
        {
            const double value = sourceType == TypeCode::Single
                                     ? static_cast<double>(std::any_cast<float>(input))
                                     : std::any_cast<double>(input);
            if (std::isnan(value))
                throw OverflowException();
            const double bound = 9223372036854775808.0;  // 2^63, exact in double
            if (!(value > -bound && value < bound))
                throw OverflowException();
            result.mantissa = static_cast<std::int64_t>(std::round(value));
            result.isNegative = result.mantissa < 0;
            if (result.mantissa < 0)
                result.mantissa = -result.mantissa;
            return result;
        }
        default:
            ThrowInvalidCast(sourceType, TypeCode::Decimal);
    }
}

// The target-side dispatch over the C# matrix's 12 targets. The per-target source
// switches are lifted into the four per-conversion-class helpers (the file-header
// correspondence note); the Boolean source arm every target carries is handled here.
std::any PrimitiveCast(TypeCode targetType, const std::any& input, bool isChecked)
{
    const TypeCode sourceType = TypeCodeOfBoxedValue(input);

    // The C# `if (sourceType == targetType) return input;`
    if (sourceType == targetType)
        return input;

    switch (targetType)
    {
        case TypeCode::Char:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<char16_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<char16_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<char16_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<char16_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::SByte:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::int8_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::int8_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::int8_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::int8_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::Byte:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::uint8_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::uint8_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::uint8_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::uint8_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::Int16:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::int16_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::int16_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::int16_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::int16_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::UInt16:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::uint16_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::uint16_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::uint16_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::uint16_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::Int32:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::int32_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::int32_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::int32_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::int32_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::UInt32:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::uint32_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::uint32_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::uint32_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::uint32_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::Int64:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::int64_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::int64_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::int64_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::int64_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::UInt64:
        {
            if (sourceType == TypeCode::Boolean)
                return std::any(static_cast<std::uint64_t>(std::any_cast<bool>(input) ? 1 : 0));
            if (sourceType == TypeCode::Single || sourceType == TypeCode::Double)
                return std::any(FloatingToIntegral<std::uint64_t>(
                    sourceType == TypeCode::Single
                        ? static_cast<double>(std::any_cast<float>(input))
                        : std::any_cast<double>(input),
                    isChecked));
            if (sourceType == TypeCode::Decimal)
                return std::any(DecimalToIntegral<std::uint64_t>(std::any_cast<Decimal>(input)));
            if (sourceType >= TypeCode::Char && sourceType <= TypeCode::UInt64)
                return std::any(IntegralToIntegral<std::uint64_t>(
                    UnwrapIntegral(sourceType, input), isChecked));
            ThrowInvalidCast(sourceType, targetType);
        }
        case TypeCode::Single:
        case TypeCode::Double:
        {
            // The floating targets never throw in either context: the C# checked context
            // does not apply to integral->floating or floating->floating conversions
            // (a double out of float range yields +/-Infinity, not an exception).
            const bool isSingle = targetType == TypeCode::Single;
            switch (sourceType)
            {
                case TypeCode::Boolean:
                {
                    const bool value = std::any_cast<bool>(input);
                    if (isSingle)
                        return std::any(static_cast<float>(value ? 1.0 : 0.0));
                    return std::any(value ? 1.0 : 0.0);
                }
                case TypeCode::Char:
                {
                    const auto value = std::any_cast<char16_t>(input);
                    if (isSingle)
                        return std::any(static_cast<float>(value));
                    return std::any(static_cast<double>(value));
                }
                case TypeCode::SByte:
                case TypeCode::Byte:
                case TypeCode::Int16:
                case TypeCode::UInt16:
                case TypeCode::Int32:
                case TypeCode::UInt32:
                case TypeCode::Int64:
                case TypeCode::UInt64:
                {
                    // Direct per-type conversion -- a single correctly-rounded
                    // integral->floating step (converting through double first would
                    // double-round the int64/uint64 sources).
                    const IntegralSource source = UnwrapIntegral(sourceType, input);
                    if (isSingle)
                        return std::any(source.isSigned
                                            ? static_cast<float>(
                                                  static_cast<std::int64_t>(source.bits))
                                            : static_cast<float>(source.bits));
                    return std::any(source.isSigned
                                        ? static_cast<double>(
                                              static_cast<std::int64_t>(source.bits))
                                        : static_cast<double>(source.bits));
                }
                case TypeCode::Single:
                    return std::any(static_cast<double>(std::any_cast<float>(input)));
                case TypeCode::Double:
                    return std::any(static_cast<float>(std::any_cast<double>(input)));
                case TypeCode::Decimal:
                {
                    const double value = DecimalToDouble(std::any_cast<Decimal>(input));
                    if (isSingle)
                        return std::any(static_cast<float>(value));
                    return std::any(value);
                }
                default:
                    ThrowInvalidCast(sourceType, targetType);
            }
        }
        case TypeCode::Decimal:
            if (sourceType == TypeCode::Boolean)
            {
                Decimal result;
                result.mantissa = std::any_cast<bool>(input) ? 1 : 0;
                result.scale = 0;
                result.isNegative = false;
                return std::any(result);
            }
            return std::any(IntegralOrFloatingToDecimal(sourceType, input));
        default:
            ThrowInvalidCast(sourceType, targetType);
    }
}

} // namespace

// The C# `public static object Cast(TypeCode targetType, object input, bool
// checkForOverflow)` -- the null passthrough then the checked/unchecked pair (folded
// into the isChecked flag the per-conversion-class helpers thread).
std::any Cast(TypeCode targetType, const std::any& input, bool checkForOverflow)
{
    if (!input.has_value())
        return input;
    return PrimitiveCast(targetType, input, checkForOverflow);
}

} // namespace ILSpy::Decompiler::Util
