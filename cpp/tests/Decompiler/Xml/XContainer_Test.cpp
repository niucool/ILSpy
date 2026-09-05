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

// Tests for the port-authored System.Xml.Linq XContainer machinery and the
// XDocument validation (cpp/Decompiler/Xml/XContainer.hpp, XDocument.hpp):
// the Add paths (node, string, null, nested collections), the text-content
// states (string concatenation, materialization, merging), the load-bearing
// lazy Nodes() iteration semantics, RemoveNodes/ReplaceNodes, the copy
// constructors, and the document-level content validation. Every expectation
// is pinned against the REAL .NET 10 System.Xml.Linq driven by the gold
// probe (C:/temp-probe/XDomProbe/Program.cs -- the section headers below
// name the probe sections).

#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XContainer.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XNode.hpp"
#include "Decompiler/Xml/XProcessingInstruction.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <gtest/gtest.h>

#include <memory>
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

std::shared_ptr<Xml::XComment> AddComment(Xml::XDocument& document, const std::string& value)
{
	auto comment = std::make_shared<Xml::XComment>(value);
	document.Add(comment);
	return comment;
}

std::size_t NodeCount(Xml::XDocument& document)
{
	std::size_t count = 0;
	for (const auto& node : document.Nodes())
		count++;
	return count;
}

} // namespace

// (probe: "XDocument Add string") The string Add paths over a document.
TEST(XContainerTest, DocumentAddString)
{
	// Whitespace text becomes string content; the first Nodes()/LastNode()
	// read materializes it into a single XText node.
	Xml::XDocument document;
	document.Add(std::string("  \t\r\n "));
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(1));

	Xml::XDocument documentB;
	documentB.Add(std::string("  "));
	EXPECT_EQ(NodeCount(documentB), static_cast<std::size_t>(1));

	// An empty string is recorded as empty-string content: no nodes, but the
	// content is not null (DeepEquals distinguishes it -- see below).
	Xml::XDocument empty;
	empty.Add(std::string(""));
	EXPECT_EQ(NodeCount(empty), static_cast<std::size_t>(0));
	EXPECT_EQ(empty.FirstNode(), nullptr);

	// A null content entry is a no-op.
	Xml::XDocument nullAdd;
	nullAdd.Add(nullptr);
	EXPECT_EQ(nullAdd.FirstNode(), nullptr);

	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] { Xml::XDocument().Add(std::string("hello")); }),
		std::string("Non-whitespace characters cannot be added to content."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] { Xml::XDocument().Add(std::make_shared<Xml::XText>("x")); }),
		std::string("Non-whitespace characters cannot be added to content."));
}

// (probe: "XDocument Add string" + "string content vs node content
// observability") Consecutive string Adds concatenate into the string
// content; a following node Add materializes the string into an XText.
TEST(XContainerTest, StringContentConcatenationAndMaterialization)
{
	Xml::XDocument document;
	document.Add(std::string("  "));
	document.Add(std::string(" \t "));
	// Still string content: one node after materialization, both parts.
	auto first = document.FirstNode();
	ASSERT_NE(first, nullptr);
	EXPECT_EQ(first->NodeType(), Xml::XmlNodeType::Text);
	EXPECT_EQ(static_cast<Xml::XText*>(first)->Value(), std::string("   \t "));

	// A node Add converts the string content into an XText node first.
	auto comment = std::make_shared<Xml::XComment>("c");
	document.Add(comment);
	std::vector<Xml::XmlNodeType> types;
	for (const auto& node : document.Nodes())
		types.push_back(node->NodeType());
	EXPECT_EQ(types, (std::vector<Xml::XmlNodeType> { Xml::XmlNodeType::Text, Xml::XmlNodeType::Comment }));

	// The materialized node is stable across reads.
	EXPECT_EQ(document.LastNode(), comment.get());
	EXPECT_EQ(document.FirstNode(), first);
}

// (probe: "string content vs node content observability" + "DeepEquals")
// The empty-string-content state is observable through DeepEquals.
TEST(XContainerTest, EmptyStringContentDeepEquals)
{
	Xml::XDocument empty;
	Xml::XDocument withEmptyString;
	withEmptyString.Add(std::string(""));
	EXPECT_FALSE(Xml::XNode::DeepEquals(&empty, &withEmptyString));
	EXPECT_TRUE(Xml::XNode::DeepEquals(&empty, &Xml::XDocument()));

	// A whitespace-string-content document equals one with a materialized
	// XText of the same text.
	Xml::XDocument withString;
	withString.Add(std::string("  "));
	Xml::XDocument withNode;
	withNode.Add(std::make_shared<Xml::XText>("  "));
	EXPECT_TRUE(Xml::XNode::DeepEquals(&withString, &withNode));

	// ...but not one with two text nodes adding up to different text.
	Xml::XDocument withTwoNodes;
	withTwoNodes.Add(std::make_shared<Xml::XText>("  "));
	withTwoNodes.Add(std::make_shared<Xml::XText>(" "));
	EXPECT_FALSE(Xml::XNode::DeepEquals(&withString, &withTwoNodes));
}

// (probe: "XDocument node validation") The document-level content rules.
TEST(XContainerTest, DocumentValidation)
{
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] { Xml::XDocument().Add(std::make_shared<Xml::XCData>("c")); }),
		std::string("A node of type CDATA cannot be added to content."));

	Xml::XDocument document;
	auto documentType = std::make_shared<Xml::XDocumentType>("root", std::nullopt, std::nullopt, std::nullopt);
	document.Add(documentType);
	EXPECT_EQ(document.DocumentType(), documentType.get());

	document.Add(std::make_shared<Xml::XProcessingInstruction>("xml-stylesheet", "x"));
	AddComment(document, "comment");
	document.Add(std::make_shared<Xml::XText>("   "));
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(4));

	// A document type after a comment is still allowed (only a second
	// document type, or one after an element, is rejected).
	Xml::XDocument second;
	AddComment(second, "c");
	auto lateType = std::make_shared<Xml::XDocumentType>("a", std::nullopt, std::nullopt, std::nullopt);
	second.Add(lateType);
	EXPECT_EQ(second.DocumentType(), lateType.get());

	EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] {
		Xml::XDocument two;
		two.Add(std::make_shared<Xml::XDocumentType>("a", std::nullopt, std::nullopt, std::nullopt));
		two.Add(std::make_shared<Xml::XDocumentType>("b", std::nullopt, std::nullopt, std::nullopt));
	}), std::string("This operation would create an incorrectly structured document."));
}

// (probe: "RemoveNodes / ReplaceNodes") Bulk child replacement.
TEST(XContainerTest, RemoveNodesAndReplaceNodes)
{
	Xml::XDocument document;
	AddComment(document, "c1");
	AddComment(document, "c2");
	document.Add(std::string("   "));
	document.RemoveNodes();
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(0));
	EXPECT_EQ(document.FirstNode(), nullptr);

	Xml::XDocument other;
	AddComment(other, "c1");
	AddComment(other, "c2");
	other.ReplaceNodes({ std::make_shared<Xml::XComment>("n1"), std::string("  ") });
	std::vector<std::string> rendered;
	for (const auto& node : other.Nodes())
		rendered.push_back(node->NodeType() == Xml::XmlNodeType::Comment
				? std::static_pointer_cast<Xml::XComment>(node)->Value()
				: std::string("<t>"));
	EXPECT_EQ(rendered, (std::vector<std::string> { "n1", "<t>" }));

	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([] { Xml::XDocument().ReplaceNodes(std::string("x")); }),
		std::string("Non-whitespace characters cannot be added to content."));
}

// (probe: "AddBeforeSelf / AddAfterSelf / Inserter text batching" via
// AddFirst) AddFirst inserts at the front -- through the Inserter with a
// null anchor, including the string-content paths.
TEST(XContainerTest, AddFirst)
{
	Xml::XDocument document;
	auto c1 = AddComment(document, "c1");
	AddComment(document, "c2");
	document.AddFirst(std::make_shared<Xml::XComment>("c0"));
	std::vector<std::string> values;
	for (const auto& node : document.Nodes())
		if (const auto* comment = dynamic_cast<const Xml::XComment*>(node.get()))
			values.push_back(comment->Value());
	EXPECT_EQ(values, (std::vector<std::string> { "c0", "c1", "c2" }));

	// AddFirst with text over an empty document records the string content.
	Xml::XDocument empty;
	empty.AddFirst(std::string("  "));
	EXPECT_EQ(NodeCount(empty), static_cast<std::size_t>(1));

	// AddFirst with text over a node-carrying document inserts a text node at
	// the front.
	Xml::XDocument withNodes;
	AddComment(withNodes, "x");
	withNodes.AddFirst(std::string("  "));
	auto first = withNodes.FirstNode();
	ASSERT_NE(first, nullptr);
	EXPECT_EQ(first->NodeType(), Xml::XmlNodeType::Text);

	// AddFirst with an empty string over an empty document records the empty
	// string content (no nodes).
	Xml::XDocument emptyText;
	emptyText.AddFirst(std::string(""));
	EXPECT_EQ(NodeCount(emptyText), static_cast<std::size_t>(0));
	EXPECT_EQ(emptyText.FirstNode(), nullptr);
	EXPECT_EQ(c1->Value(), std::string("c1"));
}

// (probe: "iteration aborts on remove (load-bearing lazy semantics)") The
// Nodes() sequence stops after the consumer removes the current node -- the
// exact state-machine semantics the AttributeRewritePass do-while depends on.
TEST(XContainerTest, NodesIterationStopsOnRemove)
{
	Xml::XDocument document;
	for (int i = 0; i < 4; ++i)
		AddComment(document, "n" + std::to_string(i));

	int removedCount = 0;
	for (const auto& node : document.Nodes()) {
		node->Remove();
		removedCount++;
	}
	EXPECT_EQ(removedCount, 1);
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(3));
}

// (probe: "iteration aborts on remove" partial iteration) Breaking out of
// the iteration leaves the rest of the tree intact.
TEST(XContainerTest, NodesPartialIteration)
{
	Xml::XDocument document;
	for (int i = 0; i < 3; ++i)
		AddComment(document, "k" + std::to_string(i));

	std::vector<std::string> seen;
	for (const auto& node : document.Nodes()) {
		seen.push_back(std::static_pointer_cast<Xml::XComment>(node)->Value());
		if (seen.size() == 2)
			break;
	}
	EXPECT_EQ(seen, (std::vector<std::string> { "k0", "k1" }));
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(3));
}

// (probe: "iteration aborts on remove" remove-of-last case) Removing a LATER
// sibling during the iteration lets the walk continue to the new last node.
TEST(XContainerTest, NodesIterationWithLaterSiblingRemoval)
{
	Xml::XDocument document;
	auto w1 = AddComment(document, "w1");
	auto w2 = AddComment(document, "w2");
	auto w3 = AddComment(document, "w3");

	std::vector<std::string> seen;
	for (const auto& node : document.Nodes()) {
		seen.push_back(std::static_pointer_cast<Xml::XComment>(node)->Value());
		if (node.get() == w1.get())
			w3->Remove();
	}
	EXPECT_EQ(seen, (std::vector<std::string> { "w1", "w2" }));
	EXPECT_EQ(document.LastNode(), w2.get());
}

// (probe: "copy ctors") The deep-copy constructors.
TEST(XContainerTest, CopyConstructors)
{
	Xml::XDocument document;
	AddComment(document, "c1");
	document.Add(std::make_shared<Xml::XText>("  "));
	AddComment(document, "c2");

	Xml::XDocument copy(document);
	EXPECT_EQ(NodeCount(copy), static_cast<std::size_t>(3));
	EXPECT_NE(copy.FirstNode(), document.FirstNode());
	std::vector<std::string> rendered;
	for (const auto& node : copy.Nodes())
		rendered.push_back(node->NodeType() == Xml::XmlNodeType::Comment
				? std::static_pointer_cast<Xml::XComment>(node)->Value()
				: "<" + std::static_pointer_cast<Xml::XText>(node)->Value() + ">");
	EXPECT_EQ(rendered, (std::vector<std::string> { "c1", "<  >", "c2" }));

	// A string-content document copies the string; the copy materializes on
	// the first read.
	Xml::XDocument stringContent;
	stringContent.Add(std::string("  "));
	Xml::XDocument stringCopy(stringContent);
	EXPECT_EQ(NodeCount(stringCopy), static_cast<std::size_t>(1));
}

// (probe: "XDocument node validation" + Add) The params-collection content
// form flattens nested collections and skips null entries.
TEST(XContainerTest, CollectionContent)
{
	Xml::XDocument document;
	document.Add({ std::make_shared<Xml::XComment>("a"), nullptr, std::string("  "),
		std::vector<Xml::XContent> { std::make_shared<Xml::XComment>("b") } });
	std::vector<std::string> values;
	for (const auto& node : document.Nodes())
		if (const auto* comment = dynamic_cast<const Xml::XComment*>(node.get()))
			values.push_back(comment->Value());
	EXPECT_EQ(values, (std::vector<std::string> { "a", "b" }));
	EXPECT_EQ(NodeCount(document), static_cast<std::size_t>(3));
}
