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

// Tests for the port-authored System.Xml.Linq name layer (cpp/Decompiler/Xml/
// XName.hpp + XNamespace.hpp): the XNamespace facts, the XName Get forms and
// expanded-name parse, and the error arms. Every expectation is pinned against
// the REAL .NET 10 System.Xml.Linq classes driven by the gold probe
// (C:\temp-probe\XmlNameProbe\Program.cs): the N*/X* ids refer to that probe's
// output lines.

#include "Decompiler/Xml/XName.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

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

TEST(XNamespaceTest, NoneAndXmlnsSingletons)
{
	// N1: XNamespace.None has the empty namespace name and renders empty.
	EXPECT_EQ(Xml::XNamespace::None().NamespaceName(), std::string(""));
	EXPECT_EQ(Xml::XNamespace::None().ToString(), std::string(""));
	EXPECT_TRUE(Xml::XNamespace::Get("") == Xml::XNamespace::None());
	// N2: the well-known xmlns namespace URI.
	EXPECT_EQ(Xml::XNamespace::Xmlns().NamespaceName(), std::string("http://www.w3.org/2000/xmlns/"));
	EXPECT_TRUE(Xml::XNamespace::Xmlns() == Xml::XNamespace::Get("http://www.w3.org/2000/xmlns/"));
	// The "xml" prefix namespace (the C# XNamespace.Xml member).
	EXPECT_EQ(Xml::XNamespace::Xml().NamespaceName(), std::string("http://www.w3.org/XML/1998/namespace"));
}

TEST(XNamespaceTest, ValueEquality)
{
	// N3: namespaces compare by namespace name (the C# interned instances).
	EXPECT_TRUE(Xml::XNamespace::Get("http://foo") == Xml::XNamespace::Get("http://foo"));
	EXPECT_FALSE(Xml::XNamespace::Get("http://foo") == Xml::XNamespace::Get("http://bar"));
	EXPECT_EQ(Xml::XNamespace::Get("http://foo").ToString(), std::string("http://foo"));
}

TEST(XNamespaceTest, GetNameAndOperatorPlus)
{
	// N5: ns.GetName(local) == ns + local, with the namespace and local parts
	// observable through the result.
	Xml::XNamespace ns = Xml::XNamespace::Get("http://foo");
	Xml::XName byGetName = ns.GetName("a");
	Xml::XName byPlus = ns + "a";
	EXPECT_TRUE(byGetName == byPlus);
	EXPECT_EQ(byGetName.ToString(), std::string("{http://foo}a"));
	EXPECT_EQ(byGetName.LocalName(), std::string("a"));
	EXPECT_EQ(byGetName.NamespaceName(), std::string("http://foo"));
	EXPECT_TRUE(byGetName.Namespace() == ns);
	// The None namespace composes to a bare local name.
	EXPECT_EQ((Xml::XNamespace::None() + "a").ToString(), std::string("a"));
}

TEST(XNamespaceTest, GetNameErrorArms)
{
	// N8: the empty local name and the colon (the VerifyNCName validation).
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::XNamespace::Get("http://foo").GetName("");
	}), std::string("The value cannot be an empty string. (Parameter 'name')"));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XNamespace::Get("http://foo").GetName("a:b");
	}), std::string("The ':' character, hexadecimal value 0x3A, cannot be included in a name."));
}

TEST(XNameTest, LocalNameForms)
{
	// X1: a bare name carries no namespace.
	Xml::XName name = Xml::XName::Get("foo");
	EXPECT_EQ(name.LocalName(), std::string("foo"));
	EXPECT_EQ(name.NamespaceName(), std::string(""));
	EXPECT_TRUE(name.Namespace() == Xml::XNamespace::None());
	EXPECT_EQ(name.ToString(), std::string("foo"));
	// X2: the two-argument form.
	Xml::XName qualified = Xml::XName::Get("foo", "http://ns");
	EXPECT_EQ(qualified.LocalName(), std::string("foo"));
	EXPECT_EQ(qualified.NamespaceName(), std::string("http://ns"));
	EXPECT_EQ(qualified.ToString(), std::string("{http://ns}foo"));
	// X3: the expanded spelling is the same name (the C# atomization;
	// the port's structural equality).
	EXPECT_TRUE(Xml::XName::Get("{http://ns}foo") == qualified);
	// X6: the implicit string constructor parses the expanded form.
	EXPECT_EQ(Xml::XName(std::string("{http://ns}foo")).ToString(), std::string("{http://ns}foo"));
}

TEST(XNameTest, EqualityAndHash)
{
	// X5/X7: names differ by namespace and by local name; equal names (any
	// spelling) hash equally.
	EXPECT_FALSE(Xml::XName::Get("foo", "ns1") == Xml::XName::Get("foo", "ns2"));
	EXPECT_FALSE(Xml::XName::Get("foo", "ns") == Xml::XName::Get("bar", "ns"));
	std::hash<Xml::XName> hasher;
	EXPECT_EQ(hasher(Xml::XName::Get("{http://ns}foo")), hasher(Xml::XName::Get("foo", "http://ns")));
	// X5: a dictionary keyed by XName sees one entry for both spellings.
	std::unordered_map<Xml::XName, int> dict;
	dict[Xml::XName::Get("foo", "ns")] = 1;
	dict[Xml::XName::Get("{ns}foo")] = 2;
	EXPECT_EQ(dict.size(), 1u);
	EXPECT_EQ(dict[Xml::XName::Get("foo", "ns")], 2);
}

TEST(XNameTest, ExpandedNameParse)
{
	// X9: the parse splits at the LAST '}' and never validates the
	// namespace text.
	Xml::XName twoClose = Xml::XName::Get("{a}b}c");
	EXPECT_EQ(twoClose.NamespaceName(), std::string("a}b"));
	EXPECT_EQ(twoClose.LocalName(), std::string("c"));
	EXPECT_EQ(Xml::XName::Get("{a:b}c").NamespaceName(), std::string("a:b"));
	EXPECT_EQ(Xml::XName::Get("{ }a").NamespaceName(), std::string(" "));
	EXPECT_EQ(Xml::XName::Get("{a{b}c").NamespaceName(), std::string("a{b"));
	EXPECT_EQ(Xml::XName::Get("{a{b}c").LocalName(), std::string("c"));
	// X8: an empty explicit namespace string is the None namespace.
	EXPECT_EQ(Xml::XName::Get("foo", "").ToString(), std::string("foo"));
}

TEST(XNameTest, ExpandedNameErrorArms)
{
	// X8: the empty expanded name and the malformed-brace forms.
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::XName::Get("");
	}), std::string("The value cannot be an empty string. (Parameter 'expandedName')"));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::XName::Get("{ns}");
	}), std::string("'{ns}' is an invalid expanded name."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::XName::Get("{a");
	}), std::string("'{a' is an invalid expanded name."));
	// X9: an empty namespace inside the braces is invalid too ("{}a").
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] {
		return Xml::XName::Get("{}a");
	}), std::string("'{}a' is an invalid expanded name."));
}

TEST(XNameTest, LocalNameValidation)
{
	// X8/X9: the local name is validated as an NCName on every path.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("a:b");
	}), std::string("The ':' character, hexadecimal value 0x3A, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("{ns}a:b");
	}), std::string("The ':' character, hexadecimal value 0x3A, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("a}b");
	}), std::string("The '}' character, hexadecimal value 0x7D, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("}a");
	}), std::string("Name cannot begin with the '}' character, hexadecimal value 0x7D."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get(" foo");
	}), std::string("Name cannot begin with the ' ' character, hexadecimal value 0x20."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("foo ");
	}), std::string("The ' ' character, hexadecimal value 0x20, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("{a}\tb");
	}), std::string("Name cannot begin with the '\t' character, hexadecimal value 0x09."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("a{b");
	}), std::string("The '{' character, hexadecimal value 0x7B, cannot be included in a name."));
}

TEST(XNameTest, VerifyNCNameTableBoundaries)
{
	// V_ncname: the strict start-char table rejects units that are only
	// continuation chars and the XML-1.0-4th-edition holes.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("\xC5\xBF");
	}), std::string("Name cannot begin with the '\xC5\xBF' character, hexadecimal value 0x17F.")); // U+017F long s
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("\xC2\xB5");
	}), std::string("Name cannot begin with the '\xC2\xB5' character, hexadecimal value 0xB5.")); // U+00B5 micro
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("\xE9\xBE\xA6");
	}), std::string("Name cannot begin with the '\xE9\xBE\xA6' character, hexadecimal value 0x9FA6.")); // past the CJK block
	// U+3007 (ideographic number zero), U+05D0 (hebrew alef) and U+4E2D are
	// start chars.
	EXPECT_NO_THROW(Xml::XName::Get("\xE3\x80\x87"));
	EXPECT_NO_THROW(Xml::XName::Get("\xD7\x90"));
	EXPECT_NO_THROW(Xml::XName::Get("\xE4\xB8\xAD"));
	// U+02D0 and U+00B7 are continuation chars only.
	EXPECT_NO_THROW(Xml::XName::Get("u\xCB\x90" "v"));
	EXPECT_NO_THROW(Xml::XName::Get("a\xC2\xB7" "b"));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([] {
		return Xml::XName::Get("\xCB\x90");
	}), std::string("Name cannot begin with the '\xCB\x90' character, hexadecimal value 0x2D0."));
}

} // namespace
