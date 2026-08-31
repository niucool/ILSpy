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

// The C# `decimal` language alias (System.Decimal) stand-in -- the Util-level primitive
// the CSharpOperators operator tables and the CSharpPrimitiveCast decimal arms share.
// A real System.Decimal is a 96-bit mantissa + a scale (0..28) + a separate sign bit;
// the stand-in keeps that SHAPE (with the mantissa truncated to its low 64 bits) so the
// unary +/- lambda bodies compile faithfully -- negation is the sign flip, the
// System.Decimal negation semantics. The full arithmetic fidelity arrives with the
// deferred constant-evaluation path (convention (j)): the operator tables consume only
// the TYPE (`TypeCodeFor<Decimal>` -> `TypeCode::Decimal`), never a stored value; the
// CSharpPrimitiveCast decimal arms are the first VALUE consumers, within the range the
// 64-bit stand-in mantissa holds.

#pragma once

#include "Decompiler/Util/CSharpPrimitiveCast.hpp"  // DivideByZeroException (the System-exception home, the iteration-98 placement; no include cycle -- CSharpPrimitiveCast.hpp does not include this header)

#include <cstdint>
#include <stdexcept>

namespace ILSpy::Decompiler::Util {

struct Decimal {
    std::int64_t mantissa = 0;
    std::uint8_t scale = 0;
    bool isNegative = false;
};

// The C# unary `+d` -- the identity (System.Decimal defines unary plus as the identity).
inline Decimal operator+(Decimal value)
{
    return value;
}

// The C# unary `-d` -- the sign flip (System.Decimal negation never touches the
// mantissa/scale, so no signed-overflow edge exists).
inline Decimal operator-(Decimal value)
{
    value.isNegative = !value.isNegative;
    return value;
}

// The binary `decimal` stand-in operators (the * / / / % / + / - bodies the arithmetic
// operator tables store, convention (l)): the System.Decimal SHAPE is a scaled
// magnitude with a separate sign, so the multiplicative operators combine the
// magnitudes/scales/signs and the additive operators align the scales first (the
// smaller-scale operand scaled up by 10^diff -- the System.Decimal addition alignment).
// The 64-bit stand-in magnitude wraps where the real 96-bit one would not (the
// uint64 -> int64 narrowing of a wrapped magnitude is implementation-defined before
// C++20; MSVC defines the two's-complement wrap), and the division scale handling is the
// direct approximation (the real System.Decimal raises the quotient scale to keep the
// precision); the full arithmetic fidelity arrives with the deferred constant-evaluation
// path (convention (j)). Division/remainder by a zero divisor throws -- the C# decimal
// DivideByZeroException in BOTH the checked and unchecked contexts.

// Normalizes a stand-in value to the (non-negative magnitude, authoritative sign flag)
// pair the binary operators combine: the flag is the sign the unary operators flip, and a
// negative mantissa (never produced in intended use -- the default is 0) folds into the
// flag so the magnitude stays non-negative.
inline Decimal NormalizeDecimal(Decimal value)
{
    if (value.mantissa < 0)
    {
        value.mantissa = static_cast<std::int64_t>(
            0ull - static_cast<std::uint64_t>(value.mantissa));
        value.isNegative = !value.isNegative;
    }
    return value;
}

// 10^power (unsigned wrap past 10^19 -- the stand-in's 64-bit magnitude standing in for
// the real 96-bit one).
inline std::uint64_t DecimalPow10(std::uint8_t power)
{
    std::uint64_t result = 1;
    for (std::uint8_t i = 0; i < power; i++)
        result *= 10ull;
    return result;
}

// The C# binary `a * b` (the same body for the checked/unchecked pair -- the stand-in
// wraps where the real System.Decimal would throw OverflowException).
inline Decimal operator*(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    Decimal result;
    result.mantissa = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.mantissa)
                                                * static_cast<std::uint64_t>(b.mantissa));
    result.scale = static_cast<std::uint8_t>(a.scale + b.scale);
    result.isNegative = a.isNegative != b.isNegative;
    return result;
}

// The C# binary `a / b`.
inline Decimal operator/(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    if (b.mantissa == 0)
        throw DivideByZeroException();
    Decimal result;
    result.mantissa = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.mantissa)
                                                / static_cast<std::uint64_t>(b.mantissa));
    // The stand-in approximates the result scale with the operand scale difference
    // (floored at 0); the real System.Decimal raises the scale to keep the quotient's
    // precision (the deferred constant-evaluation fidelity).
    result.scale = a.scale > b.scale ? static_cast<std::uint8_t>(a.scale - b.scale) : 0;
    result.isNegative = a.isNegative != b.isNegative;
    return result;
}

// The C# binary `a % b`: both operands aligned to the larger scale, the remainder taking
// the dividend's sign.
inline Decimal operator%(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    if (b.mantissa == 0)
        throw DivideByZeroException();
    const std::uint8_t scale = a.scale > b.scale ? a.scale : b.scale;
    std::uint64_t ma = static_cast<std::uint64_t>(a.mantissa);
    std::uint64_t mb = static_cast<std::uint64_t>(b.mantissa);
    if (a.scale < scale)
        ma *= DecimalPow10(static_cast<std::uint8_t>(scale - a.scale));
    if (b.scale < scale)
        mb *= DecimalPow10(static_cast<std::uint8_t>(scale - b.scale));
    Decimal result;
    const std::uint64_t remainder = ma % mb;
    result.mantissa = static_cast<std::int64_t>(remainder);
    result.scale = scale;
    result.isNegative = a.isNegative && remainder != 0;
    return result;
}

// The C# binary `a + b`: both magnitudes scaled to the larger scale, then combined by
// sign (the larger magnitude wins a sign mismatch; equal magnitudes cancel to +0).
inline Decimal operator+(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    const std::uint8_t scale = a.scale > b.scale ? a.scale : b.scale;
    std::uint64_t ma = static_cast<std::uint64_t>(a.mantissa);
    std::uint64_t mb = static_cast<std::uint64_t>(b.mantissa);
    if (a.scale < scale)
        ma *= DecimalPow10(static_cast<std::uint8_t>(scale - a.scale));
    if (b.scale < scale)
        mb *= DecimalPow10(static_cast<std::uint8_t>(scale - b.scale));
    Decimal result;
    std::uint64_t sum;
    bool negative;
    if (a.isNegative == b.isNegative)
    {
        sum = ma + mb;  // unsigned wrap
        negative = a.isNegative;
    }
    else
    {
        if (ma >= mb)
        {
            sum = ma - mb;
            negative = a.isNegative;
        }
        else
        {
            sum = mb - ma;
            negative = b.isNegative;
        }
    }
    result.mantissa = static_cast<std::int64_t>(sum);
    result.scale = scale;
    result.isNegative = negative && sum != 0;  // zero is sign-neutral
    return result;
}

// The C# binary `a - b` -- the addition of the negated subtrahend (the unary sign flip;
// a zero subtrahend's flipped sign cancels back out through the sum != 0 guard).
inline Decimal operator-(Decimal a, Decimal b)
{
    b.isNegative = !b.isNegative;
    return a + b;
}

// The C# relational `a < b` / `a <= b` / `a > b` / `a >= b` bodies the comparison operator
// tables store (convention (l)): both magnitudes scaled to the larger scale (the
// addition alignment), then compared by sign and magnitude -- a negative value is less
// than a non-negative one; same-sign values compare by their aligned magnitudes with the
// negative direction inverted.
inline int CompareDecimal(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    if (a.isNegative != b.isNegative)
        return a.isNegative ? -1 : 1;
    const std::uint8_t scale = a.scale > b.scale ? a.scale : b.scale;
    std::uint64_t ma = static_cast<std::uint64_t>(a.mantissa);
    std::uint64_t mb = static_cast<std::uint64_t>(b.mantissa);
    if (a.scale < scale)
        ma *= DecimalPow10(static_cast<std::uint8_t>(scale - a.scale));
    if (b.scale < scale)
        mb *= DecimalPow10(static_cast<std::uint8_t>(scale - b.scale));
    const int result = ma < mb ? -1 : (ma > mb ? 1 : 0);
    return a.isNegative ? -result : result;
}

inline bool operator<(Decimal a, Decimal b) { return CompareDecimal(a, b) < 0; }
inline bool operator<=(Decimal a, Decimal b) { return CompareDecimal(a, b) <= 0; }
inline bool operator>(Decimal a, Decimal b) { return CompareDecimal(a, b) > 0; }
inline bool operator>=(Decimal a, Decimal b) { return CompareDecimal(a, b) >= 0; }

} // namespace ILSpy::Decompiler::Util
