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

// Tests for the ported ICSharpCode.BamlDecompiler XamlPathDeserializer
// (cpp/BamlDecompiler/Xaml/XamlPathDeserializer.hpp): the WPF binary Geometry
// path decode. Every expectation is pinned against the REAL internal
// ICSharpCode.BamlDecompiler class driven through reflection by the gold probe
// (C:\temp-probe\PathProbe\Program.cs); the P3 ids refer to that probe's
// Deserialize output lines over the identical crafted op streams.
//
// Stream geometry recap: each op byte's low nibble is the opcode, the high
// nibble's 0x40/0x80 bits select the scaled int32 coordinate form, and the
// 0x10/0x20 bits are the (rendered-nowhere) filled/closed/stroked/smoothJoin
// flags. Double tags: 1 -> 0, 2 -> 1, 3 -> -1, 4 -> scaled int32, 5 -> raw
// IEEE double.

#include "BamlDecompiler/Xaml/XamlPathDeserializer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace Xaml = ILSpy::BamlDecompiler::Xaml;

std::vector<std::uint8_t> HexBytes(const char* hex)
{
	std::vector<std::uint8_t> bytes;
	const char* p = hex;
	while (*p) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		bytes.push_back(static_cast<std::uint8_t>(std::strtoul(p,
			const_cast<char**>(&p), 16)));
	}
	return bytes;
}

std::string Deser(const std::vector<std::uint8_t>& bytes)
{
	Baml::BamlBinaryReader reader(bytes.data(), bytes.size());
	return Xaml::XamlPathDeserializer::Deserialize(reader);
}

std::string DeserHex(const char* hex)
{
	return Deser(HexBytes(hex));
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

TEST(XamlPathDeserializerTest, ClosedOnlyRendersTheEmptyPath)
{
	// P3a.
	EXPECT_EQ(DeserHex("08"), "");
}

TEST(XamlPathDeserializerTest, BeginFigureRendersTaggedAndScaledPoints)
{
	// P3b: tagged coordinates (1,-1).
	EXPECT_EQ(DeserHex("00 02 03 08"), "M1,-1");
	// P3c: the op byte carries filled+closed (ignored) and the scaled X
	// form (11700684 -> 11.700684); Y is tagged 0.
	EXPECT_EQ(DeserHex("70 CC 89 B2 00 01 08"), "M11.700684,0");
	// P3ac: only Y scaled (10); X tagged (1).
	EXPECT_EQ(DeserHex("80 02 80 96 98 00 08"), "M1,10");
	// P3ab: both scaled -- 1/1e6 and 2/1e6 render scientifically.
	EXPECT_EQ(DeserHex("F0 01 00 00 00 02 00 00 00 08"), "M1E-06,2E-06");
}

TEST(XamlPathDeserializerTest, LineToRendersRawDoubles)
{
	// P3d: M(0,0) then L over tag-5 IEEE doubles 2.5/-3.25.
	EXPECT_EQ(DeserHex(
		"00 01 01"
		" 01 05 00 00 00 00 00 00 04 40 05 00 00 00 00 00 00 0A C0"
		" 08"),
		"M0,0 L2.5,-3.25");
}

TEST(XamlPathDeserializerTest, BezierFormsRenderTheirPoints)
{
	// P3e: Q takes pt1 (bool-tagged) plus pt2 (always tagged).
	EXPECT_EQ(DeserHex("00 01 01 02 02 03 03 01 08"), "M0,0 Q1,-1 -1,0");
	// P3f: C takes three points.
	EXPECT_EQ(DeserHex("00 01 01 03 02 03 02 02 01 01 08"),
		"M0,0 C1,-1 1,1 0,0");
}

TEST(XamlPathDeserializerTest, PolyFormsRenderThePrefixAndEachPoint)
{
	// P3g: PolyLineTo count=2.
	EXPECT_EQ(DeserHex("00 01 01 04 02 00 00 00 02 03 03 02 08"),
		"M0,0 L1,-1 -1,1");
	// P3h: PolyQuadraticBezierTo with count 0 renders the bare prefix.
	EXPECT_EQ(DeserHex("00 01 01 05 00 00 00 00 08"), "M0,0 Q");
	// P3i: PolyBezierTo count=1.
	EXPECT_EQ(DeserHex("00 01 01 06 01 00 00 00 02 03 08"), "M0,0 C1,-1");
	// P3j: a negative count never iterates (the C# loop condition).
	EXPECT_EQ(DeserHex("00 01 01 04 FF FF FF FF 08"), "M0,0 L");
}

TEST(XamlPathDeserializerTest, ArcToRendersSizeAngleFlagsAndEndPoint)
{
	// P3k: flags byte F0 -> largeArc off, sweep on.
	EXPECT_EQ(DeserHex("00 01 01 07 01 02 F0 02 03 02 08"),
		"M0,0 A1,-1 1 0 1 0,1");
	// P3l: flags byte 0F -> largeArc on, sweep off.
	EXPECT_EQ(DeserHex("00 01 01 07 01 02 0F 02 03 02 08"),
		"M0,0 A1,-1 1 1 0 0,1");
	// P3m: the angle takes the tag-4 scaled form (11.700684).
	EXPECT_EQ(DeserHex("00 01 01 07 01 02 00 02 03 04 CC 89 B2 00 08"),
		"M0,0 A1,-1 11.700684 0 0 0,1");
	// P3n: the end point's X takes the scaled form (10); flags F0.
	EXPECT_EQ(DeserHex("00 01 01 47 80 96 98 00 01 F0 02 03 02 08"),
		"M0,0 A1,-1 1 0 1 10,0");
	// P3o: the A arm has NO trailing space, so a following op concatenates
	// directly against the end point.
	EXPECT_EQ(DeserHex("07 01 02 00 01 01 02 00 02 03 08"),
		"A0,0 1 0 0 0,1M1,-1");
}

TEST(XamlPathDeserializerTest, FillRuleInsertsAtTheFront)
{
	// P3p: the fillRule bit (0x10) on the FillRule opcode inserts "F1 " at
	// position 0.
	EXPECT_EQ(DeserHex("19 00 02 03 08"), "F1 M1,-1");
	// P3q: the bit clear inserts nothing.
	EXPECT_EQ(DeserHex("09 00 02 03 08"), "M1,-1");
	// P3r: two fill-rule ops insert twice.
	EXPECT_EQ(DeserHex("19 19 00 02 03 08"), "F1 F1 M1,-1");
	// P3s: a fill rule AFTER the ops still lands at the front.
	EXPECT_EQ(DeserHex("00 02 03 19 08"), "F1 M1,-1");
	// P3t: only the fill rule -- the trailing space is Trimmed away.
	EXPECT_EQ(DeserHex("19 08"), "F1");
}

TEST(XamlPathDeserializerTest, CompositeStreamRendersEverySegment)
{
	// P3u: M(0,0), L over both-scaled coordinates (10,10), then Q.
	EXPECT_EQ(DeserHex(
		"00 01 01"
		" C1 80 96 98 00 80 96 98 00"
		" 02 02 03 02 03"
		" 08"),
		"M0,0 L10,10 Q1,-1 1,-1");
}

TEST(XamlPathDeserializerTest, UnknownOpcodesAreConsumedAndIgnored)
{
	// P3v/P3w: op nibbles with no enumerator do nothing (the C# switch has
	// no default) and the loop continues with the NEXT byte.
	EXPECT_EQ(DeserHex("0A 08"), "");
	EXPECT_EQ(DeserHex("0A 00 02 03 08"), "M1,-1");
}

TEST(XamlPathDeserializerTest, SpecialValueCoordinatesSpellTheirSymbols)
{
	// P3x: NaN coordinates.
	EXPECT_EQ(DeserHex(
		"00 05 00 00 00 00 00 00 F8 7F 05 00 00 00 00 00 00 F8 7F 08"),
		"MNaN,NaN");
	// P3y: the infinities keep their signs.
	EXPECT_EQ(DeserHex(
		"00 05 00 00 00 00 00 00 F0 7F 05 00 00 00 00 00 00 F0 FF 08"),
		"MInfinity,-Infinity");
	// P3z: negative zero keeps its sign (unlike the IL operand spelling).
	EXPECT_EQ(DeserHex(
		"00 05 00 00 00 00 00 00 00 80 05 00 00 00 00 00 00 00 80 08"),
		"M-0,-0");
	// P3aa: raw tag-5 doubles 11.700684/-11.700684 (built from the IEEE
	// bits, matching the probe's programmatic stream).
	{
		const double x = 11.700684, y = -11.700684;
		std::vector<std::uint8_t> bytes = {0x00, 0x05};
		const auto append = [&bytes](const double& d) {
			std::uint8_t raw[8];
			std::memcpy(raw, &d, sizeof(raw));
			bytes.insert(bytes.end(), raw, raw + sizeof(raw));
		};
		append(x);
		bytes.push_back(0x05);
		append(y);
		bytes.push_back(0x08);
		EXPECT_EQ(Deser(bytes), "M11.700684,-11.700684");
	}
}

TEST(XamlPathDeserializerTest, TruncatedStreamsThrowEndOfStream)
{
	// P3ad: end of stream inside an op's operand.
	EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
		return DeserHex("00");
	}).c_str(), "Unable to read beyond the end of the stream.");
	// P3ae: the empty stream (the first op byte read fails).
	EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
		return DeserHex("");
	}).c_str(), "Unable to read beyond the end of the stream.");
	// P3ag: the poly count read truncated.
	EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
		return DeserHex("00 04 01 02");
	}).c_str(), "Unable to read beyond the end of the stream.");
	// P3ah: the arc angle's scaled int32 truncated.
	EXPECT_STREQ(ThrowsMessage<std::out_of_range>([&] {
		return DeserHex("00 01 01 07 01 02 00 01 01 04 01 02");
	}).c_str(), "Unable to read beyond the end of the stream.");
}

TEST(XamlPathDeserializerTest, UnknownDoubleTagThrowsInsideAnOp)
{
	// P3af: the InvalidDataException escapes from the point read.
	EXPECT_STREQ(ThrowsMessage<std::runtime_error>([&] {
		return DeserHex("00 00 02 03 08");
	}).c_str(), "Unknown double type.");
}

} // namespace
