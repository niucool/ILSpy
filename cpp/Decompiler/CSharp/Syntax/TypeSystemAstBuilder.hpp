// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the static helpers on ICSharpCode.Decompiler/CSharp/Syntax/TypeSystemAstBuilder.cs
// that depend only on already-ported TypeSystem / Syntax leaves. The full
// TypeSystemAstBuilder class (the long-pole CSharpAmbience blocker, 2782 C# lines)
// derives from CSharpResolver and threads the full IType / IMember / ITypeDefinition
// surface, so it is ported incrementally: each self-contained static helper lands as a
// free function in this namespace ahead of the instance methods, which are deferred
// until the CSharpResolver dependency chain is ported.
//
// ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497) is the first such
// helper: a pure switch on Accessibility (the D373 leaf) that maps a symbol's
// visibility to the Syntax Modifiers bits (the D270 enum). It is the public static
// API the CSharpAmbience / GetMemberModifiers path calls; the usePrivateProtected
// bool arg gates whether ProtectedAndInternal maps to `private protected`
// (Private | Protected) or to the C# 7 fallback `protected` (Protected) -- the
// UsePrivateProtectedAccessibility property of TypeSystemAstBuilder.

#pragma once

#include "Modifiers.hpp"

#include "Decompiler/TypeSystem/Accessibility.hpp"

#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// TypeSystemAstBuilder.ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497).
// Maps an Accessibility value to the corresponding Syntax Modifiers bits. The
// ProtectedAndInternal case is gated by usePrivateProtected: when true it emits
// `private protected` (Modifiers::Private | Modifiers::Protected, the C# 7.2
// private-protected accessibility); when false it falls back to the pre-C#-7.2
// `protected` (Modifiers::Protected). The default case (Accessibility::None or any
// other value) returns Modifiers::None.
inline Modifiers ModifierFromAccessibility(::ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                                          bool usePrivateProtected) noexcept {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    switch (accessibility) {
        case TS::Accessibility::Private:
            return Modifiers::Private;
        case TS::Accessibility::Public:
            return Modifiers::Public;
        case TS::Accessibility::Protected:
            return Modifiers::Protected;
        case TS::Accessibility::Internal:
            return Modifiers::Internal;
        case TS::Accessibility::ProtectedOrInternal:
            return Modifiers::Protected | Modifiers::Internal;
        case TS::Accessibility::ProtectedAndInternal:
            return usePrivateProtected ? (Modifiers::Private | Modifiers::Protected)
                                      : Modifiers::Protected;
        default:
            return Modifiers::None;
    }
}

// ---------------------------------------------------------------------------
// Pure-math fraction helpers (TypeSystemAstBuilder.cs lines 1458-1490 and
// 1725-1773). These back ConvertFloatingPointLiteral, which renders a
// floating-point literal as a rational `num / den` BinaryOperatorExpression
// when the decimal form is long and an exact-enough fraction exists within the
// max-denominator bound. They are pure (no type-system state), so they port
// ahead of the instance method that drives them.
//
// The C# `long` arguments are System.Int64 (64-bit); C++ `long` is 32-bit on
// MSVC Windows, so the port uses std::int64_t to preserve the 64-bit range the
// continued-fraction accumulator relies on. The C# `(long Num, long Den)` tuple
// return ports to std::pair<std::int64_t, std::int64_t>.
// ---------------------------------------------------------------------------

// TypeSystemAstBuilder.IsValidFraction (TypeSystemAstBuilder.cs line 1458).
// A (num, den) pair is a valid printable fraction when the denominator is
// positive, the numerator is non-zero, the fraction is proper (|num| < den)
// unless it is whole (den == 1) or a unit (|num| == 1), and the denominator is
// 5-smooth (divisible by 2, 3, or 5) -- the denominators that yield a short
// terminating decimal expansion. The 5-smooth gate rejects coincidental
// fractions such as 1f/MathF.PI == 113f/355f.
inline bool IsValidFraction(std::int64_t num, std::int64_t den) noexcept {
    if (!(den > 0 && num != 0))
        return false;

    if (den == 1 || std::llabs(num) == 1)
        return true;
    return std::llabs(num) < den && (den % 2 == 0 || den % 3 == 0 || den % 5 == 0);
}

// TypeSystemAstBuilder.EqualDoubles / EqualFloats (TypeSystemAstBuilder.cs
// lines 1469 and 1477). The C# `in` parameters and [MethodImpl(NoInlining)]
// force the values through memory (no more than 64/32 bits of precision),
// defeating any JIT retention in an 80-bit x87 register. The C++ port passes
// by value: MSVC x64 uses SSE2 (exact 64/32-bit), so by-value carries no
// extended precision, and the NoInlining hint has no C++ counterpart.
inline bool EqualDoubles(double val1, double val2) noexcept {
    return val1 == val2;
}

inline bool EqualFloats(float val1, float val2) noexcept {
    return val1 == val2;
}

// TypeSystemAstBuilder.IsEqual (TypeSystemAstBuilder.cs line 1484). Compares a
// candidate fraction num/den against the boxed constant value, dispatching to
// EqualDoubles or EqualFloats by the isDouble flag. The C# `object` parameter
// (the coerced constant value, a boxed double or float) ports to const
// std::any&; the C# `(double)`/`(float)` cast (which throws InvalidCastException
// on a mismatched box) ports to std::any_cast (which throws std::bad_any_cast),
// the D374 object?-to-std::any convention.
inline bool IsEqual(std::int64_t num, std::int64_t den,
                    const std::any& constantValue, bool isDouble) {
    if (isDouble) {
        return EqualDoubles(std::any_cast<double>(constantValue),
                            num / static_cast<double>(den));
    } else {
        return EqualFloats(std::any_cast<float>(constantValue),
                           num / static_cast<float>(den));
    }
}

// TypeSystemAstBuilder.FractionApprox (TypeSystemAstBuilder.cs line 1725).
// Returns the best rational approximation (num, den) to `value` with
// den <= maxDenominator, via the continued-fraction / semi-convergent algorithm.
// Returns (0, 0) when |value| exceeds 0x7FFFFFFF (the magnitude guard) or when
// no non-trivial denominator is produced. The sign is stripped for the
// continued-fraction accumulation (which would overflow on a large negative)
// and re-applied from the original startValue at the return.
inline std::pair<std::int64_t, std::int64_t> FractionApprox(double value, int maxDenominator) {
    // The range check has to be on the magnitude: the sign is stripped below, so a
    // large negative value would otherwise reach the continued-fraction loop and
    // overflow the terms it accumulates.
    if (std::fabs(value) > 0x7FFFFFFF)
        return {0, 0};

    double startValue = value;
    if (value < 0)
        value = -value;

    std::int64_t ai;
    std::int64_t m[2][2] = {{1, 0}, {0, 1}};

    double v = value;

    while (m[1][0] * (ai = static_cast<std::int64_t>(v)) + m[1][1] <= maxDenominator) {
        std::int64_t t = m[0][0] * ai + m[0][1];
        m[0][1] = m[0][0];
        m[0][0] = t;
        t = m[1][0] * ai + m[1][1];
        m[1][1] = m[1][0];
        m[1][0] = t;
        if (v - static_cast<double>(ai) == 0)
            break;
        v = 1 / (v - static_cast<double>(ai));
        if (std::fabs(v) >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            // values greater than long.MaxValue cannot be stored in fraction without overflow.
            // Because the implicit conversion of long.MaxValue to double loses precision,
            // it's possible that a value v that is strictly greater than long.MaxValue will
            // nevertheless compare equal, so we use ">=" to compensate.
            break;
        }
    }

    if (m[1][0] == 0)
        return {0, 0};

    std::int64_t firstN = m[0][0];
    std::int64_t firstD = m[1][0];

    ai = (maxDenominator - m[1][1]) / m[1][0];
    std::int64_t secondN = m[0][0] * ai + m[0][1];
    std::int64_t secondD = m[1][0] * ai + m[1][1];

    double firstDelta = std::fabs(value - firstN / static_cast<double>(firstD));
    double secondDelta = std::fabs(value - secondN / static_cast<double>(secondD));

    if (firstDelta < secondDelta)
        return {startValue < 0 ? -firstN : firstN, firstD};
    return {startValue < 0 ? -secondN : secondN, secondD};
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
