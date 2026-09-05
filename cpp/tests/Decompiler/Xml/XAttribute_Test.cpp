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

// Tests for the port-authored System.Xml.Linq XAttribute (cpp/Decompiler/Xml/
// XAttribute.hpp): the ctor validation (the namespace-declaration rules), the
// sibling navigation, Remove, the value mutation, and the namespace helpers.
// Every expectation is pinned against the REAL .NET 10 System.Xml.Linq driven
// by the gold probe (C:/temp-probe/XElemProbe/Program.cs -- the section
// headers below name the probe sections).

#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
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

// Renders an element's attributes as "n1=v1|n2=v2|..." (the probe's join
// shape).
std::string Attrs(Xml::XElement& element)
{
	std::string result;
	bool first = true;
	for (const auto& attribute : element.Attributes()) {
		if (!first)
			result += "|";
		first = false;
		result += attribute->Name().ToString() + "=" + attribute->Value();
	}
	return result;
}

const std::string kXmlNamespaceName = "http://www.w3.org/XML/1998/namespace";
const std::string kXmlnsNamespaceName = "http://www.w3.org/2000/xmlns/";

} // namespace

// (probe: "XAttribute basics")
TEST(XAttributeTest, Basics)
{
	auto attribute = std::make_shared<Xml::XAttribute>("a", "1");
	EXPECT_EQ(attribute->Name().ToString(), "a");
	EXPECT_EQ(attribute->Value(), "1");
	EXPECT_EQ(attribute->NodeType(), Xml::XmlNodeType::Attribute);
	EXPECT_EQ(attribute->NextAttribute(), nullptr);
	EXPECT_EQ(attribute->PreviousAttribute(), nullptr);
	EXPECT_EQ(attribute->Parent(), nullptr);
	EXPECT_EQ(attribute->Document(), nullptr);

	attribute->Value("2");
	EXPECT_EQ(attribute->Value(), "2");

	auto copy = std::make_shared<Xml::XAttribute>(*attribute);
	EXPECT_EQ(copy->Name().ToString(), "a");
	EXPECT_EQ(copy->Value(), "2");
	EXPECT_EQ(copy->Parent(), nullptr);
	EXPECT_NE(copy.get(), attribute.get());
}

// (probe: "XAttribute basics") Remove on an unparented attribute.
TEST(XAttributeTest, RemoveUnparentedThrows)
{
	auto attribute = std::make_shared<Xml::XAttribute>("a", "1");
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] { attribute->Remove(); }),
		"The parent is missing.");
}

// (probe: "XAttribute IsNamespaceDeclaration")
TEST(XAttributeTest, IsNamespaceDeclaration)
{
	EXPECT_TRUE((std::make_shared<Xml::XAttribute>("xmlns", "ns"))->IsNamespaceDeclaration());
	EXPECT_TRUE((std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("x"), "ns"))
	                ->IsNamespaceDeclaration());
	EXPECT_FALSE((std::make_shared<Xml::XAttribute>("a", "1"))->IsNamespaceDeclaration());
	EXPECT_TRUE(
		(std::make_shared<Xml::XAttribute>(Xml::XNamespace::None().GetName("xmlns"), "http://x"))
			->IsNamespaceDeclaration());
}

// (probe: "XAttribute ValidateAttribute") The prefixed xmlns-namespace
// declaration rules.
TEST(XAttributeTest, ValidateAttributeXmlnsNamespace)
{
	const auto xmlNsName = Xml::XNamespace::Xmlns().GetName("x");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a(xmlNsName, ""); }),
		"The prefix 'x' cannot be bound to the empty namespace name.");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a(xmlNsName, kXmlNamespaceName); }),
		"The prefix 'xml' is bound to the namespace name 'http://www.w3.org/XML/1998/namespace'. Other "
		"prefixes must not be bound to this namespace name, and it must not be declared as the default "
		"namespace.");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a(xmlNsName, kXmlnsNamespaceName); }),
		"The prefix 'xmlns' is bound to the namespace name 'http://www.w3.org/2000/xmlns/'. It must not be "
		"declared. Other prefixes must not be bound to this namespace name, and it must not be declared as "
		"the default namespace.");

	const auto xmlLocal = Xml::XNamespace::Xmlns().GetName("xml");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a(xmlLocal, "other"); }),
		"The prefix 'xml' is bound to the namespace name 'http://www.w3.org/XML/1998/namespace'. Other "
		"prefixes must not be bound to this namespace name, and it must not be declared as the default "
		"namespace.");
	const auto xmlnsLocal = Xml::XNamespace::Xmlns().GetName("xmlns");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a(xmlnsLocal, "other"); }),
		"The prefix 'xmlns' is bound to the namespace name 'http://www.w3.org/2000/xmlns/'. It must not be "
		"declared. Other prefixes must not be bound to this namespace name, and it must not be declared as "
		"the default namespace.");

	// A prefixed declaration with a regular namespace is fine.
	EXPECT_NO_THROW(Xml::XAttribute(xmlNsName, "other"));
}

// (probe: "XAttribute ValidateAttribute") The default xmlns declaration
// rules (a bare local-name xmlns).
TEST(XAttributeTest, ValidateAttributeDefaultXmlns)
{
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a("xmlns", kXmlNamespaceName); }),
		"The prefix 'xml' is bound to the namespace name 'http://www.w3.org/XML/1998/namespace'. Other "
		"prefixes must not be bound to this namespace name, and it must not be declared as the default "
		"namespace.");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XAttribute a("xmlns", kXmlnsNamespaceName); }),
		"The prefix 'xmlns' is bound to the namespace name 'http://www.w3.org/2000/xmlns/'. It must not be "
		"declared. Other prefixes must not be bound to this namespace name, and it must not be declared as "
		"the default namespace.");
	EXPECT_NO_THROW(Xml::XAttribute("xmlns", "http://x"));
	// The xml namespace name is legal on a regular attribute name.
	EXPECT_NO_THROW(Xml::XAttribute("xml", kXmlNamespaceName));
}

// (probe: "XAttribute ValidateAttribute") The Value setter runs the same
// validation.
TEST(XAttributeTest, ValueSetterValidates)
{
	auto attribute = std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("x"), "v");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { attribute->Value(""); }),
		"The prefix 'x' cannot be bound to the empty namespace name.");
	EXPECT_EQ(attribute->Value(), "v");
}

// (probe: "XAttribute basics" + "Attribute remove") The sibling navigation
// over an attached attribute list.
TEST(XAttributeTest, SiblingNavigation)
{
	Xml::XElement element("e");
	element.Add(std::make_shared<Xml::XAttribute>("a", "1"));
	element.Add(std::make_shared<Xml::XAttribute>("b", "2"));
	element.Add(std::make_shared<Xml::XAttribute>("c", "3"));

	Xml::XAttribute* a = element.Attribute("a");
	Xml::XAttribute* b = element.Attribute("b");
	Xml::XAttribute* c = element.Attribute("c");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	ASSERT_NE(c, nullptr);

	EXPECT_EQ(a->NextAttribute(), b);
	EXPECT_EQ(b->NextAttribute(), c);
	EXPECT_EQ(c->NextAttribute(), nullptr);
	EXPECT_EQ(a->PreviousAttribute(), nullptr);
	EXPECT_EQ(b->PreviousAttribute(), a);
	EXPECT_EQ(c->PreviousAttribute(), b);
	EXPECT_EQ(a->Parent(), &element);
	EXPECT_EQ(element.FirstAttribute(), a);
	EXPECT_EQ(element.LastAttribute(), c);
}

// (probe: "Attribute remove") Remove detaches and keeps the node alive
// while a shared_ptr holds it.
TEST(XAttributeTest, RemoveDetaches)
{
	Xml::XElement element("e",
		{ std::make_shared<Xml::XAttribute>("a", "1"), std::make_shared<Xml::XAttribute>("b", "2"),
			std::make_shared<Xml::XAttribute>("c", "3") });

	auto b = element.Attribute("b")->shared_from_this();
	b->Remove();
	EXPECT_EQ(Attrs(element), "a=1|c=3");
	EXPECT_EQ(b->Parent(), nullptr);
	EXPECT_EQ(b->NextAttribute(), nullptr);
	EXPECT_EQ(b->PreviousAttribute(), nullptr);

	auto a = element.Attribute("a")->shared_from_this();
	a->Remove();
	EXPECT_EQ(Attrs(element), "c=3");

	auto c = element.Attribute("c")->shared_from_this();
	c->Remove();
	EXPECT_EQ(Attrs(element), "");
	EXPECT_EQ(element.HasAttributes(), false);

	// The detached attribute survives while the shared_ptr holds it.
	EXPECT_EQ(a->Value(), "1");

	// Removing again throws (the parent is missing).
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] { c->Remove(); }), "The parent is missing.");
}

// (probe: "namespace prefix walk" + XAttribute.GetPrefixOfNamespace) The
// internal prefix lookup on an unparented attribute (the xml/xmlns
// fallbacks).
TEST(XAttributeTest, GetPrefixOfNamespaceUnparented)
{
	auto attribute = std::make_shared<Xml::XAttribute>("a", "1");
	EXPECT_EQ(*attribute->GetPrefixOfNamespace(Xml::XNamespace::Xml()), "xml");
	EXPECT_EQ(*attribute->GetPrefixOfNamespace(Xml::XNamespace::Xmlns()), "xmlns");
	EXPECT_EQ(attribute->GetPrefixOfNamespace(Xml::XNamespace::Get("http://none")),
		std::optional<std::string> {});
	EXPECT_EQ(*attribute->GetPrefixOfNamespace(Xml::XNamespace::None()), "");
}
