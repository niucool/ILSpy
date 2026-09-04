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

// Tests for `DisassemblerHelpers` (cpp/Decompiler/Disassembler/DisassemblerHelpers.hpp --
// the port of the Phase-6 static helper class MethodBodyDisassembler and
// ReflectionDisassembler consume): the IL_xxxx offset labels, the identifier
// escape rules over the ILKeywords set, the operand writers (the ILDasm
// literal spellings), the string escaper, the primitive type-name table, and
// the parameter/variable references. The WriteParameterReference tests pin the
// Static-flag sequence mapping against a real mscorlib fixture (the
// MetadataAttributes_Test conventions): String.Copy(String str) is static
// (IL index 0 == "str"), String.Substring(int startIndex) is an instance
// method (IL index 0 is the implicit this, IL index 1 == "startIndex").

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

using namespace ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace OUT = ILSpy::Decompiler::Output;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(MD::MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindMethodToken(MD::MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// OffsetToString / WriteOffsetReference / WriteVariableReference
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersTest, OffsetToStringFormatsLowercaseHexPaddedToFour) {
	EXPECT_EQ(OffsetToString(0), "IL_0000");
	EXPECT_EQ(OffsetToString(0x1234), "IL_1234");
	EXPECT_EQ(OffsetToString(0xFFFF), "IL_ffff");
}

TEST(DisassemblerHelpersTest, OffsetToStringDoesNotTruncatePastFourDigits) {
	// The C# "{0:x4}" is a minimum width, not a limit.
	EXPECT_EQ(OffsetToString(0x10000), "IL_10000");
	EXPECT_EQ(OffsetToString(0x123456), "IL_123456");
}

TEST(DisassemblerHelpersTest, OffsetToStringLongOverloadMatches) {
	EXPECT_EQ(OffsetToString(std::int64_t(0)), "IL_0000");
	EXPECT_EQ(OffsetToString(std::int64_t(0x1234)), "IL_1234");
	EXPECT_EQ(OffsetToString(std::int64_t(0x123456789ABLL)), "IL_123456789ab");
}

TEST(DisassemblerHelpersTest, WriteOffsetReferenceNullWritesNull) {
	OUT::PlainTextOutput output;
	WriteOffsetReference(output, std::nullopt);
	EXPECT_EQ(output.ToString(), "null");
}

TEST(DisassemblerHelpersTest, WriteOffsetReferenceWritesTheLabel) {
	OUT::PlainTextOutput output;
	WriteOffsetReference(output, 8);
	EXPECT_EQ(output.ToString(), "IL_0008");

	OUT::PlainTextOutput output2;
	WriteOffsetReference(output2, 0x1234);
	EXPECT_EQ(output2.ToString(), "IL_1234");
}

TEST(DisassemblerHelpersTest, WriteVariableReferenceWritesTheBareIndex) {
	OUT::PlainTextOutput output;
	WriteVariableReference(output, 3);
	EXPECT_EQ(output.ToString(), "3");

	OUT::PlainTextOutput output2;
	WriteVariableReference(output2, 17);
	EXPECT_EQ(output2.ToString(), "17");
}

// ---------------------------------------------------------------------------
// ILKeywords + IsValidIdentifier + Escape
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersEscapeTest, ILKeywordsContainTheILAsmKeywords) {
	const auto& keywords = MD::ILKeywords();
	EXPECT_GT(keywords.count("class"), 0u);
	EXPECT_GT(keywords.count("int32"), 0u);
	EXPECT_GT(keywords.count("static"), 0u);
	EXPECT_GT(keywords.count("catch"), 0u);
	EXPECT_GT(keywords.count("native"), 0u);
	EXPECT_GT(keywords.count("uint8"), 0u);
	// The not-in-spec-but-ILAsm-treats-as-such tail of the C# list.
	EXPECT_GT(keywords.count("property"), 0u);
	EXPECT_GT(keywords.count("codelabel"), 0u);
	EXPECT_GT(keywords.count("strict"), 0u);
}

TEST(DisassemblerHelpersEscapeTest, ILKeywordsContainTheOpcodeDisplayNames) {
	// BuildKeywordList adds every non-empty opcode display name.
	const auto& keywords = MD::ILKeywords();
	EXPECT_GT(keywords.count("nop"), 0u);
	EXPECT_GT(keywords.count("ldarg.0"), 0u);
	EXPECT_GT(keywords.count("ret"), 0u);
	EXPECT_GT(keywords.count("ldftn"), 0u);
	// The prefix opcode names keep their trailing dot.
	EXPECT_GT(keywords.count("unaligned."), 0u);
	EXPECT_GT(keywords.count("volatile."), 0u);
	EXPECT_GT(keywords.count("readonly."), 0u);
}

TEST(DisassemblerHelpersEscapeTest, ILKeywordsDoNotContainOrdinaryIdentifiers) {
	const auto& keywords = MD::ILKeywords();
	EXPECT_EQ(keywords.count("Main"), 0u);
	EXPECT_EQ(keywords.count("notakeyword"), 0u);
	EXPECT_EQ(keywords.count("System"), 0u);
	EXPECT_EQ(keywords.count(""), 0u);
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierAcceptsOrdinaryNames) {
	EXPECT_TRUE(IsValidIdentifier("Main"));
	EXPECT_TRUE(IsValidIdentifier("_x"));
	EXPECT_TRUE(IsValidIdentifier("a$b"));
	EXPECT_TRUE(IsValidIdentifier("x@y"));
	EXPECT_TRUE(IsValidIdentifier("q?r"));
	EXPECT_TRUE(IsValidIdentifier("t`u"));
	EXPECT_TRUE(IsValidIdentifier("v.w"));
	EXPECT_TRUE(IsValidIdentifier("M\xc3\xa9thodo"));  // UTF-8 "Méthodo": letters
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierRejectsTheEmptyString) {
	EXPECT_FALSE(IsValidIdentifier(""));
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierRejectsDigitStarts) {
	EXPECT_FALSE(IsValidIdentifier("1abc"));
	EXPECT_FALSE(IsValidIdentifier("0"));
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierAcceptsOnlyCtorAndCctorDotForms) {
	EXPECT_TRUE(IsValidIdentifier(".ctor"));
	EXPECT_TRUE(IsValidIdentifier(".cctor"));
	EXPECT_FALSE(IsValidIdentifier(".foo"));
	EXPECT_FALSE(IsValidIdentifier("."));
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierRejectsDoubleDots) {
	EXPECT_FALSE(IsValidIdentifier("a..b"));
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierRejectsILKeywords) {
	EXPECT_FALSE(IsValidIdentifier("int"));
	EXPECT_FALSE(IsValidIdentifier("class"));
	EXPECT_FALSE(IsValidIdentifier("ldarg.0"));
	EXPECT_FALSE(IsValidIdentifier("unaligned."));
	EXPECT_FALSE(IsValidIdentifier("uint32"));
}

TEST(DisassemblerHelpersEscapeTest, IsValidIdentifierRejectsInvalidCharacters) {
	EXPECT_FALSE(IsValidIdentifier("a b"));   // space is not an identifier char
	EXPECT_FALSE(IsValidIdentifier("a;b"));
	EXPECT_FALSE(IsValidIdentifier("a(b)"));
}

TEST(DisassemblerHelpersEscapeTest, EscapePassesValidIdentifiersThrough) {
	EXPECT_EQ(Escape("Main"), "Main");
	EXPECT_EQ(Escape(".ctor"), ".ctor");
	EXPECT_EQ(Escape("a$b"), "a$b");
}

TEST(DisassemblerHelpersEscapeTest, EscapeQuotesKeywords) {
	EXPECT_EQ(Escape("int"), "'int'");
	EXPECT_EQ(Escape("class"), "'class'");
	EXPECT_EQ(Escape("ldarg.0"), "'ldarg.0'");
}

TEST(DisassemblerHelpersEscapeTest, EscapeQuotesInvalidShapes) {
	EXPECT_EQ(Escape(".foo"), "'.foo'");
	EXPECT_EQ(Escape("a..b"), "'a..b'");
	EXPECT_EQ(Escape("1abc"), "'1abc'");
	EXPECT_EQ(Escape("a b"), "'a b'");
	EXPECT_EQ(Escape(""), "''");
}

TEST(DisassemblerHelpersEscapeTest, EscapeQuotesSingleQuotesWithBackslash) {
	// The ECMA octal escape is deliberately not used (the C# comment: ILDasm
	// uses \').
	EXPECT_EQ(Escape("a'b"), "'a\\'b'");
}

// ---------------------------------------------------------------------------
// EscapeString
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersEscapeTest, EscapeStringLeavesPlainAsciiUnchanged) {
	EXPECT_EQ(EscapeString("abc"), "abc");
	EXPECT_EQ(EscapeString("a b"), "a b");  // the plain space is NOT escaped
}

TEST(DisassemblerHelpersEscapeTest, EscapeStringUsesTheNamedEscapes) {
	EXPECT_EQ(EscapeString("a\"b"), "a\\\"b");
	EXPECT_EQ(EscapeString("a\\b"), "a\\\\b");
	// The full control-char set: \0 \a \b \f \n \r \t \v.
	const std::string input{std::string("\0\a\b\f\n\r\t\v", 8)};
	EXPECT_EQ(EscapeString(input), "\\0\\a\\b\\f\\n\\r\\t\\v");
}

TEST(DisassemblerHelpersEscapeTest, EscapeStringEscapesControlCharsAsUnicode) {
	EXPECT_EQ(EscapeString("\x01"), "\\u0001");
	EXPECT_EQ(EscapeString("\x7f"), "\\u007f");
	// 0x85 (NEL) is inside the control set 0x7F-0x9F.
	EXPECT_EQ(EscapeString("\xc2\x85"), "\\u0085");
}

TEST(DisassemblerHelpersEscapeTest, EscapeStringEscapesNonSpaceWhitespace) {
	// U+00A0 (no-break space) as UTF-8.
	EXPECT_EQ(EscapeString("\xc2\xa0"), "\\u00a0");
	// U+2028 (line separator) as UTF-8.
	EXPECT_EQ(EscapeString("\xe2\x80\xa8"), "\\u2028");
}

TEST(DisassemblerHelpersEscapeTest, EscapeStringEmitsSurrogateHalvesForNonBmp) {
	// U+1F600 (GRINNING FACE) as UTF-8: the C# iterates the two UTF-16
	// surrogate halves, each IsSurrogate half escaping separately.
	EXPECT_EQ(EscapeString("\xf0\x9f\x98\x80"), "\\ud83d\\ude00");
}

TEST(DisassemblerHelpersEscapeTest, EscapeStringPassesNonAsciiLettersThrough) {
	// "é" as UTF-8: a letter, not control/whitespace/surrogate.
	EXPECT_EQ(EscapeString("\xc3\xa9"), "\xc3\xa9");
}

// ---------------------------------------------------------------------------
// WriteOperand
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersTest, WriteOperandLongWritesInvariantDigits) {
	OUT::PlainTextOutput output;
	WriteOperand(output, std::int64_t(123));
	EXPECT_EQ(output.ToString(), "123");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, std::int64_t(-45));
	EXPECT_EQ(output2.ToString(), "-45");
}

TEST(DisassemblerHelpersTest, WriteOperandFloatZeroAndNegativeZero) {
	OUT::PlainTextOutput output;
	WriteOperand(output, 0.0f);
	EXPECT_EQ(output.ToString(), "0.0");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, -0.0f);
	EXPECT_EQ(output2.ToString(), "-0.0");
}

TEST(DisassemblerHelpersTest, WriteOperandFloatRoundTripFormat) {
	OUT::PlainTextOutput output;
	WriteOperand(output, 1.5f);
	EXPECT_EQ(output.ToString(), "1.5");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, 2.0f);
	EXPECT_EQ(output2.ToString(), "2");

	OUT::PlainTextOutput output3;
	WriteOperand(output3, 1e-30f);
	// The C# "R" format uses the uppercase exponent marker.
	EXPECT_EQ(output3.ToString(), "1E-30");
}

TEST(DisassemblerHelpersTest, WriteOperandFloatDumpsInfinityAndNaNBytes) {
	OUT::PlainTextOutput output;
	WriteOperand(output, std::numeric_limits<float>::infinity());
	EXPECT_EQ(output.ToString(), "(00 00 80 7F)");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, -std::numeric_limits<float>::infinity());
	EXPECT_EQ(output2.ToString(), "(00 00 80 FF)");

	OUT::PlainTextOutput output3;
	WriteOperand(output3, std::numeric_limits<float>::quiet_NaN());
	EXPECT_EQ(output3.ToString(), "(00 00 C0 7F)");
}

TEST(DisassemblerHelpersTest, WriteOperandDoubleZeroAndNegativeZero) {
	OUT::PlainTextOutput output;
	WriteOperand(output, 0.0);
	EXPECT_EQ(output.ToString(), "0.0");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, -0.0);
	EXPECT_EQ(output2.ToString(), "-0.0");
}

TEST(DisassemblerHelpersTest, WriteOperandDoubleRoundTripFormat) {
	OUT::PlainTextOutput output;
	WriteOperand(output, 1.5);
	EXPECT_EQ(output.ToString(), "1.5");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, 1.0);
	EXPECT_EQ(output2.ToString(), "1");

	OUT::PlainTextOutput output3;
	WriteOperand(output3, 1e-300);
	EXPECT_EQ(output3.ToString(), "1E-300");
}

// ---------------------------------------------------------------------------
// WriteOperand float/double round-trip NOTATION -- the .NET
// `ToString("R")` FormatGeneral rule (probed against the real .NET 10
// System.Number.FormatFloat: 'R' routes to FormatGeneral over the
// Grisu3/Dragon4 shortest round-trip digits with nMaxDigits = the type's
// MaxRoundTripDigits): FIXED notation while the decimal scale (the leading
// digit's exponent plus one) stays within [-3, 17] for double / [-3, 9] for
// float, SCIENTIFIC otherwise, the exponent spelled 'E', a sign, and a
// minimum of two digits. Every expectation below is the real .NET output
// for the same literal (C:/temp-probe/RFormatProbe/rmatrix.txt).
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersTest, WriteOperandDoubleRoundTripNotationBoundaries) {
	// Fixed while the scale <= 17: the digits consumed in order, zero-padded
	// past their end, no decimal point when they are exhausted.
	OUT::PlainTextOutput o1;
	WriteOperand(o1, 1e16);
	EXPECT_EQ(o1.ToString(), "10000000000000000");

	OUT::PlainTextOutput o2;
	WriteOperand(o2, 1.5e16);
	EXPECT_EQ(o2.ToString(), "15000000000000000");

	// Seventeen digits exactly at the boundary still render fixed.
	OUT::PlainTextOutput o3;
	WriteOperand(o3, 12345678901234568.0);
	EXPECT_EQ(o3.ToString(), "12345678901234568");

	OUT::PlainTextOutput o4;
	WriteOperand(o4, 1.25e16);
	EXPECT_EQ(o4.ToString(), "12500000000000000");

	OUT::PlainTextOutput o5;
	WriteOperand(o5, 864000000000.0);
	EXPECT_EQ(o5.ToString(), "864000000000");

	OUT::PlainTextOutput o6;
	WriteOperand(o6, 10000000.0);
	EXPECT_EQ(o6.ToString(), "10000000");

	// The scale-18 side flips scientific (the exponent two digits minimum).
	OUT::PlainTextOutput o7;
	WriteOperand(o7, 1e17);
	EXPECT_EQ(o7.ToString(), "1E+17");

	OUT::PlainTextOutput o8;
	WriteOperand(o8, 1.5e17);
	EXPECT_EQ(o8.ToString(), "1.5E+17");

	OUT::PlainTextOutput o9;
	WriteOperand(o9, 1.25e17);
	EXPECT_EQ(o9.ToString(), "1.25E+17");

	OUT::PlainTextOutput o10;
	WriteOperand(o10, 123456789012345678.0);
	EXPECT_EQ(o10.ToString(), "1.2345678901234568E+17");
}

TEST(DisassemblerHelpersTest, WriteOperandDoubleRoundTripSmallValueBoundaries) {
	// Fixed while the scale >= -3: '0', '.', the -scale zeros, the digits.
	OUT::PlainTextOutput o1;
	WriteOperand(o1, 0.0001);
	EXPECT_EQ(o1.ToString(), "0.0001");

	OUT::PlainTextOutput o2;
	WriteOperand(o2, 1.25e-4);
	EXPECT_EQ(o2.ToString(), "0.000125");

	OUT::PlainTextOutput o3;
	WriteOperand(o3, 0.000123456789012345);
	EXPECT_EQ(o3.ToString(), "0.000123456789012345");

	// The scale -4 side flips scientific.
	OUT::PlainTextOutput o4;
	WriteOperand(o4, 1e-5);
	EXPECT_EQ(o4.ToString(), "1E-05");

	OUT::PlainTextOutput o5;
	WriteOperand(o5, 1.5e-5);
	EXPECT_EQ(o5.ToString(), "1.5E-05");

	// Subnormals take the same scientific path (double.Epsilon).
	OUT::PlainTextOutput o6;
	WriteOperand(o6, 4.9406564584124654e-324);
	EXPECT_EQ(o6.ToString(), "5E-324");
}

TEST(DisassemblerHelpersTest, WriteOperandDoubleRoundTripExtremesAndNegatives) {
	OUT::PlainTextOutput o1;
	WriteOperand(o1, 1.7976931348623157e308);
	EXPECT_EQ(o1.ToString(), "1.7976931348623157E+308");

	OUT::PlainTextOutput o2;
	WriteOperand(o2, -1.7976931348623157e308);
	EXPECT_EQ(o2.ToString(), "-1.7976931348623157E+308");

	OUT::PlainTextOutput o3;
	WriteOperand(o3, 2.2250738585072014e-308);
	EXPECT_EQ(o3.ToString(), "2.2250738585072014E-308");

	OUT::PlainTextOutput o4;
	WriteOperand(o4, -1.5e16);
	EXPECT_EQ(o4.ToString(), "-15000000000000000");

	OUT::PlainTextOutput o5;
	WriteOperand(o5, -12345678901234568.0);
	EXPECT_EQ(o5.ToString(), "-12345678901234568");

	OUT::PlainTextOutput o6;
	WriteOperand(o6, 123.456);
	EXPECT_EQ(o6.ToString(), "123.456");

	OUT::PlainTextOutput o7;
	WriteOperand(o7, 0.1);
	EXPECT_EQ(o7.ToString(), "0.1");

	OUT::PlainTextOutput o8;
	WriteOperand(o8, -4.9406564584124654e-324);
	EXPECT_EQ(o8.ToString(), "-5E-324");
}

TEST(DisassemblerHelpersTest, WriteOperandFloatRoundTripNotationBoundaries) {
	// Float keeps fixed notation only while the scale <= 9 (the float
	// round-trip budget), so 1e8 stays fixed where the double rule would.
	OUT::PlainTextOutput o1;
	WriteOperand(o1, 1e8f);
	EXPECT_EQ(o1.ToString(), "100000000");

	OUT::PlainTextOutput o2;
	WriteOperand(o2, 1.5e8f);
	EXPECT_EQ(o2.ToString(), "150000000");

	// The shortest round-trip digits zero-pad past their end: the float
	// nearest 123456789 is 123456792, but its shortest round-trip digits are
	// 12345679, so both literals render "123456790".
	OUT::PlainTextOutput o3;
	WriteOperand(o3, 123456789.0f);
	EXPECT_EQ(o3.ToString(), "123456790");

	OUT::PlainTextOutput o4;
	WriteOperand(o4, 123456792.0f);
	EXPECT_EQ(o4.ToString(), "123456790");

	// The scale-10 side flips scientific.
	OUT::PlainTextOutput o5;
	WriteOperand(o5, 1e9f);
	EXPECT_EQ(o5.ToString(), "1E+09");

	OUT::PlainTextOutput o6;
	WriteOperand(o6, 1.5e9f);
	EXPECT_EQ(o6.ToString(), "1.5E+09");

	OUT::PlainTextOutput o7;
	WriteOperand(o7, 1e16f);
	EXPECT_EQ(o7.ToString(), "1E+16");

	OUT::PlainTextOutput o8;
	WriteOperand(o8, 1.5e16f);
	EXPECT_EQ(o8.ToString(), "1.5E+16");

	OUT::PlainTextOutput o9;
	WriteOperand(o9, 1.2345678e10f);
	EXPECT_EQ(o9.ToString(), "1.2345678E+10");
}

TEST(DisassemblerHelpersTest, WriteOperandFloatRoundTripSmallValuesAndExtremes) {
	OUT::PlainTextOutput o1;
	WriteOperand(o1, 0.0001f);
	EXPECT_EQ(o1.ToString(), "0.0001");

	// Float digits stop at nine: 0.000123456789 renders 0.00012345679.
	OUT::PlainTextOutput o2;
	WriteOperand(o2, 0.000123456789f);
	EXPECT_EQ(o2.ToString(), "0.00012345679");

	OUT::PlainTextOutput o3;
	WriteOperand(o3, 1e-5f);
	EXPECT_EQ(o3.ToString(), "1E-05");

	OUT::PlainTextOutput o4;
	WriteOperand(o4, 1.5e-5f);
	EXPECT_EQ(o4.ToString(), "1.5E-05");

	OUT::PlainTextOutput o5;
	WriteOperand(o5, 1234567.9f);
	EXPECT_EQ(o5.ToString(), "1234567.9");

	// float.Epsilon: the subnormal takes the scientific path.
	OUT::PlainTextOutput o6;
	WriteOperand(o6, 1.401298464324817e-45f);
	EXPECT_EQ(o6.ToString(), "1E-45");

	OUT::PlainTextOutput o7;
	WriteOperand(o7, 3.4028235e38f);
	EXPECT_EQ(o7.ToString(), "3.4028235E+38");

	OUT::PlainTextOutput o8;
	WriteOperand(o8, -3.4028235e38f);
	EXPECT_EQ(o8.ToString(), "-3.4028235E+38");
}

TEST(DisassemblerHelpersTest, WriteOperandDoubleDumpsInfinityAndNaNBytes) {
	OUT::PlainTextOutput output;
	WriteOperand(output, std::numeric_limits<double>::infinity());
	EXPECT_EQ(output.ToString(), "(00 00 00 00 00 00 F0 7F)");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, std::numeric_limits<double>::quiet_NaN());
	EXPECT_EQ(output2.ToString(), "(00 00 00 00 00 00 F8 7F)");
}

TEST(DisassemblerHelpersTest, WriteOperandStringWritesQuotedEscapedLiteral) {
	OUT::PlainTextOutput output;
	WriteOperand(output, std::string_view("abc"));
	EXPECT_EQ(output.ToString(), "\"abc\"");

	OUT::PlainTextOutput output2;
	WriteOperand(output2, std::string_view("a\"b"));
	EXPECT_EQ(output2.ToString(), "\"a\\\"b\"");

	OUT::PlainTextOutput output3;
	WriteOperand(output3, std::string_view("a\nb"));
	EXPECT_EQ(output3.ToString(), "\"a\\nb\"");
}

TEST(DisassemblerHelpersTest, WriteOperandObjectDispatchesOnTheHeldType) {
	OUT::PlainTextOutput output;
	WriteOperand(output, std::any(std::string("abc")));
	EXPECT_EQ(output.ToString(), "\"abc\"");  // delegates to the string overload

	OUT::PlainTextOutput output2;
	WriteOperand(output2, std::any(char16_t('A')));
	EXPECT_EQ(output2.ToString(), "65");  // the char's code unit as a decimal

	OUT::PlainTextOutput output3;
	WriteOperand(output3, std::any(static_cast<char16_t>(0x263A)));
	EXPECT_EQ(output3.ToString(), "9786");

	OUT::PlainTextOutput output4;
	WriteOperand(output4, std::any(1.5f));
	EXPECT_EQ(output4.ToString(), "1.5");  // delegates to the float overload

	OUT::PlainTextOutput output5;
	WriteOperand(output5, std::any(2.5));
	EXPECT_EQ(output5.ToString(), "2.5");  // delegates to the double overload

	OUT::PlainTextOutput output6;
	WriteOperand(output6, std::any(true));
	EXPECT_EQ(output6.ToString(), "true");

	OUT::PlainTextOutput output7;
	WriteOperand(output7, std::any(false));
	EXPECT_EQ(output7.ToString(), "false");
}

TEST(DisassemblerHelpersTest, WriteOperandObjectWritesIntegralDigits) {
	OUT::PlainTextOutput o1;
	WriteOperand(o1, std::any(static_cast<std::int8_t>(-5)));
	EXPECT_EQ(o1.ToString(), "-5");

	OUT::PlainTextOutput o2;
	WriteOperand(o2, std::any(static_cast<std::uint8_t>(200)));
	EXPECT_EQ(o2.ToString(), "200");

	OUT::PlainTextOutput o3;
	WriteOperand(o3, std::any(static_cast<std::int16_t>(-1000)));
	EXPECT_EQ(o3.ToString(), "-1000");

	OUT::PlainTextOutput o4;
	WriteOperand(o4, std::any(static_cast<std::uint16_t>(60000)));
	EXPECT_EQ(o4.ToString(), "60000");

	OUT::PlainTextOutput o5;
	WriteOperand(o5, std::any(static_cast<std::int32_t>(-42)));
	EXPECT_EQ(o5.ToString(), "-42");

	OUT::PlainTextOutput o6;
	WriteOperand(o6, std::any(static_cast<std::uint32_t>(4000000000u)));
	EXPECT_EQ(o6.ToString(), "4000000000");

	OUT::PlainTextOutput o7;
	WriteOperand(o7, std::any(static_cast<std::int64_t>(-9000000000LL)));
	EXPECT_EQ(o7.ToString(), "-9000000000");

	OUT::PlainTextOutput o8;
	WriteOperand(o8, std::any(static_cast<std::uint64_t>(18000000000000000000ULL)));
	EXPECT_EQ(o8.ToString(), "18000000000000000000");
}

TEST(DisassemblerHelpersTest, WriteOperandObjectThrowsOnNull) {
	OUT::PlainTextOutput output;
	EXPECT_THROW(WriteOperand(output, std::any()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// PrimitiveTypeName
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersTest, PrimitiveTypeNameMapsTheBclPrimitives) {
	EXPECT_STREQ(PrimitiveTypeName("System.SByte"), "int8");
	EXPECT_STREQ(PrimitiveTypeName("System.Int16"), "int16");
	EXPECT_STREQ(PrimitiveTypeName("System.Int32"), "int32");
	EXPECT_STREQ(PrimitiveTypeName("System.Int64"), "int64");
	EXPECT_STREQ(PrimitiveTypeName("System.Byte"), "uint8");
	EXPECT_STREQ(PrimitiveTypeName("System.UInt16"), "uint16");
	EXPECT_STREQ(PrimitiveTypeName("System.UInt32"), "uint32");
	EXPECT_STREQ(PrimitiveTypeName("System.UInt64"), "uint64");
	EXPECT_STREQ(PrimitiveTypeName("System.Single"), "float32");
	EXPECT_STREQ(PrimitiveTypeName("System.Double"), "float64");
	EXPECT_STREQ(PrimitiveTypeName("System.Void"), "void");
	EXPECT_STREQ(PrimitiveTypeName("System.Boolean"), "bool");
	EXPECT_STREQ(PrimitiveTypeName("System.String"), "string");
	EXPECT_STREQ(PrimitiveTypeName("System.Char"), "char");
	EXPECT_STREQ(PrimitiveTypeName("System.Object"), "object");
	// The multi-word IL spelling for IntPtr.
	EXPECT_STREQ(PrimitiveTypeName("System.IntPtr"), "native int");
}

TEST(DisassemblerHelpersTest, PrimitiveTypeNameReturnsNullOutsideTheTable) {
	// The C# table has no UIntPtr entry (System.UIntPtr renders by full name).
	EXPECT_EQ(PrimitiveTypeName("System.UIntPtr"), nullptr);
	EXPECT_EQ(PrimitiveTypeName("System.Decimal"), nullptr);
	EXPECT_EQ(PrimitiveTypeName("My.Ns.Type"), nullptr);
	EXPECT_EQ(PrimitiveTypeName(""), nullptr);
}

// ---------------------------------------------------------------------------
// WriteParameterReference (over the real mscorlib fixture)
// ---------------------------------------------------------------------------

TEST(DisassemblerHelpersParameterTest, StaticMethodParameterNameResolves) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	auto stringTok = FindType(f, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto copyTok = FindMethodToken(f, stringTok, "Copy");
	ASSERT_NE(copyTok, 0u);

	// String.Copy(String str) is static: IL index 0 is the first declared
	// parameter, named "str".
	OUT::PlainTextOutput output;
	WriteParameterReference(output, f, copyTok, 0);
	EXPECT_EQ(output.ToString(), "str");

	// An index past the parameter count falls back to the bare index.
	OUT::PlainTextOutput output2;
	WriteParameterReference(output2, f, copyTok, 4);
	EXPECT_EQ(output2.ToString(), "4");
}

TEST(DisassemblerHelpersParameterTest, InstanceMethodSkipsTheImplicitThis) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	auto stringTok = FindType(f, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto substringTok = FindMethodToken(f, stringTok, "Substring");
	ASSERT_NE(substringTok, 0u);

	// String.Substring(int startIndex) is an instance method: IL index 0 is
	// the implicit `this` (no Param row -- the bare-index fallback), IL index
	// 1 is the first declared parameter, named "startIndex".
	OUT::PlainTextOutput output;
	WriteParameterReference(output, f, substringTok, 0);
	EXPECT_EQ(output.ToString(), "0");

	OUT::PlainTextOutput output2;
	WriteParameterReference(output2, f, substringTok, 1);
	EXPECT_EQ(output2.ToString(), "startIndex");
}

TEST(DisassemblerHelpersParameterTest, InvalidMethodTokenFallsBackToTheIndex) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	// A token from the wrong table (or 0) never throws; the fallback writes the
	// bare index.
	OUT::PlainTextOutput output;
	WriteParameterReference(output, f, 0, 2);
	EXPECT_EQ(output.ToString(), "2");
}

// ---------------------------------------------------------------------------
// The C# `WriteTo(this ExceptionRegion, MetadataFile, MetadataGenericContext,
// ITextOutput)` extension (DisassemblerHelpers.cs line 72) -- the
// exception-region-to-text writer the MethodBodyDisassembler's
// WriteExceptionHandlers consumes (the gnhf-139..142 deferral, now unblocked
// by the gnhf-143 EntityHandle.WriteTo). The C++ model is the
// Metadata::ExceptionHandlerClause the method-body decoder produces; the C#
// FilterOffset == -1 sentinel ports to the Kind == Filter discriminant, and
// the C# CatchType.IsNil guard ports to a zero catch token.
// ---------------------------------------------------------------------------

namespace {
MD::ExceptionHandlerClause MakeClause(MD::ExceptionHandlerKind kind,
    std::uint32_t tryOffset, std::uint32_t tryLength,
    std::uint32_t handlerOffset, std::uint32_t handlerLength,
    std::uint32_t catchTokenOrFilterOffset = 0) {
    MD::ExceptionHandlerClause clause;
    clause.Kind = kind;
    clause.TryOffset = tryOffset;
    clause.TryLength = tryLength;
    clause.HandlerOffset = handlerOffset;
    clause.HandlerLength = handlerLength;
    clause.ClassTokenOrFilterOffset = catchTokenOrFilterOffset;
    return clause;
}

MD::MetadataGenericContext NullContext() { return MD::MetadataGenericContext{}; }
} // namespace

TEST(DisassemblerHelpersExceptionRegionTest, CatchClauseRendersTryKindTypeAndHandler) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());
	std::uint32_t objectToken = FindType(f, "System", "Object");
	ASSERT_NE(objectToken, 0u);

	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Catch, 0x0000, 0x0008, 0x0008, 0x0008,
	            objectToken),
	    f, NullContext(), output);
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0008 catch System.Object IL_0008-IL_0010");
}

TEST(DisassemblerHelpersExceptionRegionTest, CatchTypeRefRendersAssemblyQualifiedType) {
	const char* sysPath =
#if defined(_WIN32)
	    "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
	    "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
#else
	    "/usr/lib/mono/4.5/System.dll";
#endif
	if (!std::filesystem::exists(sysPath)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(sysPath);
	ASSERT_TRUE(f.IsValid());
	// System.dll's TypeRef to System.String carries an AssemblyRef scope, so
	// the catch-type rendering is the classic `[mscorlib]System.String`.
	std::uint32_t stringRef = 0;
	for (std::uint32_t row = 1;; row++) {
		std::uint32_t token = (0x01u << 24) | row;
		auto info = f.GetTypeRefNameInfo(token);
		if (!info) break;
		if (info->Namespace == "System" && info->Name == "String") {
			stringRef = token;
			break;
		}
	}
	ASSERT_NE(stringRef, 0u) << "System.dll must carry a System.String TypeRef";

	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Catch, 0x0000, 0x0004, 0x0004, 0x0004,
	            stringRef),
	    f, NullContext(), output);
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0004 catch [mscorlib]System.String IL_0004-IL_0008");
}

TEST(DisassemblerHelpersExceptionRegionTest, FilterClauseRendersFilterAndHandlerMarker) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Filter, 0x0000, 0x0004, 0x0006, 0x000A,
	            0x0004),
	    f, NullContext(), output);
	// The C# body writes " handler " (with its trailing space) and then the
	// unconditional ' ' before the handler range -- the doubled space is the
	// C# verbatim shape.
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0004 filter IL_0004 handler  IL_0006-IL_0010");
}

TEST(DisassemblerHelpersExceptionRegionTest, FinallyClauseRendersNoType) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Finally, 0x0000, 0x0004, 0x0004, 0x0004),
	    f, NullContext(), output);
	// Neither the filter nor the catch-type arm fires, so the unconditional
	// pre-handler space is the only one (the C# verbatim shape).
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0004 finally IL_0004-IL_0008");
}

TEST(DisassemblerHelpersExceptionRegionTest, FaultClauseRendersNoType) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());

	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Fault, 0x0000, 0x0004, 0x0004, 0x0004),
	    f, NullContext(), output);
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0004 fault IL_0004-IL_0008");
}

TEST(DisassemblerHelpersExceptionRegionTest, CatchClauseWithNilTypeRendersNoType) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());
	// The C# `if (!exceptionHandler.CatchType.IsNil)` guard: a nil catch type
	// (corrupt metadata) skips the space + type, leaving the unconditional
	// handler-range space only.
	OUT::PlainTextOutput output;
	WriteTo(MakeClause(MD::ExceptionHandlerKind::Catch, 0x0000, 0x0004, 0x0004, 0x0004, 0),
	    f, NullContext(), output);
	EXPECT_EQ(output.ToString(),
		".try IL_0000-IL_0004 catch IL_0004-IL_0008");
}

TEST(DisassemblerHelpersExceptionRegionTest, RealMethodBodiesRenderTheirRegions) {
	const char* path = FixturePath();
	if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
	MD::MetadataFile f(path);
	ASSERT_TRUE(f.IsValid());
	// A real EH-bearing method body: every region renders the ".try " prefix
	// and the handler-range suffix consistent with the decoded clause.
	bool sawRegion = false;
	for (const auto& t : f.TypeDefs()) {
		for (const auto& m : f.GetMethods(t.Token)) {
			if (m.RVA == 0) continue;
			auto body = f.GetMethodBody(m.RVA);
			if (!body.IsValid() || body.Handlers().empty()) continue;
			for (const auto& clause : body.Handlers()) {
				OUT::PlainTextOutput output;
				WriteTo(clause, f, NullContext(), output);
				std::string text = output.ToString();
				EXPECT_TRUE(text.rfind(".try ", 0) == 0) << text;
				// "handler" only appears in the filter arm of the C# body.
				if (clause.Kind == MD::ExceptionHandlerKind::Filter) {
					EXPECT_NE(text.find(" handler "), std::string::npos) << text;
				} else {
					EXPECT_EQ(text.find(" handler "), std::string::npos) << text;
				}
				sawRegion = true;
			}
			if (sawRegion) break;
		}
		if (sawRegion) break;
	}
	ASSERT_TRUE(sawRegion) << "mscorlib must carry EH-bearing method bodies";
}

// ---------------------------------------------------------------------------
// SubsystemToString / CorFlagsToString -- the System.Reflection
// .PortableExecutable enum ToString semantics the WriteModuleHeader
// .subsystem/.corflags comments render. The matrices below are probed
// against the real .NET 10 enums (the mscorlib/System.Runtime renders pin
// the real-fixture values; these pin the full spelling rules).
// ---------------------------------------------------------------------------
TEST(DisassemblerHelpersTest, SubsystemToStringMatchesNetEnum)
{
    // The named members (the probe-verified .NET spellings; note OS2Cui,
    // PosixCui, and the EFI/Xbox family).
    EXPECT_EQ(SubsystemToString(0), "Unknown");
    EXPECT_EQ(SubsystemToString(1), "Native");
    EXPECT_EQ(SubsystemToString(2), "WindowsGui");
    EXPECT_EQ(SubsystemToString(3), "WindowsCui");
    EXPECT_EQ(SubsystemToString(5), "OS2Cui");
    EXPECT_EQ(SubsystemToString(7), "PosixCui");
    EXPECT_EQ(SubsystemToString(8), "NativeWindows");
    EXPECT_EQ(SubsystemToString(9), "WindowsCEGui");
    EXPECT_EQ(SubsystemToString(10), "EfiApplication");
    EXPECT_EQ(SubsystemToString(11), "EfiBootServiceDriver");
    EXPECT_EQ(SubsystemToString(12), "EfiRuntimeDriver");
    EXPECT_EQ(SubsystemToString(13), "EfiRom");
    EXPECT_EQ(SubsystemToString(14), "Xbox");
    EXPECT_EQ(SubsystemToString(16), "WindowsBootApplication");
    // Unnamed values render the decimal (never negative -- ushort-backed).
    EXPECT_EQ(SubsystemToString(4), "4");
    EXPECT_EQ(SubsystemToString(17), "17");
    EXPECT_EQ(SubsystemToString(0xFFFFu), "65535");
}

TEST(DisassemblerHelpersTest, CorFlagsToStringMatchesNetFlagsFormat)
{
    // The named flags.
    EXPECT_EQ(CorFlagsToString(0x1), "ILOnly");
    EXPECT_EQ(CorFlagsToString(0x2), "Requires32Bit");
    EXPECT_EQ(CorFlagsToString(0x4), "ILLibrary");
    EXPECT_EQ(CorFlagsToString(0x8), "StrongNameSigned");
    EXPECT_EQ(CorFlagsToString(0x10), "NativeEntryPoint");
    EXPECT_EQ(CorFlagsToString(0x10000), "TrackDebugData");
    EXPECT_EQ(CorFlagsToString(0x20000), "Prefers32Bit");
    // Exact unions join the names ascending by value with ", ".
    EXPECT_EQ(CorFlagsToString(0x9), "ILOnly, StrongNameSigned");
    EXPECT_EQ(CorFlagsToString(0x11), "ILOnly, NativeEntryPoint");
    EXPECT_EQ(CorFlagsToString(0xC), "ILLibrary, StrongNameSigned");
    EXPECT_EQ(CorFlagsToString(0x10009),
        "ILOnly, StrongNameSigned, TrackDebugData");
    EXPECT_EQ(CorFlagsToString(0x20009),
        "ILOnly, StrongNameSigned, Prefers32Bit");
    EXPECT_EQ(CorFlagsToString(0x2000C),
        "ILLibrary, StrongNameSigned, Prefers32Bit");
    // An unmatched leftover bit discards the names and renders the FULL
    // value in decimal (the .NET flags-format fallback -- probed:
    // (CorFlags)0x40000009 renders "1073741833", not
    // "ILOnly, 1073741824").
    EXPECT_EQ(CorFlagsToString(0x40000009), "1073741833");
    EXPECT_EQ(CorFlagsToString(0x40000008), "1073741832");
    EXPECT_EQ(CorFlagsToString(0x40000001), "1073741825");
    EXPECT_EQ(CorFlagsToString(0x40000000), "1073741824");
    // Zero renders "0" (CorFlags carries no None member).
    EXPECT_EQ(CorFlagsToString(0), "0");
}

