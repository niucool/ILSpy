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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlUtils.cs (see XamlUtils.hpp
// for the porting decisions) -- the self-contained members plus the two
// ToString extension methods (the implementation halves need the complete
// XamlContext/XamlType/XElement types, so they live here rather than the
// header).

#include "BamlDecompiler/Xaml/XamlUtils.hpp"

#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XName.hpp"

#include <cstdio>
#include <stdexcept>

namespace ILSpy::BamlDecompiler::Xaml {

std::string Escape(std::string_view value)
{
	if (value.empty())
		return std::string(value);
	if (value.front() == '{')
		return "{}" + std::string(value);
	return std::string(value);
}

double ReadXamlDouble(Baml::BamlBinaryReader& reader, bool scaledInt)
{
	if (!scaledInt) {
		switch (reader.ReadByte()) {
		case 1:
			return 0;
		case 2:
			return 1;
		case 3:
			return -1;
		case 4:
			break;
		case 5:
			return reader.ReadDouble();
		default:
			// The C# InvalidDataException (mapped to std::runtime_error
			// with the exact message -- see the header note).
			throw std::runtime_error("Unknown double type.");
		}
	}
	// Dividing by 1000000.0 is important to get back the original numbers;
	// we can't multiply by the inverse of it (0.000001).
	// (11700684 * 0.000001) != (11700684 / 1000000.0)
	//   => 11.700683999999999 != 11.700684
	return reader.ReadInt32() / 1000000.0;
}

std::string& EscapeName(std::string& sb, std::string_view name)
{
	// The C# `foreach (char ch in name)` walks the UTF-16 code units, so a
	// supplementary-plane character is seen as its two surrogate halves --
	// and each half is a char.IsSurrogate unit, escaping separately.
	const std::u16string units = ILSpy::Decompiler::Util::Utf8ToUtf16(name);
	for (char16_t unit : units) {
		if (ILSpy::Decompiler::Util::IsWhiteSpace(unit)
			|| ILSpy::Decompiler::Util::IsControl(unit)
			|| ILSpy::Decompiler::Util::IsSurrogate(unit)) {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x",
				static_cast<unsigned>(static_cast<std::uint16_t>(unit)));
			sb += buf;
		} else {
			sb += ILSpy::Decompiler::Util::Utf16ToUtf8(
				std::u16string_view(&unit, 1));
		}
	}
	return sb;
}

std::string EscapeName(std::string_view name)
{
	std::string sb;
	EscapeName(sb, name);
	return sb;
}

// The C# `public static string ToString(this XamlContext ctx, XElement elem,
// XamlType type)`.
std::string ToString(XamlContext& ctx, ILSpy::Decompiler::Xml::XElement& elem,
	XamlType& type)
{
	type.ResolveNamespace(elem, ctx);
	return ToString(ctx, elem, type.ToXName(ctx));
}

// The C# `public static string ToString(this XamlContext ctx, XElement elem,
// XName name)`.
std::string ToString(const XamlContext&,
	const ILSpy::Decompiler::Xml::XElement& elem,
	const ILSpy::Decompiler::Xml::XName& name)
{
	std::string sb;
	if (name.Namespace() != elem.GetDefaultNamespace()) {
		const std::optional<std::string> prefix =
			elem.GetPrefixOfNamespace(name.Namespace());
		if (prefix.has_value() && !prefix->empty()) {
			sb += *prefix;
			sb += ':';
		}
	}
	sb += name.LocalName();
	return sb;
}

} // namespace ILSpy::BamlDecompiler::Xaml
