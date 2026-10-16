// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

// The C# FractionApprox (TypeSystemAstBuilder.cs lines 1725-1784): the
// continued-fraction approximation of a double value with a bounded
// denominator (the theory-of-continued-fractions matrix walk). Returns the
// best (numerator, denominator) pair; the sign of the input is restored.
inline std::pair<long, long> FractionApprox(double value, int maxDenominator) {
    if (std::fabs(value) > 0x7FFFFFFF) return {0, 0};
    double startValue = value;
    if (value < 0) value = -value;
    long ai;
    long m[2][2] = {{1, 0}, {0, 1}};
    double v = value;
    while (m[1][0] * (ai = static_cast<long>(v)) + m[1][1] <=
           maxDenominator) {
        long t = m[0][0] * ai + m[0][1];
        m[0][1] = m[0][0];
        m[0][0] = t;
        t = m[1][0] * ai + m[1][1];
        m[1][1] = m[1][0];
        m[1][0] = t;
        if (v - ai == 0) break;
        v = 1 / (v - ai);
        if (std::fabs(v) >=
            static_cast<double>(std::numeric_limits<long>::max()))
            break;
    }
    if (m[1][0] == 0) return {0, 0};
    long firstN = m[0][0];
    long firstD = m[1][0];
    ai = (maxDenominator - m[1][1]) / m[1][0];
    long secondN = m[0][0] * ai + m[0][1];
    long secondD = m[1][0] * ai + m[1][1];
    double firstDelta =
        std::fabs(value - firstN / static_cast<double>(firstD));
    double secondDelta =
        std::fabs(value - secondN / static_cast<double>(secondD));
    if (firstDelta < secondDelta)
        return {startValue < 0 ? -firstN : firstN, firstD};
    return {startValue < 0 ? -secondN : secondN, secondD};
}

// The C# IsValidFraction (lines 1458-1466): a positive denominator, a
// nonzero numerator, and either a trivial part (1) or |num| < den with
// the denominator built from the 2/3/5 prime family.
inline bool IsValidFraction(long num, long den) {
    if (!(den > 0 && num != 0)) return false;
    if (den == 1 || std::labs(num) == 1) return true;
    return std::labs(num) < den && (den % 2 == 0 || den % 3 == 0 ||
                                    den % 5 == 0);
}

static std::string SpecialDoubleConstantText(double value) {
    static const double kFields[2] = {3.141592653589793,
                                       2.718281828459045};
    static const char* kNames[2] = {"PI", "E"};
    constexpr int kMaxDenominator = 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    std::string str = buf;
    if (str.size() - (str[0] == '-' ? 2 : 1) <= 5) return std::string();
    for (int i = 0; i < 2; ++i) {
        auto [num, den] =
            CSharp::FractionApprox(value / kFields[i], kMaxDenominator);
        if (!CSharp::IsValidFraction(num, den)) continue;
        // The multiply form: field * n / d == value.
        double approx = kFields[i] * static_cast<double>(num) /
                        static_cast<double>(den);
        if (approx == value) {
            std::string expr = std::string("Math.") + kNames[i];
            if (num == -1) expr = "-" + expr;
            else if (num != 1)
                expr += " * " + std::to_string(num) + ".0";
            if (den != 1)
                expr += " / " + std::to_string(den) + ".0";
            return expr;
        }
        // The division form: n / (d * field) == value.
        double divApprox = static_cast<double>(num) /
                           (static_cast<double>(den) * kFields[i]);
        if (divApprox == value) {
            std::string field = std::string("Math.") + kNames[i];
            if (den == 1)
                return std::to_string(num) + ".0 / " + field;
            return std::to_string(num) + ".0 / (" +
                   std::to_string(den) + ".0 * " + field + ")";
        }
    }
    return std::string();
}

// The preferred machine-scale denominators (the upstream's
// preferredFractionDenominators): powers of two used for binary scaling,
// and 2^n-1 values used when normalizing integers (for example byte colors
// / 255). Kept as a targeted candidate set rather than increasing the
// generic denominator limit, which would reintroduce accidental fraction
// matches for ordinary floating-point values.
inline constexpr int kPreferredFractionDenominators[] = {
    127, 128,     255, 256,     1023, 1024,   4095, 4096,
    8192,         16384,       32767, 32768, 65535, 65536,
    1048576
};

// The upstream's GetIntegerLiteralLength: the decimal digits (plus the
// sign) of a long.
inline int GetIntegerLiteralLength(long long value) {
    int length = value < 0 ? 1 : 0;
    do {
        length++;
        value /= 10;
    } while (value != 0);
    return length;
}

// The upstream's GetFractionDisplayLength: the printed length of
// `num / den` (the `num` and `den` literals plus the ` / ` separator and
// the per-type suffix; the float integer literals carry `f` (1 char), the
// double ones `.0` (2 chars)).
inline int GetFractionDisplayLength(long long num, long long den,
                                    bool isDouble) {
    int numericSuffixLength = isDouble ? 2 : 1;
    return GetIntegerLiteralLength(num) + GetIntegerLiteralLength(den) +
           3 + 2 * numericSuffixLength;
}

// The upstream's IsSimpleFraction: unit fractions and denominators
// composed only of 2, 3 and 5 are already conventional forms; do not
// expand them just to reach a preferred scale.
inline bool IsSimpleFraction(long long num, long long den) {
    if (num == 1 || num == -1) return true;
    while (den % 2 == 0) den /= 2;
    while (den % 3 == 0) den /= 3;
    while (den % 5 == 0) den /= 5;
    return den == 1;
}

// The upstream's GetPreferredFractionScore: powers of two are native to
// binary floating point (the weaker normalization signal); 2^n-1 values
// get the slightly larger bonus.
inline int GetPreferredFractionScore(long long num, int den, bool isDouble) {
    bool isPowerOfTwo = (den & (den - 1)) == 0;
    int readabilityBonus = isPowerOfTwo ? 1 : 2;
    return GetFractionDisplayLength(num, den, isDouble) - readabilityBonus;
}

// The upstream's TryGetPreferredFraction: the best (shortest-displaying)
// machine-scale fraction that reconstructs the value exactly. Only
// |value| < 1 participates (the normalized-integer domain).
inline bool TryGetPreferredFraction(double value, bool isDouble,
                                    long long& num, long long& den,
                                    int& score) {
    num = 0;
    den = 0;
    score = std::numeric_limits<int>::max();
    if (!(std::fabs(value) < 1.0)) return false;
    for (int candidateDen : kPreferredFractionDenominators) {
        long long candidateNum =
            static_cast<long long>(std::llround(value * candidateDen));
        if (candidateNum == 0 || candidateNum <= -candidateDen ||
            candidateNum >= candidateDen)
            continue;
        double approx = static_cast<double>(candidateNum) / candidateDen;
        bool equal = isDouble
            ? approx == value
            : static_cast<float>(approx) == static_cast<float>(value);
        if (!equal) continue;
        int candidateScore =
            GetPreferredFractionScore(candidateNum, candidateDen, isDouble);
        if (candidateScore < score ||
            (candidateScore == score && candidateDen < den)) {
            num = candidateNum;
            den = candidateDen;
            score = candidateScore;
        }
    }
    return den != 0;
}

// The special named floating-point constants (the C# IsSpecialConstant's
// float/double surface with the negation retry): MinValue, MaxValue,
// Epsilon, NaN and the infinities render as the named field references;
// a negative value whose positive counterpart is a special constant
// renders `-<field>` (e.g. -double.Epsilon).
inline std::string SpecialNamedFloatingConstant(bool isFloat, double v) {
    const char* typeName = isFloat ? "float" : "double";
    double maxV = isFloat ? std::numeric_limits<float>::max()
                          : std::numeric_limits<double>::max();
    double epsV = isFloat
        ? static_cast<double>(std::numeric_limits<float>::denorm_min())
        : std::numeric_limits<double>::denorm_min();
    auto named = [&](double value) -> std::string {
        if (std::isnan(value)) return "NaN";
        if (std::isinf(value))
            return value < 0 ? "NegativeInfinity" : "PositiveInfinity";
        if (value == -maxV) return "MinValue";
        if (value == maxV) return "MaxValue";
        if (value == epsV) return "Epsilon";
        return std::string();
    };
    std::string direct = named(v);
    if (!direct.empty()) return std::string(typeName) + "." + direct;
    if (v < 0) {
        std::string positive = named(-v);
        if (!positive.empty())
            return "-" + std::string(typeName) + "." + positive;
    }
    return std::string();
}

} // namespace ILSpy::Decompiler::CSharp
