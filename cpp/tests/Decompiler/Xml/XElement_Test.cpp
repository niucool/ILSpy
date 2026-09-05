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

// Tests for the port-authored System.Xml.Linq XElement (cpp/Decompiler/Xml/
// XElement.hpp) and the XContainer Element/Elements surface: the attribute
// list machinery (attach, duplicate rejection, clone-on-attach, remove), the
// lazy Elements()/Attributes() iteration semantics, the value and name
// mutation, SetAttributeValue, DeepEquals, the namespace prefix resolution,
// the copy constructor, and the XDocument Root. Every expectation is pinned
// against the REAL .NET 10 System.Xml.Linq driven by the gold probe
// (C:/temp-probe/XElemProbe/Program.cs -- the section headers below name the
// probe sections).

#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XContainer.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XNode.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string Elems(Xml::XContainer& container)
{
	std::string result;
	bool first = true;
	for (const auto& element : container.Elements()) {
		if (!first)
			result += "|";
		first = false;
		result += element->Name().ToString();
	}
	return result;
}

std::string NodeNames(Xml::XContainer& container)
{
	std::string result;
	bool first = true;
	for (const auto& node : container.Nodes()) {
		if (!first)
			result += "|";
		first = false;
		result += node->NodeType() == Xml::XmlNodeType::Element
			? static_cast<Xml::XElement&>(*node).Name().ToString()
			: "text";
	}
	return result;
}

auto Attr(std::string name, std::string value)
{
	return std::make_shared<Xml::XAttribute>(Xml::XName(std::move(name)), std::move(value));
}

auto Elem(std::string name)
{
	return std::make_shared<Xml::XElement>(Xml::XName(std::move(name)));
}

// The C# `new XElement(name, params object[] content)` -- make_shared cannot
// forward a braced list, so the params form goes through this helper.
std::shared_ptr<Xml::XElement> MakeElem(std::string name, Xml::XContent content)
{
	return std::make_shared<Xml::XElement>(Xml::XName(std::move(name)), std::move(content));
}

} // namespace

// (probe: "XElement basics" + "More edge facts")
TEST(XElementTest, Basics)
{
	Xml::XElement root("root");
	EXPECT_EQ(root.Name().ToString(), "root");
	EXPECT_EQ(root.NodeType(), Xml::XmlNodeType::Element);
	EXPECT_TRUE(root.IsEmpty());
	EXPECT_FALSE(root.HasAttributes());
	EXPECT_FALSE(root.HasElements());
	EXPECT_EQ(root.Value(), "");
	EXPECT_EQ(root.FirstAttribute(), nullptr);
	EXPECT_EQ(root.LastAttribute(), nullptr);
	EXPECT_EQ(root.Attribute("nope"), nullptr);
	EXPECT_EQ(root.Element("nope"), nullptr);
	EXPECT_EQ(root.Parent(), nullptr);
	EXPECT_EQ(root.Document(), nullptr);

	auto child = std::make_shared<Xml::XElement>(Xml::XName("child"), "text");
	auto element = MakeElem("root",
		{ Attr("a", "1"), child, Xml::XContent(std::string("tail")), Attr("b", "2") });
	EXPECT_EQ(Attrs(*element), "a=1|b=2");
	EXPECT_EQ(element->FirstAttribute()->Name().ToString(), "a");
	EXPECT_EQ(element->LastAttribute()->Name().ToString(), "b");
	EXPECT_EQ(element->Attribute("a")->Value(), "1");
	EXPECT_EQ(Elems(*element), "child");
	EXPECT_TRUE(element->HasElements());
	EXPECT_EQ(element->Value(), "texttail");
	EXPECT_FALSE(element->IsEmpty());

	auto attrOnly = std::make_shared<Xml::XElement>(Xml::XName("e"), Attr("a", "1"));
	EXPECT_TRUE(attrOnly->IsEmpty());
	EXPECT_TRUE(attrOnly->HasAttributes());

	auto textOnly = std::make_shared<Xml::XElement>(Xml::XName("e"), "just text");
	EXPECT_FALSE(textOnly->HasElements());
	EXPECT_FALSE(textOnly->IsEmpty());
}

// (probe: "XElement name set")
TEST(XElementTest, NameSet)
{
	Xml::XElement element("root");
	element.Name("renamed");
	EXPECT_EQ(element.Name().ToString(), "renamed");
	element.Name("{http://ns}el");
	EXPECT_EQ(element.Name().ToString(), "{http://ns}el");
}

// (probe: "Value set/get" + "More edge facts")
TEST(XElementTest, ValueGetSet)
{
	Xml::XElement element("e");
	element.Value("hello");
	EXPECT_EQ(element.Value(), "hello");
	std::size_t nodeCount = 0;
	for (const auto& node : element.Nodes())
		nodeCount++;
	EXPECT_EQ(nodeCount, 1);

	element.Value("second");
	nodeCount = 0;
	for (const auto& node : element.Nodes())
		nodeCount++;
	EXPECT_EQ(nodeCount, 1);
	EXPECT_EQ(element.Value(), "second");

	Xml::XElement e3("e", { Elem("c1"), Xml::XContent(std::string("t")),
		std::make_shared<Xml::XElement>(Xml::XName("c2"), "deep") });
	EXPECT_EQ(e3.Value(), "tdeep");
	e3.Value("replaced");
	EXPECT_EQ(e3.Value(), "replaced");
	std::size_t childCount = 0;
	for (const auto& node : e3.Elements())
		childCount++;
	EXPECT_EQ(childCount, 0);

	// The Value setter keeps attributes.
	Xml::XElement e17("e", Attr("k", "1"));
	e17.Value("v");
	EXPECT_EQ(Attrs(e17), "k=1");
	EXPECT_EQ(e17.Value(), "v");
}

// (probe: "Add attribute paths")
TEST(XElementTest, AddAttributePaths)
{
	Xml::XElement element("e");
	element.Add(Attr("x", "1"));
	EXPECT_EQ(Attrs(element), "x=1");
	// The attribute list is independent of the node content.
	element.Add("text");
	element.Add(Attr("y", "2"));
	EXPECT_EQ(Attrs(element), "x=1|y=2");
	std::size_t nodeCount = 0;
	for (const auto& node : element.Nodes())
		nodeCount++;
	EXPECT_EQ(nodeCount, 1);

	// Duplicate attribute names are rejected.
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] { element.Add(Attr("x", "9")); }),
		"Duplicate attribute.");

	// An attribute attached to another element is cloned on attach.
	Xml::XElement holder("holder");
	auto shared = Attr("shared", "v");
	holder.Add(shared);
	Xml::XElement other("e6");
	other.Add(shared);
	EXPECT_EQ(other.Attribute("shared")->Value(), "v");
	EXPECT_NE(other.Attribute("shared"), shared.get());
	EXPECT_EQ(shared->Parent(), &holder);
	EXPECT_EQ(Attrs(holder), "shared=v");

	// A document rejects attribute content outright.
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>(
					  [&] { Xml::XDocument().Add(Attr("a", "1")); }),
		"An attribute cannot be added to content.");
}

// (probe: "SetAttributeValue")
TEST(XElementTest, SetAttributeValue)
{
	Xml::XElement element("e");
	element.SetAttributeValue("k", std::optional<std::string>("v1"));
	EXPECT_EQ(Attrs(element), "k=v1");
	element.SetAttributeValue("k", std::optional<std::string>("v2"));
	EXPECT_EQ(Attrs(element), "k=v2");
	element.SetAttributeValue("k", std::nullopt);
	EXPECT_EQ(Attrs(element), "");
	// Removing a missing attribute is a no-op.
	element.SetAttributeValue("k", std::nullopt);
	EXPECT_EQ(Attrs(element), "");
}

// (probe: "RemoveAttributes/ReplaceAttributes/RemoveAll/ReplaceAll")
TEST(XElementTest, RemoveReplaceAttributes)
{
	Xml::XElement element("e", { Attr("a", "1"), Elem("c"), Attr("b", "2") });
	auto removedA = element.Attribute("a")->shared_from_this();
	element.RemoveAttributes();
	EXPECT_EQ(Attrs(element), "");
	EXPECT_EQ(Elems(element), "c");
	EXPECT_EQ(removedA->Parent(), nullptr);

	element.Add(Attr("n", "1"));
	element.ReplaceAttributes(Attr("r1", "x"));
	EXPECT_EQ(Attrs(element), "r1=x");
	element.ReplaceAttributes({ Attr("r2", "y"), Attr("r3", "z") });
	EXPECT_EQ(Attrs(element), "r2=y|r3=z");

	element.Add(Elem("cc"));
	element.RemoveAll();
	EXPECT_EQ(Attrs(element), "");
	EXPECT_EQ(Elems(element), "");

	element.ReplaceAll({ Attr("z", "1"), std::make_shared<Xml::XElement>(Xml::XName("zz"), "v") });
	EXPECT_EQ(Attrs(element), "z=1");
	EXPECT_EQ(Elems(element), "zz");
	EXPECT_EQ(element.Value(), "v");
}

// (probe: "Attributes filtered" + "Elements/Element")
TEST(XElementTest, FilteredLookups)
{
	Xml::XElement element("e", { Attr("a", "1"), Attr("b", "2"), Attr("c", "3") });
	EXPECT_EQ(Attrs(element), "a=1|b=2|c=3");

	std::string filtered;
	for (const auto& attribute : element.Attributes(Xml::XName("a")))
		filtered += attribute->Value();
	EXPECT_EQ(filtered, "1");
	std::size_t missing = 0;
	for (const auto& attribute : element.Attributes(Xml::XName("nope")))
		missing++;
	EXPECT_EQ(missing, 0);

	Xml::XElement parent("r",
		{ Elem("a"), std::make_shared<Xml::XText>("t"), Elem("b"), Elem("a") });
	EXPECT_EQ(Elems(parent), "a|b|a");
	std::size_t aCount = 0;
	for (const auto& element2 : parent.Elements(Xml::XName("a")))
		aCount++;
	EXPECT_EQ(aCount, 2);
	std::size_t nopeCount = 0;
	for (const auto& element2 : parent.Elements(Xml::XName("nope")))
		nopeCount++;
	EXPECT_EQ(nopeCount, 0);
	EXPECT_EQ(parent.Element("a")->Name().ToString(), "a");
	EXPECT_EQ(parent.Element("nope"), nullptr);

	// Element/Elements never materialize string content.
	Xml::XElement textElement("e", "text");
	EXPECT_EQ(textElement.Element("e"), nullptr);
	std::size_t textElems = 0;
	for (const auto& element2 : textElement.Elements())
		textElems++;
	EXPECT_EQ(textElems, 0);
}

// (probe: "Elements/Element") The XDocument surface over elements.
TEST(XElementTest, DocumentElements)
{
	Xml::XDocument document(
		std::make_shared<Xml::XElement>(Xml::XName("r"), Elem("c")));
	EXPECT_EQ(Elems(document), "r");
	ASSERT_NE(document.Root(), nullptr);
	EXPECT_EQ(document.Root()->Name().ToString(), "r");
	// Element on the document finds child elements (the root).
	EXPECT_NE(document.Element("r"), nullptr);
}

// (probe: "Elements lazy remove" + "Attributes lazy remove") The lazy
// iteration stops after a removed item (the C# iterator stop condition).
TEST(XElementTest, LazyRemoveDuringIteration)
{
	Xml::XElement element("r", { Elem("a"), Elem("b"), Elem("c"), Elem("d") });
	std::vector<std::string> processed;
	for (const auto& child : element.Elements()) {
		processed.push_back(child->Name().ToString());
		child->Remove();
	}
	EXPECT_EQ((std::vector<std::string> { "a" }), processed);
	EXPECT_EQ(Elems(element), "b|c|d");

	Xml::XElement element2("r", { Attr("a", "1"), Attr("b", "2"), Attr("c", "3") });
	std::vector<std::string> processedAttrs;
	for (const auto& attribute : element2.Attributes()) {
		processedAttrs.push_back(attribute->Name().ToString());
		attribute->Remove();
	}
	EXPECT_EQ((std::vector<std::string> { "a" }), processedAttrs);
	EXPECT_EQ(Attrs(element2), "b=2|c=3");
}

// (probe: "DeepEquals")
TEST(XElementTest, DeepEquals)
{
	auto x1 = MakeElem("r", { Attr("a", "1"), MakeElem("c", "t") });
	auto x2 = MakeElem("r", { Attr("a", "1"), MakeElem("c", "t") });
	auto x3 = MakeElem("r", { Attr("a", "1"), MakeElem("c", "u") });
	auto x4 = MakeElem("r", { Attr("a", "1"), Attr("b", "2"), MakeElem("c", "t") });
	auto x5 = MakeElem("s", { Attr("a", "1"), MakeElem("c", "t") });

	EXPECT_TRUE(Xml::XNode::DeepEquals(x1.get(), x2.get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(x1.get(), x3.get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(x1.get(), x4.get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(x1.get(), x5.get()));
	EXPECT_TRUE(Xml::XNode::DeepEquals(x1.get(), x1.get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(x1.get(), std::make_shared<Xml::XText>("r").get()));

	// Attribute ORDER matters.
	auto y1 = MakeElem("r", { Attr("a", "1"), Attr("b", "2") });
	auto y2 = MakeElem("r", { Attr("b", "2"), Attr("a", "1") });
	EXPECT_FALSE(Xml::XNode::DeepEquals(y1.get(), y2.get()));

	// No attributes equals no attributes.
	auto z1 = MakeElem("r", {});
	auto z2 = MakeElem("r", {});
	EXPECT_TRUE(Xml::XNode::DeepEquals(z1.get(), z2.get()));

	// String content equals an XText node.
	auto t1 = MakeElem("r", "text");
	auto t2 = MakeElem("r", std::make_shared<Xml::XText>("text"));
	EXPECT_TRUE(Xml::XNode::DeepEquals(t1.get(), t2.get()));
}

// (probe: "namespace prefix walk")
TEST(XElementTest, NamespacePrefixWalk)
{
	auto inner = MakeElem("i",
		{ Attr("q", "v"),
			std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("p"), "http://p") });
	Xml::XElement outer(Xml::XName("o"),
		{ std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("p2"), "http://p2"),
			inner });

	EXPECT_EQ(*inner->GetPrefixOfNamespace(Xml::XNamespace::Get("http://p")), "p");
	EXPECT_EQ(*inner->GetPrefixOfNamespace(Xml::XNamespace::Get("http://p2")), "p2");
	EXPECT_EQ(*inner->GetPrefixOfNamespace(Xml::XNamespace::Xml()), "xml");
	EXPECT_EQ(*inner->GetPrefixOfNamespace(Xml::XNamespace::Xmlns()), "xmlns");
	EXPECT_EQ(inner->GetPrefixOfNamespace(Xml::XNamespace::Get("http://none")), std::nullopt);
	EXPECT_EQ(inner->GetDefaultNamespace().NamespaceName(), "");

	Xml::XElement withDefault("d", Attr("xmlns", "http://def"));
	EXPECT_EQ(withDefault.GetDefaultNamespace().NamespaceName(), "http://def");
	// A default-namespace declaration never answers GetPrefixOfNamespace.
	EXPECT_EQ(withDefault.GetPrefixOfNamespace(Xml::XNamespace::Get("http://def")), std::nullopt);

	EXPECT_EQ(inner->GetNamespaceOfPrefix("p")->NamespaceName(), "http://p");
	EXPECT_EQ(inner->GetNamespaceOfPrefix("p2")->NamespaceName(), "http://p2");
	EXPECT_EQ(inner->GetNamespaceOfPrefix("xml")->NamespaceName(), "http://www.w3.org/XML/1998/namespace");
	EXPECT_EQ(inner->GetNamespaceOfPrefix("xmlns")->NamespaceName(), "http://www.w3.org/2000/xmlns/");
	EXPECT_EQ(inner->GetNamespaceOfPrefix("nope"), std::nullopt);
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { inner->GetNamespaceOfPrefix(""); }),
		"The value cannot be an empty string. (Parameter 'prefix')");

	// Shadowing: the innermost scope's declaration wins.
	auto innerShadow = MakeElem("i2",
		std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("p"), "http://inner"));
	Xml::XElement outerShadow(Xml::XName("o2"),
		{ std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("p"), "http://outer"),
			innerShadow });
	EXPECT_EQ(*innerShadow->GetPrefixOfNamespace(Xml::XNamespace::Get("http://inner")), "p");
	EXPECT_EQ(*outerShadow.GetPrefixOfNamespace(Xml::XNamespace::Get("http://outer")), "p");

	// An unparented element falls back to the implicit xml/xmlns prefixes.
	Xml::XElement unparented("u");
	EXPECT_EQ(*unparented.GetPrefixOfNamespace(Xml::XNamespace::Xml()), "xml");
	EXPECT_EQ(*unparented.GetPrefixOfNamespace(Xml::XNamespace::Xmlns()), "xmlns");
	EXPECT_EQ(unparented.GetPrefixOfNamespace(Xml::XNamespace::None()), std::nullopt);

	// An explicitly declared xml prefix is found through the walk.
	Xml::XElement xmlDeclared("x",
		std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns().GetName("xml"),
			"http://www.w3.org/XML/1998/namespace"));
	auto childOfDeclared = Elem("y");
	xmlDeclared.Add(childOfDeclared);
	EXPECT_EQ(*childOfDeclared->GetPrefixOfNamespace(Xml::XNamespace::Xml()), "xml");
}

// (probe: "copy ctor")
TEST(XElementTest, CopyConstructor)
{
	Xml::XElement original("r", { Attr("a", "1"), Elem("c"), Xml::XContent(std::string("tail")) });
	auto copy = std::make_shared<Xml::XElement>(original);
	EXPECT_EQ(copy->Name().ToString(), "r");
	EXPECT_EQ(Attrs(*copy), "a=1");
	EXPECT_EQ(Elems(*copy), "c");
	EXPECT_EQ(copy->Value(), "tail");
	EXPECT_EQ(copy->Parent(), nullptr);
	EXPECT_NE(copy->Attribute("a"), original.Attribute("a"));

	copy->Add(Attr("b", "2"));
	EXPECT_EQ(Attrs(original), "a=1");
}

// (probe: "clone through Add of parented" + "ValidateNode")
TEST(XElementTest, AddParentedAndValidateNode)
{
	// Adding a parented node clones it.
	Xml::XElement parent1("p1", Elem("shared"));
	Xml::XElement parent2("p2");
	parent2.Add(parent1.Element("shared")->shared_from_this());
	EXPECT_EQ(parent2.Element("shared")->Name().ToString(), "shared");
	EXPECT_NE(parent1.Element("shared"), nullptr);

	// Adding an ancestor clones instead of throwing.
	auto p3 = std::make_shared<Xml::XElement>(Xml::XName("p3"));
	auto root = std::make_shared<Xml::XElement>(Xml::XName("root"), p3);
	EXPECT_NO_THROW(root->Add(root));

	// Documents and document types cannot be added to an element.
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>(
					  [&] { Xml::XElement("e").Add(std::make_shared<Xml::XDocument>()); }),
		"A node of type Document cannot be added to content.");
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>(
					  [&] {
						  Xml::XElement("e").Add(
							  std::make_shared<Xml::XDocumentType>("n", std::nullopt, std::nullopt,
								  std::nullopt));
					  }),
		"A node of type DocumentType cannot be added to content.");
}

// (probe: "XElement basics" + "More edge facts") The element content walk:
// attributes live outside the node list.
TEST(XElementTest, ElementNodesWithAttributes)
{
	auto child = Elem("c");
	Xml::XElement element("e", { Attr("a", "1"), child });
	EXPECT_EQ(NodeNames(element), "c");
	EXPECT_EQ(element.LastNode(), child.get());
	EXPECT_EQ(element.FirstNode(), element.LastNode());
}
