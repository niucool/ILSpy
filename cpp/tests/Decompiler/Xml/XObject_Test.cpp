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

// Tests for the port-authored System.Xml.Linq XObject base and the leaf node
// types (cpp/Decompiler/Xml/XObject.hpp, XText/XCData/XComment/
// XProcessingInstruction/XDocumentType.hpp): the annotation machinery, the
// Document walk, the NodeType surface, and the leaf value members with their
// validation. Every expectation is pinned against the REAL .NET 10
// System.Xml.Linq driven by the gold probe (C:/temp-probe/XDomProbe/
// Program.cs -- the section headers below name the probe sections).

#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XObject.hpp"
#include "Decompiler/Xml/XProcessingInstruction.hpp"
#include "Decompiler/Xml/XText.hpp"
#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XmlNodeType.hpp"

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

struct IntAnnotation {
	int value;
};

struct TagAnnotation {
	std::string tag;
};

} // namespace

// (probe: "XmlNodeType values") The System.Xml.XmlNodeType numeric values.
TEST(XObjectTest, XmlNodeTypeValues)
{
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::None), 0);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Element), 1);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Attribute), 2);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Text), 3);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::CDATA), 4);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::ProcessingInstruction), 7);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Comment), 8);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Document), 9);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::DocumentType), 10);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::Whitespace), 13);
	EXPECT_EQ(static_cast<std::int32_t>(Xml::XmlNodeType::XmlDeclaration), 17);
}

// (probe: "XmlNodeType values") The .NET Enum.ToString member names.
TEST(XObjectTest, XmlNodeTypeToString)
{
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::None), std::string("None"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::Element), std::string("Element"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::CDATA), std::string("CDATA"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::ProcessingInstruction), std::string("ProcessingInstruction"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::Document), std::string("Document"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::DocumentType), std::string("DocumentType"));
	EXPECT_EQ(Xml::ToString(Xml::XmlNodeType::XmlDeclaration), std::string("XmlDeclaration"));
}

// (probe: "XObject annotations") The annotation list: insertion order, the
// typed lookups, removal, and re-lookup.
TEST(XObjectTest, Annotations)
{
	Xml::XDocument document;
	document.AddAnnotation(IntAnnotation { 1 });
	document.AddAnnotation(std::string("str-annot"));
	document.AddAnnotation(IntAnnotation { 2 });

	EXPECT_EQ(document.Annotation<IntAnnotation>()->value, 1);
	EXPECT_EQ(*document.Annotation<std::string>(), std::string("str-annot"));
	EXPECT_EQ(document.Annotation<TagAnnotation>(), nullptr);

	std::vector<IntAnnotation*> ints = document.Annotations<IntAnnotation>();
	ASSERT_EQ(ints.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(ints[0]->value, 1);
	EXPECT_EQ(ints[1]->value, 2);
	EXPECT_EQ(document.Annotations<TagAnnotation>().size(), static_cast<std::size_t>(0));

	// Removal keeps the other annotations in order.
	document.RemoveAnnotations<IntAnnotation>();
	EXPECT_EQ(document.Annotation<IntAnnotation>(), nullptr);
	EXPECT_EQ(document.Annotations<IntAnnotation>().size(), static_cast<std::size_t>(0));
	EXPECT_EQ(*document.Annotation<std::string>(), std::string("str-annot"));
	document.RemoveAnnotations<std::string>();
	EXPECT_EQ(document.Annotation<std::string>(), nullptr);
}

// (probe: "XObject annotations") A stored shared_ptr annotation is shared
// with its holder -- the mutation the BamlDecompiler's XmlnsScope
// annotation pattern relies on.
TEST(XObjectTest, SharedPtrAnnotationAliases)
{
	auto scope = std::make_shared<std::string>("scope");
	Xml::XDocument document;
	document.AddAnnotation(scope);
	auto* stored = document.Annotation<std::shared_ptr<std::string>>();
	ASSERT_NE(stored, nullptr);
	EXPECT_EQ(**stored, std::string("scope"));
	*scope = "mutated";
	EXPECT_EQ(**stored, std::string("mutated"));
}

// (probe: "XObject annotations") The Document walk.
TEST(XObjectTest, DocumentWalk)
{
	Xml::XDocument document;
	EXPECT_EQ(document.Document(), &document);

	auto text = std::make_shared<Xml::XText>("  ");
	EXPECT_EQ(text->Document(), nullptr);
	document.Add(text);
	EXPECT_EQ(text->Document(), &document);

	auto comment = std::make_shared<Xml::XComment>("c");
	EXPECT_EQ(comment->Document(), nullptr);
}

// (probe: "XObject annotations") The node types of every ported class.
TEST(XObjectTest, NodeTypes)
{
	EXPECT_EQ(Xml::XText("t").NodeType(), Xml::XmlNodeType::Text);
	EXPECT_EQ(Xml::XCData("c").NodeType(), Xml::XmlNodeType::CDATA);
	EXPECT_EQ(Xml::XComment("c").NodeType(), Xml::XmlNodeType::Comment);
	EXPECT_EQ(Xml::XProcessingInstruction("t", "d").NodeType(), Xml::XmlNodeType::ProcessingInstruction);
	EXPECT_EQ(Xml::XDocument().NodeType(), Xml::XmlNodeType::Document);
	Xml::XDocumentType documentType("n", std::nullopt, std::nullopt, std::nullopt);
	EXPECT_EQ(documentType.NodeType(), Xml::XmlNodeType::DocumentType);
}

// (probe: "leaf value members") The comment value.
TEST(XObjectTest, XCommentValue)
{
	Xml::XComment comment("v");
	EXPECT_EQ(comment.Value(), std::string("v"));
	comment.Value("w");
	EXPECT_EQ(comment.Value(), std::string("w"));
}

// (probe: "leaf value members") The processing-instruction target/data
// surface and its validation.
TEST(XObjectTest, XProcessingInstructionMembers)
{
	Xml::XProcessingInstruction pi("t", "d");
	EXPECT_EQ(pi.Target(), std::string("t"));
	EXPECT_EQ(pi.Data(), std::string("d"));
	pi.Data("d2");
	EXPECT_EQ(pi.Data(), std::string("d2"));
	pi.Target("t2");
	EXPECT_EQ(pi.Target(), std::string("t2"));

	// The target is an NCName: a bad start unit and an embedded colon throw
	// the exact XmlException shapes.
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([&] { pi.Target("1bad"); }),
		std::string("Name cannot begin with the '1' character, hexadecimal value 0x31."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([&] { pi.Target("a:b"); }),
		std::string("The ':' character, hexadecimal value 0x3A, cannot be included in a name."));

	// The target must not be "xml" in any case.
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XProcessingInstruction("xml", "d"); }),
		std::string("'xml' is an invalid name for a processing instruction."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XProcessingInstruction("XML", "d"); }),
		std::string("'XML' is an invalid name for a processing instruction."));
	EXPECT_EQ(pi.Target(), std::string("t2"));
}

// (probe: "leaf value members" + "XDocumentType qname") The document-type
// name validation -- a Name (colons allowed), unlike the NCName rule.
TEST(XObjectTest, XDocumentTypeMembers)
{
	Xml::XDocumentType documentType("n", "p", "s", "i");
	EXPECT_EQ(documentType.Name(), std::string("n"));
	EXPECT_EQ(documentType.PublicId(), std::optional<std::string>(std::string("p")));
	EXPECT_EQ(documentType.SystemId(), std::optional<std::string>(std::string("s")));
	EXPECT_EQ(documentType.InternalSubset(), std::optional<std::string>(std::string("i")));

	documentType.PublicId(std::nullopt);
	EXPECT_EQ(documentType.PublicId(), std::nullopt);
	documentType.InternalSubset("sub");
	EXPECT_EQ(documentType.InternalSubset(), std::optional<std::string>(std::string("sub")));

	documentType.Name("renamed");
	EXPECT_EQ(documentType.Name(), std::string("renamed"));

	// The QName-style acceptance: colons anywhere.
	EXPECT_NO_THROW(Xml::XDocumentType("ns:e", std::nullopt, std::nullopt, std::nullopt));
	EXPECT_NO_THROW(Xml::XDocumentType(":a", std::nullopt, std::nullopt, std::nullopt));
	EXPECT_NO_THROW(Xml::XDocumentType("a:", std::nullopt, std::nullopt, std::nullopt));
	EXPECT_NO_THROW(Xml::XDocumentType("a:b:c", std::nullopt, std::nullopt, std::nullopt));

	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([&] { Xml::XDocumentType("1bad", std::nullopt, std::nullopt, std::nullopt); }),
		std::string("Name cannot begin with the '1' character, hexadecimal value 0x31."));
	EXPECT_EQ(ThrowsMessage<Xml::XmlException>([&] { Xml::XDocumentType("a b", std::nullopt, std::nullopt, std::nullopt); }),
		std::string("The ' ' character, hexadecimal value 0x20, cannot be included in a name."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { Xml::XDocumentType("", std::nullopt, std::nullopt, std::nullopt); }),
		std::string("The value cannot be an empty string. (Parameter 'name')"));
}

// (probe: "leaf value members") The text node value.
TEST(XObjectTest, XTextValue)
{
	Xml::XText text("v");
	EXPECT_EQ(text.Value(), std::string("v"));
	text.Value("w2");
	EXPECT_EQ(text.Value(), std::string("w2"));
}

// (probe: "leaf value members") The copy constructors clone the payload
// only -- the copies are fresh, unparented nodes.
TEST(XObjectTest, CopyConstructorsCreateFreshNodes)
{
	auto original = std::make_shared<Xml::XComment>("c");
	Xml::XDocument document;
	document.Add(original);

	auto copy = std::make_shared<Xml::XComment>(*original);
	EXPECT_EQ(copy->Value(), std::string("c"));
	EXPECT_EQ(copy->parent_, nullptr);
	EXPECT_NE(copy.get(), original.get());

	auto textCopy = std::static_pointer_cast<Xml::XText>(std::make_shared<Xml::XCData>("d")->CloneNode());
	EXPECT_EQ(textCopy->NodeType(), Xml::XmlNodeType::CDATA);
	EXPECT_EQ(textCopy->Value(), std::string("d"));
}
