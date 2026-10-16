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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlPathDeserializer.cs (see
// XamlPathDeserializer.hpp for the wire form and the porting decisions).

#include "BamlDecompiler/Xaml/XamlPathDeserializer.hpp"

#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

namespace ILSpy::BamlDecompiler::Xaml {
namespace {

// The C# `sb.ToString().Trim()`: the char.IsWhiteSpace units removed from
// both ends.
std::string TrimWhitespace(const std::string& text)
{
	const std::u16string units = ILSpy::Decompiler::Util::Utf8ToUtf16(text);
	std::size_t start = 0;
	std::size_t end = units.size();
	while (start < end && ILSpy::Decompiler::Util::IsWhiteSpace(units[start]))
		start++;
	while (end > start && ILSpy::Decompiler::Util::IsWhiteSpace(units[end - 1]))
		end--;
	if (start == 0 && end == units.size())
		return text;
	return ILSpy::Decompiler::Util::Utf16ToUtf8(units.substr(start, end - start));
}

} // namespace

std::string XamlPathDeserializer::Point::ToString() const
{
	return ILSpy::Decompiler::Disassembler::FormatRoundTrip(X) + ","
		+ ILSpy::Decompiler::Disassembler::FormatRoundTrip(Y);
}

void XamlPathDeserializer::UnpackBools(std::uint8_t b, bool& b1, bool& b2,
	bool& b3, bool& b4)
{
	b1 = (b & 0x10) != 0;
	b2 = (b & 0x20) != 0;
	b3 = (b & 0x40) != 0;
	b4 = (b & 0x80) != 0;
}

XamlPathDeserializer::Point XamlPathDeserializer::ReadPoint(
	Baml::BamlBinaryReader& reader)
{
	// The C# evaluates the two reads left-to-right (X first); C++ argument
	// evaluation order is unspecified, so the reads are sequenced
	// explicitly.
	const double x = ReadXamlDouble(reader);
	const double y = ReadXamlDouble(reader);
	return Point(x, y);
}

void XamlPathDeserializer::ReadPointBoolBool(Baml::BamlBinaryReader& reader,
	std::uint8_t b, Point& pt, bool& b1, bool& b2)
{
	bool sx, sy;
	UnpackBools(b, b1, b2, sx, sy);

	// Sequenced explicitly (the C# reads X first; C++ argument evaluation
	// order is unspecified).
	const double x = ReadXamlDouble(reader, sx);
	const double y = ReadXamlDouble(reader, sy);
	pt = Point(x, y);
}

std::vector<XamlPathDeserializer::Point>
XamlPathDeserializer::ReadPointsBoolBool(Baml::BamlBinaryReader& reader,
	std::uint8_t b, bool& b1, bool& b2)
{
	bool b3, b4;
	UnpackBools(b, b1, b2, b3, b4);

	const std::int32_t count = reader.ReadInt32();
	std::vector<Point> pts;
	for (std::int32_t i = 0; i < count; i++)
		pts.push_back(ReadPoint(reader));
	return pts;
}

std::string XamlPathDeserializer::Deserialize(Baml::BamlBinaryReader& reader)
{
	bool end = false;
	std::string sb;

	while (!end) {
		const std::uint8_t b = reader.ReadByte();

		switch (static_cast<PathOpCodes>(b & 0xf)) {
		case PathOpCodes::BeginFigure: {
			Point pt1;
			bool filled, closed;
			ReadPointBoolBool(reader, b, pt1, filled, closed);
			(void)filled;
			(void)closed;

			sb += "M";
			sb += pt1.ToString();
			sb += " ";
			break;
		}

		case PathOpCodes::LineTo: {
			Point pt1;
			bool stroked, smoothJoin;
			ReadPointBoolBool(reader, b, pt1, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;

			sb += "L";
			sb += pt1.ToString();
			sb += " ";
			break;
		}

		case PathOpCodes::QuadraticBezierTo: {
			Point pt1;
			bool stroked, smoothJoin;
			ReadPointBoolBool(reader, b, pt1, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;
			const Point pt2 = ReadPoint(reader);

			sb += "Q";
			sb += pt1.ToString();
			sb += " ";
			sb += pt2.ToString();
			sb += " ";
			break;
		}

		case PathOpCodes::BezierTo: {
			Point pt1;
			bool stroked, smoothJoin;
			ReadPointBoolBool(reader, b, pt1, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;
			const Point pt2 = ReadPoint(reader);
			const Point pt3 = ReadPoint(reader);

			sb += "C";
			sb += pt1.ToString();
			sb += " ";
			sb += pt2.ToString();
			sb += " ";
			sb += pt3.ToString();
			sb += " ";
			break;
		}

		case PathOpCodes::PolyLineTo: {
			bool stroked, smoothJoin;
			const auto pts = ReadPointsBoolBool(reader, b, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;

			sb += "L";
			for (const Point& pt : pts) {
				sb += pt.ToString();
				sb += " ";
			}
			break;
		}

		case PathOpCodes::PolyQuadraticBezierTo: {
			bool stroked, smoothJoin;
			const auto pts = ReadPointsBoolBool(reader, b, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;

			sb += "Q";
			for (const Point& pt : pts) {
				sb += pt.ToString();
				sb += " ";
			}
			break;
		}

		case PathOpCodes::PolyBezierTo: {
			bool stroked, smoothJoin;
			const auto pts = ReadPointsBoolBool(reader, b, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;

			sb += "C";
			for (const Point& pt : pts) {
				sb += pt.ToString();
				sb += " ";
			}
			break;
		}

		case PathOpCodes::ArcTo: {
			Point pt1;
			bool stroked, smoothJoin;
			ReadPointBoolBool(reader, b, pt1, stroked, smoothJoin);
			(void)stroked;
			(void)smoothJoin;
			const std::uint8_t b2 = reader.ReadByte();
			const bool largeArc = (b2 & 0x0f) != 0;
			const bool sweepDirection = (b2 & 0xf0) != 0;
			const Point size = ReadPoint(reader);
			const double angle = ReadXamlDouble(reader);

			// The C# AppendFormat(InvariantCulture, "A{0} {1:R} {2} {3} {4}",
			// size, angle, largeArc ? '1' : '0', sweepDirection ? '1' : '0',
			// pt1) -- note NO trailing space.
			sb += "A";
			sb += size.ToString();
			sb += " ";
			sb += ILSpy::Decompiler::Disassembler::FormatRoundTrip(angle);
			sb += " ";
			sb += largeArc ? "1" : "0";
			sb += " ";
			sb += sweepDirection ? "1" : "0";
			sb += " ";
			sb += pt1.ToString();
			break;
		}

		case PathOpCodes::Closed:
			end = true;
			break;

		case PathOpCodes::FillRule: {
			bool fillRule, b2, b3, b4;
			UnpackBools(b, fillRule, b2, b3, b4);
			(void)b2;
			(void)b3;
			(void)b4;
			if (fillRule)
				sb.insert(0, "F1 ");
			break;
		}
		}
	}

	return TrimWhitespace(sb);
}

} // namespace ILSpy::BamlDecompiler::Xaml
