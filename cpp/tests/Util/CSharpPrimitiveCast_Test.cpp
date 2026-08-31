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

// CSharpPrimitiveCast (Util/CSharpPrimitiveCast.cpp) -- the constant-value converter the
// CSharpResolver.Convert region consumes. The suite pins the C# checked/unchecked
// primitive-cast semantics per conversion class (ECMA-334 10.3.2): the entry contract
// (null / same-type), the Boolean source, the integral-to-integral range/wrap pair, the
// floating-to-integral truncation, the never-throwing floating targets, the decimal arms
// (which throw on overflow in BOTH contexts -- the System.Decimal op_Explicit quirk),
// and the InvalidCastException directions.

#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::Util::Cast;
using ILSpy::Decompiler::Util::Decimal;
using ILSpy::Decompiler::Util::InvalidCastException;
using ILSpy::Decompiler::Util::OverflowException;

// The extraction helpers -- the port boxes each C# primitive as its C++ counterpart
// (the D374/D424 constant-value convention), so the tests unbox with any_cast.
template <typename T>
T As(const std::any& result)
{
    return std::any_cast<T>(result);
}

TEST(Util_CSharpPrimitiveCast, NullInputReturnsNullInBothContexts)
{
    EXPECT_FALSE(Cast(TypeCode::Int32, std::any(), true).has_value());
    EXPECT_FALSE(Cast(TypeCode::Int32, std::any(), false).has_value());
}

TEST(Util_CSharpPrimitiveCast, SameTypeCodeReturnsInputUnchanged)
{
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, std::int32_t(5), true)), 5);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, 1.5f, false)), 1.5f);
    const Decimal input{19, 1, false};
    const Decimal result = As<Decimal>(Cast(TypeCode::Decimal, input, true));
    EXPECT_EQ(result.mantissa, 19);
    EXPECT_EQ(result.scale, 1);
    EXPECT_FALSE(result.isNegative);
}

TEST(Util_CSharpPrimitiveCast, BooleanSourceMapsToZeroOrOnePerTarget)
{
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, true, true)), 1);
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, false, false)), 0);
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, true, true)), 1);
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, false, true)), 0);
    EXPECT_DOUBLE_EQ(As<double>(Cast(TypeCode::Double, true, true)), 1.0);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, false, true)), 0.0f);
    const Decimal decimalTrue = As<Decimal>(Cast(TypeCode::Decimal, true, true));
    EXPECT_EQ(decimalTrue.mantissa, 1);
    EXPECT_EQ(decimalTrue.scale, 0);
    EXPECT_FALSE(decimalTrue.isNegative);
}

// ---- integral -> integral, checked ----------------------------------------

TEST(Util_CSharpPrimitiveCast, CheckedIntegralInRangeConverts)
{
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(100), true)), 100);
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, std::int32_t(65), true)), u'A');
    EXPECT_EQ(As<std::uint32_t>(Cast(TypeCode::UInt32, std::int32_t(5), true)), 5u);
    EXPECT_EQ(As<std::int64_t>(Cast(TypeCode::Int64, std::uint32_t(7), true)), 7);
}

TEST(Util_CSharpPrimitiveCast, CheckedIntegralBoundaryValuesConvert)
{
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(127), true)), 127);
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(-128), true)), -128);
    EXPECT_EQ(As<std::uint64_t>(Cast(TypeCode::UInt64, std::numeric_limits<std::int64_t>::max(), true)),
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, std::uint16_t(65535), true)), 65535);
    EXPECT_EQ(As<std::int64_t>(Cast(TypeCode::Int64,
                                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                                    true)),
              std::numeric_limits<std::int64_t>::max());
}

TEST(Util_CSharpPrimitiveCast, CheckedIntegralOutOfRangeThrows)
{
    EXPECT_THROW(Cast(TypeCode::SByte, std::int32_t(128), true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Byte, std::int32_t(-1), true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::UInt16, std::int32_t(-1), true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Char, std::int32_t(-1), true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::UInt64, std::int64_t(-1), true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Int64, std::numeric_limits<std::uint64_t>::max(), true),
                 OverflowException);
    EXPECT_THROW(Cast(TypeCode::SByte, char16_t(0x80), true), OverflowException);
}

TEST(Util_CSharpPrimitiveCast, CharSourceConvertsToNumericTargets)
{
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, char16_t(u'a'), true)), 97);
    EXPECT_DOUBLE_EQ(As<double>(Cast(TypeCode::Double, char16_t(u'a'), true)), 97.0);
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, char16_t(0x7F), true)), 127);
}

// ---- integral -> integral, unchecked (the two's-complement wrap) -----------

TEST(Util_CSharpPrimitiveCast, UncheckedIntegralWrapsToTheLowBits)
{
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(128), false)), -128);
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(-129), false)), 127);
    EXPECT_EQ(As<std::uint8_t>(Cast(TypeCode::Byte, std::int32_t(-1), false)), 255);
    EXPECT_EQ(As<std::uint16_t>(Cast(TypeCode::UInt16, std::int32_t(-1), false)), 65535);
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, std::numeric_limits<std::uint64_t>::max(), false)), -1);
    EXPECT_EQ(As<std::uint64_t>(Cast(TypeCode::UInt64, std::int64_t(-1), false)),
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(As<std::uint32_t>(Cast(TypeCode::UInt32, std::int64_t(-1), false)), 0xFFFFFFFFu);
}

TEST(Util_CSharpPrimitiveCast, UncheckedIntegralInRangeMatchesTheCheckedResult)
{
    EXPECT_EQ(As<std::int8_t>(Cast(TypeCode::SByte, std::int32_t(100), false)), 100);
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, std::int32_t(65), false)), u'A');
}

// ---- floating -> integral --------------------------------------------------

TEST(Util_CSharpPrimitiveCast, CheckedFloatingTruncatesTowardZeroThenRangeChecks)
{
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, 2.9, true)), 2);
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, -2.9, true)), -2);
    // The trunc-then-check crux: the operand rounds toward zero FIRST, so a value
    // whose fraction crosses the boundary still fits.
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, 2147483647.9, true)), 2147483647);
    EXPECT_EQ(As<std::uint32_t>(Cast(TypeCode::UInt32, -0.5, true)), 0u);
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, 65535.9, true)), 65535);
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, 1.1f, true)), 1);
}

TEST(Util_CSharpPrimitiveCast, CheckedFloatingOutOfRangeOrNaNThrows)
{
    EXPECT_THROW(Cast(TypeCode::Int32, 2.5e9, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::UInt32, -1.5, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::SByte, 128.0f, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Char, 65536.0, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Int32, std::nan(""), true), OverflowException);
    // 2^63 exactly: the truncated value is not < 2^63, so int64 rejects it.
    EXPECT_THROW(Cast(TypeCode::Int64, 9223372036854775808.0, true), OverflowException);
    // An in-range value just below 2^63 converts.
    EXPECT_EQ(As<std::int64_t>(Cast(TypeCode::Int64, 9223372036854774784.0, true)),
              9223372036854774784LL);
}

TEST(Util_CSharpPrimitiveCast, UncheckedFloatingSaturates)
{
    // The C# unchecked result is an unspecified value; the port truncates toward zero
    // and saturates (the documented deterministic reading).
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, 2.9, false)), 2);
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, 2.5e9, false)),
              std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, -2.5e9, false)),
              std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, std::nan(""), false)), 0);
    EXPECT_EQ(As<std::uint32_t>(Cast(TypeCode::UInt32, -1.5, false)), 0u);
}

// ---- integral / decimal -> floating (never throws) -------------------------

TEST(Util_CSharpPrimitiveCast, IntegralToSingleRoundsToNearestAndNeverThrows)
{
    // 2^24 + 1 is not representable in float -- the round-to-nearest neighbor is 2^24.
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, std::int64_t(16777217), true)), 16777216.0f);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, std::numeric_limits<std::uint64_t>::max(), true)),
                    18446744073709551616.0f);
    EXPECT_DOUBLE_EQ(As<double>(Cast(TypeCode::Double, std::int32_t(-5), true)), -5.0);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, char16_t(u'a'), true)), 97.0f);
}

TEST(Util_CSharpPrimitiveCast, DoubleToSingleOutOfRangeYieldsInfinityWithoutThrowing)
{
    // The checked context does not apply to floating-point narrowing: an out-of-range
    // double->float conversion yields Infinity, not an OverflowException.
    EXPECT_TRUE(std::isinf(As<float>(Cast(TypeCode::Single, 1e300, true))));
    EXPECT_TRUE(std::isinf(As<float>(Cast(TypeCode::Single, -1e300, false))));
    EXPECT_GT(As<float>(Cast(TypeCode::Single, 1e300, true)), 0.0f);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, 0.1, true)), 0.1f);
}

TEST(Util_CSharpPrimitiveCast, SingleToDoubleIsExact)
{
    EXPECT_DOUBLE_EQ(As<double>(Cast(TypeCode::Double, 0.1f, true)),
                     static_cast<double>(0.1f));
}

// ---- the decimal arms -------------------------------------------------------

TEST(Util_CSharpPrimitiveCast, IntegralToDecimalPacksAtScaleZero)
{
    const Decimal positive = As<Decimal>(Cast(TypeCode::Decimal, std::int32_t(5), true));
    EXPECT_EQ(positive.mantissa, 5);
    EXPECT_EQ(positive.scale, 0);
    EXPECT_FALSE(positive.isNegative);

    const Decimal negative = As<Decimal>(Cast(TypeCode::Decimal, std::int32_t(-7), false));
    EXPECT_EQ(negative.mantissa, 7);
    EXPECT_EQ(negative.scale, 0);
    EXPECT_TRUE(negative.isNegative);

    const Decimal fromLong = As<Decimal>(Cast(TypeCode::Decimal, std::numeric_limits<std::int64_t>::max(), true));
    EXPECT_EQ(fromLong.mantissa, std::numeric_limits<std::int64_t>::max());
    EXPECT_FALSE(fromLong.isNegative);
}

TEST(Util_CSharpPrimitiveCast, UInt64ToDecimalThrowsPastTheStandInMantissa)
{
    // The stand-in fidelity limit: a real System.Decimal holds the full ulong range
    // exactly; the 64-bit stand-in mantissa cannot hold values above 2^63-1.
    EXPECT_NO_THROW(Cast(TypeCode::Decimal,
                         static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), true));
    EXPECT_THROW(Cast(TypeCode::Decimal, std::numeric_limits<std::uint64_t>::max(), true),
                 OverflowException);
}

TEST(Util_CSharpPrimitiveCast, FloatingToDecimalRoundsToTheNearestInteger)
{
    const Decimal positive = As<Decimal>(Cast(TypeCode::Decimal, 123.0, true));
    EXPECT_EQ(positive.mantissa, 123);
    EXPECT_EQ(positive.scale, 0);
    EXPECT_FALSE(positive.isNegative);

    const Decimal negative = As<Decimal>(Cast(TypeCode::Decimal, -7.0, false));
    EXPECT_EQ(negative.mantissa, 7);
    EXPECT_TRUE(negative.isNegative);
}

TEST(Util_CSharpPrimitiveCast, FloatingToDecimalOutOfRangeThrowsInBothContexts)
{
    // 1e30 exceeds even the real System.Decimal range (7.9e28), so both the real
    // conversion and the stand-in throw -- in BOTH contexts (the System.Decimal
    // op_Explicit quirk: decimal overflows are never "unchecked").
    EXPECT_THROW(Cast(TypeCode::Decimal, 1e30, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Decimal, 1e30, false), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Decimal, 1e19, true), OverflowException);  // the stand-in's 2^63 limit
}

TEST(Util_CSharpPrimitiveCast, DecimalToIntegralTruncatesTowardZero)
{
    const Decimal onePointNine{19, 1, false};
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, onePointNine, true)), 1);
    const Decimal negativeOnePointNine{19, 1, true};
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, negativeOnePointNine, true)), -1);
    EXPECT_EQ(As<char16_t>(Cast(TypeCode::Char, Decimal{65, 0, false}, true)), u'A');
    EXPECT_EQ(As<std::uint16_t>(Cast(TypeCode::UInt16, Decimal{65535, 0, false}, true)), 65535);
    // A scale of 20+ truncates any stand-in magnitude to zero.
    EXPECT_EQ(As<std::int32_t>(Cast(TypeCode::Int32, Decimal{123, 25, false}, true)), 0);
}

TEST(Util_CSharpPrimitiveCast, DecimalToSingleAndDoubleScaleTheMantissa)
{
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, Decimal{25, 1, false}, true)), 2.5f);
    EXPECT_DOUBLE_EQ(As<double>(Cast(TypeCode::Double, Decimal{25, 1, true}, true)), -2.5);
    EXPECT_FLOAT_EQ(As<float>(Cast(TypeCode::Single, Decimal{65, 0, false}, true)), 65.0f);
}

TEST(Util_CSharpPrimitiveCast, DecimalToIntegralOutOfRangeThrowsInBothContexts)
{
    // The System.Decimal op_Explicit quirk: the decimal -> integral conversion throws
    // OverflowException when out of range REGARDLESS of the checked/unchecked context
    // (the operator itself throws), unlike the integral -> integral pair.
    EXPECT_THROW(Cast(TypeCode::SByte, Decimal{200, 0, false}, true), OverflowException);
    EXPECT_THROW(Cast(TypeCode::SByte, Decimal{200, 0, false}, false), OverflowException);
    EXPECT_THROW(Cast(TypeCode::UInt32, Decimal{1, 0, true}, false), OverflowException);
    EXPECT_THROW(Cast(TypeCode::Int32, Decimal{3000000000, 0, false}, false), OverflowException);
}

// ---- the invalid directions -------------------------------------------------

TEST(Util_CSharpPrimitiveCast, StringSourceThrowsInvalidCast)
{
    EXPECT_THROW(Cast(TypeCode::Int32, std::string("5"), true), InvalidCastException);
    EXPECT_THROW(Cast(TypeCode::Decimal, std::string("5"), false), InvalidCastException);
}

TEST(Util_CSharpPrimitiveCast, StringTargetThrowsInvalidCast)
{
    // String is not one of the 12 target codes -- every non-string source falls to the
    // default InvalidCastException arm. (A string SOURCE with the String target takes
    // the same-type early return -- the faithful C# passthrough -- pinned below.)
    EXPECT_THROW(Cast(TypeCode::String, std::int32_t(5), true), InvalidCastException);
    EXPECT_EQ(As<std::string>(Cast(TypeCode::String, std::string("5"), false)), "5");
}

TEST(Util_CSharpPrimitiveCast, NonPrimitiveTargetAndSourceThrowInvalidCast)
{
    // Boolean is a SOURCE in every target, but never a target code.
    EXPECT_THROW(Cast(TypeCode::Boolean, std::int32_t(1), true), InvalidCastException);
    // An any holding a non-primitive type maps to Object -- the default arm.
    EXPECT_THROW(Cast(TypeCode::Int32, std::vector<int>{1}, true), InvalidCastException);
    EXPECT_THROW(Cast(TypeCode::Empty, std::int32_t(1), true), InvalidCastException);
    EXPECT_THROW(Cast(TypeCode::Object, std::int32_t(1), false), InvalidCastException);
}

TEST(Util_CSharpPrimitiveCast, ExceptionTypesDeriveRuntimeErrorButStayDistinct)
{
    // Both exception kinds derive std::runtime_error (a caller catching the base sees
    // either), but the two types stay distinct so the ResolveCast catch arms can
    // discriminate them.
    try
    {
        Cast(TypeCode::SByte, std::int32_t(128), true);
        FAIL() << "the checked overflow must throw";
    }
    catch (const OverflowException&)
    {
    }
    catch (...)
    {
        FAIL() << "the wrong exception kind escaped";
    }
    try
    {
        Cast(TypeCode::String, std::int32_t(5), true);
        FAIL() << "the unsupported target must throw";
    }
    catch (const InvalidCastException&)
    {
    }
    catch (...)
    {
        FAIL() << "the wrong exception kind escaped";
    }
}

} // namespace
