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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlPathDeserializer.cs (Ki, 2015,
// MIT): the WPF binary Geometry path decoder the
// KnownTypes.XamlPathDataSerializer arm of PropertyCustomHandler consumes --
// the mini-language string ("M0,0 L10,10 ...") rebuilt from the compact
// binary form the BAML stream carries.
//
// The wire form: a sequence of op bytes whose LOW nibble is the PathOpCodes
// opcode and whose HIGH nibble carries four bool bits (UnpackBools: 0x10 /
// 0x20 / 0x40 / 0x80). The 0x40/0x80 bits select the SCALED int32 coordinate
// form for the point-reading ops (the XamlUtils ReadXamlDouble's scaledInt
// path); the 0x10/0x20 bits are the filled/closed/stroked/smoothJoin flags
// the C# reads but never renders. An op byte whose low nibble matches no
// opcode is consumed and ignored (the C# switch has no default); the loop
// ends at the Closed opcode. Coordinates render through Point::ToString (the
// invariant "{0:R},{1:R}" join), so NaN/Infinity/negative zero spell their
// .NET symbol names.
//
// C#-to-C++ porting decisions:
//  * The C# takes System.IO.BinaryReader -- the port takes the BamlBinaryReader
//    (the port's BinaryReader stand-in; PropertyCustomHandler passes the same
//    reader its own record payload came from).
//  * The stroked/smoothJoin/filled/closed flags are OUT parameters in the C#
//    (ReadPointBoolBool's b1/b2) that Deserialize discards; the port keeps the
//    same read shape with (void) discards.
//  * The result's final Trim() removes the char.IsWhiteSpace set from both
//    ends (the render only ever produces trailing ' ' whitespace, but the
//    port trims the full classification for faithfulness).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"

namespace ILSpy::BamlDecompiler::Xaml {

class XamlPathDeserializer {
public:
	// The C# `public static string Deserialize(BinaryReader reader)`.
	static std::string Deserialize(Baml::BamlBinaryReader& reader);

private:
	enum class PathOpCodes : std::uint8_t {
		BeginFigure,
		LineTo,
		QuadraticBezierTo,
		BezierTo,
		PolyLineTo,
		PolyQuadraticBezierTo,
		PolyBezierTo,
		ArcTo,
		Closed,
		FillRule,
	};

	struct Point {
		double X = 0;
		double Y = 0;

		Point() = default;
		Point(double x, double y)
			: X(x), Y(y) {}

		// The C# `string.Format(CultureInfo.InvariantCulture, "{0:R},{1:R}",
		// X, Y)`.
		std::string ToString() const;
	};

	static void UnpackBools(std::uint8_t b, bool& b1, bool& b2, bool& b3,
		bool& b4);
	static Point ReadPoint(Baml::BamlBinaryReader& reader);
	static void ReadPointBoolBool(Baml::BamlBinaryReader& reader,
		std::uint8_t b, Point& pt, bool& b1, bool& b2);
	static std::vector<Point> ReadPointsBoolBool(Baml::BamlBinaryReader& reader,
		std::uint8_t b, bool& b1, bool& b2);
};

} // namespace ILSpy::BamlDecompiler::Xaml
