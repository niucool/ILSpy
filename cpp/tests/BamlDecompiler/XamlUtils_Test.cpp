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

// Tests for the ported self-contained members of ICSharpCode.BamlDecompiler
// XamlUtils (cpp/BamlDecompiler/Xaml/XamlUtils.hpp): the ReadXamlDouble
// tagged/scaled decode, the "{}" curly-brace escape, and the \uXXXX name
// escaper. Every expectation is pinned against the REAL internal
// ICSharpCode.BamlDecompiler classes driven through reflection by the gold
// probe (C:\temp-probe\PathProbe\Program.cs): the P1 ids refer to the probe's
// ReadXamlDouble output lines (raw IEEE-754 bit patterns dumped through
// BitConverter.DoubleToInt64Bits), the P2 ids to the Escape/EscapeName lines.

#include "BamlDecompiler/Xaml/XamlUtils.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace Xaml = ILSpy::BamlDecompiler::Xaml;

std::int64_t Bits(double d)
{
	std::int64_t bits;
	std::memcpy(&bits, &d, sizeof(bits));
	return bits;
}

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

TEST(XamlUtilsTest, ReadXamlDoubleTaggedConstants)
{
	// P1a/P1b/P1c: tags 1/2/3 are the 0/1/-1 constants.
	{
		const std::uint8_t bytes[] = {1, 0};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), 0);
	}
	{
		const std::uint8_t bytes[] = {2, 0};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), 4607182418800017408LL);
	}
	{
		const std::uint8_t bytes[] = {3, 0};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), -4616189618054758400LL);
	}
}

TEST(XamlUtilsTest, ReadXamlDoubleTaggedScaledAndRaw)
{
	// P1d: tag 4 falls through to the scaled int32 read (11700684 ->
	// 11.700684, the C# comment's canonical value).
	{
		const std::uint8_t bytes[] = {4, 0xCC, 0x89, 0xB2, 0x00};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), 4622776517567555891LL);
	}
	// P1e: tag 4 with int32 -1.
	{
		const std::uint8_t bytes[] = {4, 0xFF, 0xFF, 0xFF, 0xFF};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), -4706042843746669171LL);
	}
	// P1f: tag 5 is a raw IEEE double (2.5).
	{
		const std::uint8_t bytes[] = {5, 0, 0, 0, 0, 0, 0, 0x04, 0x40};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader)), 4612811918334230528LL);
	}
}

TEST(XamlUtilsTest, ReadXamlDoubleScaledForm)
{
	// P1g-P1l: scaledInt=true reads the int32 directly.
	const struct {
		std::int32_t input;
		std::int64_t bits;
	} cases[] = {
		{11700684, 4622776517567555891LL},
		{1, 4517329193108106637LL},
		{-11700684, -4600595519287219917LL},
		{-2147483647 - 1, -4566431255298183795LL},
		{0, 0},
		{42, 4541322930770855881LL},
	};
	for (const auto& c : cases) {
		std::uint8_t bytes[4];
		std::memcpy(bytes, &c.input, sizeof(bytes));
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(Bits(Xaml::ReadXamlDouble(reader, true)), c.bits) << c.input;
	}
}

TEST(XamlUtilsTest, ReadXamlDoubleScaledRendersThroughTheRoundTripFormat)
{
	// P1m-P1p: the scaled values the path renders compose with the "R"
	// format -- 1/1e6 is not the double literal 1e-6's bits * ... but the
	// division result, which round-trips as "1E-06".
	{
		std::int32_t v = 11700684;
		std::uint8_t bytes[4];
		std::memcpy(bytes, &v, sizeof(bytes));
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(ILSpy::Decompiler::Disassembler::FormatRoundTrip(
				Xaml::ReadXamlDouble(reader, true)),
			"11.700684");
	}
	{
		std::int32_t v = 1;
		std::uint8_t bytes[4];
		std::memcpy(bytes, &v, sizeof(bytes));
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(ILSpy::Decompiler::Disassembler::FormatRoundTrip(
				Xaml::ReadXamlDouble(reader, true)),
			"1E-06");
	}
	{
		std::int32_t v = 42;
		std::uint8_t bytes[4];
		std::memcpy(bytes, &v, sizeof(bytes));
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(ILSpy::Decompiler::Disassembler::FormatRoundTrip(
				Xaml::ReadXamlDouble(reader, true)),
			"4.2E-05");
	}
	{
		std::int32_t v = -11700684;
		std::uint8_t bytes[4];
		std::memcpy(bytes, &v, sizeof(bytes));
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_EQ(ILSpy::Decompiler::Disassembler::FormatRoundTrip(
				Xaml::ReadXamlDouble(reader, true)),
			"-11.700684");
	}
}

TEST(XamlUtilsTest, ReadXamlDoubleUnknownTagThrows)
{
	// P1q/P1r/P1s: tags other than 1-5 throw InvalidDataException, which
	// the port maps to std::runtime_error with the exact message.
	for (std::uint8_t tag : {std::uint8_t{0}, std::uint8_t{6}, std::uint8_t{255}}) {
		const std::uint8_t bytes[] = {tag, 0};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_STREQ(ThrowsMessage<std::runtime_error>([&] {
			Xaml::ReadXamlDouble(reader);
		}).c_str(), "Unknown double type.") << +tag;
	}
}

TEST(XamlUtilsTest, ReadXamlDoubleTruncatedReadsThrow)
{
	// P1t: end of stream at the tag byte (the empty stream).
	{
		Baml::BamlBinaryReader reader(nullptr, 0);
		EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
			Xaml::ReadXamlDouble(reader);
		}).c_str(), "Unable to read beyond the end of the stream.");
	}
	// P1u: tag 5 with a short double payload.
	{
		const std::uint8_t bytes[] = {5, 1, 2, 3, 4, 5};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
			Xaml::ReadXamlDouble(reader);
		}).c_str(), "Unable to read beyond the end of the stream.");
	}
	// P1v: tag 4 with a short int32 payload.
	{
		const std::uint8_t bytes[] = {4, 1, 2, 3};
		Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
		EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
			Xaml::ReadXamlDouble(reader);
		}).c_str(), "Unable to read beyond the end of the stream.");
	}
}

TEST(XamlUtilsTest, EscapePrefixesTheCurlyBrace)
{
	// P2a/P2b/P2c/P2d.
	EXPECT_EQ(Xaml::Escape("{x}"), "{}{x}");
	EXPECT_EQ(Xaml::Escape(""), "");
	EXPECT_EQ(Xaml::Escape("abc"), "abc");
	EXPECT_EQ(Xaml::Escape("}{"), "}{");
}

TEST(XamlUtilsTest, EscapeNamePassesPlainNamesThrough)
{
	// P2e/P2k/P2o: ordinary names (including CJK letters) pass through
	// unchanged.
	EXPECT_EQ(Xaml::EscapeName("abc"), "abc");
	EXPECT_EQ(Xaml::EscapeName(""), "");
	EXPECT_EQ(Xaml::EscapeName("\xe4\xb8\xad\xe6\x96\x87"), "\xe4\xb8\xad\xe6\x96\x87");
	// U+00A1 is the first C1-adjacent unit that is neither a control nor
	// a whitespace unit -- it passes through raw (U+009E is a C1 control
	// and DOES escape, like U+009F).
	EXPECT_EQ(Xaml::EscapeName("a\xc2\xa1" "b"), "a\xc2\xa1" "b");
}

TEST(XamlUtilsTest, EscapeNameEscapesWhitespaceAndControlUnits)
{
	// P2f: the space.
	EXPECT_EQ(Xaml::EscapeName("a b"), "a\\u0020b");
	// P2g: a C0 control.
	EXPECT_EQ(Xaml::EscapeName("a\x01" "b"), "a\\u0001b");
	// P2h: NBSP is a whitespace unit.
	EXPECT_EQ(Xaml::EscapeName("a\xc2\xa0" "b"), "a\\u00a0b");
	// P2i: the tab.
	EXPECT_EQ(Xaml::EscapeName("a\tb"), "a\\u0009b");
	// P2l: DEL.
	EXPECT_EQ(Xaml::EscapeName("a\x7f" "b"), "a\\u007fb");
	// P2m: U+009F is the LAST control unit.
	EXPECT_EQ(Xaml::EscapeName("a\xc2\x9f" "b"), "a\\u009fb");
	// P2q: the carriage return.
	EXPECT_EQ(Xaml::EscapeName("a\rb"), "a\\u000db");
	// P2r: NEL is a control AND a whitespace unit.
	EXPECT_EQ(Xaml::EscapeName("a\xc2\x85" "b"), "a\\u0085b");
}

TEST(XamlUtilsTest, EscapeNameEscapesSurrogateHalves)
{
	// P2j: a supplementary-plane character walks as its TWO UTF-16
	// surrogate halves, each escaped (U+1F600 -> \ud83d\ude00).
	EXPECT_EQ(Xaml::EscapeName("\xf0\x9f\x98\x80"), "\\ud83d\\ude00");
}

TEST(XamlUtilsTest, EscapeNameBuilderOverloadComposes)
{
	// The StringBuilder overload appends (the C# handlers build composed
	// names through it).
	std::string sb = "x=";
	Xaml::EscapeName(sb, "a b");
	EXPECT_EQ(sb, "x=a\\u0020b");
	Xaml::EscapeName(sb, "c");
	EXPECT_EQ(sb, "x=a\\u0020bc");
	std::string& same = Xaml::EscapeName(sb, "\xc2\xa0");
	EXPECT_EQ(&same, &sb);
	EXPECT_EQ(sb, "x=a\\u0020bc\\u00a0");
}

} // namespace
