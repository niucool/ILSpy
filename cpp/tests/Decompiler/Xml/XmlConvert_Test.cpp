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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the port-authored System.Xml XmlConvert stand-in
// (cpp/Decompiler/Xml/XmlConvert.hpp): EncodeLocalName and VerifyNCName with
// the probed XML 1.0 4th-edition NCName tables. Every expectation is pinned
// against the REAL .NET 10 System.Xml driven by the gold probe
// (C:\temp-probe\XmlNameProbe\Program.cs): the C*/S_us_* ids refer to that
// probe's output lines (the full-BMP table sweep produced the range tables
// embedded in the port).

#include "Decompiler/Xml/XmlConvert.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

namespace Xml = ILSpy::Decompiler::Xml;

template <typename TException, typename TFunc>
std::string ThrowsMessage(TFunc f)
{
	try {
		f();
	} catch (const TException& ex) {
		return ex.what();
	}
	return "<no throw>";
}

TEST(XmlConvertTest, EncodeLocalNameIdentity)
{
	// C_Foo/C_FooBar123: valid NCNames pass through unchanged.
	EXPECT_EQ(Xml::EncodeLocalName("Foo"), std::string("Foo"));
	EXPECT_EQ(Xml::EncodeLocalName("FooBar123"), std::string("FooBar123"));
	// Underscores are start chars and need no escaping by themselves.
	EXPECT_EQ(Xml::EncodeLocalName("_"), std::string("_"));
	EXPECT_EQ(Xml::EncodeLocalName("__"), std::string("__"));
	EXPECT_EQ(Xml::EncodeLocalName("_x"), std::string("_x"));
	EXPECT_EQ(Xml::EncodeLocalName("x"), std::string("x"));
	EXPECT_EQ(Xml::EncodeLocalName("x_x"), std::string("x_x"));
	EXPECT_EQ(Xml::EncodeLocalName("a_b"), std::string("a_b"));
	EXPECT_EQ(Xml::EncodeLocalName("_ab"), std::string("_ab"));
	// '.' and '-' are continuation chars.
	EXPECT_EQ(Xml::EncodeLocalName("a.b"), std::string("a.b"));
	EXPECT_EQ(Xml::EncodeLocalName("a-"), std::string("a-"));
	EXPECT_EQ(Xml::EncodeLocalName("a0"), std::string("a0"));
	// The empty name returns unchanged (the C# IsNullOrEmpty early out).
	EXPECT_EQ(Xml::EncodeLocalName(""), std::string(""));
	// Letters outside ASCII that the XML 1.0 4th-edition table keeps.
	EXPECT_EQ(Xml::EncodeLocalName("\xE4\xB8\xAD"), std::string("\xE4\xB8\xAD")); // U+4E2D
	EXPECT_EQ(Xml::EncodeLocalName("\xD7\x90"), std::string("\xD7\x90")); // U+05D0
	EXPECT_EQ(Xml::EncodeLocalName("\xC3\x80"), std::string("\xC3\x80")); // U+00C0
}

TEST(XmlConvertTest, EncodeLocalNameEscapes)
{
	// C_9lives/C_Foo[0020]Bar/C_a[003A]b/C_[002B]: every escape is
	// "_x" + uppercase X4 hex + "_".
	EXPECT_EQ(Xml::EncodeLocalName("9lives"), std::string("_x0039_lives"));
	EXPECT_EQ(Xml::EncodeLocalName("Foo Bar"), std::string("Foo_x0020_Bar"));
	EXPECT_EQ(Xml::EncodeLocalName("a:b"), std::string("a_x003A_b"));
	EXPECT_EQ(Xml::EncodeLocalName("+"), std::string("_x002B_"));
	EXPECT_EQ(Xml::EncodeLocalName("a+b"), std::string("a_x002B_b"));
	EXPECT_EQ(Xml::EncodeLocalName(" "), std::string("_x0020_"));
	EXPECT_EQ(Xml::EncodeLocalName("a "), std::string("a_x0020_"));
	EXPECT_EQ(Xml::EncodeLocalName(" a"), std::string("_x0020_a"));
	EXPECT_EQ(Xml::EncodeLocalName("a b c"), std::string("a_x0020_b_x0020_c"));
	EXPECT_EQ(Xml::EncodeLocalName("\t"), std::string("_x0009_"));
	EXPECT_EQ(Xml::EncodeLocalName("a\tb"), std::string("a_x0009_b"));
	// C_[0000]/C_a[0000]b: the NUL escapes like any invalid unit.
	EXPECT_EQ(Xml::EncodeLocalName(std::string("\0", 1)), std::string("_x0000_"));
	EXPECT_EQ(Xml::EncodeLocalName(std::string("a\0b", 3)), std::string("a_x0000_b"));
	// C_a[07F]b: DEL (0x7F) escapes.
	EXPECT_EQ(Xml::EncodeLocalName("a\x7F" "b"), std::string("a_x007F_b"));
	// C_[002E]a/C_[002D]a: '.' and '-' are not start chars.
	EXPECT_EQ(Xml::EncodeLocalName(".a"), std::string("_x002E_a"));
	EXPECT_EQ(Xml::EncodeLocalName("-a"), std::string("_x002D_a"));
	// C_a[005C]b/C_a[002F]b: backslash and slash.
	EXPECT_EQ(Xml::EncodeLocalName("a\\b"), std::string("a_x005C_b"));
	EXPECT_EQ(Xml::EncodeLocalName("a/b"), std::string("a_x002F_b"));
}

TEST(XmlConvertTest, EncodeLocalNameNonAsciiTables)
{
	// The XML 1.0 4th-edition holes probed unit-by-unit: U+00B5 (micro),
	// U+00D7 (times), U+00B7 (middle dot -- continuation only), U+0301
	// (combining acute -- continuation only), U+200B, U+FDFD, U+E000,
	// U+FFFD and U+FFFE all escape; U+06DD (Arabic end-of-ayah) escapes.
	EXPECT_EQ(Xml::EncodeLocalName("\xC2\xB5"), std::string("_x00B5_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xC3\x97"), std::string("_x00D7_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xC2\xB7"), std::string("_x00B7_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xCC\x81"), std::string("_x0301_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xE2\x80\x8B"), std::string("_x200B_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xEF\xB7\xBD"), std::string("_xFDFD_")); // U+FDFD
	EXPECT_EQ(Xml::EncodeLocalName("\xEE\x80\x80"), std::string("_xE000_"));
	EXPECT_EQ(Xml::EncodeLocalName("\xEF\xBF\xBD"), std::string("_xFFFD_")); // U+FFFD
	EXPECT_EQ(Xml::EncodeLocalName("\xDB\x9D"), std::string("_x06DD_")); // U+06DD
	// U+00B7 and U+0301 in continuation position stay (a literal 'a' makes
	// the accent render; only the escape behavior is asserted).
	EXPECT_EQ(Xml::EncodeLocalName("a\xC2\xB7" "b"), std::string("a\xC2\xB7" "b"));
	// The CJK block ends at U+9FA5: U+9FA6 escapes.
	EXPECT_EQ(Xml::EncodeLocalName("\xE9\xBE\xA5"), std::string("\xE9\xBE\xA5")); // U+9FA5 kept
	EXPECT_EQ(Xml::EncodeLocalName("\xE9\xBE\xA6"), std::string("_x9FA6_")); // U+9FA6 escaped
	// The Hangul block ends at U+D7A3 (inclusive); U+D7A4 escapes.
	EXPECT_EQ(Xml::EncodeLocalName("\xED\x9E\xA3"), std::string("\xED\x9E\xA3")); // U+D7A3 kept
	EXPECT_EQ(Xml::EncodeLocalName("\xED\x9E\xA4"), std::string("_xD7A4_")); // U+D7A4 escaped
}

TEST(XmlConvertTest, EncodeLocalNameSurrogates)
{
	// A supplementary code point escapes with X8 hex of the whole point
	// (the .NET surrogate-pair arm); a LONE surrogate half escapes as X4 of
	// the unit itself.
	EXPECT_EQ(Xml::EncodeLocalName("\xF0\x9F\x98\x80"), std::string("_x0001F600_")); // U+1F600
	EXPECT_EQ(Xml::EncodeLocalName("a\xF0\x9F\x98\x80" "b"), std::string("a_x0001F600_b"));
	EXPECT_EQ(Xml::EncodeLocalName("\xF0\x90\x80\x80"), std::string("_x00010000_")); // U+10000
	EXPECT_EQ(Xml::EncodeLocalName("\xF4\x8F\xBF\xBF"), std::string("_x0010FFFF_")); // U+10FFFF
	// A lone high surrogate (WTF-8 bytes): the per-unit X4 escape.
	EXPECT_EQ(Xml::EncodeLocalName("\xED\xA0\x80"), std::string("_xD800_"));
	// The U+1F600 pair spelled as WTF-8 surrogate halves combines first.
	EXPECT_EQ(Xml::EncodeLocalName("\xED\xA0\xBD\xED\xB8\x80"), std::string("_x0001F600_"));
}

TEST(XmlConvertTest, EncodeLocalNameUnderscoreSelfEscape)
{
	// S_us_*: an underscore that would introduce an escape sequence is
	// itself escaped as _x005F_ so decoding is unambiguous. The recognizer
	// needs '_' + [Xx] + 4 hex + ('_' or 4 hex + '_').
	EXPECT_EQ(Xml::EncodeLocalName("_x0041_"), std::string("_x005F_x0041_"));
	EXPECT_EQ(Xml::EncodeLocalName("a_x0041_"), std::string("a_x005F_x0041_"));
	EXPECT_EQ(Xml::EncodeLocalName("_X0041_"), std::string("_x005F_X0041_"));
	EXPECT_EQ(Xml::EncodeLocalName("_xabcd_"), std::string("_x005F_xabcd_"));
	EXPECT_EQ(Xml::EncodeLocalName("_x0020_"), std::string("_x005F_x0020_"));
	EXPECT_EQ(Xml::EncodeLocalName("_x0041__"), std::string("_x005F_x0041__"));
	// Without the trailing underscore (or with non-hex digits) the sequence
	// is not ambiguous and stays.
	EXPECT_EQ(Xml::EncodeLocalName("_x0041"), std::string("_x0041"));
	EXPECT_EQ(Xml::EncodeLocalName("_x0041x"), std::string("_x0041x"));
	EXPECT_EQ(Xml::EncodeLocalName("_x004"), std::string("_x004"));
	EXPECT_EQ(Xml::EncodeLocalName("_x00zz"), std::string("_x00zz"));
	EXPECT_EQ(Xml::EncodeLocalName("_xG041"), std::string("_xG041"));
	EXPECT_EQ(Xml::EncodeLocalName("a_x0041b"), std::string("a_x0041b"));
	EXPECT_EQ(Xml::EncodeLocalName("a_x004b"), std::string("a_x004b"));
	EXPECT_EQ(Xml::EncodeLocalName("x_x0041"), std::string("x_x0041"));
	// "_x_x0041": the '_' before 'x' is followed by '_', not hex.
	EXPECT_EQ(Xml::EncodeLocalName("_x_x0041"), std::string("_x_x0041"));
}

TEST(XmlConvertTest, VerifyNCNameMessages)
{
	// X9_tab_first/X9_tab_mid: the raw unit appears in the message.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName("\tb");
	}), std::string("Name cannot begin with the '\t' character, hexadecimal value 0x09."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName("a\tb");
	}), std::string("The '\t' character, hexadecimal value 0x09, cannot be included in a name."));
	// X9_colon_first: the colon shape.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName(":a");
	}), std::string("Name cannot begin with the ':' character, hexadecimal value 0x3A."));
	// X9_nul_mid: the NUL unit renders as '.' with value 0x00.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName(std::string("a\0b", 3));
	}), std::string("The '.' character, hexadecimal value 0x00, cannot be included in a name."));
	// X9_verifyncname_empty: the empty name is an ArgumentException.
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::VerifyNCName("");
	}), std::string("The value cannot be an empty string. (Parameter 'name')"));
	// The valid name returns unchanged.
	EXPECT_EQ(Xml::VerifyNCName("Foo_Bar"), std::string("Foo_Bar"));
}

TEST(XmlConvertTest, VerifyNCNameSurrogateMessages)
{
	// X9_astral_first/X9_astral_mid: a real surrogate pair reports the
	// combined code point and the pair renders as the supplementary
	// character.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName("\xF0\x9F\x98\x80" "b");
	}), std::string("Name cannot begin with the '\xF0\x9F\x98\x80' character, hexadecimal value 0x1F600."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName("a\xF0\x9F\x98\x80" "b");
	}), std::string("The '\xF0\x9F\x98\x80' character, hexadecimal value 0x1F600, cannot be included in a name."));
	// X9_lone_surrogate_first: a lone high surrogate followed by a
	// non-low-surrogate unit takes the pair-form branch anyway and prints
	// the un-validated CombineSurrogateChar garbage (the .NET quirk the
	// port reproduces; the lone surrogate reaches the port as WTF-8 bytes
	// and the display string keeps them).
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyNCName("\xED\xA0\x80" "b");
	}), std::string("Name cannot begin with the '\xED\xA0\x80" "b' character, hexadecimal value 0xFFFF2462."));
}

// (probe: "leaf value members" VerifyName[...]) XmlConvert.VerifyName -- the
// QName-style name validation XDocumentType uses (colons allowed anywhere,
// unlike the NCName rule).
TEST(XmlConvertTest, VerifyNameQNameRules)
{
	// The colon acceptance: leading, trailing, multiple.
	EXPECT_EQ(Xml::VerifyName("ns:e"), std::string("ns:e"));
	EXPECT_EQ(Xml::VerifyName(":a"), std::string(":a"));
	EXPECT_EQ(Xml::VerifyName("a:"), std::string("a:"));
	EXPECT_EQ(Xml::VerifyName("a:b:c"), std::string("a:b:c"));
	EXPECT_EQ(Xml::VerifyName("_x"), std::string("_x"));
	EXPECT_EQ(Xml::VerifyName("\xC3\xA9x"), std::string("\xC3\xA9x"));
	EXPECT_EQ(Xml::VerifyName("x\xC3\xA9y"), std::string("x\xC3\xA9y"));

	// The rejection shapes match VerifyNCName.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyName("a b");
	}), std::string("The ' ' character, hexadecimal value 0x20, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::VerifyName("1bad");
	}), std::string("Name cannot begin with the '1' character, hexadecimal value 0x31."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::VerifyName("");
	}), std::string("The value cannot be an empty string. (Parameter 'name')"));
}

} // namespace
